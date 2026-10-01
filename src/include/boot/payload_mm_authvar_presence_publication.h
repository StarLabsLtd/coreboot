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

/* Validate completed same-boot service bootstrap and its exact published tuple. */
bool platform_payload_mm_authvar_service_published(
	const struct lb_header *header, uintptr_t table_end);

/* Register before bootmem initialization. */
enum cb_err payload_mm_authvar_presence_publication_reserve(void);
/* Read the immutable early admission decision, never rerun the board opt-in. */
enum cb_err payload_mm_authvar_presence_publication_loader_required(bool *required);

/* Append the endpoint as the final record within the exclusive table bound. */
enum cb_err lb_add_payload_mm_authvar_presence_endpoint(
	struct lb_header *header, uintptr_t table_end);

#if ENV_TEST
void payload_mm_authvar_presence_publication_reset_test(void);
void payload_mm_authvar_presence_publication_scrub_test_hook(
	const void *buffer, size_t size);
void payload_mm_authvar_presence_publication_pre_poison_test_hook(
	uint32_t observed_state);
void payload_mm_authvar_presence_publication_pre_reserve_claim_test_hook(void);
#endif

#endif
