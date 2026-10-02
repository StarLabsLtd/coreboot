## SPDX-License-Identifier: GPL-2.0-only

bootblock-y += bootblock.c

romstage-y += ../qemu-i440fx/memmap.c

postcar-y += ../qemu-i440fx/memmap.c
postcar-y += ../qemu-i440fx/exit_car.S

ramstage-y += ../qemu-i440fx/memmap.c
ramstage-y += ../qemu-i440fx/northbridge.c
ramstage-y += ../qemu-i440fx/rom_media.c
ramstage-y += cpu.c
ramstage-$(CONFIG_Q35_SMM_INVOCATION_NATIVE_COMPONENT) += loader_instance.c
ramstage-$(CONFIG_Q35_SMM_INVOCATION_NATIVE_CAUSE_COMPONENT) += native_apmc.S
ramstage-$(CONFIG_Q35_SMM_INVOCATION_NATIVE_SERVICE_COMPONENT) += native_service_sender.c
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
smm-$(CONFIG_Q35_SMM_INVOCATION_NATIVE_CAUSE_COMPONENT) += native_cause.c
smm-$(CONFIG_Q35_SMM_INVOCATION_NATIVE_SERVICE_COMPONENT) += native_service_receiver.c
smm-$(CONFIG_Q35_SMM_INVOCATION_NATIVE_PUBLIC_SERVICE_COMPONENT) += public_service.c
smm-$(CONFIG_Q35_SMM_INVOCATION_FAIL_STOP_TEST) += smm_invocation_fail_stop.c

cbfs-files-$(CONFIG_Q35_SMM_INVOCATION_NATIVE_CAUSE_COMPONENT) += q35/native-apmc
q35/native-apmc-file := $(obj)/mainboard/emulation/qemu-q35/native_apmc.raw
q35/native-apmc-type := raw
q35/native-apmc-compression := none
cbfs-files-$(CONFIG_Q35_SMM_INVOCATION_NATIVE_CAUSE_COMPONENT) += q35/native-apmc-bootstrap
q35/native-apmc-bootstrap-file := $(obj)/mainboard/emulation/qemu-q35/native_apmc_bootstrap.raw
q35/native-apmc-bootstrap-type := raw
q35/native-apmc-bootstrap-compression := none

$(obj)/mainboard/emulation/qemu-q35/native_apmc.raw: $(obj)/ramstage/mainboard/emulation/qemu-q35/native_apmc.o
	$(OBJCOPY_ramstage) -O binary -j .text.q35_native_apmc $< $@

$(obj)/mainboard/emulation/qemu-q35/native_apmc_bootstrap.raw: $(obj)/ramstage/mainboard/emulation/qemu-q35/native_apmc.o
	$(OBJCOPY_ramstage) -O binary -j .text.q35_native_apmc_bootstrap $< $@
