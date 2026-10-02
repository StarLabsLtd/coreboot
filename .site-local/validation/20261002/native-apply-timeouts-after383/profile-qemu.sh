#!/bin/sh
set -eu
output=/home/sean/authenticated-apply-timeout-preservation.Yw0LJM/profile
mkdir "$output"
cp --reflink=auto /home/sean/q35-authenticated-apply-compile.X6I2ay/native/capsule-provider-info-close-component/run.fS9hZ4/execution-before.rom "$output/probe.rom"
sha256sum "$output/probe.rom" > "$output/input.sha256"
qemu-img create -q -f raw "$output/nvme.raw" 16M
status=0
/usr/bin/time -v timeout 90 strace -f -c -e trace=pwrite64,fsync,fdatasync \
    -o "$output/syscall-summary.txt" /usr/bin/qemu-system-x86_64 \
    -machine q35,smm=on,accel=tcg -m 1024M -cpu max -smp 1 -nodefaults \
    -device intel-iommu,pt=off -device VGA,bus=pcie.0,addr=01 \
    -drive "if=none,id=nvme0,format=raw,file=$output/nvme.raw" \
    -device nvme,drive=nvme0,serial=CDK2MMRESET,bus=pcie.0,addr=03 \
    -device qemu-xhci,bus=pcie.0,addr=04 \
    -device edu,dma_mask=0xffffffff,bus=pcie.0,addr=05 \
    -global driver=cfi.pflash01,property=secure,value=on \
    -drive "if=pflash,format=raw,file=$output/probe.rom,cache=writeback" \
    -display none -serial "file:$output/probe.serial.log" \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    > "$output/probe.qemu.log" 2> "$output/time-and-strace.log" || status=$?
printf '%s\n' "$status" > "$output/diagnostic-exit-status.txt"
printf 'Diagnostic exit status: %s (not an acceptance gate)\n' "$status"
cat "$output/syscall-summary.txt"
