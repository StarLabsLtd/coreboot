#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

extract_loader()
{
	awk '
		/^int smm_load_module\(/ { copying = 1 }
		copying {
			print
			line = $0
			opens += gsub(/{/, "{", line)
			line = $0
			closes += gsub(/}/, "}", line)
			if (opens && opens == closes)
				exit
		}
	' "$1"
}

check_loader()
{
	source=$1
	function_body="$temporary/function.c"

	extract_loader "$source" > "$function_body"
	test -s "$function_body" || return 1
	test "$(grep -c 'platform_payload_mm_authvar_smm_arena_required();' \
		"$function_body")" -eq 1 || return 1
	test "$(grep -c 'platform_payload_mm_authvar_mor_private_smi_required();' \
		"$function_body")" -eq 1 || return 1
	test "$(grep -c 'return -1;' "$function_body")" -eq 1 || return 1
	test "$(grep -c 'return 0;' "$function_body")" -eq 1 || return 1
	grep -q 'if (authvar_arena_started)' "$function_body" || return 1
	grep -q 'platform_payload_mm_authvar_smm_arena_abort();' \
		"$function_body" || return 1
	grep -q 'if (authvar_channel_required && !authvar_arena_required)' \
		"$function_body" || return 1
	test "$(grep -c 'scrub_authvar_loader(published_channel,' \
		"$function_body")" -eq 2 || return 1
	test "$(grep -c 'scrub_authvar_loader(published_arena,' \
		"$function_body")" -eq 2 || return 1
	test "$(grep -c 'scrub_authvar_loader(&authvar_seed,' \
		"$function_body")" -eq 2 || return 1
	test "$(grep -c 'scrub_authvar_loader(&authvar_arena,' \
		"$function_body")" -eq 2 || return 1
	awk '
		/authvar_channel_required && !authvar_arena_required/ { dependency = NR }
		/authvar_arena_started = true/ { started = NR }
		/platform_payload_mm_authvar_smm_arena_seed\(&authvar_seed\)/ { seed = NR }
		/scrub_authvar_loader\(published_arena,/ && !arena_zero { arena_zero = NR }
		/scrub_authvar_loader\(published_channel,/ && !channel_zero { channel_zero = NR }
		/smm_module_setup_stub\(/ { stub = NR }
		/payload_mm_authvar_mor_private_smi_loader_provision\(/ { provision = NR }
		/PAYLOAD_MM_AUTHVAR_SMM_ARENA_READY, __ATOMIC_RELEASE/ { publish = NR }
		END { exit !(dependency && started && seed && arena_zero && channel_zero &&
			stub && provision && publish && dependency < started && started < seed &&
			arena_zero < stub && channel_zero < stub && stub < provision &&
			provision < publish) }
	' "$function_body"
}

source="$root/src/cpu/x86/smm/smm_module_loader.c"
check_loader "$source"

mutant()
{
	name=$1
	expression=$2
	mutated="$temporary/$name.c"

	sed "$expression" "$source" > "$mutated"
	! cmp -s "$source" "$mutated"
	if check_loader "$mutated" >/dev/null 2>&1; then
		echo "ERROR: $name mutation survived" >&2
		exit 1
	fi
}

mutant early-return \
	'/if (rmodule_parse/,/goto fail;/{s/goto fail;/return -1;/}'
mutant unguarded-abort 's/if (authvar_arena_started)/if (true)/'
mutant no-arena-abort 's/platform_payload_mm_authvar_smm_arena_abort();/(void)0;/'
mutant no-dependency \
	's/authvar_channel_required \&\& !authvar_arena_required/false/'
mutant no-channel-cleanup \
	's/scrub_authvar_loader(published_channel, sizeof(\*published_channel));/(void)0;/'
mutant no-arena-cleanup \
	's/scrub_authvar_loader(published_arena, sizeof(\*published_arena));/(void)0;/'
mutant no-channel-zero \
	'0,/scrub_authvar_loader(published_channel,/{s/scrub_authvar_loader(published_channel,/(void)(published_channel, /;}'
mutant no-arena-zero \
	'0,/scrub_authvar_loader(published_arena,/{s/scrub_authvar_loader(published_arena,/(void)(published_arena, /;}'
mutant late-request-start \
	'/authvar_arena_started = true;/{h;d}; /platform_payload_mm_authvar_smm_arena_seed(\&authvar_seed)/{p;x}'
mutant seed-leak \
	's/scrub_authvar_loader(\&authvar_seed, sizeof(authvar_seed));//'
mutant receipt-leak \
	's/scrub_authvar_loader(\&authvar_arena, sizeof(authvar_arena));//'
mutant provision-missing \
	's/payload_mm_authvar_mor_private_smi_loader_provision(/payload_mm_authvar_mor_private_smi_send_install(/'
mutant stub-after-provision \
	'/if (smm_module_setup_stub/{N;h;d}; /if (payload_mm_authvar_mor_private_smi_loader_provision/{N;N;p;x}'
mutant arena-before-provision \
	'/if (payload_mm_authvar_mor_private_smi_loader_provision/{N;N;h;d}; /__atomic_store_n(\&published_arena->state,/{N;p;x}'

printf '%s\n' 'Payload-MM authvar SMM loader failure-atomic tests: PASS'
