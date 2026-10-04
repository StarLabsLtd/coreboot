#!/usr/bin/env bash
# Outside-repo bookkeeping for the reviewed original180 guest runners.
set -euo pipefail
driver=/home/sean/fresh-normal-focus-guest-after657.M2fS2l
rootreceipt=/home/sean/fresh-normal-focus-after657.CftZZg
build=$rootreceipt/build
firmware=/home/sean/Documents/cdk2
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
expected=b37a977ec5f81b6fa0bce527ab74acd743040a24
producer_expected=7ee34bed989c46913c3ee6672fb25e83227c3b6c
bundle=/home/sean/Documents/cdk2-validation/qemu/pr331-627e3bbe/qemu/fixtures
unused_request_fixture=/home/sean/normal-fwui-native-aftere63.BRRN8B/requester
requester=/home/sean/linux-fwui-requester-recovery-after959f.u4JpHs/linux-fwui.efi
bearssl=/home/sean/Documents/cdk2/3rdparty/bearssl
lvgl=/home/sean/Documents/cdk2/3rdparty/lvgl
vboot=/home/sean/Documents/coreboot/3rdparty/vboot
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean LANG=C LC_ALL=C
unset PYTHONOPTIMIZE
ulimit -c 0

snapshot_source()
{
	for source_root in "$firmware" "$producer" "$bearssl" "$lvgl" "$vboot"; do
		git -C "$source_root" ls-files --stage |
			awk '$1 == "100644" || $1 == "100755" {sub(/^[^\t]*\t/, ""); print}' |
			while IFS= read -r path; do sha256sum "$source_root/$path"; done
	done
}
snapshot_links()
{
	git -C "$vboot" ls-files --stage |
		awk '$1 == "120000" {sub(/^[^\t]*\t/, ""); print}' |
		while IFS= read -r path; do
			test -L "$vboot/$path"
			printf '%s\t%s\n' "$vboot/$path" "$(readlink "$vboot/$path")"
		done
}
snapshot_tools()
{
	for tool in python3 bash git qemu-system-x86_64 jq mcopy objcopy cpio cc gcc ld as \
		sha256sum cp dd stat cut grep awk sed dirname mktemp ln rm sleep date readelf \
		perl sort cmp mkdir realpath touch xargs readlink time rg cat; do
		alias=$(type -P "$tool")
		actual=$(realpath -e "$alias")
		printf 'TOOL\t%s\t%s\t%s\n' "$tool" "$alias" "$actual"
		sha256sum "$actual"
	done
	for tool in cc1 as ld collect2 lto-wrapper; do
		selected=$(cc "-print-prog-name=$tool")
		case "$selected" in /*) ;; *) selected=$(type -P "$selected") ;; esac
		printf 'SELECTED\t%s\t%s\n' "$tool" "$selected"
		sha256sum "$(realpath -e "$selected")"
	done
	for archive in libgcc.a liblto_plugin.so; do
		selected=$(cc "-print-file-name=$archive")
		printf 'SUPPORT\t%s\t%s\n' "$archive" "$selected"
		sha256sum "$(realpath -e "$selected")"
	done
}
snapshot_inputs()
{
	rg --files -uu "$build" | LC_ALL=C sort | xargs -d '\n' sha256sum
	sha256sum "$rootreceipt/build.sh" "$rootreceipt/ready-head.txt" "$rootreceipt/aggregate.status" \
		"$bundle/nvme-final-c4bec.raw" "$bundle/usb.raw" \
		"$unused_request_fixture/boot-to-fw-ui.raw" "$unused_request_fixture/boot-to-fw-ui.manifest" \
		"$requester" "$requester.manifest"
}
finish()
{
	status=$?
	trap - EXIT
	set +e
	sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log" 2>&1 || status=1
	sha256sum -c "$receipt/inputs-before.sha256" > "$receipt/inputs-after-check.log" 2>&1 || status=1
	sha256sum -c "$receipt/recipe-before.sha256" > "$receipt/recipe-after-check.log" 2>&1 || status=1
	snapshot_source > "$receipt/source-after.sha256" || status=1
	snapshot_links > "$receipt/vboot-links-after.tsv" || status=1
	snapshot_tools > "$receipt/tools-after.tsv" || status=1
	snapshot_inputs > "$receipt/inputs-after.sha256" || status=1
	for kind in source inputs; do cmp "$receipt/$kind-before.sha256" "$receipt/$kind-after.sha256" || status=1; done
	cmp "$receipt/vboot-links-before.tsv" "$receipt/vboot-links-after.tsv" || status=1
	cmp "$receipt/tools-before.tsv" "$receipt/tools-after.tsv" || status=1
	git -C "$firmware" rev-parse HEAD > "$receipt/head-after.txt"
	git -C "$firmware" status --porcelain=v1 > "$receipt/status-after.txt"
	git -C "$producer" rev-parse HEAD > "$receipt/producer-head-after.txt"
	git -C "$producer" status --porcelain=v1 > "$receipt/producer-status-after.txt"
	for kind in head status producer-head producer-status; do
		cmp "$receipt/$kind-before.txt" "$receipt/$kind-after.txt" || status=1
	done
	git -C "$firmware" verify-commit "$expected" > "$receipt/signature-after.log" 2>&1 || status=1
	printf '%s\n' "$status" > "$receipt/outer.status"
	exit "$status"
}
prepare()
{
	receipt=$driver/$1
	mkdir "$receipt"
	trap finish EXIT
	test "$(cat "$rootreceipt/aggregate.status")" = 0
	test "$(cat "$rootreceipt/ready-head.txt")" = "$expected"
	test "$(git -C "$firmware" rev-parse HEAD)" = "$expected"
	test "$(git -C "$producer" rev-parse HEAD)" = "$producer_expected"
	test -z "$(git -C "$firmware" status --porcelain=v1)"
	test -z "$(git -C "$producer" status --porcelain=v1)"
	git -C "$firmware" verify-commit "$expected" > "$receipt/signature-before.log" 2>&1
	git -C "$producer" verify-commit "$producer_expected" > "$receipt/producer-signature.log" 2>&1
	git -C "$firmware" rev-parse HEAD > "$receipt/head-before.txt"
	git -C "$firmware" status --porcelain=v1 > "$receipt/status-before.txt"
	git -C "$producer" rev-parse HEAD > "$receipt/producer-head-before.txt"
	git -C "$producer" status --porcelain=v1 > "$receipt/producer-status-before.txt"
	snapshot_source > "$receipt/source-before.sha256"
	snapshot_links > "$receipt/vboot-links-before.tsv"
	snapshot_tools > "$receipt/tools-before.tsv"
	snapshot_inputs > "$receipt/inputs-before.sha256"
	sha256sum "$driver/guest-common.sh" "$driver/run-ui-cancel.sh" "$driver/run-linux-reset.sh" \
		> "$receipt/recipe-before.sha256"
	printf 'PATH=%s\nTMPDIR=%s\nLANG=%s\nLC_ALL=%s\nPYTHONOPTIMIZE=unset\n' \
		"$PATH" "$TMPDIR" "$LANG" "$LC_ALL" > "$receipt/environment.txt"
	cd "$firmware"
}
run_command()
{
	printf '%q ' "${command[@]}" > "$receipt/command.sh.txt"
	printf '\n' >> "$receipt/command.sh.txt"
	/usr/bin/time -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
		-o "$receipt/whole-run.time" "${command[@]}" > "$receipt/whole-run.log" 2>&1
}
