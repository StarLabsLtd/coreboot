#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ulimit -c 0

compile()
{
	optimization=$1
	source=$2
	output=$3
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin \
		-fno-pie -no-pie "$optimization" -g -fno-omit-frame-pointer \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"-DBROKER_MEDIA_SOURCE=\"$source\"" \
		"$root/tests/lib/capsule_broker_media_transaction_test.c" \
		"$root/src/lib/capsule_broker_endpoint.c" \
		"$root/src/lib/payload_mm_fmp_owner_layout.c" \
		"$root/src/lib/capsule_update_backend.c" -o "$output"
}

for mutation in begin-release end-release partial-pair end-precurrent; do
	awk -v mutation="$mutation" '
		mutation == "begin-release" && $0 == "\tmedia->end(media->context);" {
			print "\t/* MEDIA_BEGIN_RELEASE_DISCARDED */"; count++; next
		}
		mutation == "end-release" && $0 == "\tstatus = media->end(media->context);" {
			print "\tstatus = CB_SUCCESS; (void)media; /* MEDIA_END_RELEASE_DISCARDED */"; count++; next
		}
		mutation == "partial-pair" && $0 == "\t    !snapshot.media.sync || (!!snapshot.media.begin != !!snapshot.media.end) ||" {
			print "\t    !snapshot.media.sync || /* MEDIA_PAIR_GUARD_DISCARDED */"; count++; next
		}
		mutation == "end-precurrent" && $0 == "\tcurrent = guarded_media_current(owned);" {
			print "\tcurrent = true; /* MEDIA_PRECURRENT_DISCARDED */"; count++; next
		}
		{ print }
		END { if (count != 1) exit 1 }
	' "$root/src/lib/capsule_broker.c" > "$temporary/$mutation.c"
	awk '
		$0 == "\t/* MEDIA_BEGIN_RELEASE_DISCARDED */" {
			print "\tmedia->end(media->context);"; count++; next
		}
		$0 == "\tstatus = CB_SUCCESS; (void)media; /* MEDIA_END_RELEASE_DISCARDED */" {
			print "\tstatus = media->end(media->context);"; count++; next
		}
		$0 == "\t    !snapshot.media.sync || /* MEDIA_PAIR_GUARD_DISCARDED */" {
			print "\t    !snapshot.media.sync || (!!snapshot.media.begin != !!snapshot.media.end) ||"; count++; next
		}
		$0 == "\tcurrent = true; /* MEDIA_PRECURRENT_DISCARDED */" {
			print "\tcurrent = guarded_media_current(owned);"; count++; next
		}
		{ print }
		END { if (count != 1) exit 1 }
	' "$temporary/$mutation.c" > "$temporary/restored.c"
	cmp "$root/src/lib/capsule_broker.c" "$temporary/restored.c"
done

for optimization in -O0 -O2; do
	compile "$optimization" "$root/src/lib/capsule_broker.c" "$temporary/test"
	for mode in happy partial-pair begin-failure begin-current-loss \
		begin-context-mutation begin-end-mutation begin-close reentry \
		end-failure end-context-mutation end-restores-current apply-failure apply-current-loss; do
		"$temporary/test" "$mode"
		printf 'Media forwarding %s %s: PASS\n' "$optimization" "$mode"
	done
	for mutation in begin-release end-release partial-pair end-precurrent; do
		case "$mutation" in
			begin-release) scenario=begin-current-loss; assertion='ends ==';;
			end-release) scenario=happy; assertion='ends ==';;
			partial-pair) scenario=partial-pair; assertion='capsule_broker_policy_install';;
			end-precurrent) scenario=end-restores-current; assertion='guarded_end(&context) == CB_ERR';;
		esac
		compile "$optimization" "$temporary/$mutation.c" "$temporary/mutant"
		status=0
		"$temporary/mutant" "$scenario" > "$temporary/negative.log" 2>&1 || status=$?
		if [ "$status" -ne 134 ] ||
		   ! grep -F "MEDIA_TRANSACTION_ASSERT: $assertion" "$temporary/negative.log" ||
		   grep -E 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' "$temporary/negative.log"; then
			printf 'Wrong media mutation refusal: %s (%s)\n' "$mutation" "$status" >&2
			exit 1
		fi
		printf 'Media forwarding %s causal %s: PASS\n' "$optimization" "$mutation"
	done
done
