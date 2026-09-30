#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
source="$root/src/cpu/x86/smm/smm_module_loader.c"
fixture="$root/tests/lib/payload_mm_authvar_canonical_arena_loader_test.sh"

# Execute the exact loader and real allocator instead of counting raw source
# statements across mutually exclusive legacy and canonical boot paths.
sh "$fixture"
mutation()
{
	name=$1
	expression=$2
	mutated="$temporary/$name.c"
	sed "$expression" "$source" > "$mutated"
	! cmp -s "$source" "$mutated"
	if CANONICAL_ARENA_LOADER_SOURCE="$mutated" sh "$fixture" \
		> "$temporary/$name.log" 2>&1; then
		printf 'loader mutation survived: %s\n' "$name" >&2
		exit 1
	fi
	# A compile failure is not evidence that the runtime fixture caught the defect.
	if grep -q 'error:' "$temporary/$name.log"; then
		printf 'loader mutation did not compile: %s\n' "$name" >&2
		cat "$temporary/$name.log" >&2
		exit 1
	fi
}
mutation no-canonical-generation \
	's/authvar_seed.cold_boot_generation = snapshot.binding.generation;/authvar_seed.cold_boot_generation = 1;/'
mutation owner-not-capability \
	's/memcpy(authvar_seed.owner, snapshot.binding.capability,/memset(authvar_seed.owner, 1,/'
mutation no-cold-resample \
	's/!mainboard_authvar_presence_cold_boot()/false/g'
mutation no-identity-resample \
	's/memcmp(\&identity, \&identity_check, sizeof(identity))/false/'
mutation no-topology-resample \
	's/memcmp(\&topology, \&topology_check, sizeof(topology))/false/'
mutation no-binding-resample \
	's/memcmp(\&snapshot, bootstrap, sizeof(snapshot))/false/'
mutation no-arena-cleanup \
	's/scrub_authvar_loader(published_arena, sizeof(\*published_arena));/(void)0;/'
mutation no-channel-cleanup \
	's/scrub_authvar_loader(published_channel, sizeof(\*published_channel));/(void)0;/'
echo 'Actual SMM arena loader failure-atomic mutation tests: PASS'
