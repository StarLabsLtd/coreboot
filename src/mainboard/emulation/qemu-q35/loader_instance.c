/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/cpu.h>
#include <cpu/x86/smm_invocation_loader_composition.h>
#include <device/fw_cfg.h>
#include <random.h>
#include <string.h>

#if !ENV_RAMSTAGE
#error "Q35 native component loader instance is ramstage-only"
#endif

_Static_assert(CONFIG_MAX_CPUS == 1 && !CONFIG_HAVE_ACPI_RESUME,
	"Q35 native component requires one CPU and a non-S3 loader");

#define CPUID_RDRAND (1U << 30)

static uint32_t consumed;

enum cb_err smm_invocation_platform_loader_instance_take(
	struct smm_invocation_loader_instance_seed *seed)
{
	uint32_t expected = 0;

	if (!seed)
		return CB_ERR_ARG;
	memset(seed, 0, sizeof(*seed));
	if (!__atomic_compare_exchange_n(&consumed, &expected, 1, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;

	/* This correlation identity is not DMA or invocation-cause authority. */
	if (fw_cfg_max_cpus() != 1 || cpuid_get_max_func() < 1 ||
	    !(cpuid_ecx(1) & CPUID_RDRAND) ||
	    get_random_number_64(&seed->loader_instance_nonce.low) != CB_SUCCESS ||
	    get_random_number_64(&seed->loader_instance_nonce.high) != CB_SUCCESS ||
	    smm_invocation_loader_instance_nonce_is_zero(seed->loader_instance_nonce)) {
		memset(seed, 0, sizeof(*seed));
		return CB_ERR;
	}
	seed->revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION;
	seed->size = sizeof(*seed);
	seed->lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	return CB_SUCCESS;
}
