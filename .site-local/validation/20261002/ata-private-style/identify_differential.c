/* SPDX-License-Identifier: GPL-2.0-only */
#include <cdk2/ata_bus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EFI_STATUS baseline_parse(const UINT8 *, struct cdk2_ata_bus_media *);

int main(void)
{
	UINT8 identify[512];
	struct cdk2_ata_bus_media baseline, candidate;
	unsigned long state = 0x6ac82957;

	for (size_t iteration = 0; iteration < 100000; iteration++) {
		for (size_t byte = 0; byte < sizeof(identify); byte++) {
			state ^= state << 13;
			state ^= state >> 7;
			state ^= state << 17;
			identify[byte] = state;
		}
		/* Keep both 28-bit and 48-bit IDENTIFY branches well represented. */
		identify[99] |= 0x02;
		identify[166] = 0;
		identify[167] = iteration & 1 ? 0 : 0x44;
		if (iteration % 3 == 0) {
			identify[206] = 0;
			identify[207] = 0;
		}
		if (iteration % 5 == 0) {
			identify[510] = 0xa5;
			unsigned char sum = 0;
			for (size_t byte = 0; byte < 511; byte++)
				sum += identify[byte];
			identify[511] = -sum;
		}
		memset(&baseline, 0x5a, sizeof(baseline));
		memset(&candidate, 0x5a, sizeof(candidate));
		EFI_STATUS expected = baseline_parse(identify, &baseline);
		EFI_STATUS actual = cdk2_ata_bus_parse_identify(identify, &candidate);
		if (actual != expected || memcmp(&baseline, &candidate, sizeof(baseline)))
			abort();
	}
	puts("100000 IDENTIFY baseline/candidate status+media comparisons: PASS");
	return 0;
}
