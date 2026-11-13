#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

require_options()
{
	dsc=$1
	shift
	for option; do
		if ! grep -Eq "^[[:space:]]*DEFINE[[:space:]]+$option[[:space:]]*=" "$dsc"; then
			echo "DAVE source check: $dsc does not declare $option" >&2
			exit 1
		fi
	done
}

# Non-ignored untracked source can be included by the build without changing
# a Git tree. Check every initialized submodule independently, including ones
# whose parent metadata sets ignore=untracked. Do not remove the user's files.
check_untracked()
(
	cd "$1" || exit 1
	DAVE_SOURCE_SCAN_FILE=$(mktemp "${TMPDIR:-/tmp}/dave-source-scan.XXXXXXXX") || exit 1
	export DAVE_SOURCE_SCAN_FILE
	trap 'rm -f "$DAVE_SOURCE_SCAN_FILE"' EXIT HUP INT TERM
	git ls-files --others --exclude-standard -z > "$DAVE_SOURCE_SCAN_FILE" || exit 1
	if [ -s "$DAVE_SOURCE_SCAN_FILE" ]; then
		echo "DAVE source check: untracked source files in $PWD" >&2
		exit 1
	fi
	git submodule foreach --recursive '
		git ls-files --others --exclude-standard -z > "$DAVE_SOURCE_SCAN_FILE" || exit 1
		if [ -s "$DAVE_SOURCE_SCAN_FILE" ]; then
			echo "DAVE source check: untracked source files in $PWD" >&2
			exit 1
		fi
	' >/dev/null || exit 1
)

check_untracked "$1" || {
	echo "DAVE source check: unable to establish a clean source tree" >&2
	exit 1
}

require_options "$1/UefiPayloadPkg/UefiPayloadPkg.dsc" \
	STARLABS_DAVE COREBOOT_DAVE_VBOOT COREBOOT_DAVE_SRK_PIN
if [ "${2:-}" = "y" ]; then
	require_options "$1/UefiPayloadPkg/CorebootFixedMmService.dsc" \
		STARLABS_DAVE STATIC_SECURE_BOOT COREBOOT_DAVE_VBOOT COREBOOT_DAVE_SRK_PIN
fi
