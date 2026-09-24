## SPDX-License-Identifier: GPL-2.0-only

all-$(CONFIG_SMMSTORE_READ_REGION) += read_region.c
all-$(CONFIG_SMMSTORE) += store.c

ramstage-$(CONFIG_SMMSTORE) += ramstage.c

smm-$(CONFIG_SMMSTORE) += store.c smi.c
