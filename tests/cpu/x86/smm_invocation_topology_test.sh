#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	> "$temporary/include/config.h"

build()
{
	output=$1
	flags=$2
	source=${3:-$root/src/cpu/x86/smm_invocation_topology.c}
	# Deliberate normal flag splitting for this strict host harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-pthread $flags -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/cpu/x86/smm_invocation_topology_test.c" "$source" \
		-o "$temporary/$output"
}

for optimization in 0 2; do
	build "plain-O$optimization" "-O$optimization"
	"$temporary/plain-O$optimization"
	build "sanitize-O$optimization" \
		"-O$optimization -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/sanitize-O$optimization"
done

stale_reload="$temporary/stale-reload.c"
sed '0,/^\tsmm_invocation_topology_scrub(topology);$/{s//\t(void)topology;/}' \
	"$root/src/cpu/x86/smm_invocation_topology.c" > "$stale_reload"
if cmp -s "$stale_reload" "$root/src/cpu/x86/smm_invocation_topology.c"; then
	printf '%s\n' 'stale-reload mutant did not change source' >&2
	exit 1
fi
for optimization in 0 2; do
	build "stale-reload-O$optimization" "-O$optimization" "$stale_reload"
	if "$temporary/stale-reload-O$optimization" >/dev/null 2>&1; then
		printf 'surviving mutant: stale-reload O%s\n' "$optimization" >&2
		exit 1
	fi
done

build tsan "-O1 -g -fsanitize=thread"
TSAN_OPTIONS=halt_on_error=1 "$temporary/tsan"

${CC:-cc} -m32 -march=i686 -std=gnu11 -Wall -Wextra -Werror \
	-Wconversion -Wshadow -ffreestanding -fno-builtin -D__COREBOOT__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" -c \
	"$root/src/cpu/x86/smm_invocation_topology.c" -o "$temporary/topology-32.o"
if nm -u "$temporary/topology-32.o" | grep -q '__atomic_'; then
	printf '%s\n' '32-bit topology object gained libatomic dependency' >&2
	exit 1
fi
ld -m elf_i386 -r "$temporary/topology-32.o" -o "$temporary/topology-32-linked.o"

mutate()
{
	name=$1
	expression=$2
	mutant="$temporary/$name.c"
	sed "$expression" "$root/src/cpu/x86/smm_invocation_topology.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/cpu/x86/smm_invocation_topology.c"; then
		printf 'mutant did not change source: %s\n' "$name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		build "$name-O$optimization" "-O$optimization" "$mutant"
		if "$temporary/$name-O$optimization" >/dev/null 2>&1; then
			printf 'surviving mutant: %s O%s\n' "$name" "$optimization" >&2
			exit 1
		fi
	done
}

mutate exact-bsp 's/if (initial_apic_id == builder->bsp_apic_id)/if (true)/'
mutate bsp-zero 's/topology->bsp_cpu != 0U/false/'
mutate exact-map 's/memcmp(topology->initial_apic_ids, installed_apic_ids,/memcmp(installed_apic_ids, installed_apic_ids,/'
mutate release-ready 's/TOPOLOGY_TEST_HOOK(2);/__atomic_store_n(\&topology->state, SMM_INVOCATION_TOPOLOGY_READY, __ATOMIC_RELEASE); TOPOLOGY_TEST_HOOK(2);/'
mutate full-width 's/\*installed_apic_id = initial_apic_id;/\*installed_apic_id = (uint16_t)initial_apic_id;/'
mutate active-bound \
	's/topology->active_cpus > SMM_INVOCATION_TOPOLOGY_MAX_CPUS/false/'
mutate duplicate-id 's/topology->initial_apic_ids\[cpu\] == initial_apic_id/false/'
mutate installed-count 's/installed_cpus != builder->active_cpus/((void)installed_cpus, false)/'
mutate runtime-count 's/runtime_cpus != builder->active_cpus/((void)runtime_cpus, false)/'
mutate revision-binding 's/topology->revision != SMM_INVOCATION_TOPOLOGY_REVISION/false/'
mutate size-binding 's/topology->size != sizeof(\*topology)/false/'
mutate active-binding 's/topology->active_cpus != builder->active_cpus/false/'
mutate reserved-zero 's/topology->reserved\[1\] ||/false ||/'
mutate loader-fail-scrub 's/if (result)/if (false)/'

failure_scrub="$temporary/failure-scrub.c"
sed '/if (builder)/,/smm_invocation_topology_scrub(topology);/{s/smm_invocation_topology_scrub(topology);/(void)topology;/}' \
	"$root/src/cpu/x86/smm_invocation_topology.c" > "$failure_scrub"
if cmp -s "$failure_scrub" "$root/src/cpu/x86/smm_invocation_topology.c"; then
	printf '%s\n' 'failure-scrub mutant did not change source' >&2
	exit 1
fi
for optimization in 0 2; do
	build "failure-scrub-O$optimization" "-O$optimization" "$failure_scrub"
	if "$temporary/failure-scrub-O$optimization" >/dev/null 2>&1; then
		printf 'surviving mutant: failure-scrub O%s\n' "$optimization" >&2
		exit 1
	fi
done

grep -q '^config SMM_INVOCATION_TOPOLOGY$' "$root/src/cpu/x86/Kconfig"
if rg -q 'select[[:space:]]+SMM_INVOCATION_TOPOLOGY' "$root/src"; then
	printf '%s\n' 'topology gained a platform selector' >&2
	exit 1
fi
if rg -q 'lb_new_record|lb_add|CBMEM|APM_CNT|outb' \
	"$root/src/cpu/x86/smm_invocation_topology.c" \
	"$root/src/include/cpu/x86/smm_invocation_topology.h"; then
	printf '%s\n' 'topology gained a public endpoint or command' >&2
	exit 1
fi
topology_pod="$temporary/topology-pod.txt"
sed -n '/^struct smm_invocation_topology {/,/^} __aligned(8);/p' \
	"$root/src/include/cpu/x86/smm_invocation_topology.h" > "$topology_pod"
if rg -q '\*|callback|generation|uintptr_t|size_t' "$topology_pod"; then
	printf '%s\n' 'protected topology is not pointer-free POD' >&2
	exit 1
fi
if test "$(rg -c 'dev->path\.apic\.initial_lapicid' \
	"$root/src/cpu/x86/smm/smm_module_loader.c")" -ne 1; then
	printf '%s\n' 'APIC IDs are not captured by one authoritative read' >&2
	exit 1
fi
grep -q 'smihandler_params->num_cpus != params->num_cpus' \
	"$root/src/cpu/x86/smm/smm_module_loader.c"
grep -q 'return smm_invocation_topology_loader_result(published_topology, -1);' \
	"$root/src/cpu/x86/smm/smm_module_loader.c"
stub_setup_line=$(grep -n 'smm_stub_place_staggered_entry_points(params);' \
	"$root/src/cpu/x86/smm/smm_module_loader.c" | cut -d: -f1)
publish_line=$(grep -n 'smm_invocation_topology_publish(&topology_builder' \
	"$root/src/cpu/x86/smm/smm_module_loader.c" | cut -d: -f1)
test "$stub_setup_line" -lt "$publish_line"
if rg -q 'smm_invocation_topology_(begin|append|publish)' "$root/src" \
	-g '!src/cpu/x86/smm/smm_module_loader.c' \
	-g '!src/cpu/x86/smm_invocation_topology.c' \
	-g '!src/include/cpu/x86/smm_invocation_topology.h'; then
	printf '%s\n' 'topology gained a production consumer' >&2
	exit 1
fi

printf '%s\n' 'SMM invocation topology tests passed'
