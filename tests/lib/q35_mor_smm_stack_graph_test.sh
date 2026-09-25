#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/q35-mor-smm-stack.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
fixture=$temporary/fixture.ci
graph=$root/tests/lib/q35_mor_smm_stack_graph.awk

cat > "$fixture" <<'EOF'
node: { title: "src/cpu/x86/smm/smm_module_handler.c:smm_handler_start" label: "smm_handler_start\nsrc/cpu/x86/smm/smm_module_handler.c:1:1\n16 bytes (static)" }
node: { title: "payload_mm_authvar_mor_private_smi_dispatch" label: "payload_mm_authvar_mor_private_smi_dispatch\nsrc/lib/payload_mm_authvar_mor_private_smi_receiver.c:1:1\n32 bytes (static)" }
node: { title: "src/lib/payload_mm_authvar_mor_private_smi_receiver.c:bootstrap_receive" label: "bootstrap_receive\nsrc/lib/payload_mm_authvar_mor_private_smi_receiver.c:2:1\n64 bytes (dynamic,bounded)" }
node: { title: "platform_payload_mm_authvar_mor_private_smi_bootstrap" label: "platform_payload_mm_authvar_mor_private_smi_bootstrap\nsrc/include/boot/payload_mm_authvar_mor_private_smi.h:1:1" shape: ellipse }
node: { title: "src/mainboard/emulation/qemu-q35/mor_platform_smm.c:platform_payload_mm_authvar_mor_private_smi_bootstrap" label: "platform_payload_mm_authvar_mor_private_smi_bootstrap\nsrc/mainboard/emulation/qemu-q35/mor_platform_smm.c:1:1\n128 bytes (static)" }
node: { title: "payload_mm_authvar_smm_bootstrap_install" label: "payload_mm_authvar_smm_bootstrap_install\nsrc/lib/payload_mm_authvar_smm_bootstrap.c:1:1\n256 bytes (dynamic,bounded)" }
node: { title: "src/lib/payload_mm_authvar_smm_bootstrap.c:media_revalidate" label: "media_revalidate\nsrc/lib/payload_mm_authvar_smm_bootstrap.c:3:1\n32 bytes (static)" }
node: { title: "src/lib/payload_mm_authvar_smm_media_qemu.c:media_facts" label: "media_facts\nsrc/lib/payload_mm_authvar_smm_media_qemu.c:1:1\n16 bytes (static)" }
node: { title: "src/lib/payload_mm_authvar_smm_media_qemu.c:media_install" label: "media_install\nsrc/lib/payload_mm_authvar_smm_media_qemu.c:2:1\n16 bytes (static)" }
node: { title: "__indirect_call" label: "__indirect_call\nfixture:1:1" shape: ellipse }
edge: { sourcename: "src/cpu/x86/smm/smm_module_handler.c:smm_handler_start" targetname: "payload_mm_authvar_mor_private_smi_dispatch" label: "src/cpu/x86/smm/smm_module_handler.c:261:24" }
edge: { sourcename: "payload_mm_authvar_mor_private_smi_dispatch" targetname: "src/lib/payload_mm_authvar_mor_private_smi_receiver.c:bootstrap_receive" label: "src/lib/payload_mm_authvar_mor_private_smi_receiver.c:656:9" }
edge: { sourcename: "src/lib/payload_mm_authvar_mor_private_smi_receiver.c:bootstrap_receive" targetname: "platform_payload_mm_authvar_mor_private_smi_bootstrap" label: "receiver.c:1:1" }
edge: { sourcename: "src/mainboard/emulation/qemu-q35/mor_platform_smm.c:platform_payload_mm_authvar_mor_private_smi_bootstrap" targetname: "payload_mm_authvar_smm_bootstrap_install" label: "platform.c:1:1" }
edge: { sourcename: "payload_mm_authvar_smm_bootstrap_install" targetname: "src/lib/payload_mm_authvar_smm_bootstrap.c:media_revalidate" label: "bootstrap.c:1:1" }
edge: { sourcename: "payload_mm_authvar_smm_bootstrap_install" targetname: "__indirect_call" label: "src/lib/payload_mm_authvar_smm_bootstrap.c:367:6" }
edge: { sourcename: "payload_mm_authvar_smm_bootstrap_install" targetname: "__indirect_call" label: "src/lib/payload_mm_authvar_smm_bootstrap.c:466:6" }
edge: { sourcename: "src/lib/payload_mm_authvar_smm_bootstrap.c:media_revalidate" targetname: "__indirect_call" label: "src/lib/payload_mm_authvar_smm_bootstrap.c:128:10" }
EOF

test "$(awk -v limit=4096 -f "$graph" "$fixture")" -eq 568

missing=$temporary/missing.ci
sed '/bootstrap_receive.*platform_payload_mm_authvar_mor_private_smi_bootstrap/d' \
	"$fixture" > "$missing"
! awk -v limit=4096 -f "$graph" "$missing" >/dev/null 2>&1

missing_entry=$temporary/missing-entry.ci
sed '/smm_handler_start.*payload_mm_authvar_mor_private_smi_dispatch/d' \
	"$fixture" > "$missing_entry"
! awk -v limit=4096 -f "$graph" "$missing_entry" >/dev/null 2>&1

missing_dispatch=$temporary/missing-dispatch.ci
sed '/payload_mm_authvar_mor_private_smi_dispatch.*bootstrap_receive/d' \
	"$fixture" > "$missing_dispatch"
! awk -v limit=4096 -f "$graph" "$missing_dispatch" >/dev/null 2>&1

missing_entry_frame=$temporary/missing-entry-frame.ci
sed 's/\\n16 bytes (static)/" shape: ellipse }/' "$fixture" > \
	"$missing_entry_frame"
! cmp -s "$fixture" "$missing_entry_frame"
! awk -v limit=4096 -f "$graph" "$missing_entry_frame" >/dev/null 2>&1

unbounded_dispatch=$temporary/unbounded-dispatch.ci
sed 's/\\n32 bytes (static)/\\n32 bytes (dynamic)/' "$fixture" > \
	"$unbounded_dispatch"
! cmp -s "$fixture" "$unbounded_dispatch"
! awk -v limit=4096 -f "$graph" "$unbounded_dispatch" >/dev/null 2>&1

recursive=$temporary/recursive.ci
cp "$fixture" "$recursive"
printf '%s\n' 'edge: { sourcename: "payload_mm_authvar_smm_bootstrap_install" targetname: "src/lib/payload_mm_authvar_mor_private_smi_receiver.c:bootstrap_receive" label: "mutation:1:1" }' >> "$recursive"
! awk -v limit=4096 -f "$graph" "$recursive" >/dev/null 2>&1

unresolved=$temporary/unresolved.ci
cp "$fixture" "$unresolved"
printf '%s\n' 'edge: { sourcename: "src/lib/payload_mm_authvar_mor_private_smi_receiver.c:bootstrap_receive" targetname: "__indirect_call" label: "mutation:1:1" }' >> "$unresolved"
! awk -v limit=4096 -f "$graph" "$unresolved" >/dev/null 2>&1

deep=$temporary/deep.ci
cp "$fixture" "$deep"
printf '%s\n' \
	'node: { title: "deep" label: "deep\nmutation:1:1\n4096 bytes (static)" }' \
	'edge: { sourcename: "payload_mm_authvar_smm_bootstrap_install" targetname: "deep" label: "mutation:1:1" }' >> "$deep"
! awk -v limit=4096 -f "$graph" "$deep" >/dev/null 2>&1

printf '%s\n' 'Q35 MOR rooted SMM stack graph tests: PASS'
