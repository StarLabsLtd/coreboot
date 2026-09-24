/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_MOR_COLD_BOOT_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_MOR_COLD_BOOT_H

#include <commonlib/bsd/cb_err.h>
#include <stddef.h>
#include <stdint.h>

#define STARBOOK_MTL_MOR_COLD_REVISION 1U

enum starbook_mtl_mor_boot_kind {
	STARBOOK_MTL_MOR_BOOT_UNKNOWN,
	STARBOOK_MTL_MOR_BOOT_COLD,
	STARBOOK_MTL_MOR_BOOT_S3,
};

struct starbook_mtl_mor_cold_payload {
	uint32_t revision;
	uint32_t size;
	uint32_t boot_kind;
	uint32_t sealed;
	uint64_t generation;
	uint64_t seal;
} __aligned(8);

struct starbook_mtl_mor_cold_record {
	struct starbook_mtl_mor_cold_payload primary;
	struct starbook_mtl_mor_cold_payload mirror;
} __aligned(8);

struct starbook_mtl_mor_cold_capture {
	uint32_t boot_kind;
	uint32_t captured;
};

_Static_assert(sizeof(struct starbook_mtl_mor_cold_payload) == 32,
	"MTL MOR cold-boot payload ABI changed");
_Static_assert(sizeof(struct starbook_mtl_mor_cold_record) == 64,
	"MTL MOR cold-boot record ABI changed");

struct starbook_mtl_mor_cold_ops {
	void *context;
	enum cb_err (*protected_limit)(void *context, uint64_t *exclusive_limit);
	enum cb_err (*quiesce)(void *context);
	enum cb_err (*random64)(void *context, uint64_t *value);
};

void starbook_mtl_mor_cold_capture(
	struct starbook_mtl_mor_cold_capture *capture, int s3wake);
enum cb_err starbook_mtl_mor_cold_publish(
	struct starbook_mtl_mor_cold_capture *capture,
	struct starbook_mtl_mor_cold_record *record, uintptr_t entry_base,
	size_t entry_size, const struct starbook_mtl_mor_cold_ops *ops);
enum cb_err starbook_mtl_mor_cold_consume(
	struct starbook_mtl_mor_cold_record *record, uintptr_t entry_base,
	size_t entry_size, const struct starbook_mtl_mor_cold_ops *ops,
	uint64_t *generation);

void mainboard_mor_cold_capture(int s3wake);
enum cb_err mainboard_mor_cold_publish(void);
enum cb_err starbook_mtl_mor_cold_ramstage_consume(uint64_t *generation);

#endif
