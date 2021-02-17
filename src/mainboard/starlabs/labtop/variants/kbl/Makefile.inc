## SPDX-License-Identifier: GPL-2.0-only
CPPFLAGS_common += -I$(src)/mainboard/$(MAINBOARDDIR)

bootblock-y += gpio.c

romstage-y += romstage.c

ramstage-y += gpio.c
