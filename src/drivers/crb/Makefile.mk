## SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_CRB_TPM),y)
all-y += tis.c tpm.c
ifeq ($(CONFIG_DAVE_MODE)$(CONFIG_TCG_OPAL_S3_UNLOCK),yy)
smm-y += tpm.c
endif
endif
