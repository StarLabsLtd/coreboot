ifeq ($(CONFIG_SOC_INTEL_APOLLOLAKE),y)

subdirs-y += ../../../cpu/intel/common
subdirs-y += ../../../cpu/intel/microcode
subdirs-y += ../../../cpu/intel/turbo

bootblock-y += bootblock/bootblock.c
bootblock-y += ../common/block/cpu/pm_timer_emulation.c
bootblock-y += car.c
bootblock-y += heci.c
bootblock-y += gspi.c
bootblock-y += i2c.c
bootblock-y += lpc.c
bootblock-y += mmap_boot.c
bootblock-y += pmutil.c
bootblock-y += spi.c
bootblock-y += uart.c

romstage-y += car.c
romstage-y += ../../../cpu/intel/car/romstage.c
romstage-y += romstage.c
romstage-y += report_platform.c
romstage-y += gspi.c
romstage-y += heci.c
romstage-y += i2c.c
romstage-y += uart.c
romstage-y += meminit.c
ifeq ($(CONFIG_SOC_INTEL_GEMINILAKE),y)
romstage-y += meminit_util_glk.c
else
romstage-y += meminit_util_apl.c
endif
romstage-y += mmap_boot.c
romstage-y += pmutil.c
romstage-y += reset.c
romstage-y += spi.c

smm-y += mmap_boot.c
smm-y += pmutil.c
smm-y += smihandler.c
smm-y += spi.c
smm-y += uart.c
smm-y += elog.c
smm-y += xhci.c

ramstage-$(CONFIG_HAVE_ACPI_TABLES) += acpi.c
ramstage-y += cpu.c
ramstage-y += chip.c
ramstage-y += cse.c
ramstage-y += elog.c
ramstage-y += graphics.c
ramstage-y += gspi.c
ramstage-y += heci.c
ramstage-y += i2c.c
ramstage-y += lpc.c
ramstage-y += mmap_boot.c
ramstage-y += uart.c
ramstage-y += nhlt.c
ramstage-y += spi.c
ramstage-y += systemagent.c
ramstage-y += pmutil.c
ramstage-y += pnpconfig.c
ramstage-y += pmc.c
ramstage-y += reset.c
ramstage-y += xdci.c
ramstage-y += sd.c
ramstage-y += xhci.c

postcar-y += mmap_boot.c
postcar-y += spi.c
postcar-y += i2c.c
postcar-y += heci.c
postcar-y += reset.c
postcar-y += uart.c
postcar-y += gspi.c

verstage-y += car.c
verstage-y += i2c.c
verstage-y += gspi.c
verstage-y += heci.c
verstage-y += mmap_boot.c
verstage-y += uart.c
verstage-y += pmutil.c
verstage-y += reset.c
verstage-y += spi.c

ifeq ($(CONFIG_SOC_INTEL_GEMINILAKE),y)
bootblock-y += gpio_glk.c
romstage-y += gpio_glk.c
smm-y += gpio_glk.c
ramstage-y += gpio_glk.c
verstage-y += gpio_glk.c
else
bootblock-y += gpio_apl.c
romstage-y += gpio_apl.c
smm-y += gpio_apl.c
ramstage-y += gpio_apl.c
verstage-y += gpio_apl.c
endif

CPPFLAGS_common += -I$(src)/soc/intel/apollolake/include

# Since FSP-M runs in CAR we need to relocate it to a specific address
$(call strip_quotes,$(CONFIG_FSP_M_CBFS))-options := -b $(CONFIG_FSP_M_ADDR)

# Handle GLK paging requirements
ifeq ($(CONFIG_PAGING_IN_CACHE_AS_RAM),y)
cbfs-files-y += pt
pt-file := pt.c:struct
pt-type := raw
cbfs-files-y += pdpt
pdpt-file := pdpt.c:struct
pdpt-type := raw
endif

ifeq ($(CONFIG_NEED_LBP2),y)
$(objcbfs)/lbp2.bin: $(IFWITOOL)
ifeq ($(CONFIG_LBP2_FROM_IFWI),y)
	$(IFWITOOL) $(CONFIG_IFWI_FILE_NAME) create -f $@ -s
	$(IFWITOOL) $@ delete -n OBBP
else
	cp $(CONFIG_LBP2_FILE_NAME) $@
endif

files_added:: $(objcbfs)/lbp2.bin
	$(CBFSTOOL) $(obj)/coreboot.rom write -r $(CONFIG_LBP2_FMAP_NAME) -f $< --fill-upward
endif

# Bootblock on Apollolake platform lies in the IFWI region. In order to place
# the bootblock at the right location in IFWI image -
# a. Using ifwitool:
#    1. Create IFWI image (ifwi.bin.tmp) from input image
#       (CONFIG_IFWI_FILE_NAME).
#    2. Delete OBBP sub-partition, if present.
#    3. Replace IBBL directory entry in IBBP sub-partition with currently
#       generated bootblock.bin.
# b. Using cbfstool:
#    1. Write ifwi.bin.tmp to coreboot.rom using CONFIG_IFWI_FMAP_NAME.
ifeq ($(CONFIG_NEED_IFWI),y)
files_added:: $(IFWITOOL)
	$(IFWITOOL) $(CONFIG_IFWI_FILE_NAME) create -f $(objcbfs)/ifwi.bin.tmp
	$(IFWITOOL) $(objcbfs)/ifwi.bin.tmp delete -n OBBP
	$(IFWITOOL) $(objcbfs)/ifwi.bin.tmp replace -n IBBP -f $(objcbfs)/bootblock.bin -d -e IBBL
	$(CBFSTOOL) $(obj)/coreboot.rom write -r $(CONFIG_IFWI_FMAP_NAME) -f $(objcbfs)/ifwi.bin.tmp --fill-upward
endif

# DSP firmware settings files.
ifeq ($(CONFIG_SOC_INTEL_GEMINILAKE),y)
NHLT_BLOB_PATH = 3rdparty/blobs/soc/intel/glk/nhlt-blobs
else
NHLT_BLOB_PATH = 3rdparty/blobs/soc/intel/apollolake/nhlt-blobs
endif
DMIC_1CH_48KHZ_16B = dmic-1ch-48khz-16b.bin
DMIC_2CH_48KHZ_16B = dmic-2ch-48khz-16b.bin
DMIC_4CH_48KHZ_16B = dmic-4ch-48khz-16b.bin
MAX98357_RENDER = max98357-render-2ch-48khz-24b.bin
DA7219_RENDER_CAPTURE = dialog-2ch-48khz-24b.bin
RT5682_RENDER_CAPTURE = rt5682-2ch-48khz-24b.bin

cbfs-files-$(CONFIG_NHLT_DMIC_1CH_16B) += $(DMIC_1CH_48KHZ_16B)
$(DMIC_1CH_48KHZ_16B)-file := $(NHLT_BLOB_PATH)/$(DMIC_1CH_48KHZ_16B)
$(DMIC_1CH_48KHZ_16B)-type := raw

cbfs-files-$(CONFIG_NHLT_DMIC_2CH_16B) += $(DMIC_2CH_48KHZ_16B)
$(DMIC_2CH_48KHZ_16B)-file := $(NHLT_BLOB_PATH)/$(DMIC_2CH_48KHZ_16B)
$(DMIC_2CH_48KHZ_16B)-type := raw

cbfs-files-$(CONFIG_NHLT_DMIC_4CH_16B) += $(DMIC_4CH_48KHZ_16B)
$(DMIC_4CH_48KHZ_16B)-file := $(NHLT_BLOB_PATH)/$(DMIC_4CH_48KHZ_16B)
$(DMIC_4CH_48KHZ_16B)-type := raw

cbfs-files-$(CONFIG_NHLT_MAX98357) += $(MAX98357_RENDER)
$(MAX98357_RENDER)-file := $(NHLT_BLOB_PATH)/$(MAX98357_RENDER)
$(MAX98357_RENDER)-type := raw

cbfs-files-$(CONFIG_NHLT_DA7219) += $(DA7219_RENDER_CAPTURE)
$(DA7219_RENDER_CAPTURE)-file := $(NHLT_BLOB_PATH)/$(DA7219_RENDER_CAPTURE)
$(DA7219_RENDER_CAPTURE)-type := raw

cbfs-files-$(CONFIG_NHLT_RT5682) += $(RT5682_RENDER_CAPTURE)
$(RT5682_RENDER_CAPTURE)-file := $(NHLT_BLOB_PATH)/$(RT5682_RENDER_CAPTURE)
$(RT5682_RENDER_CAPTURE)-type := raw

ifeq ($(CONFIG_SOC_INTEL_GEMINILAKE),y)
# Gemini Lake B0 (706a1) only atm.
cpu_microcode_bins += $(wildcard 3rdparty/intel-microcode/intel-ucode/06-7a-*)
else
# Apollo Lake 506c2, B0 (506c9) and E0 (506ca) only atm.
cpu_microcode_bins += $(wildcard 3rdparty/intel-microcode/intel-ucode/06-5c-*)
endif

# IFWI Stitching

ifeq ($(CONFIG_IFWI_STITCH_COMPONENTS),y)

coreboot: $(objcbfs)/ibbl.rom $(objcbfs)/ibbm.rom $(objcbfs)/cbfs.rom $(objcbfs)/nvstorage.rom

$(objcbfs)/ibbl.rom: $(CBFSTOOL) $(objcbfs)/bootblock.bin
	cp $(objcbfs)/bootblock.bin $@
	printf "\nBuilt $@ (0x%x)\n" `stat -c "%s" $@`

$(objcbfs)/ibbm.rom: $(objcbfs)/ibbl.rom
	# Size is calculate as:
	# IBBP-Sub-Partition - IBBL - Manifest - BPM.met - Padding
	dd if=/dev/zero bs=1 count=$(shell echo $$((32768 - \
		$(shell stat -Lc %s $(objcbfs)/ibbl.rom) \
		- 748 - 180 - 3168))) | tr "\000" "\377" > $@
	printf "\nBuilt $@ (0x%x)\n" `stat -c "%s" $@`

$(objcbfs)/cbfs.rom: $(CBFSTOOL) $(obj)/coreboot.rom
	$(CBFSTOOL) $(obj)/coreboot.rom read -r OBB -f $@
	printf "\nBuilt $@ (0x%x)\n" `stat -c "%s" $@`

$(objcbfs)/nvstorage.rom: $(CBFSTOOL) $(obj)/coreboot.rom
	$(CBFSTOOL) $(obj)/coreboot.rom read -r NVSTORAGE -f $@
	printf "\nBuilt $@ (0x%x)\n" `stat -c "%s" $@`

endif

ifeq ($(CONFIG_IFWI_STITCH_IMAGE),y)
# Copy all required files into build dir

$(obj)/cse_image.bin:
	cp $(CONFIG_IFWI_CSE_IMAGE) $@

$(obj)/ISH.bin:
	cp $(CONFIG_IFWI_ISH_FW) $@

$(obj)/pdt.bin:
	cp $(CONFIG_IFWI_PDT) $@

$(obj)/iUnit.bin:
	cp $(CONFIG_IFWI_IUNIT) $@

$(obj)/pdr.bin:
	cp $(CONFIG_IFWI_PDR) $@

$(obj)/dsp_fw.bin:
	cp $(CONFIG_IFWI_DSP_FW) $@

$(obj)/pmcp.bin:
	cp $(CONFIG_IFWI_PMCP) $@

$(obj)/smip_iafw.bin:
	# Create empty smip_iafw.bin with Python
	@printf "    Generating $@\n"
	python -c "import struct; \
	fp = open ('$@', 'wb'); \
	fp.write (b'\xAF\xBE\xED\xDE' + b'\x00' * 0x380 + b'\xAA\xCC\xFF\xAA'); \
	fp.close();"

$(obj)/fit:
	cp $(CONFIG_IFWI_INTEL_FIT) $@

$(obj)/vsccommn.bin:
	cp $(CONFIG_IFWI_INTEL_VSCCOMMN) $@

$(obj)/meu:
	cp $(CONFIG_IFWI_INTEL_MEU) $@

$(obj)/private.pem:
	cp $(CONFIG_IFWI_PRIVATE_KEY) $@

$(obj)/public.pem: $(call strip_quotes, $(CONFIG_IFWI_PRIVATE_KEY))
	openssl pkey -in $< -pubout > $@

coreboot: $(obj)/coreboot-ifwi.rom

# 1. Create a hash of the keys
$(obj)/private_hash: $(obj)/meu $(obj)/meu_config.xml $(obj)/private.pem
	$(obj)/meu -cfg $(obj)/meu_config.xml -keyhash $@ -key $(obj)/private.pem

$(obj)/public_hash: $(obj)/meu $(obj)/meu_config.xml $(obj)/public.pem
	$(obj)/meu -cfg $(obj)/meu_config.xml -keyhash $@ -key $(obj)/public.pem

# 2. Configure meu_config.xml
$(obj)/meu_config.xml:
	sed \
	-e 's%@signing_key@%$(obj)/private.pem%g' \
	src/soc/intel/apollolake/stitch/meu_config.xml.in > $@

# 2. Configure spi.xml
hash=$(shell cat $(obj)/private_hash.txt)

ifeq ($(CONFIG_SOC_INTEL_GEMINILAKE),y)
patch1=3rdparty/intel-microcode/intel-ucode/06-7a-01
patch2=3rdparty/intel-microcode/intel-ucode/06-7a-08
sku=GLK
region=1548
else
patch1=3rdparty/intel-microcode/intel-ucode/06-5c-09
patch2=3rdparty/intel-microcode/intel-ucode/06-5c-0a
sku=APL
region=415
endif

ifeq ($(CONFIG_IFWI_BOOTGUARD),y)
bootguard=Boot Guard Profile 2 - VM
else
bootguard=Boot Guard Profile 0 - Legacy
endif

$(obj)/spi.xml: $(obj)/private_hash
	sed \
	-e 's%@signing_key@%$(obj)/private.pem%g' \
	-e 's%@key_hash@%$(hash)%g' \
	-e 's%@patch1@%$(patch1)%g' \
	-e 's%@patch2@%$(patch2)%g' \
	-e "s%@sku@%$(sku)%g" \
	-e 's%@region@%$(region)%g' \
	-e 's%@bootguard@%$(bootguard)%g' \
	src/soc/intel/apollolake/stitch/spi.xml.in > $@

# 4. Create bios.bin
$(obj)/bios.bin: $(objcbfs)/ibbl.rom $(objcbfs)/ibbm.rom $(objcbfs)/cbfs.rom $(objcbfs)/nvstorage.rom $(obj)/meu $(obj)/meu_config.xml
	$(obj)/meu -f src/soc/intel/apollolake/stitch/bios.xml -cfg $(obj)/meu_config.xml -o $@ -key $(CONFIG_IFWI_PRIVATE_KEY)

# 5. Create oemkeymn2.bin
$(obj)/oemkeymn2.bin: $(obj)/meu $(obj)/public_hash
	$(obj)/meu -f src/soc/intel/apollolake/stitch/OEMKeyManifest.xml -cfg $(obj)/meu_config.xml -o $@

# 7. Create coreboot.rom
$(obj)/coreboot-ifwi.rom: $(obj)/cse_image.bin $(obj)/bios.bin \
			  $(obj)/pmcp.bin $(obj)/smip_iafw.bin \
			  $(obj)/pdt.bin \
			  $(obj)/fit $(obj)/vsccommn.bin \
			  $(obj)/meu $(obj)/spi.xml \
			  $(obj)/oemkeymn2.bin
	$(obj)/fit -b -f $(obj)/spi.xml -o $@ -st_path /usr/bin/openssl

endif

endif # if CONFIG_SOC_INTEL_APOLLOLAKE
