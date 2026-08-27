/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef COMMONLIB_OPTION_CFR_H
#define COMMONLIB_OPTION_CFR_H

#include <types.h>

/*
 * PUBLIC API
 *
 * The CFR table information is for conveying available boot
 * option of the firmware to the loaded OS image or payload.
 * Primarily this is expected to be information that is user
 * visible.
 *
 * The boot options are grouped into forms, where each form hold
 * one ore more options.
 * Boot options that are not used in the current boot flow, but
 * are reachable under certain conditions should be marked as hidden.
 *
 * A boot option can be one of the following types:
 * - boolean
 * - number
 * - enum
 * - string
 *
 * Each boot option has an UI name and UI help text that is free to
 * choose. All strings are in ASCII.
 *
 * All of the information should be Position Independent Data.
 * That is it should be safe to relocated any of the information
 * without it's meaning/correctness changing.   For table that
 * can reasonably be used on multiple architectures the data
 * size should be fixed.  This should ease the transition between
 * 32 bit and 64 bit architectures etc.
 *
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
 */

#define CFR_VERSION 0x00000000

enum cfr_tags {
	CFR_TAG_OPTION_FORM		= 1,
	CFR_TAG_ENUM_VALUE		= 2,
	CFR_TAG_OPTION_ENUM		= 3,
	CFR_TAG_OPTION_NUMBER		= 4,
	CFR_TAG_OPTION_BOOL		= 5,
	CFR_TAG_OPTION_VARCHAR		= 6,
	CFR_TAG_VARCHAR_OPT_NAME	= 7,
	CFR_TAG_VARCHAR_UI_NAME		= 8,
	CFR_TAG_VARCHAR_UI_HELPTEXT	= 9,
	CFR_TAG_VARCHAR_DEF_VALUE	= 10,
	CFR_TAG_OPTION_COMMENT		= 11,
	CFR_TAG_DEP_VALUES		= 12,
	/* Reserved by the Linux CFR runtime-apply ABI. */
	CFR_TAG_RUNTIME_APPLY		= 13,
	CFR_TAG_OPTION_ACCESS		= 14,
};

enum cfr_settings_access_permissions {
	CFR_SETTINGS_ACCESS_READ = 1 << 0,
	CFR_SETTINGS_ACCESS_WRITE = 1 << 1,
};

#define CFR_SETTINGS_ACCESS_PERMISSIONS_MASK \
	(CFR_SETTINGS_ACCESS_READ | CFR_SETTINGS_ACCESS_WRITE)
#define CFR_OPTION_ACCESS_VERSION 1
#define CFR_OPTION_ACCESS_SIZE_V1 24

/* Stable firmware-owned token and permissions for the settings service. */
struct __packed lb_cfr_option_access {
	uint32_t tag;		/* CFR_TAG_OPTION_ACCESS */
	uint32_t size;
	uint32_t version;
	uint32_t token;
	uint32_t permissions;	/* enum cfr_settings_access_permissions */
	uint32_t reserved;
};

_Static_assert(sizeof(struct lb_cfr_option_access) == CFR_OPTION_ACCESS_SIZE_V1,
	       "CFR option access ABI size changed");

#define CFR_SETTINGS_VERSION 1
#define CFR_SETTINGS_MAILBOX_SIZE 64

enum cfr_settings_command {
	CFR_SETTINGS_CMD_NONE = 0,
	CFR_SETTINGS_CMD_GET = 1,
	CFR_SETTINGS_CMD_SET = 2,
};

enum cfr_settings_status {
	CFR_SETTINGS_STATUS_IDLE = 0,
	CFR_SETTINGS_STATUS_BUSY = 1,
	CFR_SETTINGS_STATUS_VALUE = 2,
	CFR_SETTINGS_STATUS_UNCHANGED = 3,
	CFR_SETTINGS_STATUS_APPLIED = 4,
	CFR_SETTINGS_STATUS_REBOOT_REQUIRED = 5,
	CFR_SETTINGS_STATUS_ROLLED_BACK = 6,
	CFR_SETTINGS_STATUS_INDETERMINATE = 7,
	CFR_SETTINGS_STATUS_INVALID_MAILBOX = 8,
	CFR_SETTINGS_STATUS_INVALID_COMMAND = 9,
	CFR_SETTINGS_STATUS_INVALID_TOKEN = 10,
	CFR_SETTINGS_STATUS_DENIED = 11,
	CFR_SETTINGS_STATUS_CONFLICT = 12,
	CFR_SETTINGS_STATUS_INVALID_VALUE = 13,
	CFR_SETTINGS_STATUS_DEPENDENCY_FAILED = 14,
	CFR_SETTINGS_STATUS_STORAGE_ERROR = 15,
};

enum cfr_settings_response_flags {
	CFR_SETTINGS_RESP_FAULTED = 1 << 0,
	CFR_SETTINGS_RESP_CURRENT_VALID = 1 << 1,
};

/*
 * Communication buffer shared with the payload or operating system. The
 * buffer address is firmware-owned and advertised by LB_TAG_CFR_SETTINGS.
 */
struct __packed cfr_settings_mailbox {
	uint32_t version;
	uint32_t size;
	uint64_t sequence;
	uint32_t command;
	uint32_t status;
	uint32_t token;
	uint32_t expected_value;
	uint32_t value;
	uint32_t current_value;
	uint32_t response_flags;
	uint32_t reserved[5];
};

_Static_assert(sizeof(struct cfr_settings_mailbox) == CFR_SETTINGS_MAILBOX_SIZE,
	       "CFR settings mailbox ABI size changed");

/*
 * The optional flags describe the visibilty of the option and the
 * effect on the non-volatile variable.
 * CFR_OPTFLAG_READONLY:
 *   Prevents writes to the variable.
 * CFR_OPTFLAG_INACTIVE:
 *   Implies READONLY. The option is visible, but cannot be modified
 *   because one of the dependencies are not given. However there's a
 *   possibility to enable the option by changing runtime configuration.
 *
 *   For example: Setting SATA mode, but SATA is globally disabled.
 * CFR_OPTFLAG_SUPPRESS:
 *   Runtime code sets this flag to indicate that the option has no effect
 *   and is never reachable, not even by changing runtime configuration.
 *   This option is never shown in the UI.
 * CFR_OPTFLAG_VOLATILE:
 *   Implies READONLY.
 *   The option is not backed by a non-volatile variable. This is useful
 *   to display the current state of a specific component, a dependency or
 *   a serial number. This information could be passed in a new coreboot
 *   table, but it not useful other than to be shown at this spot in the
 *   UI.
 * CFR_OPTFLAG_RUNTIME:
 *  The option is allowed to be changed by a post payload entity. On UEFI
 *  this sets the EFI_VARIABLE_RUNTIME_ACCESS attribute.
 *  It is out of scope of this specification how non runtime variables
 *  are protected after the payload has hand over control.
 */
enum cfr_option_flags {
	CFR_OPTFLAG_READONLY	= 1 << 0,
	CFR_OPTFLAG_INACTIVE	= 1 << 1,
	CFR_OPTFLAG_SUPPRESS	= 1 << 2,
	CFR_OPTFLAG_VOLATILE	= 1 << 3,
	CFR_OPTFLAG_RUNTIME	= 1 << 4,
};

struct __packed lb_cfr_varbinary {
	uint32_t tag;		/*
				 * CFR_TAG_VARCHAR_OPT_NAME, CFR_TAG_VARCHAR_UI_NAME,
				 * CFR_TAG_VARCHAR_UI_HELPTEXT, CFR_TAG_VARCHAR_DEF_VALUE
				 * or CFR_TAG_DEP_VALUES
				 */
	uint32_t size;		/* Length of the entire structure */
	uint32_t data_length;	/* Length of data, including NULL terminator for strings */
};

struct __packed lb_cfr_enum_value {
	uint32_t tag;		/* CFR_TAG_ENUM_VALUE */
	uint32_t size;
	uint32_t value;
	/*
	 * struct lb_cfr_varbinary		ui_name
	 */
};

/*
 * The optional flags describe how a numeric option is to be displayed.
 * CFR_NUM_OPT_DISPFLAG_HEX:
 *   Displays a NUMBER option in hexadecimal instead of decimal notation.
 */
enum cfr_numeric_option_display_flags {
	CFR_NUM_OPT_DISPFLAG_HEX	= 1 << 0,
};

/* Supports multiple option types: ENUM, NUMBER, BOOL */
struct __packed lb_cfr_numeric_option {
	uint32_t tag;		/*
				 * CFR_TAG_OPTION_ENUM, CFR_TAG_OPTION_NUMBER or
				 * CFR_TAG_OPTION_BOOL
				 */
	uint32_t size;
	uint64_t object_id;	/* Uniqueue ID */
	uint64_t dependency_id;	/* Grayout if value of lb_cfr_numeric_option with given ID is 0.
				 * Ignore if field is 0.
				 */
	uint32_t flags;		/* enum cfr_option_flags */
	uint32_t default_value;
	uint32_t min;
	uint32_t max;
	uint32_t step;
	uint32_t display_flags;	/* enum cfr_numeric_option_display_flags */

	/*
	 * struct lb_cfr_varbinary		opt_name
	 * struct lb_cfr_varbinary		ui_name
	 * struct lb_cfr_varbinary		ui_helptext (Optional)
	 * struct lb_cfr_varbinary		dependency_values (Optional)
	 * struct lb_cfr_enum_value		enum_values[]
	 * struct lb_cfr_option_access		settings_access (Optional)
	 */
};

struct __packed lb_cfr_varchar_option {
	uint32_t tag;		/* CFR_TAG_OPTION_VARCHAR */
	uint32_t size;
	uint64_t object_id;	/* Uniqueue ID */
	uint64_t dependency_id;	/* Grayout if value of lb_cfr_numeric_option with given ID is 0.
				 * Ignore if field is 0.
				 */
	uint32_t flags;		/* enum cfr_option_flags */
	/*
	 * struct lb_cfr_varbinary		default_value
	 * struct lb_cfr_varbinary		opt_name
	 * struct lb_cfr_varbinary		ui_name
	 * struct lb_cfr_varbinary		ui_helptext (Optional)
	 * struct lb_cfr_varbinary		dependency_values (Optional)
	 */
};

/*
 * A CFR option comment is roughly equivalent to a Kconfig comment.
 * Option comments are *NOT* string options (see CFR_OPTION_VARCHAR
 * instead) but they're considered an option for simplicity's sake.
 */
struct __packed lb_cfr_option_comment {
	uint32_t tag;		/* CFR_TAG_OPTION_COMMENT */
	uint32_t size;
	uint64_t object_id;	/* Uniqueue ID */
	uint64_t dependency_id;	/* Grayout if value of lb_cfr_numeric_option with given ID is 0.
				 * Ignore if field is 0.
				 */
	uint32_t flags;		/* enum cfr_option_flags */
	/*
	 * struct lb_cfr_varbinary		ui_name
	 * struct lb_cfr_varbinary		ui_helptext (Optional)
	 * struct lb_cfr_varbinary		dependency_values (Optional)
	 */
};

/* CFR forms are considered options as they can be nested inside other forms */
struct __packed lb_cfr_option_form {
	uint32_t tag;		/* CFR_TAG_OPTION_FORM */
	uint32_t size;
	uint64_t object_id;	/* Uniqueue ID */
	uint64_t dependency_id;	/* Grayout if value of lb_cfr_numeric_option with given ID is 0.
				 * Ignore if field is 0.
				 */
	uint32_t flags;		/* enum cfr_option_flags */
	/*
	 * struct lb_cfr_varbinary		ui_name
	 * struct lb_cfr_varbinary		dependency_values (Optional)
	 * struct lb_cfr_varchar_option		options[]
	 */
};

struct __packed lb_cfr_header {
	uint32_t tag;
	uint32_t size;
};

#endif	/* DRIVERS_OPTION_CFR_H */
