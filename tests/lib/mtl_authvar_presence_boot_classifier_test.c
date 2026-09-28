/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <soc/authvar_presence_boot_classifier.h>
#include <soc/pm.h>
#include <soc/pmc.h>
#include <string.h>

static unsigned int mutation;

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

void mtl_authvar_presence_boot_classifier_test_hook(
	const struct chipset_power_state *power_state)
{
	struct chipset_power_state *mutable = (void *)power_state;

	switch (mutation) {
	case 1:
		mutable->pm1_sts ^= WAK_STS;
		break;
	case 2:
		mutable->pm1_cnt ^= 1U << SLP_TYP_SHIFT;
		break;
	case 3:
		mutable->gen_pmcon_a ^= PWR_FLR;
		break;
	case 4:
		mutable->gblrst_cause[0] ^= 1U;
		break;
	case 5:
		mutable->gblrst_cause[1] ^= 1U;
		break;
	case 6:
		mutable->hpr_cause0 ^= 1U;
		break;
	case 7:
		mutable->prev_sleep_state ^= 1U;
		break;
	}
}

static struct chipset_power_state state(uint16_t pm1_sts, uint32_t sleep_type,
	uint32_t gen_pmcon_a, uint32_t normalized)
{
	return (struct chipset_power_state) {
		.pm1_sts = pm1_sts,
		.pm1_cnt = sleep_type << SLP_TYP_SHIFT,
		.gen_pmcon_a = gen_pmcon_a,
		.prev_sleep_state = normalized,
	};
}

static struct mtl_authvar_presence_boot_evidence classify(
	const struct chipset_power_state *power_state)
{
	struct mtl_authvar_presence_boot_evidence evidence = { 0 };

	assert(mtl_authvar_presence_boot_classify(power_state, &evidence) ==
		CB_SUCCESS);
	assert(evidence.revision == MTL_AUTHVAR_PRESENCE_BOOT_EVIDENCE_REVISION);
	assert(evidence.size == sizeof(evidence));
	return evidence;
}

static void exact_snapshot(void)
{
	struct chipset_power_state power_state = state(WAK_STS | PWRBTN_STS,
		SLP_TYP_S4, 0x100U, ACPI_S4);
	struct chipset_power_state saved;
	struct mtl_authvar_presence_boot_evidence evidence;

	power_state.gblrst_cause[0] = 0x11223344U;
	power_state.gblrst_cause[1] = 0x55667788U;
	power_state.hpr_cause0 = 0x99aabbccU;
	saved = power_state;
	evidence = classify(&power_state);
	assert(!memcmp(&power_state, &saved, sizeof(power_state)));
	assert(evidence.classification == MTL_AUTHVAR_PRESENCE_BOOT_S4);
	assert(evidence.pm1_sts == power_state.pm1_sts);
	assert(evidence.pm1_cnt == power_state.pm1_cnt);
	assert(evidence.gen_pmcon_a == power_state.gen_pmcon_a);
	assert(evidence.gblrst_cause[0] == power_state.gblrst_cause[0]);
	assert(evidence.gblrst_cause[1] == power_state.gblrst_cause[1]);
	assert(evidence.hpr_cause0 == power_state.hpr_cause0);
	assert(evidence.normalized_sleep_state == ACPI_S4);
	assert(evidence.reserved == 0);
}

static uint32_t oracle(uint32_t reset, bool wake, uint32_t power,
	uint32_t sleep_type, uint32_t normalized)
{
	if (reset)
		return MTL_AUTHVAR_PRESENCE_BOOT_RESET;
	if (wake) {
		if (sleep_type == SLP_TYP_S3 && normalized == ACPI_S3)
			return MTL_AUTHVAR_PRESENCE_BOOT_S3;
		if (sleep_type == SLP_TYP_S4 && normalized == ACPI_S4)
			return MTL_AUTHVAR_PRESENCE_BOOT_S4;
		return MTL_AUTHVAR_PRESENCE_BOOT_UNKNOWN;
	}
	if (power && normalized == ACPI_S5)
		return MTL_AUTHVAR_PRESENCE_BOOT_COLD;
	return MTL_AUTHVAR_PRESENCE_BOOT_UNKNOWN;
}

static void cartesian_oracle(void)
{
	static const uint32_t resets[] = {
		0, GBL_RST_STS, HOST_RST_STS, GBL_RST_STS | HOST_RST_STS,
	};
	static const uint32_t powers[] = {
		0, PWR_FLR, SUS_PWR_FLR, PWR_FLR | SUS_PWR_FLR,
	};
	static const uint32_t sleeps[] = {
		SLP_TYP_S0, SLP_TYP_S3, SLP_TYP_S4, SLP_TYP_S5, 2U,
	};
	static const uint32_t normalized[] = {
		ACPI_S0, ACPI_S3, ACPI_S4, ACPI_S5, UINT32_MAX,
	};
	size_t reset;
	size_t wake;
	size_t power;
	size_t sleep;
	size_t previous;

	for (reset = 0; reset < ARRAY_SIZE(resets); reset++) {
		for (wake = 0; wake < 2; wake++) {
			for (power = 0; power < ARRAY_SIZE(powers); power++) {
				for (sleep = 0; sleep < ARRAY_SIZE(sleeps); sleep++) {
					for (previous = 0;
					     previous < ARRAY_SIZE(normalized); previous++) {
						struct chipset_power_state power_state = state(
							wake ? WAK_STS : 0, sleeps[sleep],
							resets[reset] | powers[power],
							normalized[previous]);
						const uint32_t expected = oracle(resets[reset],
							wake, powers[power], sleeps[sleep],
							normalized[previous]);

						assert(classify(&power_state).classification ==
							expected);
					}
				}
			}
		}
	}
}

static void reset_precedence(void)
{
	static const uint32_t resets[] = {
		GBL_RST_STS,
		HOST_RST_STS,
		GBL_RST_STS | HOST_RST_STS,
	};
	static const uint32_t sleeps[] = {
		SLP_TYP_S0, SLP_TYP_S1, SLP_TYP_S3, SLP_TYP_S4, SLP_TYP_S5, 2U,
	};
	static const uint32_t normalized[] = {
		ACPI_S0, ACPI_S3, ACPI_S4, ACPI_S5,
	};
	size_t reset;
	size_t wake;
	size_t sleep;
	size_t power;
	size_t previous;

	for (reset = 0; reset < ARRAY_SIZE(resets); reset++) {
		for (wake = 0; wake < 2; wake++) {
			for (sleep = 0; sleep < ARRAY_SIZE(sleeps); sleep++) {
				for (power = 0; power < 4; power++) {
					for (previous = 0;
					     previous < ARRAY_SIZE(normalized); previous++) {
						struct chipset_power_state power_state =
							state(wake ? WAK_STS : 0,
								sleeps[sleep], resets[reset] |
								(power & 1U ? PWR_FLR : 0) |
								(power & 2U ? SUS_PWR_FLR : 0),
								normalized[previous]);

						assert(classify(&power_state).classification ==
							MTL_AUTHVAR_PRESENCE_BOOT_RESET);
					}
				}
			}
		}
	}
}

static void sleep_and_cold_classes(void)
{
	struct chipset_power_state power_state;
	uint32_t sleep_type;
	uint32_t normalized;

	power_state = state(WAK_STS, SLP_TYP_S3, 0, ACPI_S3);
	assert(classify(&power_state).classification == MTL_AUTHVAR_PRESENCE_BOOT_S3);
	power_state = state(WAK_STS, SLP_TYP_S4, 0, ACPI_S4);
	assert(classify(&power_state).classification == MTL_AUTHVAR_PRESENCE_BOOT_S4);

	for (sleep_type = 0; sleep_type < 8; sleep_type++) {
		for (normalized = ACPI_S0; normalized <= ACPI_S5; normalized++) {
			uint32_t expected = MTL_AUTHVAR_PRESENCE_BOOT_UNKNOWN;

			if (sleep_type == SLP_TYP_S3 && normalized == ACPI_S3)
				expected = MTL_AUTHVAR_PRESENCE_BOOT_S3;
			if (sleep_type == SLP_TYP_S4 && normalized == ACPI_S4)
				expected = MTL_AUTHVAR_PRESENCE_BOOT_S4;
			power_state = state(WAK_STS, sleep_type,
				PWR_FLR | SUS_PWR_FLR, normalized);
			assert(classify(&power_state).classification == expected);
		}
	}

	power_state = state(0, SLP_TYP_S0, PWR_FLR, ACPI_S5);
	assert(classify(&power_state).classification == MTL_AUTHVAR_PRESENCE_BOOT_COLD);
	power_state = state(0, SLP_TYP_S3, SUS_PWR_FLR, ACPI_S5);
	assert(classify(&power_state).classification == MTL_AUTHVAR_PRESENCE_BOOT_COLD);
	power_state = state(0, SLP_TYP_S4, PWR_FLR | SUS_PWR_FLR, ACPI_S5);
	assert(classify(&power_state).classification == MTL_AUTHVAR_PRESENCE_BOOT_COLD);

	power_state = state(0, SLP_TYP_S5, 0, ACPI_S5);
	assert(classify(&power_state).classification == MTL_AUTHVAR_PRESENCE_BOOT_UNKNOWN);
	power_state = state(0, SLP_TYP_S5, PWR_FLR, ACPI_S0);
	assert(classify(&power_state).classification == MTL_AUTHVAR_PRESENCE_BOOT_UNKNOWN);
	power_state = state(WAK_STS, SLP_TYP_S5, PWR_FLR, ACPI_S5);
	assert(classify(&power_state).classification == MTL_AUTHVAR_PRESENCE_BOOT_UNKNOWN);
}

static void causes_are_diagnostic_only(void)
{
	struct chipset_power_state power_state = state(0, SLP_TYP_S5, 0, ACPI_S5);

	power_state.gblrst_cause[0] = UINT32_MAX;
	power_state.gblrst_cause[1] = UINT32_MAX;
	power_state.hpr_cause0 = UINT32_MAX;
	assert(classify(&power_state).classification == MTL_AUTHVAR_PRESENCE_BOOT_UNKNOWN);
	power_state.gen_pmcon_a = PWR_FLR;
	assert(classify(&power_state).classification == MTL_AUTHVAR_PRESENCE_BOOT_COLD);
}

static void invalid_arguments_do_not_publish(void)
{
	struct chipset_power_state power_state = state(0, SLP_TYP_S5, PWR_FLR, ACPI_S5);
	struct mtl_authvar_presence_boot_evidence evidence;
	struct mtl_authvar_presence_boot_evidence saved;
	union {
		struct chipset_power_state power_state;
		struct mtl_authvar_presence_boot_evidence evidence;
	} alias = { 0 };
	union {
		uint64_t alignment;
		uint8_t bytes[sizeof(struct chipset_power_state) +
			sizeof(struct mtl_authvar_presence_boot_evidence) + 8];
	} storage = { 0 };
	struct chipset_power_state *partial_state = (void *)storage.bytes;
	struct mtl_authvar_presence_boot_evidence *partial_evidence =
		(void *)(storage.bytes + 4);
	struct mtl_authvar_presence_boot_evidence *misaligned_evidence =
		(void *)(storage.bytes + 1);
	struct chipset_power_state *misaligned_state = (void *)(storage.bytes + 1);

	memset(&evidence, 0xa5, sizeof(evidence));
	saved = evidence;
	assert(mtl_authvar_presence_boot_classify(NULL, &evidence) == CB_ERR);
	assert(!memcmp(&evidence, &saved, sizeof(evidence)));
	assert(mtl_authvar_presence_boot_classify(&power_state, NULL) == CB_ERR);
	alias.power_state = power_state;
	assert(mtl_authvar_presence_boot_classify(&alias.power_state,
		&alias.evidence) == CB_ERR);
	*partial_state = power_state;
	assert(mtl_authvar_presence_boot_classify(partial_state,
		partial_evidence) == CB_ERR);
	assert(mtl_authvar_presence_boot_classify(&power_state,
		misaligned_evidence) == CB_ERR);
	assert(mtl_authvar_presence_boot_classify(misaligned_state, &evidence) == CB_ERR);
	assert(mtl_authvar_presence_boot_classify(
		(void *)(UINTPTR_MAX - sizeof(power_state) + 2U), &evidence) == CB_ERR);
	assert(mtl_authvar_presence_boot_classify(
		(void *)(UINTPTR_MAX - sizeof(power_state) + 1U),
		(void *)(UINTPTR_MAX - sizeof(power_state) + 1U)) == CB_ERR);
	assert(mtl_authvar_presence_boot_classify(
		(void *)(UINTPTR_MAX - sizeof(power_state) + 1U),
		(void *)(UINTPTR_MAX - sizeof(power_state) + 5U)) == CB_ERR);
	assert(!memcmp(&evidence, &saved, sizeof(evidence)));
}

static void synchronous_mutation_fails_untouched(void)
{
	unsigned int field;

	for (field = 1; field <= 7; field++) {
		struct chipset_power_state power_state =
			state(0, SLP_TYP_S5, PWR_FLR, ACPI_S5);
		struct mtl_authvar_presence_boot_evidence evidence;
		struct mtl_authvar_presence_boot_evidence saved;

		memset(&evidence, 0x5a, sizeof(evidence));
		saved = evidence;
		mutation = field;
		assert(mtl_authvar_presence_boot_classify(&power_state,
			&evidence) == CB_ERR);
		mutation = 0;
		assert(!memcmp(&evidence, &saved, sizeof(evidence)));
	}
}

int main(void)
{
	exact_snapshot();
	cartesian_oracle();
	reset_precedence();
	sleep_and_cold_classes();
	causes_are_diagnostic_only();
	invalid_arguments_do_not_publish();
	synchronous_mutation_fails_untouched();
	return 0;
}
