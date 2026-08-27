/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/cfr.h>
#include <commonlib/helpers.h>
#include <drivers/option/cfr_settings.h>
#include <string.h>

#define CFR_SETTINGS_MAX_POLICY_ENTRIES 64
#define CFR_SETTINGS_MAX_ENUM_VALUES 256
#define CFR_SETTINGS_MAX_FORM_ROOTS 32
#define CFR_SETTINGS_MAX_TREE_DEPTH 8
#define CFR_SETTINGS_MAX_TREE_OBJECTS 512
#define CFR_SETTINGS_MAX_DEPENDENCIES 16
#define CFR_SETTINGS_DEPENDENCY_WORDS ((CFR_SETTINGS_MAX_POLICY_ENTRIES + 31) / 32)

struct effective_dependency {
	const struct sm_object *option;
	const uint32_t *values;
	size_t num_values;
};

struct dependency_path {
	struct effective_dependency entries[CFR_SETTINGS_MAX_DEPENDENCIES];
	size_t num_entries;
};

struct target_path {
	struct dependency_path dependencies;
	uint32_t form_flags;
	size_t occurrences;
	bool malformed;
};

struct tree_frame {
	const struct sm_object *const *objects;
	size_t next;
	size_t saved_dependencies;
	uint32_t form_flags;
};

struct tree_walk_scratch {
	const struct sm_object *target;
	struct target_path result;
	struct dependency_path current_path;
	struct tree_frame frames[CFR_SETTINGS_MAX_TREE_DEPTH];
	size_t depth;
	size_t objects_seen;
};

enum policy_validation_state {
	POLICY_NOT_VALIDATED,
	POLICY_VALID,
	POLICY_INVALID,
};

/* SMM dispatch serializes access to this non-reentrant validation workspace. */
static struct tree_walk_scratch tree_scratch;
static uint32_t dependency_graph[CFR_SETTINGS_MAX_POLICY_ENTRIES]
				[CFR_SETTINGS_DEPENDENCY_WORDS];
static uint16_t dependency_indegree[CFR_SETTINGS_MAX_POLICY_ENTRIES];
static uint16_t dependency_work[CFR_SETTINGS_MAX_POLICY_ENTRIES];
static size_t dependency_queue[CFR_SETTINGS_MAX_POLICY_ENTRIES];
static enum policy_validation_state validation_state;
static const struct cfr_settings_policy *validated_policy;
static size_t validated_policy_entries;

#if ENV_TEST
void cfr_settings_policy_reset_for_test(void)
{
	validation_state = POLICY_NOT_VALIDATED;
	validated_policy = NULL;
	validated_policy_entries = 0;
}
#endif

const struct cfr_settings_policy *__weak
mainboard_cfr_settings_policy(size_t *num_entries)
{
	*num_entries = 0;
	return NULL;
}

const struct sm_object *const *__weak mainboard_cfr_settings_forms(size_t *num_forms)
{
	*num_forms = 0;
	return NULL;
}

enum cb_err __weak mainboard_cfr_settings_validate(uint32_t token,
						   uint32_t old_value,
						   uint32_t new_value)
{
	(void)token;
	(void)old_value;
	(void)new_value;
	return CB_SUCCESS;
}

enum cb_err __weak mainboard_cfr_settings_apply(uint32_t token, uint32_t value)
{
	(void)token;
	(void)value;
	return CB_ERR_NOT_IMPLEMENTED;
}

enum cb_err __weak mainboard_cfr_settings_verify(uint32_t token, uint32_t value)
{
	(void)token;
	(void)value;
	return CB_ERR_NOT_IMPLEMENTED;
}

static uint32_t option_flags(const struct sm_object *option)
{
	switch (option->kind) {
	case SM_OBJ_ENUM:
		return option->sm_enum.flags;
	case SM_OBJ_NUMBER:
		return option->sm_number.flags;
	case SM_OBJ_BOOL:
		return option->sm_bool.flags;
	default:
		return 0;
	}
}

static const char *option_name(const struct sm_object *option)
{
	switch (option->kind) {
	case SM_OBJ_ENUM:
		return option->sm_enum.opt_name;
	case SM_OBJ_NUMBER:
		return option->sm_number.opt_name;
	case SM_OBJ_BOOL:
		return option->sm_bool.opt_name;
	default:
		return NULL;
	}
}

static uint32_t option_default_value(const struct sm_object *option)
{
	switch (option->kind) {
	case SM_OBJ_ENUM:
		return option->sm_enum.default_value;
	case SM_OBJ_NUMBER:
		return option->sm_number.default_value;
	case SM_OBJ_BOOL:
		return option->sm_bool.default_value;
	default:
		return 0;
	}
}

static bool enum_is_well_formed(const struct sm_object *option)
{
	const struct sm_enum_value *values = option->sm_enum.values;

	if (!values)
		return false;

	for (size_t i = 0; i < CFR_SETTINGS_MAX_ENUM_VALUES; i++) {
		if (!values[i].ui_name)
			return i != 0;

		for (size_t j = 0; j < i; j++)
			if (values[j].value == values[i].value)
				return false;
	}

	return false;
}

static bool enum_contains_value(const struct sm_object *option, uint32_t value)
{
	const struct sm_enum_value *values = option->sm_enum.values;

	if (!values)
		return false;

	for (size_t i = 0; i < CFR_SETTINGS_MAX_ENUM_VALUES; i++) {
		if (!values[i].ui_name)
			return false;
		if (values[i].value == value)
			return true;
	}

	return false;
}

static uint32_t number_max(const struct sm_object *option)
{
	if (!option->sm_number.min && !option->sm_number.max)
		return UINT32_MAX;

	return option->sm_number.max;
}

bool cfr_settings_value_is_valid(const struct sm_object *option, uint32_t value)
{
	uint32_t max;

	if (!option)
		return false;

	switch (option->kind) {
	case SM_OBJ_BOOL:
		return value <= 1;
	case SM_OBJ_ENUM:
		return enum_contains_value(option, value);
	case SM_OBJ_NUMBER:
		max = number_max(option);
		if (!option->sm_number.step || value < option->sm_number.min || value > max)
			return false;
		return (value - option->sm_number.min) % option->sm_number.step == 0;
	default:
		return false;
	}
}

static bool dependency_values_are_valid(const struct sm_object *controller,
					const uint32_t *values, size_t num_values)
{
	if (!!values != !!num_values || num_values > CFR_SETTINGS_MAX_ENUM_VALUES)
		return false;
	if (!num_values)
		return true;
	if (!controller || (controller->kind != SM_OBJ_BOOL &&
			    controller->kind != SM_OBJ_ENUM))
		return false;

	for (size_t i = 0; i < num_values; i++) {
		if ((controller->kind == SM_OBJ_BOOL && values[i] > 1) ||
		    (controller->kind == SM_OBJ_ENUM &&
		     !enum_contains_value(controller, values[i])))
			return false;

		for (size_t j = 0; j < i; j++)
			if (values[j] == values[i])
				return false;
	}

	return true;
}

static bool dependency_is_well_formed(const struct sm_object *object)
{
	if (!object->dep)
		return !object->dep_values && !object->num_dep_values;

	if (object->dep == object ||
	    (object->dep->kind != SM_OBJ_BOOL && object->dep->kind != SM_OBJ_ENUM))
		return false;

	return dependency_values_are_valid(object->dep, object->dep_values,
					    object->num_dep_values);
}

static bool option_flags_are_known(uint32_t flags)
{
	const uint32_t known_flags = CFR_OPTFLAG_READONLY | CFR_OPTFLAG_INACTIVE |
		CFR_OPTFLAG_SUPPRESS | CFR_OPTFLAG_VOLATILE | CFR_OPTFLAG_RUNTIME;

	return !(flags & ~known_flags);
}

static bool form_is_well_formed(const struct sm_object *form)
{
	return form && form->kind == SM_OBJ_FORM && !form->ctor &&
		form->sm_form.obj_list && form->sm_form.ui_name &&
		form->sm_form.ui_name[0] && option_flags_are_known(form->sm_form.flags) &&
		dependency_is_well_formed(form);
}

static bool entry_is_well_formed(const struct cfr_settings_policy *entry)
{
	const uint32_t known_flags = CFR_SETTINGS_POLICY_READ |
		CFR_SETTINGS_POLICY_WRITE | CFR_SETTINGS_POLICY_RUNTIME_APPLY;
	const struct sm_object *option = entry->option;
	const char *name;
	uint32_t flags;

	if (!entry->token || !option || entry->flags & ~known_flags ||
	    !(entry->flags & CFR_SETTINGS_POLICY_READ))
		return false;

	if ((entry->flags & CFR_SETTINGS_POLICY_RUNTIME_APPLY) &&
	    !(entry->flags & CFR_SETTINGS_POLICY_WRITE))
		return false;

	if (option->ctor)
		return false;

	if (option->kind != SM_OBJ_BOOL && option->kind != SM_OBJ_ENUM &&
	    option->kind != SM_OBJ_NUMBER)
		return false;

	name = option_name(option);
	flags = option_flags(option);
	if (!name || !name[0] || !option_flags_are_known(flags) ||
	    flags & CFR_OPTFLAG_VOLATILE)
		return false;

	if (entry->flags & CFR_SETTINGS_POLICY_WRITE) {
		if (!(flags & CFR_OPTFLAG_RUNTIME) ||
		    flags & (CFR_OPTFLAG_READONLY | CFR_OPTFLAG_INACTIVE |
			     CFR_OPTFLAG_SUPPRESS))
			return false;
	}

	if (option->kind == SM_OBJ_ENUM && !enum_is_well_formed(option))
		return false;

	if (option->kind == SM_OBJ_NUMBER &&
	    (option->sm_number.min > number_max(option) || !option->sm_number.step))
		return false;
	if (!cfr_settings_value_is_valid(option, option_default_value(option)))
		return false;

	if (!dependency_is_well_formed(option))
		return false;

	return true;
}

static bool path_add_dependency(struct dependency_path *path,
				const struct sm_object *option,
				const uint32_t *values, size_t num_values)
{
	if (!option)
		return !values && !num_values;

	if (path->num_entries >= ARRAY_SIZE(path->entries) ||
	    !dependency_values_are_valid(option, values, num_values))
		return false;

	path->entries[path->num_entries++] = (struct effective_dependency) {
		.option = option,
		.values = values,
		.num_values = num_values,
	};
	return true;
}

static void record_target(const struct sm_object *object, uint32_t form_flags)
{
	if (object != tree_scratch.target)
		return;

	tree_scratch.result.occurrences++;
	if (tree_scratch.result.occurrences != 1)
		return;

	tree_scratch.result.dependencies = tree_scratch.current_path;
	tree_scratch.result.form_flags = form_flags;
	if (!path_add_dependency(&tree_scratch.result.dependencies, object->dep,
				 object->dep_values, object->num_dep_values))
		tree_scratch.result.malformed = true;
}

static bool descend_form(const struct sm_object *form, uint32_t inherited_flags)
{
	struct tree_frame *frame;

	if (!form_is_well_formed(form) ||
	    tree_scratch.depth >= ARRAY_SIZE(tree_scratch.frames))
		return false;

	frame = &tree_scratch.frames[tree_scratch.depth++];
	frame->objects = form->sm_form.obj_list;
	frame->next = 0;
	frame->saved_dependencies = tree_scratch.current_path.num_entries;
	frame->form_flags = inherited_flags | form->sm_form.flags;

	return path_add_dependency(&tree_scratch.current_path, form->dep,
				   form->dep_values, form->num_dep_values);
}

static bool walk_root(const struct sm_object *root)
{
	if (++tree_scratch.objects_seen > CFR_SETTINGS_MAX_TREE_OBJECTS ||
	    !form_is_well_formed(root))
		return false;

	record_target(root, 0);
	if (tree_scratch.result.malformed || !descend_form(root, 0))
		return false;

	while (tree_scratch.depth) {
		struct tree_frame *frame =
			&tree_scratch.frames[tree_scratch.depth - 1];
		const struct sm_object *object = frame->objects[frame->next++];

		if (!object) {
			tree_scratch.current_path.num_entries = frame->saved_dependencies;
			tree_scratch.depth--;
			continue;
		}

		if (++tree_scratch.objects_seen > CFR_SETTINGS_MAX_TREE_OBJECTS)
			return false;

		record_target(object, frame->form_flags);
		if (tree_scratch.result.malformed)
			return false;
		if (object->kind == SM_OBJ_FORM &&
		    !descend_form(object, frame->form_flags))
			return false;
	}

	return true;
}

static const struct target_path *find_target_path(
	const struct sm_object *target, const struct sm_object *const *forms,
	size_t num_forms)
{
	memset(&tree_scratch, 0, sizeof(tree_scratch));
	tree_scratch.target = target;

	for (size_t i = 0; i < num_forms; i++) {
		if (!walk_root(forms[i])) {
			tree_scratch.result.malformed = true;
			break;
		}
	}

	return &tree_scratch.result;
}

static const struct cfr_settings_policy *find_raw_policy_entry(
	const struct cfr_settings_policy *policy, size_t num_entries,
	const struct sm_object *option)
{
	for (size_t i = 0; i < num_entries; i++)
		if (policy[i].option == option)
			return &policy[i];

	return NULL;
}

static bool dependency_graph_is_acyclic(size_t num_entries)
{
	size_t head = 0;
	size_t tail = 0;
	size_t visited = 0;

	memcpy(dependency_work, dependency_indegree, sizeof(dependency_work));
	for (size_t i = 0; i < num_entries; i++)
		if (!dependency_work[i])
			dependency_queue[tail++] = i;

	while (head < tail) {
		const size_t current = dependency_queue[head++];

		visited++;
		for (size_t i = 0; i < num_entries; i++) {
			if (!(dependency_graph[current][i / 32] & BIT(i % 32)))
				continue;
			if (--dependency_work[i] == 0)
				dependency_queue[tail++] = i;
		}
	}

	return visited == num_entries;
}

static bool policy_is_well_formed(const struct cfr_settings_policy *policy,
				  size_t num_entries)
{
	size_t num_forms;
	const struct sm_object *const *forms = mainboard_cfr_settings_forms(&num_forms);

	if (!policy || !num_entries || num_entries > CFR_SETTINGS_MAX_POLICY_ENTRIES ||
	    !forms || !num_forms || num_forms > CFR_SETTINGS_MAX_FORM_ROOTS)
		return false;

	memset(dependency_graph, 0, sizeof(dependency_graph));
	memset(dependency_indegree, 0, sizeof(dependency_indegree));

	for (size_t i = 0; i < num_entries; i++) {
		const char *name = option_name(policy[i].option);

		if (!entry_is_well_formed(&policy[i]))
			return false;

		for (size_t j = 0; j < i; j++)
			if (policy[j].token == policy[i].token ||
			    policy[j].option == policy[i].option ||
			    !strcmp(option_name(policy[j].option), name))
				return false;
	}

	for (size_t i = 0; i < num_entries; i++) {
		const struct target_path *path =
			find_target_path(policy[i].option, forms, num_forms);
		if (path->malformed || path->occurrences != 1)
			return false;

		if ((policy[i].flags & CFR_SETTINGS_POLICY_WRITE) &&
		    path->form_flags & (CFR_OPTFLAG_READONLY | CFR_OPTFLAG_INACTIVE |
					CFR_OPTFLAG_SUPPRESS | CFR_OPTFLAG_VOLATILE))
			return false;

		for (size_t j = 0; j < path->dependencies.num_entries; j++) {
			const struct cfr_settings_policy *controller =
				find_raw_policy_entry(policy, num_entries,
						      path->dependencies.entries[j].option);
			size_t controller_index;
			uint32_t controller_bit;

			if (!controller || !(controller->flags & CFR_SETTINGS_POLICY_READ))
				return false;

			controller_index = controller - policy;
			controller_bit = BIT(controller_index % 32);
			if (!(dependency_graph[i][controller_index / 32] & controller_bit)) {
				dependency_graph[i][controller_index / 32] |= controller_bit;
				dependency_indegree[controller_index]++;
			}
		}
	}

	return dependency_graph_is_acyclic(num_entries);
}

static const struct cfr_settings_policy *get_policy(size_t *num_entries)
{
	size_t current_entries;
	const struct cfr_settings_policy *policy =
		mainboard_cfr_settings_policy(&current_entries);

	if (validation_state == POLICY_NOT_VALIDATED) {
		validated_policy = policy;
		validated_policy_entries = current_entries;
		validation_state = policy_is_well_formed(policy, current_entries) ?
			POLICY_VALID : POLICY_INVALID;
	}

	if (validation_state != POLICY_VALID || policy != validated_policy ||
	    current_entries != validated_policy_entries) {
		*num_entries = 0;
		return NULL;
	}

	*num_entries = validated_policy_entries;
	return validated_policy;
}

const struct cfr_settings_policy *cfr_settings_policy_for_token(uint32_t token)
{
	size_t num_entries;
	const struct cfr_settings_policy *policy = get_policy(&num_entries);

	for (size_t i = 0; i < num_entries; i++)
		if (policy[i].token == token)
			return &policy[i];

	return NULL;
}

const struct cfr_settings_policy *cfr_settings_policy_for_option(
	const struct sm_object *option)
{
	size_t num_entries;
	const struct cfr_settings_policy *policy = get_policy(&num_entries);

	return find_raw_policy_entry(policy, num_entries, option);
}

bool cfr_settings_policy_can_write(const struct cfr_settings_policy *policy)
{
	return policy && (policy->flags & CFR_SETTINGS_POLICY_WRITE);
}

static bool dependency_value_matches(const struct effective_dependency *dependency,
				     uint32_t value)
{
	if (!dependency->num_values)
		return value != 0;

	for (size_t i = 0; i < dependency->num_values; i++)
		if (dependency->values[i] == value)
			return true;

	return false;
}

bool cfr_settings_dependencies_satisfied(const struct cfr_settings_policy *entry,
					 cfr_settings_read_fn read_option)
{
	size_t head = 0;
	size_t tail = 0;
	size_t num_entries;
	size_t num_forms;
	const struct cfr_settings_policy *policy = get_policy(&num_entries);
	const struct sm_object *const *forms = mainboard_cfr_settings_forms(&num_forms);
	const struct cfr_settings_policy *validated_entry;

	if (!entry || !read_option || !policy)
		return false;

	validated_entry = find_raw_policy_entry(policy, num_entries, entry->option);
	if (!validated_entry || validated_entry->token != entry->token)
		return false;

	memset(dependency_work, 0, sizeof(dependency_work));
	dependency_queue[tail++] = validated_entry - policy;
	dependency_work[validated_entry - policy] = 1;

	while (head < tail) {
		const size_t current = dependency_queue[head++];
		const struct target_path *path =
			find_target_path(policy[current].option, forms, num_forms);

		if (path->malformed || path->occurrences != 1)
			return false;

		for (size_t i = 0; i < path->dependencies.num_entries; i++) {
			const struct cfr_settings_policy *controller =
				find_raw_policy_entry(policy, num_entries,
						      path->dependencies.entries[i].option);
			size_t controller_index;
			uint32_t value;

			if (!controller ||
			    read_option(controller->option, &value) != CB_SUCCESS ||
			    !dependency_value_matches(&path->dependencies.entries[i], value))
				return false;

			controller_index = controller - policy;
			if (!dependency_work[controller_index]) {
				dependency_work[controller_index] = 1;
				dependency_queue[tail++] = controller_index;
			}
		}
	}

	return true;
}
