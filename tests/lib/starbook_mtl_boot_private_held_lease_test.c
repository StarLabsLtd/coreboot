/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Join the actual bootmem/receipt owner and actual DMA authority walker.
 * MMIO, protected placement, and held all-CPU admission are host models;
 * this fixture does not establish selected hardware or resumed-CPU authority.
 */
#define initialize bootmem_fixture_initialize
#define main bootmem_fixture_main
#include "bootmem_aligned_reservation_test.c"
#undef main
#undef initialize

#define initialize dma_fixture_initialize
#define main dma_fixture_main
#define payload_mm_sha256 dma_fixture_sha256
#define smm_invocation_runtime_range_is_protected dma_fixture_range_is_protected
#include "starbook_mtl_dma_smm_authority_integration_test.c"
#undef smm_invocation_runtime_range_is_protected
#undef payload_mm_sha256
#undef main
#undef initialize

#include <boot/payload_boot_private_buffer.h>
#include <boot/payload_mm_authvar_service_receiver.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_install.h"

int dprintf(int descriptor, const char *format, ...);

#undef assert
#define assert(condition) do { \
	if (!(condition)) { \
		dprintf(2, "BOOT-private lease oracle failure: %d\n", __LINE__); \
		abort(); \
	} \
} while (0)

static bool bootstrap_admitted = true;
static bool runtime_admitted = true;
static bool claimed_drift;
static bool ap_initiator;
static unsigned int admission_reads;
static struct smm_invocation_token wave_token;
static uint64_t wave_value;
static uint8_t wave_command;
static bool close_at_publication;
static bool view_drift_during_walk;
static bool view_drift_observed;
static struct payload_mm_authvar_range *delivery_source_drift;

static bool delivery_ordinary_dram_range(void *context, uint64_t base, size_t size)
{
	return ordinary_dram_range(context, base, size) ||
		(size == 65536U && base >= 0x100000U && base <= 0x1000000U - size);
}

void starbook_mtl_boot_private_lease_test_hook(void)
{
	if (close_at_publication)
		starbook_mtl_boot_private_lease_close();
}

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base, size_t size)
{
	/* Both handles model protected placement; identity must still remain fixed. */
	if (view_drift_observed && (uintptr_t)view == 0x1234U)
		return base && size && base != &frame ? CB_SUCCESS : CB_ERR;
	return dma_fixture_range_is_protected(view, base, size);
}

enum payload_mm_verify_status payload_mm_sha256(const void *message,
	size_t message_size, uint8_t digest[32])
{
	enum payload_mm_verify_status status = dma_fixture_sha256(message, message_size, digest);

	if (delivery_source_drift && evidence.command == SMM_APMC_AUTHVAR_SERVICE &&
	    (uintptr_t)message >= TABLE_BASE &&
	    (uintptr_t)message < TABLE_BASE + TABLE_PAGES * PAGE_SIZE) {
		delivery_source_drift->base += 65536;
		delivery_source_drift = NULL;
	}
	if (view_drift_during_walk && evidence.command == SMM_APMC_AUTHVAR_SERVICE &&
	    (uintptr_t)message >= TABLE_BASE &&
	    (uintptr_t)message < TABLE_BASE + TABLE_PAGES * PAGE_SIZE) {
		runtime_view = (void *)0x5678;
		view_drift_observed = true;
	}
	return status;
}

bool platform_payload_mm_authvar_service_bootstrap_admitted(void)
{
	if (claimed_drift && ++admission_reads > 1U)
		evidence.token.rendezvous_digest[0] ^= 1U;
	return bootstrap_admitted && evidence.command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE;
}

bool platform_payload_mm_authvar_service_runtime_admitted(void)
{
	if (claimed_drift && ++admission_reads > 1U)
		evidence.token.rendezvous_digest[0] ^= 1U;
	return runtime_admitted && evidence.command == SMM_APMC_AUTHVAR_SERVICE;
}

static enum smm_invocation_match wave_match(void *context, uint32_t cpu, uint8_t command)
{
	assert(context == &wave_value);
	return command == wave_command && cpu == (ap_initiator ? 1U : 0U) ?
		SMM_INVOCATION_MATCHED : SMM_INVOCATION_NOT_MATCHED;
}

static enum cb_err wave_read(void *context, uint32_t cpu, uint64_t *value)
{
	assert(context == &wave_value && cpu == (ap_initiator ? 1U : 0U));
	*value = wave_value;
	return CB_SUCCESS;
}

static enum cb_err wave_write(void *context, uint32_t cpu, uint64_t value)
{
	assert(context == &wave_value && cpu == (ap_initiator ? 1U : 0U));
	wave_value = value;
	return CB_SUCCESS;
}

static const struct smm_invocation_save_state_ops wave_ops = {
	.match_apmc_write = wave_match, .read_value = wave_read, .write_value = wave_write,
	.context = &wave_value, .context_size = sizeof(wave_value),
};

static enum cb_err wave_begin(uint8_t command, uint64_t sentinel)
{
	struct smm_invocation_admission_token admission;
	uint64_t generation;

	assert(smm_invocation_evidence_entry_ready(&evidence));
	assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
		instance.loader_instance_nonce, instance.lifecycle, &admission) ==
		SMM_INVOCATION_TRY_SUCCESS);
	for (uint32_t cpu = 0; cpu < topology.active_cpus; cpu++)
		assert(smm_invocation_evidence_arrive_try(&evidence, cpu,
			topology.initial_apic_ids[cpu], &generation, &admission) ==
			SMM_INVOCATION_TRY_SUCCESS);
	for (uint32_t cpu = 0; cpu < topology.active_cpus; cpu++)
		assert(smm_invocation_evidence_rendezvous_ack_try(&evidence, generation,
			cpu, &admission) == SMM_INVOCATION_TRY_SUCCESS);
	wave_command = command;
	wave_value = sentinel;
	return smm_invocation_evidence_claim(&evidence, command, sentinel, &wave_ops, &wave_token);
}

static void wave_finish(void)
{
	uint64_t generation = wave_token.smi_generation;

	assert(smm_invocation_evidence_publish_and_request_close(&evidence,
		&wave_token, 0, &wave_ops) == CB_SUCCESS);
	assert(wave_value == 0);
	for (uint32_t cpu = 0; cpu < topology.active_cpus; cpu++)
		assert(smm_invocation_evidence_depart_try(&evidence, cpu, generation) ==
			SMM_INVOCATION_TRY_SUCCESS);
	assert(smm_invocation_evidence_eos_consume(&evidence, generation,
		instance.loader_instance_nonce, instance.lifecycle, topology.bsp_cpu));
}

int main(int argc, char **argv)
{
	struct bootmem_aligned_reservation_handle handle;
	struct bootmem_reservation_receipt_authority signer = { 0 }, verifier = { 0 };
	struct bootmem_reservation_receipt receipt;
	struct bootmem_aligned_reservation reservation;
	uint8_t secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE] = { 1 };
	const char *scenario;

	assert(argc == 2);
	scenario = argv[1];
	if (!strcmp(scenario, "arena-overlap"))
		resources[0].size = 0x740000;
	assert(payload_boot_private_buffer_reserve(&handle) == 0);
	assert(bootmem_reservation_receipt_provision(&signer, &verifier, secret,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, 11, &handle) == CB_SUCCESS);
	bootmem_fixture_initialize();
	assert(!bootmem_aligned_reservation_query(&handle, &reservation));
	assert(payload_boot_private_buffer_emit(&handle, &signer, &receipt) == CB_SUCCESS);
	dma_fixture_initialize(false);
	dependencies.ordinary_dram_range = delivery_ordinary_dram_range;
	assert(starbook_mtl_dma_receipt_provision_receive(&ops) == CB_SUCCESS);
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION, .size = sizeof(seed),
		.lifecycle = instance.lifecycle, .active_cpus = topology.active_cpus,
		.bsp_cpu = topology.bsp_cpu, .loader_instance_nonce = instance.loader_instance_nonce,
	};

	for (uint32_t cpu = 0; cpu < topology.active_cpus; cpu++)
		topology.initial_apic_ids[cpu] = cpu;
	memcpy(seed.participant_apic_ids, topology.initial_apic_ids,
		topology.active_cpus * sizeof(seed.participant_apic_ids[0]));
	/* Construct the native owner once; subsequent waves never reset it. */
	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	for (unsigned int wave = 0; wave < 7; wave++) {
		assert(wave_begin(SMM_APMC_AUTHVAR_SERVICE, SMM_APMC_AUTHVAR_SERVICE) == CB_SUCCESS);
		wave_finish();
	}
	if (!strcmp(scenario, "bootstrap-ap-denied"))
		ap_initiator = true;
	enum cb_err bootstrap_claim = wave_begin(SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		(0x123000ULL << 32) | STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST);

	if (ap_initiator) {
		assert(bootstrap_claim != CB_SUCCESS);
		assert(starbook_mtl_boot_private_lease_prepare(&verifier, &receipt) != CB_SUCCESS);
		return 0;
	}
	assert(bootstrap_claim == CB_SUCCESS);
	if (!strcmp(scenario, "evidence-alias")) {
		const struct smm_invocation_evidence before = evidence;

		assert(starbook_mtl_boot_private_lease_prepare(&verifier,
			(void *)&evidence) != CB_SUCCESS);
		assert(!memcmp(&before, &evidence, sizeof(before)));
		assert(starbook_mtl_boot_private_lease_prepare(&verifier, &receipt) != CB_SUCCESS);
		return 0;
	}
	if (!strcmp(scenario, "bootstrap-denied"))
		bootstrap_admitted = false;
	else if (!strcmp(scenario, "receipt-mutated"))
		receipt.mac[0] ^= 1;
	else if (!strcmp(scenario, "bootstrap-drift"))
		claimed_drift = true;
	else if (!strcmp(scenario, "close-at-publication"))
		close_at_publication = true;
	if (!strcmp(scenario, "arena-overlap") || !strcmp(scenario, "bootstrap-denied") ||
	    !strcmp(scenario, "bootstrap-ap-denied") ||
	    !strcmp(scenario, "receipt-mutated") ||
	    !strcmp(scenario, "bootstrap-drift") || !strcmp(scenario, "close-at-publication")) {
		assert(starbook_mtl_boot_private_lease_prepare(&verifier, &receipt) != CB_SUCCESS);
		assert(starbook_mtl_boot_private_lease_prepare(&verifier, &receipt) != CB_SUCCESS);
		return 0;
	}
	assert(starbook_mtl_boot_private_lease_prepare(&verifier, &receipt) == CB_SUCCESS);
	assert(authority_terminal_and_scrubbed(&verifier));
	assert(zero(&receipt, sizeof(receipt)));
	wave_finish();
	admission_reads = 0;
	if (!strcmp(scenario, "ap-valid"))
		ap_initiator = true;
	assert(wave_begin(SMM_APMC_AUTHVAR_SERVICE, SMM_APMC_AUTHVAR_SERVICE) == CB_SUCCESS);
	if (!strcmp(scenario, "runtime-denied"))
		runtime_admitted = false;
	else if (!strcmp(scenario, "claim-denied"))
		evidence.token.revision = 0;
	else if (!strcmp(scenario, "claim-drift"))
		claimed_drift = true;
	else if (!strcmp(scenario, "view-drift-during-walk"))
		view_drift_during_walk = true;
	else if (!strcmp(scenario, "table-drift"))
		*(volatile uint64_t *)TABLE_BASE ^= 0x1000U;
	else if (!strcmp(scenario, "ecam-drift"))
		*config32(0, 0xa0, 8) ^= 0x100U;
	else if (!strcmp(scenario, "routing-drift"))
		*config32(0, 0x31, 0x18) ^= 0x100U;
	else if (!strcmp(scenario, "s3"))
		instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	else if (!strcmp(scenario, "close-before-use"))
		starbook_mtl_boot_private_lease_close();
	else if (!strcmp(scenario, "fresh-binding-denied")) {
		struct starbook_mtl_dma_smm_binding binding = { .receipt = (void *)0x12345678 };

		assert(starbook_mtl_dma_smm_binding_get(&binding) != CB_SUCCESS);
		assert(binding.receipt == (void *)0x12345678);
	}
	if (!strncmp(scenario, "delivery-", 9)) {
		struct payload_mm_authvar_range communication = { .base = 0x400000, .size = 65536 };
		bool valid = !strcmp(scenario, "delivery-valid") ||
			!strcmp(scenario, "delivery-source-drift") ||
			!strcmp(scenario, "delivery-successor") ||
			!strcmp(scenario, "delivery-close") ||
			!strcmp(scenario, "delivery-view-drift");

		if (!strcmp(scenario, "delivery-size"))
			communication.size--;
		else if (!strcmp(scenario, "delivery-overflow"))
			communication.base = UINT64_MAX - 65535U;
		else if (!strcmp(scenario, "delivery-arena"))
			communication.base = 0x800000;
		else if (!strcmp(scenario, "delivery-private"))
			communication.base = reservation.base;
		else if (!strcmp(scenario, "delivery-table"))
			communication.base = TABLE_BASE;
		else if (!strcmp(scenario, "delivery-mmio"))
			communication.base = GFX_BASE;
		else if (!strcmp(scenario, "delivery-source-drift-during-walk"))
			delivery_source_drift = &communication;
		else if (!strcmp(scenario, "delivery-recheck-first")) {
			assert(platform_payload_mm_authvar_service_delivery_held(
				PAYLOAD_MM_AUTHVAR_DELIVERY_RECHECK, &communication) != CB_SUCCESS);
			return 0;
		}
		assert((platform_payload_mm_authvar_service_delivery_held(
			PAYLOAD_MM_AUTHVAR_DELIVERY_BEGIN, &communication) == CB_SUCCESS) == valid);
		if (!valid) {
			assert(starbook_mtl_boot_private_lease_begin_held() != CB_SUCCESS);
			return 0;
		}
		if (!strcmp(scenario, "delivery-valid")) {
			assert(platform_payload_mm_authvar_service_delivery_held(
				PAYLOAD_MM_AUTHVAR_DELIVERY_RECHECK, &communication) == CB_SUCCESS);
			starbook_mtl_boot_private_lease_close();
			return 0;
		}
		if (!strcmp(scenario, "delivery-source-drift"))
			communication.base += 65536;
		else if (!strcmp(scenario, "delivery-successor")) {
			wave_finish();
			assert(wave_begin(SMM_APMC_AUTHVAR_SERVICE, SMM_APMC_AUTHVAR_SERVICE) == CB_SUCCESS);
		} else if (!strcmp(scenario, "delivery-close"))
			starbook_mtl_boot_private_lease_close();
		else if (!strcmp(scenario, "delivery-view-drift"))
			view_drift_during_walk = true;
		assert(platform_payload_mm_authvar_service_delivery_held(
			PAYLOAD_MM_AUTHVAR_DELIVERY_RECHECK, &communication) != CB_SUCCESS);
		assert(starbook_mtl_boot_private_lease_begin_held() != CB_SUCCESS);
		return 0;
	}
	if (strcmp(scenario, "valid") && strcmp(scenario, "ap-valid") &&
	    strcmp(scenario, "successor-refused")) {
		assert(starbook_mtl_boot_private_lease_begin_held() != CB_SUCCESS);
		if (view_drift_during_walk)
			assert(view_drift_observed);
		assert(starbook_mtl_boot_private_lease_begin_held() != CB_SUCCESS);
		return 0;
	}
	assert(starbook_mtl_boot_private_lease_begin_held() == CB_SUCCESS);
	assert(starbook_mtl_boot_private_lease_recheck_held() == CB_SUCCESS);
	if (!strcmp(scenario, "successor-refused")) {
		wave_finish();
		assert(wave_begin(SMM_APMC_AUTHVAR_SERVICE, SMM_APMC_AUTHVAR_SERVICE) == CB_SUCCESS);
		assert(starbook_mtl_boot_private_lease_recheck_held() != CB_SUCCESS);
	} else {
		starbook_mtl_boot_private_lease_close();
	}
	assert(starbook_mtl_boot_private_lease_begin_held() != CB_SUCCESS);
	assert(starbook_mtl_boot_private_lease_recheck_held() != CB_SUCCESS);
	assert(starbook_mtl_boot_private_lease_prepare(&verifier, &receipt) != CB_SUCCESS);
	return 0;
}
