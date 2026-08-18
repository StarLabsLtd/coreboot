## SPDX-License-Identifier: GPL-2.0-only

ramstage-$(CONFIG_SMMSTORE) += store.c ramstage.c

smm-$(CONFIG_SMMSTORE) += store.c smi.c
