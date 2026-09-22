#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
manifest="$root/tests/lib/payload_mm_authvar_edk2_2609_semantics.tsv"

grep -qx '# oracle	StarLabsLtd/edk2	26.09	aab7b589fc59b7e2b8fb7eb79519bf1a5e5a5272' \
	"$manifest"
[ "$(grep -c '^# blob	' "$manifest")" -eq 8 ]
[ "$(grep -vc '^#' "$manifest")" -ge 60 ]
[ "$(awk -F '\t' '!/^#/ && NF != 7 { bad++ } END { print bad + 0 }' "$manifest")" -eq 0 ]
[ "$(awk -F '\t' '!/^#/ && NR > 1 { seen[$1]++ } END { \
	for (id in seen) if (seen[id] != 1) bad++; print bad + 0 }' \
	"$manifest")" -eq 0 ]
for area in ordinary authentication secure-boot private-auth mode name \
	enumeration read quota journal lifecycle; do
	awk -F '\t' -v wanted="$area" '$2 == wanted { found = 1 } END { exit !found }' \
		"$manifest"
done
for term in PK KEK db dbx dbt certdb certdbv SetupMode SecureBoot AuditMode \
	DeployedMode VendorKeys CustomMode SHA256 SHA384 SHA512 APPEND QUERY \
	READY_TO_BOOT ENTER_RUNTIME SVA S3 LegacyBoot; do
	grep -q "$term" "$manifest"
done

printf '%s\n' 'EDK2 26.09 authenticated-variable semantic manifest: PASS'
