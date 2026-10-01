#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
cleanup()
{
	if [ "${KEEP_BOOT_PRIVATE_LEASE_TMP:-0}" = 1 ]; then
		printf 'BOOT-private held-lease artifacts: %s\n' "$temporary" >&2
	else
		rm -rf "$temporary"
	fi
}
trap cleanup EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_COMPOSITION 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_INSTANCE 1' \
	'#define CONFIG_SMM_INVOCATION_TOPOLOGY 1' \
	'#define CONFIG_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION 1' \
	'#define CONFIG_PAYLOAD_BOOT_PRIVATE_BUFFER 1' \
	'#define CONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT 1' \
	'#define CONFIG_BOOTMEM_ALIGNED_RESERVATIONS 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED 1' \
	'#define CONFIG_SMM_APMC_ROUTE_AUTHVAR_SERVICE 1' \
	'#define CONFIG_ECAM_MMCONF_BUS_NUMBER 2' \
	'#define CONFIG_ECAM_MMCONF_BASE_ADDRESS 0xc0000000' \
	'#define CONFIG_ECAM_MMCONF_LENGTH 0x200000' \
	> "$temporary/include/config.h"
make -C "$root" build-tests/lib/bootmem-aligned-reservation-test >/dev/null
config="$root/build/tests/tests/lib/bootmem-aligned-reservation-test"
common="-std=gnu23 -Wall -Wextra -Werror -Wshadow -fno-builtin
	-Wno-unused-parameter -Wno-sign-compare
	-D__COREBOOT__ -D__TEST__ -D__TEST_SRCOBJ__ -DBOOTMEM_RECEIPT_TEST
	-DSTARBOOK_MTL_DMA_SMM_TEST_ECAM_BUSES=2
	-include $root/src/include/kconfig.h -include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$config -I$root/tests/include/mocks -I$root/tests/include
	-I$root/src -I$root/src/include -I$root/src/commonlib/include
	-I$root/src/commonlib/bsd/include -I$root/src/arch/x86/include
	-I$root/src/soc/intel/common/block/include -I$root/build/tests
	-I$root/src/mainboard/starlabs/starbook/variants/mtl -no-pie"
sources="$root/tests/lib/starbook_mtl_boot_private_held_lease_test.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_receiver.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_policy.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_authority.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_requester_authority.c
	$root/src/soc/intel/common/block/vtd/vtd_translation.c
	$root/src/soc/intel/common/block/vtd/vtd_translation_verify.c
	$root/src/cpu/x86/smm_invocation_evidence.c
	$root/src/cpu/x86/smm_invocation_evidence_loader.c
	$root/src/lib/payload_boot_private_buffer.c
	$root/src/lib/bootmem.c $root/src/lib/bootmem_reservation_receipt.c
	$root/src/lib/memrange.c $root/src/device/device_util.c"
cases='valid ap-valid successor-refused arena-overlap bootstrap-denied bootstrap-ap-denied
	receipt-mutated bootstrap-drift evidence-alias runtime-denied claim-denied
	claim-drift table-drift ecam-drift routing-drift s3 close-before-use fresh-binding-denied
	close-at-publication view-drift-during-walk'
compile()
{
	flags=$1
	receiver=$2
	objects=
	for source in $sources; do
		case "$source" in
		*/bootmem.c|*/bootmem_reservation_receipt.c|*/payload_boot_private_buffer.c|\
		*/memrange.c|*/device_util.c|*/starbook_mtl_boot_private_held_lease_test.c)
			stage=RAMSTAGE ;;
		*) stage=SMM ;;
		esac
		if [ "$source" = "$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_receiver.c" ]; then
			source=$receiver
		fi
		object="$temporary/$(basename "$source").o"
		# Keep actual allocator compilation ramstage and receiver compilation SMM.
		# shellcheck disable=SC2086
		${CC:-cc} $common $flags -D__${stage}__ -c "$source" -o "$object"
		objects="$objects $object"
	done
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $objects -o "$temporary/test"
}

run_cases()
{
	for scenario in $cases; do
		UBSAN_OPTIONS=halt_on_error=1 "$temporary/test" "$scenario" || return 1
	done
}

receiver="$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_receiver.c"
for flags in '-O0' '-O2' '-O1 -g -fsanitize=undefined -fno-omit-frame-pointer'; do
	compile "$flags" "$receiver"
	run_cases
done
for mutation in geometry claim authority admission publication view; do
	case "$mutation" in
	geometry) expression='/if (!boot_private_geometry_valid() || !dependencies_valid/s/!boot_private_geometry_valid()/false/' ;;
	claim) expression='/^static enum cb_err boot_private_verify_held/,$s/memcmp(\&token, \&owner.boot_private.claimed, sizeof(token))/false/g' ;;
	authority) expression='/^static enum cb_err observer_verify_translation_boot_private/,/^static enum cb_err boot_private_verify_held/ {
/return starbook_mtl_dma_smm_authority_verify_live_policy/,+1c\
\treturn CB_SUCCESS;
}' ;;
	admission) expression='/^static enum cb_err boot_private_verify_held/,$s/!platform_payload_mm_authvar_service_runtime_admitted()/false/g' ;;
	publication) expression='/^[[:space:]]*expected = BOOT_PRIVATE_PREPARING;/,+3c\
\t__atomic_store_n(\&owner.boot_private.state, BOOT_PRIVATE_READY, __ATOMIC_RELEASE);' ;;
	view) expression='/^static enum cb_err boot_private_verify_held/,$ {
/smm_invocation_runtime_view_get(\&rechecked_view)/,+1c\
\t    false ||
/const struct smm_invocation_runtime_view \*rechecked_view;/d
}' ;;
	esac
	sed "$expression" "$receiver" > "$temporary/receiver-$mutation.c"
	! cmp -s "$receiver" "$temporary/receiver-$mutation.c"
	compile '-O2 -g -fsanitize=undefined' "$temporary/receiver-$mutation.c"
	if (run_cases) > "$temporary/$mutation.log" 2>&1; then
		printf 'BOOT-private held lease %s mutant survived\n' "$mutation" >&2
		exit 1
	fi
	grep -q 'BOOT-private lease oracle failure:' "$temporary/$mutation.log"
done
native_common=$(printf '%s' "$common" | sed 's/-D__TEST__ //')
# Compile the real SMM-only path, including physical reserved-receipt checks.
# This is an object/frame guard, not a release whole-callchain proof.
# shellcheck disable=SC2086
${CC:-cc} $native_common -m32 -D__SMM__ -O2 -ffreestanding -fstack-usage \
	-c "$receiver" -o "$temporary/native32-receiver.o"
awk -F '\t' '
	$1 ~ /:(boot_private_verify_held|starbook_mtl_boot_private_lease_prepare)$/ {
		if ($2 > 1024) exit 1
		found++
	}
	END { if (found != 2) exit 1 }
' "$temporary/native32-receiver.su"
if nm -u "$temporary/native32-receiver.o" | grep -E '__atomic_|__sync_'; then
	exit 1
fi
printf '%s\n' 'BOOT-private held SMM lease tests: PASS (modeled placement/admission/MMIO only)'
