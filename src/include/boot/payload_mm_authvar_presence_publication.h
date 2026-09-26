/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION_H

#include <boot/payload_mm_authvar_presence_producer.h>
#include <commonlib/bsd/cb_err.h>
#include <commonlib/coreboot_tables.h>
#include <stdbool.h>

/* Trusted platform opt-in. The weak default is inert. */
bool platform_payload_mm_authvar_presence_required(void);
bool platform_payload_mm_authvar_presence_composition(
	struct payload_mm_authvar_presence_composition *composition);

/* Register before bootmem initialization. */
enum cb_err payload_mm_authvar_presence_publication_reserve(void);

/* Compose after bootmem resolution and append exactly one endpoint record. */
enum cb_err lb_add_payload_mm_authvar_presence_endpoint(
	struct lb_header *header);

#if ENV_TEST
void payload_mm_authvar_presence_publication_reset_test(void);
void payload_mm_authvar_presence_publication_scrub_test_hook(
	const void *buffer, size_t size);
void payload_mm_authvar_presence_publication_pre_poison_test_hook(
	uint32_t observed_state);
void payload_mm_authvar_presence_publication_pre_reserve_claim_test_hook(void);
#endif

#endif
