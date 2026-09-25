## SPDX-License-Identifier: GPL-2.0-only

bootblock-y += bootblock.c

romstage-y += ../qemu-i440fx/memmap.c

postcar-y += ../qemu-i440fx/memmap.c
postcar-y += ../qemu-i440fx/exit_car.S

ramstage-y += ../qemu-i440fx/memmap.c
ramstage-y += ../qemu-i440fx/northbridge.c
ramstage-y += ../qemu-i440fx/rom_media.c
ramstage-y += cpu.c
ramstage-$(CONFIG_PAYLOAD_RESOURCE_HANDOFF) += payload_resource_handoff.c
ramstage-$(CONFIG_Q35_VTD_DMA_TEST_BACKEND) += vtd_dma_handoff.c q35_dma_policy.c
ramstage-$(CONFIG_Q35_VTD_DMA_TEST_BACKEND) += vtd_registers.c
ramstage-$(CONFIG_Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER) += q35_mor_pci_guard.c
ramstage-$(CONFIG_Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER) += mor_platform.c
smm-$(CONFIG_Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER) += mor_platform_smm.c
ramstage-$(CONFIG_PAYLOAD_LOCAL_APIC_TIMER_INFO) += lapic_timer.c

all-y += ../qemu-i440fx/bootmode.c
all-y += memmap.c

ramstage-$(CONFIG_CHROMEOS) += chromeos.c

smm-y += ../qemu-i440fx/rom_media.c
smm-y += smihandler.c
