/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <device/pci_bme_quiesce.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

struct function {
	uint32_t id;
	uint32_t class;
	uint16_t command;
};
struct context {
	struct function functions[3][256];
	struct pci_bme_quiesce_snapshot *snapshot;
	struct pci_bme_quiesce_io *io;
	bool retain_bme;
	bool alter_non_bme;
	bool alter_class;
	bool transient_side_effect;
	bool side_effect_pending;
	uint16_t restore_command;
	bool mutate_snapshot;
	bool mutate_io;
};

static uint32_t read32(void *opaque, uint8_t bus, uint8_t devfn, uint16_t offset)
{
	struct context *context = opaque;
	struct function *f = &context->functions[bus][devfn];

	if (context->mutate_snapshot && !bus && !devfn) {
		context->snapshot->bus_count = 3;
		context->mutate_snapshot = false;
	}
	if (context->mutate_io && !bus && !devfn)
		context->io->read32 = NULL;
	if (!f->id)
		return UINT32_MAX;
	if (offset == 0)
		return f->id;
	if (offset == 4) {
		const uint16_t command = f->command;

		if (context->side_effect_pending) {
			f->command = context->restore_command;
			context->side_effect_pending = false;
		}
		return command;
	}
	return f->class;
}

static void write16(void *opaque, uint8_t bus, uint8_t devfn, uint16_t offset,
	uint16_t value)
{
	struct context *context = opaque;
	assert(offset == 4);
	if (!context->retain_bme) {
		context->functions[bus][devfn].command = context->alter_non_bme ?
			(value ^ 1U) : value;
		if (context->alter_class)
			context->functions[bus][devfn].class ^= 0x100U;
		if (context->alter_non_bme && context->transient_side_effect) {
			context->restore_command = value;
			context->side_effect_pending = true;
		}
	}
}

static void add(struct context *c, unsigned int index)
{
	c->functions[index / 256][index % 256] = (struct function) {
		.id = (0x2000U + index) << 16 | (0x1000U + index),
		.class = (0x010000U + index) << 8, .command = 7,
	};
}

int main(int argc, char **argv)
{
	struct context context = { 0 };
	struct pci_bme_quiesce_snapshot snapshot = { 0 };
	struct pci_bme_quiesce_snapshot workspace = { 0 };
	struct pci_bme_quiesce_io io = { &context, read32, write16 };

	assert(argc == 2);
	context.snapshot = &snapshot;
	if (!strcmp(argv[1], "invalid")) {
		memset(&snapshot, 0xa5, sizeof(snapshot));
		assert(pci_bme_quiesce(NULL, 2, &snapshot, &workspace) == CB_ERR_ARG);
		assert(snapshot.failed == 1);
		assert(snapshot.bus_count == 0 && snapshot.count == 0);
		for (size_t index = 0; index < PCI_BME_QUIESCE_MAX_FUNCTIONS; index++)
			assert(!snapshot.functions[index].bdf &&
				!snapshot.functions[index].vendor &&
				!snapshot.functions[index].device &&
				!snapshot.functions[index].command &&
				!snapshot.functions[index].class);
		return 0;
	}
	if (!strcmp(argv[1], "overflow")) {
		for (unsigned int i = 0; i < 513; i++)
			add(&context, i);
		assert(pci_bme_quiesce(&io, 3, &snapshot, &workspace) == CB_ERR &&
			snapshot.failed);
		for (unsigned int i = 0; i < 513; i++)
			assert(!(context.functions[i / 256][i % 256].command & 4));
		return 0;
	}
	add(&context, 0);
	add(&context, 257);
	if (!strcmp(argv[1], "readback"))
		context.retain_bme = true;
	if (!strcmp(argv[1], "alias")) {
		struct pci_bme_quiesce_io *bad = (void *)&snapshot;
		assert(pci_bme_quiesce(bad, 2, &snapshot, &workspace) == CB_ERR_ARG);
		return 0;
	}
	if (!strcmp(argv[1], "context-alias")) {
		io.context = &snapshot;
		assert(pci_bme_quiesce(&io, 2, &snapshot, &workspace) == CB_ERR_ARG);
		return 0;
	}
	if (!strcmp(argv[1], "workspace-alias")) {
		assert(pci_bme_quiesce(&io, 2, &snapshot, &snapshot) == CB_ERR_ARG);
		assert(snapshot.failed);
		return 0;
	}
	if (!strcmp(argv[1], "io-mutation")) {
		context.io = &io;
		context.mutate_io = true;
		assert(pci_bme_quiesce(&io, 2, &snapshot, &workspace) == CB_ERR &&
			snapshot.failed);
		return 0;
	}
	if (!strcmp(argv[1], "initial-mutation"))
		context.mutate_snapshot = true;
	if (!strcmp(argv[1], "side-effect"))
		context.alter_non_bme = context.transient_side_effect = true;
	if (!strcmp(argv[1], "class-side-effect"))
		context.alter_class = true;
	if (!strcmp(argv[1], "readback")) {
		assert(pci_bme_quiesce(&io, 2, &snapshot, &workspace) == CB_ERR &&
			snapshot.failed);
		return 0;
	}
	if (!strcmp(argv[1], "initial-mutation") ||
	    !strcmp(argv[1], "side-effect") ||
	    !strcmp(argv[1], "class-side-effect")) {
		assert(pci_bme_quiesce(&io, 2, &snapshot, &workspace) == CB_ERR &&
			snapshot.failed);
		return 0;
	}
	if (!strcmp(argv[1], "max")) {
		memset(&context, 0, sizeof(context));
		for (unsigned int index = 0; index < 512; index++)
			add(&context, index);
	}
	assert(pci_bme_quiesce(&io, 2, &snapshot, &workspace) == CB_SUCCESS);
	if (!strcmp(argv[1], "mutation"))
		context.mutate_snapshot = true;
	if (!strcmp(argv[1], "late-io-mutation")) {
		context.io = &io;
		context.mutate_io = true;
	}
	if (!strcmp(argv[1], "hot-add"))
		add(&context, 2);
	else if (!strcmp(argv[1], "remove"))
		memset(&context.functions[0][0], 0, sizeof(struct function));
	else if (!strcmp(argv[1], "identity"))
		context.functions[0][0].id++;
	else if (!strcmp(argv[1], "class"))
		context.functions[0][0].class += 0x100;
	else if (!strcmp(argv[1], "command"))
		context.functions[0][0].command ^= 1;
	else if (!strcmp(argv[1], "bme"))
		context.functions[0][0].command |= 4;
	else if (!strcmp(argv[1], "invalid-reuse")) {
		io.read32 = NULL;
		assert(pci_bme_quiesce_revalidate(&io, &snapshot, &workspace) ==
			CB_ERR_ARG && snapshot.failed);
		io.read32 = read32;
		assert(pci_bme_quiesce_revalidate(&io, &snapshot, &workspace) ==
			CB_ERR_ARG && snapshot.failed);
		return 0;
	} else if (!strcmp(argv[1], "header")) {
		snapshot.count = 0;
		assert(pci_bme_quiesce_revalidate(&io, &snapshot, &workspace) ==
			CB_ERR_ARG && snapshot.failed);
		return 0;
	} else
		assert(!strcmp(argv[1], "success") || !strcmp(argv[1], "max") ||
			!strcmp(argv[1], "mutation") ||
			!strcmp(argv[1], "late-io-mutation"));
	if (!strcmp(argv[1], "success") || !strcmp(argv[1], "max"))
		assert(pci_bme_quiesce_revalidate(&io, &snapshot, &workspace) ==
			CB_SUCCESS);
	else
		assert(pci_bme_quiesce_revalidate(&io, &snapshot, &workspace) == CB_ERR &&
			snapshot.failed);
	puts("PASS");
	return 0;
}
