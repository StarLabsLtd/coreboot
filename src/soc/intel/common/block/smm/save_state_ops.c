/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/intel/em64t100_save_state.h>
#include <cpu/intel/em64t101_save_state.h>
#include <cpu/x86/save_state.h>
#include <cpu/x86/smm.h>
#include <string.h>
#include <types.h>

static int em64t10x_get_reg(const enum cpu_reg reg, const int node, void *out, const uint8_t length)
{
	em64t101_smm_state_save_area_t *state;
	u64 value;

	if (!out || length > sizeof(value))
		return -1;

	state = smm_get_save_state(node);
	if (!state)
		return -1;

	switch (reg) {
	case RAX:
		value = state->rax;
		break;
	case RBX:
		value = state->rbx;
		break;
	case RCX:
		value = state->rcx;
		break;
	case RDX:
		value = state->rdx;
		break;
	default:
		return -1;
	}

	memcpy(out, &value, length);
	return 0;
}

static int em64t10x_set_reg(const enum cpu_reg reg, const int node, void *in, const uint8_t length)
{
	em64t101_smm_state_save_area_t *state;
	u64 value;

	if (!in || length > sizeof(value))
		return -1;

	state = smm_get_save_state(node);
	if (!state)
		return -1;

	value = 0;
	memcpy(&value, in, length);

	switch (reg) {
	case RAX:
		state->rax = value;
		break;
	case RBX:
		state->rbx = value;
		break;
	case RCX:
		state->rcx = value;
		break;
	case RDX:
		state->rdx = value;
		break;
	default:
		return -1;
	}

	return 0;
}

static int em64t10x_apmc_node(u8 cmd)
{
	int node;

	for (node = 0; node < CONFIG_MAX_CPUS; node++) {
		em64t101_smm_state_save_area_t *state;
		u32 io_misc_info;
		u8 reg_al;

		state = smm_get_save_state(node);
		if (!state)
			continue;

		io_misc_info = state->io_misc_info;

		/* Synchronous I/O (bit0 == 1). */
		if (!(io_misc_info & (1 << 0)))
			continue;

		/* Write (bit4 == 0). */
		if (io_misc_info & (1 << 4))
			continue;

		/* APMC port. */
		if (((io_misc_info >> 16) & 0xff) != APM_CNT)
			continue;

		reg_al = (u8)state->rax;
		if (reg_al != cmd)
			continue;

		return node;
	}

	return -1;
}

static const uint32_t em64t101_revision_table[] = {
	0x30101,
	SMM_REV_INVALID,
};

static const struct smm_save_state_ops em64t101_save_state_ops = {
	.revision_table = em64t101_revision_table,
	.get_reg = em64t10x_get_reg,
	.set_reg = em64t10x_set_reg,
	.apmc_node = em64t10x_apmc_node,
};

const struct smm_save_state_ops *em64t101_ops = &em64t101_save_state_ops;

static const uint32_t em64t100_revision_table[] = {
	0x30100,
	SMM_REV_INVALID,
};

static const struct smm_save_state_ops em64t100_save_state_ops = {
	.revision_table = em64t100_revision_table,
	.get_reg = em64t10x_get_reg,
	.set_reg = em64t10x_set_reg,
	.apmc_node = em64t10x_apmc_node,
};

const struct smm_save_state_ops *em64t100_ops = &em64t100_save_state_ops;
