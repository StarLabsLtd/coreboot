## SPDX-License-Identifier: GPL-2.0-only

PHONY+=add_ite_fw
INTERMEDIATE+=add_ite_fw

ifeq ($(CONFIG_EC_STARLABS_IT5570),y)
all-y += ec.c
smm-$(CONFIG_DEBUG_SMI) += ec.c

ifeq ($(CONFIG_EC_STARLABS_IT_BIN),y)
add_ite_fw: $(obj)/coreboot.pre
	$(CBFSTOOL) $(obj)/coreboot.pre write -r EC -f $(CONFIG_EC_STARLABS_IT_BIN_PATH) -u

ifeq ($(CONFIG_EC_STARLABS_IT_BIN_PATH),)
files_added:: warn_no_ite_fw

PHONY+=warn_no_ite_fw
warn_no_ite_fw:
	printf "\n\t** WARNING **\n"
	printf "coreboot has been built without the IT5570 EC Firmware.\n"
	printf "Do not flash this image.  Your LabTop Mk IV's power button\n"
	printf "may not respond when you press it.\n\n"

endif
endif
endif
