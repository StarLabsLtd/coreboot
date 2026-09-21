#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

legacy_symbols='capsule_broker_handle(
capsule_broker_authenticate_intent(
capsule_broker_checkpoint_grant(
payload_mm_fmp_checkpoint_commit('
for symbol in $legacy_symbols; do
	if grep -R -F "$symbol" "$root/src/lib" "$root/src/include"; then
		printf '%s\n' "legacy capsule route remains: $symbol" >&2
		exit 1
	fi
done

cases='endpoint fmap-compressed install-storage install-read-scratch-protection
install-write-scratch-protection install-communication install-staging install-dma
install-spi install-raw install-rendezvous install-scratch install-scratch-oversize
install-geometry install-write-scratch install-write-scratch-oversize
install-write-read-overlap install-write-communication
install-write-staging install-write-media-overlap install-write-hash-overlap
install-write-auth-overlap
install-image-size
install-scratch-communication install-scratch-staging install-region-count
install-fmap-count install-fmap-count-large install-fmap-name
install-fmap-name-empty install-fmap-flags install-fmap-range
install-fmap-route install-fmap-immutable install-fmap-duplicate
install-overlap-scratch-media install-overlap-scratch-hash
install-overlap-scratch-auth install-overlap-media-hash
install-overlap-media-auth install-overlap-hash-auth
install-missing-hash install-media-context-null install-media-context-large
install-media-context-staging install-sha-context-null install-sha-context-large
install-sha-context-staging install-proof-context-null
install-missing-authenticate install-authenticate-context-null
install-authenticate-context-large install-proof-context-large
install-authenticate-context-staging install-proof-context-staging
authenticate-happy authenticate-source-mutation authenticate-context-mutation
authenticate-check-only authenticate-set-one-shot authenticate-reject
authenticate-image-mutation authenticate-before-callback-mutation
authenticate-raw-zero authenticate-raw-oob authenticate-raw-overflow
authenticate-raw-wrong-size authenticate-raw-source-mutation
authenticate-raw-output-mutation
authenticate-lsv-high authenticate-raw-reserved
authenticate-owner-mutation authenticate-guard authenticate-digest
authenticate-generation authenticate-image-size authenticate-operation
authenticate-flags authenticate-revision authenticate-size
authenticate-transaction authenticate-algorithm authenticate-digest-size
authenticate-reserved authenticate-version authenticate-closed
authenticate-unstaged
authenticate-reenter authenticate-callback-grant authenticate-callback-apply
authenticate-callback-close authenticate-callback-mutation
proof-close-1 proof-close-2 proof-close-3 proof-close-4 proof-close-5
proof-reenter-1 proof-reenter-2 proof-reenter-3 proof-reenter-4 proof-reenter-5
hash-close-1 hash-close-2 hash-reenter-1 hash-reenter-2
no-grant grant-transaction grant-version digest guard
preflight preflight-range preflight-smmstore
preflight-policy
media-erase media-write media-short-write media-sync media-source-guard
media-read media-verify
stale close s3
policy-snapshot grant-edges grant-max generation-match'
cases="$cases bound-happy bound-sequence bound-digest bound-unstaged
bound-mutation bound-mutation-generation bound-mutation-transaction
bound-mutation-version"
cases="$cases source-mutation"
cases="$cases success-happy success-generation success-transaction
success-sequence success-digest success-close success-write-failure
success-output-overlap success-output-misaligned"

run_test()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin "$@" \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/capsule_broker_test.c" \
		"$root/src/lib/capsule_broker.c" \
		"$root/src/lib/capsule_broker_endpoint.c" \
		"$root/src/lib/payload_mm_fmp_owner_layout.c" \
		"$root/src/lib/capsule_update_backend.c" \
		-o "$temporary/$name"
	"$temporary/$name"
	for test_case in $cases; do
		"$temporary/$name" "$test_case"
	done
}

run_test ordinary
run_test optimized -O2
run_test strict -O2 -Wconversion -Wsign-conversion
run_test sanitized -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
printf '%s\n' 'Capsule broker O0/O2/strict/ASan+UBSan hostile cases: PASS'
