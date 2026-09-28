/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <soc/authvar_presence_boot_classifier.h>
#include <soc/pm.h>
#include <soc/pmc.h>
#include <rules.h>
#include <stddef.h>
#include <string.h>

#if ENV_TEST
void mtl_authvar_presence_boot_classifier_test_hook(
	const struct chipset_power_state *power_state);
#define TEST_HOOK(power_state) \
	mtl_authvar_presence_boot_classifier_test_hook(power_state)
#else
#define TEST_HOOK(power_state) do { } while (0)
#endif

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool objects_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;
	const uintptr_t first_end = first_base + first_size - 1U;
	const uintptr_t second_end = second_base + second_size - 1U;

	return first_base <= second_end && second_base <= first_end;
}

static enum mtl_authvar_presence_boot_class classify(
	const struct mtl_authvar_presence_boot_evidence *evidence)
{
	const bool wake = evidence->pm1_sts & WAK_STS;
	const bool power_failure = evidence->gen_pmcon_a & (PWR_FLR | SUS_PWR_FLR);
	const uint32_t sleep_type =
		(evidence->pm1_cnt & SLP_TYP) >> SLP_TYP_SHIFT;

	/* Reset evidence takes precedence over a stale PM1 sleep value. */
	if (evidence->gen_pmcon_a & (GBL_RST_STS | HOST_RST_STS))
		return MTL_AUTHVAR_PRESENCE_BOOT_RESET;

	if (wake) {
		if (sleep_type == SLP_TYP_S3 && evidence->normalized_sleep_state == ACPI_S3)
			return MTL_AUTHVAR_PRESENCE_BOOT_S3;
		if (sleep_type == SLP_TYP_S4 && evidence->normalized_sleep_state == ACPI_S4)
			return MTL_AUTHVAR_PRESENCE_BOOT_S4;
		return MTL_AUTHVAR_PRESENCE_BOOT_UNKNOWN;
	}

	if (power_failure && evidence->normalized_sleep_state == ACPI_S5)
		return MTL_AUTHVAR_PRESENCE_BOOT_COLD;

	return MTL_AUTHVAR_PRESENCE_BOOT_UNKNOWN;
}

static struct mtl_authvar_presence_boot_evidence snapshot(
	const struct chipset_power_state *power_state)
{
	return (struct mtl_authvar_presence_boot_evidence) {
		.revision = MTL_AUTHVAR_PRESENCE_BOOT_EVIDENCE_REVISION,
		.size = sizeof(struct mtl_authvar_presence_boot_evidence),
		.classification = MTL_AUTHVAR_PRESENCE_BOOT_UNKNOWN,
		.pm1_sts = power_state->pm1_sts,
		.pm1_cnt = power_state->pm1_cnt,
		.gen_pmcon_a = power_state->gen_pmcon_a,
		.gblrst_cause = {
			power_state->gblrst_cause[0],
			power_state->gblrst_cause[1],
		},
		.hpr_cause0 = power_state->hpr_cause0,
		.normalized_sleep_state = power_state->prev_sleep_state,
	};
}

enum cb_err mtl_authvar_presence_boot_classify(
	const struct chipset_power_state *power_state,
	struct mtl_authvar_presence_boot_evidence *evidence)
{
	struct mtl_authvar_presence_boot_evidence first;
	struct mtl_authvar_presence_boot_evidence second;

	if (!object_valid(power_state, sizeof(*power_state), _Alignof(uint32_t)) ||
	    !object_valid(evidence, sizeof(*evidence), _Alignof(*evidence)) ||
	    objects_overlap(power_state, sizeof(*power_state), evidence,
		sizeof(*evidence)))
		return CB_ERR;

	first = snapshot(power_state);
	TEST_HOOK(power_state);
	second = snapshot(power_state);
	if (memcmp(&first, &second, sizeof(first)))
		return CB_ERR;

	second.classification = classify(&second);
	memcpy(evidence, &second, sizeof(*evidence));

	return CB_SUCCESS;
}
