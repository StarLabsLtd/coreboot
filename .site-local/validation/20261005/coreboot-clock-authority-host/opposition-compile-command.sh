cc -O2 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-pie \
	-DCDK2_COREBOOT_BACKEND_TEST -DCDK2_DIAG_UNIT_TEST \
	-ffunction-sections -fdata-sections \
	-Wl,--gc-sections -MMD -MP \
	-MF "/home/sean/current693-fwui-qemu.PMINOC/coreboot-clock-host.egAs41KF/first-match-test.d" -MT "/home/sean/current693-fwui-qemu.PMINOC/coreboot-clock-host.egAs41KF/first-match-test" \
	-I/home/sean/current693-fwui-qemu.PMINOC/coreboot-clock-host.egAs41KF/no-dma-o2/include -I/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677//include -I/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677/src/boot \
	-fsanitize=address,undefined -no-pie -o "/home/sean/current693-fwui-qemu.PMINOC/coreboot-clock-host.egAs41KF/first-match-test" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677/src/boot/coreboot.c" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677//src/lib/boot_private_buffer.c" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677//src/lib/image_policy_snapshot.c" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677//src/lib/payload_mm_authvar_service.c" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677/src/boot/coreboot_dma_handoff.c" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677/src/boot/coreboot_checksum.c" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677/src/boot/coreboot_resource.c" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677/src/boot/coreboot_hobs.c" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677//src/modules/pci_host_bridge/model.c" \
	"/home/sean/current693-fwui-qemu.PMINOC/coreboot-clock-host.egAs41KF/coreboot_handoff-first-match.c" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677//src/lib/boot_logo.c" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677/src/boot/early_splash.c" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677/src/boot/services.c" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677//src/lib/linear_boot.c" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677//src/lib/diagnostic.c" \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677//src/lib/direct_image_table.c" \
	/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677//src/lib/pe_image_view.c \
	"/home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677/src/boot/coreboot_test.c"
