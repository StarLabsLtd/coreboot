# SPDX-License-Identifier: GPL-2.0-only

ramstage-y += touchpad.c
smm-$(CONFIG_STARLABS_TOUCHPAD_RUNTIME) += touchpad.c
