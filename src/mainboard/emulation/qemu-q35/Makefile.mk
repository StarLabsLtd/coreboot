## SPDX-License-Identifier: GPL-2.0-only

bootblock-y += bootblock.c
bootblock-y += rom_media_stub.c

romstage-y += ../qemu-i440fx/memmap.c
romstage-y += rom_media_stub.c

postcar-y += ../qemu-i440fx/memmap.c
postcar-y += ../qemu-i440fx/exit_car.S
postcar-y += rom_media_stub.c

ramstage-y += ../qemu-i440fx/memmap.c
ramstage-y += ../qemu-i440fx/northbridge.c
ramstage-y += ../qemu-i440fx/rom_media.c
ramstage-y += cpu.c
ramstage-$(CONFIG_PAYLOAD_RESOURCE_HANDOFF) += payload_resource_handoff.c
ramstage-$(CONFIG_PAYLOAD_LOCAL_APIC_TIMER_INFO) += lapic_timer.c

all-y += ../qemu-i440fx/bootmode.c
all-y += memmap.c

ramstage-$(CONFIG_CHROMEOS) += chromeos.c

smm-y += ../qemu-i440fx/rom_media.c
smm-y += smihandler.c
