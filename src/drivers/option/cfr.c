/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/coreboot_tables.h>
#include <commonlib/coreboot_tables.h>
#include <console/console.h>
#include <crc_byte.h>
#include <drivers/option/cfr_frontend.h>
#include <drivers/option/cfr_settings.h>
#include <inttypes.h>
#include <string.h>
#include <types.h>

/* Global override table registered by mainboard */
static const struct cfr_default_override *mb_overrides = NULL;
static size_t cfr_serialization_limit;
static uintptr_t cfr_write_end;
static bool cfr_write_failed;

void cfr_set_serialization_limit(size_t max_size)
{
	cfr_serialization_limit = max_size;
}

static bool cfr_reserve(const void *current, size_t size)
{
	const uintptr_t address = (uintptr_t)current;

	if (cfr_write_failed || address > cfr_write_end || size > cfr_write_end - address) {
		cfr_write_failed = true;
		return false;
	}

	return true;
}

void cfr_register_overrides(const struct cfr_default_override *overrides)
{
	mb_overrides = overrides;
}

/* Look up override for a given option name */
static const struct cfr_default_override *find_override(const char *opt_name)
{
	if (!mb_overrides || !opt_name)
		return NULL;

	for (const struct cfr_default_override *ovr = mb_overrides; ovr->opt_name; ovr++) {
		if (strcmp(ovr->opt_name, opt_name) == 0)
			return ovr;
	}
	return NULL;
}

static uint32_t cfr_record_size(const char *startp, const char *endp)
{
	const uintptr_t start = (uintptr_t)startp;
	const uintptr_t end = (uintptr_t)endp;

	if (start > end || end - start > UINT32_MAX) {
		/*
		 * Should never be reached unless something went really
		 * wrong. Record size can never be negative, and things
		 * would break long before record length exceeds 4 GiB.
		 */
		die("%s: bad record size (start = %" PRIxPTR ", end = %" PRIxPTR ")",
			__func__, start, end);
	}
	return (uint32_t)(end - start);
}

static uint32_t write_cfr_varchar(char *current, const char *string, uint32_t tag)
{
	size_t data_length;
	size_t record_size;

	ASSERT(string);
	if (!string)
		return 0;
	data_length = strlen(string) + 1;
	if (data_length > UINT32_MAX - sizeof(struct lb_cfr_varbinary) -
				  (LB_ENTRY_ALIGN - 1)) {
		cfr_write_failed = true;
		return 0;
	}
	record_size = ALIGN_UP(sizeof(struct lb_cfr_varbinary) + data_length, LB_ENTRY_ALIGN);
	if (record_size > UINT32_MAX || !cfr_reserve(current, record_size))
		return 0;

	struct lb_cfr_varbinary *cfr_str = (struct lb_cfr_varbinary *)current;
	cfr_str->tag = tag;
	cfr_str->data_length = data_length;
	char *data = current + sizeof(*cfr_str);
	memcpy(data, string, cfr_str->data_length);

	/* Make sure that every TAG/SIZE field is always aligned to LB_ENTRY_ALIGN */
	cfr_str->size = record_size;

	return cfr_str->size;
}

static uint32_t sm_write_string_default_value(char *current, const char *string)
{
	return write_cfr_varchar(current, string ? string : "", CFR_TAG_VARCHAR_DEF_VALUE);
}

static uint32_t sm_write_opt_name(char *current, const char *string)
{
	return write_cfr_varchar(current, string, CFR_TAG_VARCHAR_OPT_NAME);
}

static uint32_t sm_write_ui_name(char *current, const char *string)
{
	return write_cfr_varchar(current, string, CFR_TAG_VARCHAR_UI_NAME);
}

static uint32_t sm_write_ui_helptext(char *current, const char *string)
{
	/* UI Helptext is optional, return if nothing to display */
	if (!string || !strlen(string))
		return 0;

	return write_cfr_varchar(current, string, CFR_TAG_VARCHAR_UI_HELPTEXT);
}

static uint32_t sm_write_dep_values(char *current,
				    const uint32_t *dep_values, const uint32_t num_dep_values)
{
	/* Dependency values are optional */
	if (!dep_values || !num_dep_values)
		return 0;
	if (num_dep_values > UINT32_MAX / sizeof(*dep_values)) {
		cfr_write_failed = true;
		return 0;
	}
	const size_t data_length = sizeof(*dep_values) * num_dep_values;
	if (data_length > UINT32_MAX - sizeof(struct lb_cfr_varbinary) -
				  (LB_ENTRY_ALIGN - 1)) {
		cfr_write_failed = true;
		return 0;
	}
	const size_t record_size = ALIGN_UP(sizeof(struct lb_cfr_varbinary) + data_length,
					    LB_ENTRY_ALIGN);
	if (record_size > UINT32_MAX || !cfr_reserve(current, record_size))
		return 0;

	struct lb_cfr_varbinary *cfr_values = (struct lb_cfr_varbinary *)current;
	cfr_values->tag = CFR_TAG_DEP_VALUES;
	cfr_values->data_length = data_length;
	char *data = current + sizeof(*cfr_values);
	memcpy(data, dep_values, cfr_values->data_length);

	/* Make sure that every TAG/SIZE field is always aligned to LB_ENTRY_ALIGN */
	cfr_values->size = record_size;

	return cfr_values->size;
}

static bool runtime_apply_apm_cnt_supported(void)
{
	return CONFIG(DRIVERS_OPTION_CFR_RUNTIME_APPLY) &&
	       (CONFIG(SOC_INTEL_COMMON_BLOCK_SMM) ||
		CONFIG(SOC_AMD_COMMON_BLOCK_SMIHANDLER));
}

static uint32_t sm_write_runtime_apply(char *current, uint32_t flags,
				       const struct sm_runtime_apply *sm_runtime_apply)
{
	if (!(flags & CFR_OPTFLAG_RUNTIME) ||
	    sm_runtime_apply->method == CFR_RUNTIME_APPLY_NONE)
		return 0;

	switch (sm_runtime_apply->method) {
	case CFR_RUNTIME_APPLY_APM_CNT:
		if (!runtime_apply_apm_cnt_supported()) {
			printk(BIOS_ERR, "CFR: APM runtime apply requires common SMM handler support\n");
			return 0;
		}
		if (sm_runtime_apply->id > UINT8_MAX) {
			printk(BIOS_ERR, "CFR: APM runtime apply ID %#x exceeds APM_STS\n",
			       sm_runtime_apply->id);
			return 0;
		}
		break;
	case CFR_RUNTIME_APPLY_ACPI:
		break;
	default:
		printk(BIOS_ERR, "CFR: unsupported runtime apply method %#x\n",
		       sm_runtime_apply->method);
		return 0;
	}

	struct lb_cfr_runtime_apply *runtime_apply = (struct lb_cfr_runtime_apply *)current;

	runtime_apply->tag = CFR_TAG_RUNTIME_APPLY;
	runtime_apply->size = ALIGN_UP(sizeof(*runtime_apply), LB_ENTRY_ALIGN);
	runtime_apply->method = sm_runtime_apply->method;
	runtime_apply->id = sm_runtime_apply->id;

	return runtime_apply->size;
}

static uint32_t sm_write_enum_value(char *current, const struct sm_enum_value *e)
{
	struct lb_cfr_enum_value *enum_val = (struct lb_cfr_enum_value *)current;

	if (!cfr_reserve(current, sizeof(*enum_val)))
		return 0;
	enum_val->tag = CFR_TAG_ENUM_VALUE;
	enum_val->value = e->value;
	enum_val->size = sizeof(*enum_val);

	current += enum_val->size;
	current += sm_write_ui_name(current, e->ui_name);

	enum_val->size = cfr_record_size((char *)enum_val, current);
	return enum_val->size;
}

static uint32_t sm_write_option_access(char *current, uint32_t token,
				       uint32_t permissions)
{
	struct lb_cfr_option_access *access = (struct lb_cfr_option_access *)current;

	if (!token || permissions & ~CFR_SETTINGS_ACCESS_PERMISSIONS_MASK ||
	    !(permissions & CFR_SETTINGS_ACCESS_READ) ||
	    ((permissions & CFR_SETTINGS_ACCESS_WRITE) &&
	     !(permissions & CFR_SETTINGS_ACCESS_READ)))
		return 0;
	if (!cfr_reserve(current, sizeof(*access)))
		return 0;

	access->tag = CFR_TAG_OPTION_ACCESS;
	access->size = sizeof(*access);
	access->version = CFR_OPTION_ACCESS_VERSION;
	access->token = token;
	access->permissions = permissions;
	access->reserved = 0;

	return access->size;
}

static bool override_matches_numeric_tag(enum sm_object_kind override_kind, uint32_t tag)
{
	switch (tag) {
	case CFR_TAG_OPTION_ENUM:
		return override_kind == SM_OBJ_ENUM;
	case CFR_TAG_OPTION_NUMBER:
		return override_kind == SM_OBJ_NUMBER;
	case CFR_TAG_OPTION_BOOL:
		return override_kind == SM_OBJ_BOOL;
	default:
		return false;
	}
}

static uint32_t write_numeric_option(char *current, uint32_t tag, const uint64_t object_id,
		const char *opt_name, const char *ui_name, const char *ui_helptext,
		uint32_t flags, uint32_t default_value, uint32_t min, uint32_t max, uint32_t step,
		uint32_t display_flags, const struct sm_enum_value *values,
		const struct sm_runtime_apply *runtime_apply, const uint64_t dep_id,
		const uint32_t *dep_values, const uint32_t num_dep_values,
		uint32_t access_token, uint32_t access_permissions, bool publish_access)
{
	struct lb_cfr_numeric_option *option = (struct lb_cfr_numeric_option *)current;
	size_t len;

	if (!cfr_reserve(current, sizeof(*option)))
		return 0;

	/* Check for mainboard override of default value */
	const struct cfr_default_override *ovr = find_override(opt_name);
	if (ovr) {
		if (publish_access)
			printk(BIOS_WARNING,
			       "CFR: ignoring default override for settings-service option '%s'.\n",
			       opt_name);
		else if (!override_matches_numeric_tag(ovr->kind, tag))
			printk(BIOS_WARNING, "CFR: override for option '%s' has mismatched type; skipping.\n", opt_name);
		else
			default_value = ovr->uint_value;
	}

	option->tag = tag;
	option->object_id = object_id;
	option->dependency_id = dep_id;
	option->flags = flags;
	if (option->flags & (CFR_OPTFLAG_INACTIVE | CFR_OPTFLAG_VOLATILE))
		option->flags |= CFR_OPTFLAG_READONLY;
	option->default_value = default_value;
	option->min = (min <= max) ? min : 0;
	option->max = (min == 0 && max == 0) ? UINT32_MAX : max;
	option->step = step;
	option->display_flags = display_flags;
	option->size = sizeof(*option);

	current += option->size;
	len = sm_write_opt_name(current, opt_name);
	if (!len)
		return 0;
	current += len;
	len = sm_write_ui_name(current, ui_name);
	if (!len)
		return 0;
	current += len;
	current += sm_write_ui_helptext(current, ui_helptext);
	current += sm_write_dep_values(current, dep_values, num_dep_values);
	current += sm_write_runtime_apply(current, flags, runtime_apply);

	if (option->tag == CFR_TAG_OPTION_ENUM && values) {
		for (const struct sm_enum_value *e = values; e->ui_name; e++) {
			current += sm_write_enum_value(current, e);
		}
	}
	current += sm_write_option_access(current, access_token, access_permissions);

	option->size = cfr_record_size((char *)option, current);
	return option->size;
}

static uint32_t sm_write_opt_enum(char *current, const struct sm_obj_enum *sm_enum,
				  const uint64_t object_id, const uint64_t dep_id,
				  const uint32_t *dep_values, const uint32_t num_dep_values,
				  uint32_t access_token, uint32_t access_permissions,
				  bool publish_access)

{
	return write_numeric_option(current, CFR_TAG_OPTION_ENUM, object_id,
			sm_enum->opt_name, sm_enum->ui_name, sm_enum->ui_helptext,
			sm_enum->flags, sm_enum->default_value, 0, 0, 0, 0, sm_enum->values,
			&sm_enum->runtime_apply, dep_id, dep_values, num_dep_values,
			access_token, access_permissions, publish_access);
}

static uint32_t sm_write_opt_number(char *current, const struct sm_obj_number *sm_number,
				    const uint64_t object_id, const uint64_t dep_id,
				    const uint32_t *dep_values, const uint32_t num_dep_values,
				    uint32_t access_token, uint32_t access_permissions,
				    bool publish_access)

{
	return write_numeric_option(current, CFR_TAG_OPTION_NUMBER, object_id,
			sm_number->opt_name, sm_number->ui_name, sm_number->ui_helptext,
			sm_number->flags, sm_number->default_value, sm_number->min, sm_number->max,
			sm_number->step, sm_number->display_flags, NULL, &sm_number->runtime_apply,
			dep_id, dep_values, num_dep_values, access_token, access_permissions,
			publish_access);
}

static uint32_t sm_write_opt_bool(char *current, const struct sm_obj_bool *sm_bool,
				  const uint64_t object_id, const uint64_t dep_id,
				  const uint32_t *dep_values, const uint32_t num_dep_values,
				  uint32_t access_token, uint32_t access_permissions,
				  bool publish_access)

{
	return write_numeric_option(current, CFR_TAG_OPTION_BOOL, object_id,
			sm_bool->opt_name, sm_bool->ui_name, sm_bool->ui_helptext,
			sm_bool->flags, sm_bool->default_value, 0, 0, 0, 0, NULL,
			&sm_bool->runtime_apply, dep_id,
			dep_values, num_dep_values, access_token, access_permissions,
			publish_access);
}

static uint32_t sm_write_opt_varchar(char *current, const struct sm_obj_varchar *sm_varchar,
				     const uint64_t object_id, const uint64_t dep_id,
				     const uint32_t *dep_values, const uint32_t num_dep_values,
				     bool publish_access)

{
	struct lb_cfr_varchar_option *option = (struct lb_cfr_varchar_option *)current;
	size_t len;
	const char *default_value = sm_varchar->default_value;

	if (!cfr_reserve(current, sizeof(*option)))
		return 0;

	/* Check for mainboard override of default value */
	const struct cfr_default_override *ovr = find_override(sm_varchar->opt_name);
	if (ovr) {
		if (publish_access)
			printk(BIOS_WARNING,
			       "CFR: ignoring default override for settings-service option '%s'.\n",
			       sm_varchar->opt_name);
		else if (ovr->kind == SM_OBJ_VARCHAR)
			default_value = ovr->str_value;
		else
			printk(BIOS_WARNING, "CFR: override for option '%s' has mismatched type (not varchar); skipping.\n", sm_varchar->opt_name);
	}

	option->tag = CFR_TAG_OPTION_VARCHAR;
	option->object_id = object_id;
	option->dependency_id = dep_id;
	option->flags = sm_varchar->flags;
	if (option->flags & (CFR_OPTFLAG_INACTIVE | CFR_OPTFLAG_VOLATILE))
		option->flags |= CFR_OPTFLAG_READONLY;
	option->size = sizeof(*option);

	current += option->size;
	current += sm_write_string_default_value(current, default_value);
	len = sm_write_opt_name(current, sm_varchar->opt_name);
	if (!len)
		return 0;
	current += len;
	len = sm_write_ui_name(current, sm_varchar->ui_name);
	if (!len)
		return 0;
	current += len;
	current += sm_write_ui_helptext(current, sm_varchar->ui_helptext);
	current += sm_write_dep_values(current, dep_values, num_dep_values);

	option->size = cfr_record_size((char *)option, current);
	return option->size;
}

static uint32_t sm_write_opt_comment(char *current, const struct sm_obj_comment *sm_comment,
				     const uint32_t object_id, const uint32_t dep_id,
				     const uint32_t *dep_values, const uint32_t num_dep_values)
{
	struct lb_cfr_option_comment *comment = (struct lb_cfr_option_comment *)current;
	size_t len;

	if (!cfr_reserve(current, sizeof(*comment)))
		return 0;

	comment->tag = CFR_TAG_OPTION_COMMENT;
	comment->object_id = object_id;
	comment->dependency_id = dep_id;
	comment->flags = sm_comment->flags;
	if (comment->flags & (CFR_OPTFLAG_INACTIVE | CFR_OPTFLAG_VOLATILE))
		comment->flags |= CFR_OPTFLAG_READONLY;
	comment->size = sizeof(*comment);

	current += comment->size;
	len = sm_write_ui_name(current, sm_comment->ui_name);
	if (!len)
		return 0;
	current += len;
	current += sm_write_ui_helptext(current, sm_comment->ui_helptext);
	current += sm_write_dep_values(current, dep_values, num_dep_values);

	comment->size = cfr_record_size((char *)comment, current);
	return comment->size;
}

static uint64_t sm_gen_obj_id(void *ptr)
{
	uintptr_t id = (uintptr_t)ptr;
	/* Convert pointer to unique ID */
	return id ^ 0xffffcafecafecafe;
}

static uint32_t sm_write_object(char *current, const struct sm_object *sm_obj,
				bool publish_access);

static uint32_t sm_write_form(char *current, const struct sm_obj_form *sm_form,
			      const uint64_t object_id, const uint64_t dep_id,
			      const uint32_t *dep_values, const uint32_t num_dep_values,
			      bool publish_access)
{
	struct lb_cfr_option_form *form = (struct lb_cfr_option_form *)current;
	size_t len;
	size_t i = 0;

	if (!cfr_reserve(current, sizeof(*form)))
		return 0;

	form->tag = CFR_TAG_OPTION_FORM;
	form->object_id = object_id;
	form->dependency_id = dep_id;
	form->flags = sm_form->flags;
	if (form->flags & (CFR_OPTFLAG_INACTIVE | CFR_OPTFLAG_VOLATILE))
		form->flags |= CFR_OPTFLAG_READONLY;
	form->size = sizeof(*form);

	current += form->size;
	len = sm_write_ui_name(current, sm_form->ui_name);
	if (!len)
		return 0;
	current += len;
	current += sm_write_dep_values(current, dep_values, num_dep_values);

	while (sm_form->obj_list[i])
		current += sm_write_object(current, sm_form->obj_list[i++], publish_access);

	form->size = cfr_record_size((char *)form, current);
	return form->size;
}

static uint32_t sm_write_object(char *current, const struct sm_object *sm_obj,
				bool publish_access)
{
	uint64_t dep_id, obj_id;
	uint32_t access_token = 0;
	uint32_t access_permissions = 0;
	const uint32_t *dep_values;
	uint32_t num_dep_values;
	assert(sm_obj);
	struct sm_object sm_obj_copy = *sm_obj;
	const struct cfr_settings_policy *policy;

	if (CONFIG(DRIVERS_OPTION_CFR_SMM) && publish_access) {
		policy = cfr_settings_policy_for_option(sm_obj);
		if (policy)
			access_token = policy->token;
		if (policy)
			access_permissions = policy->flags &
				CFR_SETTINGS_ACCESS_PERMISSIONS_MASK;
	}

	/* Assign uniqueue ID */
	obj_id = sm_gen_obj_id((void *)sm_obj);

	/* Set dependency ID */
	dep_id = 0;
	dep_values = NULL;
	num_dep_values = 0;
	if (sm_obj->dep) {
		if (sm_obj->dep->kind == SM_OBJ_BOOL || sm_obj->dep->kind == SM_OBJ_ENUM) {
			dep_id = sm_gen_obj_id((void *)sm_obj->dep);
			dep_values = sm_obj->dep_values;
			num_dep_values = sm_obj->num_dep_values;
		}
	}

	/* Invoke callback to update fields */
	if (sm_obj->ctor) {
		sm_obj->ctor(&sm_obj_copy);
		assert(sm_obj->kind == sm_obj_copy.kind);
	}

	switch (sm_obj_copy.kind) {
	case SM_OBJ_NONE:
		return 0;
	case SM_OBJ_ENUM:
		return sm_write_opt_enum(current, &sm_obj_copy.sm_enum, obj_id,
					 dep_id, dep_values, num_dep_values, access_token,
					 access_permissions, publish_access);
	case SM_OBJ_NUMBER:
		return sm_write_opt_number(current, &sm_obj_copy.sm_number, obj_id,
					   dep_id, dep_values, num_dep_values, access_token,
					   access_permissions, publish_access);
	case SM_OBJ_BOOL:
		return sm_write_opt_bool(current, &sm_obj_copy.sm_bool, obj_id,
					 dep_id, dep_values, num_dep_values, access_token,
					 access_permissions, publish_access);
	case SM_OBJ_VARCHAR:
		return sm_write_opt_varchar(current, &sm_obj_copy.sm_varchar, obj_id,
					    dep_id, dep_values, num_dep_values, publish_access);
	case SM_OBJ_COMMENT:
		return sm_write_opt_comment(current, &sm_obj_copy.sm_comment, obj_id,
					    dep_id, dep_values, num_dep_values);
	case SM_OBJ_FORM:
		return sm_write_form(current, &sm_obj_copy.sm_form, obj_id, dep_id,
				     dep_values, num_dep_values, publish_access);
	default:
		BUG();
		printk(BIOS_ERR, "Unknown setup menu object kind %u, ignoring\n", sm_obj_copy.kind);
		return 0;
	}
}

void cfr_write_setup_menu(struct lb_cfr *cfr_root, struct sm_obj_form *sm_root[])
{
	const struct sm_object *const *settings_forms = NULL;
	size_t num_settings_forms = 0;
	void *current = cfr_root;
	struct sm_obj_form *obj;
	size_t i = 0;

	ASSERT(cfr_root);
	if (!cfr_root)
		return;
	if (cfr_serialization_limit > UINTPTR_MAX - (uintptr_t)cfr_root) {
		cfr_write_failed = true;
		return;
	}
	cfr_write_end = (uintptr_t)cfr_root + cfr_serialization_limit;
	cfr_write_failed = false;
	if (!cfr_reserve(cfr_root, sizeof(*cfr_root)))
		return;

	cfr_root->tag = LB_TAG_CFR_ROOT;
	cfr_root->size = sizeof(*cfr_root);
	cfr_root->version = CFR_VERSION;

	current += cfr_root->size;
	if (CONFIG(DRIVERS_OPTION_CFR_SMM)) {
		settings_forms = cfr_settings_policy_forms(&num_settings_forms);
		for (i = 0; settings_forms && i < num_settings_forms; i++) {
			if (!settings_forms[i] || settings_forms[i]->kind != SM_OBJ_FORM) {
				printk(BIOS_ERR, "CFR settings root %zu is not a form, ignoring\n", i);
				continue;
			}

			current += sm_write_object(current, settings_forms[i], true);
		}
	} else {
		while (sm_root && sm_root[i])
			current += sm_write_form(current, sm_root[i++], 0, 0, NULL, 0, false);
	}

	if (!CONFIG(DRIVERS_OPTION_CFR_SMM)) {
		/* Add generic forms to the legacy, non-service tree. */
		for (obj = &_cfr_forms[0]; obj != &_ecfr_forms[0]; obj++)
			current += sm_write_form(current, obj, 0, 0, NULL, 0, false);
	}

	if (cfr_write_failed) {
		printk(BIOS_ERR, "CFR: setup menu exceeds coreboot table capacity\n");
		cfr_root->size = sizeof(*cfr_root);
		cfr_root->checksum = 0;
		return;
	}

	cfr_root->size = cfr_record_size((char *)cfr_root, current);

	cfr_root->checksum = CRC(cfr_root + 1, cfr_root->size - sizeof(*cfr_root), crc32_byte);

	printk(BIOS_DEBUG, "CFR: Written %u bytes of CFR structures at %p, with CRC32 0x%08x\n",
		cfr_root->size, cfr_root, cfr_root->checksum);
}
