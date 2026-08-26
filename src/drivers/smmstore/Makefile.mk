## SPDX-License-Identifier: GPL-2.0-only

bootblock-$(CONFIG_SMMSTORE) += lookup.c
romstage-$(CONFIG_SMMSTORE) += lookup.c

ramstage-$(CONFIG_SMMSTORE) += store.c ramstage.c

smm-$(CONFIG_SMMSTORE) += store.c smi.c
