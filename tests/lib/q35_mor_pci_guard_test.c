/* SPDX-License-Identifier: GPL-2.0-only */

#include "../../src/mainboard/emulation/qemu-q35/q35_mor_pci_guard.h"

#include <assert.h>
#include <device/pci_def.h>
#include <stdbool.h>
#include <string.h>

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

#define ECAM_BASE 0x10000000U
#define FUNCTIONS_PER_BUS 256U
#define MAX_FUNCTIONS (2U * FUNCTIONS_PER_BUS)

struct fixture {
	uint16_t vendor[MAX_FUNCTIONS];
	uint16_t device[MAX_FUNCTIONS];
	uint16_t command[MAX_FUNCTIONS];
	unsigned int writes[MAX_FUNCTIONS];
	unsigned int vendor_reads[MAX_FUNCTIONS];
	unsigned int device_reads[MAX_FUNCTIONS];
	unsigned int device_fail_read[MAX_FUNCTIONS];
	unsigned int command_reads[MAX_FUNCTIONS];
	bool transient_vendor[MAX_FUNCTIONS];
	bool transient_device[MAX_FUNCTIONS];
	bool transient_command[MAX_FUNCTIONS];
	int stuck;
};

static size_t function_index(uintptr_t address)
{
	return (address - ECAM_BASE) >> 12;
}

static uint16_t fixture_read(void *context, uintptr_t address)
{
	struct fixture *fixture = context;
	const size_t index = function_index(address);
	const uint32_t offset = address & 0xfffU;

	assert(index < MAX_FUNCTIONS);
	if (offset == PCI_COMMAND) {
		fixture->command_reads[index]++;
		if (fixture->transient_command[index] &&
		    fixture->command_reads[index] == 2U)
			return UINT16_MAX;
		return fixture->command[index];
	}
	if (offset == PCI_DEVICE_ID) {
		fixture->device_reads[index]++;
		if (fixture->device_fail_read[index] == fixture->device_reads[index])
			return UINT16_MAX;
		if (fixture->transient_device[index] &&
		    fixture->device_reads[index] == 2U)
			return UINT16_MAX;
		return fixture->device[index];
	}
	assert(offset == PCI_VENDOR_ID);
	fixture->vendor_reads[index]++;
	if (fixture->transient_vendor[index] &&
	    fixture->vendor_reads[index] == 2U)
		return UINT16_MAX;
	return fixture->vendor[index];
}

static void fixture_write(void *context, uintptr_t address, uint16_t value)
{
	struct fixture *fixture = context;
	const size_t index = function_index(address);

	assert(index < MAX_FUNCTIONS && (address & 0xfffU) == PCI_COMMAND);
	fixture->writes[index]++;
	if ((int)index != fixture->stuck)
		fixture->command[index] = value;
}

static void setup(struct fixture *fixture, size_t devices)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->stuck = -1;
	for (size_t index = 0; index < ARRAY_SIZE(fixture->vendor); index++)
		fixture->vendor[index] = index < devices ? 0x1234U : UINT16_MAX;
	for (size_t index = 0; index < ARRAY_SIZE(fixture->device); index++)
		fixture->device[index] = index < devices ? (uint16_t)(0x5678U + index) :
			UINT16_MAX;
	for (size_t index = 0; index < devices; index++)
		fixture->command[index] = PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
}

static void test_capture_fail_closed(void)
{
	struct fixture fixture;
	struct q35_mor_pci_io io = {
		.context = &fixture,
		.read16 = fixture_read,
		.write16 = fixture_write,
	};
	struct q35_mor_pci_requester requesters[MAX_FUNCTIONS];
	size_t count = 7U;

	setup(&fixture, 70U);
	fixture.stuck = 0;
	assert(q35_mor_pci_guard_capture(&io, ECAM_BASE, 1U, requesters,
		64U, &count) == CB_ERR);
	assert(count == 0U);
	for (size_t index = 0; index < 70U; index++) {
		assert(fixture.writes[index] == 1U);
		assert(fixture.command[index] & PCI_COMMAND_MEMORY);
	}
	assert(!(fixture.command[69] & PCI_COMMAND_MASTER));

	setup(&fixture, 65U);
	count = 7U;
	assert(q35_mor_pci_guard_capture(&io, ECAM_BASE, 1U, requesters,
		64U, &count) == CB_ERR);
	assert(count == 0U && fixture.writes[64] == 1U);

	setup(&fixture, 251U);
	fixture.transient_vendor[0] = true;
	fixture.transient_command[125] = true;
	fixture.transient_device[200] = true;
	fixture.transient_vendor[250] = true;
	count = 7U;
	assert(q35_mor_pci_guard_capture(&io, ECAM_BASE, 1U, requesters,
		ARRAY_SIZE(requesters), &count) == CB_ERR);
	assert(count == 0U);
	assert(fixture.writes[0] == 1U && fixture.writes[125] == 1U &&
		fixture.writes[200] == 1U &&
		fixture.writes[250] == 1U);
	assert(!(fixture.command[0] & PCI_COMMAND_MASTER) &&
		!(fixture.command[125] & PCI_COMMAND_MASTER) &&
		!(fixture.command[250] & PCI_COMMAND_MASTER));
}

static void test_capture_and_validate(void)
{
	struct fixture fixture;
	struct q35_mor_pci_io io = {
		.context = &fixture,
		.read16 = fixture_read,
		.write16 = fixture_write,
	};
	struct q35_mor_pci_requester requesters[MAX_FUNCTIONS];
	size_t count = 0;

	setup(&fixture, 3U);
	assert(q35_mor_pci_guard_capture(&io, ECAM_BASE, 1U, requesters,
		ARRAY_SIZE(requesters), &count) == CB_SUCCESS);
	assert(count == 3U);
	for (size_t index = 0; index < count; index++) {
		assert(requesters[index].bdf == index);
		assert(requesters[index].vendor_id == 0x1234U);
		assert(requesters[index].device_id == 0x5678U + index);
		assert(fixture.vendor_reads[index] == 4U);
		assert(fixture.device_reads[index] == 4U);
		assert(fixture.command[index] == PCI_COMMAND_MEMORY);
	}
	assert(q35_mor_pci_guard_validate(&io, ECAM_BASE, 1U, requesters,
		count) == CB_SUCCESS);
	fixture.command[2] |= PCI_COMMAND_MASTER;
	assert(q35_mor_pci_guard_validate(&io, ECAM_BASE, 1U, requesters,
		count) == CB_ERR);
	fixture.command[2] &= (uint16_t)~PCI_COMMAND_MASTER;
	fixture.device[2]++;
	assert(q35_mor_pci_guard_validate(&io, ECAM_BASE, 1U, requesters,
		count) == CB_ERR);
	fixture.device[2]--;
	fixture.vendor[2]++;
	assert(q35_mor_pci_guard_validate(&io, ECAM_BASE, 1U, requesters,
		count) == CB_ERR);
	fixture.vendor[2]--;
	fixture.device_reads[1] = 0;
	fixture.transient_device[1] = true;
	assert(q35_mor_pci_guard_validate(&io, ECAM_BASE, 1U, requesters,
		count) == CB_ERR);
	fixture.transient_device[1] = false;
	fixture.vendor[3] = 0x1234U;
	fixture.device[3] = 0x7777U;
	assert(q35_mor_pci_guard_validate(&io, ECAM_BASE, 1U, requesters,
		count) == CB_ERR);

	setup(&fixture, FUNCTIONS_PER_BUS + 2U);
	assert(q35_mor_pci_guard_capture(&io, ECAM_BASE, 2U, requesters,
		ARRAY_SIZE(requesters), &count) == CB_SUCCESS);
	assert(count == FUNCTIONS_PER_BUS + 2U);
	assert(requesters[FUNCTIONS_PER_BUS].bdf == 0x100U);
	assert(q35_mor_pci_guard_validate(&io, ECAM_BASE, 2U, requesters,
		count) == CB_SUCCESS);
}

static void capture_fixture(struct fixture *fixture,
	const struct q35_mor_pci_io *io,
	struct q35_mor_pci_requester *requesters, size_t *count,
	size_t devices)
{
	setup(fixture, devices);
	*count = 0;
	assert(q35_mor_pci_guard_capture(io, ECAM_BASE, 1U, requesters,
		MAX_FUNCTIONS, count) == CB_SUCCESS);
	assert(*count == devices);
}

static void test_requiesce_retained_topology(void)
{
	struct fixture fixture;
	struct q35_mor_pci_io io = {
		.context = &fixture,
		.read16 = fixture_read,
		.write16 = fixture_write,
	};
	struct q35_mor_pci_requester requesters[MAX_FUNCTIONS];
	size_t count;

	capture_fixture(&fixture, &io, requesters, &count, 3U);
	for (size_t index = 0; index < count; index++)
		fixture.command[index] |= PCI_COMMAND_MASTER;
	assert(q35_mor_pci_guard_requiesce(&io, ECAM_BASE, 1U, requesters,
		count) == CB_SUCCESS);
	for (size_t index = 0; index < count; index++) {
		assert(fixture.writes[index] == 2U);
		assert(!(fixture.command[index] & PCI_COMMAND_MASTER));
	}

	/* An added function is topology drift and must prevent every write. */
	capture_fixture(&fixture, &io, requesters, &count, 3U);
	fixture.vendor[3] = 0x1234U;
	assert(q35_mor_pci_guard_requiesce(&io, ECAM_BASE, 1U, requesters,
		count) == CB_ERR);
	for (size_t index = 0; index < count; index++)
		assert(fixture.writes[index] == 1U);

	/* A missing or unstable retained function likewise fails before writes. */
	capture_fixture(&fixture, &io, requesters, &count, 3U);
	fixture.vendor[1] = UINT16_MAX;
	assert(q35_mor_pci_guard_requiesce(&io, ECAM_BASE, 1U, requesters,
		count) == CB_ERR);
	for (size_t index = 0; index < count; index++)
		assert(fixture.writes[index] == 1U);

	/* A different device at the same BDF is topology drift before writes. */
	capture_fixture(&fixture, &io, requesters, &count, 3U);
	fixture.device[1]++;
	assert(q35_mor_pci_guard_requiesce(&io, ECAM_BASE, 1U, requesters,
		count) == CB_ERR);
	for (size_t index = 0; index < count; index++)
		assert(fixture.writes[index] == 1U);

	capture_fixture(&fixture, &io, requesters, &count, 3U);
	fixture.device_reads[1] = 0;
	fixture.transient_device[1] = true;
	assert(q35_mor_pci_guard_requiesce(&io, ECAM_BASE, 1U, requesters,
		count) == CB_ERR);
	for (size_t index = 0; index < count; index++)
		assert(fixture.writes[index] == 1U);

	/* Identity failure after preflight still quiesces every retained BDF. */
	capture_fixture(&fixture, &io, requesters, &count, 3U);
	for (size_t index = 0; index < count; index++)
		fixture.command[index] |= PCI_COMMAND_MASTER;
	memset(fixture.device_reads, 0, sizeof(fixture.device_reads));
	fixture.device_fail_read[1] = 3U;
	assert(q35_mor_pci_guard_requiesce(&io, ECAM_BASE, 1U, requesters,
		count) == CB_ERR);
	for (size_t index = 0; index < count; index++) {
		assert(fixture.writes[index] == 2U);
		assert(!(fixture.command[index] & PCI_COMMAND_MASTER));
	}

	capture_fixture(&fixture, &io, requesters, &count, 3U);
	fixture.vendor_reads[1] = 0;
	fixture.transient_vendor[1] = true;
	assert(q35_mor_pci_guard_requiesce(&io, ECAM_BASE, 1U, requesters,
		count) == CB_ERR);
	for (size_t index = 0; index < count; index++)
		assert(fixture.writes[index] == 1U);

	capture_fixture(&fixture, &io, requesters, &count, 3U);
	fixture.command_reads[1] = 0;
	fixture.transient_command[1] = true;
	assert(q35_mor_pci_guard_requiesce(&io, ECAM_BASE, 1U, requesters,
		count) == CB_ERR);
	for (size_t index = 0; index < count; index++)
		assert(fixture.writes[index] == 1U);

	capture_fixture(&fixture, &io, requesters, &count, 3U);
	requesters[0].bdf = 1U;
	requesters[1].bdf = 0U;
	assert(q35_mor_pci_guard_requiesce(&io, ECAM_BASE, 1U, requesters,
		count) == CB_ERR);
	for (size_t index = 0; index < count; index++)
		assert(fixture.writes[index] == 1U);

	/* A failed readback is terminal, but must not skip later requesters. */
	capture_fixture(&fixture, &io, requesters, &count, 3U);
	for (size_t index = 0; index < count; index++)
		fixture.command[index] |= PCI_COMMAND_MASTER;
	fixture.stuck = 1;
	assert(q35_mor_pci_guard_requiesce(&io, ECAM_BASE, 1U, requesters,
		count) == CB_ERR);
	for (size_t index = 0; index < count; index++)
		assert(fixture.writes[index] == 2U);
	assert(fixture.command[1] & PCI_COMMAND_MASTER);
	assert(!(fixture.command[0] & PCI_COMMAND_MASTER));
	assert(!(fixture.command[2] & PCI_COMMAND_MASTER));
}

static void test_arguments(void)
{
	struct fixture fixture;
	struct q35_mor_pci_io io = {
		.context = &fixture,
		.read16 = fixture_read,
		.write16 = fixture_write,
	};
	struct q35_mor_pci_requester requesters[4];
	size_t count = 9U;

	setup(&fixture, 1U);
	assert(q35_mor_pci_guard_capture(NULL, ECAM_BASE, 1U, requesters,
		ARRAY_SIZE(requesters), &count) == CB_ERR_ARG);
	assert(q35_mor_pci_guard_capture(&io, 0, 1U, requesters,
		ARRAY_SIZE(requesters), &count) == CB_ERR_ARG);
	assert(q35_mor_pci_guard_capture(&io, ECAM_BASE, 0, requesters,
		ARRAY_SIZE(requesters), &count) == CB_ERR_ARG);
	assert(q35_mor_pci_guard_capture(&io, ECAM_BASE, 257U, requesters,
		ARRAY_SIZE(requesters), &count) == CB_ERR_ARG);
	assert(q35_mor_pci_guard_capture(&io, ECAM_BASE, 1U, requesters,
		0, &count) == CB_ERR_ARG);
	assert(q35_mor_pci_guard_validate(&io, ECAM_BASE, 1U, requesters,
		0) == CB_ERR_ARG);
	assert(q35_mor_pci_guard_requiesce(&io, ECAM_BASE, 1U, requesters,
		0) == CB_ERR_ARG);
}

int main(void)
{
	test_capture_fail_closed();
	test_capture_and_validate();
	test_requiesce_retained_topology();
	test_arguments();
	return 0;
}
