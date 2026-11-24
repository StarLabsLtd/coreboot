# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_RUN_AMD_PEI_GOP),y)

ramstage-y += loader.c

rmodules_x86_32-y += host.c pe32.c

amd_gop_objects = $(call src-to-obj,rmodules_x86_32,$(dir)/host.c) \
	$(call src-to-obj,rmodules_x86_32,$(dir)/pe32.c) \
	$(obj)/amd_pei_gop/vendor.o

$(filter-out $(obj)/amd_pei_gop/vendor.o,$(amd_gop_objects)): CFLAGS_rmodules_x86_32 += \
	-fshort-wchar -malign-double \
	-I$(src)/vendorcode/intel/edk2/UDK2017/MdePkg/Include \
	-I$(src)/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32

$(obj)/amd_pei_gop/amd_gop.ffs: $(call strip_quotes,$(CONFIG_DRIVERS_AMD_PEI_GOP_FILE)) $(dir)/make_ffs.py
	@mkdir -p $(dir $@)
	python3 $(src)/drivers/efi/amd_pei_gop/make_ffs.py $< $@

$(obj)/amd_pei_gop/vendor.o: $(obj)/amd_pei_gop/amd_gop.ffs
	cd $(dir $<); $(OBJCOPY_rmodules_x86_32) -I binary -O elf32-i386 -B i386 amd_gop.ffs vendor.o

$(eval $(call rmodule_link,$(obj)/amd_pei_gop/host.elf,$(amd_gop_objects),x86_32))

$(obj)/amd_pei_gop/host.bin: $(obj)/amd_pei_gop/host.elf.rmod
	$(OBJCOPY_rmodules_x86_32) -O binary $< $@

cbfs-files-y += amd/pei_gop
amd/pei_gop-file := $(obj)/amd_pei_gop/host.bin
amd/pei_gop-type := raw

cbfs-files-y += amd/atom
amd/atom-file := $(call strip_quotes,$(CONFIG_DRIVERS_AMD_PEI_GOP_ATOM_FILE))
amd/atom-type := raw

endif
