## SPDX-License-Identifier: GPL-2.0-only

CPPFLAGS_common += -I$(src)/vendorcode/intel/tcg_storage_core

ramstage-$(CONFIG_TCG_OPAL_S3_UNLOCK) += opal_s3_resume.c

smm-$(CONFIG_TCG_OPAL_S3_UNLOCK) += opal_s3_smm.c
smm-$(CONFIG_TCG_OPAL_S3_UNLOCK) += opal_nvme.c
smm-$(CONFIG_TCG_OPAL_S3_UNLOCK) += opal_unlock.c
smm-$(CONFIG_TCG_OPAL_S3_UNLOCK) += ../../../vendorcode/intel/tcg_storage_core/tcg_storage_core.c
smm-$(CONFIG_TCG_OPAL_S3_UNLOCK) += ../../../vendorcode/intel/tcg_storage_core/tcg_storage_util.c
