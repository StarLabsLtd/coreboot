#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
packet=/home/sean/disk-retained-public-packet.RMcIOv/disk-retained-native-after627
consumer=/home/sean/Documents/.cdk2-worktrees/disk-retained-four-epoch-observer-after626-ready
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
native=/home/sean/normal-disk-retained-native.dSunYE
pair=/home/sean/normal-efi-disk-producers-after625.ZBl2DT
core=/home/sean/normal-efi-disk-core-final.TJMaS8
app=/home/sean/normal-efi-disk-request-after624-build.E29WEn
mkdir -p "$packet/source"
test ! -e "$packet/original-copies.tsv"
copy_original()
{
 source=$1
 destination=$packet/$2
 mkdir -p "${destination%/*}"
 cp -- "$source" "$destination"
 sum=$(sha256sum "$source")
 printf '%s\t%s\t%s\n' "$2" "$source" "${sum%% *}" >> "$packet/original-copies.tsv"
}
for file in run-native-before-parser-fix.sh run-native.sh launch.sh native.log native.time launch.log launch.time outer.status preflight-input-check-before-source-correction.log; do
 copy_original "$native/$file" "prior-preflight/$file"
done
for file in inputs-before.json initial-producer.config; do
 copy_original "$native/run-1/$file" "prior-preflight/run-1/$file"
done
for file in run-native-2.sh launch-2.sh native-2.log native-2.time launch-2.log launch-2.time outer-2.status; do
 copy_original "$native/$file" "native/$file"
done
for file in result.json inputs-before.json inputs-after.json command.json initial-producer.config target-producer.config serial.log qemu.log; do
 copy_original "$native/run-2/$file" "native/run-2/$file"
done
for epoch in 0 1 2 3; do
 for file in cbmem-observer.json cbmem-table-0.bin cbmem-table-1.bin cbmem-console.bin cbmem-live.log; do
  copy_original "$native/run-2/boot-$epoch/$file" "native/run-2/boot-$epoch/$file"
 done
 for file in publication-first.json publication-last.json publication-first-table.bin publication-last-table.bin; do
  copy_original "$native/run-2/scratch-$epoch/$file" "native/run-2/scratch-$epoch/$file"
 done
done
for group in author peer; do
 if test "$group" = author; then
  codecs=$native/run-2/disk-retained-store.InVMHB
 else
  codecs=/home/sean/disk-retained-saved-peer.LqedYX/disk-retained-store.5HPM4G
 fi
 for file in closure-before.sha256 closure-check.log dependencies.list dependencies-o0.mk dependencies-o2.mk build-o0.log build-o2.log check-o0.log check-o2.log extract.log config.log producer.config; do
  copy_original "$codecs/$file" "codec-$group/$file"
 done
done
for file in check.py check.log check.time store.log store.time; do
 copy_original "/home/sean/disk-retained-saved-peer.LqedYX/$file" "saved-peer/$file"
done
for file in replay.py replay.log replay.time; do
 copy_original "/home/sean/disk-retained-native-saved.1mGtgT/$file" "saved-author/$file"
done
for group in config-host ready-host; do
 if test "$group" = config-host; then
  proof=/home/sean/disk-retained-config-host.okEESf
 else
  proof=/home/sean/disk-retained-ready-host.MwSahN
 fi
 for file in run.sh host.log host.time host.status source.txt inputs-before.sha256 inputs-after-check.log; do
  copy_original "$proof/$file" "$group/$file"
 done
done
copy_original /home/sean/disk-retained-config-host.okEESf/config-correction.diff config-host/config-correction.diff
copy_original /home/sean/disk-retained-ready-host.MwSahN/whole-tree-parity.diff ready-host/whole-tree-parity.diff
for group in parser-failure prior-compile corrected-compile; do
 case "$group" in
  parser-failure) proof=/home/sean/disk-retained-codec-compile.G48G3o ;;
  prior-compile) proof=/home/sean/disk-retained-codec-compile-final.wqeSTE ;;
  corrected-compile) proof=/home/sean/disk-retained-codec-namespace.EkxISm ;;
 esac
 for file in run.sh dependencies-o0.mk dependencies-o2.mk dependencies.list inputs-before.sha256; do
  copy_original "$proof/$file" "$group/$file"
 done
 if test "$group" != parser-failure; then
  for file in build-o0.log build-o2.log inputs-after-check.log; do
   copy_original "$proof/$file" "$group/$file"
  done
 fi
done
for file in compile.log compile.time namespace-check.log; do
 copy_original "/home/sean/disk-retained-codec-namespace.EkxISm/$file" "corrected-compile/$file"
done
for file in build-producers.sh generate.sh generation-before.sha256 generation.log generation.time capsule.sha256 build-outer.time; do
 copy_original "$pair/$file" "producer/$file"
done
for version in initial9 targetA; do
 for file in input.config full.config configure.log build.log build.time outputs.sha256 inputs-before.sha256 inputs-after-check.log source-before.sha256 source-after-check.log source-head.txt; do
  copy_original "$pair/$version/$file" "producer/$version/$file"
 done
done
for file in resolved.config include/cdk2/config.h source-before.sha256 inputs-before.sha256 outputs.sha256 build.log build.time native/native-direct-image-inventory.tsv native/native-direct-composition-inventory.tsv; do
 copy_original "$core/$file" "compiled625-core/$file"
done
for file in build.log build.time inputs-before.sha256 tools-before.sha256 include/cdk2/config.h; do
 copy_original "$app/$file" "compiled625-app/$file"
done
archive_sources()
{
 repository=$1
 commit=$2
 archive=$3
 shift 3
 git -C "$repository" archive --format=tar -o "$packet/source/$archive" "$commit" -- "$@"
 git -C "$repository" log -1 --format='%H %G? %P' "$commit" >> "$packet/source-commits.txt"
 for path do
  blob=$(git -C "$repository" rev-parse "$commit:$path")
  sum=$(git -C "$repository" show "$commit:$path" | sha256sum)
  printf '%s\t%s\t%s\t%s\t%s\n' "source/$archive" "$commit" "$path" "$blob" "${sum%% *}" >> "$packet/source-identities.tsv"
 done
}
set -- tests/system_fmp_disk_retained_native_test.py tests/system_fmp_disk_retained_runner_test.py tests/system_fmp_disk_retained_store.c tests/system_fmp_disk_retained_store_test.sh tests/system_fmp_core_ram_native_test.py tests/system_fmp_core_ram_refusal.py util/qemu/bin/qmp_cbmem_console.py
archive_sources "$consumer" 0647ce39aa9a4b95fbceeecee59d6cfd16199064 preflight0647.tar "$@"
archive_sources "$consumer" a19efbefcf75f8a9301aa7b6bd380040b35f23bd tested-a19.tar "$@"
archive_sources "$consumer" 10879997a472866e9450efcd7817546cca6bb9e2 ready627.tar "$@"
archive_sources "$consumer" db662ce11383ae56e5d28b5c281f920b5ffa1ae9 compiled625-context.tar \
 src/modules/dxe_core/entry.c src/modules/capsule_runtime/entry.c \
 src/modules/system_fmp/provider.c src/modules/system_fmp/session.c src/boot/Makefile \
 include/cdk2/capsule_boot.h include/cdk2/system_fmp_transport.h \
 tests/normal_efi_disk_request_app.c tests/normal_efi_disk_request_app_build.sh tests/normal_efi_disk_request_app.ld
archive_sources "$producer" 7ee34bed989c46913c3ee6672fb25e83227c3b6c producer389-codecs.tar \
 src/drivers/efi/Kconfig Makefile.mk \
 src/lib/payload_mm_authvar_fv.c src/lib/payload_mm_authvar_store.c \
 src/lib/payload_mm_authvar_record.c src/lib/payload_mm_authvar_store_semantics.c \
 src/lib/payload_mm_authvar_writer.c src/lib/payload_mm_authvar_default_store.c \
 src/lib/payload_mm_authvar_ftw.c src/lib/uuid.c src/lib/hexstrtobin.c \
 src/include/boot/payload_mm_authvar.h src/include/boot/payload_mm_authvar_ftw.h \
 src/include/boot/payload_mm_authvar_record.h src/include/boot/payload_mm_authvar_store.h \
 src/include/boot/payload_mm_authvar_store_semantics.h src/include/uuid.h
copy_original /home/sean/disk-retained-public-packet.RMcIOv/collect.sh collect.sh
cd "$packet"
find . -type f ! -name files.sha256 -print0 | sort -z | xargs -0 sha256sum > files.sha256
