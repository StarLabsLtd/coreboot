#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
cat > "$temporary/include/config.h" <<'EOF'
#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0
#define CONFIG_PAYLOAD_MM_FMP_OWNER_AUTHVAR 1
#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 1
#define CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR 1
EOF

for optimization in 0 2; do
	"${HOSTCC:-cc}" -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wshadow -Wstrict-prototypes -fno-builtin \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_executor_test.c" \
		"$root/tests/lib/payload_mm_authvar_fmp_executor_stubs.c" \
		"$root/src/lib/payload_mm_authvar_executor.c" \
		"$root/src/lib/payload_mm_authvar_coordinator.c" \
		"$root/src/lib/payload_mm_authvar_set_preflight.c" \
		"$root/src/lib/payload_mm_authvar_controlled_mode.c" \
		"$root/src/lib/payload_mm_authvar_view.c" \
		"$root/src/lib/payload_mm_authvar_authority.c" \
		"$root/src/lib/payload_mm_authvar_candidate.c" \
		"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_certdb.c" \
		"$root/src/lib/payload_mm_authvar_mode.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		"$root/src/lib/payload_mm_authvar_route.c" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" \
		"$root/src/lib/payload_mm_fmp_state.c" \
		"$root/src/lib/payload_mm_fmp_owner.c" \
		-o "$temporary/test-O$optimization"
	for test_case in clean generic-get generic-get-sha-armed \
		coordinator-native-ordinary coordinator-native-ordinary-sha-armed \
		undersize-arena fmp-layout fmp-initialize-empty \
		fmp-initialize-suffixed-isolation \
		fmp-initialize-policy-live fmp-initialize-policy-sealed \
		fmp-initialize-source-modes-invalid \
		fmp-initialize-ready-to-boot fmp-initialize-runtime \
		fmp-initialize-wrong-size-combined \
		fmp-initialize-wrong-size-combined-zero \
		fmp-initialize-wrong-size-combined-oversize \
		fmp-initialize-wrong-size-legacy \
		fmp-initialize-wrong-size-legacy-zero \
		fmp-initialize-wrong-size-legacy-oversize \
		fmp-initialize-legacy-wrong-attributes \
		fmp-initialize-combined-malformed-legacy \
		fmp-initialize-duplicate fmp-initialize-legal-transition \
		fmp-initialize-lsv-below-trusted \
		fmp-initialize-lsv-equal-trusted \
		fmp-initialize-lsv-above-trusted \
		fmp-initialize-final-end-mutate-authoritative \
		fmp-initialize-final-end-mutate-published \
		fmp-initialize-reentry-1 fmp-initialize-reentry-2 \
		fmp-initialize-reentry-3 fmp-initialize-reentry-4 \
		fmp-initialize-reentry-5 fmp-initialize-reentry-6 \
		fmp-read-does-not-migrate fmp-cas-does-not-migrate \
		fmp-initialize-authoritative-end-fail \
		fmp-initialize-final-begin-fail \
		fmp-initialize-final-read-fail \
		fmp-initialize-final-end-fail \
		fmp-initialize-cleanup-fail-begin \
		fmp-initialize-cleanup-fail-read \
		fmp-initialize-cleanup-fail-program \
		fmp-initialize-cleanup-fail-end \
		fmp-initialize-combined-wins \
		fmp-required-size-minus-one fmp-offset-corrupt \
		fmp-policy-live-identity fmp-policy-live-current \
		fmp-policy-live-candidate fmp-policy-sealed-identity \
		fmp-policy-sealed-current fmp-policy-sealed-candidate \
		fmp-policy-sealed-observation \
		fmp-read fmp-success fmp-replace fmp-current fmp-end-failure \
		fmp-reclaim fmp-reclaim-tandem-padding-program \
		fmp-recovery-abort-old fmp-recovery-replay-new \
		fmp-recovery-malformed \
		fmp-transient-read fmp-transient-program fmp-transient-erase \
		fmp-malformed-attributes fmp-malformed-size fmp-malformed-data \
		fmp-malformed-absent-padding fmp-trusted-lsv-floor \
		fmp-monotonic-lsv \
		fmp-runtime-read fmp-runtime-compare fmp-busy-reentry \
		fmp-callback-mutate-current fmp-callback-mutate-candidate \
		fmp-callback-mutate-published \
		fmp-callback-mutate-published-fail \
		fmp-admission-invalid-operation fmp-admission-read-current \
		fmp-admission-read-candidate fmp-admission-null-published \
		fmp-admission-null-current fmp-admission-null-candidate \
		fmp-admission-misaligned-published \
		fmp-admission-misaligned-current \
		fmp-admission-misaligned-candidate \
		fmp-admission-unprotected-published \
		fmp-admission-unprotected-current \
		fmp-admission-unprotected-candidate \
		fmp-admission-alias-current-candidate \
		fmp-admission-alias-current-published \
		fmp-admission-alias-candidate-published \
		fmp-admission-arena-published fmp-admission-arena-current \
		fmp-admission-arena-candidate fmp-admission-media-published \
		fmp-admission-media-current fmp-admission-media-candidate \
		fmp-admission-owner-published fmp-admission-owner-current \
		fmp-admission-owner-candidate \
		fmp-admission-state-owner-published-full \
		fmp-admission-state-owner-published-partial \
		fmp-admission-state-owner-current-full \
		fmp-admission-state-owner-current-partial \
		fmp-admission-state-owner-candidate-full \
		fmp-admission-state-owner-candidate-partial \
		fmp-admission-state-authority-published-full \
		fmp-admission-state-authority-published-partial \
		fmp-admission-state-authority-current-full \
		fmp-admission-state-authority-current-partial \
		fmp-admission-state-authority-candidate-full \
		fmp-admission-state-authority-candidate-partial \
		fmp-admission-authvar-owner-published-full \
		fmp-admission-authvar-owner-published-partial \
		fmp-admission-authvar-owner-current-full \
		fmp-admission-authvar-owner-current-partial \
		fmp-admission-authvar-owner-candidate-full \
		fmp-admission-authvar-owner-candidate-partial \
		fmp-invalid-transition fmp-end-mutate-0 fmp-end-mutate-1 \
		fmp-end-mutate-2 fmp-end-mutate-3 fmp-tandem-program \
		fmp-control-phase-corrupt fmp-control-digest-corrupt \
		fmp-tandem-tail-program fmp-tandem-read fmp-tandem-end \
		fmp-tandem-tail-end \
		fmp-sha-failure fmp-active-first-persist \
		generic-authvar-owner-output-full \
		generic-authvar-owner-output-partial; do
		ASAN_OPTIONS=detect_leaks=1 "$temporary/test-O$optimization" "$test_case"
	done
	for mask in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
		ASAN_OPTIONS=detect_leaks=1 "$temporary/test-O$optimization" \
			"fmp-initialize-mask-$mask"
	done
	slot=0
	while [ "$slot" -le 3 ]; do
		ASAN_OPTIONS=detect_leaks=1 "$temporary/test-O$optimization" \
			"fmp-initialize-odd-attrs-$slot"
		ASAN_OPTIONS=detect_leaks=1 "$temporary/test-O$optimization" \
			"fmp-initialize-combined-odd-attrs-$slot"
		slot=$((slot + 1))
	done
	classification=0
	while [ "$classification" -le 80 ]; do
		ASAN_OPTIONS=detect_leaks=1 "$temporary/test-O$optimization" \
			"fmp-initialize-classifications-$classification"
		classification=$((classification + 1))
	done
done

"${HOSTCC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror \
	-Wshadow -Wstrict-prototypes -fno-builtin \
	-fsanitize=address,undefined -fno-sanitize-recover=all \
	-D__TEST__ -D__COREBOOT__ -D__SMM__ -DEXECUTOR_REAL_MEDIA \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
	-I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	"$root/tests/lib/payload_mm_authvar_executor_test.c" \
	"$root/tests/lib/payload_mm_authvar_fmp_executor_stubs.c" \
	"$root/src/lib/payload_mm_authvar.c" \
	"$root/src/lib/payload_mm_authvar_runtime.c" \
	"$root/src/lib/payload_mm_authvar_media.c" \
	"$root/src/lib/payload_mm_authvar_executor.c" \
	"$root/src/lib/payload_mm_authvar_coordinator.c" \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" \
	"$root/src/lib/payload_mm_authvar_controlled_mode.c" \
	"$root/src/lib/payload_mm_authvar_view.c" \
	"$root/src/lib/payload_mm_authvar_authority.c" \
	"$root/src/lib/payload_mm_authvar_candidate.c" \
	"$root/src/lib/payload_mm_authvar_bundle.c" \
	"$root/src/lib/payload_mm_authvar_certdb.c" \
	"$root/src/lib/payload_mm_authvar_mode.c" \
	"$root/src/lib/payload_mm_authvar_format.c" \
	"$root/src/lib/payload_mm_authvar_route.c" \
	"$root/src/lib/payload_mm_authvar_fv.c" \
	"$root/src/lib/payload_mm_authvar_ftw.c" \
	"$root/src/lib/payload_mm_authvar_store.c" \
	"$root/src/lib/payload_mm_authvar_store_semantics.c" \
	"$root/src/lib/payload_mm_authvar_record.c" \
	"$root/src/lib/payload_mm_authvar_writer.c" \
	"$root/src/lib/payload_mm_fmp_state.c" \
	"$root/src/lib/payload_mm_fmp_owner.c" \
	-o "$temporary/real-media"
reset_count=$(ASAN_OPTIONS=detect_leaks=1 "$temporary/real-media" \
	count-fmp-initialize)
test "$reset_count" -gt 0
cut=1
while [ "$cut" -le "$reset_count" ]; do
	ASAN_OPTIONS=detect_leaks=1 "$temporary/real-media" \
		"reset-fmp-initialize-$cut"
	cut=$((cut + 1))
done
reclaim_reset_count=$(ASAN_OPTIONS=detect_leaks=1 "$temporary/real-media" \
	count-fmp-initialize-reclaim)
test "$reclaim_reset_count" -gt 0
cut=1
while [ "$cut" -le "$reclaim_reset_count" ]; do
	ASAN_OPTIONS=detect_leaks=1 "$temporary/real-media" \
		"reset-fmp-initialize-reclaim-$cut"
	cut=$((cut + 1))
done
printf '%s\n' 'Payload-MM FMP executor workspace O0/O2 ASan+UBSan build: PASS'
