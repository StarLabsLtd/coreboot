#!/bin/bash
set -euo pipefail
proof=/home/sean/ata-style-proof.yq0R3e
root=/home/sean/Documents/.cdk2-worktrees/ata-private-style-after499
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1
for optimization in 0 2 s; do
	for variant in default selected; do
		if test "$variant" = default; then binary=model-o$optimization;
		else binary=selected-model-o$optimization; fi
		printf 'STRICT_SAN %s O%s\n' "$variant" "$optimization"
		"$proof/$binary"
		if cmp -s "$proof/$variant-baseline-o$optimization.o" \
			"$proof/$variant-candidate-o$optimization.o"; then
			printf 'OBJECT %s O%s CMP0\n' "$variant" "$optimization"
		else
			printf 'OBJECT %s O%s DIFFERENT (not a byte-parity claim)\n' \
				"$variant" "$optimization"
		fi
	done
	printf 'DIFFERENTIAL baseline/candidate O%s\n' "$optimization"
	"$proof/differential-o$optimization"
done
cd "$root"
TMPDIR=/home/sean PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin \
	make CDK2_CONFIG_READY=1 -j2 native-ata-bus-model-test native-ata-bus-io-test \
	native-ata-bus-block-test native-ata-bus-binding-test native-ata-bus-entry-test \
	"$root/build/cdk2/native/AtaBusDxe.efi"
TMPDIR=/home/sean util/lint/cdk2-checkpatch --show-types --file --quiet \
	--max-line-length 96 src/modules/ata_bus/model.c
git diff --check
sha256sum src/modules/ata_bus/model.c build/cdk2/include/cdk2/config.h \
	/home/sean/Documents/cdk2/build/cdk2/include/cdk2/config.h \
	build/cdk2/native/AtaBusDxe.efi
printf 'ATA bounded replay: PASS (x86_64 private-style only; no hardware claim)\n'
