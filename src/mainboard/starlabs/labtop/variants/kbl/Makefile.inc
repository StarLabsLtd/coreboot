## SPDX-License-Identifier: GPL-2.0-only
CPPFLAGS_common += -I$(src)/mainboard/$(MAINBOARDDIR)

<<<<<<< HEAD
bootblock-y += gpio.c

romstage-y += romstage.c

ramstage-y += devtree.c
ramstage-y += gpio.c
ramstage-y += hda_verb.c
=======
romstage-y += romstage.c
>>>>>>> 0aad105d98... Rebase
