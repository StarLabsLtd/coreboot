/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_H

#include <boot/payload_mm_authvar_presence.h>

#define PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_CONTEXT_MAX 128U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE 4096U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT 4096U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION 1U

/* Private, one-use ramstage-to-SMM input. It is not a wire or table ABI. */
struct payload_mm_authvar_presence_seed {
	uint32_t revision;
	uint32_t size;
	struct lb_authvar_presence_endpoint endpoint;
	uint8_t capability[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE];
};

typedef bool (*payload_mm_authvar_presence_producer_state_fn)(void *context);
typedef bool (*payload_mm_authvar_presence_producer_range_fn)(void *context,
	uint64_t base, uint64_t size);
typedef enum cb_err (*payload_mm_authvar_presence_producer_install_fn)(
	void *context, const struct payload_mm_authvar_presence_seed *seed);
typedef void (*payload_mm_authvar_presence_producer_close_fn)(void *context);

/* All callbacks and context are trusted platform composition inputs. */
struct payload_mm_authvar_presence_composition {
	uint32_t revision;
	uint32_t size;
	uint32_t trigger_address;
	uint32_t trigger_value;
	payload_mm_authvar_presence_producer_state_fn cold_boot;
	payload_mm_authvar_presence_producer_install_fn authority_install;
	payload_mm_authvar_presence_producer_close_fn authority_close;
	payload_mm_authvar_presence_producer_range_fn dma_protected;
	payload_mm_authvar_presence_producer_state_fn cpu_rendezvous_ready;
	payload_mm_authvar_presence_producer_state_fn cold_reset_ready;
	payload_mm_authvar_presence_producer_state_fn lifecycle_sealed;
	payload_mm_authvar_presence_producer_state_fn platform_ready;
	void *context;
	size_t context_size;
};

/* Register before bootmem initialization, then compose after it. */
enum cb_err payload_mm_authvar_presence_producer_reserve(void);
enum cb_err payload_mm_authvar_presence_producer_compose(
	const struct payload_mm_authvar_presence_composition *composition);

/* The sole publication boundary. Failure always leaves record zeroed. */
enum cb_err payload_mm_authvar_presence_producer_publication_take(
	struct lb_authvar_presence_endpoint *record);

/* Restriction-only rollback before publication. */
void payload_mm_authvar_presence_producer_abort(void);

#if ENV_TEST
void payload_mm_authvar_presence_producer_reset_test(void);
const void *payload_mm_authvar_presence_producer_test_state(size_t *size);
#endif

#endif
