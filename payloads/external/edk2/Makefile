## SPDX-License-Identifier: GPL-2.0-only

# force the shell to bash - the edksetup.sh script doesn't work with dash
export SHELL := env bash

project_name = edk2
export WORKSPACE := $(CURDIR)/workspace

strip_quotes = $(subst ",,$(1))
edk2_repo_basename = $(patsubst %.git,%,$(notdir $(subst :,/,$(patsubst file://%,%,$(patsubst %/,%,$(1))))))

EDK2_REPOSITORY := $(call strip_quotes,$(CONFIG_EDK2_REPOSITORY))
EDK2_PLATFORMS_REPOSITORY := $(call strip_quotes,$(CONFIG_EDK2_PLATFORMS_REPOSITORY))
EDK2_PATH_REPO_ROOT := $(word 3,$(subst /, ,$(EDK2_REPOSITORY)))
ifneq ($(filter /% file:///%,$(EDK2_REPOSITORY)),)
# Keep local/private edk2 paths from collapsing into workspace/home or
# workspace/sean.
EDK2_PATH_REPO_ROOT := $(call edk2_repo_basename,$(EDK2_REPOSITORY))
endif
ifeq ($(CONFIG_EDK2_CDK2),y)
# CDK2 is usually tested from local/private forks, including scp-style SSH URLs.
EDK2_PATH_REPO_ROOT := $(call edk2_repo_basename,$(EDK2_REPOSITORY))
endif
export EDK2_PATH := $(WORKSPACE)/$(EDK2_PATH_REPO_ROOT)

ifeq ($(CONFIG_EDK2_CDK2),y)
EDK2_GENERATE_CAPSULE := $(EDK2_PATH)/Build/cdk2-coreboot/basetools/Python/BinWrappers/PosixLike/GenerateCapsule
else
EDK2_GENERATE_CAPSULE := $(EDK2_PATH)/BaseTools/BinWrappers/PosixLike/GenerateCapsule
endif

EDK2_CLONE_FLAGS := --recurse-submodules
EDK2_SUBMODULE_FLAGS := --init --checkout --recursive
ifeq ($(CONFIG_EDK2_CDK2),y)
# cdk2 only needs EDK2's direct build submodules. Do not pull nested OpenSSL
# development repositories into every Coreboot build.
EDK2_CLONE_FLAGS := --no-recurse-submodules
EDK2_SUBMODULE_FLAGS := --init --checkout
endif

define edk2_abspath
$(if $(filter /%,$(1)),$(1),$(EDK2_PATH)/$(1))
endef

ifeq ($(CONFIG_EDK2_USE_EDK2_PLATFORMS),y)
BUILD_STR += -D USE_EDK2_PLATFORMS=TRUE
export EDK2_PLATFORMS_PATH := $(WORKSPACE)/edk2-platforms
export PACKAGES_PATH := $(EDK2_PATH):\
			$(EDK2_PLATFORMS_PATH)/Platform/Intel:\
			$(EDK2_PLATFORMS_PATH)/Silicon/Intel:\
			$(EDK2_PLATFORMS_PATH)/Features/Intel:\
			$(EDK2_PLATFORMS_PATH)/Features/Intel/Debugging:\
			$(EDK2_PLATFORMS_PATH)/Features/Intel/Network:\
			$(EDK2_PLATFORMS_PATH)/Features/Intel/OutOfBandManagement:\
			$(EDK2_PLATFORMS_PATH)/Features/Intel/PowerManagement:\
			$(EDK2_PLATFORMS_PATH)/Features/Intel/SystemInformation:\
			$(EDK2_PLATFORMS_PATH)/Features/Intel/UserInterface
else
export PACKAGES_PATH := $(EDK2_PATH)
endif

EDK2_UEFIPAYLOAD_OUT ?= ../../../build/UEFIPAYLOAD.fd
EDK2_UNIVERSAL_PAYLOAD_OUT ?= ../../../build/UniversalPayload.fit
EDK2_CDK2_OUT ?= ../../../build/cdk2-coreboot-image.elf
HOSTCC ?= cc
ifndef CDK2_NATIVE_CC
CDK2_NATIVE_CC := $(GCC_CC_x86_64)
endif
ifndef CDK2_NATIVE_OBJCOPY
CDK2_NATIVE_OBJCOPY := $(OBJCOPY_x86_64)
endif

CDK2_COREBOOT_CONFIG_KEYS := \
	CONFIG_CDK2_BUILD_DEBUG \
	CONFIG_CDK2_SERIAL \
	CONFIG_CDK2_CBMEM_CONSOLE \
	CONFIG_CDK2_CBMEM_TIMESTAMPS \
	CONFIG_CDK2_SMMSTORE \
	CONFIG_CDK2_CAPSULE \
	CONFIG_CDK2_CAPSULE_MAIN_FW_GUID \
	CONFIG_CDK2_SECURE_BOOT \
	CONFIG_CDK2_SETUP_UI \
	CONFIG_CDK2_SHELL \
	CONFIG_CDK2_PS2_KEYBOARD \
	CONFIG_CDK2_TPM12 \
	CONFIG_CDK2_TPM2 \
	CONFIG_CDK2_CPU_TIMER \
	CONFIG_CDK2_BOOT_TIMEOUT \
	CONFIG_CDK2_LAPIC_TIMER \
	CONFIG_CDK2_HPET_TIMER

ifeq ($(CONFIG_EDK2_UEFIPAYLOAD),y)
BUILD_STR += -p UefiPayloadPkg/UefiPayloadPkg.dsc
endif
BUILD_STR += -t GCC
BUILD_STR += -D BOOTLOADER=COREBOOT
ifneq ($(V),1)
BUILD_STR += -q
ifeq ($(CONFIG_EDK2_UEFIPAYLOAD),y)
BUILD_STR += -s
endif
endif

#
# EDK II (edk2/master) has the following build options relevant to coreboot:
#
#
# OPTION			= DEFAULT_VALUE
#
# BOOTSPLASH_IMAGE		= FALSE
ifneq ($(CONFIG_EDK2_BOOTSPLASH_FILE),)
BUILD_STR += -D BOOTSPLASH_IMAGE=TRUE
endif
# BOOT_MANAGER_ESCAPE		= FALSE
ifeq ($(CONFIG_EDK2_BOOT_MANAGER_ESCAPE),y)
BUILD_STR += -D BOOT_MANAGER_ESCAPE=TRUE
endif
# BUILD_TARGETS			= DEBUG
ifeq ($(CONFIG_EDK2_DEBUG),y)
RELEASE_STR = DEBUG
else
RELEASE_STR = RELEASE
endif
# CPU_TIMER_LIB_ENABLE		= TRUE
ifneq ($(CONFIG_EDK2_CPU_TIMER_LIB),y)
BUILD_STR += -D CPU_TIMER_LIB_ENABLE=FALSE
else
BUILD_STR += --pcd gUefiCpuPkgTokenSpaceGuid.PcdCpuCoreCrystalClockFrequency=$(CONFIG_CPU_XTAL_HZ)
endif
# DISABLE_SERIAL_TERMINAL	= FALSE
ifneq ($(CONFIG_EDK2_SERIAL_SUPPORT),y)
BUILD_STR += -D SERIAL_DRIVER_ENABLE=FALSE
BUILD_STR += -D DISABLE_SERIAL_TERMINAL=TRUE
endif
# VARIABLE_SUPPORT		= EMU
# MAX_VARIABLE_SIZE		= 0x10000
ifeq ($(CONFIG_SMMSTORE),y)
BUILD_STR += -D VARIABLE_SUPPORT=SMMSTORE
BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdMaxVariableSize=0x8000
endif
# PCIE_BASE_ADDRESS		= 0
ifneq ($(CONFIG_ECAM_MMCONF_LENGTH),)
BUILD_STR += --pcd gEfiMdePkgTokenSpaceGuid.PcdPciExpressBaseAddress=$(CONFIG_ECAM_MMCONF_BASE_ADDRESS)
endif
# PCIE_BASE_LENGTH		= 0
ifneq ($(CONFIG_ECAM_MMCONF_LENGTH),)
BUILD_STR += --pcd gEfiMdePkgTokenSpaceGuid.PcdPciExpressBaseSize=$(CONFIG_ECAM_MMCONF_LENGTH)
endif
# PS2_KEYBOARD_ENABLE		= FALSE
ifeq ($(CONFIG_EDK2_PS2_SUPPORT),y)
BUILD_STR += -D PS2_KEYBOARD_ENABLE=TRUE
endif
# PLATFORM_BOOT_TIMEOUT		= 3
ifneq ($(CONFIG_EDK2_BOOT_TIMEOUT),)
BUILD_STR += -D PLATFORM_BOOT_TIMEOUT=$(CONFIG_EDK2_BOOT_TIMEOUT)
endif
# SIO_BUS_ENABLE		= FALSE
ifeq ($(CONFIG_EDK2_PS2_SUPPORT),y)
BUILD_STR += -D SIO_BUS_ENABLE=TRUE
endif
# Avoid probing every ACPI EC protocol when coreboot already knows the type.
# Zero retains EDK2's automatic probing for platforms without a mapping here.
ifeq ($(CONFIG_EC_STARLABS_MERLIN),y)
BUILD_STR += --pcd gUefiPayloadPkgTokenSpaceGuid.PcdEcAcpiBatteryProfile=2
else ifeq ($(CONFIG_EC_GOOGLE_CHROMEEC),y)
BUILD_STR += --pcd gUefiPayloadPkgTokenSpaceGuid.PcdEcAcpiBatteryProfile=1
else ifeq ($(CONFIG_BOARD_EMULATION_QEMU_X86_Q35),y)
BUILD_STR += --pcd gUefiPayloadPkgTokenSpaceGuid.PcdEcAcpiBatteryProfile=4
endif
# SHELL_TYPE			= BUILD_SHELL
ifneq ($(CONFIG_EDK2_HAVE_EFI_SHELL),y)
BUILD_STR += -D SHELL_TYPE=NONE
endif
# USE_CBMEM_FOR_CONSOLE		= FALSE
ifeq ($(CONFIG_EDK2_CBMEM_LOGGING),y)
BUILD_STR += -D USE_CBMEM_FOR_CONSOLE=TRUE
BUILD_STR += -D CBMEM_TIMESTAMPS=TRUE
endif
# USE_SPI_FLASH_FOR_CONSOLE	= FALSE
ifeq ($(CONFIG_EDK2_SPI_FLASH_LOGGING),y)
BUILD_STR += -D USE_CBMEM_FOR_CONSOLE=TRUE
BUILD_STR += -D USE_SPI_FLASH_FOR_CONSOLE=TRUE
endif
# SD_MMC_TIMEOUT		= 1000000
ifneq ($(CONFIG_EDK2_SD_MMC_TIMEOUT),)
BUILD_STR += -D SD_MMC_TIMEOUT=$(shell echo $$(( $(CONFIG_EDK2_SD_MMC_TIMEOUT) * 1000)) )
endif
# EDK2_SECURE_BOOT_SUPPORT      = FALSE
ifeq ($(CONFIG_EDK2_SECURE_BOOT_SUPPORT), y)
BUILD_STR += -D SECURE_BOOT_ENABLE=TRUE
endif
# PCIEXP_SUPPORT_RESIZABLE_BARS = FALSE
ifeq ($(CONFIG_PCIEXP_SUPPORT_RESIZABLE_BARS), y)
BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdPcieResizableBarSupport=TRUE
endif
# Preserve VRT bit in RTC_ADDRESS_REGISTER_D
# AMD SoC sets this every 1.024msec, but the generic code assumes it's read-only and
# hardcoded to 1 like on every Intel chipset since Intel ICH.
ifeq ($(CONFIG_SOC_AMD_COMMON_BLOCK_PSP_GEN2), y)
BUILD_STR += --pcd gPcAtChipsetPkgTokenSpaceGuid.PcdInitialValueRtcRegisterD=0x80
endif
# CAPSULE_SUPPORT		= FALSE
# CAPSULE_MAIN_FW_GUID	=
ifeq ($(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES),y)
BUILD_STR += -D CAPSULE_SUPPORT=TRUE
BUILD_STR += -D ESRT_SUPPORT=TRUE
BUILD_STR += -D CAPSULE_MAIN_FW_GUID=$(CONFIG_DRIVERS_EFI_MAIN_FW_GUID)
endif
ifeq ($(CONFIG_DRIVERS_EFI_CAPSULE_ACCEPT_EMBEDDED_DRIVERS),y)
BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdCapsuleEmbeddedDriverSupport=TRUE
EDK2_CDK2_EXTRA_DEFINES += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdCapsuleEmbeddedDriverSupport=TRUE
endif
ifeq ($(CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT),y)
BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdCapsuleOnDiskSupport=TRUE
EDK2_CDK2_EXTRA_DEFINES += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdCapsuleOnDiskSupport=TRUE
endif
ifeq ($(CONFIG_DRIVERS_EFI_CAPSULE_EMBED_FMP_DXE),y)
BUILD_STR += -D CAPSULE_EMBED_FMP_DXE=TRUE
EDK2_CDK2_EXTRA_DEFINES += -D CAPSULE_EMBED_FMP_DXE=TRUE
endif

CAPSULE_GUID := $(call strip_quotes,$(CONFIG_DRIVERS_EFI_MAIN_FW_GUID))
CAPSULE_REGIONS := $(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_REGIONS))
CAPSULE_LOCALVERSION := $(call strip_quotes,$(CONFIG_LOCALVERSION))
CAPSULE_SIGNER_PRIVATE_CERT := $(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_SIGNER_PRIVATE_CERT))
CAPSULE_OTHER_PUBLIC_CERT := $(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_OTHER_PUBLIC_CERT))
CAPSULE_TRUSTED_PUBLIC_CERT := $(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT))

CAPSULE_SIGNER_PRIVATE_CERT_PATH := $(call edk2_abspath,$(CAPSULE_SIGNER_PRIVATE_CERT))
CAPSULE_OTHER_PUBLIC_CERT_PATH := $(call edk2_abspath,$(CAPSULE_OTHER_PUBLIC_CERT))
CAPSULE_TRUSTED_PUBLIC_CERT_PATH := $(call edk2_abspath,$(CAPSULE_TRUSTED_PUBLIC_CERT))

# Workspace-local PCD include for FmpDxe; path is relative to EDK2 WORKSPACE for DSC (!include).
CAPSULE_FMP_PCD_INC_REL := Conf/CapsuleFmpPkcs7Pcd.inc
CAPSULE_FMP_PCD_INC_PCD_NAME := gFmpDevicePkgTokenSpaceGuid.PcdFmpDevicePkcs7CertBufferXdr
ifeq ($(CONFIG_EDK2_CDK2),y)
CAPSULE_FMP_PCD_INC_PATH := $(EDK2_PATH)/$(CAPSULE_FMP_PCD_INC_REL)
else
CAPSULE_FMP_PCD_INC_PATH := $(WORKSPACE)/$(CAPSULE_FMP_PCD_INC_REL)
endif

ifeq ($(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES),y)
ifneq ($(CAPSULE_TRUSTED_PUBLIC_CERT),)
BUILD_STR += -D FMP_DEVICE_PKCS7_PCD_INC=$(CAPSULE_FMP_PCD_INC_REL)
EDK2_CDK2_EXTRA_DEFINES += -D FMP_DEVICE_PKCS7_PCD_INC=$(CAPSULE_FMP_PCD_INC_REL)
endif
endif

# TODO @starlabs: Depends on unmerged EDK2 patches.
# AppendRmapManifest.py moved from BaseTools/Scripts to UefiPayloadPkg/Tools in
# newer edk2 trees.
EDK2_APPEND_RMAP_MANIFEST = $(firstword $(wildcard \
	$(EDK2_PATH)/UefiPayloadPkg/Tools/AppendRmapManifest.py \
	$(EDK2_PATH)/BaseTools/Scripts/AppendRmapManifest.py \
))

ifeq ($(CONFIG_EDK2_CDK2),y)
EDK2_FMP_DXE = $(EDK2_PATH)/Build/cdk2-coreboot/edk2/UefiPayloadPkgX64/$(RELEASE_STR)_GCC/X64/FmpDxe.efi
else
# If FmpDxe is built, edk2 also places it in the top-level X64 output dir.
EDK2_FMP_DXE = $(WORKSPACE)/Build/UefiPayloadPkgX64/$(RELEASE_STR)_GCC/X64/FmpDxe.efi
endif

define EDK2_ENSURE_FMP_CERT_INC
	if [ "$(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES)" = "y" ] && [ -n "$(CAPSULE_TRUSTED_PUBLIC_CERT)" ]; then \
		type openssl > /dev/null 2>&1 || { echo "ERROR: Please install openssl."; exit 1; }; \
		test -f "$(CAPSULE_TRUSTED_PUBLIC_CERT_PATH)"; \
		binto_pcd="$(EDK2_PATH)/BaseTools/Scripts/BinToPcd.py"; \
		if [ ! -f "$$binto_pcd" ]; then \
			echo "ERROR: BinToPcd.py not found (expected EDK2 BaseTools/Scripts/BinToPcd.py)."; \
			exit 1; \
		fi; \
		mkdir -p "$(dir $(CAPSULE_FMP_PCD_INC_PATH))"; \
		der="$(WORKSPACE)/.capsule_fmp_cert.$$$$.der"; \
		openssl x509 -in "$(CAPSULE_TRUSTED_PUBLIC_CERT_PATH)" -outform der -out "$$der" || exit 1; \
		python3 "$$binto_pcd" -q -x -i "$$der" \
			-o "$(CAPSULE_FMP_PCD_INC_PATH)" -p "$(CAPSULE_FMP_PCD_INC_PCD_NAME)" || exit 1; \
		rm -f "$$der"; \
	fi;
endef

#
# One or more downstream edk2 repositories support the following additional options:
#
ifeq ($(CONFIG_EDK2_REPO_OFFICIAL),)

# FOLLOW_BGRT_SPEC		= FALSE
ifeq ($(CONFIG_EDK2_FOLLOW_BGRT_SPEC),y)
BUILD_STR += -D FOLLOW_BGRT_SPEC=TRUE
endif
# GOP_DRIVER			= FALSE
ifeq ($(CONFIG_EDK2_GOP_DRIVER), y)
BUILD_STR += -D USE_PLATFORM_GOP=TRUE
endif
# PRIORITIZE_INTERNAL		= FALSE
ifeq ($(CONFIG_EDK2_PRIORITIZE_INTERNAL),y)
BUILD_STR += -D PRIORITIZE_INTERNAL=TRUE
endif
# TPM_ENABLE			= TRUE
ifeq ($(CONFIG_EDK2_DISABLE_TPM),y)
BUILD_STR += -D TPM_ENABLE=FALSE
endif
# TIMER_SUPPORT			= HPET
ifeq ($(CONFIG_EDK2_USE_LAPIC_TIMER),y)
BUILD_STR += -D TIMER_SUPPORT=LAPIC
endif
# LOAD_OPTION_ROMS		= FALSE
ifeq ($(CONFIG_EDK2_LOAD_OPTION_ROMS),y)
BUILD_STR += -D LOAD_OPTION_ROMS=TRUE
endif
# NETWORK_IPXE                  = FALSE
ifeq ($(CONFIG_EDK2_IPXE),y)
BUILD_STR += -D NETWORK_IPXE=TRUE
BUILD_STR += --pcd gUefiPayloadPkgTokenSpaceGuid.PcdiPXEOptionName=L"$(CONFIG_EDK2_IPXE_OPTION_NAME)"
endif
# EDK2_USE_LVGL_RENDERER		= FALSE
ifeq ($(CONFIG_EDK2_USE_LVGL_RENDERER),y)
BUILD_STR += -D LVGL_ENABLE=TRUE
EDK2_CDK2_EXTRA_DEFINES += -D LVGL_ENABLE=TRUE
endif

endif # !CONFIG_EDK2_REPO_OFFICIAL

#
# EDKII has the below PCDs that are relevant to coreboot:
#
# Allows EDKII to use the full framebuffer
ifeq ($(CONFIG_EDK2_FULL_SCREEN_SETUP),y)
BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow=0
BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn=0
BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutRow=0
BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutColumn=0
else
BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow=40
BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn=128
BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutRow=40
BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutColumn=128
endif

BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemId="COREv4"
BUILD_STR += --pcd gUefiCpuPkgTokenSpaceGuid.PcdFirstTimeWakeUpAPsBySipi=FALSE

# Coreboot already bounds MP initialization to the board's CPU capacity.
# Pass that bound to EDK2 instead of retaining its generic 1024-CPU default.
BUILD_STR += --pcd gUefiCpuPkgTokenSpaceGuid.PcdCpuMaxLogicalProcessorNumber=$(CONFIG_MAX_CPUS)

# coreboot produces SMBIOS 3.0.0 tables
BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosVersion=0x0300
BUILD_STR += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosDocRev=0x0

bootloader = $(word 8,$(subst /, ,$(BUILD_STR)))

ifneq ($(CONFIG_EDK2_CUSTOM_BUILD_PARAMS),)
BUILD_STR += $(CONFIG_EDK2_CUSTOM_BUILD_PARAMS)
EDK2_CDK2_EXTRA_DEFINES += $(CONFIG_EDK2_CUSTOM_BUILD_PARAMS)
endif

ifeq ($(CONFIG_EDK2_CDK2),y)
all: cdk2
else
all: UefiPayloadPkg
endif

$(WORKSPACE):
	mkdir -p $(WORKSPACE)

$(EDK2_PLATFORMS_PATH): $(WORKSPACE)
	if [ ! -d "$(EDK2_PLATFORMS_PATH)" ]; then \
		git clone --recurse-submodules $(CONFIG_EDK2_PLATFORMS_REPOSITORY) $(EDK2_PLATFORMS_PATH) -j5; \
	fi
	cd $(EDK2_PLATFORMS_PATH); \
		if ! git rev-parse --verify -q $(CONFIG_EDK2_PLATFORMS_TAG_OR_REV)^{object} >/dev/null; then \
			echo "    $(CONFIG_EDK2_PLATFORMS_TAG_OR_REV) is not a valid git reference"; \
			exit 1; \
		fi; \
		if git status --ignore-submodules=dirty | grep -q "nothing to commit, working tree clean"; then \
			echo "    Checking out edk2-platforms revision $(CONFIG_EDK2_PLATFORMS_TAG_OR_REV)"; \
			git checkout --detach $(CONFIG_EDK2_PLATFORMS_TAG_OR_REV) -f; \
		else \
			echo "    Working directory not clean; will not overwrite"; \
		fi; \
		git submodule update --init --checkout

$(EDK2_PATH): $(WORKSPACE)
	if [ ! -d "$(EDK2_PATH)" ]; then \
		git clone $(EDK2_CLONE_FLAGS) $(CONFIG_EDK2_REPOSITORY) $(EDK2_PATH) -j5; \
	fi
	# FDF RAW paths are relative to WORKSPACE, not PACKAGES_PATH.  Keep cdk2
	# third-party objects visible when the repository is staged below WORKSPACE.
	if [ -d "$(EDK2_PATH)/3rdparty" ]; then \
		if [ -L "$(WORKSPACE)/3rdparty" ] && [ "$$(readlink "$(WORKSPACE)/3rdparty")" != "$(EDK2_PATH)/3rdparty" ]; then \
			rm "$(WORKSPACE)/3rdparty"; \
		fi; \
		if [ ! -e "$(WORKSPACE)/3rdparty" ] && [ ! -L "$(WORKSPACE)/3rdparty" ]; then \
			ln -s "$(EDK2_PATH)/3rdparty" "$(WORKSPACE)/3rdparty"; \
		fi; \
	fi
	cd $(EDK2_PATH); \
		git checkout MdeModulePkg/Logo/Logo.bmp > /dev/null 2>&1 || true; \
		echo "    Fetching new commits from $(CONFIG_EDK2_REPOSITORY)"; \
		git fetch origin 2>/dev/null; \
		if ! git rev-parse --verify -q $(CONFIG_EDK2_TAG_OR_REV)^{object} >/dev/null; then \
			echo "    $(CONFIG_EDK2_TAG_OR_REV) is not a valid git reference"; \
			exit 1; \
		fi; \
		if git status --ignore-submodules=dirty | grep -q -e clean -e "nothing added"; then \
			echo "    Checking out $(project_name) revision $(CONFIG_EDK2_TAG_OR_REV)"; \
			git checkout --detach $(CONFIG_EDK2_TAG_OR_REV) -f; \
		git submodule update $(EDK2_SUBMODULE_FLAGS); \
		else \
			echo "    Working directory not clean; will not overwrite"; \
		fi; \
		git submodule update $(EDK2_SUBMODULE_FLAGS)

logo: $(EDK2_PATH)
	case "$(CONFIG_EDK2_BOOTSPLASH_FILE)" in \
		"") ;; \
		/*) convert -background None $(CONFIG_EDK2_BOOTSPLASH_FILE) \
			BMP3:$(EDK2_PATH)/MdeModulePkg/Logo/Logo.bmp;; \
		*) convert -background None $(top)/$(CONFIG_EDK2_BOOTSPLASH_FILE) \
			BMP3:$(EDK2_PATH)/MdeModulePkg/Logo/Logo.bmp;; \
	esac \

gop_driver: $(EDK2_PATH)
	if [ -n "$(CONFIG_EDK2_GOP_DRIVER)" ]; then \
		echo "Using GOP driver $(CONFIG_EDK2_GOP_FILE)"; \
		cp $(top)/$(CONFIG_EDK2_GOP_FILE) $(EDK2_PATH)/UefiPayloadPkg/IntelGopDriver.efi; \
		cp $(top)/$(CONFIG_INTEL_GMA_VBT_FILE) $(EDK2_PATH)/UefiPayloadPkg/vbt.bin; \
	fi; \

ipxe_rom: $(EDK2_PATH)
	if [ "$(CONFIG_EDK2_IPXE)" == "y" ]; then \
		echo "    Including externally built iPXE"; \
		mkdir -p $(EDK2_PATH)/UefiPayloadPkg/NetworkDrivers/; \
		cp $(top)/payloads/external/iPXE/ipxe/ipxe.rom \
			$(EDK2_PATH)/UefiPayloadPkg/NetworkDrivers/ipxe.efi; \
	fi \

checktools:
	echo -n "EDK2: Checking uuid-dev:"
	echo "#include <uuid/uuid.h>" > libtest.c
	echo "int main(int argc, char **argv) { (void) argc; (void) argv; return 0; }" >> libtest.c
	$(HOSTCC) $(HOSTCCFLAGS) libtest.c -o libtest >/dev/null 2>&1 && echo "    Found!" || \
		( echo "    Not found!"; \
			echo "ERROR: please_install uuid-dev (libuuid-devel)"; exit 1 )
	rm -rf libtest.c libtest
	echo -n "EDK2: Checking nasm:"
	type nasm > /dev/null 2>&1 && echo "        Found!" || \
		( echo "        Not found!"; echo "ERROR: Please install nasm."; exit 1 )
	echo -n "EDK2: Checking imagemagick:"
	-convert -size 1x1 xc: test.png &> /dev/null;
	if [ -f test.png ]; then \
		rm test.png && echo " Found!"; \
	else \
		echo " Not found!"; \
		echo "ERROR: Please install imagemagick"; \
		exit 1; \
	fi

checktools_cdk2:
	echo -n "EDK2: Checking uuid-dev:"
	echo "#include <uuid/uuid.h>" > libtest.c
	echo "int main(int argc, char **argv) { (void) argc; (void) argv; return 0; }" >> libtest.c
	$(HOSTCC) $(HOSTCCFLAGS) libtest.c -o libtest >/dev/null 2>&1 && echo "    Found!" || \
		( echo "    Not found!"; \
			echo "ERROR: please_install uuid-dev (libuuid-devel)"; exit 1 )
	rm -rf libtest.c libtest
	echo -n "EDK2: Checking nasm:"
	type nasm > /dev/null 2>&1 && echo "        Found!" || \
		( echo "        Not found!"; echo "ERROR: Please install nasm."; exit 1 )

ifeq ($(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES)$(CONFIG_DRIVERS_EFI_GENERATE_CAPSULE),yy)
capsule_keys: $(EDK2_PATH)
	$(EDK2_ENSURE_FMP_CERT_INC)

prep: capsule_keys
EDK2_CDK2_DEPS += capsule_keys
endif

print:
	echo "   ##### $(project_name) Build Summary #####"
	echo "   Repository:     $(CONFIG_EDK2_REPOSITORY)"
	echo "   Branch:         $(CONFIG_EDK2_TAG_OR_REV)"
	echo "   Packages path:  $(PACKAGES_PATH)"
	echo " $(BUILD_STR)" | \
		sed -e 's/ --/ -/g' -e 's/ -/ \n   /g' | sort | sed \
		-e 's/a /Architecture:   /g' \
		-e 's/b /Release:        /g' \
		-e 's/D /Option:         /g' \
		-e 's/pcd /Pcd:            /g' \
		-e 's/p /Payload:        /g' \
		-e 's/q /Build:          Quiet/' \
		-e 's/s /Build:          Silent/' \
		-e 's/t /Toolchain:      /'

prep: $(EDK2_PATH) $(EDK2_PLATFORMS_PATH) clean checktools logo gop_driver ipxe_rom
	cd $(WORKSPACE); \
		source $(EDK2_PATH)/edksetup.sh; \
		unset CC; $(MAKE) -C $(EDK2_PATH)/BaseTools 2>&1; \

$(WORKSPACE)/Build/UefiPayloadPkgX64/$(RELEASE_STR)_GCC/FV/UEFIPAYLOAD.fd: \
	prep print
	cd $(WORKSPACE); \
		source $(EDK2_PATH)/edksetup.sh; \
		$(EDK2_ENSURE_FMP_CERT_INC) \
		echo -n "EDK2: Building... "; \
		build -a IA32 -a X64 -b $(RELEASE_STR) $(BUILD_STR) -D BUILD_ARCH=X64 \
			-y $(WORKSPACE)/Build/UefiPayloadPkgX64/UEFIPAYLOAD.txt; \
		if [ ! -f $@ ]; then \
			echo "Failed!"; \
			exit 1; \
		fi
		echo "Success!"; \

$(WORKSPACE)/Build/UefiPayloadPkgIA32/UniversalPayload.fit: \
	prep print
	cd $(WORKSPACE); \
		source $(EDK2_PATH)/edksetup.sh; \
		$(EDK2_ENSURE_FMP_CERT_INC) \
		echo -n "EDK2: Building... "; \
		bash $(EDK2_PATH)/UefiPayloadPkg/UniversalPayloadBuild.sh -a IA32 -b $(RELEASE_STR) $(BUILD_STR) \
			-D BUILD_ARCH=IA32 --Fit \
			--pcd gUefiPayloadPkgTokenSpaceGuid.PcdHandOffFdtEnable=TRUE
		if [ ! -f $@ ]; then \
			echo "Failed!"; \
			exit 1; \
		fi
		echo "Success!"; \

UefiPayloadPkg: $(WORKSPACE)/Build/UefiPayloadPkgX64/$(RELEASE_STR)_GCC/FV/UEFIPAYLOAD.fd
	mkdir -p $(dir $(EDK2_UEFIPAYLOAD_OUT))
	mv $(WORKSPACE)/Build/UefiPayloadPkgX64/$(RELEASE_STR)_GCC/FV/UEFIPAYLOAD.fd \
		$(EDK2_UEFIPAYLOAD_OUT)

UniversalPayload: $(WORKSPACE)/Build/UefiPayloadPkgIA32/UniversalPayload.fit
	mkdir -p $(dir $(EDK2_UNIVERSAL_PAYLOAD_OUT))
	mv $(WORKSPACE)/Build/UefiPayloadPkgIA32/UniversalPayload.fit \
		$(EDK2_UNIVERSAL_PAYLOAD_OUT)

cdk2: $(EDK2_PATH) checktools_cdk2 $(EDK2_CDK2_DEPS)
	set -e; \
	cdk2_build_dir=Build/cdk2-coreboot; \
	cdk2_defconfig=$(EDK2_PATH)/$$cdk2_build_dir/coreboot.defconfig; \
	mkdir -p $(EDK2_PATH)/$$cdk2_build_dir; \
	test -f $(EDK2_PATH)/UefiPayloadPkg/cdk2/defconfig; \
	tr -d '\r' < $(EDK2_PATH)/UefiPayloadPkg/cdk2/defconfig > $$cdk2_defconfig; \
	cdk2_config_keys=$$cdk2_defconfig.keys; \
	cdk2_base_config=$$cdk2_defconfig.base; \
	printf '%s\n' $(CDK2_COREBOOT_CONFIG_KEYS) > $$cdk2_config_keys; \
	awk 'NR == FNR { keys[$$1] = 1; next } \
	     /^CONFIG_[A-Za-z0-9_]+=/{ split($$1, key, "="); if (key[1] in keys) next } \
	     /^# CONFIG_[A-Za-z0-9_]+ is not set$$/{ if ($$2 in keys) next } \
	     { print }' $$cdk2_config_keys $$cdk2_defconfig > $$cdk2_base_config; \
	mv $$cdk2_base_config $$cdk2_defconfig; \
	cdk2_bool() { \
		if [ "$$2" = "y" ]; then \
			printf 'CONFIG_%s=y\n' "$$1"; \
		else \
			printf '# CONFIG_%s is not set\n' "$$1"; \
		fi; \
	}; \
	{ \
		cdk2_bool CDK2_BUILD_DEBUG "$(CONFIG_EDK2_DEBUG)"; \
		if [ "$(CONFIG_EDK2_SERIAL_SUPPORT)" = "y" ] || [ "$(CONFIG_EDK2_CBMEM_LOGGING)" = "y" ]; then \
			cdk2_bool CDK2_SERIAL y; \
		else \
			cdk2_bool CDK2_SERIAL n; \
		fi; \
		cdk2_bool CDK2_CBMEM_CONSOLE "$(CONFIG_EDK2_CBMEM_LOGGING)"; \
		cdk2_bool CDK2_CBMEM_TIMESTAMPS "$(CONFIG_EDK2_CBMEM_LOGGING)"; \
		cdk2_bool CDK2_SMMSTORE "$(CONFIG_SMMSTORE)"; \
		cdk2_bool CDK2_CAPSULE "$(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES)"; \
		if [ "$(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES)" = "y" ]; then \
			printf 'CONFIG_CDK2_CAPSULE_MAIN_FW_GUID="%s"\n' "$(CAPSULE_GUID)"; \
		fi; \
		cdk2_bool CDK2_SECURE_BOOT "$(CONFIG_EDK2_SECURE_BOOT_SUPPORT)"; \
		cdk2_bool CDK2_SETUP_UI "$(CONFIG_EDK2_CDK2_SETUP_UI)"; \
		cdk2_bool CDK2_SHELL "$(CONFIG_EDK2_HAVE_EFI_SHELL)"; \
		cdk2_bool CDK2_PS2_KEYBOARD "$(CONFIG_EDK2_PS2_SUPPORT)"; \
		if [ "$(CONFIG_EDK2_DISABLE_TPM)" = "y" ]; then \
			cdk2_bool CDK2_TPM12 n; \
			cdk2_bool CDK2_TPM2 n; \
		else \
			cdk2_bool CDK2_TPM12 y; \
			cdk2_bool CDK2_TPM2 y; \
		fi; \
		cdk2_bool CDK2_CPU_TIMER "$(CONFIG_EDK2_CPU_TIMER_LIB)"; \
		if [ -n "$(CONFIG_EDK2_BOOT_TIMEOUT)" ]; then \
			printf 'CONFIG_CDK2_BOOT_TIMEOUT=%s\n' "$(CONFIG_EDK2_BOOT_TIMEOUT)"; \
		fi; \
		if [ "$(CONFIG_EDK2_USE_LAPIC_TIMER)" = "y" ]; then \
			cdk2_bool CDK2_LAPIC_TIMER y; \
			cdk2_bool CDK2_HPET_TIMER n; \
		else \
			cdk2_bool CDK2_LAPIC_TIMER n; \
			cdk2_bool CDK2_HPET_TIMER y; \
		fi; \
	} >> $$cdk2_defconfig; \
	rm -f $$cdk2_config_keys; \
	cd $(EDK2_PATH); \
		unset WORKSPACE PACKAGES_PATH EDK_TOOLS_PATH CONF_PATH CC CFLAGS CPPFLAGS LDFLAGS; \
		source edksetup.sh; \
		unset CC CXX AR; MAKEFLAGS= MFLAGS= MAKEOVERRIDES= $(MAKE) -C BaseTools 2>&1; \
		$(MAKE) MAKEOVERRIDES= CC="$(HOSTCC)" \
			$(if $(CDK2_NATIVE_CC),CDK2_NATIVE_CC="$(CDK2_NATIVE_CC)") \
			$(if $(CDK2_NATIVE_OBJCOPY),CDK2_NATIVE_OBJCOPY="$(CDK2_NATIVE_OBJCOPY)") \
			$(if $(EDK2_CDK2_EXTRA_DEFINES),CDK2_EXTRA_DEFINES="$(EDK2_CDK2_EXTRA_DEFINES)") \
			CDK2_BUILD_DIR=$$cdk2_build_dir \
			CDK2_DEFCONFIG=$$cdk2_defconfig \
			CDK2_BACKEND_OUTPUT_DIRECTORY=$$cdk2_build_dir/edk2/UefiPayloadPkgX64 \
			-f UefiPayloadPkg/cdk2/Makefile defconfig; \
		$(MAKE) MAKEOVERRIDES= CC="$(HOSTCC)" \
			$(if $(CDK2_NATIVE_CC),CDK2_NATIVE_CC="$(CDK2_NATIVE_CC)") \
			$(if $(CDK2_NATIVE_OBJCOPY),CDK2_NATIVE_OBJCOPY="$(CDK2_NATIVE_OBJCOPY)") \
			$(if $(EDK2_CDK2_EXTRA_DEFINES),CDK2_EXTRA_DEFINES="$(EDK2_CDK2_EXTRA_DEFINES)") \
			CDK2_BUILD_DIR=$$cdk2_build_dir \
			CDK2_DEFCONFIG=$$cdk2_defconfig \
			CDK2_BACKEND_OUTPUT_DIRECTORY=$$cdk2_build_dir/edk2/UefiPayloadPkgX64 \
			-f UefiPayloadPkg/cdk2/Makefile build; \
	cdk2_image=$(EDK2_PATH)/$$cdk2_build_dir/native/cdk2-coreboot-image.elf; \
	test -s $$cdk2_image; \
	mkdir -p $(dir $(EDK2_CDK2_OUT)); \
	cp $$cdk2_image $(EDK2_CDK2_OUT)

clean:
	test -d $(WORKSPACE) && (cd $(WORKSPACE); rm -rf Build; rm -f Conf/tools_def.txt Conf/CapsuleFmpPkcs7Pcd.inc) || exit 0
ifeq ($(CONFIG_EDK2_CDK2),y)
	test -d $(EDK2_PATH) && (cd $(EDK2_PATH); rm -rf Build/cdk2-coreboot; rm -f Conf/tools_def.txt Conf/CapsuleFmpPkcs7Pcd.inc) || exit 0
endif

distclean:
	rm -rf $(WORKSPACE)

.PHONY: $(EDK2_PATH) checktools checktools_cdk2 logo UefiPayloadPkg UniversalPayload cdk2 clean distclean

.PHONY: coreboot_capsule capsule_keys

coreboot_capsule: $(EDK2_PATH)
	test -n "$(COREBOOT_ROM)"
	test -n "$(COREBOOT_CAPSULE_OUT)"
	test -f "$(COREBOOT_ROM)"
	test -x "$(EDK2_GENERATE_CAPSULE)"
	test -f "$(CAPSULE_SIGNER_PRIVATE_CERT_PATH)"
	test -f "$(CAPSULE_OTHER_PUBLIC_CERT_PATH)"
	test -f "$(CAPSULE_TRUSTED_PUBLIC_CERT_PATH)"
	rmap_rom="$${COREBOOT_CAPSULE_OUT%.cap}.rmap.rom"; \
	set -e; \
	if [ -f "$(EDK2_APPEND_RMAP_MANIFEST)" ]; then \
		python3 "$(EDK2_APPEND_RMAP_MANIFEST)" -o "$$rmap_rom" $(foreach r,$(CAPSULE_REGIONS),-r $(r)) "$(COREBOOT_ROM)"; \
	else \
		cp "$(COREBOOT_ROM)" "$$rmap_rom"; \
	fi; \
	embedded_driver_args=(); \
	if [ "$(CONFIG_DRIVERS_EFI_CAPSULE_EMBED_FMP_DXE)" = "y" ]; then \
		test -f "$(EDK2_FMP_DXE)"; \
		embedded_driver_args=( --embedded-driver "$(EDK2_FMP_DXE)" ); \
	fi; \
	fw_version=$$(( $(CONFIG_DRIVERS_EFI_MAIN_FW_VERSION) )); \
	if [ $$fw_version -eq 0 ]; then \
		if [[ "$(CAPSULE_LOCALVERSION)" =~ ^[^0-9]*([0-9]+)[.]([0-9]+) ]]; then \
			fw_version=$$(( (10#$${BASH_REMATCH[1]} << 16) | 10#$${BASH_REMATCH[2]} )); \
		fi; \
	fi; \
	lsv=$$(( $(CONFIG_DRIVERS_EFI_MAIN_FW_LSV) )); \
	if [ $$lsv -eq 0 ]; then \
		lsv=$$fw_version; \
	fi; \
	capflags=( --capflag PersistAcrossReset ); \
	if [ "$(CONFIG_DRIVERS_EFI_CAPSULE_INITIATE_RESET)" = "y" ]; then \
		capflags+=( --capflag InitiateReset ); \
	fi; \
	"$(EDK2_GENERATE_CAPSULE)" -e -o "$(COREBOOT_CAPSULE_OUT)" \
		--guid "$(CAPSULE_GUID)" \
		"$${capflags[@]}" \
		--fw-version $$fw_version --lsv $$lsv \
		--signer-private-cert "$(CAPSULE_SIGNER_PRIVATE_CERT_PATH)" \
		--other-public-cert "$(CAPSULE_OTHER_PUBLIC_CERT_PATH)" \
		--trusted-public-cert "$(CAPSULE_TRUSTED_PUBLIC_CERT_PATH)" \
		"$${embedded_driver_args[@]}" \
		"$$rmap_rom"
