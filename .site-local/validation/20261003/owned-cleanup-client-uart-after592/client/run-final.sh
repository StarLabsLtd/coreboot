#!/bin/sh
set -eu
source=/home/sean/Documents/.cdk2-worktrees/system-fmp-signature-refusal-client-after587
receipt=/home/sean/system-fmp-signature-refusal-client-gate.KpVb3k
cd "$source"
ulimit -s 65536
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean
sha256sum include/cdk2/system_fmp_transport.h src/modules/system_fmp/transport.c \
 src/modules/system_fmp/session.c tests/system_fmp_transport_client_test.c \
 tests/system_fmp_session_test.c > "$receipt/final-source-before.sha256"
sh tests/system_fmp_transport_client_test.sh
make -j2 CDK2_CONFIG="$receipt/resolved.config" CDK2_BUILD_DIR="$receipt" \
 COREBOOT_TREE=/home/sean/Documents/.coreboot-worktrees/q35-exclusive-e8-dma-scope-after385 \
 COREBOOT_CONFIG=/home/sean/q35-e8-scope-full.9sM2hH/full.config \
 CDK2_BEARSSL_DIR=/home/sean/Documents/.cdk2-worktrees/fmp-owner-native-state-after-update/3rdparty/bearssl \
 CDK2_LVGL_ROOT=/home/sean/Documents/.cdk2-worktrees/fmp-owner-native-state-after-update/3rdparty/lvgl \
 native-system-fmp-session-test native-system-fmp-coordinator-test
sha256sum -c "$receipt/final-source-before.sha256"
git diff --check
git diff | sh util/lint/lint-007-checkpatch diff
