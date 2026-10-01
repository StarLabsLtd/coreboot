/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_invocation_topology.h>
#include "authvar_presence_bootstrap_install.h"
#include "dma_smm_receipt_provision.h"
#include <boot/coreboot_tables.h>
#include <boot/payload_mm_authvar_presence_publication.h>
#include <boot/payload_mm_authvar_presence_tuple_sender.h>
#include <console/console.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Coreboot's freestanding stdlib header omits these host process helpers. */
extern void exit(int status) __noreturn;
extern unsigned long strtoul(const char *text, char **end, int base);

static struct starbook_mtl_presence_bootstrap_frame frame;
static unsigned int scenario, frame_queries, emissions, triggers;

static void require(bool condition)
{
	if (!condition)
		exit(90);
}

enum cb_err payload_mm_authvar_presence_publication_loader_required(bool *required)
{
	*required = scenario != 1;
	return scenario == 2 ? CB_ERR : CB_SUCCESS;
}

bool starbook_mtl_dma_receipt_transport_frame(uintptr_t *base, size_t *size)
{
	frame_queries++;
	*base = (uintptr_t)&frame;
	*size = sizeof(frame);
	if (scenario == 3)
		return false;
	if (scenario == 4)
		(*size)--;
	if (scenario == 5)
		(*base)++;
	if (scenario == 6)
		*base = UINT32_MAX - sizeof(frame) + 2U;
	return true;
}

enum cb_err payload_mm_authvar_presence_tuple_sender_receipts_take(
	struct payload_mm_authvar_presence_bootstrap_receipts *receipts,
	struct payload_mm_authvar_presence_tuple_sender *sender)
{
	emissions++;
	memset(receipts, 0x36, sizeof(*receipts));
	memset(sender, 0, sizeof(*sender));
	return scenario == 7 ? CB_ERR : CB_SUCCESS;
}

uint64_t starbook_mtl_presence_bootstrap_trigger_test(uint32_t request, uint32_t address)
{
	triggers++;
	require(request == STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST);
	require(address == (uintptr_t)&frame);
	require(frame.revision == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REVISION);
	require(frame.size == sizeof(frame));
	require(frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST);
	require(!frame.reserved && !frame.initiator_cpu && !frame.maximum_cpus);
	frame.state = STARBOOK_MTL_PRESENCE_BOOTSTRAP_ACCEPTED;
	frame.maximum_cpus = CONFIG_MAX_CPUS;
	switch (scenario) {
	case 8: return 0;
	case 9: frame.revision++; break;
	case 10: frame.size--; break;
	case 11: frame.state = STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST; break;
	case 12: frame.reserved = 1; break;
	case 13: frame.initiator_cpu = 1; break;
	case 14: frame.maximum_cpus = 0; break;
	case 15: frame.maximum_cpus = CONFIG_MAX_CPUS + 1; break;
	case 16: ((unsigned char *)&frame.receipts)[0] ^= 1; break;
	}
	return STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS;
}

void die(const char *format, ...)
{
	(void)format;
	require(scenario >= 2 && scenario <= 16);
	require(frame_queries == (scenario == 2 ? 0U : 1U));
	require(emissions == (scenario >= 7 ? 1U : 0U));
	require(triggers == (scenario >= 8 ? 1U : 0U));
	exit(77);
}

int main(int argc, char **argv)
{
	struct starbook_mtl_presence_bootstrap_frame original;
	require(argc == 2);
	scenario = (unsigned int)strtoul(argv[1], NULL, 10);
	memset(&frame, 0x5a, sizeof(frame));
	original = frame;
	lb_board(NULL);
	require(scenario < 2);
	if (scenario == 1) {
		require(!frame_queries && !emissions && !triggers);
		require(!memcmp(&frame, &original, sizeof(frame)));
	} else {
		memset(&original, 0, sizeof(original));
		require(frame_queries == 1 && emissions == 1 && triggers == 1);
		require(!memcmp(&frame, &original, sizeof(frame)));
	}
	return 0;
}
