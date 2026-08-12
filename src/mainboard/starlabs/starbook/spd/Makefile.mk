## SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_BOARD_STARLABS_STARBOOK_RPL_U),y)
SPD_GEN_MEM_TECH := lp5

SPD_SOURCES = 32gb-5500
SPD_SOURCES += 32gb-6400
SPD_SOURCES += 32gb-7500
else
SPD_SOURCES = micron-MT40A1G16KD-062E-E	# 0b0000

LIB_SPD_DEPS = $(foreach f, $(SPD_SOURCES), src/mainboard/$(MAINBOARDDIR)/spd/$(f).spd.hex)
endif
