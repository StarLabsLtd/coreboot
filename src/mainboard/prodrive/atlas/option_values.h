/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef ATLAS_OPTION_VALUES_H
#define ATLAS_OPTION_VALUES_H

#include <option.h>

/* Alder Lake FSP UPD encodings. */
#define ATLAS_C_STATE_DEFAULT 254
#define ATLAS_C_STATE_AUTO 255
#define ATLAS_C_STATE_VALUES(X, ...) { \
	X("C0/C1", 0) \
	X("C2", 1) \
	X("C3", 2) \
	X("C6", 3) \
	X("C7", 4) \
	X("C7S", 5) \
	X("C8", 6) \
	X("C9", 7) \
	X("C10", 8) \
	X("Default", ATLAS_C_STATE_DEFAULT) \
	X("Auto", ATLAS_C_STATE_AUTO) \
	__VA_ARGS__ \
}

#define ATLAS_NUM_PCIE_SSC_SETTINGS 21
#define ATLAS_PCIE_SSC_AUTO 0xff

enum atlas_primary_display {
	ATLAS_DISPLAY_IGD,
	ATLAS_DISPLAY_PEG,
	ATLAS_DISPLAY_PCH,
	ATLAS_DISPLAY_AUTO,
	ATLAS_DISPLAY_HYBRID,
};

static inline unsigned int atlas_pkg_c_state_limit(void)
{
#define C_STATE_VALUE(label, value) value,
	static const unsigned int values[] = ATLAS_C_STATE_VALUES(C_STATE_VALUE);
#undef C_STATE_VALUE

	return get_uint_option_checked("pkg_c_state_limit", ATLAS_C_STATE_AUTO,
				       OPTION_ENUM_VALUES(values, ARRAY_SIZE(values)));
}

static inline unsigned int atlas_pcie_ssc(void)
{
	return get_uint_option_checked("pch_pcie_pll_ssc", ATLAS_PCIE_SSC_AUTO,
				       OPTION_RANGE(0, ATLAS_NUM_PCIE_SSC_SETTINGS - 1));
}

static inline unsigned int atlas_primary_display(void)
{
	const unsigned int fallback = CONFIG(ONBOARD_VGA_IS_PRIMARY) ?
		ATLAS_DISPLAY_IGD : ATLAS_DISPLAY_AUTO;

	/* Keep the FSP hybrid value available to callers that set the option directly. */
	return get_uint_option_checked("primary_display", fallback,
				       OPTION_RANGE(ATLAS_DISPLAY_IGD, ATLAS_DISPLAY_HYBRID));
}

#endif /* ATLAS_OPTION_VALUES_H */
