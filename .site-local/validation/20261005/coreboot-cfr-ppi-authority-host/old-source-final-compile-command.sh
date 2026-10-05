cc -std=c11 -O2 -g -Wall -Wextra -Werror -fshort-wchar -fsanitize=address,undefined -fno-sanitize-recover=all -fno-pie \
	-DCDK2_COREBOOT_BACKEND_TEST -DCDK2_DIAG_UNIT_TEST \
	-ffunction-sections -fdata-sections \
	-Wl,--gc-sections -MMD -MP \
	-MF "/home/sean/current693-fwui-qemu.PMINOC/cfr-ppi-host.K3XaGlLC/old-source-final-test.d" -MT "/home/sean/current693-fwui-qemu.PMINOC/cfr-ppi-host.K3XaGlLC/old-source-final-test" \
	-I/home/sean/current693-fwui-qemu.PMINOC/cfr-ppi-host.K3XaGlLC/o2/include -I/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675//include -I/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675/src/boot \
	-fsanitize=address,undefined -no-pie -o "/home/sean/current693-fwui-qemu.PMINOC/cfr-ppi-host.K3XaGlLC/old-source-final-test" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675/src/boot/coreboot.c" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675//src/lib/boot_private_buffer.c" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675//src/lib/image_policy_snapshot.c" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675//src/lib/payload_mm_authvar_service.c" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675/src/boot/coreboot_dma_handoff.c" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675/src/boot/coreboot_checksum.c" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675/src/boot/coreboot_resource.c" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675/src/boot/coreboot_hobs.c" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675//src/modules/pci_host_bridge/model.c" \
	"/home/sean/current693-fwui-qemu.PMINOC/cfr-ppi-host.K3XaGlLC/coreboot_handoff-signed-parent.c" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675//src/lib/boot_logo.c" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675/src/boot/early_splash.c" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675/src/boot/services.c" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675//src/lib/linear_boot.c" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675//src/lib/diagnostic.c" \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675//src/lib/direct_image_table.c" \
	/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675//src/lib/pe_image_view.c \
	"/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675/src/boot/coreboot_test.c"
