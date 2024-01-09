## SPDX-License-Identifier: GPL-2.0-only

all-$(CONFIG_PAYLOAD_MM_INTERFACE) += util.c
ramstage-$(CONFIG_PAYLOAD_MM_INTERFACE) += cbtable.c
smm-$(CONFIG_PAYLOAD_MM_INTERFACE) += util.c smi.c
