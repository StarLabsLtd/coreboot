## SPDX-License-Identifier: GPL-2.0-only

bootblock-y += gpio.c

romstage-y += romstage.c
romstage-$(CONFIG_CAPSULE_BROKER_CBMEM_BUFFERS) += capsule_broker.c

ramstage-y += devtree.c
ramstage-y += gpio.c
ramstage-y += hda_verb.c
ramstage-y += ramstage.c
ramstage-$(CONFIG_CAPSULE_BROKER_CBMEM_BUFFERS) += capsule_broker.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_BOOT_CONTROLLER_INVENTORY) += payload_resource_handoff.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_BOOT_CONTROLLER_INVENTORY) += payload_resource_policy.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_DMA_DIAGNOSTIC) += dma_diagnostic.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_DMA_DIAGNOSTIC) += dma_diagnostic_platform.c
