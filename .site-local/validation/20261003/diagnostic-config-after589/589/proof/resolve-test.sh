#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
proof=$1
candidate=$2
producer=/home/sean/q35-e8-scope-full.9sM2hH/full.config
p1_input=/home/sean/diagnostic-private-formatters-gate.MqmT1a/resolved.config
for profile in p0 p1; do
	for variant in baseline candidate; do
		source="$proof/baseline"
		if test "$variant" = candidate; then source=$candidate; fi
		input="$source/defconfig"
		coreboot=
		if test "$profile" = p1; then input=$p1_input; coreboot=$producer; fi
		output="$proof/$profile-$variant"
		make -C "$source" CDK2_BUILD_DIR="$output" \
			CDK2_CONFIG="$output/resolved.config" \
			CDK2_CONFIG_HEADER="$output/include/cdk2/config.h" \
			CDK2_DEFCONFIG="$input" COREBOOT_CONFIG="$coreboot" defconfig
		for file in resolved.config include/cdk2/config.h; do
			mkdir -p "$output/normalized/$(dirname "$file")"
			sed '/CDK2_LATE_LINK/d; /CDK2_CPU_TIMER/d; /CDK2_MAX_LOGICAL_PROCESSORS/d' \
				"$output/$file" > "$output/normalized/$file"
		done
	done
	for file in resolved.config include/cdk2/config.h; do
		cmp "$proof/$profile-baseline/normalized/$file" \
			"$proof/$profile-candidate/normalized/$file"
	done
	if grep -E 'CDK2_LATE_LINK|CDK2_CPU_TIMER|CDK2_MAX_LOGICAL_PROCESSORS' \
		"$proof/$profile-candidate/resolved.config" \
		"$proof/$profile-candidate/include/cdk2/config.h"; then
		echo 'Retired symbol retained after genuine resolution' >&2
		exit 1
	fi
	if test "$profile" = p0; then expected=0; else expected=1; fi
	grep -qx "#define CONFIG_CDK2_COREBOOT_CAPSULE_PROFILE $expected" \
		"$proof/$profile-candidate/include/cdk2/config.h"
done
sha256sum -c "$proof/source-before.sha256"
printf '%s\n' 'Actual P0/P1 resolution differs only in the three retired symbols: PASS'
