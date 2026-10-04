#!/bin/bash
set -euo pipefail
source=/home/sean/Documents/.coreboot-worktrees/tpm-fifo-command-ready-after7ee
receipt=/home/sean/tpm-fifo-command-ready-host-gates.XFCZpH
arguments=()
configs=()
for argument in "$@"; do
	case "$argument" in
	-I*)
		if test -f "${argument#-I}/config.h"; then
			configs+=("${argument#-I}/config.h")
		fi
		;;
	esac
	if test "${CAUSAL_OLD_PREDICATE:-0}" = 1 &&
	   test "$argument" = "$source/src/drivers/pc80/tpm/tis.c"; then
		argument=$receipt/old-tis.c
	fi
	arguments+=("$argument")
done
invocation=$(mktemp -d "$receipt/compiler-inputs.XXXXXX")
printf '%q ' /usr/bin/gcc -I"$source/src/drivers/pc80/tpm" "${arguments[@]}" > "$invocation/command"
printf '\n' >> "$invocation/command"
test "${#configs[@]}" -gt 0
sha256sum "${configs[@]}" > "$invocation/config-before.sha256"
for config in "${configs[@]}"; do
	cp "$config" "$invocation/$(basename "$(dirname "$config")")-config.h"
done
status=0
/usr/bin/gcc -I"$source/src/drivers/pc80/tpm" "${arguments[@]}" || status=$?
printf '%s\n' "$status" > "$invocation/compile.status"
sha256sum -c "$invocation/config-before.sha256" > "$invocation/config-after-check.log"
exit "$status"
