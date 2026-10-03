#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
packet=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/private-auth2-after617
test ! -e "$packet"
mkdir -p "$packet"/{fresh/run,failure-prevm,failure-oracle,peer,sources,expected}
fresh=/home/sean/native-private-auth2-final-retry.mPv628
run=$fresh/artifacts/run.wIor8J
failed=/home/sean/native-private-auth2-final-parent.WUaxVf
oldrun=$failed/artifacts/run.Y4vYPw
prevm=/home/sean/native-private-auth2-after610.FeimaI
consumer=/home/sean/Documents/.cdk2-worktrees/native-private-auth2-after616
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
peer=/home/sean/private-auth2-clean-history-peer.r2vlvZ
for file in run.sh native.log native.time outer.status consumer-base.txt producer-head.txt \
	bearssl-head.txt source-before.sha256 source-after-check.log source-after.status \
	inputs-before.sha256 inputs-after-check.log codec-dependencies.make codec-dependencies.txt \
	codec-closure-before.sha256 codec-after-check.log patch-check.log; do
	cp -- "$fresh/$file" "$packet/fresh/$file"
done
for file in config.log build.log enrolled-inputs.log inventory.tsv resolved-payload.config \
	coreboot.config coreboot-absent.config coreboot-old-general.config \
	coreboot.config.inputs.sha256 coreboot-absent.config.inputs.sha256 \
	coreboot-old-general.config.inputs.sha256 source-before.sha256 source-after.sha256 \
	payload.inputs.sha256 native.outputs.sha256 nvme-base.sha256-check shape-guard.log \
	private-media.log ordinary.vm-status enrolled.vm-status; do
	cp -- "$run/$file" "$packet/fresh/run/$file"
done
cp -- "$run/configured/include/cdk2/config.h" "$packet/fresh/run/config.h"
for case in absent old-general ordinary enrolled; do
	cp -- "$run/$case.serial.log" "$run/$case.qemu.log" "$packet/fresh/run/"
done
cp -- "$run/absent.refusal-registers.log" "$run/old-general.refusal-registers.log" \
	"$packet/fresh/run/"
for file in build-o0.log build-o2.log check-o0.log check-o2.log inputs-before.sha256; do
	cp -- "$run/private-media/private-media.H6Jy16/$file" "$packet/fresh/run/codec-$file"
done
# Public certificate and independently constructed expected binding, not keys
# or bytes learned from the installed variable media.
for file in db.cert.der private.binding.bin private_certdb.bin; do
	cp -- "$run/configured/native/fullgraph-enrolled-inputs/$file" "$packet/expected/$file"
done
for file in native.log native.time run.sh source-before.sha256 source-after-check.log \
	source-after.status inputs-before.sha256 inputs-after-check.log codec-closure-before.sha256 \
	codec-after-check.log outer.status consumer-base.txt producer-head.txt bearssl-head.txt; do
	cp -- "$failed/$file" "$packet/failure-oracle/$file"
done
for file in ordinary.serial.log enrolled.serial.log absent.serial.log old-general.serial.log \
	ordinary.vm-status enrolled.vm-status private-media.log; do
	cp -- "$oldrun/$file" "$packet/failure-oracle/$file"
done
cp -- "$oldrun/private-media/private-media.MSiHod/check-o0.log" \
	"$packet/failure-oracle/codec-assertion.log"
for file in native.log native.time run.sh source-before.sha256 source-after-check.log \
	source-after.status inputs-before.sha256 inputs-after-check.log outer.status; do
	cp -- "$prevm/$file" "$packet/failure-prevm/$file"
done
cp -- "$prevm/artifacts/run.ZKioUc/build.log" "$packet/failure-prevm/build.log"
for file in saved-run-check.py saved-run-check.log saved-run-check.time fresh-codec.log \
	fresh-codec.time codec.log codec.time; do
	cp -- "$peer/$file" "$packet/peer/$file"
done
for file in codec.log codec.time codec-closure-before.sha256 codec-closure-after-check.log; do
	cp -- "/home/sean/private-auth2-clean-history.QrezfE/$file" "$packet/peer/saved-author-$file"
done

paths=(src/boot/Makefile tests/protected_variable_fullgraph_runtime_app.c
	tests/protected_variable_fullgraph_enrolled_inputs.sh
	tests/protected_variable_fullgraph_production_test.sh
	tests/protected_variable_private_media_check.c
	tests/protected_variable_private_media_check.sh)
archive_sources()
{
	local tree=$1 revision=$2 label=$3
	shift 3
	git -C "$tree" archive --format=tar "$revision" -- "$@" |
		zstd -q -T1 -o "$packet/sources/$label.tar.zst"
	for path in "$@"; do
		blob=$(git -C "$tree" rev-parse "$revision:$path")
		printf '%s\t%s\t%s\n' "$revision" "$blob" "$path"
	done > "$packet/sources/$label.tsv"
}
archive_sources "$consumer" 9120b63d0430af6f0239856175871022f96bc37c consumer-617 "${paths[@]}"
archive_sources "$consumer" ad7d8e1e86d37f191d0b825e9730559b1fb7d3cb consumer-before-oracle-fix "${paths[@]}"
archive_sources "$consumer" 9a694e9ee070db9761af222215f169e2d985e2e3 consumer-before-audit-fix "${paths[@]}"
mapfile -t codec_paths < <(sed -n "s|^$producer/||p" "$fresh/codec-dependencies.txt")
codec_paths+=(tests/lib/payload_mm_authvar_ftw_test.c)
archive_sources "$producer" 7ee34bed989c46913c3ee6672fb25e83227c3b6c producer-389-codec "${codec_paths[@]}"
cp -- "$(dirname -- "$packet")/README.txt" "$(dirname -- "$packet")/check-receipts.py" \
	"$(dirname -- "$packet")/prepare.sh" "$packet/"
(cd "$packet"; find . -type f ! -name files.sha256 -printf '%P\0' | sort -z |
	xargs -0 sha256sum > files.sha256)
(cd "$packet"; sha256sum -c files.sha256 >/dev/null; python3 check-receipts.py)
tar -C "$(dirname -- "$packet")" -cf - private-auth2-after617 |
	zstd -q -T1 -o "$(dirname -- "$packet")/private-auth2-after617.tar.zst"
sha256sum "$(dirname -- "$packet")/private-auth2-after617.tar.zst"
