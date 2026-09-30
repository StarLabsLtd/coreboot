#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
source_file="$root/src/soc/intel/meteorlake/authvar_presence_boot_classifier.c"

mkdir -p "$temporary/include"
cat > "$temporary/include/config.h" <<'EOF'
#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0
#define CONFIG_ACPI_INTEL_HARDWARE_SLEEP_VALUES 1
#define CONFIG_ACPI_AMD_HARDWARE_SLEEP_VALUES 0
#define CONFIG_CONSOLE_OVERRIDE_LOGLEVEL 0
#define CONFIG_BOOTBLOCK_CONSOLE 0
#define CONFIG_DEBUG_FUNC 0
#define CONFIG_RTC 0
#define CONFIG_HAVE_SMI_HANDLER 0
#define CONFIG_HAVE_ACPI_RESUME 0
EOF

build()
{
	output=$1
	flags=$2
	source=${3:-$source_file}
	# Deliberate normal flag splitting for this strict host harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wshadow \
		-Wno-unused-parameter $flags -D__COREBOOT__ -D__TEST__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/commonlib/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/soc/intel/meteorlake/include" \
		-I"$root/src/soc/intel/common/pch/include" \
		"$root/tests/lib/mtl_authvar_presence_boot_classifier_test.c" \
		"$source" -o "$temporary/$output"
}

mutant()
{
	name=$1
	expression=$2
	mutant_source="$temporary/$name.c"

	sed "$expression" "$source_file" > "$mutant_source"
	if cmp -s "$source_file" "$mutant_source"; then
		printf 'mutant did not change source: %s\n' "$name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		if ! build "$name-O$optimization" "-O$optimization" "$mutant_source"; then
			printf 'MTL boot classifier mutant did not compile: %s/O%s\n' \
				"$name" "$optimization" >&2
			exit 1
		fi
		if "$temporary/$name-O$optimization" >/dev/null 2>&1; then
			printf 'MTL boot classifier mutant survived: %s/O%s\n' \
				"$name" "$optimization" >&2
			exit 1
		fi
	done
}

for optimization in 0 2; do
	build "plain-O$optimization" "-O$optimization"
	"$temporary/plain-O$optimization"
	build "sanitize-O$optimization" \
		"-O$optimization -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/sanitize-O$optimization"
done

build tsan "-O1 -g -fsanitize=thread"
TSAN_OPTIONS=halt_on_error=1 "$temporary/tsan"

mutant drop-global-reset \
	's/(GBL_RST_STS | HOST_RST_STS)/(HOST_RST_STS)/'
mutant drop-host-reset \
	's/(GBL_RST_STS | HOST_RST_STS)/(GBL_RST_STS)/'
mutant reset-after-wake \
	's/if (evidence->gen_pmcon_a & (GBL_RST_STS | HOST_RST_STS))/if ((evidence->gen_pmcon_a \& (GBL_RST_STS | HOST_RST_STS)) \&\& !wake)/'
mutant drop-wake-gate 's/if (wake) {/if ((void)wake, true) {/'
mutant admit-s3-mismatch \
	's/ && evidence->normalized_sleep_state == ACPI_S3//'
mutant admit-s4-mismatch \
	's/ && evidence->normalized_sleep_state == ACPI_S4//'
mutant admit-soft-s5 \
	's/if (power_failure &&/if ((void)power_failure,/'
mutant drop-pwr-flr 's/(PWR_FLR | SUS_PWR_FLR)/(SUS_PWR_FLR)/'
mutant drop-sus-pwr-flr 's/(PWR_FLR | SUS_PWR_FLR)/(PWR_FLR)/'
mutant causes-authoritative \
	's/if (evidence->gen_pmcon_a & (GBL_RST_STS | HOST_RST_STS))/if ((evidence->gen_pmcon_a \& (GBL_RST_STS | HOST_RST_STS)) || evidence->gblrst_cause[0] || evidence->gblrst_cause[1] || evidence->hpr_cause0)/'
mutant drop-recheck \
	'/if (memcmp(&first, &second, sizeof(first)))/,+1c\
\t(void)first;'
mutant allow-overlap \
	's/return first_base <= second_end && second_base <= first_end;/return first_base <= second_end \&\& second_base <= first_end \&\& false;/'

${CC:-cc} -m32 -march=i686 -std=gnu11 -Wall -Wextra -Werror \
	-Wshadow -Wno-unused-parameter -ffreestanding -fno-builtin \
	-D__COREBOOT__ -include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/commonlib/include" \
	-I"$root/src/arch/x86/include" \
	-I"$root/src/soc/intel/meteorlake/include" \
	-I"$root/src/soc/intel/common/pch/include" \
	-c "$source_file" -o "$temporary/classifier-32.o"
! nm -u "$temporary/classifier-32.o" | grep -q '__atomic_'

if rg -n 'endpoint|selector|route|transport|BOOT_STATE_INIT|mainboard_' \
	"$source_file" \
	"$root/src/soc/intel/meteorlake/include/soc/authvar_presence_boot_classifier.h"; then
	printf '%s\n' 'MTL presence boot classifier exceeded its dormant scope' >&2
	exit 1
fi

printf '%s\n' 'MTL authenticated-variable presence boot classifier: PASS'
