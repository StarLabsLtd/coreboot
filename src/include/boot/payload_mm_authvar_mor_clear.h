/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_H
#define BOOT_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_H

#include <boot/payload_mm_authvar_mor_grant.h>
#include <commonlib/bsd/cb_err.h>
#include <commonlib/bsd/compiler.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RAW_MAX_SPANS 32U

struct payload_mm_authvar_mor_clear_inventory {
	uint32_t revision;
	uint32_t size;
	uint64_t generation;
	uint8_t identity[32];
	uint32_t span_count;
	uint32_t reserved;
	struct payload_mm_authvar_mor_grant_span spans[
		PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RAW_MAX_SPANS];
} __aligned(8);

struct payload_mm_authvar_mor_clear_plan {
	uint32_t revision;
	uint32_t size;
	uint64_t inventory_generation;
	uint8_t inventory_identity[32];
	uint32_t span_count;
	uint32_t reserved;
	struct payload_mm_authvar_mor_grant_span spans[
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS];
} __aligned(8);

struct payload_mm_authvar_mor_clear_dma_snapshot {
	uint64_t generation;
	uint8_t identity[32];
	uint8_t reserved[8];
} __aligned(8);

struct payload_mm_authvar_mor_clear_facts {
	uint32_t revision;
	uint32_t size;
	uint64_t cold_boot_generation;
	struct payload_mm_authvar_mor_clear_dma_snapshot dma_before;
	struct payload_mm_authvar_mor_clear_dma_snapshot dma_after;
	uint64_t inventory_generation;
	uint8_t inventory_identity[32];
	uint8_t reserved[8];
} __aligned(8);

struct payload_mm_authvar_mor_clear_record {
	uint64_t base;
	uint64_t size;
	uint64_t written_bytes;
	uint64_t cache_writeback_fenced_bytes;
	uint64_t zero_readback_bytes;
	uint8_t reserved[8];
} __aligned(8);

struct payload_mm_authvar_mor_clear_transcript {
	uint32_t revision;
	uint32_t size;
	uint64_t cold_boot_generation;
	struct payload_mm_authvar_mor_entry entry;
	uint32_t reserved0;
	struct payload_mm_authvar_mor_clear_dma_snapshot dma_before;
	struct payload_mm_authvar_mor_clear_dma_snapshot dma_after;
	uint64_t inventory_generation;
	uint8_t inventory_identity[32];
	uint32_t cleared_span_count;
	uint32_t reserved1;
	struct payload_mm_authvar_mor_clear_record records[
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS];
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_authvar_mor_clear_inventory) == 824,
	"MOR clear inventory ABI changed");
_Static_assert(sizeof(struct payload_mm_authvar_mor_clear_plan) == 416,
	"MOR clear plan ABI changed");
_Static_assert(sizeof(struct payload_mm_authvar_mor_clear_dma_snapshot) == 48,
	"MOR clear DMA snapshot ABI changed");
_Static_assert(sizeof(struct payload_mm_authvar_mor_clear_facts) == 160,
	"MOR clear facts ABI changed");
_Static_assert(sizeof(struct payload_mm_authvar_mor_clear_record) == 48,
	"MOR clear record ABI changed");
_Static_assert(sizeof(struct payload_mm_authvar_mor_clear_transcript) == 888,
	"MOR clear transcript ABI changed");
_Static_assert(_Alignof(struct payload_mm_authvar_mor_clear_inventory) == 8 &&
	_Alignof(struct payload_mm_authvar_mor_clear_plan) == 8 &&
	_Alignof(struct payload_mm_authvar_mor_clear_dma_snapshot) == 8 &&
	_Alignof(struct payload_mm_authvar_mor_clear_facts) == 8 &&
	_Alignof(struct payload_mm_authvar_mor_clear_transcript) == 8,
	"MOR clear ABI alignment changed");
_Static_assert(offsetof(struct payload_mm_authvar_mor_clear_inventory, spans) == 56 &&
	offsetof(struct payload_mm_authvar_mor_clear_plan, spans) == 56 &&
	offsetof(struct payload_mm_authvar_mor_clear_transcript, records) == 168,
	"MOR clear ABI fields moved");

enum cb_err payload_mm_authvar_mor_clear_plan_build(
	const struct payload_mm_authvar_mor_clear_inventory *inventory,
	struct payload_mm_authvar_mor_clear_plan *plan);

enum cb_err payload_mm_authvar_mor_clear_plan_validate(
	const struct payload_mm_authvar_mor_clear_plan *plan);

enum cb_err payload_mm_authvar_mor_clear_receipt_build(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct payload_mm_authvar_mor_entry *entry,
	const struct payload_mm_authvar_mor_clear_facts *facts,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	struct payload_mm_authvar_mor_grant *grant);

#endif /* BOOT_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_H */
