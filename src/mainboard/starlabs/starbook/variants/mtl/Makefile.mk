## SPDX-License-Identifier: GPL-2.0-only

bootblock-y += gpio.c

romstage-y += romstage.c
romstage-$(CONFIG_STARLABS_STARBOOK_MTL_LOADER_INSTANCE_SOURCE) += loader_instance_source.c
romstage-$(CONFIG_STARLABS_STARBOOK_MTL_LOADER_INSTANCE_AUTHORITY) += loader_instance_authority.c
romstage-$(CONFIG_CAPSULE_BROKER_CBMEM_BUFFERS) += capsule_broker.c

ramstage-y += devtree.c
ramstage-y += gpio.c
ramstage-y += hda_verb.c
ramstage-y += ramstage.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_LOADER_INSTANCE_SOURCE) += loader_instance_source.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_LOADER_INSTANCE_AUTHORITY) += loader_instance_authority.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_LOADER_INSTANCE_AUTHORITY) += smm_invocation_loader_instance.c
ramstage-$(CONFIG_CAPSULE_BROKER_CBMEM_BUFFERS) += capsule_broker.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_BOOT_CONTROLLER_INVENTORY) += payload_resource_handoff.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_BOOT_CONTROLLER_INVENTORY) += payload_resource_policy.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_DMA_DIAGNOSTIC) += dma_diagnostic.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_DMA_DIAGNOSTIC) += dma_diagnostic_platform.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_DMA_LIVE_BACKEND) += dma_live.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_DMA_LIVE_BACKEND) += dma_live_platform.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_MOR_DMA_GUARD) += dma_guard.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_MOR_DMA_GUARD) += mor_live_inventory.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_BINDING) += mor_clear_x86.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER) += mor_platform.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER) += mor_private_boundary.c
ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_DMA_HANDOFF) += dma_live_handoff.c

smm-$(CONFIG_STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER) += mor_platform_smm.c
smm-$(CONFIG_STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP) += smm_invocation_fail_stop.c
smm-$(CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_ROUTE_COMPOSITION_OWNER) += \
	authvar_presence_route_composition.c
