/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_H

#include <commonlib/bsd/cb_err.h>
#include <cpu/x86/smm_invocation_loader_instance.h>
#include <stddef.h>
#include <stdint.h>

#define STARBOOK_MTL_LOADER_INSTANCE_FANOUT_REVISION 1U

enum starbook_mtl_loader_instance_fanout_state {
	STARBOOK_MTL_LOADER_INSTANCE_FANOUT_EMPTY,
	STARBOOK_MTL_LOADER_INSTANCE_FANOUT_PROVISIONING,
	STARBOOK_MTL_LOADER_INSTANCE_FANOUT_COMMITTING,
	STARBOOK_MTL_LOADER_INSTANCE_FANOUT_COMMITTED,
	STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONING,
	STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONED,
};

enum starbook_mtl_loader_instance_take_state {
	STARBOOK_MTL_LOADER_INSTANCE_TAKE_EMPTY,
	STARBOOK_MTL_LOADER_INSTANCE_TAKE_READY,
	STARBOOK_MTL_LOADER_INSTANCE_TAKE_TAKING,
	STARBOOK_MTL_LOADER_INSTANCE_TAKE_CONSUMED,
};

struct starbook_mtl_loader_instance_fanout {
	uint32_t state;
	uint32_t revision;
	uint32_t size;
	uint32_t loader_state;
	uint32_t lifecycle;
	uint32_t reserved;
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	uint64_t legacy_generation;
	uint32_t owner_attempt;
	uint32_t reserved_owner;
	uint64_t owner_identity;
	uint64_t reserved_tail;
	uint64_t protected_base;
	uint64_t protected_size;
	uint64_t protected_limit;
} __aligned(8);

struct starbook_mtl_loader_instance_owner {
	uint64_t fanout_identity;
	uint64_t protected_base;
	uint64_t protected_size;
	uint64_t protected_limit;
	uint32_t attempt;
	uint32_t reserved;
} __aligned(8);

_Static_assert(sizeof(struct starbook_mtl_loader_instance_fanout) == 96,
	"MTL loader-instance fanout ABI changed");
_Static_assert(offsetof(struct starbook_mtl_loader_instance_fanout,
	loader_instance_nonce) == 24,
	"MTL loader-instance fanout layout changed");
_Static_assert(offsetof(struct starbook_mtl_loader_instance_fanout,
	owner_identity) == 56,
	"MTL loader-instance owner layout changed");
_Static_assert(offsetof(struct starbook_mtl_loader_instance_fanout,
	protected_base) == 72,
	"MTL loader-instance protected range layout changed");
_Static_assert(sizeof(struct starbook_mtl_loader_instance_owner) == 40,
	"MTL loader-instance owner ABI changed");
_Static_assert(_Alignof(struct starbook_mtl_loader_instance_owner) == 8,
	"MTL loader-instance owner alignment changed");
_Static_assert(offsetof(struct starbook_mtl_loader_instance_owner,
	fanout_identity) == 0,
	"MTL loader-instance owner identity layout changed");
_Static_assert(offsetof(struct starbook_mtl_loader_instance_owner,
	protected_base) == 8,
	"MTL loader-instance owner range layout changed");
_Static_assert(offsetof(struct starbook_mtl_loader_instance_owner,
	protected_size) == 16,
	"MTL loader-instance owner size layout changed");
_Static_assert(offsetof(struct starbook_mtl_loader_instance_owner,
	protected_limit) == 24,
	"MTL loader-instance owner limit layout changed");
_Static_assert(offsetof(struct starbook_mtl_loader_instance_owner,
	attempt) == 32,
	"MTL loader-instance owner attempt layout changed");
_Static_assert(offsetof(struct starbook_mtl_loader_instance_owner,
	reserved) == 36,
	"MTL loader-instance owner reserved layout changed");

enum cb_err starbook_mtl_loader_instance_fanout_begin(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size, uint64_t protected_limit,
	struct starbook_mtl_loader_instance_owner *owner);
enum cb_err starbook_mtl_loader_instance_fanout_commit(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size, uint64_t protected_limit,
	uint32_t lifecycle,
	struct smm_invocation_loader_instance_nonce loader_instance_nonce,
	struct starbook_mtl_loader_instance_owner *owner);
void starbook_mtl_loader_instance_fanout_abort(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size, uint64_t protected_limit,
	struct starbook_mtl_loader_instance_owner *owner);
enum cb_err starbook_mtl_loader_instance_fanout_take(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size, uint64_t protected_limit,
	struct smm_invocation_loader_instance_seed *seed);
enum cb_err starbook_mtl_loader_instance_fanout_read_legacy(
	const struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size, uint64_t protected_limit,
	uint32_t *lifecycle, uint64_t *generation);
enum cb_err starbook_mtl_loader_instance_fanout_take_requiesced(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size,
	struct smm_invocation_loader_instance_seed *seed,
	enum cb_err (*requiesce)(uint64_t *protected_limit));

enum cb_err starbook_mtl_smm_invocation_loader_instance_take(
	struct smm_invocation_loader_instance_seed *seed);

#endif
