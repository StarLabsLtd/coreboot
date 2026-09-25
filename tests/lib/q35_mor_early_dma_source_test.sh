#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/q35-mor-early-dma.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
source_file=$root/src/mainboard/emulation/qemu-q35/vtd_dma_handoff.c

function_body()
{
	name=$1
	input=$2
	awk -v name="$name" '
		$0 ~ "^[[:space:]]*(static[[:space:]]+)?(bool|void|int)[[:space:]]+" name "\\(" {
			copy = 1
		}
		copy {
			print
			line = $0
			opens = gsub(/\{/, "{", line)
			closes = gsub(/\}/, "}", line)
			depth += opens - closes
			if (seen && depth == 0)
				exit
			if (opens)
				seen = 1
		}
	' "$input"
}

selected_view()
{
	awk '
		/^#if CONFIG\(Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER\)$/ {
			selected = 1; next
		}
		selected == 1 && /^#else$/ { selected = 2; next }
		selected && /^#endif$/ { selected = 0; next }
		selected != 2 { print }
	' "$1"
}

feature_off_view()
{
	awk '
		/^#if CONFIG\(Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER\)$/ {
			selected = 1; next
		}
		selected == 1 && /^#else$/ { selected = 2; next }
		selected && /^#endif$/ { selected = 0; next }
		!selected || selected == 2 { print }
	' "$1"
}

continuous_dma_valid()
{
	input=$1
	early=$temporary/early.$$
	pre_device=$temporary/pre-device.$$
	requiesce=$temporary/requiesce.$$
	backend=$temporary/backend.$$
	selected=$temporary/selected.$$
	off=$temporary/off.$$

	function_body q35_mor_dma_early_default_deny "$input" > "$early"
	function_body q35_mor_dma_pre_device_guard_capture "$input" > "$pre_device"
	function_body q35_mor_dma_requiesce_after_device_init "$input" > "$requiesce"
	function_body q35_dma_backend_enable "$input" > "$backend"
	selected_view "$backend" > "$selected"
	feature_off_view "$backend" > "$off"
	test -s "$early" && test -s "$pre_device" && test -s "$requiesce" &&
		test -s "$backend" &&
		grep -q 'memset(mor_early_root, 0, sizeof(mor_early_root))' "$early" &&
		grep -q 'q35_vtd_default_deny(&io, (uint32_t)root)' "$early" &&
		test "$(grep -n 'memset(mor_early_root' "$early" | cut -d: -f1)" \
			-lt "$(grep -n 'q35_vtd_default_deny' "$early" | cut -d: -f1)" &&
		grep -q 'q35_mor_dma_early_default_deny()' "$pre_device" &&
		grep -q 'q35_mor_dma_capture_pre_device_inventory()' "$pre_device" &&
		test "$(grep -n 'q35_mor_dma_early_default_deny' "$pre_device" | \
			cut -d: -f1)" -lt \
			"$(grep -n 'q35_mor_dma_capture_pre_device_inventory' \
			"$pre_device" | cut -d: -f1)" &&
		test "$(grep -c 'q35_mor_dma_early_root_valid()' "$requiesce")" -eq 2 &&
		grep -q 'q35_mor_pci_guard_requiesce' "$requiesce" &&
		grep -q 'q35_mor_pci_guard_validate' "$requiesce" &&
		! grep -q 'pci_inventory(true)' "$requiesce" &&
		test "$(grep -n 'q35_mor_dma_early_root_valid()' "$requiesce" | \
			sed -n '1s/:.*//p')" -lt \
			"$(grep -n 'q35_mor_pci_guard_requiesce' "$requiesce" | \
			cut -d: -f1)" &&
		test "$(grep -n 'q35_mor_pci_guard_requiesce' "$requiesce" | \
			cut -d: -f1)" -lt \
			"$(grep -n 'q35_mor_dma_early_root_valid()' "$requiesce" | \
				sed -n '2s/:.*//p')" &&
		grep -q 'q35_mor_dma_requiesce_after_device_init' "$selected" &&
		grep -q 'q35_mor_dma_switch_protected_root' "$selected" &&
		test "$(grep -n 'q35_mor_dma_requiesce_after_device_init' \
			"$selected" | cut -d: -f1)" -lt \
			"$(grep -n 'q35_mor_dma_switch_protected_root' "$selected" | \
				cut -d: -f1)" &&
		! grep -q 'pci_inventory(true)' "$selected" &&
		grep -q 'q35_vtd_default_deny' "$off" &&
		grep -q 'pci_inventory(true)' "$off" &&
		! grep -q 'q35_mor_dma_switch_protected_root' "$off" &&
		grep -q 'identity ^= mor_pci_requesters\[index\]\.bdf' "$input" &&
		grep -q 'identity ^= mor_pci_requesters\[index\]\.vendor_id' "$input" &&
		grep -q 'identity ^= mor_pci_requesters\[index\]\.device_id' "$input" &&
		grep -q 'BOOT_STATE_INIT_ENTRY(BS_PRE_DEVICE, BS_ON_ENTRY' "$input"
}

continuous_dma_valid "$source_file"

identity_omitted=$temporary/identity-omitted.c
sed '/identity ^= mor_pci_requesters\[index\]\.device_id;/d' \
	"$source_file" > "$identity_omitted"
if continuous_dma_valid "$identity_omitted"; then
	printf '%s\n' 'ERROR: incomplete retained PCI identity hash survived' >&2
	exit 1
fi

reordered=$temporary/reordered.c
sed 's/!q35_mor_dma_early_default_deny() ||/!q35_mor_dma_capture_pre_device_inventory() ||/;
	s/!q35_mor_dma_capture_pre_device_inventory())/!q35_mor_dma_early_default_deny())/' \
	"$source_file" > "$reordered"
if continuous_dma_valid "$reordered"; then
	printf '%s\n' 'ERROR: capture-before-deny mutant survived' >&2
	exit 1
fi

unverified_requiesce=$temporary/unverified-requiesce.c
sed '0,/!q35_mor_dma_early_root_valid() ||/s//false ||/' \
	"$source_file" > "$unverified_requiesce"
if continuous_dma_valid "$unverified_requiesce"; then
	printf '%s\n' 'ERROR: unverified early-root re-quiesce mutant survived' >&2
	exit 1
fi

generic_recapture=$temporary/generic-recapture.c
sed '0,/q35_mor_pci_guard_requiesce(&mor_pci_io,/s//pci_inventory(true) || q35_mor_pci_guard_requiesce(\&mor_pci_io,/' \
	"$source_file" > "$generic_recapture"
if continuous_dma_valid "$generic_recapture"; then
	printf '%s\n' 'ERROR: generic late PCI recapture mutant survived' >&2
	exit 1
fi

missing_post_root_check=$temporary/missing-post-root-check.c
awk '
	/q35_mor_dma_requiesce_after_device_init\(void\)/ { function_seen = 1 }
	function_seen && /!q35_mor_dma_early_root_valid\(\)/ {
		validations++
		if (validations == 2)
			sub(/!q35_mor_dma_early_root_valid\(\)/, "false")
	}
	{ print }
' "$source_file" > "$missing_post_root_check"
if continuous_dma_valid "$missing_post_root_check"; then
	printf '%s\n' 'ERROR: missing post-re-quiesce early-root check mutant survived' >&2
	exit 1
fi

late_capture=$temporary/late-capture.c
sed 's/if (!q35_mor_dma_pre_device_guard_valid())/if (!pci_inventory(true))/' \
	"$source_file" > "$late_capture"
if continuous_dma_valid "$late_capture"; then
	printf '%s\n' 'ERROR: selected late-capture mutant survived' >&2
	exit 1
fi

selected_reenable=$temporary/selected-reenable.c
sed '0,/q35_mor_dma_switch_protected_root(&io,/s//q35_vtd_default_deny(\&io,/' \
	"$source_file" > "$selected_reenable"
if continuous_dma_valid "$selected_reenable"; then
	printf '%s\n' 'ERROR: selected translation-reenable mutant survived' >&2
	exit 1
fi

off_regression=$temporary/off-regression.c
sed '0,/result = q35_vtd_default_deny(&io,/s//result = q35_mor_dma_switch_protected_root(\&io,/' \
	"$source_file" > "$off_regression"
if continuous_dma_valid "$off_regression"; then
	printf '%s\n' 'ERROR: feature-off path regression mutant survived' >&2
	exit 1
fi

printf '%s\n' 'Q35 MOR early continuous-DMA source tests: PASS'
