/* SPDX-License-Identifier: GPL-2.0-only */

#include "native_cause.h"
#include <boot_device.h>
#include <cbfs.h>
#include <cpu/amd/amd64_save_state.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_save_state.h>
#include <fmap.h>
#include <southbridge/intel/common/pmbase.h>
#include <southbridge/intel/common/pmutil.h>
#include <string.h>

#if !ENV_SMM
#error "Q35 native cause capture is SMM-only"
#endif

static const uint8_t runtime_leaf[] = {
	0x0f, 0xb6, 0xc2, 0x0f, 0xb7, 0xd6, 0x31, 0xc9, 0xee, 0x31, 0xc0, 0xc3,
};
static const uint8_t bootstrap_leaf[] = { 0xee, 0xc3 };

static struct {
	uint32_t active;
	uint8_t command;
	bool long_mode;
	const struct smm_invocation_runtime_view *view;
	struct smm_invocation_runtime_binding runtime;
	amd64_smm_state_save_area_t *node;
	amd64_smm_state_save_area_t snapshot;
	const uint8_t *leaf;
	uintptr_t page_slots[3];
	uint64_t page_entries[3];
} owner;

static const uint8_t *flash_leaf(bool long_mode)
{
	const struct region_device *root = boot_device_ro();
	struct region_device store;
	enum cbfs_type type;
	size_t size;
	const uint8_t *leaf = cbfs_ro_type_map(long_mode ?
		"q35/native-apmc" : "q35/native-apmc-bootstrap", &size, &type);
	const uint8_t *expected = long_mode ? runtime_leaf : bootstrap_leaf;
	const size_t expected_size = long_mode ? sizeof(runtime_leaf) : sizeof(bootstrap_leaf);
	uint64_t media_base, offset;

	if (!root || !leaf || type != CBFS_TYPE_RAW || size != expected_size ||
	    !region_device_sz(root) || region_device_sz(root) > UINT32_MAX ||
	    fmap_locate_area_as_rdev("SMMSTORE", &store))
		return NULL;
	media_base = 0x100000000ULL - region_device_sz(root);
	if ((uintptr_t)leaf < media_base)
		return NULL;
	offset = (uintptr_t)leaf - media_base;
	if (offset > region_device_sz(root) - size ||
	    region_device_offset(&store) > region_device_sz(root) ||
	    !region_device_sz(&store) ||
	    region_device_sz(&store) > region_device_sz(root) - region_device_offset(&store) ||
	    (offset < region_device_offset(&store) + region_device_sz(&store) &&
	     region_device_offset(&store) < offset + size) || memcmp(leaf, expected, size))
		return NULL;
	return leaf;
}

static bool page_slot(uint64_t page, uint32_t index, unsigned int level)
{
	uintptr_t smram_base;
	size_t smram_size;

	smm_region(&smram_base, &smram_size);
	/* This is only the controlled component's low-RAM, three-level path. */
	if (!smram_size || page < 0x100000 || page % 4096 ||
	    page > UINT32_MAX || smram_base < 4096 || page > smram_base - 4096 ||
	    index >= 512)
		return false;
	owner.page_slots[level] = (uintptr_t)page + index * sizeof(uint64_t);
	owner.page_entries[level] = *(const volatile uint64_t *)owner.page_slots[level];
	return true;
}

static bool identity_mapping(void)
{
	const uint64_t physical_mask = 0x000ffffffffff000ULL;
	const uint64_t table_flags = 0x23;
	const uint64_t page_flags = 0xe3;
	const uint64_t address = (uintptr_t)owner.leaf;
	uint64_t entry;

	if (owner.snapshot.cr3 & 4095 ||
	    !page_slot(owner.snapshot.cr3, 0, 0))
		return false;
	entry = owner.page_entries[0];
	if ((entry & ~physical_mask & ~table_flags) || (entry & 3) != 3 ||
	    !page_slot(entry & physical_mask, address >> 30, 1))
		return false;
	entry = owner.page_entries[1];
	if ((entry & ~physical_mask & ~table_flags) || (entry & 3) != 3 ||
	    !page_slot(entry & physical_mask, (address >> 21) & 511, 2))
		return false;
	entry = owner.page_entries[2];
	return !(entry & ~physical_mask & ~page_flags) && (entry & 0x83) == 0x83 &&
		(entry & physical_mask) == (address & ~0x1fffffULL);
}

bool q35_native_cause_current(void)
{
	struct smm_invocation_runtime_binding runtime;
	const struct smm_invocation_runtime_view *view;

	if (!owner.active || (read_pmbase32(SMI_EN) & ~EOS) != (GBL_SMI_EN | APMC_EN) ||
	    apm_get_apmc() != owner.command ||
	    smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    memcmp(&runtime, &owner.runtime, sizeof(runtime)) ||
	    smm_invocation_runtime_view_get(&view) != CB_SUCCESS || view != owner.view ||
	    smm_get_save_state(0) != owner.node ||
	    memcmp(owner.node, &owner.snapshot, sizeof(owner.snapshot)) ||
	    flash_leaf(owner.long_mode) != owner.leaf)
		return false;
	if (owner.long_mode)
		for (unsigned int level = 0; level < 3; level++)
			if (*(const volatile uint64_t *)owner.page_slots[level] !=
			    owner.page_entries[level])
				return false;
	return true;
}

static enum smm_invocation_match match(void *context, uint32_t cpu, uint8_t command)
{
	if (context != &owner || cpu || command != owner.command)
		return SMM_INVOCATION_NOT_MATCHED;
	return q35_native_cause_current() ? SMM_INVOCATION_MATCHED : SMM_INVOCATION_MATCH_ERROR;
}

static enum cb_err read_value(void *context, uint32_t cpu, uint64_t *value)
{
	if (context != &owner || cpu || !value || !q35_native_cause_current())
		return CB_ERR;
	*value = ((uint64_t)(uint32_t)owner.snapshot.rcx << 32) |
		(uint32_t)owner.snapshot.rax;
	return CB_SUCCESS;
}

static enum cb_err write_value(void *context, uint32_t cpu, uint64_t value)
{
	if (context != &owner || cpu || !q35_native_cause_current())
		return CB_ERR;
	owner.node->rax = (uint32_t)value;
	owner.node->rcx = (uint32_t)(value >> 32);
	owner.snapshot.rax = (uint32_t)value;
	owner.snapshot.rcx = (uint32_t)(value >> 32);
	if (!q35_native_cause_current())
		smm_invocation_platform_fail_stop();
	return CB_SUCCESS;
}

static const struct smm_invocation_save_state_ops ops = {
	.match_apmc_write = match,
	.read_value = read_value,
	.write_value = write_value,
	.context = &owner,
	.context_size = sizeof(owner),
};

enum cb_err q35_native_cause_begin(uint8_t command,
	const struct smm_invocation_save_state_ops **output)
{
	struct smm_save_state_span span;
	uint32_t cpus;

	if (!output || owner.active)
		return CB_ERR_ARG;
	*output = NULL;
	memset(&owner, 0, sizeof(owner));
	if (smm_invocation_runtime_binding_get(&owner.runtime) != CB_SUCCESS ||
	    smm_invocation_runtime_view_get(&owner.view) != CB_SUCCESS ||
	    smm_invocation_runtime_cpu_count(owner.view, &cpus) != CB_SUCCESS || cpus != 1 ||
	    smm_invocation_runtime_range_is_protected(owner.view, &owner, sizeof(owner)) !=
		CB_SUCCESS ||
	    smm_invocation_runtime_save_state_span(owner.view, 0, &span) != CB_SUCCESS)
		goto failed;
	owner.node = smm_get_save_state(0);
	if (!owner.node || span.base != (uintptr_t)owner.node || span.size != sizeof(*owner.node))
		goto failed;
	owner.snapshot = *owner.node;
	owner.command = command;
	owner.long_mode = !!(owner.snapshot.efer & (1U << 10));
	if (owner.snapshot.smm_revision != 0x20064 ||
	    (owner.snapshot.rflags & (1U << 9)) || owner.snapshot.cs_base ||
	    (owner.snapshot.cs_selector & 3) || !(owner.snapshot.cr0 & 1) ||
	    (owner.snapshot.cr4 & ((1U << 12) | (1U << 17))) ||
	    (uint8_t)owner.snapshot.rax != command ||
	    (uint16_t)owner.snapshot.rdx != 0xb2)
		goto failed;
	owner.leaf = flash_leaf(owner.long_mode);
	if (!owner.leaf || owner.snapshot.rip != (uintptr_t)owner.leaf +
		(owner.long_mode ? 9 : 1))
		goto failed;
	if (owner.long_mode) {
		if (!(owner.snapshot.cs_attributcs & (1U << 13)) ||
		    (owner.snapshot.cs_attributcs & (1U << 14)) ||
		    !(owner.snapshot.cr0 & (1U << 31)) || !(owner.snapshot.cr4 & (1U << 5)) ||
		    owner.snapshot.rax != command || owner.snapshot.rdx != 0xb2 ||
		    owner.snapshot.rcx || !identity_mapping())
			goto failed;
	} else if ((owner.snapshot.cs_attributcs & (1U << 13)) ||
		   !(owner.snapshot.cs_attributcs & (1U << 14)) ||
		   (owner.snapshot.cr0 & (1U << 31))) {
		goto failed;
	}
	owner.active = 1;
	if (!q35_native_cause_current())
		goto failed;
	*output = &ops;
	return CB_SUCCESS;
failed:
	memset(&owner, 0, sizeof(owner));
	return CB_ERR;
}

void q35_native_cause_end(void)
{
	memset(&owner, 0, sizeof(owner));
}
