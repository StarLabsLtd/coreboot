## SPDX-License-Identifier: GPL-2.0-only
ramstage-$(CONFIG_SOC_AMD_COMMON_BLOCK_IOMMU) += iommu.c
ramstage-$(CONFIG_SOC_AMD_CEZANNE_DMA_HANDOFF) += iommu_dma.c iommu_runtime.c
