/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef DRIVERS_OPTION_CFR_H
#define DRIVERS_OPTION_CFR_H

#include <commonlib/coreboot_tables.h>
#include <types.h>

enum cfr_option_flags {
	CFR_OPTFLAG_READONLY	= 1 << 0,
	CFR_OPTFLAG_GRAYOUT	= 1 << 1,
	CFR_OPTFLAG_SUPPRESS	= 1 << 2,
	CFR_OPTFLAG_VOLATILE	= 1 << 3,
};

/* Front-end */
struct sm_enum_value {
	const char *ui_name;
	uint32_t value;
};

#define SM_ENUM_VALUE_END	((struct sm_enum_value) {0})

struct sm_obj_enum {
	uint32_t object_id;
	uint32_t flags;		/* enum cfr_option_flags */
	const char *opt_name;
	const char *ui_name;
	const char *ui_helptext;
	uint32_t default_value;
	const struct sm_enum_value *values;
};

struct sm_obj_number {
	uint32_t object_id;
	uint32_t flags;		/* enum cfr_option_flags */
	const char *opt_name;
	const char *ui_name;
	const char *ui_helptext;
	uint32_t default_value;
};

struct sm_obj_bool {
	uint32_t object_id;
	uint32_t flags;		/* enum cfr_option_flags */
	const char *opt_name;
	const char *ui_name;
	const char *ui_helptext;
	bool default_value;
};

struct sm_obj_varchar {
	uint32_t object_id;
	uint32_t flags;		/* enum cfr_option_flags */
	const char *opt_name;
	const char *ui_name;
	const char *ui_helptext;
	const char *default_value;
};

struct sm_obj_comment {
	uint32_t object_id;
	uint32_t flags;		/* enum cfr_option_flags */
	const char *ui_name;
	const char *ui_helptext;
};

struct sm_object;

struct sm_obj_form {
	uint32_t object_id;
	uint32_t flags;		/* enum cfr_option_flags */
	const char *ui_name;
	const struct sm_object *obj_list;
	size_t num_objects;
};

enum sm_object_kind {
	SM_OBJ_NONE = 0,
	SM_OBJ_ENUM,
	SM_OBJ_NUMBER,
	SM_OBJ_BOOL,
	SM_OBJ_VARCHAR,
	SM_OBJ_COMMENT,
	SM_OBJ_FORM,
};

struct sm_object {
	enum sm_object_kind kind;
	union {
		const struct sm_obj_enum sm_enum;
		const struct sm_obj_number sm_number;
		const struct sm_obj_bool sm_bool;
		const struct sm_obj_varchar sm_varchar;
		const struct sm_obj_comment sm_comment;
		const struct sm_obj_form sm_form;
	};
};

/* sm_object helpers with type checking */
#define SM_DECLARE_ENUM(structure)	{ .kind = SM_OBJ_ENUM,    .sm_enum    = (structure) }
#define SM_DECLARE_NUMBER(structure)	{ .kind = SM_OBJ_NUMBER,  .sm_number  = (structure) }
#define SM_DECLARE_BOOL(structure)	{ .kind = SM_OBJ_BOOL,    .sm_bool    = (structure) }
#define SM_DECLARE_VARCHAR(structure)	{ .kind = SM_OBJ_VARCHAR, .sm_varchar = (structure) }
#define SM_DECLARE_COMMENT(structure)	{ .kind = SM_OBJ_COMMENT, .sm_comment = (structure) }
#define SM_DECLARE_FORM(structure)	{ .kind = SM_OBJ_FORM,    .sm_form    = (structure) }

struct setup_menu_root {
	const struct sm_obj_form *form_list;
	size_t num_forms;
};

void cfr_write_setup_menu(struct lb_header *header, const struct setup_menu_root *sm_root);

/* Back-end */
struct lb_cfr_varbinary {
	uint32_t tag;		/* Any CFR_VARBINARY or CFR_VARCHAR */
	uint32_t size;		/* Length of the entire structure */
	uint32_t data_length;	/* Length of data, including NULL terminator for strings */
	uint8_t data[];
};

/*
 * CFR records form a tree structure. The size of a record includes
 * the size of its own fields plus the size of all children records.
 * CFR tags can appear multiple times except for `LB_TAG_CFR` which
 * is used for the root record.
 *
 * The following structures have comments that describe the supported
 * children records. These comments cannot be replaced with code! The
 * structures are variable-length, so the offsets won't be valid most
 * of the time. Besides, the implementation uses `sizeof()` to obtain
 * the size of the "record header" (the fixed-length members); adding
 * the children structures as struct members will increase the length
 * returned by `sizeof()`, which complicates things for zero reason.
 *
 * TODO: This should be documentation instead.
 */
struct lb_cfr_enum_value {
	uint32_t tag;
	uint32_t size;
	uint32_t value;
	/*
	 * CFR_UI_NAME ui_name
	 */
};

/* Supports multiple option types: ENUM, NUMBER, BOOL */
struct lb_cfr_numeric_option {
	uint32_t tag;		/* CFR_OPTION_ENUM, CFR_OPTION_NUMBER, CFR_OPTION_BOOL */
	uint32_t size;
	uint32_t object_id;
	uint32_t flags;		/* enum cfr_option_flags */
	uint32_t default_value;
	/*
	 * CFR_VARCHAR_OPT_NAME		opt_name
	 * CFR_VARCHAR_UI_NAME		ui_name
	 * CFR_VARCHAR_UI_HELPTEXT	ui_helptext (Optional)
	 * CFR_ENUM_VALUE		enum_values[]
	 */
};

struct lb_cfr_varchar_option {
	uint32_t tag;		/* CFR_OPTION_VARCHAR */
	uint32_t size;
	uint32_t object_id;
	uint32_t flags;		/* enum cfr_option_flags */
	/*
	 * CFR_VARCHAR		default_value
	 * CFR_OPT_NAME		opt_name
	 * CFR_UI_NAME		ui_name
	 * CFR_UI_HELPTEXT	ui_helptext (Optional)
	 */
};

/*
 * A CFR option comment is roughly equivalent to a Kconfig comment.
 * Option comments are *NOT* string options (see CFR_OPTION_VARCHAR
 * instead) but they're considered an option for simplicity's sake.
 */
struct lb_cfr_option_comment {
	uint32_t tag;		/* CFR_OPTION_COMMENT */
	uint32_t size;
	uint32_t object_id;
	uint32_t flags;		/* enum cfr_option_flags */
	/*
	 * CFR_UI_NAME		ui_name
	 * CFR_UI_HELPTEXT	ui_helptext (Optional)
	 */
};

/* CFR forms are considered options as they can be nested inside other forms */
struct lb_cfr_option_form {
	uint32_t tag;		/* CFR_OPTION_FORM */
	uint32_t size;
	uint32_t object_id;
	uint32_t flags;		/* enum cfr_option_flags */
	/*
	 * CFR_UI_NAME		ui_name
	 * <T in CFR_OPTION>	options[]
	 */
};

struct lb_cfr {
	uint32_t tag;
	uint32_t size;
	uint32_t checksum;	/* Of the entire structure with this field set to 0 */
	/* CFR_FORM forms[] */
};

#endif	/* DRIVERS_OPTION_CFR_H */
