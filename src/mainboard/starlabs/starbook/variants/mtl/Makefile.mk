## SPDX-License-Identifier: GPL-2.0-only

bootblock-y += gpio.c

romstage-y += romstage.c
romstage-$(CONFIG_CAPSULE_BROKER_CBMEM_BUFFERS) += capsule_broker.c

ramstage-y += devtree.c
ramstage-y += gpio.c
ramstage-y += hda_verb.c
ramstage-y += ramstage.c
ramstage-$(CONFIG_CAPSULE_BROKER_CBMEM_BUFFERS) += capsule_broker.c
