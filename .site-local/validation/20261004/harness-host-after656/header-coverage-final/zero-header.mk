# SPDX-License-Identifier: GPL-2.0-only

# Native cdk2 stage build fragment. It is included after the top-level Kconfig
# has been resolved, so the same generated config controls the freestanding and
# coreboot entry builds.

ifeq ($(filter extra-prereqs,$(.FEATURES)),)
$(error Native cdk2 configuration dependencies require GNU Make extra-prereqs support)
endif

CDK2_NATIVE_HOST_SANITIZERS := -fsanitize=address,undefined

# Retain every translation unit's dependency rule from the successful compile.
# A shared -MF filename would otherwise retain only the last input's headers.
define cdk2_native_host_test
	@set -e; dependency="$@.d.$$$$.tmp"; \
		trap 'rm -f "$$dependency"' EXIT HUP INT TERM; \
		$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(1) \
			-MMD -MP -MF - -MT "$@" -o "$@" $(2) > "$$dependency"; \
		mv -f "$$dependency" "$@.d"; trap - EXIT HUP INT TERM
endef

CDK2_NATIVE_BUILD_DIR ?= $(CDK2_BUILD_DIR)/native
ifneq ($(abspath $(CDK2_NATIVE_BUILD_DIR)),$(abspath $(CDK2_BUILD_DIR)/native))
$(error CDK2_NATIVE_BUILD_DIR must be CDK2_BUILD_DIR/native; resolved configuration and native artifacts would diverge)
endif
CDK2_UTIL_DIR := $(CDK2_DIR)/util
CDK2_LIB_DIR := $(CDK2_DIR)/src/lib
CDK2_NATIVE_ELF ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-stage.elf
CDK2_NATIVE_MAP ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-stage.map
CDK2_NATIVE_COREBOOT_ELF ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-coreboot-stage.elf
CDK2_NATIVE_COREBOOT_MAP ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-coreboot-stage.map
CDK2_NATIVE_COREBOOT_IMAGE ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-coreboot-image.elf
CDK2_NATIVE_COREBOOT_IMAGE_MAP ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-coreboot-image.map
CDK2_NATIVE_COREBOOT_PAYLOAD_DEPS = \
	$(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY) \
	$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_OBJ) \
	$(CDK2_NATIVE_DIRECT_COMPOSITION_INVENTORY)
CDK2_NATIVE_COREBOOT_PAYLOAD_INPUTS = "$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_OBJ)"
CDK2_NATIVE_COREBOOT_LINKER_SCRIPT = $(CDK2_NATIVE_DIR)/cdk2_direct.ld
CDK2_NATIVE_COREBOOT_IMAGE_VALIDATION_DEPS = \
	$(CDK2_NATIVE_ELF_CHECK) native-direct-mtrr-ownership-audit \
	$(if $(filter y,$(CONFIG_CDK2_QEMU_ACCEPTANCE_PROFILE)),\
		native-direct-composition-contract-test)
CDK2_NATIVE_LINEAR_ADMISSION_COMMAND = \
	sh "$(CDK2_DIR)/util/direct-image-inventory" --check-linear \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_MODULE_REGISTRY)"
CDK2_NATIVE_DIRECT_LINK_MAPS := $(CDK2_NATIVE_MAP) $(CDK2_NATIVE_COREBOOT_MAP) $(CDK2_NATIVE_COREBOOT_IMAGE_MAP)
CDK2_NATIVE_OVERRIDE_ELF ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-stage-override.elf
CDK2_NATIVE_OVERRIDE_MAP ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-stage-override.map
CDK2_NATIVE_PE_EXEC_SECTIONS := $(CDK2_NATIVE_BUILD_DIR)/cdk2-pe-exec-sections
CDK2_NATIVE_PE_EXEC_FIXTURE := $(CDK2_NATIVE_BUILD_DIR)/pe-exec-sections-fixture
CDK2_NATIVE_PE_RELOCATION_LOAD_TEST := \
	$(CDK2_NATIVE_BUILD_DIR)/pe-relocation-load-test
CDK2_NATIVE_DIRECT_PCD_CLOSURE_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-direct-pcd-closure-test
CDK2_NATIVE_BUILD_DIR_COHERENCE_TEST ?= \
	$(CDK2_DIR)/tests/native_build_dir_coherence_test.sh
# Declared here because the cross-module diagnostic inventory is defined before
# these modules' implementation blocks later in this file.
CDK2_NATIVE_FTW_PE ?= $(CDK2_NATIVE_BUILD_DIR)/FaultTolerantWriteDxe.efi
CDK2_NATIVE_CON_SPLITTER_PE ?= $(CDK2_NATIVE_BUILD_DIR)/ConSplitterDxe.efi
CDK2_NATIVE_SHELL_PE ?= $(CDK2_NATIVE_BUILD_DIR)/Shell.efi
CDK2_NATIVE_PERELOCCHECK ?= $(CDK2_NATIVE_PE_EXEC_SECTIONS)
CDK2_NATIVE_PE_LINK ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-native-pe-link
CDK2_NATIVE_PE_RELOCATION_MARKER_OBJ ?= \
	$(CDK2_NATIVE_BUILD_DIR)/pe-relocation-marker.o
CDK2_NATIVE_METRONOME_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-metronome-test
CDK2_NATIVE_SCSI_BUS_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-scsi-bus-test
CDK2_NATIVE_SCSI_BINDING_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-scsi-binding-test
CDK2_NATIVE_SCSI_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-scsi-entry-test
CDK2_NATIVE_SCSI_DEPENDENCY_TEST ?= $(CDK2_DIR)/tests/scsi_bus_dependency_test.sh
CDK2_NATIVE_SCSI_BUS_OBJS := $(CDK2_NATIVE_BUILD_DIR)/scsi-bus-model.o \
	$(CDK2_NATIVE_BUILD_DIR)/scsi-bus-binding.o \
	$(CDK2_NATIVE_BUILD_DIR)/scsi-bus-entry.o
CDK2_NATIVE_SCSI_BUS_PE ?= $(CDK2_NATIVE_BUILD_DIR)/ScsiBusDxe.efi
CDK2_NATIVE_SCSI_BUS_QEMU_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/scsi-bus-qemu.o
CDK2_NATIVE_SCSI_BUS_QEMU_PE ?= $(CDK2_NATIVE_BUILD_DIR)/scsi-bus-qemu.efi
CDK2_NATIVE_SCSI_DISK_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-scsi-disk-test
CDK2_NATIVE_SCSI_DISK_IO_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-scsi-disk-io-test
CDK2_NATIVE_SCSI_DISK_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/scsi-disk-,\
	model.o io.o async.o block.o binding.o backend.o disk_info.o entry.o diagnostic.o)
CDK2_NATIVE_SCSI_DISK_DIAG_CORE ?= $(CDK2_NATIVE_BUILD_DIR)/scsi-disk-diagnostic-core.o
CDK2_NATIVE_SCSI_DISK_PE ?= $(CDK2_NATIVE_BUILD_DIR)/ScsiDisk.efi
CDK2_NATIVE_SCSI_DISK_QEMU_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/scsi-disk-qemu.o
CDK2_NATIVE_SCSI_DISK_QEMU_PE ?= $(CDK2_NATIVE_BUILD_DIR)/scsi-disk-qemu.efi
CDK2_NATIVE_XHCI_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/xhci-model-test
CDK2_NATIVE_XHCI_CONTROLLER_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/xhci-controller-test
CDK2_NATIVE_XHCI_PCI_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/xhci-pci-test
CDK2_NATIVE_XHCI_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/xhci-,\
	model.o controller.o pci_adapter.o usb2_abi.o diagnostic.o entry.o deadline.o)
CDK2_NATIVE_XHCI_PE ?= $(CDK2_NATIVE_BUILD_DIR)/XhciDxe.efi
CDK2_NATIVE_USB_BUS_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/usb-bus-,\
	model.o usb_io.o bus.o binding.o entry.o diagnostic.o)
CDK2_NATIVE_USB_BUS_DIAG_CORE ?= $(CDK2_NATIVE_BUILD_DIR)/usb-bus-diagnostic-core.o
CDK2_NATIVE_USB_BUS_PE ?= $(CDK2_NATIVE_BUILD_DIR)/UsbBusDxe.efi
CDK2_NATIVE_USB_BUS_LINEARITY_TEST ?= $(CDK2_DIR)/tests/usb_bus_linearity_test.sh
CDK2_NATIVE_USB_BUS_ROLLBACK_MUTATION_TEST ?= \
	$(CDK2_DIR)/tests/usb_bus_rollback_mutation_test.sh
CDK2_NATIVE_USB_BUS_IO_MUTATION_TEST ?= \
	$(CDK2_DIR)/tests/usb_bus_io_mutation_test.sh
CDK2_NATIVE_LINEAR_CONNECT_OWNERSHIP_TEST ?= \
	$(CDK2_DIR)/tests/linear_connect_ownership_test.sh
CDK2_NATIVE_USB_MASS_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/usb-mass-,\
	model.o transport.o scsi.o block.o binding.o entry.o diagnostic.o)
CDK2_NATIVE_USB_MASS_DIAG_CORE ?= $(CDK2_NATIVE_BUILD_DIR)/usb-mass-diagnostic-core.o
CDK2_NATIVE_USB_MASS_PE ?= $(CDK2_NATIVE_BUILD_DIR)/UsbMassStorageDxe.efi
CDK2_NATIVE_USB_MASS_QEMU_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/usb-mass-qemu.o
CDK2_NATIVE_USB_MASS_QEMU_PE ?= $(CDK2_NATIVE_BUILD_DIR)/usb-mass-qemu.efi
CDK2_NATIVE_USB_KEYBOARD_OBJS := $(addprefix \
	$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-,model.o transport.o protocol.o \
	binding.o entry.o)
CDK2_NATIVE_USB_KEYBOARD_PE ?= $(CDK2_NATIVE_BUILD_DIR)/UsbKbDxe.efi
CDK2_NATIVE_USB_MOUSE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-usb-mouse-test
CDK2_NATIVE_USB_MOUSE_ENTRY_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-usb-mouse-entry-test
CDK2_NATIVE_USB_MOUSE_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/usb-mouse-,\
	model.o binding.o entry.o)
CDK2_NATIVE_USB_MOUSE_PE ?= $(CDK2_NATIVE_BUILD_DIR)/UsbMouseDxe.efi
CDK2_NATIVE_SIO_BUS_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-sio-bus-test
CDK2_NATIVE_SIO_BUS_ENTRY_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-sio-bus-entry-test
CDK2_NATIVE_SIO_BUS_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/sio-bus-,\
	sio_bus.o binding.o entry.o)
CDK2_NATIVE_SIO_BUS_PE ?= $(CDK2_NATIVE_BUILD_DIR)/SioBusDxe.efi
CDK2_NATIVE_PS2_MOUSE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ps2-mouse-test
CDK2_NATIVE_PS2_MOUSE_DRIVER_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-ps2-mouse-driver-test
CDK2_NATIVE_PS2_MOUSE_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/ps2-mouse-,\
	ps2_mouse.o driver.o entry.o)
CDK2_NATIVE_PS2_MOUSE_PE ?= $(CDK2_NATIVE_BUILD_DIR)/Ps2MouseDxe.efi
CDK2_NATIVE_TERMINAL_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-terminal-test
CDK2_NATIVE_TERMINAL_DRIVER_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-terminal-driver-test
CDK2_NATIVE_TERMINAL_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/terminal-,\
	terminal.o driver.o entry.o)
CDK2_NATIVE_TERMINAL_PE ?= $(CDK2_NATIVE_BUILD_DIR)/TerminalDxe.efi
CDK2_NATIVE_TERMINAL_DIAGNOSTIC_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/terminal-diagnostic-test
CDK2_NATIVE_TERMINAL_DIAG_CORE ?= \
	$(CDK2_NATIVE_BUILD_DIR)/terminal-diagnostic-core.o
CDK2_NATIVE_GRAPHICS_OUTPUT_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-graphics-output-test
CDK2_NATIVE_GRAPHICS_OUTPUT_DRIVER_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-graphics-output-driver-test
CDK2_NATIVE_GRAPHICS_OUTPUT_OBJS := $(addprefix \
	$(CDK2_NATIVE_BUILD_DIR)/graphics-output-,graphics_output.o driver.o \
	diagnostic.o entry.o)
CDK2_NATIVE_GRAPHICS_OUTPUT_PE ?= \
	$(CDK2_NATIVE_BUILD_DIR)/GraphicsOutputDxe.efi
CDK2_NATIVE_ACPI_TABLE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-acpi-table-test
CDK2_NATIVE_ACPI_TABLE_DRIVER_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-acpi-table-driver-test
CDK2_NATIVE_ACPI_TABLE_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/acpi-table-,\
	acpi_table.o diagnostic.o driver.o)
CDK2_NATIVE_ACPI_TABLE_PE ?= $(CDK2_NATIVE_BUILD_DIR)/AcpiTableDxe.efi
CDK2_NATIVE_ACPI_TABLE_DIAGNOSTIC_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/acpi-table-diagnostic-test
CDK2_NATIVE_ACPI_TABLE_DIAG_CORE ?= \
	$(CDK2_NATIVE_BUILD_DIR)/acpi-table-diagnostic-core.o
CDK2_NATIVE_SMMSTORE_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/smmstore-,\
	smmstore.o variable_store.o fvb.o diagnostic.o driver.o)
CDK2_NATIVE_SMMSTORE_PE ?= \
	$(CDK2_NATIVE_BUILD_DIR)/SmmStoreFvbRuntimeDxe.efi
CDK2_NATIVE_SMMSTORE_DIAGNOSTIC_DEBUG_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/smmstore-diagnostic-debug-test
CDK2_NATIVE_SMMSTORE_DIAGNOSTIC_RELEASE_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/smmstore-diagnostic-release-test
CDK2_NATIVE_SATA_CONTROLLER_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-sata-controller-test
CDK2_NATIVE_ATA_ATAPI_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ata-atapi-test
CDK2_NATIVE_ATA_ATAPI_BINDING_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ata-atapi-binding-test
CDK2_NATIVE_ATA_ATAPI_AHCI_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ata-atapi-ahci-test
CDK2_NATIVE_ATA_ATAPI_IDE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ata-atapi-ide-test
CDK2_NATIVE_ATA_ATAPI_PCI_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ata-atapi-pci-test
CDK2_NATIVE_ATA_ATAPI_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ata-atapi-entry-test
CDK2_NATIVE_ATA_PROTOCOL_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ata-protocol-test
CDK2_NATIVE_ATA_ASYNC_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ata-async-test
CDK2_NATIVE_ATA_BUS_MODEL_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ata-bus-model-test
CDK2_NATIVE_ATA_BUS_IO_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ata-bus-io-test
CDK2_NATIVE_ATA_BUS_BLOCK_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ata-bus-block-test
CDK2_NATIVE_ATA_BUS_BINDING_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ata-bus-binding-test
CDK2_NATIVE_ATA_BUS_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ata-bus-entry-test
CDK2_NATIVE_ATA_BUS_DEPENDENCY_TEST := $(CDK2_DIR)/tests/ata_bus_dependency_test.sh
CDK2_NATIVE_ATA_BUS_MODULES := model io block binding disk_security entry runtime diagnostic
CDK2_NATIVE_ATA_BUS_DIAG_CORE ?= $(CDK2_NATIVE_BUILD_DIR)/ata-bus-diagnostic-core.o
CDK2_NATIVE_ATA_BUS_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/ata-bus-,$(addsuffix .o,$(CDK2_NATIVE_ATA_BUS_MODULES)))
CDK2_NATIVE_ATA_BUS_PE ?= $(CDK2_NATIVE_BUILD_DIR)/AtaBusDxe.efi
CDK2_NATIVE_ATA_BUS_QEMU_OBJ := $(CDK2_NATIVE_BUILD_DIR)/ata-bus-qemu.o
CDK2_NATIVE_ATA_BUS_QEMU_PE := $(CDK2_NATIVE_BUILD_DIR)/ata-bus-qemu.efi
CDK2_NATIVE_ATA_BACKEND_IDE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ata-backend-ide-test
CDK2_NATIVE_ATA_DEPENDENCY_TEST := $(CDK2_DIR)/tests/ata_atapi_dependency_test.sh
CDK2_NATIVE_ATA_REPRODUCIBLE_TEST := $(CDK2_DIR)/tests/ata_atapi_reproducible_test.sh
CDK2_NATIVE_ATA_MODULES := model binding ata_async ahci ahci_async ide ide_async \
	pci_adapter backend ata_protocol ext_scsi entry diagnostic
CDK2_NATIVE_ATA_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/ata-atapi-,$(addsuffix .o,$(CDK2_NATIVE_ATA_MODULES)))
CDK2_NATIVE_ATA_DIAG_CORE ?= $(CDK2_NATIVE_BUILD_DIR)/ata-atapi-diagnostic-core.o
CDK2_NATIVE_ATA_PE ?= $(CDK2_NATIVE_BUILD_DIR)/AtaAtapiPassThruDxe.efi
CDK2_NATIVE_ATA_QEMU_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/ata-atapi-qemu.o
CDK2_NATIVE_ATA_QEMU_PE ?= $(CDK2_NATIVE_BUILD_DIR)/ata-atapi-qemu.efi
CDK2_NATIVE_SATA_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-sata-entry-test
CDK2_NATIVE_SATA_DEPENDENCY_TEST := $(CDK2_DIR)/tests/sata_dependency_test.sh
CDK2_NATIVE_SATA_OBJS := $(CDK2_NATIVE_BUILD_DIR)/sata-model.o \
	$(CDK2_NATIVE_BUILD_DIR)/sata-entry.o \
	$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_BUILD_DIR)/sata-diagnostic.o)
CDK2_NATIVE_SATA_DIAG_CORE := $(CDK2_NATIVE_BUILD_DIR)/sata-diagnostic-core.o
CDK2_NATIVE_SATA_PE ?= $(CDK2_NATIVE_BUILD_DIR)/SataController.efi
CDK2_NATIVE_SATA_QEMU_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/sata-qemu.o
CDK2_NATIVE_SATA_QEMU_PE ?= $(CDK2_NATIVE_BUILD_DIR)/sata-qemu.efi
CDK2_NATIVE_PCI_HOST_BRIDGE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-pci-host-bridge-test
CDK2_NATIVE_PCI_HOST_BRIDGE_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-pci-host-bridge-entry-test
CDK2_NATIVE_PCI_HOST_BRIDGE_ROLLBACK_MUTATION_TEST ?= \
	$(CDK2_DIR)/tests/pci_host_bridge_rollback_mutation_test.sh
CDK2_NATIVE_PCI_ROOT_IO_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-pci-root-io-test
CDK2_NATIVE_PCI_BUS_MODEL_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-pci-bus-model-test
CDK2_NATIVE_PCI_BUS_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-pci-bus-entry-test
CDK2_NATIVE_PCI_ENUMERATE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-pci-enumerate-test
CDK2_NATIVE_PCI_IMMUTABLE_ENTRY_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-pci-immutable-entry-test
CDK2_NATIVE_PCI_CONFIG_TOGGLE_TEST ?= \
	$(CDK2_DIR)/tests/pci_bus_config_toggle_test.sh
CDK2_NATIVE_PCI_BUS_CONFIG_KEY := \
	immutable-$(if $(filter y,$(CONFIG_CDK2_LINEAR_BOOT)),y,n)
CDK2_NATIVE_PCI_BUS_CONFIG_KEY := $(CDK2_NATIVE_PCI_BUS_CONFIG_KEY)-debug-$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),y,n)
CDK2_NATIVE_PCI_BUS_CONFIG_KEY := $(CDK2_NATIVE_PCI_BUS_CONFIG_KEY)-diagnostic-$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),y,n)
CDK2_NATIVE_PCI_BUS_CONFIG_KEY := $(CDK2_NATIVE_PCI_BUS_CONFIG_KEY)-spi-$(if $(filter y,$(CONFIG_CDK2_SPI_CONSOLE)),y,n)
CDK2_NATIVE_PCI_BUS_OBJ_DIR := \
	$(CDK2_NATIVE_BUILD_DIR)/pci-bus-$(CDK2_NATIVE_PCI_BUS_CONFIG_KEY)
CDK2_NATIVE_PCI_BUS_CONFIG_HEADER := \
	$(CDK2_NATIVE_PCI_BUS_OBJ_DIR)/include/cdk2/config.h
CDK2_NATIVE_PCI_BUS_INCLUDES = \
	-I$(CDK2_NATIVE_PCI_BUS_OBJ_DIR)/include $(CDK2_NATIVE_INCLUDES)
ifeq ($(CONFIG_CDK2_LINEAR_BOOT),y)
CDK2_NATIVE_PCI_BUS_OBJS ?= \
	$(addprefix $(CDK2_NATIVE_PCI_BUS_OBJ_DIR)/pci-bus-,pci_io.o \
	pci_io_abi.o binding.o driver.o immutable_entry.o diagnostic.o)
CDK2_PCI_BUS_OBJECT_CFLAGS := -DCDK2_PCI_IMMUTABLE
else
CDK2_NATIVE_PCI_BUS_OBJS ?= \
	$(addprefix $(CDK2_NATIVE_PCI_BUS_OBJ_DIR)/pci-bus-,model.o allocator.o \
	host.o rom.o cardbus.o pci_io.o pci_io_abi.o binding.o adapter.o driver.o \
	entry.o diagnostic.o)
endif
CDK2_NATIVE_PCI_BUS_DIAG_CORE := $(CDK2_NATIVE_PCI_BUS_OBJ_DIR)/diagnostic-core.o
CDK2_NATIVE_PCI_BUS_MEM_OBJ := $(CDK2_NATIVE_PCI_BUS_OBJ_DIR)/mem.o
CDK2_NATIVE_PCI_BUS_PE ?= $(CDK2_NATIVE_PCI_BUS_OBJ_DIR)/PciBusDxe.efi
CDK2_NATIVE_PCI_BUS_MAP ?= $(CDK2_NATIVE_PCI_BUS_OBJ_DIR)/PciBusDxe.map
CDK2_NATIVE_NVME_MODEL_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-nvme-model-test
CDK2_NATIVE_NVME_CONTROLLER_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-nvme-controller-test
CDK2_NATIVE_NVME_BINDING_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-nvme-binding-test
CDK2_NATIVE_NVME_PASS_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-nvme-pass-test
CDK2_NATIVE_NVME_BLOCK_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-nvme-block-test
CDK2_NATIVE_NVME_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-nvme-entry-test
CDK2_NATIVE_NVME_PCI_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-nvme-pci-test
CDK2_NATIVE_NVME_DIAGNOSTIC_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-nvme-diagnostic-test
CDK2_NATIVE_NVME_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/nvme-,model.o \
	controller.o discovery.o io.o pci_adapter.o binding.o pass_thru.o block.o \
	diagnostic.o entry.o)
CDK2_NATIVE_NVME_PE ?= $(CDK2_NATIVE_BUILD_DIR)/NvmExpressDxe.efi
CDK2_NATIVE_SDMMC_PCI_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/sdmmc-pci-,\
	model.o pci.o async.o protocol.o binding.o entry.o) \
	$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),\
		$(CDK2_NATIVE_BUILD_DIR)/sdmmc-pci-diagnostic.o)
CDK2_NATIVE_SDMMC_PCI_PE ?= $(CDK2_NATIVE_BUILD_DIR)/SdMmcPciHcDxe.efi
CDK2_NATIVE_SDMMC_PCI_DIAGNOSTIC_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-sdmmc-pci-diagnostic-test
CDK2_NATIVE_SDMMC_PCI_DIAG_CORE ?= \
	$(CDK2_NATIVE_BUILD_DIR)/sdmmc-pci-diagnostic-core.o
CDK2_NATIVE_SD_DXE_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/sd-dxe-,\
	model.o backend.o block.o disk_info.o binding.o diagnostic.o entry.o) \
	$(CDK2_NATIVE_BUILD_DIR)/sdmmc-pci-model.o \
	$(CDK2_NATIVE_BUILD_DIR)/sdmmc-pci-protocol.o
CDK2_NATIVE_SD_DXE_PE ?= $(CDK2_NATIVE_BUILD_DIR)/SdDxe.efi
CDK2_NATIVE_SD_DXE_QEMU_OBJ := $(CDK2_NATIVE_BUILD_DIR)/sd-dxe-qemu.o
CDK2_NATIVE_SD_DXE_QEMU_PE := $(CDK2_NATIVE_BUILD_DIR)/sd-dxe-qemu.efi
CDK2_NATIVE_EMMC_DXE_MODEL_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-emmc-dxe-model-test
CDK2_NATIVE_EMMC_DXE_BACKEND_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-emmc-dxe-backend-test
CDK2_NATIVE_EMMC_DXE_BLOCK_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-emmc-dxe-block-test
CDK2_NATIVE_EMMC_DXE_DISK_INFO_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-emmc-dxe-disk-info-test
CDK2_NATIVE_EMMC_DXE_BINDING_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-emmc-dxe-binding-test
CDK2_NATIVE_EMMC_DXE_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/emmc-dxe-,\
	model.o backend.o block.o disk_info.o binding.o entry.o diagnostic.o)
CDK2_NATIVE_EMMC_DXE_DIAG_CORE ?= $(CDK2_NATIVE_BUILD_DIR)/emmc-dxe-diagnostic-core.o
CDK2_NATIVE_EMMC_DXE_PE ?= $(CDK2_NATIVE_BUILD_DIR)/EmmcDxe.efi
CDK2_NATIVE_EMMC_DXE_QEMU_OBJ := $(CDK2_NATIVE_BUILD_DIR)/emmc-dxe-qemu.o
CDK2_NATIVE_EMMC_DXE_QEMU_PE := $(CDK2_NATIVE_BUILD_DIR)/emmc-dxe-qemu.efi
CDK2_NATIVE_VARIABLE_RUNTIME_MODULES := model persistence service backend diagnostic entry \
	runtime
ifeq ($(CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME),y)
CDK2_NATIVE_VARIABLE_RUNTIME_MODULES := model service diagnostic entry
CDK2_NATIVE_VARIABLE_PROTECTED_SOURCES := \
	src/modules/authvar_transport/image_policy.c \
	src/modules/authvar_transport/owner.c \
	src/modules/authvar_transport/namespace.c \
	src/modules/authvar_transport/runtime.c \
	src/modules/authvar_transport/transport.c \
	src/modules/authvar_transport/native_x86.c \
	src/lib/payload_mm_authvar_service.c \
	src/lib/image_policy_snapshot.c \
	src/lib/image_authorization.c \
	src/lib/pe_authenticode.c \
	src/lib/pe_image_view.c \
	src/lib/signature_database.c \
	src/lib/mem.c \
	src/boot/coreboot.c \
	src/boot/coreboot_checksum.c \
	src/boot/coreboot_resource.c
CDK2_NATIVE_VARIABLE_PROTECTED_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/variable-protected-,\
	$(addsuffix .o,$(notdir $(basename $(CDK2_NATIVE_VARIABLE_PROTECTED_SOURCES)))))
CDK2_NATIVE_VARIABLE_POLICY_CRYPTO_OBJS = $(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_OBJS) \
	$(CDK2_NATIVE_TCG2_HASH_OBJS)
endif
CDK2_NATIVE_VARIABLE_RUNTIME_OBJS := $(addprefix \
	$(CDK2_NATIVE_BUILD_DIR)/variable-runtime-,\
	$(addsuffix .o,$(CDK2_NATIVE_VARIABLE_RUNTIME_MODULES)))
CDK2_NATIVE_VARIABLE_RUNTIME_PE ?= $(CDK2_NATIVE_BUILD_DIR)/VariableRuntimeDxe.efi
CDK2_NATIVE_VARIABLE_RUNTIME_QEMU_OBJ := \
	$(CDK2_NATIVE_BUILD_DIR)/variable-runtime-qemu.o
CDK2_NATIVE_VARIABLE_RUNTIME_QEMU_PE := \
	$(CDK2_NATIVE_BUILD_DIR)/variable-runtime-qemu.efi
CDK2_NATIVE_CPU_ARCH_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-cpu-arch-test

CDK2_NATIVE_MTRR_STAGE_AUDIT_MANIFEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/mtrr-stage-ownership-inputs


CDK2_NATIVE_CPU_PAGE_TABLE_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-cpu-page-table-test
CDK2_NATIVE_CPU_INTERRUPT_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-cpu-interrupt-test
CDK2_NATIVE_CPU_DRIVER_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-cpu-driver-test
CDK2_NATIVE_CPU_DIAGNOSTIC_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-cpu-diagnostic-test
CDK2_NATIVE_CPU_MODULES := model page_table interrupt diagnostic driver
CDK2_NATIVE_CPU_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/cpu-arch-,\
	$(addsuffix .o,$(CDK2_NATIVE_CPU_MODULES))) \
	$(CDK2_NATIVE_BUILD_DIR)/cpu-arch-interrupt-asm.o
CDK2_NATIVE_CPU_PE ?= $(CDK2_NATIVE_BUILD_DIR)/CpuDxe.efi
CDK2_NATIVE_PCI_HOST_BRIDGE_MODEL_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/pci-host-bridge-model.o
CDK2_NATIVE_PCI_HOST_BRIDGE_ENTRY_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/pci-host-bridge-entry.o
CDK2_NATIVE_PCI_HOST_BRIDGE_PROFILE_TEST ?= \
	$(CDK2_DIR)/tests/pci_host_bridge_profile_test.sh
CDK2_NATIVE_PCI_HOST_BRIDGE_DIAGNOSTIC_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/pci-host-bridge-diagnostic.o
CDK2_NATIVE_PCI_HOST_BRIDGE_DIAG_CORE ?= $(CDK2_NATIVE_BUILD_DIR)/pci-host-bridge-diagnostic-core.o
CDK2_NATIVE_PCI_ROOT_IO_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/pci-root-io.o
CDK2_NATIVE_PCI_HOST_BRIDGE_PE ?= $(CDK2_NATIVE_BUILD_DIR)/PciHostBridgeDxe.efi
CDK2_NATIVE_PCI_HOST_BRIDGE_QEMU_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/pci-host-bridge-qemu.o
CDK2_NATIVE_PCI_HOST_BRIDGE_QEMU_PE ?= $(CDK2_NATIVE_BUILD_DIR)/pci-host-bridge-qemu.efi
CDK2_NATIVE_METRONOME_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/metronome.o
CDK2_NATIVE_METRONOME_PE ?= $(CDK2_NATIVE_BUILD_DIR)/metronome.efi
CDK2_NATIVE_WATCHDOG_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-watchdog-test
CDK2_NATIVE_WATCHDOG_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/watchdog.o
CDK2_NATIVE_WATCHDOG_PE ?= $(CDK2_NATIVE_BUILD_DIR)/watchdog.efi
CDK2_NATIVE_STATUS_CODE_ROUTER_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-status-code-router-test
CDK2_NATIVE_STATUS_CODE_ROUTER_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/status-code-router.o
CDK2_NATIVE_STATUS_CODE_ROUTER_PE ?= $(CDK2_NATIVE_BUILD_DIR)/status-code-router.efi
CDK2_NATIVE_STATUS_CODE_HANDLER_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-status-code-handler-test
CDK2_NATIVE_STATUS_CODE_HANDLER_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/status-code-handler.o
CDK2_NATIVE_STATUS_CODE_HANDLER_PE ?= $(CDK2_NATIVE_BUILD_DIR)/status-code-handler.efi
CDK2_NATIVE_SERVICE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-services-test
CDK2_NATIVE_DXE_CORE_FV_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-dxe-core-fv-test
CDK2_NATIVE_COREBOOT_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-coreboot-test
CDK2_NATIVE_PE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-pe-test
CDK2_NATIVE_PE_LOW_STACK_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-pe-low-stack-test
CDK2_NATIVE_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-entry-test
CDK2_NATIVE_ELF_CHECK ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-elfcheck
CDK2_NATIVE_ELF_CHECK_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-elfcheck-test
CDK2_NATIVE_SECURITY_STUB_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-security-stub-test
CDK2_NATIVE_SECURITY_STUB_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/security-stub.o
CDK2_NATIVE_SECURITY_STUB_PE ?= $(CDK2_NATIVE_BUILD_DIR)/SecurityStubDxe.efi
CDK2_NATIVE_NULL_MEMORY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-null-memory-test-test
CDK2_NATIVE_NULL_MEMORY_TEST_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/null-memory-test.o
CDK2_NATIVE_NULL_MEMORY_TEST_PE ?= $(CDK2_NATIVE_BUILD_DIR)/null-memory-test.efi
CDK2_NATIVE_MONO_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-monotonic-counter-test
CDK2_NATIVE_MONO_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/monotonic-counter.o
CDK2_NATIVE_MONO_PE ?= $(CDK2_NATIVE_BUILD_DIR)/MonotonicCounterRuntimeDxe.efi
CDK2_NATIVE_RUNTIME_ARCH_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-runtime-arch-test
CDK2_NATIVE_RUNTIME_ARCH_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-runtime-arch-entry-test
CDK2_NATIVE_RUNTIME_ARCH_PE ?= $(CDK2_NATIVE_BUILD_DIR)/RuntimeDxe.efi
CDK2_NATIVE_UHCI_MODEL_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-uhci-model-test
CDK2_NATIVE_UHCI_PCI_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-uhci-pci-test
CDK2_NATIVE_UHCI_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-uhci-entry-test
CDK2_NATIVE_UHCI_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/uhci-,model.o transfer.o usb2_abi.o pci_adapter.o diagnostic.o entry.o)
CDK2_NATIVE_UHCI_PE ?= $(CDK2_NATIVE_BUILD_DIR)/UhciDxe.efi
CDK2_NATIVE_EHCI_MODEL_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ehci-model-test
CDK2_NATIVE_EHCI_PCI_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ehci-pci-test
CDK2_NATIVE_EHCI_TRANSFER_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ehci-transfer-test
CDK2_NATIVE_EHCI_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/ehci-,model.o transfer.o usb2_abi.o pci_adapter.o entry.o diagnostic.o)
CDK2_NATIVE_EHCI_DIAG_CORE ?= $(CDK2_NATIVE_BUILD_DIR)/ehci-diagnostic-core.o
CDK2_NATIVE_EHCI_PE ?= $(CDK2_NATIVE_BUILD_DIR)/EhciDxe.efi
CDK2_NATIVE_EC_BATTERY_PE ?= $(CDK2_NATIVE_BUILD_DIR)/EcAcpiBatteryStatusDxe.efi
CDK2_NATIVE_CPU_IO2_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-cpu-io2-test
CDK2_NATIVE_CPU_IO2_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/cpu-io2.o
CDK2_NATIVE_CPU_IO2_PE ?= $(CDK2_NATIVE_BUILD_DIR)/CpuIo2Dxe.efi
CDK2_NATIVE_ENGLISH_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-english-test
CDK2_NATIVE_ENGLISH_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/english.o
CDK2_NATIVE_ENGLISH_PE ?= $(CDK2_NATIVE_BUILD_DIR)/EnglishDxe.efi
CDK2_NATIVE_DISK_IO_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-disk-io-test
CDK2_NATIVE_FAT_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-fat-test
CDK2_NATIVE_FAT_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/fat-,fat.o binding.o protocol.o entry.o diagnostic.o)
CDK2_NATIVE_FAT_DIAG_CORE ?= $(CDK2_NATIVE_BUILD_DIR)/fat-diagnostic-core.o
CDK2_NATIVE_FAT_PE ?= $(CDK2_NATIVE_BUILD_DIR)/Fat.efi
CDK2_NATIVE_FAT_QEMU_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/fat-qemu.o
CDK2_NATIVE_FAT_QEMU_PE ?= $(CDK2_NATIVE_BUILD_DIR)/fat-qemu.efi
CDK2_NATIVE_DISK_IO_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/disk-io.o
CDK2_NATIVE_DISK_IO_PE ?= $(CDK2_NATIVE_BUILD_DIR)/DiskIoDxe.efi
CDK2_NATIVE_SERIAL_IO_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-serial-io-test
CDK2_NATIVE_SERIAL_IO_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/serial-io.o
CDK2_NATIVE_SERIAL_IO_PE ?= $(CDK2_NATIVE_BUILD_DIR)/SerialDxe.efi
CDK2_NATIVE_RESET_SYSTEM_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-reset-system-test
CDK2_NATIVE_RESET_SYSTEM_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/reset-system.o
CDK2_NATIVE_RESET_SYSTEM_DIAGNOSTIC_OBJ ?= \
	$(CDK2_NATIVE_BUILD_DIR)/reset-system-diagnostic.o
CDK2_NATIVE_RESET_SYSTEM_PE ?= $(CDK2_NATIVE_BUILD_DIR)/ResetSystemRuntimeDxe.efi
CDK2_NATIVE_PCAT_RTC_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-pcat-rtc-test
CDK2_NATIVE_PCAT_RTC_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/pcat-rtc.o
CDK2_NATIVE_PCAT_RTC_PE ?= $(CDK2_NATIVE_BUILD_DIR)/PcRtc.efi
CDK2_NATIVE_LOCAL_APIC_TIMER_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-local-apic-timer-test
CDK2_NATIVE_LOCAL_APIC_TIMER_DRIVER_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-local-apic-timer-driver-test
CDK2_NATIVE_LOCAL_APIC_TIMER_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/local-apic-timer.o
CDK2_NATIVE_LOCAL_APIC_TIMER_DRIVER_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/local-apic-timer-driver.o
CDK2_NATIVE_LOCAL_APIC_TIMER_PE ?= $(CDK2_NATIVE_BUILD_DIR)/LocalApicTimerDxe.efi
CDK2_NATIVE_SYSTEM_FMP_PE ?= $(CDK2_NATIVE_BUILD_DIR)/SystemFmpDxe.efi
CDK2_NATIVE_SYSTEM_FMP_PROVIDER_OBJ := \
	$(CDK2_NATIVE_BUILD_DIR)/system-fmp-provider.o
CDK2_NATIVE_SYSTEM_FMP_ENTRY_OBJ := \
	$(CDK2_NATIVE_BUILD_DIR)/system-fmp-entry.o
CDK2_NATIVE_SYSTEM_FMP_TRUST_OBJ := \
	$(CDK2_NATIVE_BUILD_DIR)/system-fmp-starlabs_trust.o
CDK2_NATIVE_SYSTEM_FMP_OBJS = \
	$(CDK2_NATIVE_SYSTEM_FMP_COMPOSITION_OBJ) \
	$(CDK2_NATIVE_SYSTEM_FMP_PROVIDER_OBJ) \
	$(CDK2_NATIVE_SYSTEM_FMP_ENTRY_OBJ) \
	$(CDK2_NATIVE_SYSTEM_FMP_TRUST_OBJ)
CDK2_NATIVE_SYSTEM_FMP_PRODUCTION_CLOSURE_TEST := \
	$(CDK2_DIR)/tests/system_fmp_production_closure_test.sh
CDK2_NATIVE_SYSTEM_FMP_PROVIDER_TEST_O0 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-provider-test-o0
CDK2_NATIVE_SYSTEM_FMP_PROVIDER_TEST_O2 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-provider-test-o2
CDK2_NATIVE_SYSTEM_FMP_PROVIDER_TEST_SANITIZED := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-provider-test-sanitized
CDK2_NATIVE_SYSTEM_FMP_ENTRY_TEST_O0 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-entry-test-o0
CDK2_NATIVE_SYSTEM_FMP_ENTRY_TEST_O2 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-entry-test-o2
CDK2_NATIVE_SYSTEM_FMP_ENTRY_TEST_SANITIZED := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-entry-test-sanitized

# Unselected typed policy and oracle slices: explicit build/test targets only.
# coordinator.c/state.c are historical differential oracles; new protected
# composition is system_fmp_session and has no local durable-state dependency.
# No provider,
# stage object, Kconfig selection, protocol installation or firmware packaging.
CDK2_NATIVE_SYSTEM_FMP_POLICY_SOURCES := $(addprefix $(CDK2_DIR)/src/modules/system_fmp/,\
	policy.c test_key.c retained_policy.c starlabs_trust.c)
CDK2_NATIVE_SYSTEM_FMP_POLICY_HEADERS := $(CDK2_DIR)/include/cdk2/system_fmp_policy.h \
	$(CDK2_DIR)/include/cdk2/system_fmp_test_key.h $(CDK2_DIR)/include/uefi.h
CDK2_NATIVE_SYSTEM_FMP_POLICY_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/system-fmp-,\
	policy.o test_key.o retained_policy.o starlabs_trust.o)
CDK2_NATIVE_SYSTEM_FMP_POLICY_TEST := $(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-policy-test
CDK2_NATIVE_SYSTEM_FMP_POLICY_EVIDENCE_SELFTEST := \
	$(CDK2_DIR)/tests/system_fmp_policy_evidence_selftest.sh
CDK2_NATIVE_SYSTEM_FMP_AUTH_OBJ := \
	$(CDK2_NATIVE_BUILD_DIR)/system-fmp-auth.o
CDK2_NATIVE_SYSTEM_FMP_AUTH_TEST_O0 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-auth-test-o0
CDK2_NATIVE_SYSTEM_FMP_AUTH_TEST_O2 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-auth-test-o2
CDK2_NATIVE_SYSTEM_FMP_AUTH_TEST_SANITIZED := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-auth-test-sanitized
CDK2_NATIVE_SYSTEM_FMP_AUTH_TESTS := $(CDK2_NATIVE_SYSTEM_FMP_AUTH_TEST_O0) \
	$(CDK2_NATIVE_SYSTEM_FMP_AUTH_TEST_O2) \
	$(CDK2_NATIVE_SYSTEM_FMP_AUTH_TEST_SANITIZED)
CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_OBJ := \
	$(CDK2_NATIVE_BUILD_DIR)/system-fmp-payload.o
CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_TEST_O0 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-payload-test-o0
CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_TEST_O2 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-payload-test-o2
CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_TEST_SANITIZED := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-payload-test-sanitized
CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_TESTS := \
	$(CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_TEST_O0) \
	$(CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_TEST_O2) \
	$(CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_TEST_SANITIZED)
CDK2_NATIVE_SYSTEM_FMP_STATE_OBJ := \
	$(CDK2_NATIVE_BUILD_DIR)/system-fmp-state.o
CDK2_NATIVE_SYSTEM_FMP_STATE_TEST_O0 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-state-test-o0
CDK2_NATIVE_SYSTEM_FMP_STATE_TEST_O2 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-state-test-o2
CDK2_NATIVE_SYSTEM_FMP_STATE_TEST_SANITIZED := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-state-test-sanitized
CDK2_NATIVE_SYSTEM_FMP_STATE_TESTS := $(CDK2_NATIVE_SYSTEM_FMP_STATE_TEST_O0) \
	$(CDK2_NATIVE_SYSTEM_FMP_STATE_TEST_O2) \
	$(CDK2_NATIVE_SYSTEM_FMP_STATE_TEST_SANITIZED)
CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_OBJ := \
	$(CDK2_NATIVE_BUILD_DIR)/system-fmp-board-binding.o
CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_TEST_O0 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-board-binding-test-o0
CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_TEST_O2 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-board-binding-test-o2
CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_TEST_SANITIZED := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-board-binding-test-sanitized
CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_TESTS := \
	$(CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_TEST_O0) \
	$(CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_TEST_O2) \
	$(CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_TEST_SANITIZED)
CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_OBJ := \
	$(CDK2_NATIVE_BUILD_DIR)/system-fmp-coordinator.o
CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_TEST_O0 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-coordinator-test-o0
CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_TEST_O2 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-coordinator-test-o2
CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_TEST_SANITIZED := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-coordinator-test-sanitized
CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_TESTS := \
	$(CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_TEST_O0) \
	$(CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_TEST_O2) \
	$(CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_TEST_SANITIZED)
CDK2_NATIVE_SYSTEM_FMP_SESSION_OBJ := \
	$(CDK2_NATIVE_BUILD_DIR)/system-fmp-session.o
CDK2_NATIVE_SYSTEM_FMP_HANDOFF_OBJ := \
	$(CDK2_NATIVE_BUILD_DIR)/system-fmp-handoff.o
CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_SUPPORT := \
	$(CDK2_DIR)/src/modules/system_fmp/auth.c \
	$(CDK2_DIR)/src/modules/system_fmp/handoff.c \
	$(CDK2_DIR)/src/boot/coreboot.c \
	$(CDK2_DIR)/src/boot/coreboot_checksum.c \
	$(CDK2_DIR)/src/boot/coreboot_resource.c
CDK2_NATIVE_SYSTEM_FMP_TRANSPORT_DIAGNOSTIC_TEST := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-transport-diagnostic-test
CDK2_NATIVE_SYSTEM_FMP_TRANSPORT_OBJ := \
	$(CDK2_NATIVE_BUILD_DIR)/system-fmp-transport.o
CDK2_NATIVE_SYSTEM_FMP_COMPOSITION_OBJ := \
	$(CDK2_NATIVE_BUILD_DIR)/system-fmp-composition.o
CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_O0 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-session-test-o0
CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_O2 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-session-test-o2
CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_SANITIZED := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-session-test-sanitized
CDK2_NATIVE_SYSTEM_FMP_SESSION_TESTS := \
	$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_O0) \
	$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_O2) \
	$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_SANITIZED)
CDK2_NATIVE_SYSTEM_FMP_HEADERS_TEST := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-headers-test

.PHONY: native-system-fmp-policy native-system-fmp-policy-test
native-system-fmp-policy: $(CDK2_NATIVE_SYSTEM_FMP_POLICY_OBJS)

$(CDK2_NATIVE_SYSTEM_FMP_POLICY_OBJS): $(CDK2_NATIVE_BUILD_DIR)/system-fmp-%.o: \
		$(CDK2_DIR)/src/modules/system_fmp/%.c $(CDK2_NATIVE_SYSTEM_FMP_POLICY_HEADERS) \
		| $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_POLICY_TEST): $(CDK2_DIR)/tests/system_fmp_policy_test.c \
		$(CDK2_NATIVE_SYSTEM_FMP_POLICY_SOURCES) \
		$(CDK2_NATIVE_SYSTEM_FMP_POLICY_HEADERS) \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/sha256.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		"$<" $(CDK2_NATIVE_SYSTEM_FMP_POLICY_SOURCES) \
		"$(CDK2_DIR)/src/modules/qemu_test_fmp/sha256.c" -o "$@"

native-system-fmp-policy-test: native-system-fmp-policy \
		system-fmp-test-key-prototype-test $(CDK2_NATIVE_SYSTEM_FMP_POLICY_TEST)
	@test -z "$(filter $(CDK2_NATIVE_SYSTEM_FMP_POLICY_OBJS),\
		$(CDK2_NATIVE_STAGE_OBJS) $(CDK2_NATIVE_COREBOOT_OBJS))"
	@sh "$(CDK2_DIR)/tests/system_fmp_policy_test.sh" "$(CDK2_NATIVE_SYSTEM_FMP_POLICY_TEST)"
	@sh "$(CDK2_NATIVE_SYSTEM_FMP_POLICY_EVIDENCE_SELFTEST)" \
		"$(CDK2_DIR)/tests/system_fmp_policy_test.sh" \
		"$(CDK2_NATIVE_SYSTEM_FMP_POLICY_TEST)" \
		"$(CDK2_DIR)/migration/system-fmp-fixed-policy.tsv"

.PHONY: native-system-fmp-auth native-system-fmp-auth-test
native-system-fmp-auth: $(CDK2_NATIVE_SYSTEM_FMP_AUTH_OBJ)

$(CDK2_NATIVE_SYSTEM_FMP_AUTH_OBJ): \
		$(CDK2_DIR)/src/modules/system_fmp/auth.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_auth.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_AUTH_TEST_O0): \
		$(CDK2_DIR)/tests/system_fmp_auth_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/auth.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_auth.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O0 \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/auth.c" -lcrypto -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_AUTH_TEST_O2): \
		$(CDK2_DIR)/tests/system_fmp_auth_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/auth.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_auth.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O2 \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/auth.c" -lcrypto -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_AUTH_TEST_SANITIZED): \
		$(CDK2_DIR)/tests/system_fmp_auth_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/auth.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_auth.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O1 \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/auth.c" -lcrypto -o "$@"

native-system-fmp-auth-test: native-system-fmp-auth \
		$(CDK2_NATIVE_SYSTEM_FMP_AUTH_TESTS)
	@test -z "$(filter $(CDK2_NATIVE_SYSTEM_FMP_AUTH_OBJ),\
		$(CDK2_NATIVE_STAGE_OBJS) $(CDK2_NATIVE_COREBOOT_OBJS))"
	@"$(CDK2_DIR)/tests/system_fmp_auth_test.sh" \
		"$(CDK2_NATIVE_SYSTEM_FMP_AUTH_TEST_O0)"
	@"$(CDK2_DIR)/tests/system_fmp_auth_test.sh" \
		"$(CDK2_NATIVE_SYSTEM_FMP_AUTH_TEST_O2)"
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$(CDK2_DIR)/tests/system_fmp_auth_test.sh" \
		"$(CDK2_NATIVE_SYSTEM_FMP_AUTH_TEST_SANITIZED)"

native-check: native-system-fmp-auth-test

CDK2_BEARSSL_DIR := $(CDK2_DIR)/3rdparty/bearssl
CDK2_BEARSSL_X509_SOURCE := $(CDK2_NATIVE_BUILD_DIR)/bearssl-x509-minimal.c
CDK2_BEARSSL_SOURCES := \
	codec/ccopy.c codec/dec32be.c codec/dec64be.c codec/enc32be.c codec/enc64be.c \
	int/i31_add.c int/i31_bitlen.c int/i31_decmod.c int/i31_decode.c \
	int/i31_encode.c int/i31_fmont.c int/i31_modpow2.c int/i31_montmul.c \
	int/i31_muladd.c int/i31_ninv31.c int/i31_sub.c int/i31_tmont.c \
	int/i32_div32.c hash/multihash.c hash/sha2small.c hash/sha2big.c \
	rsa/rsa_i31_pkcs1_vrfy.c rsa/rsa_i31_pub.c rsa/rsa_pkcs1_sig_unpad.c \
	x509/x509_decoder.c x509/x509_minimal.c
CDK2_BEARSSL_UNPATCHED_SOURCES := $(filter-out x509/x509_minimal.c,\
	$(CDK2_BEARSSL_SOURCES))
CDK2_BEARSSL_SOURCE_PATHS := $(addprefix $(CDK2_BEARSSL_DIR)/src/,\
	$(CDK2_BEARSSL_SOURCES))
CDK2_BEARSSL_SOURCE_PATHS := $(filter-out $(CDK2_BEARSSL_DIR)/src/x509/x509_minimal.c,\
	$(CDK2_BEARSSL_SOURCE_PATHS)) $(CDK2_BEARSSL_X509_SOURCE)

$(CDK2_BEARSSL_X509_SOURCE): $(CDK2_BEARSSL_DIR)/src/x509/x509_minimal.c \
		$(CDK2_BEARSSL_DIR)/src/x509/x509_minimal.t0 \
		$(CDK2_DIR)/3rdparty/bearssl-patches/critical-timestamp-eku.c.patch \
		$(CDK2_DIR)/3rdparty/bearssl-patches/critical-timestamp-eku.t0.patch \
		$(CDK2_DIR)/util/bearssl/apply-x509-eku-patch.sh | $(CDK2_NATIVE_BUILD_DIR)
	@sh "$(CDK2_DIR)/util/bearssl/apply-x509-eku-patch.sh" "$(CDK2_BEARSSL_DIR)" "$@"
CDK2_NATIVE_SYSTEM_FMP_CRYPTO_OBJ := \
	$(CDK2_NATIVE_BUILD_DIR)/cms.o
CDK2_NATIVE_CMS_SPC_OBJ := $(CDK2_NATIVE_BUILD_DIR)/cms-spc.o
CDK2_NATIVE_SYSTEM_FMP_CRYPTO_BEARSSL_OBJS := $(foreach source,$(CDK2_BEARSSL_SOURCES),\
	$(CDK2_NATIVE_BUILD_DIR)/bearssl-$(subst /,-,$(source:.c=.o)))
CDK2_NATIVE_SYSTEM_FMP_CRYPTO_OBJS := $(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_OBJ) \
	$(CDK2_NATIVE_CMS_SPC_OBJ) \
	$(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_BEARSSL_OBJS)
CDK2_NATIVE_SYSTEM_FMP_CRYPTO_CLOSURE := \
	$(CDK2_NATIVE_BUILD_DIR)/system-fmp-crypto-closure.o
CDK2_NATIVE_SYSTEM_FMP_CRYPTO_TEST_O2 := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-crypto-test-o2
CDK2_NATIVE_SYSTEM_FMP_CRYPTO_TEST_SANITIZED := \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-system-fmp-crypto-test-sanitized
CDK2_SYSTEM_FMP_CRYPTO_FLAGS := -DBR_USE_UNIX_TIME=0 \
	-I$(CDK2_BEARSSL_DIR)/inc -I$(CDK2_BEARSSL_DIR)/src

.PHONY: native-system-fmp-crypto native-system-fmp-crypto-test
native-system-fmp-crypto: $(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_OBJS)

$(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_CLOSURE): \
		$(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o
	@$(CDK2_NATIVE_LD) -r -o "$@" $^

$(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_OBJ): \
		$(CDK2_DIR)/src/lib/cms.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_crypto.h \
		$(CDK2_DIR)/include/cdk2/authenticode_crypto.h \
		$(CDK2_DIR)/include/cdk2/authenticode_spc.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		$(CDK2_SYSTEM_FMP_CRYPTO_FLAGS) -MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_CMS_SPC_OBJ): $(CDK2_DIR)/src/lib/authenticode_spc.c \
		$(CDK2_DIR)/include/cdk2/authenticode_spc.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

define cdk2_bearssl_object_rule
$(CDK2_NATIVE_BUILD_DIR)/bearssl-$(subst /,-,$(1:.c=.o)): \
		$(CDK2_BEARSSL_DIR)/src/$(1) | $(CDK2_NATIVE_BUILD_DIR)
	@$$(CDK2_NATIVE_CC) $$(CDK2_NATIVE_CFLAGS) \
		$$(CDK2_SYSTEM_FMP_CRYPTO_FLAGS) -MMD -MP -c "$$<" -o "$$@"
endef
$(foreach source,$(CDK2_BEARSSL_UNPATCHED_SOURCES),\
	$(eval $(call cdk2_bearssl_object_rule,$(source))))

$(CDK2_NATIVE_BUILD_DIR)/bearssl-x509-x509_minimal.o: \
		$(CDK2_BEARSSL_X509_SOURCE) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) \
		$(CDK2_SYSTEM_FMP_CRYPTO_FLAGS) -MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_TEST_O2): \
		$(CDK2_DIR)/tests/system_fmp_crypto_test.c \
		$(CDK2_DIR)/src/lib/cms.c $(CDK2_DIR)/src/lib/authenticode_spc.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_crypto.h \
		$(CDK2_DIR)/include/cdk2/authenticode_crypto.h \
		$(CDK2_DIR)/include/cdk2/authenticode_spc.h \
		$(CDK2_BEARSSL_SOURCE_PATHS) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O2 \
		$(CDK2_NATIVE_INCLUDES) $(CDK2_SYSTEM_FMP_CRYPTO_FLAGS) \
		"$<" "$(CDK2_DIR)/src/lib/cms.c" \
		"$(CDK2_DIR)/src/lib/authenticode_spc.c" \
		$(CDK2_BEARSSL_SOURCE_PATHS) -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_TEST_SANITIZED): \
		$(CDK2_DIR)/tests/system_fmp_crypto_test.c \
		$(CDK2_DIR)/src/lib/cms.c $(CDK2_DIR)/src/lib/authenticode_spc.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_crypto.h \
		$(CDK2_DIR)/include/cdk2/authenticode_crypto.h \
		$(CDK2_DIR)/include/cdk2/authenticode_spc.h \
		$(CDK2_BEARSSL_SOURCE_PATHS) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O1 \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		$(CDK2_NATIVE_INCLUDES) $(CDK2_SYSTEM_FMP_CRYPTO_FLAGS) \
		"$<" "$(CDK2_DIR)/src/lib/cms.c" \
		"$(CDK2_DIR)/src/lib/authenticode_spc.c" \
		$(CDK2_BEARSSL_SOURCE_PATHS) -o "$@"

native-system-fmp-crypto-test: native-system-fmp-crypto \
		$(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_CLOSURE) \
		$(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_TEST_O2) \
		$(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_TEST_SANITIZED)
	@test -z "$(filter $(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_OBJS),\
		$(CDK2_NATIVE_STAGE_OBJS) $(CDK2_NATIVE_COREBOOT_OBJS))"
	@"$(CDK2_DIR)/tests/system_fmp_crypto_closure_test.sh" \
		"$(CDK2_NATIVE_NM)" "$(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_CLOSURE)"
	@"$(CDK2_DIR)/tests/system_fmp_crypto_test.sh" \
		"$(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_TEST_O2)"
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$(CDK2_DIR)/tests/system_fmp_crypto_test.sh" \
		"$(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_TEST_SANITIZED)"

native-check: native-system-fmp-crypto-test

CDK2_NATIVE_AUTHENTICODE_CRYPTO_TEST_O0 := \
	$(CDK2_NATIVE_BUILD_DIR)/authenticode-crypto-test-o0
CDK2_NATIVE_AUTHENTICODE_CRYPTO_TEST_O2 := \
	$(CDK2_NATIVE_BUILD_DIR)/authenticode-crypto-test-o2
CDK2_AUTHENTICODE_CRYPTO_TEST_SOURCES := \
	$(CDK2_DIR)/tests/authenticode_crypto_test.c \
	$(CDK2_DIR)/src/lib/cms.c $(CDK2_DIR)/src/lib/authenticode_spc.c \
	$(CDK2_DIR)/src/lib/pe_authenticode.c $(CDK2_DIR)/src/lib/pe_image_view.c \
	$(CDK2_DIR)/src/lib/tcg_hash/software_hash.c \
	$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha1.c \
	$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha256.c \
	$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha512.c \
	$(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/lib/crypto/sm3.c

define cdk2_authenticode_crypto_test_rule
$(CDK2_NATIVE_BUILD_DIR)/authenticode-crypto-test-o$(1): \
		$(CDK2_AUTHENTICODE_CRYPTO_TEST_SOURCES) $(CDK2_BEARSSL_SOURCE_PATHS) \
		$(CDK2_DIR)/include/cdk2/authenticode_crypto.h \
		$(CDK2_DIR)/include/cdk2/authenticode_spc.h \
		$(CDK2_DIR)/include/cdk2/pe_authenticode.h \
		$(CDK2_DIR)/include/cdk2/system_fmp_crypto.h \
		$(CDK2_DIR)/include/cdk2/system_fmp_auth.h | $(CDK2_NATIVE_BUILD_DIR)
	@$$(CDK2_NATIVE_HOST_CC) $$(CDK2_NATIVE_HOST_CFLAGS) -O$(1) \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		$$(CDK2_NATIVE_INCLUDES) $$(CDK2_SYSTEM_FMP_CRYPTO_FLAGS) \
		-I$$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot \
		-I$$(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/include \
		$$(CDK2_AUTHENTICODE_CRYPTO_TEST_SOURCES) $$(CDK2_BEARSSL_SOURCE_PATHS) -o "$$@"
endef
$(foreach optimization,0 2,\
	$(eval $(call cdk2_authenticode_crypto_test_rule,$(optimization))))

.PHONY: native-authenticode-crypto-test
native-authenticode-crypto-test: $(CDK2_NATIVE_AUTHENTICODE_CRYPTO_TEST_O0) \
		$(CDK2_NATIVE_AUTHENTICODE_CRYPTO_TEST_O2)
	@sh "$(CDK2_DIR)/tests/authenticode_spc_test.sh" \
		"$(CDK2_NATIVE_AUTHENTICODE_CRYPTO_TEST_O0)" \
		"$(CDK2_NATIVE_AUTHENTICODE_CRYPTO_TEST_O2)"

native-check: native-authenticode-crypto-test

CDK2_AUTHENTICODE_TIMESTAMP_TEST_SOURCES := \
	$(filter-out $(CDK2_DIR)/tests/authenticode_crypto_test.c,\
		$(CDK2_AUTHENTICODE_CRYPTO_TEST_SOURCES)) \
	$(CDK2_DIR)/tests/authenticode_timestamp_test.c \
	$(CDK2_DIR)/src/lib/image_authorization.c \
	$(CDK2_DIR)/src/lib/signature_database.c

define cdk2_authenticode_timestamp_test_rule
$(CDK2_NATIVE_BUILD_DIR)/authenticode-timestamp-test-o$(1): \
		$(CDK2_AUTHENTICODE_TIMESTAMP_TEST_SOURCES) $(CDK2_BEARSSL_SOURCE_PATHS) \
		$(CDK2_DIR)/include/cdk2/authenticode_crypto.h \
		$(CDK2_DIR)/include/cdk2/image_authorization.h \
		$(CDK2_DIR)/include/cdk2/signature_database.h \
		$(CDK2_DIR)/include/cdk2/authenticode_spc.h \
		$(CDK2_DIR)/include/cdk2/pe_authenticode.h \
		$(CDK2_DIR)/include/cdk2/system_fmp_crypto.h \
		$(CDK2_DIR)/include/cdk2/system_fmp_auth.h | $(CDK2_NATIVE_BUILD_DIR)
	@$$(CDK2_NATIVE_HOST_CC) $$(CDK2_NATIVE_HOST_CFLAGS) -O$(1) \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		$$(CDK2_NATIVE_INCLUDES) $$(CDK2_SYSTEM_FMP_CRYPTO_FLAGS) \
		-I$$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot \
		-I$$(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/include \
		$$(CDK2_AUTHENTICODE_TIMESTAMP_TEST_SOURCES) $$(CDK2_BEARSSL_SOURCE_PATHS) -o "$$@"
endef
$(foreach optimization,0 2,\
	$(eval $(call cdk2_authenticode_timestamp_test_rule,$(optimization))))

.PHONY: native-authenticode-timestamp-test
native-authenticode-timestamp-test: $(CDK2_NATIVE_AUTHENTICODE_CRYPTO_TEST_O0) \
		$(CDK2_NATIVE_BUILD_DIR)/authenticode-timestamp-test-o0 \
		$(CDK2_NATIVE_BUILD_DIR)/authenticode-timestamp-test-o2
	@CDK2_AUTHENTICODE_TIMESTAMP_TEST="$(CDK2_NATIVE_BUILD_DIR)/authenticode-timestamp-test-o0" \
		CDK2_AUTHENTICODE_TIMESTAMP_TEST_O2="$(CDK2_NATIVE_BUILD_DIR)/authenticode-timestamp-test-o2" \
		sh "$(CDK2_DIR)/tests/authenticode_spc_test.sh" \
		"$(CDK2_NATIVE_AUTHENTICODE_CRYPTO_TEST_O0)"

native-check: native-authenticode-timestamp-test

CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_TEST_O0 := \
	$(CDK2_NATIVE_BUILD_DIR)/protected-image-policy-join-test-o0
CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_TEST_O2 := \
	$(CDK2_NATIVE_BUILD_DIR)/protected-image-policy-join-test-o2
CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_MUTANT_O0 := \
	$(CDK2_NATIVE_BUILD_DIR)/protected-image-policy-join-mutant-o0
CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_MUTANT_O2 := \
	$(CDK2_NATIVE_BUILD_DIR)/protected-image-policy-join-mutant-o2
CDK2_NATIVE_PROTECTED_IMAGE_POLICY_SECURITY_MUTANT := \
	$(CDK2_NATIVE_BUILD_DIR)/protected-image-policy-security-mutant.c
CDK2_NATIVE_PROTECTED_IMAGE_POLICY_CANONICAL_MUTANT := \
	$(CDK2_NATIVE_BUILD_DIR)/protected-image-policy-canonical-mutant.c
CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_CANONICAL_MUTANT_O0 := \
	$(CDK2_NATIVE_BUILD_DIR)/protected-image-policy-join-canonical-mutant-o0
CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_CANONICAL_MUTANT_O2 := \
	$(CDK2_NATIVE_BUILD_DIR)/protected-image-policy-join-canonical-mutant-o2
CDK2_PROTECTED_IMAGE_POLICY_JOIN_SOURCES := \
	$(filter-out $(CDK2_DIR)/tests/authenticode_timestamp_test.c,\
		$(CDK2_AUTHENTICODE_TIMESTAMP_TEST_SOURCES)) \
	$(CDK2_DIR)/tests/protected_image_policy_consumer_join_test.c \
	$(CDK2_DIR)/src/modules/authvar_transport/owner.c \
	$(CDK2_DIR)/src/modules/authvar_transport/runtime.c \
	$(CDK2_DIR)/src/modules/authvar_transport/namespace.c \
	$(CDK2_DIR)/src/modules/authvar_transport/image_policy.c \
	$(CDK2_DIR)/src/modules/authvar_transport/transport.c \
	$(CDK2_DIR)/src/lib/payload_mm_authvar_service.c \
	$(CDK2_DIR)/src/lib/image_policy_snapshot.c \
	$(CDK2_DIR)/src/boot/coreboot.c \
	$(CDK2_DIR)/src/lib/diagnostic.c

CDK2_PROTECTED_IMAGE_POLICY_JOIN_FLAGS = \
	$(CDK2_NATIVE_HOST_SANITIZERS) -fno-sanitize-recover=all \
	-ffunction-sections -fdata-sections -Wl,--gc-sections \
	$(CDK2_NATIVE_INCLUDES) $(CDK2_SYSTEM_FMP_CRYPTO_FLAGS) \
	-I$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot \
	-I$(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/include

.PHONY: native-protected-variable-entry-test
native-protected-variable-entry-test: $(CDK2_BEARSSL_X509_SOURCE)
	@test -n "$(COREBOOT_TREE)" || { echo "COREBOOT_TREE is required" >&2; exit 2; }
	@HOSTCC="$(CDK2_NATIVE_HOST_CC)" COREBOOT_TREE="$(abspath $(COREBOOT_TREE))" \
		MBEDTLS_SOURCE="$(MBEDTLS_SOURCE)" \
		PROTECTED_ENTRY_CRYPTO_SOURCES="$(filter-out $(CDK2_DIR)/tests/authenticode_timestamp_test.c,$(CDK2_AUTHENTICODE_TIMESTAMP_TEST_SOURCES)) $(CDK2_BEARSSL_SOURCE_PATHS)" \
		PROTECTED_ENTRY_CRYPTO_FLAGS="$(CDK2_SYSTEM_FMP_CRYPTO_FLAGS) -I$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot -I$(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/include" \
		sh "$(CDK2_DIR)/tests/protected_variable_entry_test.sh"

.PHONY: native-protected-variable-component-test
native-protected-variable-component-test: $(CDK2_CONFIG_HEADER) $(CDK2_BEARSSL_X509_SOURCE)
	@HOSTCC="$(CDK2_NATIVE_CC)" COREBOOT_ROM="$(COREBOOT_ROM)" CBFSTOOL="$(CBFSTOOL)" \
		PROTECTED_ENTRY_CRYPTO_SOURCES="$(filter-out $(CDK2_DIR)/tests/authenticode_timestamp_test.c,$(CDK2_AUTHENTICODE_TIMESTAMP_TEST_SOURCES)) $(CDK2_BEARSSL_SOURCE_PATHS)" \
		PROTECTED_ENTRY_CRYPTO_FLAGS="$(CDK2_SYSTEM_FMP_CRYPTO_FLAGS) -I$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot -I$(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/include" \
		NATIVE_PROBE_CFLAGS="$(CDK2_NATIVE_CFLAGS)" \
		sh "$(CDK2_DIR)/tests/protected_variable_native_probe_test.sh" \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_BUILD_DIR)/protected-variable-component"

.PHONY: native-protected-variable-service-component-test native-protected-variable-service-compile-test \
	native-protected-variable-public-service-component-test native-protected-variable-public-service-compile-test
native-protected-variable-service-compile-test: NATIVE_SERVICE_BUILD_ONLY=1
native-protected-variable-public-service-compile-test: NATIVE_SERVICE_BUILD_ONLY=1
native-protected-variable-public-service-component-test native-protected-variable-public-service-compile-test: NATIVE_SERVICE_PUBLIC_RAM=1
native-protected-variable-service-component-test native-protected-variable-service-compile-test \
		native-protected-variable-public-service-component-test native-protected-variable-public-service-compile-test: \
		$(CDK2_CONFIG_HEADER) $(CDK2_BEARSSL_X509_SOURCE) $(CDK2_NATIVE_NULL_MEMORY_TEST_PE)
	@HOSTCC="$(CDK2_NATIVE_CC)" COREBOOT_ROM="$(COREBOOT_ROM)" CBFSTOOL="$(CBFSTOOL)" \
		NATIVE_SERVICE_BUILD_ONLY="$(NATIVE_SERVICE_BUILD_ONLY)" \
		NATIVE_SERVICE_PUBLIC_RAM="$(NATIVE_SERVICE_PUBLIC_RAM)" \
		NATIVE_SERVICE_LIFECYCLE_SOURCES="$(filter-out $(CDK2_DIR)/src/boot/coreboot_checksum.c,$(CDK2_NATIVE_DXE_LIFECYCLE_TEST_SOURCES))" \
		NATIVE_UNSIGNED_PE="$(CDK2_NATIVE_NULL_MEMORY_TEST_PE)" \
		PROTECTED_ENTRY_CRYPTO_SOURCES="$(filter-out $(CDK2_DIR)/tests/authenticode_timestamp_test.c,$(CDK2_AUTHENTICODE_TIMESTAMP_TEST_SOURCES)) $(CDK2_BEARSSL_SOURCE_PATHS)" \
		PROTECTED_ENTRY_CRYPTO_FLAGS="$(CDK2_SYSTEM_FMP_CRYPTO_FLAGS) -I$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot -I$(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/include" \
		NATIVE_PROBE_CFLAGS="$(CDK2_NATIVE_CFLAGS)" \
		sh "$(CDK2_DIR)/tests/protected_variable_native_service_probe_test.sh" \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_BUILD_DIR)/protected-variable-service-component"

.PHONY: native-capsule-mm-reset-component-test
.PHONY: native-capsule-provider-info-close-component-test
native-capsule-provider-info-close-component-test: MM_CAPSULE_PROVIDER_CHECK=1
native-capsule-provider-cold-state-component-compile-test: MM_CAPSULE_PROVIDER_CHECK=1
native-capsule-provider-cold-state-component-compile-test: MM_CAPSULE_COLD_CHECK=1
native-capsule-provider-cold-state-component-compile-test: MM_CAPSULE_BUILD_ONLY=1
.PHONY: native-capsule-provider-authenticated-apply-component-test native-capsule-provider-authenticated-apply-component-compile-test
native-capsule-provider-authenticated-apply-component-test native-capsule-provider-authenticated-apply-component-compile-test: MM_CAPSULE_PROVIDER_CHECK=1
native-capsule-provider-authenticated-apply-component-test native-capsule-provider-authenticated-apply-component-compile-test: MM_CAPSULE_APPLY=1
native-capsule-provider-authenticated-apply-component-compile-test: MM_CAPSULE_BUILD_ONLY=1
.PHONY: native-capsule-provider-authenticated-check-denial-component-test
native-capsule-provider-authenticated-check-denial-component-test: MM_CAPSULE_PROVIDER_CHECK=1
native-capsule-provider-authenticated-check-denial-component-test: MM_CAPSULE_APPLY=1
native-capsule-provider-authenticated-check-denial-component-test: MM_CAPSULE_DENIAL=1
.PHONY: native-capsule-provider-cold-state-component-compile-test
.PHONY: native-capsule-mm-update-component-test
native-capsule-mm-update-component-test: MM_UPDATE_CAPSULE_ROUNDTRIP=1
native-capsule-mm-reset-component-test native-capsule-mm-update-component-test native-capsule-provider-info-close-component-test native-capsule-provider-cold-state-component-compile-test native-capsule-provider-authenticated-apply-component-test native-capsule-provider-authenticated-apply-component-compile-test native-capsule-provider-authenticated-check-denial-component-test: $(CDK2_CONFIG_HEADER) $(CDK2_BEARSSL_X509_SOURCE)
	@HOSTCC="$(CDK2_NATIVE_CC)" COREBOOT_ROM="$(COREBOOT_ROM)" CBFSTOOL="$(CBFSTOOL)" \
		MM_UPDATE_CAPSULE_ROUNDTRIP="$(MM_UPDATE_CAPSULE_ROUNDTRIP)" \
		MM_CAPSULE_PROVIDER_CHECK="$(MM_CAPSULE_PROVIDER_CHECK)" \
		MM_CAPSULE_COLD_CHECK="$(MM_CAPSULE_COLD_CHECK)" \
		MM_CAPSULE_APPLY="$(MM_CAPSULE_APPLY)" \
		MM_CAPSULE_DENIAL="$(MM_CAPSULE_DENIAL)" \
		MM_SIGNED_CAPSULE="$(MM_SIGNED_CAPSULE)" \
		MM_TRUST_CERTIFICATE="$(MM_TRUST_CERTIFICATE)" \
		CAPSULE_TOOL_ROOT="$(COREBOOT_TREE)/util/efi_capsule" \
		MM_APPLY_VERSION="$(MM_APPLY_VERSION)" \
		MM_PRIOR_VERSION="$(MM_PRIOR_VERSION)" \
		MM_AUTHENTICATED_IMAGE="$(MM_AUTHENTICATED_IMAGE)" \
		MM_TARGET_IMAGE="$(MM_TARGET_IMAGE)" \
		MM_CAPSULE_BUILD_ONLY="$(MM_CAPSULE_BUILD_ONLY)" \
		MM_EXPECT_RUNNING_VERSION="$(MM_EXPECT_RUNNING_VERSION)" \
		MM_EXPECT_HISTORY_VERSION="$(MM_EXPECT_HISTORY_VERSION)" \
		NATIVE_SERVICE_LIFECYCLE_SOURCES="$(filter-out $(CDK2_DIR)/src/boot/coreboot_checksum.c,$(CDK2_NATIVE_DXE_LIFECYCLE_TEST_SOURCES))" \
		PROTECTED_ENTRY_CRYPTO_SOURCES="$(filter-out $(CDK2_DIR)/tests/authenticode_timestamp_test.c,$(CDK2_AUTHENTICODE_TIMESTAMP_TEST_SOURCES)) $(CDK2_BEARSSL_SOURCE_PATHS)" \
		PROTECTED_ENTRY_CRYPTO_FLAGS="$(CDK2_SYSTEM_FMP_CRYPTO_FLAGS) -I$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot -I$(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/include" \
		NATIVE_PROBE_CFLAGS="$(CDK2_NATIVE_CFLAGS)" \
		sh "$(CDK2_DIR)/tests/capsule_mm_native_reset_probe_test.sh" \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_BUILD_DIR)/$(if $(MM_CAPSULE_PROVIDER_CHECK),capsule-provider-info-close-component,$(if $(MM_UPDATE_CAPSULE_ROUNDTRIP),capsule-mm-update-component,capsule-mm-reset-component))"

# Terminal test application for a genuine packaged graph; not an admitted
# protected payload and not a substitute runtime protocol/event composition.
CDK2_PUBLIC_FULLGRAPH_APP_SOURCES := \
	tests/protected_variable_fullgraph_runtime_app.c \
	src/modules/authvar_transport/owner.c \
	src/modules/authvar_transport/transport.c \
	src/modules/authvar_transport/native_x86.c \
	src/lib/payload_mm_authvar_service.c src/lib/image_policy_snapshot.c \
	src/boot/coreboot.c src/boot/coreboot_checksum.c src/boot/coreboot_resource.c \
	src/lib/mem.c
CDK2_PUBLIC_FULLGRAPH_APP_OBJS := $(foreach source,$(CDK2_PUBLIC_FULLGRAPH_APP_SOURCES),\
	$(CDK2_NATIVE_BUILD_DIR)/public-fullgraph-app-$(notdir $(basename $(source))).o)
CDK2_PUBLIC_FULLGRAPH_APP_PE := $(CDK2_NATIVE_BUILD_DIR)/PublicFullgraphRuntimeDiagnostic.efi
CDK2_PUBLIC_FULLGRAPH_APP_LINK_OBJ := $(CDK2_NATIVE_BUILD_DIR)/public-fullgraph-app-linked.o
define cdk2_public_fullgraph_app_rule
$(CDK2_NATIVE_BUILD_DIR)/public-fullgraph-app-$(notdir $(basename $(1))).o: \
		$(CDK2_DIR)/$(1) $(CDK2_CONFIG_HEADER) $(CDK2_DIR)/src/boot/Makefile | $(CDK2_NATIVE_BUILD_DIR)
	@$$(CDK2_NATIVE_CC) $$(CDK2_NATIVE_CFLAGS) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $$(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -MF "$$@.d" -MT "$$@" -c "$$<" -o "$$@"
endef
$(foreach source,$(CDK2_PUBLIC_FULLGRAPH_APP_SOURCES),\
	$(eval $(call cdk2_public_fullgraph_app_rule,$(source))))
$(CDK2_PUBLIC_FULLGRAPH_APP_LINK_OBJ): $(CDK2_PUBLIC_FULLGRAPH_APP_OBJS) \
		$(CDK2_DIR)/src/boot/Makefile
	@$(CDK2_NATIVE_LD) -r --gc-sections -u cdk2_public_fullgraph_runtime_entry \
		-o "$@" $(CDK2_PUBLIC_FULLGRAPH_APP_OBJS)
$(CDK2_PUBLIC_FULLGRAPH_APP_PE): $(CDK2_PUBLIC_FULLGRAPH_APP_LINK_OBJ) \
		$(CDK2_DIR)/tests/protected_variable_fullgraph_runtime_app.ld \
		$(CDK2_NATIVE_PERELOCCHECK) $(CDK2_NATIVE_PE_LINK) \
		$(CDK2_NATIVE_PE_RELOCATION_MARKER_OBJ)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 \
		--entry cdk2_public_fullgraph_runtime_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/protected_variable_fullgraph_runtime_app.ld" \
		-o "$@" $(CDK2_PUBLIC_FULLGRAPH_APP_LINK_OBJ)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"
.PHONY: native-protected-variable-fullgraph-runtime-app
native-protected-variable-fullgraph-runtime-app: $(CDK2_PUBLIC_FULLGRAPH_APP_PE)
	@printf '%s\n' "$(CDK2_PUBLIC_FULLGRAPH_APP_PE)"

# Separate enrolled test variant. The ordinary diagnostic remains unchanged;
# all firmware drivers/authorizers are the same real packaged inventory.
CDK2_PUBLIC_FULLGRAPH_CHILD_OBJ := $(CDK2_NATIVE_BUILD_DIR)/public-fullgraph-child.o
CDK2_PUBLIC_FULLGRAPH_CHILD_LINK := $(CDK2_NATIVE_BUILD_DIR)/public-fullgraph-child-linked.o
CDK2_PUBLIC_FULLGRAPH_CHILD_PE := $(CDK2_NATIVE_BUILD_DIR)/PublicFullgraphSignedChild.efi
CDK2_PUBLIC_FULLGRAPH_AUTH_DIR := $(CDK2_NATIVE_BUILD_DIR)/fullgraph-enrolled-inputs
CDK2_PUBLIC_FULLGRAPH_AUTH_OBJ := $(CDK2_NATIVE_BUILD_DIR)/fullgraph-enrolled-inputs.o
CDK2_PUBLIC_FULLGRAPH_ENROLLED_OBJ := $(CDK2_NATIVE_BUILD_DIR)/public-fullgraph-enrolled-app.o
CDK2_PUBLIC_FULLGRAPH_ENROLLED_LINK := $(CDK2_NATIVE_BUILD_DIR)/public-fullgraph-enrolled-linked.o
CDK2_PUBLIC_FULLGRAPH_ENROLLED_PE := $(CDK2_NATIVE_BUILD_DIR)/PublicFullgraphEnrolledDiagnostic.efi
CDK2_PUBLIC_FULLGRAPH_ENROLLED_OBJS := $(filter-out \
	$(CDK2_NATIVE_BUILD_DIR)/public-fullgraph-app-protected_variable_fullgraph_runtime_app.o,\
	$(CDK2_PUBLIC_FULLGRAPH_APP_OBJS)) \
	$(CDK2_PUBLIC_FULLGRAPH_ENROLLED_OBJ) $(CDK2_PUBLIC_FULLGRAPH_AUTH_OBJ)
$(CDK2_PUBLIC_FULLGRAPH_CHILD_OBJ): $(CDK2_DIR)/tests/protected_variable_fullgraph_child.c \
		$(CDK2_CONFIG_HEADER) $(CDK2_DIR)/src/boot/Makefile | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -MF "$@.d" -MT "$@" -c "$<" -o "$@"
$(CDK2_PUBLIC_FULLGRAPH_CHILD_LINK): $(CDK2_PUBLIC_FULLGRAPH_CHILD_OBJ) $(CDK2_DIR)/src/boot/Makefile
	@$(CDK2_NATIVE_LD) -r --gc-sections -u cdk2_public_fullgraph_runtime_entry -o "$@" "$<"
$(CDK2_PUBLIC_FULLGRAPH_CHILD_PE): $(CDK2_PUBLIC_FULLGRAPH_CHILD_LINK) \
		$(CDK2_DIR)/tests/protected_variable_fullgraph_runtime_app.ld \
		$(CDK2_NATIVE_PERELOCCHECK) $(CDK2_NATIVE_PE_LINK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry cdk2_public_fullgraph_runtime_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/protected_variable_fullgraph_runtime_app.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"
$(CDK2_PUBLIC_FULLGRAPH_AUTH_OBJ): $(CDK2_PUBLIC_FULLGRAPH_CHILD_PE) \
		$(CDK2_DIR)/tests/protected_variable_fullgraph_enrolled_inputs.sh \
		$(CDK2_DIR)/tests/protected_variable_native_service_auth_inputs.sh $(CDK2_DIR)/src/boot/Makefile
	@sh "$(CDK2_DIR)/tests/protected_variable_fullgraph_enrolled_inputs.sh" \
		"$(CDK2_PUBLIC_FULLGRAPH_AUTH_DIR)" "$<" \
		> "$(CDK2_NATIVE_BUILD_DIR)/fullgraph-enrolled-inputs.log" 2>&1
	@cd "$(CDK2_PUBLIC_FULLGRAPH_AUTH_DIR)" && $(CDK2_NATIVE_LD) -r -b binary \
		pk_auth.bin kek_auth.bin db_auth.bin db_newer_auth.bin db_wrong_auth.bin \
		private_create_auth.bin private_wrong_auth.bin private_append_auth.bin \
		private_delete_auth.bin private_appended_value.bin private_certdb.bin \
		child_unsigned.efi child_db.efi child_rogue.efi -o "$@"
$(CDK2_PUBLIC_FULLGRAPH_ENROLLED_OBJ): $(CDK2_DIR)/tests/protected_variable_fullgraph_runtime_app.c \
		$(CDK2_CONFIG_HEADER) $(CDK2_DIR)/src/boot/Makefile | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -DCDK2_FULLGRAPH_ENROLLED=1 \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -MF "$@.d" -MT "$@" -c "$<" -o "$@"
$(CDK2_PUBLIC_FULLGRAPH_ENROLLED_LINK): $(CDK2_PUBLIC_FULLGRAPH_ENROLLED_OBJS) \
		$(CDK2_DIR)/src/boot/Makefile
	@$(CDK2_NATIVE_LD) -r --gc-sections -u cdk2_public_fullgraph_runtime_entry \
		-o "$@" $(CDK2_PUBLIC_FULLGRAPH_ENROLLED_OBJS)
$(CDK2_PUBLIC_FULLGRAPH_ENROLLED_PE): $(CDK2_PUBLIC_FULLGRAPH_ENROLLED_LINK) \
		$(CDK2_DIR)/tests/protected_variable_fullgraph_runtime_app.ld \
		$(CDK2_NATIVE_PERELOCCHECK) $(CDK2_NATIVE_PE_LINK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry cdk2_public_fullgraph_runtime_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/protected_variable_fullgraph_runtime_app.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"
.PHONY: native-protected-variable-fullgraph-enrolled-app
native-protected-variable-fullgraph-enrolled-app: $(CDK2_PUBLIC_FULLGRAPH_ENROLLED_PE)
	@printf '%s\n' "$(CDK2_PUBLIC_FULLGRAPH_ENROLLED_PE)"

# Separate normal-profile private AUTH2 clause. No Core/driver/config override,
# enrollment, child admission, test FMP, or capsule authority is introduced.
CDK2_NORMAL_PRIVATE_AUTH_DIR := $(CDK2_NATIVE_BUILD_DIR)/normal-private-auth2-inputs
CDK2_NORMAL_PRIVATE_AUTH_OBJ := $(CDK2_NATIVE_BUILD_DIR)/normal-private-auth2-inputs.o
CDK2_NORMAL_PRIVATE_APP_OBJ := $(CDK2_NATIVE_BUILD_DIR)/normal-private-auth2-app.o
CDK2_NORMAL_PRIVATE_APP_LINK := $(CDK2_NATIVE_BUILD_DIR)/normal-private-auth2-linked.o
CDK2_NORMAL_PRIVATE_APP_PE := $(CDK2_NATIVE_BUILD_DIR)/NormalPrivateAuth2OlderAppend.efi
CDK2_NORMAL_PRIVATE_APP_OBJS := $(filter-out \
	$(CDK2_NATIVE_BUILD_DIR)/public-fullgraph-app-protected_variable_fullgraph_runtime_app.o,\
	$(CDK2_PUBLIC_FULLGRAPH_APP_OBJS)) $(CDK2_NORMAL_PRIVATE_APP_OBJ) $(CDK2_NORMAL_PRIVATE_AUTH_OBJ)
$(CDK2_NORMAL_PRIVATE_AUTH_OBJ): $(CDK2_DIR)/tests/protected_variable_fullgraph_enrolled_inputs.sh \
		$(CDK2_DIR)/tests/protected_variable_native_service_auth_inputs.sh $(CDK2_DIR)/src/boot/Makefile \
		| $(CDK2_NATIVE_BUILD_DIR)
	@sh "$(CDK2_DIR)/tests/protected_variable_fullgraph_enrolled_inputs.sh" \
		"$(CDK2_NORMAL_PRIVATE_AUTH_DIR)" --no-child normal-older-append \
		> "$(CDK2_NATIVE_BUILD_DIR)/normal-private-auth2-inputs.log" 2>&1
	@cd "$(CDK2_NORMAL_PRIVATE_AUTH_DIR)" && $(CDK2_NATIVE_LD) -r -b binary \
		private_create_auth.bin private_wrong_auth.bin private_append_auth.bin \
		private_appended_value.bin private_certdb.bin -o "$@"
$(CDK2_NORMAL_PRIVATE_APP_OBJ): $(CDK2_DIR)/tests/protected_variable_fullgraph_runtime_app.c \
		$(CDK2_CONFIG_HEADER) $(CDK2_DIR)/src/boot/Makefile | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -DCDK2_NORMAL_PRIVATE_OLDER_APPEND=1 \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -MF "$@.d" -MT "$@" -c "$<" -o "$@"
$(CDK2_NORMAL_PRIVATE_APP_LINK): $(CDK2_NORMAL_PRIVATE_APP_OBJS) $(CDK2_DIR)/src/boot/Makefile
	@$(CDK2_NATIVE_LD) -r --gc-sections -u cdk2_public_fullgraph_runtime_entry \
		-o "$@" $(CDK2_NORMAL_PRIVATE_APP_OBJS)
$(CDK2_NORMAL_PRIVATE_APP_PE): $(CDK2_NORMAL_PRIVATE_APP_LINK) \
		$(CDK2_DIR)/tests/protected_variable_fullgraph_runtime_app.ld \
		$(CDK2_NATIVE_PERELOCCHECK) $(CDK2_NATIVE_PE_LINK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry cdk2_public_fullgraph_runtime_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/protected_variable_fullgraph_runtime_app.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"
.PHONY: native-protected-variable-normal-older-append-app
native-protected-variable-normal-older-append-app: $(CDK2_NORMAL_PRIVATE_APP_PE)
	@printf '%s\n' "$(CDK2_NORMAL_PRIVATE_APP_PE)"

# Cold enrolled MAIN uses the ordinary runtime app without enrollment inputs.
CDK2_PUBLIC_FULLGRAPH_COLD_OBJ := $(CDK2_NATIVE_BUILD_DIR)/public-fullgraph-cold-app.o
CDK2_PUBLIC_FULLGRAPH_COLD_LINK := $(CDK2_NATIVE_BUILD_DIR)/public-fullgraph-cold-linked.o
CDK2_PUBLIC_FULLGRAPH_COLD_PE := $(CDK2_NATIVE_BUILD_DIR)/PublicFullgraphColdEnrolled.efi
CDK2_PUBLIC_FULLGRAPH_COLD_OBJS := $(filter-out \
	$(CDK2_NATIVE_BUILD_DIR)/public-fullgraph-app-protected_variable_fullgraph_runtime_app.o,\
	$(CDK2_PUBLIC_FULLGRAPH_APP_OBJS)) $(CDK2_PUBLIC_FULLGRAPH_COLD_OBJ)
$(CDK2_PUBLIC_FULLGRAPH_COLD_OBJ): $(CDK2_DIR)/tests/protected_variable_fullgraph_runtime_app.c \
		$(CDK2_CONFIG_HEADER) $(CDK2_DIR)/src/boot/Makefile | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -DCDK2_FULLGRAPH_COLD_ENROLLED=1 \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -MF "$@.d" -MT "$@" -c "$<" -o "$@"
$(CDK2_PUBLIC_FULLGRAPH_COLD_LINK): $(CDK2_PUBLIC_FULLGRAPH_COLD_OBJS) \
		$(CDK2_DIR)/src/boot/Makefile
	@$(CDK2_NATIVE_LD) -r --gc-sections -u cdk2_public_fullgraph_runtime_entry \
		-o "$@" $(CDK2_PUBLIC_FULLGRAPH_COLD_OBJS)
$(CDK2_PUBLIC_FULLGRAPH_COLD_PE): $(CDK2_PUBLIC_FULLGRAPH_COLD_LINK) \
		$(CDK2_DIR)/tests/protected_variable_fullgraph_runtime_app.ld \
		$(CDK2_NATIVE_PERELOCCHECK) $(CDK2_NATIVE_PE_LINK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry cdk2_public_fullgraph_runtime_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/protected_variable_fullgraph_runtime_app.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"
.PHONY: native-protected-variable-fullgraph-cold-enrolled-app
native-protected-variable-fullgraph-cold-enrolled-app: $(CDK2_PUBLIC_FULLGRAPH_COLD_PE)
	@printf '%s\n' "$(CDK2_PUBLIC_FULLGRAPH_COLD_PE)"
.PHONY: native-protected-variable-fullgraph-diagnostic-test
.PHONY: native-protected-variable-fullgraph-enrolled-diagnostic-test
native-protected-variable-fullgraph-diagnostic-test native-protected-variable-fullgraph-enrolled-diagnostic-test:
	@echo 'Historical outer-guard-discard diagnostic retired; use native-protected-variable-fullgraph-production-test' >&2
	@exit 1

.PHONY: native-protected-variable-fullgraph-production-test
native-protected-variable-fullgraph-production-test:
	@COREBOOT_TREE="$(COREBOOT_TREE)" COREBOOT_ROM="$(COREBOOT_ROM)" \
		CBFSTOOL="$(CBFSTOOL)" MAKE="$(MAKE)" \
		COREBOOT_ABSENT_ROM="$(COREBOOT_ABSENT_ROM)" CBFSTOOL_ABSENT="$(CBFSTOOL_ABSENT)" \
		COREBOOT_OLD_GENERAL_ROM="$(COREBOOT_OLD_GENERAL_ROM)" \
		CBFSTOOL_OLD_GENERAL="$(CBFSTOOL_OLD_GENERAL)" \
		bash "$(CDK2_DIR)/tests/protected_variable_fullgraph_production_test.sh" \
		"$(CDK2_NATIVE_BUILD_DIR)/protected-variable-fullgraph-production"

.PHONY: native-protected-variable-fullgraph-cold-enrolled-test
native-protected-variable-fullgraph-cold-enrolled-test:
	@COREBOOT_TREE="$(COREBOOT_TREE)" COREBOOT_ROM="$(COREBOOT_ROM)" \
		CBFSTOOL="$(CBFSTOOL)" MAKE="$(MAKE)" \
		COREBOOT_ABSENT_ROM="$(COREBOOT_ABSENT_ROM)" CBFSTOOL_ABSENT="$(CBFSTOOL_ABSENT)" \
		COREBOOT_OLD_GENERAL_ROM="$(COREBOOT_OLD_GENERAL_ROM)" \
		CBFSTOOL_OLD_GENERAL="$(CBFSTOOL_OLD_GENERAL)" \
		bash "$(CDK2_DIR)/tests/protected_variable_fullgraph_cold_enrolled_test.sh" \
		"$(CDK2_NATIVE_BUILD_DIR)/protected-variable-fullgraph-cold-enrolled"

define cdk2_protected_image_policy_join_test_rule
$(CDK2_NATIVE_BUILD_DIR)/protected-image-policy-join-test-o$(1): \
		$(CDK2_PROTECTED_IMAGE_POLICY_JOIN_SOURCES) $(CDK2_BEARSSL_SOURCE_PATHS) \
		$(CDK2_DIR)/src/modules/security_stub/security_stub.c \
		$(CDK2_DIR)/include/cdk2/image_policy_snapshot.h \
		$(CDK2_DIR)/include/cdk2/protected_image_policy.h \
		$(CDK2_DIR)/include/cdk2/authvar_namespace.h \
		$(CDK2_DIR)/include/cdk2/authvar_runtime_owner.h \
		$(CDK2_DIR)/include/cdk2/authvar_transport.h | $(CDK2_NATIVE_BUILD_DIR)
	$$(call cdk2_native_host_test,-O$(1) $$(CDK2_PROTECTED_IMAGE_POLICY_JOIN_FLAGS),\
		$$(CDK2_PROTECTED_IMAGE_POLICY_JOIN_SOURCES) $$(CDK2_BEARSSL_SOURCE_PATHS))
endef
$(foreach optimization,0 2,\
	$(eval $(call cdk2_protected_image_policy_join_test_rule,$(optimization))))

# The generic handler-discard and canonical policy-result mutations must each
# fail the joined policy oracle.
$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_SECURITY_MUTANT): \
		$(CDK2_DIR)/src/modules/security_stub/security_stub.c | $(CDK2_NATIVE_BUILD_DIR)
	@test "$$(sed -n '/^static efi_status_t CDK2_MS_ABI authenticate2(/,/^}/p' \
		"$<" | grep -c 'return status;')" -eq 1
	@sed -e '/^static efi_status_t CDK2_MS_ABI authenticate2(/,/^}/ {' \
		-e 's/return status;/return EFI_SUCCESS;/' -e '}' "$<" > "$@"
	@! cmp -s "$<" "$@"

$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_CANONICAL_MUTANT): \
		$(CDK2_DIR)/src/modules/security_stub/security_stub.c | $(CDK2_NATIVE_BUILD_DIR)
	@test "$$(grep -Fc 'status = boot_policy(image, image_size, policy_owner);' "$<")" -eq 1
	@sed 's/status = boot_policy(image, image_size, policy_owner);/status = boot_policy(image, image_size, policy_owner);\
		status = EFI_SUCCESS;/' \
		"$<" > "$@"
	@! cmp -s "$<" "$@"

define cdk2_protected_image_policy_join_mutant_rule
$(CDK2_NATIVE_BUILD_DIR)/protected-image-policy-join-mutant-o$(1): \
		$(CDK2_PROTECTED_IMAGE_POLICY_JOIN_SOURCES) $(CDK2_BEARSSL_SOURCE_PATHS) \
		$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_SECURITY_MUTANT)
	$$(call cdk2_native_host_test,-O$(1) $$(CDK2_PROTECTED_IMAGE_POLICY_JOIN_FLAGS) \
		-DCDK2_JOIN_SECURITY_STUB_SOURCE='"$$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_SECURITY_MUTANT)"' \
		,$$(CDK2_PROTECTED_IMAGE_POLICY_JOIN_SOURCES) $$(CDK2_BEARSSL_SOURCE_PATHS))
endef
$(foreach optimization,0 2,\
	$(eval $(call cdk2_protected_image_policy_join_mutant_rule,$(optimization))))

define cdk2_protected_image_policy_join_canonical_mutant_rule
$(CDK2_NATIVE_BUILD_DIR)/protected-image-policy-join-canonical-mutant-o$(1): \
		$(CDK2_PROTECTED_IMAGE_POLICY_JOIN_SOURCES) $(CDK2_BEARSSL_SOURCE_PATHS) \
		$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_CANONICAL_MUTANT)
	$$(call cdk2_native_host_test,-O$(1) $$(CDK2_PROTECTED_IMAGE_POLICY_JOIN_FLAGS) \
		-DCDK2_JOIN_SECURITY_STUB_SOURCE='"$$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_CANONICAL_MUTANT)"' \
		,$$(CDK2_PROTECTED_IMAGE_POLICY_JOIN_SOURCES) $$(CDK2_BEARSSL_SOURCE_PATHS))
endef
$(foreach optimization,0 2,\
	$(eval $(call cdk2_protected_image_policy_join_canonical_mutant_rule,$(optimization))))

.PHONY: native-protected-image-policy-join-test
native-protected-image-policy-join-test: \
		$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_TEST_O0) \
		$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_TEST_O2) \
		$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_MUTANT_O0) \
		$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_MUTANT_O2) \
		$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_CANONICAL_MUTANT_O0) \
		$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_CANONICAL_MUTANT_O2) \
		$(CDK2_NATIVE_AUTHENTICODE_CRYPTO_TEST_O0) \
		$(CDK2_NATIVE_BUILD_DIR)/authenticode-timestamp-test-o2
	@COREBOOT_TREE="$(COREBOOT_TREE)" \
		sh "$(CDK2_DIR)/tests/protected_image_policy_join_test.sh" \
		"$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_TEST_O0)" \
		"$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_TEST_O2)" \
		"$(CDK2_NATIVE_AUTHENTICODE_CRYPTO_TEST_O0)" \
		"$(CDK2_NATIVE_BUILD_DIR)/authenticode-timestamp-test-o2" \
		"$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_MUTANT_O0)" \
		"$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_MUTANT_O2)" \
		"$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_CANONICAL_MUTANT_O0)" \
		"$(CDK2_NATIVE_PROTECTED_IMAGE_POLICY_JOIN_CANONICAL_MUTANT_O2)"

CDK2_NATIVE_IMAGE_AUTHORIZATION_TEST_SOURCES := \
	$(filter-out $(CDK2_DIR)/tests/authenticode_crypto_test.c,\
		$(CDK2_AUTHENTICODE_CRYPTO_TEST_SOURCES)) \
	$(CDK2_DIR)/tests/image_authorization_test.c \
	$(CDK2_DIR)/src/lib/image_authorization.c \
	$(CDK2_DIR)/src/lib/signature_database.c

define cdk2_image_authorization_test_rule
$(CDK2_NATIVE_BUILD_DIR)/image-authorization-test-o$(1): \
		$(CDK2_NATIVE_IMAGE_AUTHORIZATION_TEST_SOURCES) $(CDK2_BEARSSL_SOURCE_PATHS) \
		$(CDK2_DIR)/include/cdk2/image_authorization.h \
		$(CDK2_DIR)/include/cdk2/signature_database.h \
		$(CDK2_DIR)/include/cdk2/authenticode_crypto.h \
		$(CDK2_DIR)/include/cdk2/authenticode_spc.h \
		$(CDK2_DIR)/include/cdk2/pe_authenticode.h \
		$(CDK2_DIR)/include/cdk2/system_fmp_crypto.h \
		$(CDK2_DIR)/include/cdk2/system_fmp_auth.h | $(CDK2_NATIVE_BUILD_DIR)
	@$$(CDK2_NATIVE_HOST_CC) $$(CDK2_NATIVE_HOST_CFLAGS) -O$(1) \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		$$(CDK2_NATIVE_INCLUDES) $$(CDK2_SYSTEM_FMP_CRYPTO_FLAGS) \
		-I$$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot \
		-I$$(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/include \
		$$(CDK2_NATIVE_IMAGE_AUTHORIZATION_TEST_SOURCES) \
		$$(CDK2_BEARSSL_SOURCE_PATHS) -o "$$@"
endef
$(foreach optimization,0 2,\
	$(eval $(call cdk2_image_authorization_test_rule,$(optimization))))

.PHONY: native-image-authorization-test
native-image-authorization-test: $(CDK2_NATIVE_BUILD_DIR)/image-authorization-test-o0 \
		$(CDK2_NATIVE_BUILD_DIR)/image-authorization-test-o2 \
		$(CDK2_NATIVE_AUTHENTICODE_CRYPTO_TEST_O0)
	@CDK2_IMAGE_AUTHORIZATION_TEST="$(CDK2_NATIVE_BUILD_DIR)/image-authorization-test-o0" \
		CDK2_IMAGE_AUTHORIZATION_TEST_O2="$(CDK2_NATIVE_BUILD_DIR)/image-authorization-test-o2" \
		sh "$(CDK2_DIR)/tests/authenticode_spc_test.sh" \
		"$(CDK2_NATIVE_AUTHENTICODE_CRYPTO_TEST_O0)"

native-check: native-image-authorization-test

.PHONY: native-protected-image-policy-consumer-test
native-protected-image-policy-consumer-test:
	@sh "$(CDK2_DIR)/tests/protected_image_policy_consumer_test.sh"

native-check: native-protected-image-policy-consumer-test

.PHONY: native-system-fmp-payload native-system-fmp-payload-test
native-system-fmp-payload: $(CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_OBJ)

$(CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_OBJ): \
		$(CDK2_DIR)/src/modules/system_fmp/payload.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_payload.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_TEST_O0): \
		$(CDK2_DIR)/tests/system_fmp_payload_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/payload.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_payload.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O0 \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/payload.c" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_TEST_O2): \
		$(CDK2_DIR)/tests/system_fmp_payload_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/payload.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_payload.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O2 \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/payload.c" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_TEST_SANITIZED): \
		$(CDK2_DIR)/tests/system_fmp_payload_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/payload.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_payload.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O1 -g \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/payload.c" -o "$@"

native-system-fmp-payload-test: native-system-fmp-payload \
		$(CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_TESTS)
	@test -z "$(filter $(CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_OBJ),\
		$(CDK2_NATIVE_STAGE_OBJS) $(CDK2_NATIVE_COREBOOT_OBJS))"
	@"$(CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_TEST_O0)"
	@"$(CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_TEST_O2)"
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1 \
		"$(CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_TEST_SANITIZED)"

native-check: native-system-fmp-payload-test

.PHONY: native-system-fmp-state native-system-fmp-state-test
native-system-fmp-state: $(CDK2_NATIVE_SYSTEM_FMP_STATE_OBJ)

$(CDK2_NATIVE_SYSTEM_FMP_STATE_OBJ): \
		$(CDK2_DIR)/src/modules/system_fmp/state.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_state.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_STATE_TEST_O0): \
		$(CDK2_DIR)/tests/system_fmp_state_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/state.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_state.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O0 \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/state.c" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_STATE_TEST_O2): \
		$(CDK2_DIR)/tests/system_fmp_state_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/state.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_state.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O2 \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/state.c" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_STATE_TEST_SANITIZED): \
		$(CDK2_DIR)/tests/system_fmp_state_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/state.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_state.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O1 \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/state.c" -o "$@"

native-system-fmp-state-test: native-system-fmp-state \
		$(CDK2_NATIVE_SYSTEM_FMP_STATE_TESTS)
	@test -z "$(filter $(CDK2_NATIVE_SYSTEM_FMP_STATE_OBJ),\
		$(CDK2_NATIVE_STAGE_OBJS) $(CDK2_NATIVE_COREBOOT_OBJS))"
	@"$(CDK2_NATIVE_SYSTEM_FMP_STATE_TEST_O0)"
	@"$(CDK2_NATIVE_SYSTEM_FMP_STATE_TEST_O2)"
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$(CDK2_NATIVE_SYSTEM_FMP_STATE_TEST_SANITIZED)"

native-check: native-system-fmp-state-test

.PHONY: native-system-fmp-board-binding \
	native-system-fmp-board-binding-test
native-system-fmp-board-binding: $(CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_OBJ)

$(CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_OBJ): \
		$(CDK2_DIR)/src/modules/system_fmp/board_binding.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_board_binding.h \
		$(CDK2_DIR)/include/coreboot_tables.h \
		$(CDK2_DIR)/src/boot/coreboot.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_TEST_O0): \
		$(CDK2_DIR)/tests/system_fmp_board_binding_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/board_binding.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_board_binding.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O0 \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/board_binding.c" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_TEST_O2): \
		$(CDK2_DIR)/tests/system_fmp_board_binding_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/board_binding.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_board_binding.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O2 \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/board_binding.c" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_TEST_SANITIZED): \
		$(CDK2_DIR)/tests/system_fmp_board_binding_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/board_binding.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_board_binding.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O1 -g \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/board_binding.c" -o "$@"

native-system-fmp-board-binding-test: native-system-fmp-board-binding \
		$(CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_TESTS)
	@test -z "$(filter $(CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_OBJ),\
		$(CDK2_NATIVE_STAGE_OBJS) $(CDK2_NATIVE_COREBOOT_OBJS))"
	@"$(CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_TEST_O0)"
	@"$(CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_TEST_O2)"
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1 \
		"$(CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_TEST_SANITIZED)"

native-check: native-system-fmp-board-binding-test

.PHONY: native-system-fmp-coordinator native-system-fmp-coordinator-test
native-system-fmp-coordinator: $(CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_OBJ)

$(CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_OBJ): \
		$(CDK2_DIR)/src/modules/system_fmp/coordinator.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_coordinator.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_TEST_O0): \
		$(CDK2_DIR)/tests/system_fmp_coordinator_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/coordinator.c \
		$(CDK2_DIR)/src/modules/system_fmp/state.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O0 \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/coordinator.c" \
		"$(CDK2_DIR)/src/modules/system_fmp/state.c" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_TEST_O2): \
		$(CDK2_DIR)/tests/system_fmp_coordinator_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/coordinator.c \
		$(CDK2_DIR)/src/modules/system_fmp/state.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O2 \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/coordinator.c" \
		"$(CDK2_DIR)/src/modules/system_fmp/state.c" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_TEST_SANITIZED): \
		$(CDK2_DIR)/tests/system_fmp_coordinator_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/coordinator.c \
		$(CDK2_DIR)/src/modules/system_fmp/state.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O1 \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		"$(CDK2_DIR)/src/modules/system_fmp/coordinator.c" \
		"$(CDK2_DIR)/src/modules/system_fmp/state.c" -o "$@"

native-system-fmp-coordinator-test: native-system-fmp-coordinator \
		$(CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_TESTS)
	@test -z "$(filter $(CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_OBJ),\
		$(CDK2_NATIVE_STAGE_OBJS) $(CDK2_NATIVE_COREBOOT_OBJS))"
	@"$(CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_TEST_O0)"
	@"$(CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_TEST_O2)"
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$(CDK2_NATIVE_SYSTEM_FMP_COORDINATOR_TEST_SANITIZED)"

native-check: native-system-fmp-coordinator-test

.PHONY: native-system-fmp-session native-system-fmp-session-test
native-system-fmp-session: $(CDK2_NATIVE_SYSTEM_FMP_SESSION_OBJ)

$(CDK2_NATIVE_SYSTEM_FMP_TRANSPORT_OBJ): \
		$(CDK2_DIR)/src/modules/system_fmp/transport.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_transport.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/coreboot-hob.o: \
		$(CDK2_DIR)/src/lib/coreboot_hob.c \
		$(CDK2_DIR)/include/cdk2/coreboot_hob.h \
		$(CDK2_DIR)/include/cdk2/hob.h \
		$(CDK2_DIR)/include/pi/hob.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_SESSION_OBJ): \
		$(CDK2_DIR)/src/modules/system_fmp/session.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_session.h $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_HANDOFF_OBJ): \
		$(CDK2_DIR)/src/modules/system_fmp/handoff.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_handoff.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_LINK_FLAGS := \
	-Wl,--gc-sections,--wrap=cdk2_system_fmp_authenticate

$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_O0): \
		$(CDK2_DIR)/tests/system_fmp_session_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/session.c \
		$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_SUPPORT) \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/sha256.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,-O0 -ffunction-sections -fdata-sections \
		$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_LINK_FLAGS),$^)

$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_O2): \
		$(CDK2_DIR)/tests/system_fmp_session_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/session.c \
		$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_SUPPORT) \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/sha256.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,-O2 -ffunction-sections -fdata-sections \
		$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_LINK_FLAGS),$^)

$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_SANITIZED): \
		$(CDK2_DIR)/tests/system_fmp_session_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/session.c \
		$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_SUPPORT) \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/sha256.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,-O1 -g \
		$(CDK2_NATIVE_HOST_SANITIZERS) -fno-sanitize-recover=all \
		-fno-omit-frame-pointer -ffunction-sections -fdata-sections \
		$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_LINK_FLAGS),$^)

$(CDK2_NATIVE_SYSTEM_FMP_HEADERS_TEST): \
		$(CDK2_DIR)/tests/system_fmp_headers_test.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) "$<" -o "$@"

native-system-fmp-session-test: native-system-fmp-session \
		$(CDK2_NATIVE_SYSTEM_FMP_COMPOSITION_OBJ) \
		$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TESTS) \
		$(CDK2_NATIVE_SYSTEM_FMP_HEADERS_TEST)
	@test -z "$(filter $(CDK2_NATIVE_SYSTEM_FMP_SESSION_OBJ),\
		$(CDK2_NATIVE_STAGE_OBJS) $(CDK2_NATIVE_COREBOOT_OBJS))"
	@"$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_O0)"
	@"$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_O2)"
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_SANITIZED)"
	@"$(CDK2_NATIVE_SYSTEM_FMP_HEADERS_TEST)"
	@for symbol in cdk2_system_fmp_session_execute \
		cdk2_system_fmp_authenticate cdk2_system_fmp_payload_check \
		cdk2_system_fmp_board_bind_identity \
		cdk2_system_fmp_client_execute cdk2_system_fmp_sha256; do \
		$(CDK2_NATIVE_NM) --defined-only \
			"$(CDK2_NATIVE_SYSTEM_FMP_COMPOSITION_OBJ)" | \
			grep -Eq "[[:space:]]$$symbol$$" || exit 1; \
	done
	@! $(CDK2_NATIVE_NM) --defined-only \
		"$(CDK2_NATIVE_SYSTEM_FMP_COMPOSITION_OBJ)" | \
		grep -Eq 'cdk2_system_fmp_(coordinator|state)'

$(CDK2_NATIVE_SYSTEM_FMP_COMPOSITION_OBJ): \
		$(CDK2_NATIVE_SYSTEM_FMP_SESSION_OBJ) \
		$(CDK2_NATIVE_SYSTEM_FMP_HANDOFF_OBJ) \
		$(CDK2_NATIVE_SYSTEM_FMP_AUTH_OBJ) \
		$(CDK2_NATIVE_SYSTEM_FMP_PAYLOAD_OBJ) \
		$(CDK2_NATIVE_SYSTEM_FMP_BOARD_BINDING_OBJ) \
		$(CDK2_NATIVE_SYSTEM_FMP_TRANSPORT_OBJ) \
		$(CDK2_NATIVE_BUILD_DIR)/coreboot-hob.o \
		$(CDK2_NATIVE_BUILD_DIR)/coreboot.o \
		$(CDK2_NATIVE_BUILD_DIR)/coreboot_checksum.o \
		$(CDK2_NATIVE_BUILD_DIR)/coreboot_resource.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),\
			$(CDK2_NATIVE_BUILD_DIR)/module-diagnostic-core.o) \
		$(CDK2_NATIVE_SYSTEM_FMP_CRYPTO_CLOSURE)
	@$(CDK2_NATIVE_LD) -r -o "$@" $^
	@test -z "$$($(CDK2_NATIVE_NM) -u "$@")"

native-check: native-system-fmp-session-test

.PHONY: native-system-fmp-transport-diagnostic-test
ifeq ($(CONFIG_CDK2_DIAGNOSTIC),y)
$(CDK2_NATIVE_SYSTEM_FMP_TRANSPORT_DIAGNOSTIC_TEST): \
		$(CDK2_DIR)/tests/system_fmp_transport_diagnostic_test.c \
		$(CDK2_DIR)/tests/system_fmp_session_test.c $(CDK2_CONFIG_HEADER) \
		$(CDK2_DIR)/src/modules/system_fmp/session.c \
		$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_SUPPORT) \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/sha256.c \
		$(CDK2_DIR)/src/lib/diagnostic.c | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,-O1 -g -DCDK2_DIAGNOSTIC \
		$(CDK2_NATIVE_HOST_SANITIZERS) -fno-sanitize-recover=all \
		-ffunction-sections -fdata-sections $(CDK2_NATIVE_INCLUDES) \
		$(CDK2_NATIVE_SYSTEM_FMP_SESSION_TEST_LINK_FLAGS),\
		$(filter-out $(CDK2_DIR)/tests/system_fmp_session_test.c,$(filter %.c,$^)))

native-system-fmp-transport-diagnostic-test: \
		$(CDK2_NATIVE_SYSTEM_FMP_TRANSPORT_DIAGNOSTIC_TEST)
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$<"
else
native-system-fmp-transport-diagnostic-test:
	@printf '%s\n' 'SystemFmp transport diagnostic HOST test: diagnostic disabled'
endif

native-check: native-system-fmp-transport-diagnostic-test

$(CDK2_NATIVE_SYSTEM_FMP_PROVIDER_OBJ): \
		$(CDK2_DIR)/src/modules/system_fmp/provider.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_provider.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_PROVIDER_TEST_O0): \
		$(CDK2_DIR)/tests/system_fmp_provider_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/provider.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,-O0 $(CDK2_NATIVE_INCLUDES),$(filter %.c,$^))

$(CDK2_NATIVE_SYSTEM_FMP_PROVIDER_TEST_O2): \
		$(CDK2_DIR)/tests/system_fmp_provider_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/provider.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,-O2 $(CDK2_NATIVE_INCLUDES),$(filter %.c,$^))

$(CDK2_NATIVE_SYSTEM_FMP_PROVIDER_TEST_SANITIZED): \
		$(CDK2_DIR)/tests/system_fmp_provider_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/provider.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,-O1 -g $(CDK2_NATIVE_HOST_SANITIZERS) -fno-sanitize-recover=all -fno-omit-frame-pointer $(CDK2_NATIVE_INCLUDES),$(filter %.c,$^))

.PHONY: native-system-fmp-provider-test
native-system-fmp-provider-test: $(CDK2_NATIVE_SYSTEM_FMP_PROVIDER_TEST_O0) \
		$(CDK2_NATIVE_SYSTEM_FMP_PROVIDER_TEST_O2) \
		$(CDK2_NATIVE_SYSTEM_FMP_PROVIDER_TEST_SANITIZED)
	@"$(CDK2_NATIVE_SYSTEM_FMP_PROVIDER_TEST_O0)"
	@"$(CDK2_NATIVE_SYSTEM_FMP_PROVIDER_TEST_O2)"
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$(CDK2_NATIVE_SYSTEM_FMP_PROVIDER_TEST_SANITIZED)"

native-check: native-system-fmp-provider-test

$(CDK2_NATIVE_SYSTEM_FMP_ENTRY_TEST_O0): \
		$(CDK2_DIR)/tests/system_fmp_entry_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/entry.c \
		$(CDK2_DIR)/src/lib/coreboot_hob.c \
		$(CDK2_DIR)/src/modules/system_fmp/starlabs_trust.c \
		$(CDK2_DIR)/include/cdk2/coreboot_hob.h \
		$(CDK2_DIR)/include/cdk2/hob.h \
		$(CDK2_DIR)/include/pi/hob.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O0 \
		-DCDK2_SYSTEM_FMP_ENTRY_TEST $(CDK2_NATIVE_INCLUDES) $(filter %.c,$^) -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_ENTRY_TEST_O2): \
		$(CDK2_DIR)/tests/system_fmp_entry_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/entry.c \
		$(CDK2_DIR)/src/lib/coreboot_hob.c \
		$(CDK2_DIR)/src/modules/system_fmp/starlabs_trust.c \
		$(CDK2_DIR)/include/cdk2/coreboot_hob.h \
		$(CDK2_DIR)/include/cdk2/hob.h \
		$(CDK2_DIR)/include/pi/hob.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O2 \
		-DCDK2_SYSTEM_FMP_ENTRY_TEST $(CDK2_NATIVE_INCLUDES) $(filter %.c,$^) -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_ENTRY_TEST_SANITIZED): \
		$(CDK2_DIR)/tests/system_fmp_entry_test.c \
		$(CDK2_DIR)/src/modules/system_fmp/entry.c \
		$(CDK2_DIR)/src/lib/coreboot_hob.c \
		$(CDK2_DIR)/src/modules/system_fmp/starlabs_trust.c \
		$(CDK2_DIR)/include/cdk2/coreboot_hob.h \
		$(CDK2_DIR)/include/cdk2/hob.h \
		$(CDK2_DIR)/include/pi/hob.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O1 -g \
		-DCDK2_SYSTEM_FMP_ENTRY_TEST \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-fno-omit-frame-pointer $(CDK2_NATIVE_INCLUDES) $(filter %.c,$^) -o "$@"

.PHONY: native-system-fmp-entry-test
native-system-fmp-entry-test: $(CDK2_NATIVE_SYSTEM_FMP_ENTRY_TEST_O0) \
		$(CDK2_NATIVE_SYSTEM_FMP_ENTRY_TEST_O2) \
		$(CDK2_NATIVE_SYSTEM_FMP_ENTRY_TEST_SANITIZED)
	@"$(CDK2_NATIVE_SYSTEM_FMP_ENTRY_TEST_O0)"
	@"$(CDK2_NATIVE_SYSTEM_FMP_ENTRY_TEST_O2)"
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$(CDK2_NATIVE_SYSTEM_FMP_ENTRY_TEST_SANITIZED)"

native-check: native-system-fmp-entry-test

$(CDK2_NATIVE_SYSTEM_FMP_ENTRY_OBJ): \
		$(CDK2_DIR)/src/modules/system_fmp/entry.c \
		$(CDK2_DIR)/include/cdk2/system_fmp_provider.h $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_SYSTEM_FMP_PE): $(CDK2_NATIVE_SYSTEM_FMP_OBJS) \
		$(CDK2_DIR)/src/modules/system_fmp/system_fmp.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_system_fmp_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/system_fmp/system_fmp.ld" \
		-o "$@" $(CDK2_NATIVE_SYSTEM_FMP_OBJS)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


.PHONY: native-system-fmp-production-closure-test
native-system-fmp-production-closure-test: $(CDK2_NATIVE_SYSTEM_FMP_PE) \
		$(CDK2_NATIVE_SYSTEM_FMP_PRODUCTION_CLOSURE_TEST)
	@sh "$(CDK2_NATIVE_SYSTEM_FMP_PRODUCTION_CLOSURE_TEST)" \
		"$(CDK2_NATIVE_NM)" $(CDK2_NATIVE_SYSTEM_FMP_OBJS)

native-check: native-system-fmp-production-closure-test


CDK2_NATIVE_CAPSULE_RUNTIME_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-capsule-runtime-test
CDK2_NATIVE_CAPSULE_DISK_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-capsule-disk-test
CDK2_NATIVE_CAPSULE_DISK_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/capsule-disk.o
CDK2_NATIVE_CAPSULE_RUNTIME_ABI_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-capsule-runtime-abi-test
CDK2_NATIVE_CAPSULE_RUNTIME_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-capsule-runtime-entry-test
CDK2_NATIVE_CAPSULE_RUNTIME_SUBSYSTEM_TEST ?= $(CDK2_DIR)/tests/capsule_runtime_subsystem_test.sh
CDK2_NATIVE_CAPSULE_RUNTIME_QEMU_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/capsule-runtime-qemu.o
CDK2_NATIVE_CAPSULE_RUNTIME_QEMU_PE ?= $(CDK2_NATIVE_BUILD_DIR)/CapsuleRuntimeQemu.efi
CDK2_NATIVE_EFI_LOADER_PROBE_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/efi-loader-probe.o
CDK2_NATIVE_EFI_LOADER_PROBE_PE ?= $(CDK2_NATIVE_BUILD_DIR)/EfiLoaderProbe.efi
CDK2_NATIVE_SECURE_BOOT_ENROLL_OBJ ?= \
	$(CDK2_NATIVE_BUILD_DIR)/secure-boot-enroll.o
CDK2_NATIVE_SECURE_BOOT_PK_AUTH_OBJ ?= \
	$(CDK2_NATIVE_BUILD_DIR)/secure-boot-pk-auth.o
CDK2_NATIVE_SECURE_BOOT_KEK_AUTH_OBJ ?= \
	$(CDK2_NATIVE_BUILD_DIR)/secure-boot-kek-auth.o
CDK2_NATIVE_SECURE_BOOT_DB_AUTH_OBJ ?= \
	$(CDK2_NATIVE_BUILD_DIR)/secure-boot-db-auth.o
CDK2_NATIVE_SECURE_BOOT_ENROLL_PE ?= \
	$(CDK2_NATIVE_BUILD_DIR)/SecureBootEnroll.efi
CDK2_NATIVE_SECURE_BOOT_PROBE_OBJ ?= \
	$(CDK2_NATIVE_BUILD_DIR)/secure-boot-loader-probe.o
CDK2_NATIVE_SECURE_BOOT_PROBE_PE ?= \
	$(CDK2_NATIVE_BUILD_DIR)/SecureBootLoaderProbe.efi
CDK2_NATIVE_CAPSULE_RUNTIME_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/capsule-runtime.o
CDK2_NATIVE_CAPSULE_RUNTIME_ABI_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/capsule-runtime-abi.o
CDK2_NATIVE_CAPSULE_RUNTIME_ENTRY_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/capsule-runtime-entry.o
CDK2_NATIVE_CAPSULE_RUNTIME_LINK_INPUTS = $(CDK2_NATIVE_CAPSULE_RUNTIME_OBJ) \
	$(CDK2_NATIVE_CAPSULE_RUNTIME_ABI_OBJ) \
	$(CDK2_NATIVE_CAPSULE_RUNTIME_ENTRY_OBJ) \
	$(CDK2_NATIVE_BUILD_DIR)/mem.o \
	$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE))
CDK2_NATIVE_CAPSULE_RUNTIME_PE ?= $(CDK2_NATIVE_BUILD_DIR)/CapsuleRuntimeDxe.efi
CDK2_NATIVE_ESRT_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-esrt-test
CDK2_NATIVE_ESRT_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-esrt-entry-test
CDK2_NATIVE_ESRT_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/esrt.o
CDK2_NATIVE_ESRT_ENTRY_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/esrt-entry.o
CDK2_NATIVE_ESRT_PE ?= $(CDK2_NATIVE_BUILD_DIR)/EsrtDxe.efi
CDK2_NATIVE_QEMU_TEST_FMP_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-qemu-test-fmp-test
CDK2_NATIVE_QEMU_TEST_FMP_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-qemu-test-fmp-entry-test
CDK2_NATIVE_QEMU_TEST_FMP_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/qemu-test-fmp-core.o
CDK2_NATIVE_QEMU_TEST_FMP_ENTRY_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/qemu-test-fmp-entry.o
CDK2_NATIVE_QEMU_TEST_FMP_SHA_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/qemu-test-fmp-sha256.o
CDK2_NATIVE_QEMU_TEST_FMP_RSA_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/qemu-test-fmp-rsa2048.o
CDK2_NATIVE_QEMU_TEST_FMP_PROTOCOL_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/qemu-test-fmp-protocol.o
CDK2_NATIVE_QEMU_TEST_FMP_OBJS := $(CDK2_NATIVE_QEMU_TEST_FMP_OBJ) \
	$(CDK2_NATIVE_QEMU_TEST_FMP_SHA_OBJ) $(CDK2_NATIVE_QEMU_TEST_FMP_RSA_OBJ) \
	$(CDK2_NATIVE_QEMU_TEST_FMP_PROTOCOL_OBJ) $(CDK2_NATIVE_QEMU_TEST_FMP_ENTRY_OBJ)
CDK2_NATIVE_QEMU_TEST_FMP_PE ?= $(CDK2_NATIVE_BUILD_DIR)/QemuTestFmpDxe.efi
CDK2_NATIVE_QEMU_TEST_FMP_ARTIFACT_PE ?= $(CDK2_NATIVE_BUILD_DIR)/qemu-test-fmp-artifact-test.efi
CDK2_NATIVE_GRAPHICS_CONSOLE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-graphics-console-test
CDK2_NATIVE_GRAPHICS_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-graphics-entry-test
CDK2_NATIVE_GRAPHICS_DIAGNOSTIC_DEBUG_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/graphics-diagnostic-debug-test
CDK2_NATIVE_GRAPHICS_DIAGNOSTIC_RELEASE_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/graphics-diagnostic-release-test
CDK2_NATIVE_GRAPHICS_CONSOLE_OBJS := $(CDK2_NATIVE_BUILD_DIR)/graphics-console-model.o \
	$(CDK2_NATIVE_BUILD_DIR)/graphics-console-diagnostic.o \
	$(CDK2_NATIVE_BUILD_DIR)/graphics-console-binding.o \
	$(CDK2_NATIVE_BUILD_DIR)/graphics-console-entry.o \
	$(CDK2_NATIVE_BUILD_DIR)/graphics-console-font_data.o
CDK2_NATIVE_GRAPHICS_CONSOLE_DIAG_CORE := \
	$(CDK2_NATIVE_BUILD_DIR)/graphics-console-diagnostic-core.o
CDK2_NATIVE_GRAPHICS_CONSOLE_PE ?= $(CDK2_NATIVE_BUILD_DIR)/GraphicsConsoleDxe.efi
CDK2_NATIVE_TCG2_TRANSPORT_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-tcg2-transport-test
CDK2_NATIVE_TCG2_COMMANDS_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-tcg2-commands-test
CDK2_NATIVE_TCG2_EVENT_LOG_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-tcg2-event-log-test
CDK2_NATIVE_TCG2_MEASURE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-tcg2-measure-test
CDK2_NATIVE_TCG2_SERVICE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-tcg2-service-test
CDK2_NATIVE_TCG2_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-tcg2-entry-test
CDK2_NATIVE_TCG2_DIAGNOSTIC_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/tcg2-diagnostic-test
CDK2_NATIVE_TCG2_DIAGNOSTIC_COALESCE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/tcg2-diagnostic-coalesce-test
CDK2_NATIVE_TCG2_PLATFORM_HOB_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/tcg2-platform-hob-test
CDK2_NATIVE_SOFTWARE_HASH_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/software-hash-test
CDK2_SOFTWARE_HASH_INCLUDES := -I"$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot" -I"$(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/include"
CDK2_SOFTWARE_HASH_SRCS := $(CDK2_DIR)/src/lib/tcg_hash/software_hash.c $(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha1.c $(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha256.c $(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha512.c $(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/lib/crypto/sm3.c
CDK2_PE_IMAGE_VIEW_SRC := $(CDK2_DIR)/src/lib/pe_image_view.c
CDK2_NATIVE_TCG2_HASH_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/tcg2-hash-,software.o sha1.o sha256.o sha512.o sm3.o)
CDK2_NATIVE_TCG2_HASH_CFLAGS = $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
	-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
	$(CDK2_NATIVE_INCLUDES) $(CDK2_SOFTWARE_HASH_INCLUDES) -MMD -MP
CDK2_NATIVE_TCG2_CORE_SOURCE_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/tcg2-,driver.o diagnostic.o tcg2_entry.o tcg2_transport.o tcg2_commands.o tcg2_event_log.o tcg2_measure.o tcg2_service.o)
CDK2_NATIVE_TCG2_CORE_OBJS := $(CDK2_NATIVE_TCG2_CORE_SOURCE_OBJS) \
	$(CDK2_NATIVE_BUILD_DIR)/tcg2-probe.o
CDK2_NATIVE_TCG2_OBJS ?= $(CDK2_NATIVE_TCG2_CORE_OBJS) $(CDK2_NATIVE_TCG2_HASH_OBJS) \
	$(CDK2_NATIVE_BUILD_DIR)/mem-memcpy.o
CDK2_NATIVE_TCG2_DIAG_CORE ?= $(CDK2_NATIVE_BUILD_DIR)/tcg2-diagnostic-core.o
CDK2_NATIVE_TCG2_PE ?= $(CDK2_NATIVE_BUILD_DIR)/Tcg2Dxe.efi
ifeq ($(CONFIG_CDK2_NATIVE_CAPSULE_RUNTIME),y)
else ifeq ($(CONFIG_CDK2_NATIVE_GRAPHICS_CONSOLE),y)
endif

CDK2_NATIVE_TPM2_ACPI_TABLE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-tpm2-acpi-table-test
CDK2_NATIVE_DEVICE_PATH_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-device-path-test
CDK2_NATIVE_DEVICE_PATH_DRIVER_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-device-path-driver-test
CDK2_NATIVE_DEVICE_PATH_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/device-path-,driver.o device_path.o device_path_to_text.o device_path_from_text.o)
CDK2_NATIVE_DEVICE_PATH_PE ?= $(CDK2_NATIVE_BUILD_DIR)/DevicePathDxe.efi
CDK2_NATIVE_PARTITION_GPT_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-partition-gpt-test
CDK2_NATIVE_PARTITION_MBR_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-partition-mbr-test
CDK2_NATIVE_PARTITION_EL_TORITO_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-partition-el-torito-test
CDK2_NATIVE_PARTITION_UDF_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-partition-udf-test
CDK2_NATIVE_PARTITION_CHILD_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-partition-child-test
CDK2_NATIVE_PARTITION_DRIVER_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-partition-driver-test
CDK2_NATIVE_PARTITION_OBJS := $(addprefix $(CDK2_NATIVE_BUILD_DIR)/partition-,partition.o partition_child.o partition_driver.o)
CDK2_NATIVE_PARTITION_DIAG_CORE ?= $(CDK2_NATIVE_BUILD_DIR)/partition-diagnostic-core.o
CDK2_NATIVE_PARTITION_OBJS += $(CDK2_NATIVE_BUILD_DIR)/partition-diagnostic.o
CDK2_NATIVE_PARTITION_PE ?= $(CDK2_NATIVE_BUILD_DIR)/PartitionDxe.efi
CDK2_NATIVE_TPM2_ACPI_TABLE_OBJS := $(CDK2_NATIVE_BUILD_DIR)/tpm2-acpi-driver.o \
	$(CDK2_NATIVE_BUILD_DIR)/tpm2-acpi-table.o
CDK2_NATIVE_TPM2_ACPI_TABLE_PE ?= $(CDK2_NATIVE_BUILD_DIR)/Tpm2AcpiTableDxe.efi
CDK2_NATIVE_LVGL_RENDERER_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-lvgl-renderer-test
CDK2_NATIVE_LVGL_SETTINGS_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-lvgl-settings-test
CDK2_LVGL_ROOT := $(CDK2_DIR)/3rdparty/lvgl
ifeq ($(CONFIG_CDK2_LVGL_RENDERER),y)
ifeq ($(wildcard $(CDK2_LVGL_ROOT)/src),)
$(error LVGL sources are unavailable; run 'git submodule update --init 3rdparty/lvgl')
endif
endif
CDK2_LVGL_CONFIG_TEMPLATE := $(CDK2_DIR)/configs/lvgl/lv_conf.h
CDK2_LVGL_CONFIG := $(CDK2_NATIVE_BUILD_DIR)/lvgl-config/lv_conf.h
CDK2_LVGL_SOURCES := $(if $(wildcard $(CDK2_LVGL_ROOT)/src),$(shell find "$(CDK2_LVGL_ROOT)/src" -type f -name '*.c' | LC_ALL=C sort))
CDK2_LVGL_OBJECTS := $(patsubst $(CDK2_LVGL_ROOT)/src/%.c,$(CDK2_NATIVE_BUILD_DIR)/lvgl/%.o,$(CDK2_LVGL_SOURCES))
CDK2_LVGL_LIBRARY ?= $(CDK2_NATIVE_BUILD_DIR)/liblvgl.a
CDK2_NATIVE_LVGL_SMOKE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-lvgl-smoke-test
CDK2_NATIVE_LVGL_UI_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-lvgl-ui-driver-test
CDK2_NATIVE_LVGL_UI_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/lvgl-ui-driver.o
CDK2_NATIVE_LVGL_UI_LINK_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/lvgl-ui-linked.o
CDK2_NATIVE_LVGL_UI_PE ?= $(CDK2_NATIVE_BUILD_DIR)/LvglUiDxe.efi
HOSTCC ?= cc
CDK2_NATIVE_CC ?= $(CC)
CDK2_NATIVE_HOST_CC ?= $(HOSTCC)
CDK2_NATIVE_OBJCOPY ?= objcopy
CDK2_NATIVE_OBJDUMP ?= objdump
CDK2_NATIVE_READELF ?= readelf
CDK2_NATIVE_NM ?= nm
CDK2_NATIVE_LD ?= $(LD)
ifeq ($(strip $(CDK2_NATIVE_LD)),)
CDK2_NATIVE_LD := ld
endif
CDK2_NATIVE_PE_LD = "$(CDK2_NATIVE_PE_LINK)" "$(CDK2_NATIVE_LD)" \
	"$(CDK2_NATIVE_PERELOCCHECK)" "$(CDK2_NATIVE_PE_RELOCATION_MARKER_OBJ)" \
	"$(CDK2_NATIVE_READELF)"
CDK2_NATIVE_COREBOOT_ENTRY ?= $(CDK2_NATIVE_BUILD_DIR)/entry32.o
# Native PE images are far smaller than the small model's 2 GiB relative
# span.  Moving the whole image does not change those internal distances.
CDK2_NATIVE_CFLAGS ?= -ffreestanding -fno-builtin -fno-stack-protector -fpie \
	-mcmodel=small -fvisibility=hidden \
	-include $(CDK2_DIR)/include/cdk2/native_visibility.h \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fdata-sections \
	-ffunction-sections -fshort-wchar -m64 -mno-red-zone -mno-sse -mno-mmx \
	-Os -Wall -Werror
override CDK2_NATIVE_CFLAGS += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
CDK2_NATIVE_HOST_CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -fshort-wchar
CDK2_NATIVE_HOST_LDFLAGS ?=
CDK2_NATIVE_DIAGNOSTIC_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-diagnostic-test
CDK2_NATIVE_DIAGNOSTIC_RUNTIME_TRANSITION_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-diagnostic-runtime-transition-test
CDK2_NATIVE_MODULE_DIAG_CORE ?= $(CDK2_NATIVE_BUILD_DIR)/module-diagnostic-core.o
CDK2_NATIVE_DEADLINE_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-deadline-test
CDK2_NATIVE_LINEAR_BOOT_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-linear-boot-test
CDK2_NATIVE_EARLY_SPLASH_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-early-splash-test
CDK2_DIAGNOSTIC_PARITY_LEDGER ?= $(CDK2_DIR)/migration/diagnostic-parity.tsv
CDK2_NATIVE_INCLUDES := -I$(CDK2_BUILD_DIR)/include -I$(CDK2_DIR)/include
CDK2_NATIVE_DEPFILES := $(CDK2_NATIVE_BUILD_DIR)/*.d \
	$(CDK2_NATIVE_PCI_BUS_OBJS:.o=.d) $(CDK2_NATIVE_PCI_BUS_MEM_OBJ:.o=.d)
CDK2_NATIVE_STAGE_OBJS := \
	$(CDK2_NATIVE_BUILD_DIR)/entry.o \
	$(CDK2_NATIVE_BUILD_DIR)/diagnostic.o \
	$(CDK2_NATIVE_BUILD_DIR)/platform.o \
	$(CDK2_NATIVE_BUILD_DIR)/coreboot.o \
	$(CDK2_NATIVE_BUILD_DIR)/coreboot_checksum.o \
	$(CDK2_NATIVE_BUILD_DIR)/coreboot_resource.o \
	$(CDK2_NATIVE_BUILD_DIR)/coreboot_hobs.o \
	$(CDK2_NATIVE_BUILD_DIR)/pe.o \
	$(CDK2_NATIVE_BUILD_DIR)/pe_image_view.o \
	$(CDK2_NATIVE_BUILD_DIR)/mem.o \
	$(CDK2_NATIVE_BUILD_DIR)/services.o \
	$(CDK2_NATIVE_BUILD_DIR)/payload.o
ifeq ($(CONFIG_CDK2_NATIVE_CAPSULE_DISK_SCANNER),y)
CDK2_NATIVE_STAGE_OBJS += $(CDK2_NATIVE_CAPSULE_DISK_OBJ)
endif
CDK2_NATIVE_COREBOOT_OBJS := \
	$(CDK2_NATIVE_COREBOOT_ENTRY) \
	$(CDK2_NATIVE_BUILD_DIR)/entry.o \
	$(CDK2_NATIVE_BUILD_DIR)/diagnostic.o \
	$(CDK2_NATIVE_BUILD_DIR)/platform.o \
	$(CDK2_NATIVE_BUILD_DIR)/coreboot.o \
	$(CDK2_NATIVE_BUILD_DIR)/coreboot_checksum.o \
	$(CDK2_NATIVE_BUILD_DIR)/coreboot_resource.o \
	$(CDK2_NATIVE_BUILD_DIR)/coreboot_hobs.o \
	$(CDK2_NATIVE_BUILD_DIR)/pe.o \
	$(CDK2_NATIVE_BUILD_DIR)/pe_image_view.o \
	$(CDK2_NATIVE_BUILD_DIR)/direct_image_table.o \
	$(CDK2_NATIVE_BUILD_DIR)/mem.o \
	$(CDK2_NATIVE_BUILD_DIR)/linear_boot.o \
	$(CDK2_NATIVE_BUILD_DIR)/boot_logo.o \
	$(CDK2_NATIVE_BUILD_DIR)/early_splash.o \
	$(CDK2_NATIVE_BUILD_DIR)/payload_mm_authvar_service.o \
	$(CDK2_NATIVE_BUILD_DIR)/image_policy_snapshot.o \
	$(CDK2_NATIVE_BUILD_DIR)/coreboot_handoff.o \
	$(CDK2_NATIVE_BUILD_DIR)/services.o \
	$(CDK2_NATIVE_BUILD_DIR)/payload.o
ifeq ($(CONFIG_PAYLOAD_DMA_HANDOFF),y)
CDK2_NATIVE_COREBOOT_OBJS += $(CDK2_NATIVE_BUILD_DIR)/coreboot_dma_handoff.o
endif
ifeq ($(CONFIG_CDK2_NATIVE_CAPSULE_DISK_SCANNER),y)
CDK2_NATIVE_COREBOOT_OBJS += $(CDK2_NATIVE_CAPSULE_DISK_OBJ)
endif
CDK2_NATIVE_OVERRIDE_OBJS := \
	$(CDK2_NATIVE_STAGE_OBJS) \
	$(CDK2_NATIVE_BUILD_DIR)/override.o
CDK2_NATIVE_CAPSULE_DISK_LINK_RETENTION := \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_CAPSULE_DISK_SCANNER)),\
		-u cdk2_capsule_disk_scan)
CDK2_NATIVE_STAGE_LINK_INPUTS := \
	$(CDK2_NATIVE_CAPSULE_DISK_LINK_RETENTION) \
	$(foreach obj,$(CDK2_NATIVE_STAGE_OBJS),"$(obj)")
CDK2_NATIVE_COREBOOT_LINK_INPUTS := \
	$(CDK2_NATIVE_CAPSULE_DISK_LINK_RETENTION) \
	$(foreach obj,$(CDK2_NATIVE_COREBOOT_OBJS),"$(obj)")
CDK2_NATIVE_OVERRIDE_LINK_INPUTS := \
	$(CDK2_NATIVE_CAPSULE_DISK_LINK_RETENTION) \
	$(foreach obj,$(CDK2_NATIVE_OVERRIDE_OBJS),"$(obj)")
CDK2_NATIVE_HOST_TOOLS := \
	$(CDK2_NATIVE_ATA_PE) $(CDK2_NATIVE_ATA_BUS_PE) \
	$(CDK2_NATIVE_SATA_PE) $(CDK2_NATIVE_SCSI_BUS_PE) \
	$(CDK2_NATIVE_XHCI_TEST) \
	$(CDK2_NATIVE_XHCI_CONTROLLER_TEST) \
	$(CDK2_NATIVE_XHCI_PCI_TEST) \
	$(CDK2_NATIVE_BUILD_DIR)/xhci-entry-test \
	$(CDK2_NATIVE_PERELOCCHECK) \
	$(CDK2_NATIVE_METRONOME_TEST) \
	$(CDK2_NATIVE_SCSI_BUS_TEST) \
	$(CDK2_NATIVE_SCSI_BINDING_TEST) \
	$(CDK2_NATIVE_SCSI_ENTRY_TEST) \
	$(CDK2_NATIVE_SATA_CONTROLLER_TEST) \
	$(CDK2_NATIVE_ATA_ATAPI_TEST) \
	$(CDK2_NATIVE_ATA_ATAPI_BINDING_TEST) \
	$(CDK2_NATIVE_ATA_ATAPI_AHCI_TEST) \
	$(CDK2_NATIVE_ATA_ATAPI_IDE_TEST) \
	$(CDK2_NATIVE_ATA_ATAPI_PCI_TEST) \
	$(CDK2_NATIVE_ATA_ATAPI_ENTRY_TEST) \
	$(CDK2_NATIVE_ATA_PROTOCOL_TEST) \
	$(CDK2_NATIVE_ATA_ASYNC_TEST) \
	$(CDK2_NATIVE_ATA_BUS_MODEL_TEST) \
	$(CDK2_NATIVE_ATA_BUS_IO_TEST) \
	$(CDK2_NATIVE_ATA_BUS_BLOCK_TEST) \
	$(CDK2_NATIVE_ATA_BUS_BINDING_TEST) \
	$(CDK2_NATIVE_ATA_BUS_ENTRY_TEST) \
	$(CDK2_NATIVE_ATA_BACKEND_IDE_TEST) \
	$(CDK2_NATIVE_ATA_QEMU_PE) \
	$(CDK2_NATIVE_SATA_ENTRY_TEST) \
	$(CDK2_NATIVE_SATA_QEMU_PE) \
	$(CDK2_NATIVE_PCI_HOST_BRIDGE_TEST) \
	$(CDK2_NATIVE_PCI_HOST_BRIDGE_ENTRY_TEST) \
	$(CDK2_NATIVE_PCI_ROOT_IO_TEST) \
	$(CDK2_NATIVE_PCI_BUS_MODEL_TEST) \
	$(CDK2_NATIVE_PCI_BUS_ENTRY_TEST) \
	$(CDK2_NATIVE_PCI_ENUMERATE_TEST) \
	$(CDK2_NATIVE_PCI_IMMUTABLE_ENTRY_TEST) \
	$(CDK2_NATIVE_WATCHDOG_TEST) \
	$(CDK2_NATIVE_STATUS_CODE_ROUTER_TEST) \
	$(CDK2_NATIVE_STATUS_CODE_HANDLER_TEST) \
	$(CDK2_NATIVE_SERVICE_TEST) \
	$(CDK2_NATIVE_COREBOOT_TEST) \
	$(CDK2_NATIVE_PE_TEST) \
	$(CDK2_NATIVE_ENTRY_TEST) \
	$(CDK2_NATIVE_ELF_CHECK) \
	$(CDK2_NATIVE_ELF_CHECK_TEST) \
	$(CDK2_NATIVE_SECURITY_STUB_TEST) \
	$(CDK2_NATIVE_NULL_MEMORY_TEST) \
	$(CDK2_NATIVE_CPU_IO2_TEST) \
	$(CDK2_NATIVE_MONO_TEST) \
	$(CDK2_NATIVE_RUNTIME_ARCH_TEST) \
	$(CDK2_NATIVE_ENGLISH_TEST) \
	$(CDK2_NATIVE_DISK_IO_TEST) \
	$(CDK2_NATIVE_FAT_TEST) \
	$(CDK2_NATIVE_SERIAL_IO_TEST) \
	$(CDK2_NATIVE_RESET_SYSTEM_TEST) \
	$(CDK2_NATIVE_PCAT_RTC_TEST) \
	$(CDK2_NATIVE_LOCAL_APIC_TIMER_TEST) \
	$(CDK2_NATIVE_LOCAL_APIC_TIMER_DRIVER_TEST) \
	$(CDK2_NATIVE_GRAPHICS_CONSOLE_TEST) \
	$(CDK2_NATIVE_GRAPHICS_OUTPUT_TEST) \
	$(CDK2_NATIVE_GRAPHICS_OUTPUT_DRIVER_TEST) \
	$(CDK2_NATIVE_GRAPHICS_ENTRY_TEST) \
	$(CDK2_NATIVE_LVGL_RENDERER_CHECK_DEPS) \
	$(CDK2_NATIVE_CAPSULE_RUNTIME_TEST) \
	$(CDK2_NATIVE_CAPSULE_RUNTIME_ABI_TEST) \
	$(CDK2_NATIVE_CAPSULE_RUNTIME_ENTRY_TEST) \
	$(CDK2_NATIVE_TCG2_TRANSPORT_TEST) \
	$(CDK2_NATIVE_TCG2_COMMANDS_TEST) \
	$(CDK2_NATIVE_TCG2_EVENT_LOG_TEST) \
	$(CDK2_NATIVE_TCG2_MEASURE_TEST) \
	$(CDK2_NATIVE_TCG2_SERVICE_TEST) \
	$(CDK2_NATIVE_TCG2_ENTRY_TEST) \
	$(CDK2_NATIVE_TCG2_DIAGNOSTIC_COALESCE_TEST) \
	$(CDK2_NATIVE_TCG2_PLATFORM_HOB_TEST) \
	$(CDK2_NATIVE_SOFTWARE_HASH_TEST) \
	$(CDK2_NATIVE_TPM2_ACPI_TABLE_TEST) \
	$(CDK2_NATIVE_ESRT_TEST) \
	$(CDK2_NATIVE_ESRT_ENTRY_TEST) \
	$(CDK2_NATIVE_QEMU_TEST_FMP_TEST) \
	$(CDK2_NATIVE_QEMU_TEST_FMP_ENTRY_TEST) \
	$(CDK2_NATIVE_DEVICE_PATH_TEST) \
	$(CDK2_NATIVE_DEVICE_PATH_DRIVER_TEST) \
	$(CDK2_NATIVE_PARTITION_GPT_TEST) \
	$(CDK2_NATIVE_PARTITION_MBR_TEST) \
	$(CDK2_NATIVE_PARTITION_EL_TORITO_TEST) \
	$(CDK2_NATIVE_PARTITION_UDF_TEST) \
	$(CDK2_NATIVE_PARTITION_CHILD_TEST) \
	$(CDK2_NATIVE_PARTITION_DRIVER_TEST)
CDK2_NATIVE_LINK_BASE_FLAGS := -static -no-pie -nostdlib -nostartfiles \
	-nodefaultlibs -Wl,-z,max-page-size=0x1000 \
	-Wl,-z,common-page-size=0x1000
CDK2_NATIVE_LINK_FLAGS := $(CDK2_NATIVE_LINK_BASE_FLAGS) \
	-Wl,-T,"$(CDK2_NATIVE_DIR)/cdk2.ld"
CDK2_NATIVE_LINK_POST_MAP_FLAGS := -Wl,--gc-sections -Wl,--build-id=none

.PHONY: native-stage native-coreboot-stage native-coreboot-image native-null-memory-test-test \
	native-status-code-router-test native-status-code-handler-test native-cpu-io2-test \
	native-english-test native-disk-io-test native-fat-test native-serial-io-test \
	native-reset-system-test native-pcat-rtc-test native-local-apic-timer-test \
	native-graphics-console-test native-capsule-runtime-test native-capsule-disk-test native-check \
	native-service-test native-coreboot-test native-cbmem-console-test native-pe-test \
	native-entry-test native-security-stub-test native-elfcheck-test FORCE
.PHONY: native-deadline-test native-deadline-provenance-test \
	native-deadline-cbmem-mutation-test native-deadline-tsc-isolation-test \
	native-linear-boot-test native-early-splash-test \
	native-build-dir-coherence-test \
	native-linear-connect-ownership-test

native-check: native-build-dir-coherence-test native-bl-support-retirement-test

.PHONY: native-dma-handoff-test
native-dma-handoff-test:
	@"$(CDK2_DIR)/tests/dma_handoff_test.sh"
native-check: native-dma-handoff-test

.PHONY: native-dma-handoff-integration-test
native-dma-handoff-integration-test:
	@"$(CDK2_DIR)/tests/dma_handoff_config_test.sh" "$(MAKE)" "$(CDK2_DIR)"
native-check: native-dma-handoff-integration-test

.PHONY: native-system-fmp-handoff-test
native-system-fmp-handoff-test:
	@"$(CDK2_DIR)/tests/system_fmp_handoff_test.sh"
native-check: native-system-fmp-handoff-test

.PHONY: native-system-fmp-transport-test
native-system-fmp-transport-test:
	@"$(CDK2_DIR)/tests/system_fmp_transport_test.sh"
native-check: native-system-fmp-transport-test

.PHONY: native-system-fmp-transport-client-test
native-system-fmp-transport-client-test:
	@"$(CDK2_DIR)/tests/system_fmp_transport_client_test.sh"
native-check: native-system-fmp-transport-client-test

.PHONY: native-system-fmp-future-floor-test
native-system-fmp-future-floor-test: $(CDK2_BEARSSL_X509_SOURCE)
	@HOSTCC="$(CDK2_NATIVE_HOST_CC)" \
		FUTURE_FLOOR_CRYPTO_SOURCES="$(CDK2_DIR)/src/lib/cms.c $(CDK2_DIR)/src/lib/authenticode_spc.c $(CDK2_BEARSSL_SOURCE_PATHS)" \
		FUTURE_FLOOR_CRYPTO_FLAGS="$(CDK2_SYSTEM_FMP_CRYPTO_FLAGS)" \
		sh "$(CDK2_DIR)/tests/system_fmp_future_floor_test.sh"
native-check: native-system-fmp-future-floor-test

.PHONY: native-system-fmp-core-ram-stage-app
native-system-fmp-core-ram-stage-app: $(CDK2_CONFIG_HEADER) \
		$(CDK2_NATIVE_PERELOCCHECK) $(CDK2_NATIVE_PE_LINK) \
		$(CDK2_NATIVE_PE_RELOCATION_MARKER_OBJ)
	@HOSTCC="$(CDK2_NATIVE_CC)" HOSTLD="$(CDK2_NATIVE_LD)" \
		HOSTREADELF="$(CDK2_NATIVE_READELF)" \
		NATIVE_PE_LINK="$(CDK2_NATIVE_PE_LINK)" \
		NATIVE_PE_AUDIT="$(CDK2_NATIVE_PERELOCCHECK)" \
		NATIVE_PE_MARKER="$(CDK2_NATIVE_PE_RELOCATION_MARKER_OBJ)" \
		MM_FULL_CORE_CAPSULE="$(MM_FULL_CORE_CAPSULE)" \
		MM_FULL_CORE_TARGET="$(MM_FULL_CORE_TARGET)" \
		MM_FULL_CORE_PUBLIC_TRUST="$(MM_FULL_CORE_PUBLIC_TRUST)" \
		MM_FULL_CORE_ATTEMPT="$(MM_FULL_CORE_ATTEMPT)" \
		MM_FULL_CORE_TARGET_RUNNING="$(MM_FULL_CORE_TARGET_RUNNING)" \
		MM_FULL_CORE_PRIOR_RUNNING="$(MM_FULL_CORE_PRIOR_RUNNING)" \
		MM_FULL_CORE_PRIOR_HISTORY="$(MM_FULL_CORE_PRIOR_HISTORY)" \
		MM_FULL_CORE_REFUSAL="$(MM_FULL_CORE_REFUSAL)" \
		MM_FULL_CORE_CERTIFIED_REFUSAL="$(MM_FULL_CORE_CERTIFIED_REFUSAL)" \
		MM_FULL_CORE_REFERENCE_CAPSULE="$(MM_FULL_CORE_REFERENCE_CAPSULE)" \
		MM_FULL_CORE_SIGNER_CERT="$(MM_FULL_CORE_SIGNER_CERT)" \
		CAPSULE_TOOL_ROOT="$(COREBOOT_TREE)/util/efi_capsule" \
		sh "$(CDK2_DIR)/tests/system_fmp_core_ram_stage_app_test.sh" \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_BUILD_DIR)/system-fmp-core-ram-stage"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 \
		"$(CDK2_NATIVE_BUILD_DIR)/system-fmp-core-ram-stage/SystemFmpCoreRamStage.efi"

.PHONY: native-system-fmp-core-ram-runner-test native-system-fmp-core-ram-native-test
native-system-fmp-core-ram-runner-test:
	@python3 "$(CDK2_DIR)/tests/system_fmp_core_ram_runner_test.py"
native-check: native-system-fmp-core-ram-runner-test

# Explicit private-media fixture: never part of an automatic write gate.
native-system-fmp-core-ram-native-test: native-system-fmp-core-ram-stage-app
	@python3 "$(CDK2_DIR)/tests/system_fmp_core_ram_native_test.py" \
		--initial "$(MM_FULL_CORE_INITIAL)" --target "$(MM_FULL_CORE_TARGET)" \
		--capsule "$(MM_FULL_CORE_CAPSULE)" --trust "$(MM_FULL_CORE_PUBLIC_TRUST)" \
		--disk "$(MM_FULL_CORE_DISK)" --output "$(MM_FULL_CORE_RUN)" \
		--attempt "$(if $(MM_FULL_CORE_ATTEMPT),$(MM_FULL_CORE_ATTEMPT),0x001a000a)" \
		--target-running "$(if $(MM_FULL_CORE_TARGET_RUNNING),$(MM_FULL_CORE_TARGET_RUNNING),0x001a000a)" \
		--prior-running "$(if $(MM_FULL_CORE_PRIOR_RUNNING),$(MM_FULL_CORE_PRIOR_RUNNING),0x001a0009)" \
		--prior-history "$(if $(MM_FULL_CORE_PRIOR_HISTORY),$(MM_FULL_CORE_PRIOR_HISTORY),0)" \
		$(if $(MM_FULL_CORE_REFUSAL),--expect-refusal "$(MM_FULL_CORE_REFUSAL)" --reference-capsule "$(MM_FULL_CORE_REFERENCE_CAPSULE)") \
		$(if $(MM_FULL_CORE_CERTIFIED_REFUSAL),--expect-certified-refusal "$(MM_FULL_CORE_CERTIFIED_REFUSAL)" --reference-capsule "$(MM_FULL_CORE_REFERENCE_CAPSULE)") \
		$(if $(MM_FULL_CORE_SIGNER_CERT),--signer-cert "$(MM_FULL_CORE_SIGNER_CERT)") \
		--app "$(CDK2_NATIVE_BUILD_DIR)/system-fmp-core-ram-stage/SystemFmpCoreRamStage.efi" \
		--config "$(CDK2_CONFIG)" --header "$(CDK2_CONFIG_HEADER)" \
		--core "$(CDK2_NATIVE_COREBOOT_IMAGE)" \
		--inventory "$(CDK2_NATIVE_BUILD_DIR)/native-direct-image-inventory.tsv" \
		--capsule-tools "$(COREBOOT_TREE)/util/efi_capsule" --cbfstool "$(CBFSTOOL)" \
		--compiler "$$(command -v "$(CDK2_NATIVE_CC)")" \
		--linker "$$(command -v "$(CDK2_NATIVE_LD)")" \
		--pe-link "$(CDK2_NATIVE_PE_LINK)" --pe-audit "$(CDK2_NATIVE_PERELOCCHECK)" \
		--pe-marker "$(CDK2_NATIVE_PE_RELOCATION_MARKER_OBJ)" \
		--binding "$(COREBOOT_CONFIG)"

.PHONY: native-system-fmp-session-containment-test
native-system-fmp-session-containment-test:
	@HOSTCC="$(CDK2_NATIVE_HOST_CC)" \
		sh "$(CDK2_DIR)/tests/system_fmp_session_containment_test.sh"
native-check: native-system-fmp-session-containment-test

.PHONY: native-bl-support-retirement-test
native-bl-support-retirement-test:
	@sh "$(CDK2_DIR)/tests/bl_support_retirement_test.sh" "$(MAKE)" "$(CDK2_DIR)"
native-check: native-deadline-provenance-test \
	native-deadline-cbmem-mutation-test native-deadline-tsc-isolation-test
native-build-dir-coherence-test: $(CDK2_NATIVE_BUILD_DIR_COHERENCE_TEST)
	@"$<" "$(MAKE)" "$(CDK2_DIR)" "$(CDK2_DIR)/src/boot/Makefile"
.PHONY: native-tpm2-acpi-table-test native-esrt-test native-esrt-ffs
.PHONY: native-device-path-test
.PHONY: native-partition-test
.PHONY: native-lvgl-renderer-test
.PHONY: native-hii-retirement-test

native-hii-retirement-test:
	@sh "$(CDK2_DIR)/tests/hii_retirement_test.sh" "$(CDK2_DIR)"

native-check: native-hii-retirement-test

$(CDK2_NATIVE_EFI_LOADER_PROBE_OBJ): \
		$(CDK2_DIR)/tests/efi_loader_probe.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) \
		$(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_EFI_LOADER_PROBE_PE): $(CDK2_NATIVE_EFI_LOADER_PROBE_OBJ) \
		$(CDK2_DIR)/tests/efi_loader_probe.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 \
		--entry efi_loader_probe_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/efi_loader_probe.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

.PHONY: native-efi-loader-probe
native-efi-loader-probe: $(CDK2_NATIVE_EFI_LOADER_PROBE_PE)
	@sha256sum "$<"

native-check: native-efi-loader-probe

$(CDK2_NATIVE_SECURE_BOOT_ENROLL_OBJ): \
		$(CDK2_DIR)/tests/secure_boot_enroll.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) \
		$(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

define cdk2_secure_boot_auth_object
$(1): $(CDK2_DIR)/util/qemu/fixtures/secure-boot-test-pki/$(2).auth | \
		$(CDK2_NATIVE_BUILD_DIR)
	@cd "$(CDK2_DIR)/util/qemu/fixtures/secure-boot-test-pki" && \
		"$(CDK2_NATIVE_OBJCOPY)" -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		$(2).auth "$(abspath $(1))"
endef
$(eval $(call cdk2_secure_boot_auth_object,\
	$(CDK2_NATIVE_SECURE_BOOT_PK_AUTH_OBJ),PK))
$(eval $(call cdk2_secure_boot_auth_object,\
	$(CDK2_NATIVE_SECURE_BOOT_KEK_AUTH_OBJ),KEK))
$(eval $(call cdk2_secure_boot_auth_object,\
	$(CDK2_NATIVE_SECURE_BOOT_DB_AUTH_OBJ),db))

$(CDK2_NATIVE_SECURE_BOOT_ENROLL_PE): \
		$(CDK2_NATIVE_SECURE_BOOT_ENROLL_OBJ) \
		$(CDK2_NATIVE_SECURE_BOOT_PK_AUTH_OBJ) \
		$(CDK2_NATIVE_SECURE_BOOT_KEK_AUTH_OBJ) \
		$(CDK2_NATIVE_SECURE_BOOT_DB_AUTH_OBJ) \
		$(CDK2_DIR)/tests/efi_loader_probe.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 \
		--entry secure_boot_enroll_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/efi_loader_probe.ld" -o "$@" \
		$(CDK2_NATIVE_SECURE_BOOT_ENROLL_OBJ) \
		$(CDK2_NATIVE_SECURE_BOOT_PK_AUTH_OBJ) \
		$(CDK2_NATIVE_SECURE_BOOT_KEK_AUTH_OBJ) \
		$(CDK2_NATIVE_SECURE_BOOT_DB_AUTH_OBJ)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

$(CDK2_NATIVE_SECURE_BOOT_PROBE_OBJ): \
		$(CDK2_DIR)/tests/secure_boot_loader_probe.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) \
		$(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_SECURE_BOOT_PROBE_PE): \
		$(CDK2_NATIVE_SECURE_BOOT_PROBE_OBJ) \
		$(CDK2_DIR)/tests/efi_loader_probe.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 \
		--entry secure_boot_loader_probe_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/efi_loader_probe.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

.PHONY: native-secure-boot-qemu-fixtures
native-secure-boot-qemu-fixtures: $(CDK2_NATIVE_SECURE_BOOT_ENROLL_PE) \
		$(CDK2_NATIVE_SECURE_BOOT_PROBE_PE)
	@sha256sum $^

.PHONY: native-secure-boot-test-pki-test
native-secure-boot-test-pki-test:
	@sh "$(CDK2_DIR)/tests/secure_boot_test_pki_test.sh"

.PHONY: native-secure-boot-loader-matrix-test
native-secure-boot-loader-matrix-test: native-secure-boot-qemu-fixtures
	@sh "$(CDK2_DIR)/tests/secure_boot_loader_matrix_test.sh" \
		"$(CDK2_NATIVE_SECURE_BOOT_ENROLL_PE)" \
		"$(CDK2_NATIVE_SECURE_BOOT_PROBE_PE)"

.PHONY: native-secure-boot-qemu-config-test
native-secure-boot-qemu-config-test:
	@sh "$(CDK2_DIR)/tests/secure_boot_qemu_config_test.sh" \
		"$(MAKE)" "$(CDK2_DIR)"

native-check: native-secure-boot-qemu-fixtures \
	native-secure-boot-test-pki-test native-secure-boot-loader-matrix-test \
	native-secure-boot-qemu-config-test

.PHONY: native-efi-loader-matrix-test
native-efi-loader-matrix-test:
	@sh "$(CDK2_DIR)/tests/efi_loader_matrix_test.sh"

native-check: native-efi-loader-matrix-test

ifeq ($(CONFIG_CDK2_LVGL_RENDERER),y)
CDK2_NATIVE_LVGL_RENDERER_CHECK_DEPS := $(CDK2_NATIVE_LVGL_RENDERER_TEST) \
	$(CDK2_NATIVE_LVGL_SMOKE_TEST) $(CDK2_NATIVE_LVGL_UI_TEST) \
	$(CDK2_NATIVE_LVGL_UI_PE)
endif

native-check: $(CDK2_NATIVE_LVGL_RENDERER_CHECK_DEPS) \
	$(if $(filter y,$(CONFIG_CDK2_LVGL_RENDERER)),lvgl-dependency-provenance-test)


$(CDK2_NATIVE_PCI_HOST_BRIDGE_DIAGNOSTIC_OBJ): \
		$(CDK2_DIR)/src/modules/pci_host_bridge/diagnostic.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_PCI_HOST_BRIDGE_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

ifeq ($(CONFIG_CDK2_NATIVE_STAGE),y)
CDK2_NATIVE_STAGE_DEPS := $(CDK2_NATIVE_ELF)
native-stage: $(CDK2_NATIVE_ELF)
native-coreboot-stage: $(CDK2_NATIVE_COREBOOT_ELF) \
	native-mtrr-stage-ownership-audit

.SECONDEXPANSION:
.PHONY: native-dxe-core-fv-protocol-test
native-dxe-core-fv-protocol-test: $(CDK2_NATIVE_DXE_CORE_FV_TEST)
	@"$(CDK2_NATIVE_DXE_CORE_FV_TEST)"

native-check: native-dxe-core-fv-protocol-test

$(CDK2_NATIVE_DXE_CORE_FV_TEST): $(CDK2_DIR)/tests/dxe_core_fv_test.c \
		$(CDK2_DIR)/src/modules/dxe_core/fv_protocol.c \
		$(CDK2_DIR)/src/modules/dxe_core/database.c \
		$(CDK2_DIR)/src/boot/pe.c \
		$(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/src/modules/dxe_core/memory.c \
		$(CDK2_DIR)/include/cdk2/dxe_image_transaction.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $^

.PHONY: native-source-payload-admission
native-source-payload-admission: $(CDK2_CONFIG_HEADER)
	@sh "$(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY_TOOL)" --check-profile \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_MODULE_REGISTRY)"

native-coreboot-image: native-source-payload-admission \
		$(CDK2_NATIVE_COREBOOT_IMAGE) \
		$$(CDK2_NATIVE_COREBOOT_IMAGE_VALIDATION_DEPS)
	@"$(CDK2_NATIVE_ELF_CHECK)" --entry cdk2_coreboot_entry32 \
		--require-direct-images "$(CDK2_NATIVE_COREBOOT_IMAGE)"


.PHONY: native-capsule-disk native-capsule-disk-config-test
native-capsule-disk-config-test:
	@"$(CDK2_DIR)/tests/capsule_disk_config_test.sh" "$(MAKE)" "$(CDK2_DIR)"
ifeq ($(CONFIG_CDK2_NATIVE_CAPSULE_DISK_SCANNER),y)
native-capsule-disk: $(CDK2_NATIVE_CAPSULE_DISK_OBJ)
	@printf '%s\n' "native capsule disk scanner: $(CDK2_NATIVE_CAPSULE_DISK_OBJ)"

$(CDK2_NATIVE_CAPSULE_DISK_OBJ): $(CDK2_DIR)/src/lib/capsule_disk.c \
		$(CDK2_DIR)/include/cdk2/capsule_disk.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"
else
native-capsule-disk:
	@printf '%s\n' 'native capsule disk scanner: disabled by Kconfig'
endif

$(CDK2_NATIVE_MODULE_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

ifeq ($(CONFIG_CDK2_NATIVE_TCG2),y)

$(CDK2_NATIVE_TCG2_CORE_SOURCE_OBJS): $(CDK2_NATIVE_BUILD_DIR)/tcg2-%.o: \
		$(CDK2_DIR)/src/modules/tcg2/%.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/tcg2-probe.o: $(CDK2_DIR)/src/lib/tpm2_acpi_hob.c \
		$(CDK2_DIR)/include/cdk2/tpm2_acpi_hob.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/tcg2-hash-software.o: $(CDK2_DIR)/src/lib/tcg_hash/software_hash.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_TCG2_HASH_CFLAGS) -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"
$(CDK2_NATIVE_BUILD_DIR)/tcg2-hash-sha1.o: $(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha1.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_TCG2_HASH_CFLAGS) -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"
$(CDK2_NATIVE_BUILD_DIR)/tcg2-hash-sha256.o: $(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha256.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_TCG2_HASH_CFLAGS) -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"
$(CDK2_NATIVE_BUILD_DIR)/tcg2-hash-sha512.o: $(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha512.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_TCG2_HASH_CFLAGS) -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"
$(CDK2_NATIVE_BUILD_DIR)/tcg2-hash-sm3.o: $(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/lib/crypto/sm3.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_TCG2_HASH_CFLAGS) -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/tcg2-diagnostic-core.o: \
		$(CDK2_DIR)/src/lib/diagnostic.c $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_TCG2_PE): $(CDK2_NATIVE_TCG2_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_TCG2_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/tcg2/tcg2.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_tcg2_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/tcg2/tcg2.ld" -o "$@" $(CDK2_NATIVE_TCG2_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_TCG2_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


endif

ifeq ($(CONFIG_CDK2_NATIVE_TCG2),y)

$(CDK2_NATIVE_BUILD_DIR)/tpm2-acpi-driver.o: $(CDK2_DIR)/src/modules/tpm2_acpi_table/driver.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args $(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) $(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/tpm2-acpi-table.o: $(CDK2_DIR)/src/modules/tpm2_acpi_table/tpm2_acpi_table.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args $(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) $(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_TPM2_ACPI_TABLE_PE): $(CDK2_NATIVE_TPM2_ACPI_TABLE_OBJS) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) $(CDK2_NATIVE_BUILD_DIR)/mem.o $(CDK2_DIR)/src/modules/tpm2_acpi_table/tpm2_acpi_table.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_tpm2_acpi_entry --image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 --build-id=none --no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/src/modules/tpm2_acpi_table/tpm2_acpi_table.ld" -o "$@" $(CDK2_NATIVE_TPM2_ACPI_TABLE_OBJS) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) $(CDK2_NATIVE_BUILD_DIR)/mem.o
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


endif

$(CDK2_NATIVE_BUILD_DIR)/device-path-driver.o: $(CDK2_DIR)/src/modules/device_path/driver.c $(CDK2_DIR)/include/cdk2/device_path.h $(CDK2_DIR)/include/cdk2/capsule_runtime_abi.h $(CDK2_DIR)/include/uefi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/device-path-%.o: $(CDK2_DIR)/src/modules/device_path/%.c $(CDK2_DIR)/include/cdk2/device_path.h $(CDK2_DIR)/include/uefi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_FAT_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_RESET_SYSTEM_DIAGNOSTIC_OBJ): \
		$(CDK2_DIR)/src/modules/reset_system/diagnostic.c $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_DEVICE_PATH_PE): $(CDK2_NATIVE_DEVICE_PATH_OBJS) \
		$(CDK2_DIR)/src/modules/device_path/device_path.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_device_path_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/device_path/device_path.ld" -o "$@" \
		$(CDK2_NATIVE_DEVICE_PATH_OBJS)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_BUILD_DIR)/partition-%.o: $(CDK2_DIR)/src/modules/partition/%.c $(CDK2_DIR)/include/cdk2/partition.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_PARTITION_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_PARTITION_PE): $(CDK2_NATIVE_PARTITION_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_PARTITION_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/partition/partition.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_partition_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/partition/partition.ld" -o "$@" \
		$(CDK2_NATIVE_PARTITION_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_PARTITION_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


ifeq ($(CONFIG_CDK2_NATIVE_DEVICE_PATH),y)
CDK2_NATIVE_DEVICE_PATH_CHECK_DEPS := $(CDK2_NATIVE_DEVICE_PATH_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_PARTITION),y)
CDK2_NATIVE_PARTITION_CHECK_DEPS := $(CDK2_NATIVE_PARTITION_PE)
endif

ifeq ($(CONFIG_CDK2_NATIVE_PCI_HOST_BRIDGE),y)
CDK2_NATIVE_PCI_HOST_BRIDGE_CHECK_DEPS := $(CDK2_NATIVE_PCI_HOST_BRIDGE_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_USB_MASS_STORAGE),y)
CDK2_NATIVE_USB_MASS_CHECK_DEPS := native-usb-mass-test \
	 native-usb-mass-qemu-oracle
endif

CDK2_NATIVE_PLATFORM_DIAGNOSTIC_CHECKS :=
ifeq ($(CONFIG_CDK2_NATIVE_ACPI_TABLE),y)
CDK2_NATIVE_PLATFORM_DIAGNOSTIC_CHECKS += native-acpi-table-diagnostic-parity
endif
ifeq ($(CONFIG_CDK2_NATIVE_TCG2),y)
CDK2_NATIVE_PLATFORM_DIAGNOSTIC_CHECKS += native-tcg2-diagnostic-parity
endif
ifeq ($(CONFIG_CDK2_ESRT),y)
CDK2_NATIVE_PLATFORM_DIAGNOSTIC_CHECKS += native-esrt-diagnostic-parity
endif
ifeq ($(CONFIG_CDK2_NATIVE_FTW),y)
CDK2_NATIVE_PLATFORM_DIAGNOSTIC_CHECKS += native-ftw-diagnostic-parity
endif
ifeq ($(CONFIG_CDK2_NATIVE_EC_BATTERY),y)
CDK2_NATIVE_PLATFORM_DIAGNOSTIC_CHECKS += native-ec-battery-diagnostic-parity
endif

native-check: native-xhci-test native-sio-bus-test \
	native-coreboot-test
native-check: $(CDK2_CONFIG_HEADER) $(CDK2_NATIVE_ELF) $(CDK2_NATIVE_OVERRIDE_ELF) \
		$(CDK2_NATIVE_COREBOOT_ELF) $(CDK2_NATIVE_COREBOOT_ENTRY) \
		$(CDK2_NATIVE_HOST_TOOLS) $(CDK2_NATIVE_DIAGNOSTIC_TEST) \
		$(CDK2_NATIVE_DIAGNOSTIC_RUNTIME_TRANSITION_TEST) \
		$(CDK2_NATIVE_DEADLINE_TEST) $(CDK2_NATIVE_LINEAR_BOOT_TEST) \
		$(CDK2_NATIVE_EARLY_SPLASH_TEST) \
		$(CDK2_NATIVE_DEVICE_PATH_CHECK_DEPS) \
		$(CDK2_NATIVE_PARTITION_CHECK_DEPS) \
		$(CDK2_NATIVE_PCI_HOST_BRIDGE_CHECK_DEPS) \
		$(CDK2_NATIVE_USB_MASS_CHECK_DEPS) \
		$(CDK2_NATIVE_PLATFORM_DIAGNOSTIC_CHECKS)
	@$(CDK2_NATIVE_NM) "$(CDK2_NATIVE_ELF)" | grep -Eq '[[:space:]]W[[:space:]]cdk2_platform_initialize_native_context$$'
	@$(CDK2_NATIVE_NM) "$(CDK2_NATIVE_OVERRIDE_ELF)" | grep -Eq '[[:space:]]T[[:space:]]cdk2_platform_initialize_native_context$$'
	@$(CDK2_NATIVE_NM) "$(CDK2_NATIVE_COREBOOT_ELF)" | grep -Eq '[[:space:]]T[[:space:]]cdk2_platform_initialize_native_context$$'
	@$(CDK2_NATIVE_NM) "$(CDK2_NATIVE_COREBOOT_ELF)" | grep -Eq '[[:space:]]T[[:space:]]cdk2_coreboot_entry32$$'
	@"$(CDK2_NATIVE_DIAGNOSTIC_TEST)"
	@"$(CDK2_NATIVE_DIAGNOSTIC_RUNTIME_TRANSITION_TEST)"
	@"$(CDK2_NATIVE_DEADLINE_TEST)"
	@"$(CDK2_NATIVE_LINEAR_BOOT_TEST)"
	@"$(CDK2_NATIVE_EARLY_SPLASH_TEST)"
	@"$(CDK2_DIR)/util/check-diagnostic-parity.sh" "$(CDK2_DIAGNOSTIC_PARITY_LEDGER)"
	@"$(CDK2_NATIVE_ELF_CHECK)" --entry cdk2_native_stage_entry "$(CDK2_NATIVE_ELF)"
	@"$(CDK2_NATIVE_ELF_CHECK)" --entry cdk2_coreboot_entry32 "$(CDK2_NATIVE_COREBOOT_ELF)"
	@test -s "$(CDK2_NATIVE_MAP)"
	@test -s "$(CDK2_NATIVE_COREBOOT_MAP)"
	@test -s "$(CDK2_NATIVE_OVERRIDE_MAP)"
	@"$(CDK2_NATIVE_SERVICE_TEST)"
	@"$(CDK2_NATIVE_COREBOOT_TEST)"
	@"$(CDK2_NATIVE_METRONOME_TEST)"
	@"$(CDK2_NATIVE_SCSI_BUS_TEST)"
	@"$(CDK2_NATIVE_SCSI_BINDING_TEST)"
	@"$(CDK2_NATIVE_SCSI_ENTRY_TEST)"
	@"$(CDK2_NATIVE_SATA_CONTROLLER_TEST)"
	@"$(CDK2_NATIVE_ATA_ATAPI_TEST)"
	@"$(CDK2_NATIVE_ATA_ATAPI_BINDING_TEST)"
	@"$(CDK2_NATIVE_ATA_ATAPI_AHCI_TEST)"
	@"$(CDK2_NATIVE_ATA_ATAPI_IDE_TEST)"
	@"$(CDK2_NATIVE_ATA_ATAPI_PCI_TEST)"
	@"$(CDK2_NATIVE_ATA_ATAPI_ENTRY_TEST)"
	@"$(CDK2_NATIVE_ATA_PROTOCOL_TEST)"
	@"$(CDK2_NATIVE_ATA_BACKEND_IDE_TEST)"
	@"$(CDK2_NATIVE_ATA_DEPENDENCY_TEST)" "$(MAKE)" "$(CDK2_DIR)" \
		"$(CDK2_BUILD_DIR)"
	@"$(CDK2_NATIVE_ATA_REPRODUCIBLE_TEST)" "$(MAKE)" "$(CDK2_DIR)"
	@"$(CDK2_NATIVE_ATA_BUS_DEPENDENCY_TEST)" "$(MAKE)" "$(CDK2_DIR)" \
		"$(CDK2_BUILD_DIR)"
	@"$(CDK2_NATIVE_SATA_ENTRY_TEST)"
	@"$(CDK2_NATIVE_SATA_DEPENDENCY_TEST)" "$(MAKE)" "$(CDK2_DIR)" \
		"$(CDK2_BUILD_DIR)" "$(CDK2_NATIVE_LD)"
	@"$(CDK2_NATIVE_PCI_HOST_BRIDGE_TEST)"
	@"$(CDK2_NATIVE_PCI_HOST_BRIDGE_ENTRY_TEST)"
	@"$(CDK2_NATIVE_PCI_HOST_BRIDGE_ENTRY_TEST)" --strict-hob-only
	@"$(CDK2_NATIVE_PCI_HOST_BRIDGE_ROLLBACK_MUTATION_TEST)" \
		"$(CDK2_NATIVE_HOST_CC)" "$(CDK2_NATIVE_HOST_CFLAGS)" \
		"$(CDK2_NATIVE_INCLUDES)"
	@"$(CDK2_NATIVE_PCI_BUS_ENTRY_TEST)"
	@"$(CDK2_NATIVE_PCI_ENUMERATE_TEST)"
	@"$(CDK2_NATIVE_PCI_IMMUTABLE_ENTRY_TEST)"
	@"$(CDK2_NATIVE_PCI_ROOT_IO_TEST)"
	@sh "$(CDK2_DIR)/tests/pci_host_bridge_profile_test.sh" \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_PCI_HOST_BRIDGE_PE)"
	@"$(CDK2_NATIVE_PCI_BUS_MODEL_TEST)"
	@"$(CDK2_NATIVE_WATCHDOG_TEST)"
	@"$(CDK2_NATIVE_STATUS_CODE_ROUTER_TEST)"
	@"$(CDK2_NATIVE_STATUS_CODE_HANDLER_TEST)"
	@"$(CDK2_NATIVE_GRAPHICS_CONSOLE_TEST)"
	@"$(CDK2_NATIVE_GRAPHICS_OUTPUT_TEST)"
	@"$(CDK2_NATIVE_GRAPHICS_OUTPUT_DRIVER_TEST)"
	@"$(CDK2_NATIVE_GRAPHICS_ENTRY_TEST)"
	$(if $(CDK2_NATIVE_LVGL_RENDERER_CHECK_DEPS),@"$(CDK2_NATIVE_LVGL_RENDERER_TEST)")
	$(if $(CDK2_NATIVE_LVGL_RENDERER_CHECK_DEPS),@"$(CDK2_NATIVE_LVGL_SMOKE_TEST)")
	$(if $(CDK2_NATIVE_LVGL_RENDERER_CHECK_DEPS),@"$(CDK2_NATIVE_LVGL_UI_TEST)")
	@"$(CDK2_NATIVE_PE_TEST)"
	@"$(CDK2_NATIVE_ENTRY_TEST)"
	@"$(CDK2_NATIVE_SECURITY_STUB_TEST)"
	@"$(CDK2_NATIVE_NULL_MEMORY_TEST)"
	@"$(CDK2_NATIVE_CPU_IO2_TEST)"
	@"$(CDK2_NATIVE_MONO_TEST)"
	@"$(CDK2_NATIVE_RUNTIME_ARCH_TEST)"
	@"$(CDK2_NATIVE_ENGLISH_TEST)"
	@"$(CDK2_NATIVE_DISK_IO_TEST)"
	@"$(CDK2_NATIVE_FAT_TEST)"
	@"$(CDK2_NATIVE_SERIAL_IO_TEST)"
	@"$(CDK2_NATIVE_RESET_SYSTEM_TEST)"
	@"$(CDK2_NATIVE_PCAT_RTC_TEST)"
	@"$(CDK2_NATIVE_LOCAL_APIC_TIMER_TEST)"
	@"$(CDK2_NATIVE_LOCAL_APIC_TIMER_DRIVER_TEST)"
	@"$(CDK2_NATIVE_CAPSULE_RUNTIME_TEST)"
	@"$(CDK2_NATIVE_CAPSULE_RUNTIME_ABI_TEST)"
	@"$(CDK2_NATIVE_CAPSULE_RUNTIME_ENTRY_TEST)"
	@"$(CDK2_NATIVE_TCG2_TRANSPORT_TEST)"
	@"$(CDK2_NATIVE_TCG2_COMMANDS_TEST)"
	@"$(CDK2_NATIVE_TCG2_EVENT_LOG_TEST)"
	@"$(CDK2_NATIVE_TCG2_MEASURE_TEST)"
	@"$(CDK2_NATIVE_TCG2_SERVICE_TEST)"
	@"$(CDK2_NATIVE_TCG2_ENTRY_TEST)"
	@"$(CDK2_NATIVE_TCG2_DIAGNOSTIC_COALESCE_TEST)"
	@"$(CDK2_NATIVE_TCG2_PLATFORM_HOB_TEST)"
	@"$(CDK2_NATIVE_TPM2_ACPI_TABLE_TEST)"
	@"$(CDK2_NATIVE_ESRT_TEST)"
	@"$(CDK2_NATIVE_ESRT_ENTRY_TEST)"
	@"$(CDK2_NATIVE_QEMU_TEST_FMP_TEST)"
	@"$(CDK2_NATIVE_QEMU_TEST_FMP_ENTRY_TEST)"
	@"$(CDK2_NATIVE_DEVICE_PATH_TEST)"
	@"$(CDK2_NATIVE_PARTITION_GPT_TEST)"
	@"$(CDK2_NATIVE_PARTITION_MBR_TEST)"
	@"$(CDK2_NATIVE_PARTITION_EL_TORITO_TEST)"
	@"$(CDK2_NATIVE_PARTITION_UDF_TEST)"
	@"$(CDK2_NATIVE_PARTITION_CHILD_TEST)"
	@"$(CDK2_NATIVE_PARTITION_DRIVER_TEST)"
ifeq ($(CONFIG_CDK2_NATIVE_QEMU_TEST_FMP),y)
endif
ifeq ($(CONFIG_CDK2_NATIVE_DEVICE_PATH),y)
	@"$(CDK2_NATIVE_DEVICE_PATH_DRIVER_TEST)"
endif
ifeq ($(CONFIG_CDK2_NATIVE_PARTITION),y)
endif
	@"$(CDK2_NATIVE_ELF_CHECK_TEST)" "$(CDK2_NATIVE_ELF_CHECK)" "$(CDK2_NATIVE_BUILD_DIR)"
	@printf '%s\n' "native cdk2 stage: $(CDK2_NATIVE_ELF)"
else
CDK2_NATIVE_STAGE_DEPS :=
native-stage:
	@printf '%s\n' 'native cdk2 stage: disabled by Kconfig'

native-coreboot-stage:
	@printf '%s\n' 'native cdk2 coreboot stage: disabled by Kconfig'

native-check: $(CDK2_CONFIG_HEADER) $(CDK2_NATIVE_HOST_TOOLS) \
		$(CDK2_NATIVE_USB_MASS_CHECK_DEPS)
	@"$(CDK2_NATIVE_SERVICE_TEST)"
	@"$(CDK2_NATIVE_COREBOOT_TEST)"
	@"$(CDK2_NATIVE_WATCHDOG_TEST)"
	@"$(CDK2_NATIVE_STATUS_CODE_ROUTER_TEST)"
	@"$(CDK2_NATIVE_CPU_IO2_TEST)"
	@"$(CDK2_NATIVE_STATUS_CODE_HANDLER_TEST)"
	@"$(CDK2_NATIVE_PCAT_RTC_TEST)"
	@"$(CDK2_NATIVE_GRAPHICS_OUTPUT_TEST)"
	@"$(CDK2_NATIVE_GRAPHICS_OUTPUT_DRIVER_TEST)"
	@"$(CDK2_NATIVE_PE_TEST)"
	@"$(CDK2_NATIVE_ENTRY_TEST)"
	@"$(CDK2_NATIVE_ELF_CHECK_TEST)" "$(CDK2_NATIVE_ELF_CHECK)" "$(CDK2_NATIVE_BUILD_DIR)"
	@printf '%s\n' 'native cdk2 stage: disabled by Kconfig'
endif


native-service-test: $(CDK2_NATIVE_SERVICE_TEST)
	@"$(CDK2_NATIVE_SERVICE_TEST)"

native-coreboot-test: $(CDK2_NATIVE_COREBOOT_TEST) \
	native-deadline-cbmem-mutation-test
	@sh "$(CDK2_DIR)/tests/coreboot_interrupt_ownership_test.sh" \
		"$(CDK2_DIR)/src/boot/coreboot_handoff.c"
	@"$(CDK2_NATIVE_COREBOOT_TEST)"

native-cbmem-console-test: $(CDK2_NATIVE_COREBOOT_TEST) $(CDK2_NATIVE_DIAGNOSTIC_TEST) \
		$(CDK2_NATIVE_DIAGNOSTIC_RUNTIME_TRANSITION_TEST) \
		native-diagnostic-literal-profile-test native-diagnostic-legacy-profile-test \
		native-diagnostic-core-dependency-test \
		native-diagnostic-smm-classification-test
	@"$(CDK2_NATIVE_COREBOOT_TEST)"
	@"$(CDK2_NATIVE_DIAGNOSTIC_TEST)"
	@"$(CDK2_NATIVE_DIAGNOSTIC_RUNTIME_TRANSITION_TEST)"
	@sh "$(CDK2_DIR)/tests/diagnostic_handoff_inventory_test.sh"

ifeq ($(CONFIG_CDK2_SPI_CONSOLE),y)
.PHONY: native-spi-console-budget-test
native-spi-console-budget-test: $(CDK2_NATIVE_DIAGNOSTIC_TEST) $(CDK2_CONFIG_HEADER) \
		$(CDK2_DIR)/tests/spi_console_budget_test.sh
	@sh "$(CDK2_DIR)/tests/spi_console_budget_test.sh" \
		"$(CDK2_NATIVE_HOST_CC)" "$(CDK2_DIR)" "$(CDK2_CONFIG_HEADER)"

native-cbmem-console-test: native-spi-console-budget-test
native-check: native-spi-console-budget-test
else
.PHONY: native-spi-console-budget-test
native-spi-console-budget-test:
endif

.PHONY: native-diagnostic-literal-profile-test
native-diagnostic-literal-profile-test: $(CDK2_CONFIG_HEADER) \
		$(CDK2_DIR)/tests/diagnostic_literal_profile_test.sh
	@"$(CDK2_DIR)/tests/diagnostic_literal_profile_test.sh" \
		"$(CDK2_NATIVE_HOST_CC)" "$(CDK2_NATIVE_LD)" "$(CDK2_NATIVE_NM)" \
		"$(CDK2_DIR)" "$(CDK2_CONFIG_HEADER)"

native-check: native-diagnostic-literal-profile-test

.PHONY: native-diagnostic-legacy-profile-test
native-diagnostic-legacy-profile-test: \
		$(CDK2_DIR)/tests/diagnostic_legacy_profile_test.sh
	@"$(CDK2_DIR)/tests/diagnostic_legacy_profile_test.sh" \
		"$(CDK2_NATIVE_HOST_CC)" "$(CDK2_NATIVE_NM)" "$(CDK2_DIR)"

.PHONY: native-diagnostic-core-dependency-test
native-diagnostic-core-dependency-test: \
		$(CDK2_DIR)/tests/diagnostic_core_dependency_test.sh
	@sh "$(CDK2_DIR)/tests/diagnostic_core_dependency_test.sh"

.PHONY: native-diagnostic-smm-classification-test
native-diagnostic-smm-classification-test: \
		$(CDK2_DIR)/tests/diagnostic_smm_classification_test.sh \
		$(CDK2_DIR)/tests/diagnostic_smm_classification_mutation_test.sh
	@sh "$(CDK2_DIR)/tests/diagnostic_smm_classification_mutation_test.sh"

native-check: native-diagnostic-legacy-profile-test \
		native-diagnostic-core-dependency-test \
		native-diagnostic-smm-classification-test

CDK2_NATIVE_DIAGNOSTIC_IMAGES := \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_XHCI)),$(CDK2_NATIVE_XHCI_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_UHCI)),$(CDK2_NATIVE_UHCI_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_SD_DXE)),$(CDK2_NATIVE_SD_DXE_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_VARIABLE_RUNTIME)),$(CDK2_NATIVE_VARIABLE_RUNTIME_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_GRAPHICS_OUTPUT)),$(CDK2_NATIVE_GRAPHICS_OUTPUT_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_CPU_ARCH)),$(CDK2_NATIVE_CPU_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_TCG2)),\
		$(CDK2_NATIVE_TCG2_PE) $(CDK2_NATIVE_TPM2_ACPI_TABLE_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_PARTITION)),$(CDK2_NATIVE_PARTITION_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_FAT)),$(CDK2_NATIVE_FAT_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_TERMINAL)),$(CDK2_NATIVE_TERMINAL_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_ACPI_TABLE)),$(CDK2_NATIVE_ACPI_TABLE_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_SMMSTORE_FVB)),$(CDK2_NATIVE_SMMSTORE_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_USB_MASS_STORAGE)),$(CDK2_NATIVE_USB_MASS_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_USB_BUS)),$(CDK2_NATIVE_USB_BUS_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_EMMC_DXE)),$(CDK2_NATIVE_EMMC_DXE_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_SDMMC_PCI)),$(CDK2_NATIVE_SDMMC_PCI_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_EHCI)),$(CDK2_NATIVE_EHCI_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_SECURITY_STUB)),$(CDK2_NATIVE_SECURITY_STUB_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_WATCHDOG)),$(CDK2_NATIVE_WATCHDOG_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_STATUS_CODE_HANDLER)),\
		$(CDK2_NATIVE_STATUS_CODE_HANDLER_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_CAPSULE_RUNTIME)),$(CDK2_NATIVE_CAPSULE_RUNTIME_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_GRAPHICS_CONSOLE)),$(CDK2_NATIVE_GRAPHICS_CONSOLE_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_RESET_SYSTEM)),$(CDK2_NATIVE_RESET_SYSTEM_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_SCSI_DISK)),$(CDK2_NATIVE_SCSI_DISK_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_ATA_BUS)),$(CDK2_NATIVE_ATA_BUS_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_ATA_ATAPI_PASS_THRU)),$(CDK2_NATIVE_ATA_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_SATA_CONTROLLER)),$(CDK2_NATIVE_SATA_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_NVME)),$(CDK2_NATIVE_NVME_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_PCI_BUS)),$(CDK2_NATIVE_PCI_BUS_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_PCI_HOST_BRIDGE)),$(CDK2_NATIVE_PCI_HOST_BRIDGE_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_FTW)),$(CDK2_NATIVE_FTW_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_CON_SPLITTER)),$(CDK2_NATIVE_CON_SPLITTER_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_SHELL)),$(CDK2_NATIVE_SHELL_PE)) \
	$(if $(filter y,$(CONFIG_CDK2_NATIVE_EC_BATTERY)),$(CDK2_NATIVE_EC_BATTERY_PE))

.PHONY: native-diagnostic-image-inventory
native-diagnostic-image-inventory: $(CDK2_NATIVE_DIAGNOSTIC_IMAGES)
	@sh "$(CDK2_DIR)/tests/diagnostic_handoff_inventory_test.sh" \
		--build-dir "$(CDK2_BUILD_DIR)" --config-header "$(CDK2_CONFIG_HEADER)" \
		$(foreach image,$(CDK2_NATIVE_DIAGNOSTIC_IMAGES),--image "$(image)")

native-linear-boot-test: $(CDK2_NATIVE_LINEAR_BOOT_TEST)
	@"$(CDK2_NATIVE_LINEAR_BOOT_TEST)"

native-linear-connect-ownership-test:
	@sh "$(CDK2_NATIVE_LINEAR_CONNECT_OWNERSHIP_TEST)" \
		"$(CDK2_DIR)" "$(CDK2_DIR)/src/boot/Makefile"

ifeq ($(CONFIG_CDK2_LINEAR_BOOT),y)
native-check: native-linear-connect-ownership-test
endif

native-deadline-test: $(CDK2_NATIVE_DEADLINE_TEST) \
		native-deadline-provenance-test native-deadline-tsc-isolation-test
	@"$(CDK2_NATIVE_DEADLINE_TEST)"

native-deadline-provenance-test:
	@sh "$(CDK2_DIR)/tests/deadline_provenance_mutation_test.sh"

native-deadline-cbmem-mutation-test: $(CDK2_NATIVE_COREBOOT_TEST)
	@sh "$(CDK2_DIR)/tests/deadline_cbmem_first_match_mutation_test.sh" \
		"$(CDK2_NATIVE_HOST_CC)" "$(CDK2_NATIVE_HOST_CFLAGS)" \
		"$(CDK2_NATIVE_INCLUDES)" "$(CDK2_NATIVE_HOST_LDFLAGS)"

native-deadline-tsc-isolation-test:
	@sh "$(CDK2_DIR)/tests/deadline_tsc_isolation_test.sh" "$(CDK2_DIR)"

native-early-splash-test: $(CDK2_NATIVE_EARLY_SPLASH_TEST)
	@"$(CDK2_NATIVE_EARLY_SPLASH_TEST)"


native-status-code-router-test: $(CDK2_NATIVE_STATUS_CODE_ROUTER_TEST)
	@"$(CDK2_NATIVE_STATUS_CODE_ROUTER_TEST)"

native-cpu-io2-test: $(CDK2_NATIVE_CPU_IO2_TEST)
	@"$<"

native-status-code-handler-test: $(CDK2_NATIVE_STATUS_CODE_HANDLER_TEST)
	@"$(CDK2_NATIVE_STATUS_CODE_HANDLER_TEST)"

native-disk-io-test: $(CDK2_NATIVE_DISK_IO_TEST)
	@"$<"

native-fat-test: $(CDK2_NATIVE_FAT_TEST) $(CDK2_NATIVE_BUILD_DIR)/cdk2-fat-binding-test $(CDK2_NATIVE_BUILD_DIR)/cdk2-fat-protocol-abi-test $(CDK2_NATIVE_BUILD_DIR)/cdk2-fat-entry-test native-fat-set-info-mutation-test
	@"$<"
	@"$(CDK2_NATIVE_BUILD_DIR)/cdk2-fat-binding-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/cdk2-fat-protocol-abi-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/cdk2-fat-entry-test"

.PHONY: native-fat-audit-consistency-test native-fat-set-info-mutation-test
native-fat-audit-consistency-test:
	@sh "$(CDK2_DIR)/tests/fat_audit_consistency_test.sh" "$(MAKE)" "$(CDK2_DIR)"

native-fat-set-info-mutation-test: $(CDK2_CONFIG_HEADER)
	@sh "$(CDK2_DIR)/tests/fat_set_info_mutation_test.sh" \
		"$(CDK2_BUILD_DIR)/include"


native-fat-oracle: $(CDK2_NATIVE_FAT_QEMU_PE)
	@printf '%s\n' "native Fat oracle: $<"

$(CDK2_NATIVE_BUILD_DIR)/fat-%.o: $(CDK2_DIR)/src/modules/fat/%.c \
	$(CDK2_DIR)/include/cdk2/fat_binding.h $(CDK2_DIR)/include/cdk2/fat.h \
	$(CDK2_DIR)/include/cdk2/english.h $(CDK2_DIR)/include/uefi.h \
	$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_FAT_PE): $(CDK2_NATIVE_FAT_OBJS) $(CDK2_NATIVE_BUILD_DIR)/mem.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_FAT_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/fat/fat.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_fat_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x200 --build-id=none \
		--no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/src/modules/fat/fat.ld" \
		-o "$@" $(CDK2_NATIVE_FAT_OBJS) $(CDK2_NATIVE_BUILD_DIR)/mem.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_FAT_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_FAT_QEMU_OBJ): $(CDK2_DIR)/tests/fat_qemu.c \
		$(CDK2_DIR)/include/cdk2/fat_binding.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_FAT_QEMU_PE): $(CDK2_NATIVE_FAT_QEMU_OBJ) \
		$(CDK2_DIR)/tests/fat_qemu.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry fat_qemu_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/tests/fat_qemu.ld" \
		-o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

native-serial-io-test: $(CDK2_NATIVE_SERIAL_IO_TEST)
	@"$(CDK2_NATIVE_SERIAL_IO_TEST)"

native-reset-system-test: $(CDK2_NATIVE_RESET_SYSTEM_TEST)
	@"$<"

native-pcat-rtc-test: $(CDK2_NATIVE_PCAT_RTC_TEST)
	@"$<"

native-tcg2-transport-test: $(CDK2_NATIVE_TCG2_TRANSPORT_TEST)
	@"$<"

native-tcg2-commands-test: $(CDK2_NATIVE_TCG2_COMMANDS_TEST)
	@"$<"

native-tcg2-event-log-test: $(CDK2_NATIVE_TCG2_EVENT_LOG_TEST)
	@"$<"

native-tcg2-measure-test: $(CDK2_NATIVE_TCG2_MEASURE_TEST)
	@"$<"

native-tcg2-service-test: $(CDK2_NATIVE_TCG2_SERVICE_TEST)
	@"$<"

native-tcg2-entry-test: $(CDK2_NATIVE_TCG2_ENTRY_TEST)
	@"$<"

native-software-hash-test: $(CDK2_NATIVE_SOFTWARE_HASH_TEST)
	@"$<"
	@sh "$(CDK2_DIR)/tests/software_hash_source_test.sh" "$(CDK2_DIR)"

native-tpm2-acpi-table-test: $(CDK2_NATIVE_TPM2_ACPI_TABLE_TEST)
	@"$<"

native-device-path-test: $(CDK2_NATIVE_DEVICE_PATH_TEST) \
		$(CDK2_NATIVE_DEVICE_PATH_DRIVER_TEST)
	@"$(CDK2_NATIVE_DEVICE_PATH_TEST)"
	@"$(CDK2_NATIVE_DEVICE_PATH_DRIVER_TEST)"

native-partition-test: $(CDK2_NATIVE_PARTITION_GPT_TEST) \
		$(CDK2_NATIVE_PARTITION_MBR_TEST) $(CDK2_NATIVE_PARTITION_EL_TORITO_TEST) \
		$(CDK2_NATIVE_PARTITION_UDF_TEST) $(CDK2_NATIVE_PARTITION_CHILD_TEST) \
		$(CDK2_NATIVE_PARTITION_DRIVER_TEST)
	@"$(CDK2_NATIVE_PARTITION_GPT_TEST)"
	@"$(CDK2_NATIVE_PARTITION_MBR_TEST)"
	@"$(CDK2_NATIVE_PARTITION_EL_TORITO_TEST)"
	@"$(CDK2_NATIVE_PARTITION_UDF_TEST)"
	@"$(CDK2_NATIVE_PARTITION_CHILD_TEST)"
	@"$(CDK2_NATIVE_PARTITION_DRIVER_TEST)"

ifeq ($(CONFIG_CDK2_NATIVE_TCG2),y)
.PHONY: native-tcg2-diagnostic-parity
native-tcg2-diagnostic-parity: $(CDK2_NATIVE_TCG2_PE) \
		$(CDK2_NATIVE_TCG2_DIAGNOSTIC_TEST)
	@if [ "$(CONFIG_CDK2_BUILD_DEBUG)" = y ]; then \
		"$(CDK2_NATIVE_TCG2_DIAGNOSTIC_TEST)" "$(CDK2_NATIVE_TCG2_PE)"; fi
else
.PHONY: native-tcg2-diagnostic-parity
native-tcg2-diagnostic-parity:
	@printf '%s\n' 'native TCG2 diagnostic parity: disabled by Kconfig'
endif

native-local-apic-timer-test: $(CDK2_NATIVE_LOCAL_APIC_TIMER_TEST)
	@"$<"

native-graphics-console-test: $(CDK2_NATIVE_GRAPHICS_CONSOLE_TEST) \
		$(CDK2_NATIVE_GRAPHICS_ENTRY_TEST) \
		$(CDK2_NATIVE_GRAPHICS_DIAGNOSTIC_DEBUG_TEST) \
		$(CDK2_NATIVE_GRAPHICS_DIAGNOSTIC_RELEASE_TEST)
	@sh "$(CDK2_DIR)/tests/graphics_console_hii_boundary_test.sh" "$(CDK2_DIR)"
	@"$(CDK2_NATIVE_GRAPHICS_CONSOLE_TEST)"
	@"$(CDK2_NATIVE_GRAPHICS_ENTRY_TEST)"
	@"$(CDK2_NATIVE_GRAPHICS_DIAGNOSTIC_DEBUG_TEST)"
	@"$(CDK2_NATIVE_GRAPHICS_DIAGNOSTIC_RELEASE_TEST)"


native-scsi-bus-test: $(CDK2_NATIVE_SCSI_BUS_TEST) $(CDK2_NATIVE_SCSI_BINDING_TEST) \
		$(CDK2_NATIVE_SCSI_ENTRY_TEST) $(CDK2_NATIVE_SCSI_BUS_OBJS)
	@"$(CDK2_NATIVE_SCSI_BUS_TEST)"
	@"$(CDK2_NATIVE_SCSI_BINDING_TEST)"
	@"$(CDK2_NATIVE_SCSI_ENTRY_TEST)"
	@"$(CDK2_NATIVE_SCSI_DEPENDENCY_TEST)" "$(MAKE)" "$(CDK2_DIR)" \
		"$(CDK2_BUILD_DIR)"

CDK2_NATIVE_SCSI_DISK_ASYNC_TEST := $(CDK2_NATIVE_BUILD_DIR)/scsi-disk-async-test
CDK2_NATIVE_SCSI_DISK_BLOCK_TEST := $(CDK2_NATIVE_BUILD_DIR)/scsi-disk-block-test
CDK2_NATIVE_SCSI_DISK_BINDING_TEST := $(CDK2_NATIVE_BUILD_DIR)/scsi-disk-binding-test
CDK2_NATIVE_SCSI_DISK_BACKEND_TEST := $(CDK2_NATIVE_BUILD_DIR)/scsi-disk-backend-test
CDK2_NATIVE_SCSI_DISK_ENTRY_TEST := $(CDK2_NATIVE_BUILD_DIR)/scsi-disk-entry-test
.PHONY: native-scsi-disk-test
native-scsi-disk-test: $(CDK2_NATIVE_SCSI_DISK_TEST) \
		$(CDK2_NATIVE_SCSI_DISK_IO_TEST) $(CDK2_NATIVE_SCSI_DISK_ASYNC_TEST) \
		$(CDK2_NATIVE_SCSI_DISK_BLOCK_TEST) $(CDK2_NATIVE_SCSI_DISK_BINDING_TEST)

native-scsi-disk-test: $(CDK2_NATIVE_SCSI_DISK_BACKEND_TEST)
native-scsi-disk-test: $(CDK2_NATIVE_SCSI_DISK_ENTRY_TEST)
	@"$(CDK2_NATIVE_SCSI_DISK_TEST)"
	@"$(CDK2_NATIVE_SCSI_DISK_IO_TEST)"
	@"$(CDK2_NATIVE_SCSI_DISK_ASYNC_TEST)"
	@"$(CDK2_NATIVE_SCSI_DISK_BLOCK_TEST)"
	@"$(CDK2_NATIVE_SCSI_DISK_BINDING_TEST)"
	@"$(CDK2_NATIVE_SCSI_DISK_BACKEND_TEST)"
	@"$(CDK2_NATIVE_SCSI_DISK_ENTRY_TEST)"

.PHONY: native-xhci-test
native-xhci-test: $(CDK2_NATIVE_XHCI_TEST) $(CDK2_NATIVE_XHCI_CONTROLLER_TEST) \
		$(CDK2_NATIVE_XHCI_PCI_TEST) $(CDK2_NATIVE_BUILD_DIR)/xhci-entry-test
	@"$(CDK2_NATIVE_XHCI_TEST)"
	@"$(CDK2_NATIVE_XHCI_CONTROLLER_TEST)"
	@"$(CDK2_NATIVE_XHCI_PCI_TEST)"
	@"$(CDK2_NATIVE_BUILD_DIR)/xhci-entry-test"

.PHONY: native-usb-bus-test
native-usb-bus-test: $(CDK2_NATIVE_BUILD_DIR)/usb-bus-model-test \
		$(CDK2_NATIVE_BUILD_DIR)/usb-bus-io-test \
		$(CDK2_NATIVE_BUILD_DIR)/usb-bus-enumeration-test \
		$(CDK2_NATIVE_BUILD_DIR)/usb-bus-binding-test \
		$(CDK2_NATIVE_BUILD_DIR)/usb-bus-rollback-test \
		$(CDK2_NATIVE_BUILD_DIR)/usb-bus-entry-test \
		native-usb-bus-rollback-mutation-test \
		native-usb-bus-io-mutation-test
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-bus-model-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-bus-io-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-bus-enumeration-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-bus-binding-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-bus-rollback-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-bus-entry-test"

.PHONY: native-usb-bus-rollback-mutation-test
native-usb-bus-rollback-mutation-test:
	@sh "$(CDK2_NATIVE_USB_BUS_ROLLBACK_MUTATION_TEST)" \
		"$(CDK2_NATIVE_HOST_CC)" "$(CDK2_NATIVE_HOST_CFLAGS)" \
		"$(CDK2_NATIVE_INCLUDES)"

.PHONY: native-usb-bus-io-mutation-test
native-usb-bus-io-mutation-test:
	@sh "$(CDK2_NATIVE_USB_BUS_IO_MUTATION_TEST)" \
		"$(CDK2_NATIVE_HOST_CC)" "$(CDK2_NATIVE_HOST_CFLAGS)" \
		"$(CDK2_NATIVE_INCLUDES)"

.PHONY: native-usb-mass-test
native-usb-mass-test: $(CDK2_NATIVE_BUILD_DIR)/usb-mass-model-test \
		$(CDK2_NATIVE_BUILD_DIR)/usb-mass-transport-test \
		$(CDK2_NATIVE_BUILD_DIR)/usb-mass-scsi-test \
		$(CDK2_NATIVE_BUILD_DIR)/usb-mass-block-test \
		$(CDK2_NATIVE_BUILD_DIR)/usb-mass-binding-test \
		$(CDK2_NATIVE_BUILD_DIR)/usb-mass-entry-test
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-mass-model-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-mass-transport-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-mass-scsi-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-mass-block-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-mass-binding-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-mass-entry-test"

.PHONY: native-usb-keyboard-test
native-usb-keyboard-test: $(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-model-test \
		$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-transport-test \
		$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-protocol-test \
		$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-binding-test \
		$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-entry-test
	@sh "$(CDK2_DIR)/tests/usb_keyboard_hii_boundary_test.sh" "$(CDK2_DIR)"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-model-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-transport-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-protocol-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-binding-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-entry-test"
native-check: native-usb-keyboard-test

.PHONY: native-usb-mouse-test
native-usb-mouse-test: $(CDK2_NATIVE_USB_MOUSE_TEST) \
		$(CDK2_NATIVE_USB_MOUSE_ENTRY_TEST)
	@"$(CDK2_NATIVE_USB_MOUSE_TEST)"
	@"$(CDK2_NATIVE_USB_MOUSE_ENTRY_TEST)"


.PHONY: native-sio-bus-test
native-sio-bus-test: $(CDK2_NATIVE_SIO_BUS_TEST) \
		$(CDK2_NATIVE_SIO_BUS_ENTRY_TEST)
	@"$(CDK2_NATIVE_SIO_BUS_TEST)"
	@"$(CDK2_NATIVE_SIO_BUS_ENTRY_TEST)"


$(CDK2_NATIVE_SIO_BUS_TEST): $(CDK2_DIR)/tests/sio_bus_test.c \
		$(CDK2_DIR)/src/modules/sio_bus/sio_bus.c \
		$(CDK2_DIR)/src/modules/sio_bus/binding.c \
		$(CDK2_DIR)/include/cdk2/sio_bus.h \
		$(CDK2_DIR)/include/cdk2/sio_bus_binding.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/sio_bus_test.c \
		$(CDK2_DIR)/src/modules/sio_bus/sio_bus.c \
		$(CDK2_DIR)/src/modules/sio_bus/binding.c)

$(CDK2_NATIVE_SIO_BUS_ENTRY_TEST): $(CDK2_DIR)/tests/sio_bus_entry_test.c \
		$(CDK2_DIR)/src/modules/sio_bus/entry.c \
		$(CDK2_DIR)/src/modules/sio_bus/sio_bus.c \
		$(CDK2_DIR)/src/modules/sio_bus/binding.c | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/sio_bus_entry_test.c \
		$(CDK2_DIR)/src/modules/sio_bus/sio_bus.c \
		$(CDK2_DIR)/src/modules/sio_bus/binding.c)


$(CDK2_NATIVE_BUILD_DIR)/sio-bus-%.o: \
		$(CDK2_DIR)/src/modules/sio_bus/%.c \
		$(CDK2_DIR)/include/cdk2/sio_bus.h \
		$(CDK2_DIR)/include/cdk2/sio_bus_binding.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections -Os -Oz,\
		$(CDK2_NATIVE_CFLAGS)) -Oz -fno-ident -fcf-protection=none \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_SIO_BUS_PE): $(CDK2_NATIVE_SIO_BUS_OBJS) \
		$(CDK2_DIR)/src/modules/sio_bus/sio_bus.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_sio_bus_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/sio_bus/sio_bus.ld" -o "$@" \
		$(CDK2_NATIVE_SIO_BUS_OBJS)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


.PHONY: native-ps2-mouse-test
native-ps2-mouse-test: $(CDK2_NATIVE_PS2_MOUSE_TEST) \
		$(CDK2_NATIVE_PS2_MOUSE_DRIVER_TEST)
	@"$(CDK2_NATIVE_PS2_MOUSE_TEST)"
	@"$(CDK2_NATIVE_PS2_MOUSE_DRIVER_TEST)"


$(CDK2_NATIVE_PS2_MOUSE_TEST): $(CDK2_DIR)/tests/ps2_mouse_test.c \
		$(CDK2_DIR)/src/modules/ps2_mouse/ps2_mouse.c \
		$(CDK2_DIR)/include/cdk2/ps2_mouse.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/ps2_mouse_test.c \
		$(CDK2_DIR)/src/modules/ps2_mouse/ps2_mouse.c)

$(CDK2_NATIVE_PS2_MOUSE_DRIVER_TEST): \
		$(CDK2_DIR)/tests/ps2_mouse_driver_test.c \
		$(CDK2_DIR)/src/modules/ps2_mouse/ps2_mouse.c \
		$(CDK2_DIR)/src/modules/ps2_mouse/driver.c \
		$(CDK2_DIR)/include/cdk2/ps2_mouse_driver.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/ps2_mouse_driver_test.c \
		$(CDK2_DIR)/src/modules/ps2_mouse/ps2_mouse.c \
		$(CDK2_DIR)/src/modules/ps2_mouse/driver.c)

$(CDK2_NATIVE_BUILD_DIR)/ps2-mouse-%.o: \
		$(CDK2_DIR)/src/modules/ps2_mouse/%.c \
		$(CDK2_DIR)/include/cdk2/ps2_mouse.h \
		$(CDK2_DIR)/include/cdk2/ps2_mouse_driver.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_PS2_MOUSE_PE): $(CDK2_NATIVE_PS2_MOUSE_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o \
		$(CDK2_DIR)/src/modules/ps2_mouse/ps2_mouse.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_ps2_mouse_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/ps2_mouse/ps2_mouse.ld" -o "$@" \
		$(CDK2_NATIVE_PS2_MOUSE_OBJS) $(CDK2_NATIVE_BUILD_DIR)/mem.o
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


.PHONY: native-terminal-test
native-terminal-test: $(CDK2_NATIVE_TERMINAL_TEST) \
		$(CDK2_NATIVE_TERMINAL_DRIVER_TEST)
	@"$(CDK2_NATIVE_TERMINAL_TEST)"
	@"$(CDK2_NATIVE_TERMINAL_DRIVER_TEST)"


.PHONY: native-terminal-diagnostic-parity
native-terminal-diagnostic-parity: $(CDK2_NATIVE_TERMINAL_PE) \
		$(CDK2_NATIVE_TERMINAL_DIAGNOSTIC_TEST)
	@if test "$(CONFIG_CDK2_BUILD_DEBUG)" = y; then \
		awk -F '\t' 'NR == 1 { for (i = 1; i <= NF; i++) \
			if ($$i == "native_event") found = 1; exit !found }' \
			"$(CDK2_DIR)/migration/terminal-diagnostic-parity.tsv" && \
		awk -F '\t' 'NR == 1 { for (i = 1; i <= NF; i++) \
			if ($$i == "native_event") column = i; next } \
			{ print $$column }' \
			"$(CDK2_DIR)/migration/terminal-diagnostic-parity.tsv" | \
			sort -u | while IFS= read -r event; do \
			strings -a "$(CDK2_NATIVE_TERMINAL_PE)" | \
				grep -Fq "$$event" || exit 1; \
		done; \
	fi

$(CDK2_NATIVE_TERMINAL_TEST): $(CDK2_DIR)/tests/terminal_test.c \
		$(CDK2_DIR)/src/modules/terminal/terminal.c \
		$(CDK2_DIR)/include/cdk2/terminal.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/terminal_test.c \
		$(CDK2_DIR)/src/modules/terminal/terminal.c)

$(CDK2_NATIVE_TERMINAL_DRIVER_TEST): \
		$(CDK2_DIR)/tests/terminal_driver_test.c \
		$(CDK2_DIR)/src/modules/terminal/terminal.c \
		$(CDK2_DIR)/src/modules/terminal/driver.c \
		$(CDK2_DIR)/include/cdk2/terminal_driver.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/terminal_driver_test.c \
		$(CDK2_DIR)/src/modules/terminal/terminal.c \
		$(CDK2_DIR)/src/modules/terminal/driver.c)

$(CDK2_NATIVE_BUILD_DIR)/terminal-%.o: \
		$(CDK2_DIR)/src/modules/terminal/%.c \
		$(CDK2_DIR)/include/cdk2/terminal.h \
		$(CDK2_DIR)/include/cdk2/terminal_driver.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		-fcf-protection=none -maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/terminal-diagnostic-core.o: \
		$(CDK2_DIR)/src/lib/diagnostic.c $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_TERMINAL_PE): $(CDK2_NATIVE_TERMINAL_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_TERMINAL_DIAG_CORE)) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o \
		$(CDK2_DIR)/src/modules/terminal/terminal.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_terminal_driver_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/terminal/terminal.ld" -o "$@" \
		$(CDK2_NATIVE_TERMINAL_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_TERMINAL_DIAG_CORE)) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_TERMINAL_DIAGNOSTIC_TEST): \
		$(CDK2_DIR)/tests/terminal_diagnostic_test.c | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,,\
		"$<")

.PHONY: native-graphics-output-test \
	native-graphics-output-diagnostic-parity
native-graphics-output-test: $(CDK2_NATIVE_GRAPHICS_OUTPUT_TEST) \
		$(CDK2_NATIVE_GRAPHICS_OUTPUT_DRIVER_TEST)
	@"$(CDK2_NATIVE_GRAPHICS_OUTPUT_TEST)"
	@"$(CDK2_NATIVE_GRAPHICS_OUTPUT_DRIVER_TEST)"


native-graphics-output-diagnostic-parity: $(CDK2_NATIVE_GRAPHICS_OUTPUT_PE)
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/graphics-output-diagnostic-parity.tsv" 15 \
		"$(CDK2_DIR)/src/modules/graphics_output"

$(CDK2_NATIVE_GRAPHICS_OUTPUT_TEST): \
		$(CDK2_DIR)/tests/graphics_output_test.c \
		$(CDK2_DIR)/src/modules/graphics_output/graphics_output.c \
		$(CDK2_DIR)/include/cdk2/graphics_output.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/graphics_output_test.c \
		$(CDK2_DIR)/src/modules/graphics_output/graphics_output.c)

$(CDK2_NATIVE_GRAPHICS_OUTPUT_DRIVER_TEST): \
		$(CDK2_DIR)/tests/graphics_output_driver_test.c \
		$(CDK2_DIR)/src/modules/graphics_output/graphics_output.c \
		$(CDK2_DIR)/src/modules/graphics_output/driver.c \
		$(CDK2_DIR)/include/cdk2/graphics_output_driver.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/graphics_output_driver_test.c \
		$(CDK2_DIR)/src/modules/graphics_output/graphics_output.c \
		$(CDK2_DIR)/src/modules/graphics_output/driver.c)

$(CDK2_NATIVE_BUILD_DIR)/graphics-output-%.o: \
		$(CDK2_DIR)/src/modules/graphics_output/%.c \
		$(CDK2_DIR)/include/cdk2/graphics_output.h \
		$(CDK2_DIR)/include/cdk2/graphics_output_driver.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_GRAPHICS_OUTPUT_PE): $(CDK2_NATIVE_GRAPHICS_OUTPUT_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o \
		$(CDK2_DIR)/src/modules/graphics_output/graphics_output.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_graphics_output_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/graphics_output/graphics_output.ld" \
		-o "$@" $(CDK2_NATIVE_GRAPHICS_OUTPUT_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


.PHONY: native-acpi-table-test
native-acpi-table-test: $(CDK2_NATIVE_ACPI_TABLE_TEST) \
		$(CDK2_NATIVE_ACPI_TABLE_DRIVER_TEST)
	@"$(CDK2_NATIVE_ACPI_TABLE_TEST)"
	@"$(CDK2_NATIVE_ACPI_TABLE_DRIVER_TEST)"


native-check: native-acpi-table-test

.PHONY: native-acpi-table-diagnostic-parity
native-acpi-table-diagnostic-parity: $(CDK2_NATIVE_ACPI_TABLE_PE) \
		$(CDK2_NATIVE_ACPI_TABLE_DIAGNOSTIC_TEST)
	@if [ "$(CONFIG_CDK2_BUILD_DEBUG)" = y ]; then \
		"$(CDK2_NATIVE_ACPI_TABLE_DIAGNOSTIC_TEST)" "$(CDK2_NATIVE_ACPI_TABLE_PE)"; fi

$(CDK2_NATIVE_ACPI_TABLE_TEST): $(CDK2_DIR)/tests/acpi_table_test.c \
		$(CDK2_DIR)/src/modules/acpi_table/acpi_table.c \
		$(CDK2_DIR)/include/cdk2/acpi_table.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/acpi_table_test.c \
		$(CDK2_DIR)/src/modules/acpi_table/acpi_table.c)

$(CDK2_NATIVE_ACPI_TABLE_DRIVER_TEST): \
		$(CDK2_DIR)/tests/acpi_table_driver_test.c \
		$(CDK2_DIR)/src/modules/acpi_table/acpi_table.c \
		$(CDK2_DIR)/src/modules/acpi_table/driver.c \
		$(CDK2_DIR)/include/cdk2/acpi_table.h \
		$(CDK2_DIR)/include/cdk2/hob.h \
		$(CDK2_DIR)/include/pi/hob.h \
		$(CDK2_DIR)/include/cdk2/hob_payload.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/acpi_table_driver_test.c \
		$(CDK2_DIR)/src/modules/acpi_table/acpi_table.c \
		$(CDK2_DIR)/src/modules/acpi_table/driver.c)

$(CDK2_NATIVE_BUILD_DIR)/acpi-table-%.o: \
		$(CDK2_DIR)/src/modules/acpi_table/%.c \
		$(CDK2_DIR)/include/cdk2/acpi_table.h $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/acpi-table-diagnostic-core.o: \
		$(CDK2_DIR)/src/lib/diagnostic.c $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_ACPI_TABLE_PE): $(CDK2_NATIVE_ACPI_TABLE_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_ACPI_TABLE_DIAG_CORE)) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o \
		$(CDK2_DIR)/src/modules/acpi_table/acpi_table.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_acpi_table_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/acpi_table/acpi_table.ld" -o "$@" \
		$(CDK2_NATIVE_ACPI_TABLE_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_ACPI_TABLE_DIAG_CORE)) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_ACPI_TABLE_DIAGNOSTIC_TEST): \
		$(CDK2_DIR)/tests/acpi_table_diagnostic_test.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -o "$@" "$<"

.PHONY: native-smmstore-test
native-smmstore-test: $(addprefix $(CDK2_NATIVE_BUILD_DIR)/,\
	smmstore-test smmstore-variable-test smmstore-fvb-test smmstore-driver-test) \
		$(CDK2_NATIVE_SMMSTORE_DIAGNOSTIC_DEBUG_TEST) \
		$(CDK2_NATIVE_SMMSTORE_DIAGNOSTIC_RELEASE_TEST)
	@"$(CDK2_NATIVE_BUILD_DIR)/smmstore-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/smmstore-variable-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/smmstore-fvb-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/smmstore-driver-test"
	@"$(CDK2_NATIVE_SMMSTORE_DIAGNOSTIC_DEBUG_TEST)"
	@"$(CDK2_NATIVE_SMMSTORE_DIAGNOSTIC_RELEASE_TEST)"


.PHONY: native-smmstore-diagnostic-parity
native-smmstore-diagnostic-parity: $(CDK2_NATIVE_SMMSTORE_PE)
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/smmstore-diagnostic-parity.tsv" 35 \
		"$(CDK2_DIR)/src/modules/smmstore_fvb"

native-check: native-smmstore-diagnostic-parity

$(CDK2_NATIVE_BUILD_DIR)/smmstore-test: $(CDK2_DIR)/tests/smmstore_test.c \
		$(CDK2_DIR)/src/modules/smmstore_fvb/smmstore.c \
		$(CDK2_DIR)/include/cdk2/smmstore_fvb.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-fsanitize=address,undefined $(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$@.d" -MT "$@" -o "$@" "$<" \
		$(CDK2_DIR)/src/modules/smmstore_fvb/smmstore.c

$(CDK2_NATIVE_BUILD_DIR)/smmstore-variable-test: \
		$(CDK2_DIR)/tests/variable_store_test.c \
		$(CDK2_DIR)/src/modules/smmstore_fvb/smmstore.c \
		$(CDK2_DIR)/src/modules/smmstore_fvb/variable_store.c \
		$(CDK2_DIR)/include/cdk2/smmstore_fvb.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-fsanitize=address,undefined $(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$@.d" -MT "$@" -o "$@" "$<" \
		$(CDK2_DIR)/src/modules/smmstore_fvb/smmstore.c \
		$(CDK2_DIR)/src/modules/smmstore_fvb/variable_store.c

$(CDK2_NATIVE_BUILD_DIR)/smmstore-fvb-test: \
		$(CDK2_DIR)/tests/smmstore_fvb_test.c \
		$(CDK2_DIR)/src/modules/smmstore_fvb/smmstore.c \
		$(CDK2_DIR)/src/modules/smmstore_fvb/fvb.c \
		$(CDK2_DIR)/include/cdk2/smmstore_fvb.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-fsanitize=address,undefined $(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$@.d" -MT "$@" -o "$@" "$<" \
		$(CDK2_DIR)/src/modules/smmstore_fvb/smmstore.c \
		$(CDK2_DIR)/src/modules/smmstore_fvb/fvb.c

$(CDK2_NATIVE_BUILD_DIR)/smmstore-driver-test: \
		$(CDK2_DIR)/tests/smmstore_driver_test.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(addprefix $(CDK2_DIR)/src/modules/smmstore_fvb/,\
		smmstore.c variable_store.c fvb.c driver.c) \
		$(CDK2_DIR)/include/cdk2/smmstore_fvb.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-fsanitize=address,undefined $(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$@.d" -MT "$@" -o "$@" "$<" \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(addprefix $(CDK2_DIR)/src/modules/smmstore_fvb/,\
		smmstore.c variable_store.c fvb.c driver.c)

$(CDK2_NATIVE_BUILD_DIR)/smmstore-diagnostic-debug-test: \
		$(CDK2_DIR)/tests/smmstore_diagnostic_test.c \
		$(CDK2_DIR)/src/modules/smmstore_fvb/diagnostic.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-fsanitize=address,undefined -DCDK2_DEBUG \
		-DCDK2_SMMSTORE_DIAG_TEST $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $^

$(CDK2_NATIVE_BUILD_DIR)/smmstore-diagnostic-release-test: \
		$(CDK2_DIR)/tests/smmstore_diagnostic_test.c \
		$(CDK2_DIR)/src/modules/smmstore_fvb/diagnostic.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-fsanitize=address,undefined -DCDK2_DIAGNOSTIC \
		-DCDK2_SMMSTORE_DIAG_TEST $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $^

$(CDK2_NATIVE_BUILD_DIR)/smmstore-%.o: \
		$(CDK2_DIR)/src/modules/smmstore_fvb/%.c \
		$(CDK2_DIR)/include/cdk2/smmstore_fvb.h $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_SMMSTORE_PE): $(CDK2_NATIVE_SMMSTORE_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o \
		$(CDK2_DIR)/src/modules/smmstore_fvb/smmstore_fvb.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 12 --entry cdk2_smmstore_fvb_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/smmstore_fvb/smmstore_fvb.ld" -o "$@" \
		$(CDK2_NATIVE_SMMSTORE_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 12 "$@"


$(CDK2_NATIVE_USB_MOUSE_TEST): $(CDK2_DIR)/tests/usb_mouse_test.c \
		$(CDK2_DIR)/src/modules/usb_mouse/model.c \
		$(CDK2_DIR)/src/modules/usb_mouse/binding.c \
		$(CDK2_DIR)/include/cdk2/usb_mouse.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_mouse_test.c \
		$(CDK2_DIR)/src/modules/usb_mouse/model.c \
		$(CDK2_DIR)/src/modules/usb_mouse/binding.c)

$(CDK2_NATIVE_USB_MOUSE_ENTRY_TEST): \
		$(CDK2_DIR)/tests/usb_mouse_entry_test.c \
		$(CDK2_DIR)/src/modules/usb_mouse/model.c \
		$(CDK2_DIR)/src/modules/usb_mouse/binding.c \
		$(CDK2_DIR)/src/modules/usb_mouse/entry.c \
		$(CDK2_DIR)/include/cdk2/usb_mouse.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_mouse_entry_test.c \
		$(CDK2_DIR)/src/modules/usb_mouse/model.c \
		$(CDK2_DIR)/src/modules/usb_mouse/binding.c \
		$(CDK2_DIR)/src/modules/usb_mouse/entry.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-mouse-%.o: \
		$(CDK2_DIR)/src/modules/usb_mouse/%.c \
		$(CDK2_DIR)/include/cdk2/usb_mouse.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_USB_BUS_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -ffunction-sections -fdata-sections \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_USB_MOUSE_PE): $(CDK2_NATIVE_USB_MOUSE_OBJS) \
		$(CDK2_DIR)/src/modules/usb_mouse/usb_mouse.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_usb_mouse_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/usb_mouse/usb_mouse.ld" -o "$@" \
		$(CDK2_NATIVE_USB_MOUSE_OBJS)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-%.o: \
		$(CDK2_DIR)/src/modules/usb_keyboard/%.c \
		$(CDK2_DIR)/include/cdk2/usb_keyboard.h \
		$(CDK2_DIR)/include/cdk2/usb_bus.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_USB_MASS_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_USB_KEYBOARD_PE): $(CDK2_NATIVE_USB_KEYBOARD_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(CDK2_DIR)/src/modules/xhci/xhci.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_usb_keyboard_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/xhci/xhci.ld" -o "$@" \
		$(CDK2_NATIVE_USB_KEYBOARD_OBJS) $(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-model-test: \
		$(CDK2_DIR)/tests/usb_keyboard_model_test.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/model.c \
		$(CDK2_DIR)/include/cdk2/usb_keyboard.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_keyboard_model_test.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/model.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-transport-test: \
		$(CDK2_DIR)/tests/usb_keyboard_transport_test.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/model.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/transport.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/protocol.c \
		$(CDK2_DIR)/include/cdk2/usb_keyboard.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_keyboard_transport_test.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/model.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/transport.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/protocol.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-protocol-test: \
		$(CDK2_DIR)/tests/usb_keyboard_protocol_test.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/model.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/transport.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/protocol.c \
		$(CDK2_DIR)/include/cdk2/usb_keyboard.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_keyboard_protocol_test.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/model.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/transport.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/protocol.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-binding-test: \
		$(CDK2_DIR)/tests/usb_keyboard_binding_test.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/model.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/transport.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/protocol.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/binding.c \
		$(CDK2_DIR)/include/cdk2/usb_keyboard.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_keyboard_binding_test.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/model.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/transport.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/protocol.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/binding.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-keyboard-entry-test: \
		$(CDK2_DIR)/tests/usb_keyboard_entry_test.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/model.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/transport.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/protocol.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/binding.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/entry.c \
		$(CDK2_DIR)/include/cdk2/usb_keyboard.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_keyboard_entry_test.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/model.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/transport.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/protocol.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/binding.c)
$(CDK2_NATIVE_USB_MASS_QEMU_OBJ): $(CDK2_DIR)/tests/usb_mass_qemu.c \
		$(CDK2_DIR)/include/cdk2/partition.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) \
		$(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_USB_MASS_QEMU_PE): $(CDK2_NATIVE_USB_MASS_QEMU_OBJ) \
		$(CDK2_DIR)/tests/usb_mass_qemu.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry usb_mass_qemu_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/usb_mass_qemu.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

.PHONY: native-usb-mass-qemu-oracle
native-usb-mass-qemu-oracle: $(CDK2_NATIVE_USB_MASS_QEMU_PE)
	@printf '%s\n' "native USB mass-storage QEMU oracle: $<"

$(CDK2_NATIVE_BUILD_DIR)/usb-mass-%.o: \
		$(CDK2_DIR)/src/modules/usb_mass/%.c \
		$(CDK2_DIR)/include/cdk2/usb_mass.h \
		$(CDK2_DIR)/include/cdk2/usb_bus.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_SCSI_DISK_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_USB_MASS_PE): $(CDK2_NATIVE_USB_MASS_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_USB_MASS_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/xhci/xhci.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_usb_mass_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/xhci/xhci.ld" \
		-o "$@" $(CDK2_NATIVE_USB_MASS_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_USB_MASS_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_BUILD_DIR)/usb-mass-entry-test: \
		$(CDK2_DIR)/tests/usb_mass_entry_test.c \
		$(CDK2_DIR)/src/modules/usb_mass/model.c \
		$(CDK2_DIR)/src/modules/usb_mass/transport.c \
		$(CDK2_DIR)/src/modules/usb_mass/scsi.c \
		$(CDK2_DIR)/src/modules/usb_mass/block.c \
		$(CDK2_DIR)/src/modules/usb_mass/binding.c \
		$(CDK2_DIR)/src/modules/usb_mass/entry.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/usb_mass.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_mass_entry_test.c \
		$(CDK2_DIR)/src/modules/usb_mass/model.c \
		$(CDK2_DIR)/src/modules/usb_mass/transport.c \
		$(CDK2_DIR)/src/modules/usb_mass/scsi.c \
		$(CDK2_DIR)/src/modules/usb_mass/block.c \
		$(CDK2_DIR)/src/modules/usb_mass/binding.c \
		$(CDK2_DIR)/src/lib/diagnostic.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-mass-binding-test: \
		$(CDK2_DIR)/tests/usb_mass_binding_test.c \
		$(CDK2_DIR)/src/modules/usb_mass/model.c \
		$(CDK2_DIR)/src/modules/usb_mass/transport.c \
		$(CDK2_DIR)/src/modules/usb_mass/scsi.c \
		$(CDK2_DIR)/src/modules/usb_mass/block.c \
		$(CDK2_DIR)/src/modules/usb_mass/binding.c \
		$(CDK2_DIR)/include/cdk2/usb_mass.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_mass_binding_test.c \
		$(CDK2_DIR)/src/modules/usb_mass/model.c \
		$(CDK2_DIR)/src/modules/usb_mass/transport.c \
		$(CDK2_DIR)/src/modules/usb_mass/scsi.c \
		$(CDK2_DIR)/src/modules/usb_mass/block.c \
		$(CDK2_DIR)/src/modules/usb_mass/binding.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-mass-block-test: \
		$(CDK2_DIR)/tests/usb_mass_block_test.c \
		$(CDK2_DIR)/src/modules/usb_mass/model.c \
		$(CDK2_DIR)/src/modules/usb_mass/transport.c \
		$(CDK2_DIR)/src/modules/usb_mass/scsi.c \
		$(CDK2_DIR)/src/modules/usb_mass/block.c \
		$(CDK2_DIR)/include/cdk2/usb_mass.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_mass_block_test.c \
		$(CDK2_DIR)/src/modules/usb_mass/model.c \
		$(CDK2_DIR)/src/modules/usb_mass/transport.c \
		$(CDK2_DIR)/src/modules/usb_mass/scsi.c \
		$(CDK2_DIR)/src/modules/usb_mass/block.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-mass-scsi-test: \
		$(CDK2_DIR)/tests/usb_mass_scsi_test.c \
		$(CDK2_DIR)/src/modules/usb_mass/model.c \
		$(CDK2_DIR)/src/modules/usb_mass/transport.c \
		$(CDK2_DIR)/src/modules/usb_mass/scsi.c \
		$(CDK2_DIR)/include/cdk2/usb_mass.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_mass_scsi_test.c \
		$(CDK2_DIR)/src/modules/usb_mass/model.c \
		$(CDK2_DIR)/src/modules/usb_mass/transport.c \
		$(CDK2_DIR)/src/modules/usb_mass/scsi.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-mass-transport-test: \
		$(CDK2_DIR)/tests/usb_mass_transport_test.c \
		$(CDK2_DIR)/src/modules/usb_mass/model.c \
		$(CDK2_DIR)/src/modules/usb_mass/transport.c \
		$(CDK2_DIR)/include/cdk2/usb_mass.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_mass_transport_test.c \
		$(CDK2_DIR)/src/modules/usb_mass/model.c \
		$(CDK2_DIR)/src/modules/usb_mass/transport.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-mass-model-test: \
		$(CDK2_DIR)/tests/usb_mass_model_test.c \
		$(CDK2_DIR)/src/modules/usb_mass/model.c \
		$(CDK2_DIR)/include/cdk2/usb_mass.h \
		$(CDK2_DIR)/include/cdk2/usb_bus.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_mass_model_test.c \
		$(CDK2_DIR)/src/modules/usb_mass/model.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-bus-entry-test: \
		$(CDK2_DIR)/tests/usb_bus_entry_test.c \
		$(CDK2_DIR)/src/modules/usb_bus/model.c \
		$(CDK2_DIR)/src/modules/usb_bus/usb_io.c \
		$(CDK2_DIR)/src/modules/usb_bus/bus.c \
		$(CDK2_DIR)/src/modules/usb_bus/binding.c \
		$(CDK2_DIR)/src/modules/usb_bus/entry.c \
		$(CDK2_DIR)/include/cdk2/usb_bus.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_bus_entry_test.c \
		$(CDK2_DIR)/src/modules/usb_bus/model.c \
		$(CDK2_DIR)/src/modules/usb_bus/usb_io.c \
		$(CDK2_DIR)/src/modules/usb_bus/bus.c \
		$(CDK2_DIR)/src/modules/usb_bus/binding.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-bus-binding-test: \
		$(CDK2_DIR)/tests/usb_bus_binding_test.c \
		$(CDK2_DIR)/src/modules/usb_bus/model.c \
		$(CDK2_DIR)/src/modules/usb_bus/usb_io.c \
		$(CDK2_DIR)/src/modules/usb_bus/bus.c \
		$(CDK2_DIR)/src/modules/usb_bus/binding.c \
		$(CDK2_DIR)/include/cdk2/usb_bus.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_bus_binding_test.c \
		$(CDK2_DIR)/src/modules/usb_bus/model.c \
		$(CDK2_DIR)/src/modules/usb_bus/usb_io.c \
		$(CDK2_DIR)/src/modules/usb_bus/bus.c \
		$(CDK2_DIR)/src/modules/usb_bus/binding.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-bus-rollback-test: \
		$(CDK2_DIR)/tests/usb_bus_rollback_test.c \
		$(CDK2_DIR)/src/modules/usb_bus/model.c \
		$(CDK2_DIR)/src/modules/usb_bus/usb_io.c \
		$(CDK2_DIR)/src/modules/usb_bus/bus.c \
		$(CDK2_DIR)/src/modules/usb_bus/binding.c \
		$(CDK2_DIR)/include/cdk2/usb_bus.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$@.d" -MT "$@" \
		-o "$@" $(CDK2_DIR)/tests/usb_bus_rollback_test.c \
		$(CDK2_DIR)/src/modules/usb_bus/model.c \
		$(CDK2_DIR)/src/modules/usb_bus/usb_io.c \
		$(CDK2_DIR)/src/modules/usb_bus/bus.c \
		$(CDK2_DIR)/src/modules/usb_bus/binding.c

$(CDK2_NATIVE_BUILD_DIR)/usb-bus-model-test: \
		$(CDK2_DIR)/tests/usb_bus_model_test.c \
		$(CDK2_DIR)/src/modules/usb_bus/model.c \
		$(CDK2_DIR)/include/cdk2/usb_bus.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_bus_model_test.c \
		$(CDK2_DIR)/src/modules/usb_bus/model.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-bus-io-test: \
		$(CDK2_DIR)/tests/usb_bus_io_test.c \
		$(CDK2_DIR)/src/modules/usb_bus/model.c \
		$(CDK2_DIR)/src/modules/usb_bus/usb_io.c \
		$(CDK2_DIR)/include/cdk2/usb_bus.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_bus_io_test.c \
		$(CDK2_DIR)/src/modules/usb_bus/model.c \
		$(CDK2_DIR)/src/modules/usb_bus/usb_io.c)

$(CDK2_NATIVE_BUILD_DIR)/usb-bus-enumeration-test: \
		$(CDK2_DIR)/tests/usb_bus_enumeration_test.c \
		$(CDK2_DIR)/src/modules/usb_bus/model.c \
		$(CDK2_DIR)/src/modules/usb_bus/usb_io.c \
		$(CDK2_DIR)/src/modules/usb_bus/bus.c \
		$(CDK2_DIR)/include/cdk2/usb_bus.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_SANITIZERS),\
		$(CDK2_DIR)/tests/usb_bus_enumeration_test.c \
		$(CDK2_DIR)/src/modules/usb_bus/model.c \
		$(CDK2_DIR)/src/modules/usb_bus/usb_io.c \
		$(CDK2_DIR)/src/modules/usb_bus/bus.c)
$(CDK2_NATIVE_BUILD_DIR)/usb-bus-%.o: \
		$(CDK2_DIR)/src/modules/usb_bus/%.c \
		$(CDK2_DIR)/include/cdk2/usb_bus.h \
		$(CDK2_DIR)/include/cdk2/xhci.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_USB_BUS_PE): $(CDK2_NATIVE_USB_BUS_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_USB_BUS_DIAG_CORE)) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o \
		$(CDK2_DIR)/src/modules/xhci/xhci.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_usb_bus_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/xhci/xhci.ld" \
		-o "$@" $(CDK2_NATIVE_USB_BUS_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_USB_BUS_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"
.PHONY: native-xhci-diagnostic-parity
native-xhci-diagnostic-parity: $(CDK2_NATIVE_XHCI_PE)
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/xhci-diagnostic-parity.tsv" 127 \
		"$(CDK2_DIR)/src/modules/xhci"

native-check: native-xhci-diagnostic-parity

$(CDK2_NATIVE_BUILD_DIR)/xhci-entry-test: $(CDK2_DIR)/tests/xhci_entry_test.c \
		$(CDK2_DIR)/src/modules/xhci/model.c \
		$(CDK2_DIR)/src/modules/xhci/controller.c \
		$(CDK2_DIR)/src/modules/xhci/pci_adapter.c \
		$(CDK2_DIR)/src/modules/xhci/usb2_abi.c \
		$(CDK2_DIR)/src/modules/xhci/diagnostic.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/src/lib/deadline.c \
		$(CDK2_DIR)/src/modules/xhci/entry.c $(CDK2_DIR)/include/cdk2/xhci.h \
		$(CDK2_DIR)/include/cdk2/xhci_control.h $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		-DCDK2_DEBUG $(CDK2_NATIVE_INCLUDES) -o "$@" \
		$(CDK2_DIR)/tests/xhci_entry_test.c \
		$(CDK2_DIR)/src/modules/xhci/model.c \
		$(CDK2_DIR)/src/modules/xhci/controller.c \
		$(CDK2_DIR)/src/modules/xhci/pci_adapter.c \
		$(CDK2_DIR)/src/modules/xhci/usb2_abi.c \
		$(CDK2_DIR)/src/modules/xhci/diagnostic.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/src/lib/deadline.c

$(CDK2_NATIVE_XHCI_TEST): $(CDK2_DIR)/tests/xhci_model_test.c \
		$(CDK2_DIR)/src/modules/xhci/model.c $(CDK2_DIR)/include/cdk2/xhci.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/xhci_model_test.c \
		$(CDK2_DIR)/src/modules/xhci/model.c

$(CDK2_NATIVE_XHCI_CONTROLLER_TEST): $(CDK2_DIR)/tests/xhci_controller_test.c \
		$(CDK2_DIR)/src/modules/xhci/model.c \
		$(CDK2_DIR)/src/modules/xhci/controller.c \
		$(CDK2_DIR)/src/modules/xhci/usb2_abi.c \
		$(CDK2_DIR)/src/lib/deadline.c \
		$(CDK2_DIR)/include/cdk2/xhci.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/xhci_controller_test.c \
		$(CDK2_DIR)/src/modules/xhci/model.c \
		$(CDK2_DIR)/src/modules/xhci/controller.c \
		$(CDK2_DIR)/src/modules/xhci/usb2_abi.c \
		$(CDK2_DIR)/src/lib/deadline.c

$(CDK2_NATIVE_XHCI_PCI_TEST): $(CDK2_DIR)/tests/xhci_pci_test.c \
		$(CDK2_DIR)/src/modules/xhci/pci_adapter.c \
		$(CDK2_DIR)/src/lib/deadline.c \
		$(CDK2_DIR)/include/cdk2/xhci.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/xhci_pci_test.c \
		$(CDK2_DIR)/src/modules/xhci/pci_adapter.c \
		$(CDK2_DIR)/src/lib/deadline.c

$(CDK2_NATIVE_BUILD_DIR)/xhci-%.o: $(CDK2_DIR)/src/modules/xhci/%.c \
		$(CDK2_DIR)/include/cdk2/xhci.h \
		$(CDK2_DIR)/include/cdk2/pci_io_abi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/xhci-deadline.o: $(CDK2_DIR)/src/lib/deadline.c \
		$(CDK2_DIR)/include/cdk2/deadline.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_XHCI_PE): $(CDK2_NATIVE_XHCI_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/xhci/xhci.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_xhci_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/xhci/xhci.ld" \
		-o "$@" $(CDK2_NATIVE_XHCI_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"
$(CDK2_NATIVE_SCSI_DISK_QEMU_OBJ): $(CDK2_DIR)/tests/scsi_disk_qemu.c \
		$(CDK2_DIR)/include/cdk2/scsi_disk.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_SCSI_DISK_QEMU_PE): $(CDK2_NATIVE_SCSI_DISK_QEMU_OBJ) \
		$(CDK2_DIR)/tests/ata_atapi_qemu.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry scsi_disk_qemu_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/ata_atapi_qemu.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

.PHONY: native-scsi-disk-oracle
native-scsi-disk-oracle: $(CDK2_NATIVE_SCSI_DISK_QEMU_PE)


.PHONY: native-scsi-bus-oracle

$(CDK2_NATIVE_LVGL_RENDERER_TEST): \
		$(CDK2_DIR)/tests/lvgl_renderer_test.c \
		$(CDK2_DIR)/src/modules/lvgl_renderer/model.c \
		$(CDK2_DIR)/src/modules/lvgl_renderer/uefi_port.c \
		$(CDK2_DIR)/include/cdk2/lvgl_renderer.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_LDFLAGS),\
		"$(CDK2_DIR)/tests/lvgl_renderer_test.c" \
		"$(CDK2_DIR)/src/modules/lvgl_renderer/model.c" \
		"$(CDK2_DIR)/src/modules/lvgl_renderer/uefi_port.c")

native-lvgl-renderer-test: lvgl-dependency-provenance-test $(CDK2_NATIVE_LVGL_RENDERER_TEST) \
		$(CDK2_NATIVE_LVGL_SMOKE_TEST) $(CDK2_NATIVE_LVGL_UI_TEST) \
		$(CDK2_NATIVE_LVGL_UI_PE)
	@"$(CDK2_NATIVE_LVGL_RENDERER_TEST)"
	@"$(CDK2_NATIVE_LVGL_SMOKE_TEST)"
	@"$(CDK2_NATIVE_LVGL_UI_TEST)"
	@sh "$(CDK2_DIR)/tests/lvgl_session_restore_mutants_test.sh" \
		"$(CDK2_NATIVE_BUILD_DIR)" "$(CDK2_CONFIG_HEADER)" \
		"$(CDK2_LVGL_ROOT)" "$(CDK2_LVGL_CONFIG)" "$(CDK2_LVGL_LIBRARY)"
	@sh "$(CDK2_DIR)/tests/splash_status_contract_test.sh" \
		"$(CDK2_DIR)/src/modules/dxe_core/entry.c" \
		"$(CDK2_DIR)/src/modules/lvgl_renderer/driver.c" \
		"$(CDK2_DIR)/include/cdk2/lvgl_ui.h"

.PHONY: native-lvgl-settings-test
native-lvgl-settings-test: $(CDK2_NATIVE_LVGL_SETTINGS_TEST)
	@"$(CDK2_NATIVE_LVGL_SETTINGS_TEST)"
	@sh "$(CDK2_DIR)/tests/lvgl_settings_source_contract_test.sh" \
		"$(CDK2_DIR)/src/modules/lvgl_renderer/driver.c"

native-check: native-lvgl-settings-test

$(CDK2_NATIVE_LVGL_SETTINGS_TEST): $(CDK2_DIR)/tests/lvgl_settings_transaction_test.c \
		$(CDK2_DIR)/src/modules/lvgl_setup/settings.c \
		$(CDK2_DIR)/src/modules/lvgl_setup/form.c \
		$(CDK2_DIR)/include/cdk2/lvgl_settings_form.h \
		$(CDK2_DIR)/include/cdk2/lvgl_settings.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" "$<" "$(CDK2_DIR)/src/modules/lvgl_setup/settings.c" \
		"$(CDK2_DIR)/src/modules/lvgl_setup/form.c"

$(CDK2_LVGL_CONFIG): $(CDK2_LVGL_CONFIG_TEMPLATE)
	@mkdir -p "$(dir $@)"
	@cp "$<" "$@"

$(CDK2_NATIVE_BUILD_DIR)/lvgl/%.o: $(CDK2_LVGL_ROOT)/src/%.c \
		$(CDK2_LVGL_CONFIG) $(CDK2_DIR)/src/boot/Makefile
	@mkdir -p "$(dir $@)"
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		-DLV_CONF_PATH='"$(CDK2_LVGL_CONFIG)"' -I"$(CDK2_LVGL_ROOT)" \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_LVGL_LIBRARY): $(CDK2_LVGL_OBJECTS)
	@rm -f "$@"
	@ar rcs "$@" $(CDK2_LVGL_OBJECTS)

$(CDK2_NATIVE_LVGL_SMOKE_TEST): $(CDK2_DIR)/tests/lvgl_smoke_test.c \
		$(CDK2_LVGL_LIBRARY) $(CDK2_LVGL_CONFIG)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-DLV_CONF_PATH='"$(CDK2_LVGL_CONFIG)"' -I"$(CDK2_LVGL_ROOT)" \
		-no-pie -o "$@" "$<" "$(CDK2_LVGL_LIBRARY)"

$(CDK2_NATIVE_LVGL_UI_TEST): $(CDK2_DIR)/tests/lvgl_ui_driver_test.c \
		$(CDK2_DIR)/tests/splash_setup_bds_join.c \
		$(CDK2_DIR)/src/modules/bds/entry.c \
		$(CDK2_DIR)/src/modules/bds/settings.c \
		$(CDK2_DIR)/src/modules/lvgl_setup/form.c \
		$(CDK2_DIR)/src/modules/variable_runtime/model.c \
		$(CDK2_DIR)/src/modules/variable_runtime/service.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/src/lib/boot_logo.c $(CDK2_DIR)/include/cdk2/boot_logo.h \
		$(CDK2_DIR)/src/modules/lvgl_renderer/driver.c \
		$(CDK2_DIR)/src/modules/lvgl_setup/settings.c \
		$(CDK2_DIR)/include/cdk2/lvgl_settings.h \
		$(CDK2_DIR)/include/cdk2/lvgl_ui.h $(CDK2_LVGL_LIBRARY) \
		$(CDK2_LVGL_CONFIG)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -DLV_CONF_PATH='"$(CDK2_LVGL_CONFIG)"' \
		-I"$(CDK2_LVGL_ROOT)" -ffunction-sections -fdata-sections \
		-Wl,--gc-sections -no-pie -o "$@" "$<" \
		"$(CDK2_DIR)/tests/splash_setup_bds_join.c" \
		"$(CDK2_DIR)/src/modules/bds/settings.c" \
		"$(CDK2_DIR)/src/modules/lvgl_setup/form.c" \
		"$(CDK2_DIR)/src/modules/variable_runtime/model.c" \
		"$(CDK2_DIR)/src/modules/variable_runtime/service.c" \
		"$(CDK2_DIR)/src/lib/diagnostic.c" \
		"$(CDK2_DIR)/src/modules/lvgl_setup/settings.c" \
		"$(CDK2_DIR)/src/lib/boot_logo.c" "$(CDK2_LVGL_LIBRARY)"

$(CDK2_NATIVE_LVGL_UI_OBJ): $(CDK2_DIR)/src/modules/lvgl_renderer/driver.c \
		$(CDK2_DIR)/include/cdk2/lvgl_ui.h \
		$(CDK2_DIR)/include/cdk2/lvgl_settings.h $(CDK2_LVGL_CONFIG) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		-DLV_CONF_PATH='"$(CDK2_LVGL_CONFIG)"' -I"$(CDK2_LVGL_ROOT)" \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_LVGL_UI_LINK_OBJ): $(CDK2_NATIVE_LVGL_UI_OBJ) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o $(CDK2_NATIVE_BUILD_DIR)/bds-lvgl-settings.o \
		$(CDK2_LVGL_LIBRARY)
	@$(CDK2_NATIVE_LD) -r --gc-sections -e cdk2_lvgl_ui_entry -o "$@" \
		"$(CDK2_NATIVE_LVGL_UI_OBJ)" "$(CDK2_NATIVE_BUILD_DIR)/mem.o" \
		"$(CDK2_NATIVE_BUILD_DIR)/bds-lvgl-settings.o" \
		--whole-archive "$(CDK2_LVGL_LIBRARY)" --no-whole-archive

$(CDK2_NATIVE_LVGL_UI_PE): $(CDK2_NATIVE_LVGL_UI_LINK_OBJ) \
		$(CDK2_DIR)/src/modules/lvgl_renderer/lvgl_ui.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_lvgl_ui_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/lvgl_renderer/lvgl_ui.ld" -o "$@" \
		"$(CDK2_NATIVE_LVGL_UI_LINK_OBJ)"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


.PHONY: native-bds-diagnostic-parity native-graphics-console-diagnostic-parity
native-bds-diagnostic-parity: native-diagnostic-ledgers
	@test "$$(wc -l < "$(CDK2_DIR)/migration/bds-diagnostic-parity.tsv")" -eq 23
	@cut -f5 "$(CDK2_DIR)/migration/bds-diagnostic-parity.tsv" | \
		sed -n '2,$$p' | sort -u | while IFS= read -r event; do \
		grep -R -F -q -- "$$event" "$(CDK2_DIR)/src/modules/bds" || exit 1; \
	done

native-graphics-console-diagnostic-parity:
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/graphics-console-diagnostic-parity.tsv" 6 \
		"$(CDK2_DIR)/src/modules/graphics_console"

native-check: native-terminal-diagnostic-parity \
	 native-graphics-output-diagnostic-parity \
	 native-bds-diagnostic-parity \
	 native-graphics-console-diagnostic-parity native-capsule-disk-test

native-capsule-delivery-policy-test:
	@sh "$(CDK2_DIR)/tests/capsule_delivery_policy_test.sh"

native-capsule-runtime-test: native-capsule-delivery-policy-test $(CDK2_NATIVE_CAPSULE_RUNTIME_TEST) \
		$(CDK2_NATIVE_CAPSULE_RUNTIME_ABI_TEST) $(CDK2_NATIVE_CAPSULE_RUNTIME_ENTRY_TEST) \
		$(CDK2_NATIVE_CAPSULE_RUNTIME_QEMU_PE) $(CDK2_NATIVE_CAPSULE_RUNTIME_PE) \
		$(CDK2_NATIVE_CAPSULE_RUNTIME_SUBSYSTEM_TEST)
	@"$(CDK2_NATIVE_CAPSULE_RUNTIME_TEST)"
	@"$(CDK2_NATIVE_CAPSULE_RUNTIME_ABI_TEST)"
	@"$(CDK2_NATIVE_CAPSULE_RUNTIME_ENTRY_TEST)"
	@"$(CDK2_NATIVE_CAPSULE_RUNTIME_SUBSYSTEM_TEST)" \
		"$(CDK2_NATIVE_CAPSULE_RUNTIME_PE)" \
		"$(CDK2_NATIVE_CAPSULE_RUNTIME_QEMU_PE)" \
		"$(CDK2_NATIVE_CAPSULE_RUNTIME_ENTRY_OBJ)" \
		"$(CONFIG_CDK2_DIAGNOSTIC)"

native-capsule-disk-test: $(CDK2_NATIVE_CAPSULE_DISK_TEST)
	@"$(CDK2_NATIVE_CAPSULE_DISK_TEST)"

native-esrt-test: $(CDK2_NATIVE_ESRT_TEST) $(CDK2_NATIVE_ESRT_ENTRY_TEST) \
		$(CDK2_NATIVE_ESRT_ENTRY_OBJ) $(CDK2_DIR)/tests/esrt_dependency_test.sh
	@"$(CDK2_NATIVE_ESRT_TEST)"
	@"$(CDK2_NATIVE_ESRT_ENTRY_TEST)"
	@sh "$(CDK2_DIR)/tests/esrt_policy_exclusion_test.sh" \
		"$(CDK2_NATIVE_ESRT_ENTRY_OBJ)"
	@"$(CDK2_DIR)/tests/esrt_dependency_test.sh" "$(MAKE)" \
		"$(CDK2_DIR)/src/boot/Makefile" "$(CDK2_DIR)" "$(CDK2_BUILD_DIR)" \
		"$(CDK2_NATIVE_BUILD_DIR)" "$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_CC)" \
		"$(CDK2_DIR)/include/cdk2/capsule_runtime_abi.h" \
		"$(CDK2_NATIVE_ESRT_ENTRY_OBJ)"

native-esrt-ffs:

native-esrt-diagnostic-parity: $(CDK2_NATIVE_ESRT_PE)
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/esrt-diagnostic-parity.tsv" 10 \
		"$(CDK2_DIR)/src/modules/esrt"

native-pe-test: $(CDK2_NATIVE_PE_TEST) $(CDK2_NATIVE_PE_LOW_STACK_TEST)
	@"$(CDK2_NATIVE_PE_TEST)"
	@"$(CDK2_NATIVE_PE_LOW_STACK_TEST)"

native-entry-test: $(CDK2_NATIVE_ENTRY_TEST)
	@"$(CDK2_NATIVE_ENTRY_TEST)"

native-security-stub-test: $(CDK2_NATIVE_SECURITY_STUB_TEST)
	@"$(CDK2_NATIVE_SECURITY_STUB_TEST)"

native-null-memory-test-test: $(CDK2_NATIVE_NULL_MEMORY_TEST)
	@"$(CDK2_NATIVE_NULL_MEMORY_TEST)"

native-monotonic-counter-test: $(CDK2_NATIVE_MONO_TEST)
	@"$(CDK2_NATIVE_MONO_TEST)"

native-runtime-arch-test: $(CDK2_NATIVE_RUNTIME_ARCH_TEST) \
		$(CDK2_NATIVE_RUNTIME_ARCH_ENTRY_TEST)
	@"$(CDK2_NATIVE_RUNTIME_ARCH_TEST)"
	@"$(CDK2_NATIVE_RUNTIME_ARCH_ENTRY_TEST)"

native-check: native-runtime-arch-test

$(CDK2_NATIVE_UHCI_MODEL_TEST): $(CDK2_DIR)/tests/uhci_model_test.c \
		$(CDK2_DIR)/src/modules/uhci/model.c \
		$(CDK2_DIR)/src/modules/uhci/transfer.c \
		$(CDK2_DIR)/src/modules/uhci/usb2_abi.c \
		$(CDK2_DIR)/include/cdk2/uhci.h \
		$(CDK2_DIR)/include/cdk2/xhci.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/uhci_model_test.c \
		$(CDK2_DIR)/src/modules/uhci/model.c \
		$(CDK2_DIR)/src/modules/uhci/transfer.c \
		$(CDK2_DIR)/src/modules/uhci/usb2_abi.c

.PHONY: native-uhci-test native-uhci-diagnostic-profile-test
native-uhci-test: $(CDK2_NATIVE_UHCI_MODEL_TEST) $(CDK2_NATIVE_UHCI_PCI_TEST) \
		$(CDK2_NATIVE_UHCI_ENTRY_TEST) native-uhci-diagnostic-profile-test
	@"$(CDK2_NATIVE_UHCI_MODEL_TEST)"
	@"$(CDK2_NATIVE_UHCI_PCI_TEST)"
	@"$(CDK2_NATIVE_UHCI_ENTRY_TEST)"

native-uhci-diagnostic-profile-test:
	@sh "$(CDK2_DIR)/tests/uhci_diagnostic_profile_test.sh" \
		"$(CDK2_NATIVE_HOST_CC)" "$(CDK2_DIR)"

$(CDK2_NATIVE_UHCI_ENTRY_TEST): $(CDK2_DIR)/tests/uhci_entry_test.c \
		$(CDK2_DIR)/src/modules/uhci/entry.c \
		$(CDK2_DIR)/src/modules/uhci/model.c \
		$(CDK2_DIR)/src/modules/uhci/transfer.c \
		$(CDK2_DIR)/src/modules/uhci/usb2_abi.c \
		$(CDK2_DIR)/src/modules/uhci/pci_adapter.c \
		$(CDK2_DIR)/src/modules/uhci/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/uhci.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" -DCDK2_DEBUG \
		$(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/uhci_entry_test.c \
		$(CDK2_DIR)/src/modules/uhci/model.c \
		$(CDK2_DIR)/src/modules/uhci/transfer.c \
		$(CDK2_DIR)/src/modules/uhci/usb2_abi.c \
		$(CDK2_DIR)/src/modules/uhci/pci_adapter.c \
		$(CDK2_DIR)/src/modules/uhci/diagnostic.c

$(CDK2_NATIVE_UHCI_PCI_TEST): $(CDK2_DIR)/tests/uhci_pci_test.c \
		$(CDK2_DIR)/src/modules/uhci/pci_adapter.c \
		$(CDK2_DIR)/src/modules/uhci/model.c \
		$(CDK2_DIR)/include/cdk2/uhci.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/uhci_pci_test.c \
		$(CDK2_DIR)/src/modules/uhci/pci_adapter.c \
		$(CDK2_DIR)/src/modules/uhci/model.c


$(CDK2_NATIVE_BUILD_DIR)/uhci-%.o: $(CDK2_DIR)/src/modules/uhci/%.c \
		$(CDK2_DIR)/include/cdk2/uhci.h $(CDK2_DIR)/include/cdk2/xhci.h \
		$(CDK2_DIR)/include/cdk2/pci_io_abi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_UHCI_PE): $(CDK2_NATIVE_UHCI_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/uhci/uhci.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_uhci_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/uhci/uhci.ld" -o "$@" $(CDK2_NATIVE_UHCI_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"
.PHONY: native-ehci-test
native-ehci-test: $(CDK2_NATIVE_EHCI_MODEL_TEST) $(CDK2_NATIVE_EHCI_PCI_TEST) \
		$(CDK2_NATIVE_EHCI_TRANSFER_TEST)
	@"$(CDK2_NATIVE_EHCI_MODEL_TEST)"
	@"$(CDK2_NATIVE_EHCI_PCI_TEST)"
	@"$(CDK2_NATIVE_EHCI_TRANSFER_TEST)"

$(CDK2_NATIVE_EHCI_MODEL_TEST): $(CDK2_DIR)/tests/ehci_model_test.c \
		$(CDK2_DIR)/src/modules/ehci/model.c $(CDK2_DIR)/include/cdk2/ehci.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) "$<" $(CDK2_DIR)/src/modules/ehci/model.c -o "$@"

$(CDK2_NATIVE_EHCI_PCI_TEST): $(CDK2_DIR)/tests/ehci_pci_test.c \
		$(CDK2_DIR)/src/modules/ehci/pci_adapter.c \
		$(CDK2_DIR)/src/modules/ehci/model.c $(CDK2_DIR)/include/cdk2/ehci.h \
		$(CDK2_DIR)/include/cdk2/pci_io_abi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) "$<" $(CDK2_DIR)/src/modules/ehci/pci_adapter.c \
		$(CDK2_DIR)/src/modules/ehci/model.c -o "$@"

$(CDK2_NATIVE_EHCI_TRANSFER_TEST): $(CDK2_DIR)/tests/ehci_transfer_test.c \
		$(CDK2_DIR)/src/modules/ehci/transfer.c \
		$(CDK2_DIR)/src/modules/ehci/usb2_abi.c \
		$(CDK2_DIR)/src/modules/ehci/model.c $(CDK2_DIR)/include/cdk2/ehci.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) "$<" $(CDK2_DIR)/src/modules/ehci/transfer.c \
		$(CDK2_DIR)/src/modules/ehci/usb2_abi.c \
		$(CDK2_DIR)/src/modules/ehci/model.c -o "$@"

native-check: native-ehci-test

.PHONY: native-nvme-test
native-nvme-test: $(CDK2_NATIVE_NVME_MODEL_TEST) \
		$(CDK2_NATIVE_NVME_CONTROLLER_TEST) $(CDK2_NATIVE_NVME_BINDING_TEST) \
		$(CDK2_NATIVE_NVME_PASS_TEST) $(CDK2_NATIVE_NVME_BLOCK_TEST)

native-nvme-test: $(CDK2_NATIVE_NVME_ENTRY_TEST) \
		$(CDK2_NATIVE_NVME_DIAGNOSTIC_TEST) $(CDK2_NATIVE_NVME_PCI_TEST)
	@"$(CDK2_NATIVE_NVME_MODEL_TEST)"
	@"$(CDK2_NATIVE_NVME_CONTROLLER_TEST)"
	@"$(CDK2_NATIVE_NVME_BINDING_TEST)"
	@"$(CDK2_NATIVE_NVME_PASS_TEST)"
	@"$(CDK2_NATIVE_NVME_BLOCK_TEST)"
	@"$(CDK2_NATIVE_NVME_ENTRY_TEST)"
	@"$(CDK2_NATIVE_NVME_PCI_TEST)"
	@"$(CDK2_NATIVE_NVME_DIAGNOSTIC_TEST)"

$(CDK2_NATIVE_NVME_MODEL_TEST): $(CDK2_DIR)/tests/nvme_model_test.c \
		$(CDK2_DIR)/src/modules/nvme/model.c $(CDK2_DIR)/include/cdk2/nvme.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) "$<" $(CDK2_DIR)/src/modules/nvme/model.c -o "$@"

$(CDK2_NATIVE_NVME_CONTROLLER_TEST): $(CDK2_DIR)/tests/nvme_controller_test.c \
		$(CDK2_DIR)/src/modules/nvme/controller.c \
		$(CDK2_DIR)/src/modules/nvme/discovery.c \
		$(CDK2_DIR)/src/modules/nvme/io.c \
		$(CDK2_DIR)/src/modules/nvme/pci_adapter.c \
		$(CDK2_DIR)/src/modules/nvme/model.c $(CDK2_DIR)/include/cdk2/nvme.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) "$<" $(CDK2_DIR)/src/modules/nvme/controller.c \
		$(CDK2_DIR)/src/modules/nvme/discovery.c \
		$(CDK2_DIR)/src/modules/nvme/io.c \
		$(CDK2_DIR)/src/modules/nvme/pci_adapter.c \
		$(CDK2_DIR)/src/modules/nvme/model.c -o "$@"

$(CDK2_NATIVE_NVME_BINDING_TEST): $(CDK2_DIR)/tests/nvme_binding_test.c \
		$(CDK2_DIR)/src/modules/nvme/binding.c $(CDK2_DIR)/include/cdk2/nvme.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) "$<" $(CDK2_DIR)/src/modules/nvme/binding.c -o "$@"

$(CDK2_NATIVE_NVME_PASS_TEST): $(CDK2_DIR)/tests/nvme_pass_thru_test.c \
		$(CDK2_DIR)/src/modules/nvme/pass_thru.c \
		$(CDK2_DIR)/src/modules/nvme/controller.c \
		$(CDK2_DIR)/src/modules/nvme/io.c \
		$(CDK2_DIR)/src/modules/nvme/model.c $(CDK2_DIR)/include/cdk2/nvme.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) "$<" $(CDK2_DIR)/src/modules/nvme/pass_thru.c \
		$(CDK2_DIR)/src/modules/nvme/controller.c \
		$(CDK2_DIR)/src/modules/nvme/io.c \
		$(CDK2_DIR)/src/modules/nvme/model.c -o "$@"

$(CDK2_NATIVE_NVME_BLOCK_TEST): $(CDK2_DIR)/tests/nvme_block_test.c \
		$(CDK2_DIR)/src/modules/nvme/block.c $(CDK2_DIR)/src/modules/nvme/io.c \
		$(CDK2_DIR)/src/modules/nvme/controller.c \
		$(CDK2_DIR)/src/modules/nvme/model.c $(CDK2_DIR)/include/cdk2/nvme.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) "$<" $(CDK2_DIR)/src/modules/nvme/block.c \
		$(CDK2_DIR)/src/modules/nvme/io.c \
		$(CDK2_DIR)/src/modules/nvme/controller.c \
		$(CDK2_DIR)/src/modules/nvme/model.c -o "$@"

$(CDK2_NATIVE_NVME_ENTRY_TEST): $(CDK2_DIR)/tests/nvme_entry_test.c \
		$(addprefix $(CDK2_DIR)/src/modules/nvme/,entry.c binding.c block.c \
		pass_thru.c pci_adapter.c discovery.c controller.c io.c model.c) \
		$(CDK2_DIR)/src/modules/dxe_core/database.c \
		$(CDK2_DIR)/include/cdk2/nvme.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		-DCDK2_NVME_ENTRY_TEST \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		$(addprefix $(CDK2_DIR)/src/modules/nvme/,entry.c binding.c block.c \
		pass_thru.c pci_adapter.c discovery.c controller.c io.c model.c) \
		$(CDK2_DIR)/src/modules/dxe_core/database.c -o "$@"

$(CDK2_NATIVE_NVME_PCI_TEST): $(CDK2_DIR)/tests/nvme_pci_adapter_test.c \
		$(CDK2_DIR)/src/modules/nvme/pci_adapter.c \
		$(CDK2_DIR)/include/cdk2/nvme.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		"$(CDK2_DIR)/tests/nvme_pci_adapter_test.c" \
		"$(CDK2_DIR)/src/modules/nvme/pci_adapter.c" -o "$@"

$(CDK2_NATIVE_NVME_DIAGNOSTIC_TEST): \
		$(CDK2_DIR)/tests/nvme_diagnostic_test.c \
		$(CDK2_DIR)/src/modules/nvme/diagnostic.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		-DCDK2_DEBUG $(CDK2_NATIVE_INCLUDES) $^ -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/nvme-%.o: $(CDK2_DIR)/src/modules/nvme/%.c \
		$(CDK2_DIR)/include/cdk2/nvme.h $(CDK2_DIR)/include/cdk2/pci_io_abi.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_EHCI_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_NVME_PE): $(CDK2_NATIVE_NVME_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/ehci/ehci.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_nvme_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x200 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/ehci/ehci.ld" -o "$@" $(CDK2_NATIVE_NVME_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 \
		--file-alignment 0x200 "$@"
.PHONY: native-nvme-diagnostic-parity
native-nvme-diagnostic-parity: $(CDK2_NATIVE_NVME_PE)
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/nvme-diagnostic-parity.tsv" 116 \
		"$(CDK2_DIR)/src/modules/nvme"

native-check: native-nvme-diagnostic-parity

.PHONY: native-storage-diagnostic-parity
native-storage-diagnostic-parity:
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/ata-atapi-diagnostic-parity.tsv" 83 "$(CDK2_DIR)/src/modules/ata_atapi_pass_thru"
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/ata-bus-diagnostic-parity.tsv" 18 "$(CDK2_DIR)/src/modules/ata_bus"
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/disk-io-diagnostic-parity.tsv" 5 "$(CDK2_DIR)/src/modules/disk_io"
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/emmc-diagnostic-parity.tsv" 2 "$(CDK2_DIR)/src/modules/emmc_dxe"
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/fat-diagnostic-parity.tsv" 18 "$(CDK2_DIR)/src/modules/fat"
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/partition-diagnostic-parity.tsv" 40 "$(CDK2_DIR)/src/modules/partition"
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/sata-controller-diagnostic-parity.tsv" 13 "$(CDK2_DIR)/src/modules/sata_controller"
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/scsi-disk-diagnostic-parity.tsv" 26 "$(CDK2_DIR)/src/modules/scsi_disk"
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/sd-dxe-diagnostic-parity.tsv" 36 "$(CDK2_DIR)/src/modules/sd_dxe"

native-check: native-storage-diagnostic-parity

native-check: native-nvme-test

CDK2_NATIVE_SDMMC_PCI_MODEL_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-sdmmc-pci-model-test
CDK2_NATIVE_SDMMC_PCI_ASYNC_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-sdmmc-pci-async-test
.PHONY: native-sdmmc-pci-test
native-sdmmc-pci-test: $(CDK2_NATIVE_SDMMC_PCI_MODEL_TEST) \
		$(CDK2_NATIVE_SDMMC_PCI_ASYNC_TEST)
	@"$(CDK2_NATIVE_SDMMC_PCI_MODEL_TEST)"
	@"$(CDK2_NATIVE_SDMMC_PCI_ASYNC_TEST)"

$(CDK2_NATIVE_SDMMC_PCI_ASYNC_TEST): \
		$(CDK2_DIR)/tests/sdmmc_pci_async_test.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/model.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/pci.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/async.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/sdmmc_pci.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		$(CDK2_DIR)/src/modules/sdmmc_pci/model.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/pci.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/async.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/diagnostic.c -o "$@"

$(CDK2_NATIVE_SDMMC_PCI_MODEL_TEST): \
		$(CDK2_DIR)/tests/sdmmc_pci_model_test.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/model.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/pci.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/async.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/protocol.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/binding.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/diagnostic.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/entry.c \
		$(CDK2_DIR)/include/cdk2/sdmmc_pci.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		$(CDK2_DIR)/src/modules/sdmmc_pci/model.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/pci.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/async.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/protocol.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/binding.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/diagnostic.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/entry.c -o "$@"

native-check: native-sdmmc-pci-test

CDK2_NATIVE_SD_DXE_MODEL_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-sd-dxe-model-test
CDK2_NATIVE_SD_DXE_BACKEND_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-sd-dxe-backend-test
CDK2_NATIVE_SD_DXE_BLOCK_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-sd-dxe-block-test
CDK2_NATIVE_SD_DXE_DISK_INFO_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-sd-dxe-disk-info-test
CDK2_NATIVE_SD_DXE_BINDING_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-sd-dxe-binding-test
.PHONY: native-sd-dxe-test
native-sd-dxe-test: $(CDK2_NATIVE_SD_DXE_MODEL_TEST) \
		$(CDK2_NATIVE_SD_DXE_BACKEND_TEST) $(CDK2_NATIVE_SD_DXE_BLOCK_TEST) \
		$(CDK2_NATIVE_SD_DXE_DISK_INFO_TEST) $(CDK2_NATIVE_SD_DXE_BINDING_TEST)
	@"$(CDK2_NATIVE_SD_DXE_DISK_INFO_TEST)"
	@"$(CDK2_NATIVE_SD_DXE_BINDING_TEST)"
	@"$(CDK2_NATIVE_SD_DXE_MODEL_TEST)"
	@"$(CDK2_NATIVE_SD_DXE_BACKEND_TEST)"
	@"$(CDK2_NATIVE_SD_DXE_BLOCK_TEST)"

$(CDK2_NATIVE_SD_DXE_MODEL_TEST): \
		$(CDK2_DIR)/tests/sd_dxe_model_test.c \
		$(CDK2_DIR)/src/modules/sd_dxe/model.c \
		$(CDK2_DIR)/include/cdk2/sd_dxe.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		$(CDK2_DIR)/src/modules/sd_dxe/model.c -o "$@"

$(CDK2_NATIVE_SD_DXE_BACKEND_TEST): \
		$(CDK2_DIR)/tests/sd_dxe_backend_test.c \
		$(CDK2_DIR)/src/modules/sd_dxe/model.c \
		$(CDK2_DIR)/src/modules/sd_dxe/backend.c \
		$(CDK2_DIR)/include/cdk2/sd_dxe.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		$(CDK2_DIR)/src/modules/sd_dxe/model.c \
		$(CDK2_DIR)/src/modules/sd_dxe/backend.c -o "$@"

$(CDK2_NATIVE_SD_DXE_BLOCK_TEST): \
		$(CDK2_DIR)/tests/sd_dxe_block_test.c \
		$(CDK2_DIR)/src/modules/sd_dxe/model.c \
		$(CDK2_DIR)/src/modules/sd_dxe/backend.c \
		$(CDK2_DIR)/src/modules/sd_dxe/block.c \
		$(CDK2_DIR)/include/cdk2/sd_dxe.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		$(CDK2_DIR)/src/modules/sd_dxe/model.c \
		$(CDK2_DIR)/src/modules/sd_dxe/backend.c \
		$(CDK2_DIR)/src/modules/sd_dxe/block.c -o "$@"

$(CDK2_NATIVE_SD_DXE_DISK_INFO_TEST): \
		$(CDK2_DIR)/tests/sd_dxe_disk_info_test.c \
		$(CDK2_DIR)/src/modules/sd_dxe/disk_info.c \
		$(CDK2_DIR)/include/cdk2/sd_dxe.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		$(CDK2_DIR)/src/modules/sd_dxe/disk_info.c -o "$@"

$(CDK2_NATIVE_SD_DXE_BINDING_TEST): \
		$(CDK2_DIR)/tests/sd_dxe_binding_test.c \
		$(CDK2_DIR)/src/modules/sd_dxe/model.c \
		$(CDK2_DIR)/src/modules/sd_dxe/backend.c \
		$(CDK2_DIR)/src/modules/sd_dxe/block.c \
		$(CDK2_DIR)/src/modules/sd_dxe/disk_info.c \
		$(CDK2_DIR)/src/modules/sd_dxe/binding.c \
		$(CDK2_DIR)/src/modules/sd_dxe/entry.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/model.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/protocol.c \
		$(CDK2_DIR)/include/cdk2/sd_dxe.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		$(CDK2_DIR)/src/modules/sd_dxe/model.c \
		$(CDK2_DIR)/src/modules/sd_dxe/backend.c \
		$(CDK2_DIR)/src/modules/sd_dxe/block.c \
		$(CDK2_DIR)/src/modules/sd_dxe/disk_info.c \
		$(CDK2_DIR)/src/modules/sd_dxe/binding.c \
		$(CDK2_DIR)/src/modules/sd_dxe/entry.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/model.c \
		$(CDK2_DIR)/src/modules/sdmmc_pci/protocol.c -o "$@"

native-check: native-sd-dxe-test

$(CDK2_NATIVE_BUILD_DIR)/sd-dxe-%.o: \
		$(CDK2_DIR)/src/modules/sd_dxe/%.c \
		$(CDK2_DIR)/include/cdk2/sd_dxe.h \
		$(CDK2_DIR)/include/cdk2/sdmmc_pci.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_EMMC_DXE_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_SD_DXE_PE): $(CDK2_NATIVE_SD_DXE_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(CDK2_DIR)/src/modules/ehci/ehci.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_sd_dxe_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/ehci/ehci.ld" -o "$@" \
		$(CDK2_NATIVE_SD_DXE_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"
.PHONY: native-emmc-dxe-test
native-emmc-dxe-test: $(CDK2_NATIVE_EMMC_DXE_MODEL_TEST) \
		$(CDK2_NATIVE_EMMC_DXE_BACKEND_TEST) \
		$(CDK2_NATIVE_EMMC_DXE_BLOCK_TEST) \
		$(CDK2_NATIVE_EMMC_DXE_DISK_INFO_TEST) \
		$(CDK2_NATIVE_EMMC_DXE_BINDING_TEST)
	@"$(CDK2_NATIVE_EMMC_DXE_MODEL_TEST)"
	@"$(CDK2_NATIVE_EMMC_DXE_BACKEND_TEST)"
	@"$(CDK2_NATIVE_EMMC_DXE_BLOCK_TEST)"
	@"$(CDK2_NATIVE_EMMC_DXE_DISK_INFO_TEST)"
	@"$(CDK2_NATIVE_EMMC_DXE_BINDING_TEST)"

$(CDK2_NATIVE_EMMC_DXE_MODEL_TEST): \
		$(CDK2_DIR)/tests/emmc_dxe_model_test.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/model.c \
		$(CDK2_DIR)/include/cdk2/emmc_dxe.h \
		$(CDK2_DIR)/include/cdk2/sd_dxe.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		$(CDK2_DIR)/src/modules/emmc_dxe/model.c -o "$@"

$(CDK2_NATIVE_EMMC_DXE_BACKEND_TEST): \
		$(CDK2_DIR)/tests/emmc_dxe_backend_test.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/model.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/backend.c \
		$(CDK2_DIR)/include/cdk2/emmc_dxe.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		$(CDK2_DIR)/src/modules/emmc_dxe/model.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/backend.c -o "$@"

$(CDK2_NATIVE_EMMC_DXE_BLOCK_TEST): \
		$(CDK2_DIR)/tests/emmc_dxe_block_test.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/model.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/backend.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/block.c \
		$(CDK2_DIR)/include/cdk2/emmc_dxe.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		$(CDK2_DIR)/src/modules/emmc_dxe/model.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/backend.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/block.c -o "$@"

$(CDK2_NATIVE_EMMC_DXE_DISK_INFO_TEST): \
		$(CDK2_DIR)/tests/emmc_dxe_disk_info_test.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/disk_info.c \
		$(CDK2_DIR)/include/cdk2/emmc_dxe.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		$(CDK2_DIR)/src/modules/emmc_dxe/disk_info.c -o "$@"

$(CDK2_NATIVE_EMMC_DXE_BINDING_TEST): \
		$(CDK2_DIR)/tests/emmc_dxe_binding_test.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/model.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/backend.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/block.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/disk_info.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/binding.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/entry.c \
		$(CDK2_DIR)/include/cdk2/emmc_dxe.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) "$<" \
		$(CDK2_DIR)/src/modules/emmc_dxe/model.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/backend.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/block.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/disk_info.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/binding.c \
		$(CDK2_DIR)/src/modules/emmc_dxe/entry.c -o "$@"

native-check: native-emmc-dxe-test

$(CDK2_NATIVE_BUILD_DIR)/emmc-dxe-%.o: \
		$(CDK2_DIR)/src/modules/emmc_dxe/%.c \
		$(CDK2_DIR)/include/cdk2/emmc_dxe.h \
		$(CDK2_DIR)/include/cdk2/sd_dxe.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_EMMC_DXE_PE): $(CDK2_NATIVE_EMMC_DXE_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_EMMC_DXE_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/ehci/ehci.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_emmc_dxe_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/ehci/ehci.ld" -o "$@" \
		$(CDK2_NATIVE_EMMC_DXE_OBJS) $(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_EMMC_DXE_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"
$(CDK2_NATIVE_EMMC_DXE_QEMU_OBJ): $(CDK2_DIR)/tests/emmc_dxe_qemu.c \
		$(CDK2_DIR)/include/cdk2/emmc_dxe.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_EMMC_DXE_QEMU_PE): $(CDK2_NATIVE_EMMC_DXE_QEMU_OBJ) \
		$(CDK2_DIR)/tests/ata_atapi_qemu.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry emmc_dxe_qemu_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/ata_atapi_qemu.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

.PHONY: native-emmc-dxe-oracle
native-emmc-dxe-oracle: $(CDK2_NATIVE_EMMC_DXE_QEMU_PE)
CDK2_NATIVE_VARIABLE_RUNTIME_MODEL_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-variable-runtime-model-test
CDK2_NATIVE_VARIABLE_RUNTIME_SERVICE_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-variable-runtime-service-test
CDK2_NATIVE_VARIABLE_RUNTIME_BACKEND_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-variable-runtime-backend-test
CDK2_NATIVE_VARIABLE_RUNTIME_ENTRY_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-variable-runtime-entry-test
CDK2_NATIVE_VARIABLE_RUNTIME_DIAGNOSTIC_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-variable-runtime-diagnostic-test

$(CDK2_NATIVE_VARIABLE_RUNTIME_MODEL_TEST): \
		$(CDK2_DIR)/tests/variable_runtime_model_test.c \
		$(CDK2_DIR)/src/modules/variable_runtime/model.c \
		$(CDK2_DIR)/src/modules/variable_runtime/persistence.c \
		$(CDK2_DIR)/include/cdk2/variable_runtime.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -DCDK2_CPU_DRIVER_TEST -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/variable_runtime/model.c" \
		"$(CDK2_DIR)/src/modules/variable_runtime/persistence.c"

.PHONY: native-variable-runtime-test
native-variable-runtime-test: $(CDK2_NATIVE_VARIABLE_RUNTIME_MODEL_TEST) \
		$(CDK2_NATIVE_VARIABLE_RUNTIME_SERVICE_TEST) \
		$(CDK2_NATIVE_VARIABLE_RUNTIME_BACKEND_TEST) \
		$(CDK2_NATIVE_VARIABLE_RUNTIME_DIAGNOSTIC_TEST) \
		native-variable-runtime-malformed-test
	@"$(CDK2_NATIVE_VARIABLE_RUNTIME_MODEL_TEST)"
	@"$(CDK2_NATIVE_VARIABLE_RUNTIME_SERVICE_TEST)"
	@"$(CDK2_NATIVE_VARIABLE_RUNTIME_BACKEND_TEST)"
	@"$(CDK2_NATIVE_VARIABLE_RUNTIME_DIAGNOSTIC_TEST)"

.PHONY: native-variable-runtime-entry-fixture-test
native-variable-runtime-entry-fixture-test: $(CDK2_NATIVE_VARIABLE_RUNTIME_ENTRY_TEST)
	@"$(CDK2_NATIVE_VARIABLE_RUNTIME_ENTRY_TEST)"

ifeq ($(CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME),y)
# The generic entry fixture models legacy FVB/FTW and authenticated adapters.
# Keep that HOST lane explicit; selected authority is tested separately below.
native-variable-runtime-test: native-variable-runtime-legacy-entry-test \
		native-protected-variable-entry-test authvar-runtime-test \
		authvar-namespace-test authvar-state-predicate-namespace-test

.PHONY: native-variable-runtime-legacy-entry-test
native-variable-runtime-legacy-entry-test:
	@HOSTCC="$(CDK2_NATIVE_HOST_CC)" HOSTFLAGS='$(CDK2_NATIVE_HOST_CFLAGS)' \
		bash "$(CDK2_DIR)/tests/dxe_core_legacy_fixture_test.sh" \
		"$(CDK2_NATIVE_BUILD_DIR)/variable-runtime-legacy-entry-fixture" \
		native-variable-runtime-entry-fixture-test \
		"$(abspath $(CDK2_CONFIG))" "$(abspath $(CDK2_CONFIG_HEADER))"
else
native-variable-runtime-test: native-variable-runtime-entry-fixture-test
endif

.PHONY: native-variable-runtime-malformed-test
native-variable-runtime-malformed-test: $(CDK2_NATIVE_VARIABLE_RUNTIME_MODEL_TEST)
	@CDK2_BUILD_DIR="$(CDK2_NATIVE_BUILD_DIR)" \
		HOSTCC="$(CDK2_NATIVE_HOST_CC)" \
		HOSTCFLAGS='$(CDK2_NATIVE_HOST_CFLAGS)' \
		sh "$(CDK2_DIR)/tests/variable_runtime_malformed_record_test.sh"

$(CDK2_NATIVE_VARIABLE_RUNTIME_DIAGNOSTIC_TEST): \
		$(CDK2_DIR)/tests/variable_runtime_diagnostic_test.c \
		$(CDK2_DIR)/src/modules/variable_runtime/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/variable_runtime.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -DCDK2_DEBUG \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/variable_runtime/diagnostic.c"

$(CDK2_NATIVE_CPU_ARCH_TEST): $(CDK2_DIR)/tests/cpu_arch_test.c \
		$(CDK2_DIR)/src/modules/cpu_arch/model.c \
		$(CDK2_DIR)/include/cdk2/cpu_arch.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/cpu_arch/model.c"

$(CDK2_NATIVE_CPU_PAGE_TABLE_TEST): $(CDK2_DIR)/tests/cpu_page_table_test.c \
		$(CDK2_DIR)/src/modules/cpu_arch/page_table.c \
		$(CDK2_DIR)/include/cdk2/cpu_arch.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/cpu_arch/page_table.c"

$(CDK2_NATIVE_CPU_INTERRUPT_TEST): \
		$(CDK2_DIR)/tests/cpu_interrupt_test.c \
		$(CDK2_DIR)/src/modules/cpu_arch/interrupt.c \
		$(CDK2_DIR)/include/cdk2/cpu_arch.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -DCDK2_CPU_INTERRUPT_TEST -o "$@" \
		"$<" "$(CDK2_DIR)/src/modules/cpu_arch/interrupt.c"

$(CDK2_NATIVE_CPU_DRIVER_TEST): $(CDK2_DIR)/tests/cpu_driver_test.c \
		$(CDK2_DIR)/src/modules/cpu_arch/driver.c \
		$(CDK2_DIR)/src/modules/cpu_arch/model.c \
		$(CDK2_DIR)/src/modules/cpu_arch/page_table.c \
		$(CDK2_DIR)/include/cdk2/cpu_arch.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -DCDK2_CPU_DRIVER_TEST -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/cpu_arch/driver.c" \
		"$(CDK2_DIR)/src/modules/cpu_arch/model.c" \
		"$(CDK2_DIR)/src/modules/cpu_arch/page_table.c"

.PHONY: native-cpu-arch-test native-mtrr-ownership-audit \
	 \
	native-mtrr-stage-ownership-audit \
	native-mtrr-parallel-test
$(CDK2_NATIVE_PE_EXEC_SECTIONS): $(CDK2_UTIL_DIR)/pe_exec_sections.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -o "$@" "$<"

$(CDK2_NATIVE_PE_LINK): $(CDK2_UTIL_DIR)/native_pe_link.c | $(CDK2_NATIVE_BUILD_DIR)
	@set -e; temporary="$@.tmp.$$$$"; \
		trap 'rm -f "$$temporary"' EXIT HUP INT TERM; \
		$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
			-o "$$temporary" "$<"; \
		mv -f "$$temporary" "$@"

$(CDK2_NATIVE_PE_EXEC_FIXTURE): $(CDK2_DIR)/tests/pe_exec_sections_fixture.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -o "$@" "$<"

$(CDK2_NATIVE_PE_RELOCATION_LOAD_TEST): \
		$(CDK2_DIR)/tests/pe_relocation_load_test.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -o "$@" "$<"

native-mtrr-stage-ownership-audit: \
		$(CDK2_NATIVE_MTRR_STAGE_AUDIT_MANIFEST) \
		$(CDK2_DIR)/tests/mtrr_ownership_test.sh \
		$(CDK2_NATIVE_PE_EXEC_SECTIONS)
	@OBJCOPY="$(CDK2_NATIVE_OBJCOPY)" OBJDUMP="$(CDK2_NATIVE_OBJDUMP)" \
		CDK2_PE_EXEC_SECTIONS="$(CDK2_NATIVE_PE_EXEC_SECTIONS)" \
		"$(CDK2_DIR)/tests/mtrr_ownership_test.sh" \
		"$(CDK2_NATIVE_MTRR_STAGE_AUDIT_MANIFEST)" \
		"$(CDK2_NATIVE_COREBOOT_TEST)"


native-mtrr-ownership-audit: native-mtrr-stage-ownership-audit
native-mtrr-parallel-test:
	@"$(CDK2_DIR)/tests/mtrr_parallel_test.sh" "$(MAKE)" "$(CDK2_DIR)"

native-cpu-arch-test: $(CDK2_NATIVE_CPU_ARCH_TEST) \
		$(CDK2_NATIVE_CPU_PE) $(CDK2_NATIVE_COREBOOT_ELF) \
		$(CDK2_NATIVE_CPU_PAGE_TABLE_TEST) \
		$(CDK2_NATIVE_CPU_INTERRUPT_TEST) \
		$(CDK2_NATIVE_CPU_DRIVER_TEST) \
		$(CDK2_NATIVE_CPU_DIAGNOSTIC_TEST) $(CDK2_NATIVE_COREBOOT_TEST) \
		native-mtrr-ownership-audit \
		$(CDK2_DIR)/tests/mtrr_ownership_test.sh \
		$(CDK2_NATIVE_PE_EXEC_SECTIONS) $(CDK2_NATIVE_PE_EXEC_FIXTURE) \
				$(CDK2_DIR)/tests/pe_exec_sections_test.sh \
		$(CDK2_DIR)/tests/mtrr_ownership_negative_test.sh \
		$(CDK2_DIR)/tests/mtrr_rdpru_test.sh \
		$(CDK2_DIR)/tests/fv_pe_extract_bss_test.sh
	@"$(CDK2_NATIVE_CPU_ARCH_TEST)"
	@"$(CDK2_NATIVE_CPU_PAGE_TABLE_TEST)"
	@"$(CDK2_NATIVE_CPU_INTERRUPT_TEST)"
	@"$(CDK2_NATIVE_CPU_DRIVER_TEST)"
	@"$(CDK2_NATIVE_CPU_DIAGNOSTIC_TEST)"
	@"$(CDK2_DIR)/tests/pe_exec_sections_test.sh" \
		"$(CDK2_NATIVE_PE_EXEC_SECTIONS)" "$(CDK2_NATIVE_PE_EXEC_FIXTURE)"
	@OBJCOPY="$(CDK2_NATIVE_OBJCOPY)" OBJDUMP="$(CDK2_NATIVE_OBJDUMP)" \
		CDK2_PE_EXEC_SECTIONS="$(CDK2_NATIVE_PE_EXEC_SECTIONS)" \
		"$(CDK2_DIR)/tests/mtrr_ownership_negative_test.sh" \
		"$(CDK2_DIR)/tests/mtrr_ownership_test.sh" \
		"$(CDK2_NATIVE_MTRR_STAGE_AUDIT_MANIFEST)" \
		"$(CDK2_NATIVE_COREBOOT_TEST)" "$(CDK2_NATIVE_HOST_CC)" \
		"$(CDK2_NATIVE_CPU_PE)" "$(CDK2_DIR)/tests/fv_pe_extract.sh" \
		"$(CDK2_NATIVE_COREBOOT_ELF)"
	@OBJCOPY="$(CDK2_NATIVE_OBJCOPY)" OBJDUMP="$(CDK2_NATIVE_OBJDUMP)" \
		"$(CDK2_DIR)/tests/mtrr_rdpru_test.sh" \
		"$(CDK2_DIR)/tests/mtrr_ownership_test.sh" \
		"$(CDK2_NATIVE_HOST_CC)"
	@"$(CDK2_DIR)/tests/fv_pe_extract_bss_test.sh" \
		"$(CDK2_DIR)/tests/fv_pe_extract.sh" \
		"$(CDK2_NATIVE_PE_EXEC_SECTIONS)" \
		"$(CDK2_DIR)/tests/nested_fv_fixture.c" \
		"$(CDK2_NATIVE_DXE_CORE_PE)" "$(CDK2_NATIVE_HOST_CC)"
	@test "$$(wc -l < \
		"$(CDK2_DIR)/migration/cpu-dxe-diagnostic-parity.tsv")" -eq 39

$(CDK2_NATIVE_MTRR_STAGE_AUDIT_MANIFEST): $(CDK2_NATIVE_COREBOOT_ELF) \
		$(CDK2_NATIVE_COREBOOT_MAP) $(CDK2_NATIVE_COREBOOT_TEST) \
		$(CDK2_CONFIG) $(CDK2_MANIFEST) FORCE | $(CDK2_NATIVE_BUILD_DIR)
	@set -o pipefail; temporary="$$(mktemp "$@.tmp.XXXXXX")"; \
	trap 'rm -f "$$temporary"' EXIT; { \
		sha256sum "$(CDK2_NATIVE_COREBOOT_ELF)" | awk \
			'{ print $$1 "|code|c0000080|1|" $$2 }'; \
		for input in "$(CDK2_NATIVE_COREBOOT_MAP)" \
			"$(CDK2_CONFIG)" "$(CDK2_MANIFEST)"; do \
			sha256sum "$$input" | awk \
				'{ print $$1 "|data|-|0|" $$2 }'; \
		done; \
		sha256sum "$(CDK2_NATIVE_COREBOOT_TEST)" | awk \
			'{ print $$1 "|test|-|0|" $$2 }'; \
	} > "$$temporary"; mv "$$temporary" "$@"; trap - EXIT

.SECONDEXPANSION:


$(CDK2_NATIVE_LOCAL_APIC_TIMER_OBJ): \
		$(CDK2_DIR)/src/modules/local_apic_timer/local_apic_timer.c \
		$(CDK2_DIR)/include/cdk2/local_apic_timer.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) \
		$(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_LOCAL_APIC_TIMER_DRIVER_OBJ): \
		$(CDK2_DIR)/src/modules/local_apic_timer/driver.c \
		$(CDK2_DIR)/include/cdk2/local_apic_timer.h \
		$(CDK2_DIR)/include/cdk2/x64_interrupt_context.h \
		$(CDK2_DIR)/include/guid/local_apic_timer_info.h \
		$(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) \
		$(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-MMD -MP -MF "$@.d" -MT "$@" \
		-fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_LOCAL_APIC_TIMER_PE): $(CDK2_NATIVE_LOCAL_APIC_TIMER_OBJ) \
		$(CDK2_NATIVE_LOCAL_APIC_TIMER_DRIVER_OBJ) \
		$(CDK2_DIR)/src/modules/local_apic_timer/local_apic_timer.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 \
		--entry cdk2_local_apic_timer_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/local_apic_timer/local_apic_timer.ld" \
		-o "$@" $(CDK2_NATIVE_LOCAL_APIC_TIMER_OBJ) \
		$(CDK2_NATIVE_LOCAL_APIC_TIMER_DRIVER_OBJ)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_CPU_DIAGNOSTIC_TEST): \
		$(CDK2_DIR)/tests/cpu_diagnostic_test.c \
		$(CDK2_DIR)/src/modules/cpu_arch/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/cpu_arch.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -DCDK2_DEBUG \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/cpu_arch/diagnostic.c"

$(CDK2_NATIVE_BUILD_DIR)/cpu-arch-%.o: \
		$(CDK2_DIR)/src/modules/cpu_arch/%.c \
		$(CDK2_DIR)/include/cdk2/cpu_arch.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) \
		$(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/cpu-arch-interrupt-asm.o: \
		$(CDK2_DIR)/src/modules/cpu_arch/interrupt.S \
		$(CDK2_DIR)/include/cdk2/x64_interrupt_context.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_CPU_PE): $(CDK2_NATIVE_CPU_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/cpu_arch/cpu_arch.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 \
		--entry cdk2_cpu_arch_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/cpu_arch/cpu_arch.ld" -o "$@" \
		$(CDK2_NATIVE_CPU_OBJS) $(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"
.PHONY: native-cpu-diagnostic-parity
native-cpu-diagnostic-parity: $(CDK2_NATIVE_CPU_PE)
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/cpu-dxe-diagnostic-parity.tsv" 39 \
		"$(CDK2_DIR)/src/modules/cpu_arch"

native-check: native-cpu-diagnostic-parity

native-check: native-cpu-arch-test

$(CDK2_NATIVE_VARIABLE_RUNTIME_SERVICE_TEST): \
		$(CDK2_DIR)/tests/variable_runtime_service_test.c \
		$(CDK2_DIR)/src/modules/variable_runtime/model.c \
		$(CDK2_DIR)/src/modules/variable_runtime/service.c \
		$(CDK2_DIR)/include/cdk2/variable_runtime.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/variable_runtime/model.c" \
		"$(CDK2_DIR)/src/modules/variable_runtime/service.c"

$(CDK2_NATIVE_VARIABLE_RUNTIME_BACKEND_TEST): \
		$(CDK2_DIR)/tests/variable_runtime_backend_test.c \
		$(CDK2_DIR)/src/modules/variable_runtime/model.c \
		$(CDK2_DIR)/src/modules/variable_runtime/persistence.c \
		$(CDK2_DIR)/src/modules/variable_runtime/backend.c \
		$(CDK2_DIR)/include/cdk2/variable_runtime.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		$(CDK2_DIR)/src/modules/variable_runtime/model.c \
		$(CDK2_DIR)/src/modules/variable_runtime/persistence.c \
		$(CDK2_DIR)/src/modules/variable_runtime/backend.c

$(CDK2_NATIVE_VARIABLE_RUNTIME_ENTRY_TEST): \
		$(CDK2_DIR)/tests/variable_runtime_entry_test.c \
		$(CDK2_DIR)/src/lib/capsule_report.c \
		$(CDK2_DIR)/include/cdk2/capsule_report.h \
		$(CDK2_DIR)/tests/esrt_entry_test.c \
		$(CDK2_DIR)/src/modules/esrt/entry.c \
		$(CDK2_DIR)/src/modules/esrt/esrt.c \
		$(CDK2_DIR)/src/modules/variable_runtime/model.c \
		$(CDK2_DIR)/src/modules/variable_runtime/persistence.c \
		$(CDK2_DIR)/src/modules/variable_runtime/service.c \
		$(CDK2_DIR)/src/modules/variable_runtime/backend.c \
		$(CDK2_DIR)/src/modules/variable_runtime/entry.c \
		$(CDK2_DIR)/src/modules/variable_runtime/diagnostic.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/tests/diagnostic_runtime_guard.h \
		$(CDK2_DIR)/include/cdk2/variable_runtime.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		-DCDK2_ESRT_REAL_POLICY_CHECK -Dmain=cdk2_esrt_observer_main \
		$(CDK2_NATIVE_INCLUDES) -c "$(CDK2_DIR)/tests/esrt_entry_test.c" \
		-o "$(CDK2_NATIVE_BUILD_DIR)/esrt-policy-fixture.o"
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-D_GNU_SOURCE -DCDK2_DIAGNOSTIC \
		-DCDK2_DIAG_RUNTIME_TRANSITION_TEST $(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		$(CDK2_DIR)/src/modules/variable_runtime/model.c \
		$(CDK2_DIR)/src/modules/variable_runtime/persistence.c \
		$(CDK2_DIR)/src/modules/variable_runtime/service.c \
		$(CDK2_DIR)/src/modules/variable_runtime/backend.c \
		$(CDK2_DIR)/src/modules/variable_runtime/entry.c \
		$(CDK2_DIR)/src/modules/variable_runtime/diagnostic.c \
		$(CDK2_NATIVE_BUILD_DIR)/esrt-policy-fixture.o \
		$(CDK2_DIR)/src/lib/capsule_report.c \
		$(CDK2_DIR)/src/modules/esrt/esrt.c \
		$(CDK2_DIR)/src/lib/diagnostic.c

native-check: native-variable-runtime-test

$(CDK2_NATIVE_BUILD_DIR)/variable-runtime-%.o: \
		$(CDK2_DIR)/src/modules/variable_runtime/%.c \
		$(CDK2_DIR)/include/cdk2/variable_runtime.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(foreach source,$(CDK2_NATIVE_VARIABLE_PROTECTED_SOURCES),\
	$(eval $(CDK2_NATIVE_BUILD_DIR)/variable-protected-$(notdir $(basename $(source))).o: \
		$(CDK2_DIR)/$(source)))
$(CDK2_NATIVE_VARIABLE_PROTECTED_OBJS): | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_VARIABLE_RUNTIME_PE): $(CDK2_NATIVE_VARIABLE_RUNTIME_OBJS) \
		$(CDK2_NATIVE_VARIABLE_PROTECTED_OBJS) \
		$(CDK2_NATIVE_VARIABLE_POLICY_CRYPTO_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/ehci/ehci.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 12 --entry cdk2_variable_runtime_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/ehci/ehci.ld" -o "$@" \
		$(CDK2_NATIVE_VARIABLE_RUNTIME_OBJS) \
		$(CDK2_NATIVE_VARIABLE_PROTECTED_OBJS) \
		$(CDK2_NATIVE_VARIABLE_POLICY_CRYPTO_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 12 "$@"
.PHONY: native-variable-runtime-diagnostic-parity
native-variable-runtime-diagnostic-parity: $(CDK2_NATIVE_VARIABLE_RUNTIME_PE)
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/variable-runtime-diagnostic-parity.tsv" 61 \
		"$(CDK2_DIR)/src/modules/variable_runtime"

native-check: native-variable-runtime-diagnostic-parity

$(CDK2_NATIVE_VARIABLE_RUNTIME_QEMU_OBJ): \
		$(CDK2_DIR)/tests/variable_runtime_qemu.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_VARIABLE_RUNTIME_QEMU_PE): \
		$(CDK2_NATIVE_VARIABLE_RUNTIME_QEMU_OBJ) \
		$(CDK2_DIR)/tests/ata_atapi_qemu.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry variable_runtime_qemu_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/ata_atapi_qemu.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

.PHONY: native-variable-runtime-oracle
native-variable-runtime-oracle: $(CDK2_NATIVE_VARIABLE_RUNTIME_QEMU_PE)
$(CDK2_NATIVE_SD_DXE_QEMU_OBJ): $(CDK2_DIR)/tests/sd_dxe_qemu.c \
		$(CDK2_DIR)/include/cdk2/sd_dxe.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_SD_DXE_QEMU_PE): $(CDK2_NATIVE_SD_DXE_QEMU_OBJ) \
		$(CDK2_DIR)/tests/ata_atapi_qemu.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry sd_dxe_qemu_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/ata_atapi_qemu.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

.PHONY: native-sd-dxe-oracle
native-sd-dxe-oracle: $(CDK2_NATIVE_SD_DXE_QEMU_PE)

$(CDK2_NATIVE_BUILD_DIR)/sdmmc-pci-%.o: \
		$(CDK2_DIR)/src/modules/sdmmc_pci/%.c \
		$(CDK2_DIR)/include/cdk2/sdmmc_pci.h \
		$(CDK2_DIR)/include/cdk2/pci_io_abi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/sdmmc-pci-diagnostic-core.o: \
		$(CDK2_DIR)/src/lib/diagnostic.c $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_SDMMC_PCI_PE): $(CDK2_NATIVE_SDMMC_PCI_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_SDMMC_PCI_DIAG_CORE)) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(CDK2_DIR)/src/modules/ehci/ehci.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_sdmmc_pci_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/ehci/ehci.ld" -o "$@" \
		$(CDK2_NATIVE_SDMMC_PCI_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_SDMMC_PCI_DIAG_CORE)) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_SDMMC_PCI_DIAGNOSTIC_TEST): \
		$(CDK2_DIR)/tests/sdmmc_pci_diagnostic_test.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) "$<" -o "$@"

.PHONY: native-sdmmc-pci-diagnostic-parity
native-sdmmc-pci-diagnostic-parity: $(CDK2_NATIVE_SDMMC_PCI_PE) \
		$(CDK2_NATIVE_SDMMC_PCI_DIAGNOSTIC_TEST)
	@if [ "$(CONFIG_CDK2_BUILD_DEBUG)" = y ]; then \
		"$(CDK2_NATIVE_SDMMC_PCI_DIAGNOSTIC_TEST)" "$(CDK2_NATIVE_SDMMC_PCI_PE)"; fi

native-check: native-sdmmc-pci-diagnostic-parity
$(CDK2_NATIVE_BUILD_DIR)/ehci-%.o: $(CDK2_DIR)/src/modules/ehci/%.c \
		$(CDK2_DIR)/include/cdk2/ehci.h $(CDK2_DIR)/include/cdk2/xhci.h \
		$(CDK2_DIR)/include/cdk2/pci_io_abi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_EHCI_PE): $(CDK2_NATIVE_EHCI_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_EHCI_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/ehci/ehci.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_ehci_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/ehci/ehci.ld" -o "$@" $(CDK2_NATIVE_EHCI_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_EHCI_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"
native-english-test: $(CDK2_NATIVE_ENGLISH_TEST)
	@"$(CDK2_NATIVE_ENGLISH_TEST)"

$(CDK2_NATIVE_PCI_BUS_CONFIG_HEADER): $(CDK2_CONFIG_HEADER)
	@mkdir -p "$(@D)"
	@set -e; temporary="$@.tmp.$$PPID"; \
		awk -v linear=$(if $(filter y,$(CONFIG_CDK2_LINEAR_BOOT)),1,0) \
			-v debug=$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),1,0) \
			-v diagnostic=$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),1,0) \
			-v spi=$(if $(filter y,$(CONFIG_CDK2_SPI_CONSOLE)),1,0) ' \
			/^#define CONFIG_CDK2_LINEAR_BOOT / { $$3 = linear } \
			/^#define CONFIG_CDK2_BUILD_DEBUG / { $$3 = debug } \
			/^#define CONFIG_CDK2_DIAGNOSTIC / { $$3 = diagnostic } \
			/^#define CONFIG_CDK2_SPI_CONSOLE / { $$3 = spi } \
			{ print } \
		' "$<" > "$$temporary"; \
		mv "$$temporary" "$@"

$(CDK2_NATIVE_PCI_BUS_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_NATIVE_PCI_BUS_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@mkdir -p "$(@D)"
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_PCI_BUS_INCLUDES) \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

ifeq ($(CONFIG_CDK2_NATIVE_SECURITY_STUB),y)


$(CDK2_NATIVE_SECURITY_STUB_PE): $(CDK2_NATIVE_SECURITY_STUB_OBJ) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) $(CDK2_DIR)/src/modules/security_stub/security_stub.ld $(CDK2_NATIVE_PERELOCCHECK)
	@SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) $(CDK2_NATIVE_PE_LD) --subsystem 11 \
		--entry security_stub_initialize --image-base 0 --section-alignment 0x1000 \
		--file-alignment 0x1000 --build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/security_stub/security_stub.ld" -o "$@" \
		$(CDK2_NATIVE_SECURITY_STUB_OBJ) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"

$(CDK2_NATIVE_SECURITY_STUB_OBJ): $(CDK2_DIR)/src/modules/security_stub/security_stub.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"


endif

$(CDK2_NATIVE_NULL_MEMORY_TEST_OBJ): $(CDK2_DIR)/src/modules/null_memory_test/null_memory_test.c $(CDK2_DIR)/include/cdk2/null_memory_test.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -ffunction-sections -fdata-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_NULL_MEMORY_TEST_PE): $(CDK2_NATIVE_NULL_MEMORY_TEST_OBJ) \
		$(CDK2_DIR)/src/modules/null_memory_test/null_memory_test.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_null_memory_test_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/null_memory_test/null_memory_test.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"


native-elfcheck-test: $(CDK2_NATIVE_ELF_CHECK) $(CDK2_NATIVE_ELF_CHECK_TEST)
	@"$(CDK2_NATIVE_ELF_CHECK_TEST)" "$(CDK2_NATIVE_ELF_CHECK)" "$(CDK2_NATIVE_BUILD_DIR)"

$(CDK2_NATIVE_BUILD_DIR):
	@mkdir -p "$@"

$(CDK2_NATIVE_DIAGNOSTIC_TEST): $(CDK2_DIR)/tests/diagnostic_test.c \
		$(CDK2_DIR)/src/lib/diagnostic.c $(CDK2_DIR)/include/cdk2/diagnostic.h \
		$(CDK2_DIR)/include/cdk2/uart.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-DCDK2_DIAG_UNIT_TEST -pthread -o "$@" \
		$(CDK2_DIR)/tests/diagnostic_test.c $(CDK2_DIR)/src/lib/diagnostic.c

$(CDK2_NATIVE_DIAGNOSTIC_RUNTIME_TRANSITION_TEST): \
		$(CDK2_DIR)/tests/diagnostic_runtime_transition_test.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/diagnostic.h $(CDK2_DIR)/include/cdk2/uart.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-DCDK2_DIAG_UNIT_TEST -o "$@" \
		$(CDK2_DIR)/tests/diagnostic_runtime_transition_test.c \
		$(CDK2_DIR)/src/lib/diagnostic.c

$(CDK2_NATIVE_DEADLINE_TEST): $(CDK2_DIR)/tests/deadline_test.c \
		$(CDK2_DIR)/src/lib/deadline.c \
		$(CDK2_DIR)/include/cdk2/deadline.h \
		$(CDK2_DIR)/include/guid/tsc_info.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/deadline_test.c \
		$(CDK2_DIR)/src/lib/deadline.c

$(CDK2_NATIVE_LINEAR_BOOT_TEST): $(CDK2_DIR)/tests/linear_boot_test.c \
		$(CDK2_DIR)/src/lib/linear_boot.c $(CDK2_DIR)/include/cdk2/linear_boot.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/linear_boot_test.c \
		$(CDK2_DIR)/src/lib/linear_boot.c

$(CDK2_NATIVE_EARLY_SPLASH_TEST): $(CDK2_DIR)/tests/early_splash_test.c \
		$(CDK2_DIR)/src/boot/early_splash.c \
		$(CDK2_DIR)/src/lib/boot_logo.c \
		$(CDK2_DIR)/include/cdk2/early_splash.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -no-pie \
		-DCDK2_EARLY_SPLASH_TEST $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/early_splash_test.c \
		$(CDK2_DIR)/src/boot/early_splash.c \
		$(CDK2_DIR)/src/lib/boot_logo.c

$(CDK2_NATIVE_COREBOOT_ENTRY): $(CDK2_NATIVE_DIR)/entry32.S | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) -c -ffreestanding -fno-pie -fno-stack-protector -o "$@" "$<"

$(CDK2_NATIVE_BUILD_DIR)/entry.o: $(CDK2_NATIVE_DIR)/entry.c $(CDK2_NATIVE_DIR)/entry.h $(CDK2_NATIVE_DIR)/context.h $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/diagnostic.o: $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/diagnostic.h $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/platform.o: $(CDK2_DIR)/src/platform/cdk2_platform_lib/cdk2_platform_lib.c $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/coreboot_handoff.o: $(CDK2_NATIVE_DIR)/coreboot_handoff.c $(CDK2_NATIVE_DIR)/coreboot_hobs.h $(CDK2_NATIVE_DIR)/pe.h $(CDK2_NATIVE_DIR)/symbols.h $(CDK2_DIR)/include/cdk2/payload_mm_authvar_service.h $(CDK2_DIR)/include/guid/deadline_tsc_info.h $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) -I$(CDK2_NATIVE_DIR) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/payload_mm_authvar_service.o: \
		$(CDK2_DIR)/src/lib/payload_mm_authvar_service.c \
		$(CDK2_DIR)/include/cdk2/payload_mm_authvar_service.h \
		$(CDK2_DIR)/include/coreboot_tables.h $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" \
		-MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/image_policy_snapshot.o: \
		$(CDK2_DIR)/src/lib/image_policy_snapshot.c \
		$(CDK2_DIR)/include/cdk2/image_policy_snapshot.h $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" \
		-MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/coreboot_dma_handoff.o: \
		$(CDK2_NATIVE_DIR)/coreboot_dma_handoff.c \
		$(CDK2_NATIVE_DIR)/coreboot.h \
		$(CDK2_DIR)/include/cdk2/dma_handoff.h \
		$(CDK2_DIR)/include/coreboot_tables.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" \
		-MT "$@" $(CDK2_NATIVE_INCLUDES) -I$(CDK2_NATIVE_DIR) -c "$<" -o "$@"

.PHONY: native-coreboot-dxe-return-trap-test
native-check: native-coreboot-dxe-return-trap-test
native-coreboot-dxe-return-trap-test: $(CDK2_NATIVE_BUILD_DIR)/coreboot_handoff.o
	@"$(CDK2_DIR)/tests/coreboot_dxe_return_trap_test.sh" "$<"

$(CDK2_NATIVE_BUILD_DIR)/linear_boot.o: $(CDK2_DIR)/src/lib/linear_boot.c \
		$(CDK2_DIR)/include/cdk2/linear_boot.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" \
		-MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/boot_logo.o: $(CDK2_DIR)/src/lib/boot_logo.c \
		$(CDK2_DIR)/include/cdk2/boot_logo.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" \
		-MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/early_splash.o: $(CDK2_DIR)/src/boot/early_splash.c \
		$(CDK2_DIR)/include/cdk2/early_splash.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" \
		-MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/coreboot.o: $(CDK2_NATIVE_DIR)/coreboot.c $(CDK2_NATIVE_DIR)/coreboot.h $(CDK2_NATIVE_DIR)/coreboot_resource.h $(CDK2_DIR)/include/coreboot_tables.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) -I$(CDK2_NATIVE_DIR) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/coreboot_checksum.o: $(CDK2_NATIVE_DIR)/coreboot_checksum.c $(CDK2_NATIVE_DIR)/coreboot_checksum.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) -I$(CDK2_NATIVE_DIR) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/coreboot_resource.o: $(CDK2_NATIVE_DIR)/coreboot_resource.c $(CDK2_NATIVE_DIR)/coreboot_resource.h $(CDK2_NATIVE_DIR)/coreboot.h $(CDK2_NATIVE_DIR)/coreboot_checksum.h $(CDK2_DIR)/include/coreboot_tables.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) -I$(CDK2_NATIVE_DIR) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/coreboot_hobs.o: $(CDK2_NATIVE_DIR)/coreboot_hobs.c $(CDK2_NATIVE_DIR)/coreboot_hobs.h $(CDK2_NATIVE_DIR)/coreboot_resource.h $(CDK2_NATIVE_DIR)/coreboot.h $(CDK2_DIR)/include/cdk2/pci_topology_handoff.h $(CDK2_DIR)/include/pi/hob.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) -I$(CDK2_NATIVE_DIR) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/pe.o: $(CDK2_NATIVE_DIR)/pe.c $(CDK2_NATIVE_DIR)/pe.h $(CDK2_DIR)/include/cdk2/pe_image_view.h $(CDK2_DIR)/include/industry_standard/pe_image.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -Wframe-larger-than=1024 \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) \
		-I$(CDK2_NATIVE_DIR) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/pe_image_view.o: $(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/include/cdk2/pe_image_view.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/mem.o: $(CDK2_LIB_DIR)/mem.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_PE_RELOCATION_MARKER_OBJ): \
		$(CDK2_DIR)/src/lib/pe_relocation_marker.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/mem-memcpy.o: $(CDK2_NATIVE_BUILD_DIR)/mem.o
	@$(CDK2_NATIVE_LD) -r --gc-sections -u memcpy "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o: $(CDK2_NATIVE_BUILD_DIR)/mem.o
	@$(CDK2_NATIVE_LD) -r --gc-sections -u memcpy -u memset "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/mem-set.o: $(CDK2_NATIVE_BUILD_DIR)/mem.o
	@$(CDK2_NATIVE_LD) -r --gc-sections -u memset "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/services.o: $(CDK2_NATIVE_DIR)/services.c $(CDK2_NATIVE_DIR)/services.h $(CDK2_NATIVE_DIR)/context.h $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/override.o: $(CDK2_NATIVE_DIR)/override.c $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"


$(CDK2_NATIVE_SECURITY_STUB_TEST): $(CDK2_DIR)/tests/security_stub_test.c $(CDK2_DIR)/src/modules/security_stub/security_stub.c $(CDK2_DIR)/src/lib/diagnostic.c $(CDK2_DIR)/include/cdk2/security_router.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_LDFLAGS),\
		"$<" $(CDK2_DIR)/src/lib/diagnostic.c)

$(CDK2_NATIVE_NULL_MEMORY_TEST): $(CDK2_DIR)/tests/null_memory_test_test.c $(CDK2_DIR)/src/modules/null_memory_test/null_memory_test.c $(CDK2_DIR)/include/cdk2/null_memory_test.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<"

$(CDK2_NATIVE_MONO_TEST): $(CDK2_DIR)/tests/monotonic_counter_test.c $(CDK2_DIR)/src/modules/monotonic_counter/monotonic_counter.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<"

$(CDK2_NATIVE_RUNTIME_ARCH_TEST): $(CDK2_DIR)/tests/runtime_arch_test.c \
		$(CDK2_DIR)/src/modules/runtime_arch/runtime_arch.c \
		$(CDK2_DIR)/include/cdk2/runtime_arch.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/runtime_arch_test.c \
		$(CDK2_DIR)/src/modules/runtime_arch/runtime_arch.c

$(CDK2_NATIVE_RUNTIME_ARCH_ENTRY_TEST): \
		$(CDK2_DIR)/tests/runtime_arch_entry_test.c \
		$(CDK2_DIR)/src/modules/runtime_arch/entry.c \
		$(CDK2_DIR)/src/modules/runtime_arch/runtime_arch.c \
		$(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/include/cdk2/runtime_arch_entry.h \
		$(CDK2_DIR)/include/cdk2/runtime_arch.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(filter-out -O%,$(CDK2_NATIVE_HOST_CFLAGS)) \
		-O1 -fsanitize=undefined -fno-sanitize-recover=undefined \
		-fno-omit-frame-pointer -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) \
		-DCDK2_RUNTIME_ARCH_ENTRY_TEST \
		$(CDK2_NATIVE_HOST_LDFLAGS) -o "$@" \
		$(CDK2_DIR)/tests/runtime_arch_entry_test.c \
		$(CDK2_DIR)/src/modules/runtime_arch/entry.c \
		$(CDK2_DIR)/src/modules/runtime_arch/runtime_arch.c \
		$(CDK2_PE_IMAGE_VIEW_SRC)

CDK2_NATIVE_RUNTIME_ARCH_OBJS := \
	$(CDK2_NATIVE_BUILD_DIR)/runtime-arch-core.o \
	$(CDK2_NATIVE_BUILD_DIR)/runtime-arch-entry.o \
	$(CDK2_NATIVE_BUILD_DIR)/runtime-arch-pe_image_view.o

$(CDK2_NATIVE_BUILD_DIR)/runtime-arch-core.o: \
		$(CDK2_DIR)/src/modules/runtime_arch/runtime_arch.c \
		$(CDK2_DIR)/include/cdk2/runtime_arch.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections, \
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/runtime-arch-entry.o: \
		$(CDK2_DIR)/src/modules/runtime_arch/entry.c \
		$(CDK2_DIR)/include/cdk2/runtime_arch_entry.h \
		$(CDK2_DIR)/include/cdk2/runtime_arch.h \
		$(CDK2_DIR)/include/cdk2/pe_image_view.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections, \
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/runtime-arch-pe_image_view.o: \
		$(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/include/cdk2/pe_image_view.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

$(CDK2_NATIVE_RUNTIME_ARCH_PE): $(CDK2_NATIVE_RUNTIME_ARCH_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-memcpy.o \
		$(CDK2_DIR)/src/modules/runtime_arch/runtime_arch.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 12 \
		--entry cdk2_runtime_arch_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x200 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/runtime_arch/runtime_arch.ld" \
		-o "$@" $(CDK2_NATIVE_RUNTIME_ARCH_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-memcpy.o
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 12 "$@"


$(CDK2_NATIVE_ENGLISH_TEST): $(CDK2_DIR)/tests/english_test.c $(CDK2_DIR)/src/modules/english/english.c $(CDK2_DIR)/include/cdk2/english.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<"

$(CDK2_NATIVE_ELF_CHECK): $(CDK2_UTIL_DIR)/elfcheck.c \
		$(CDK2_DIR)/src/lib/direct_image_table.c $(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/include/cdk2/direct_image_table.h \
		$(CDK2_DIR)/include/cdk2/pe_image_view.h \
		$(CDK2_DIR)/include/industry_standard/pe_image.h \
		$(CDK2_DIR)/include/uefi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		$(CDK2_NATIVE_HOST_LDFLAGS) -o "$@" "$<" \
		$(CDK2_DIR)/src/lib/direct_image_table.c $(CDK2_PE_IMAGE_VIEW_SRC)

$(CDK2_NATIVE_ELF_CHECK_TEST): $(CDK2_DIR)/tests/elfcheck_test.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -MMD -MP -MF "$(CDK2_NATIVE_BUILD_DIR)/elfcheck-test.d" -MT "$@" $(CDK2_NATIVE_HOST_LDFLAGS) -o "$@" "$<"

$(CDK2_NATIVE_BUILD_DIR)/payload.o: $(CDK2_NATIVE_DIR)/payload.c $(CDK2_NATIVE_DIR)/services.h $(CDK2_NATIVE_DIR)/context.h $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/direct_image_table.o: \
		$(CDK2_DIR)/src/lib/direct_image_table.c \
		$(CDK2_DIR)/include/cdk2/direct_image_table.h \
		$(CDK2_DIR)/include/cdk2/pe_image_view.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_ELF): $(CDK2_NATIVE_STAGE_OBJS) $(CDK2_NATIVE_DIR)/cdk2.ld
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_LINK_FLAGS) -Wl,-Map,"$(CDK2_NATIVE_MAP)" $(CDK2_NATIVE_LINK_POST_MAP_FLAGS) -Wl,-e,cdk2_native_stage_entry -o "$@" $(CDK2_NATIVE_STAGE_LINK_INPUTS)

$(CDK2_NATIVE_OVERRIDE_ELF): $(CDK2_NATIVE_OVERRIDE_OBJS) $(CDK2_NATIVE_DIR)/cdk2.ld
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_LINK_FLAGS) -Wl,-Map,"$(CDK2_NATIVE_OVERRIDE_MAP)" $(CDK2_NATIVE_LINK_POST_MAP_FLAGS) -Wl,-e,cdk2_native_stage_entry -o "$@" $(CDK2_NATIVE_OVERRIDE_LINK_INPUTS)

$(CDK2_NATIVE_COREBOOT_ELF) $(CDK2_NATIVE_COREBOOT_MAP) &: \
		$(CDK2_NATIVE_COREBOOT_OBJS) $(CDK2_NATIVE_DIR)/cdk2.ld
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_LINK_FLAGS) -Wl,-Map,"$(CDK2_NATIVE_COREBOOT_MAP)" $(CDK2_NATIVE_LINK_POST_MAP_FLAGS) -Wl,-e,cdk2_coreboot_entry32 -o "$(CDK2_NATIVE_COREBOOT_ELF)" $(CDK2_NATIVE_COREBOOT_LINK_INPUTS)

.PHONY: native-linear-payload-admission
.PHONY: native-linear-payload-admission-test
native-check: native-linear-payload-admission-test
native-linear-payload-admission-test:
	@sh "$(CDK2_DIR)/tests/linear_payload_admission_test.sh"

native-linear-payload-admission: $(CDK2_CONFIG_HEADER)
	@$(CDK2_NATIVE_LINEAR_ADMISSION_COMMAND)

.SECONDEXPANSION:
$(CDK2_NATIVE_METRONOME_OBJ): $(CDK2_DIR)/src/modules/metronome/metronome.c \
		$(CDK2_DIR)/include/cdk2/metronome.h \
		$(CDK2_DIR)/include/guid/acpi_board_info.h \
		$(CDK2_DIR)/include/guid/tsc_info.h $(CDK2_DIR)/include/pi/hob.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -ffunction-sections -fdata-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -MMD -MP \
		-MF "$(CDK2_NATIVE_BUILD_DIR)/metronome.d" -MT "$@" \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_METRONOME_PE): $(CDK2_NATIVE_METRONOME_OBJ) $(CDK2_DIR)/src/modules/metronome/metronome.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_metronome_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s -T "$(CDK2_DIR)/src/modules/metronome/metronome.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"


ifeq ($(CONFIG_CDK2_NATIVE_WATCHDOG),y)

$(CDK2_NATIVE_WATCHDOG_OBJ): $(CDK2_DIR)/src/modules/watchdog/watchdog.c $(CDK2_DIR)/include/cdk2/watchdog.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -ffunction-sections -fdata-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -MMD -MP \
		-MF "$(CDK2_NATIVE_BUILD_DIR)/watchdog.d" -MT "$@" \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_WATCHDOG_PE): $(CDK2_NATIVE_WATCHDOG_OBJ) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) $(CDK2_DIR)/src/modules/watchdog/watchdog.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_watchdog_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s -T "$(CDK2_DIR)/src/modules/watchdog/watchdog.ld" -o "$@" \
		$(CDK2_NATIVE_WATCHDOG_OBJ) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"


endif

ifeq ($(CONFIG_CDK2_NATIVE_STATUS_CODE_ROUTER),y)

$(CDK2_NATIVE_STATUS_CODE_ROUTER_OBJ): $(CDK2_DIR)/src/modules/status_code_router/status_code_router.c $(CDK2_DIR)/include/cdk2/status_code_router.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -ffunction-sections -fdata-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_STATUS_CODE_ROUTER_PE): $(CDK2_NATIVE_STATUS_CODE_ROUTER_OBJ) $(CDK2_DIR)/src/modules/status_code_router/status_code_router.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 12 --entry cdk2_status_code_router_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s -T "$(CDK2_DIR)/src/modules/status_code_router/status_code_router.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"


endif

$(CDK2_NATIVE_STATUS_CODE_HANDLER_OBJ): $(CDK2_DIR)/src/modules/status_code_handler/status_code_handler.c $(CDK2_DIR)/include/cdk2/status_code_router.h $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -ffunction-sections -fdata-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_STATUS_CODE_HANDLER_PE): $(CDK2_NATIVE_STATUS_CODE_HANDLER_OBJ) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) $(CDK2_DIR)/src/modules/status_code_handler/status_code_handler.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 12 --entry cdk2_status_code_handler_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s -T "$(CDK2_DIR)/src/modules/status_code_handler/status_code_handler.ld" -o "$@" \
		$(CDK2_NATIVE_STATUS_CODE_HANDLER_OBJ) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"


$(CDK2_NATIVE_MONO_OBJ): $(CDK2_DIR)/src/modules/monotonic_counter/monotonic_counter.c $(CDK2_DIR)/include/uefi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_MONO_PE): $(CDK2_NATIVE_MONO_OBJ) $(CDK2_DIR)/src/modules/monotonic_counter/monotonic_counter.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 12 --entry monotonic_counter_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/src/modules/monotonic_counter/monotonic_counter.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"


ifeq ($(CONFIG_CDK2_NATIVE_CPU_IO2),y)

$(CDK2_NATIVE_CPU_IO2_OBJ): $(CDK2_DIR)/src/modules/cpu_io2/cpu_io2.c $(CDK2_DIR)/include/cdk2/cpu_io2.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_CPU_IO2_PE): $(CDK2_NATIVE_CPU_IO2_OBJ) $(CDK2_DIR)/src/modules/cpu_io2/cpu_io2.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_cpu_io2_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/src/modules/cpu_io2/cpu_io2.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"


endif

$(CDK2_NATIVE_DISK_IO_OBJ): $(CDK2_DIR)/src/modules/disk_io/disk_io.c $(CDK2_DIR)/include/cdk2/disk_io.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_DISK_IO_PE): $(CDK2_NATIVE_DISK_IO_OBJ) $(CDK2_DIR)/src/modules/disk_io/disk_io.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_disk_io_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/src/modules/disk_io/disk_io.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"


$(CDK2_NATIVE_SERIAL_IO_OBJ): $(CDK2_DIR)/src/modules/serial_io/serial_io.c \
		$(CDK2_DIR)/include/cdk2/serial_io.h \
		$(CDK2_DIR)/include/cdk2/hob.h \
		$(CDK2_DIR)/include/cdk2/hob_payload.h \
		$(CDK2_DIR)/include/pi/hob.h \
		$(CDK2_DIR)/include/cdk2/uart.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_SERIAL_IO_PE): $(CDK2_NATIVE_SERIAL_IO_OBJ) $(CDK2_DIR)/src/modules/serial_io/serial_io.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_serial_io_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/src/modules/serial_io/serial_io.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"


$(CDK2_NATIVE_RESET_SYSTEM_OBJ): $(CDK2_DIR)/src/modules/reset_system/reset_system.c $(CDK2_DIR)/include/cdk2/reset_system.h $(CDK2_DIR)/include/guid/acpi_board_info.h $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_RESET_SYSTEM_PE): $(CDK2_NATIVE_RESET_SYSTEM_OBJ) \
		$(CDK2_NATIVE_RESET_SYSTEM_DIAGNOSTIC_OBJ) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/reset_system/reset_system.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 12 --entry cdk2_reset_system_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/src/modules/reset_system/reset_system.ld" -o "$@" \
		$(CDK2_NATIVE_RESET_SYSTEM_OBJ) \
		$(CDK2_NATIVE_RESET_SYSTEM_DIAGNOSTIC_OBJ) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"


.PHONY: native-reset-system-diagnostic-parity
native-reset-system-diagnostic-parity:
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/reset-system-diagnostic-parity.tsv" 4 \
		"$(CDK2_DIR)/src/modules/reset_system"

native-check: native-reset-system-diagnostic-parity

$(CDK2_NATIVE_PCAT_RTC_OBJ): $(CDK2_DIR)/src/modules/pcat_rtc/pcat_rtc.c $(CDK2_DIR)/include/cdk2/pcat_rtc.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_PCAT_RTC_PE): $(CDK2_NATIVE_PCAT_RTC_OBJ) $(CDK2_DIR)/src/modules/pcat_rtc/pcat_rtc.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 12 --entry cdk2_pcat_rtc_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/src/modules/pcat_rtc/pcat_rtc.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"


.SECONDEXPANSION:

# The admitted profile disables TPM 1.2.  Remove its unused driver while
# retaining Tcg2Dxe until the native TPM 2.0 replacement is admitted.

$(CDK2_NATIVE_BUILD_DIR)/graphics-console-%.o: \
	$(CDK2_DIR)/src/modules/graphics_console/%.c \
	$(CDK2_DIR)/include/cdk2/graphics_console.h \
	$(CDK2_DIR)/include/cdk2/graphics_console_binding.h \
	$(CDK2_DIR)/include/cdk2/graphics_console_font.h \
	$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/graphics-console-font_data.o: \
		$(CDK2_DIR)/src/modules/graphics_console/font_data.inc

$(CDK2_NATIVE_GRAPHICS_CONSOLE_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_GRAPHICS_CONSOLE_PE): $(CDK2_NATIVE_GRAPHICS_CONSOLE_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_GRAPHICS_CONSOLE_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/graphics_console/graphics_console.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_graphics_console_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/graphics_console/graphics_console.ld" \
		-o "$@" $(CDK2_NATIVE_GRAPHICS_CONSOLE_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_GRAPHICS_CONSOLE_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


ifeq ($(CONFIG_CDK2_NATIVE_ENGLISH),y)
$(CDK2_NATIVE_ENGLISH_OBJ): $(CDK2_DIR)/src/modules/english/english.c $(CDK2_DIR)/include/cdk2/english.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_ENGLISH_PE): $(CDK2_NATIVE_ENGLISH_OBJ) $(CDK2_DIR)/src/modules/english/english.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_english_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/src/modules/english/english.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"


endif

$(CDK2_NATIVE_COREBOOT_IMAGE): $(CDK2_NATIVE_COREBOOT_OBJS) \
		$$(CDK2_NATIVE_COREBOOT_PAYLOAD_DEPS) \
		$$(CDK2_NATIVE_COREBOOT_LINKER_SCRIPT) \
		$$(CDK2_NATIVE_DIRECT_IMAGE_RUNTIME_DEPS) | native-source-payload-admission
	@$(CDK2_NATIVE_CC) \
		$(CDK2_NATIVE_LINK_BASE_FLAGS) -Wl,-Map,"$(CDK2_NATIVE_COREBOOT_IMAGE_MAP)" \
		-Wl,-T,"$(CDK2_NATIVE_COREBOOT_LINKER_SCRIPT)" \
		$(CDK2_NATIVE_LINK_POST_MAP_FLAGS) -Wl,-e,cdk2_coreboot_entry32 \
		-o "$@" $(CDK2_NATIVE_COREBOOT_LINK_INPUTS) \
		$(CDK2_NATIVE_COREBOOT_PAYLOAD_INPUTS)
	@"$(CDK2_NATIVE_ELF_CHECK)" --entry cdk2_coreboot_entry32 \
		--require-direct-images "$@"

-include $(CDK2_NATIVE_BUILD_DIR)/watchdog.d

$(CDK2_NATIVE_CPU_IO2_TEST): $(CDK2_DIR)/tests/cpu_io2_test.c $(CDK2_DIR)/src/modules/cpu_io2/cpu_io2.c $(CDK2_DIR)/include/cdk2/cpu_io2.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<"

$(CDK2_NATIVE_DISK_IO_TEST): $(CDK2_DIR)/tests/disk_io_test.c $(CDK2_DIR)/src/modules/disk_io/disk_io.c $(CDK2_DIR)/include/cdk2/disk_io.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -DCDK2_DEBUG \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<"

$(CDK2_NATIVE_FAT_TEST): $(CDK2_DIR)/tests/fat_test.c $(CDK2_DIR)/src/modules/fat/fat.c $(CDK2_DIR)/include/cdk2/fat.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" "$(CDK2_DIR)/tests/fat_test.c" \
		"$(CDK2_DIR)/src/modules/fat/fat.c"

$(CDK2_NATIVE_BUILD_DIR)/cdk2-fat-binding-test: $(CDK2_DIR)/tests/fat_binding_test.c $(CDK2_DIR)/src/modules/fat/binding.c $(CDK2_DIR)/src/modules/fat/protocol.c $(CDK2_DIR)/src/modules/fat/fat.c $(CDK2_DIR)/include/cdk2/fat_binding.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" "$(CDK2_DIR)/tests/fat_binding_test.c" \
		"$(CDK2_DIR)/src/modules/fat/binding.c" "$(CDK2_DIR)/src/modules/fat/protocol.c" \
		"$(CDK2_DIR)/src/modules/fat/fat.c"

$(CDK2_NATIVE_BUILD_DIR)/cdk2-fat-protocol-abi-test: $(CDK2_DIR)/tests/fat_protocol_abi_test.c $(CDK2_DIR)/src/modules/fat/protocol.c $(CDK2_DIR)/src/modules/fat/binding.c $(CDK2_DIR)/src/modules/fat/fat.c $(CDK2_DIR)/include/cdk2/fat_binding.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" "$(CDK2_DIR)/tests/fat_protocol_abi_test.c" \
		"$(CDK2_DIR)/src/modules/fat/protocol.c" \
		"$(CDK2_DIR)/src/modules/fat/binding.c" "$(CDK2_DIR)/src/modules/fat/fat.c"

$(CDK2_NATIVE_BUILD_DIR)/cdk2-fat-entry-test: $(CDK2_DIR)/tests/fat_entry_test.c \
		$(CDK2_DIR)/src/modules/fat/entry.c \
		$(CDK2_DIR)/src/modules/fat/binding.c \
		$(CDK2_DIR)/src/modules/fat/protocol.c \
		$(CDK2_DIR)/src/modules/fat/fat.c \
		$(CDK2_DIR)/src/lib/diagnostic.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $^


$(CDK2_NATIVE_SERIAL_IO_TEST): $(CDK2_DIR)/tests/serial_io_test.c \
		$(CDK2_DIR)/src/modules/serial_io/serial_io.c \
		$(CDK2_DIR)/include/cdk2/serial_io.h \
		$(CDK2_DIR)/include/cdk2/hob.h \
		$(CDK2_DIR)/include/cdk2/hob_payload.h \
		$(CDK2_DIR)/include/pi/hob.h \
		$(CDK2_DIR)/include/cdk2/uart.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<"

$(CDK2_NATIVE_RESET_SYSTEM_TEST): $(CDK2_DIR)/tests/reset_system_test.c \
		$(CDK2_DIR)/src/modules/reset_system/reset_system.c \
		$(CDK2_DIR)/src/modules/reset_system/diagnostic.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/tests/diagnostic_runtime_guard.h \
		$(CDK2_DIR)/include/cdk2/reset_system.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -D_GNU_SOURCE \
		-DCDK2_DIAGNOSTIC -DCDK2_DIAG_RUNTIME_TRANSITION_TEST \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		$(CDK2_DIR)/src/modules/reset_system/diagnostic.c \
		$(CDK2_DIR)/src/lib/diagnostic.c

$(CDK2_NATIVE_PCAT_RTC_TEST): $(CDK2_DIR)/tests/pcat_rtc_test.c $(CDK2_DIR)/src/modules/pcat_rtc/pcat_rtc.c $(CDK2_DIR)/include/cdk2/pcat_rtc.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<"

$(CDK2_NATIVE_LOCAL_APIC_TIMER_TEST): $(CDK2_DIR)/tests/local_apic_timer_test.c $(CDK2_DIR)/src/modules/local_apic_timer/local_apic_timer.c $(CDK2_DIR)/include/cdk2/local_apic_timer.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/local_apic_timer_test.c" "$(CDK2_DIR)/src/modules/local_apic_timer/local_apic_timer.c"

$(CDK2_NATIVE_LOCAL_APIC_TIMER_DRIVER_TEST): $(CDK2_DIR)/tests/local_apic_timer_driver_test.c $(CDK2_DIR)/src/modules/local_apic_timer/driver.c $(CDK2_DIR)/include/cdk2/local_apic_timer.h $(CDK2_DIR)/include/cdk2/x64_interrupt_context.h $(CDK2_DIR)/include/guid/local_apic_timer_info.h $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -MMD -MP \
		-MF "$@.d" -MT "$@" -DCDK2_LOCAL_APIC_TIMER_DRIVER_TEST \
		$(CDK2_NATIVE_INCLUDES) -o "$@" \
		"$(CDK2_DIR)/tests/local_apic_timer_driver_test.c" \
		"$(CDK2_DIR)/src/modules/local_apic_timer/driver.c"

$(CDK2_NATIVE_GRAPHICS_CONSOLE_TEST): $(CDK2_DIR)/tests/graphics_console_binding_test.c $(CDK2_DIR)/src/modules/graphics_console/binding.c $(CDK2_DIR)/src/modules/graphics_console/model.c $(CDK2_DIR)/src/modules/graphics_console/font_data.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar $(CDK2_NATIVE_INCLUDES) \
		-o "$@" "$(CDK2_DIR)/tests/graphics_console_binding_test.c" \
		"$(CDK2_DIR)/src/modules/graphics_console/binding.c" \
		"$(CDK2_DIR)/src/modules/graphics_console/model.c" \
		"$(CDK2_DIR)/src/modules/graphics_console/font_data.c"

$(CDK2_NATIVE_GRAPHICS_ENTRY_TEST): $(CDK2_DIR)/tests/graphics_console_entry_test.c $(CDK2_DIR)/src/modules/graphics_console/entry.c $(CDK2_DIR)/src/modules/graphics_console/binding.c $(CDK2_DIR)/src/modules/graphics_console/model.c $(CDK2_DIR)/src/modules/graphics_console/font_data.c $(CDK2_DIR)/src/lib/diagnostic.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar $(CDK2_NATIVE_INCLUDES) \
		-o "$@" "$(CDK2_DIR)/tests/graphics_console_entry_test.c" \
		"$(CDK2_DIR)/src/modules/graphics_console/entry.c" \
		"$(CDK2_DIR)/src/modules/graphics_console/binding.c" \
		"$(CDK2_DIR)/src/modules/graphics_console/model.c" \
		"$(CDK2_DIR)/src/modules/graphics_console/font_data.c" \
		"$(CDK2_DIR)/src/lib/diagnostic.c"

$(CDK2_NATIVE_BUILD_DIR)/graphics-diagnostic-debug-test: \
		$(CDK2_DIR)/tests/graphics_console_diagnostic_test.c \
		$(CDK2_DIR)/src/modules/graphics_console/diagnostic.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-fsanitize=address,undefined -DCDK2_DEBUG $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $^

$(CDK2_NATIVE_BUILD_DIR)/graphics-diagnostic-release-test: \
		$(CDK2_DIR)/tests/graphics_console_diagnostic_test.c \
		$(CDK2_DIR)/src/modules/graphics_console/diagnostic.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-fsanitize=address,undefined -DCDK2_DIAGNOSTIC $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $^

$(CDK2_NATIVE_CAPSULE_RUNTIME_TEST): $(CDK2_DIR)/tests/capsule_runtime_test.c $(CDK2_DIR)/src/modules/capsule_runtime/capsule_runtime.c $(CDK2_DIR)/include/cdk2/capsule_runtime.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/capsule_runtime_test.c" "$(CDK2_DIR)/src/modules/capsule_runtime/capsule_runtime.c"

$(CDK2_NATIVE_CAPSULE_DISK_TEST): $(CDK2_DIR)/tests/capsule_disk_test.c \
		$(CDK2_DIR)/src/lib/capsule_disk.c \
		$(CDK2_DIR)/include/cdk2/capsule_disk.h \
		$(CDK2_DIR)/include/cdk2/english.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -o "$@" \
		"$(CDK2_DIR)/tests/capsule_disk_test.c" \
		"$(CDK2_DIR)/src/lib/capsule_disk.c"

$(CDK2_NATIVE_CAPSULE_RUNTIME_ABI_TEST): $(CDK2_DIR)/tests/capsule_runtime_abi_test.c \
		$(CDK2_DIR)/src/modules/capsule_runtime/capsule_runtime.c \
		$(CDK2_DIR)/src/modules/capsule_runtime/capsule_runtime_abi.c \
		$(CDK2_DIR)/include/cdk2/capsule_runtime_abi.h \
		$(CDK2_DIR)/include/cdk2/capsule_runtime_entry.h \
		$(CDK2_DIR)/include/cdk2/esrt_abi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/capsule_runtime_abi_test.c" "$(CDK2_DIR)/src/modules/capsule_runtime/capsule_runtime.c" "$(CDK2_DIR)/src/modules/capsule_runtime/capsule_runtime_abi.c"

$(CDK2_NATIVE_CAPSULE_RUNTIME_ENTRY_TEST): $(CDK2_DIR)/tests/capsule_runtime_entry_test.c $(CDK2_DIR)/src/modules/capsule_runtime/entry.c $(CDK2_DIR)/src/modules/capsule_runtime/capsule_runtime.c $(CDK2_DIR)/src/modules/capsule_runtime/capsule_runtime_abi.c $(CDK2_DIR)/src/lib/diagnostic.c $(CDK2_DIR)/tests/diagnostic_runtime_guard.h $(CDK2_DIR)/include/cdk2/capsule_runtime_entry.h $(CDK2_DIR)/include/cdk2/capsule_delivery_policy.h $(CDK2_DIR)/tests/capsule_delivery_policy_fixture.h $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,-D_GNU_SOURCE -DCDK2_CAPSULE_ENTRY_TEST -DCDK2_DEBUG -DCDK2_DIAGNOSTIC -DCDK2_DIAG_RUNTIME_TRANSITION_TEST $(CDK2_NATIVE_INCLUDES),$(filter %.c,$^))

$(CDK2_NATIVE_CAPSULE_RUNTIME_OBJ): $(CDK2_DIR)/src/modules/capsule_runtime/capsule_runtime.c $(CDK2_DIR)/include/cdk2/capsule_runtime.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args $(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_CAPSULE_RUNTIME_ABI_OBJ): $(CDK2_DIR)/src/modules/capsule_runtime/capsule_runtime_abi.c $(CDK2_DIR)/include/cdk2/capsule_runtime_abi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args $(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_CAPSULE_RUNTIME_ENTRY_OBJ): $(CDK2_DIR)/src/modules/capsule_runtime/entry.c $(CDK2_DIR)/include/cdk2/capsule_runtime_entry.h $(CDK2_DIR)/include/cdk2/capsule_runtime_abi.h $(CDK2_DIR)/include/cdk2/capsule_runtime.h $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args $(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_CAPSULE_RUNTIME_PE): $(CDK2_NATIVE_CAPSULE_RUNTIME_LINK_INPUTS) $(CDK2_DIR)/src/modules/capsule_runtime/capsule_runtime.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 12 --entry cdk2_capsule_runtime_entry --image-base 0 --section-alignment 0x1000 --file-alignment 0x200 --build-id=none --no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/src/modules/capsule_runtime/capsule_runtime.ld" -o "$@" $(CDK2_NATIVE_CAPSULE_RUNTIME_LINK_INPUTS)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 12 --file-alignment 0x200 "$@"

$(CDK2_NATIVE_CAPSULE_RUNTIME_QEMU_OBJ): $(CDK2_DIR)/tests/capsule_runtime_qemu.c $(CDK2_DIR)/include/cdk2/capsule_runtime_entry.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_CAPSULE_RUNTIME_QEMU_PE): $(CDK2_NATIVE_CAPSULE_RUNTIME_QEMU_OBJ) $(CDK2_NATIVE_CAPSULE_RUNTIME_LINK_INPUTS) $(CDK2_DIR)/tests/capsule_runtime_qemu.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry capsule_runtime_qemu_entry --image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 --build-id=none --no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/tests/capsule_runtime_qemu.ld" -o "$@" $(CDK2_NATIVE_CAPSULE_RUNTIME_QEMU_OBJ) $(CDK2_NATIVE_CAPSULE_RUNTIME_LINK_INPUTS)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

$(CDK2_NATIVE_TCG2_TRANSPORT_TEST): $(CDK2_DIR)/tests/tcg2_transport_test.c $(CDK2_DIR)/src/modules/tcg2/tcg2_transport.c $(CDK2_DIR)/include/cdk2/tcg2_transport.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/tcg2_transport_test.c" "$(CDK2_DIR)/src/modules/tcg2/tcg2_transport.c"

$(CDK2_NATIVE_TCG2_COMMANDS_TEST): $(CDK2_DIR)/tests/tcg2_commands_test.c $(CDK2_DIR)/src/modules/tcg2/tcg2_commands.c $(CDK2_DIR)/src/modules/tcg2/tcg2_transport.c $(CDK2_DIR)/include/cdk2/tcg2_commands.h $(CDK2_DIR)/include/cdk2/tcg2_transport.h $(CDK2_SOFTWARE_HASH_SRCS) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) $(CDK2_SOFTWARE_HASH_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/tcg2_commands_test.c" "$(CDK2_DIR)/src/modules/tcg2/tcg2_commands.c" "$(CDK2_DIR)/src/modules/tcg2/tcg2_transport.c" $(CDK2_SOFTWARE_HASH_SRCS)

$(CDK2_NATIVE_TCG2_EVENT_LOG_TEST): $(CDK2_DIR)/tests/tcg2_event_log_test.c $(CDK2_DIR)/src/modules/tcg2/tcg2_event_log.c $(CDK2_DIR)/include/cdk2/tcg2_event_log.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/tcg2_event_log_test.c" "$(CDK2_DIR)/src/modules/tcg2/tcg2_event_log.c"

$(CDK2_NATIVE_TCG2_MEASURE_TEST): $(CDK2_DIR)/tests/tcg2_measure_test.c $(CDK2_DIR)/src/modules/tcg2/tcg2_measure.c $(CDK2_DIR)/src/modules/tcg2/tcg2_event_log.c $(CDK2_DIR)/include/cdk2/tcg2_measure.h $(CDK2_DIR)/include/cdk2/tcg2_event_log.h $(CDK2_SOFTWARE_HASH_SRCS) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) $(CDK2_SOFTWARE_HASH_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/tcg2_measure_test.c" "$(CDK2_DIR)/src/modules/tcg2/tcg2_measure.c" "$(CDK2_DIR)/src/modules/tcg2/tcg2_event_log.c" $(CDK2_SOFTWARE_HASH_SRCS)

$(CDK2_NATIVE_TCG2_SERVICE_TEST): $(CDK2_DIR)/tests/tcg2_service_test.c $(CDK2_DIR)/src/modules/tcg2/tcg2_service.c $(CDK2_DIR)/src/modules/tcg2/tcg2_measure.c $(CDK2_DIR)/src/modules/tcg2/tcg2_event_log.c $(CDK2_DIR)/include/cdk2/tcg2_service.h $(CDK2_SOFTWARE_HASH_SRCS) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) $(CDK2_SOFTWARE_HASH_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/tcg2_service_test.c" "$(CDK2_DIR)/src/modules/tcg2/tcg2_service.c" "$(CDK2_DIR)/src/modules/tcg2/tcg2_measure.c" "$(CDK2_DIR)/src/modules/tcg2/tcg2_event_log.c" $(CDK2_SOFTWARE_HASH_SRCS)

$(CDK2_NATIVE_TCG2_ENTRY_TEST): $(CDK2_DIR)/tests/tcg2_entry_test.c $(CDK2_DIR)/src/modules/tcg2/tcg2_entry.c $(CDK2_DIR)/include/cdk2/tcg2_entry.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/tcg2_entry_test.c" "$(CDK2_DIR)/src/modules/tcg2/tcg2_entry.c"

$(CDK2_NATIVE_TPM2_ACPI_TABLE_TEST): $(CDK2_DIR)/tests/tpm2_acpi_table_test.c $(CDK2_DIR)/src/modules/tpm2_acpi_table/tpm2_acpi_table.c $(CDK2_DIR)/src/modules/tpm2_acpi_table/driver.c $(CDK2_DIR)/src/lib/diagnostic.c $(CDK2_DIR)/include/cdk2/tpm2_acpi_table.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/tpm2_acpi_table_test.c" "$(CDK2_DIR)/src/modules/tpm2_acpi_table/tpm2_acpi_table.c" "$(CDK2_DIR)/src/modules/tpm2_acpi_table/driver.c" "$(CDK2_DIR)/src/lib/diagnostic.c"

$(CDK2_NATIVE_ESRT_TEST): $(CDK2_DIR)/tests/esrt_test.c $(CDK2_DIR)/src/modules/esrt/esrt.c $(CDK2_DIR)/include/cdk2/esrt.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/esrt_test.c" "$(CDK2_DIR)/src/modules/esrt/esrt.c"

$(CDK2_NATIVE_ESRT_ENTRY_TEST): $(CDK2_DIR)/tests/esrt_entry_test.c $(CDK2_DIR)/src/modules/esrt/entry.c $(CDK2_DIR)/src/modules/esrt/esrt.c $(CDK2_DIR)/include/cdk2/esrt.h $(CDK2_DIR)/include/cdk2/esrt_abi.h $(CDK2_DIR)/include/cdk2/capsule_runtime_abi.h $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/esrt_entry_test.c" "$(CDK2_DIR)/src/modules/esrt/esrt.c"

$(CDK2_NATIVE_ESRT_OBJ): $(CDK2_DIR)/src/modules/esrt/esrt.c $(CDK2_DIR)/include/cdk2/esrt.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args $(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_ESRT_ENTRY_OBJ): $(CDK2_DIR)/src/modules/esrt/entry.c $(CDK2_DIR)/include/cdk2/esrt.h $(CDK2_DIR)/include/cdk2/esrt_abi.h $(CDK2_DIR)/include/cdk2/capsule_runtime_abi.h $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args -fshort-wchar $(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_ESRT_PE): $(CDK2_NATIVE_ESRT_OBJ) $(CDK2_NATIVE_ESRT_ENTRY_OBJ) $(CDK2_DIR)/src/modules/esrt/esrt.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_esrt_entry --image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 --build-id=none --no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/src/modules/esrt/esrt.ld" -o "$@" $(CDK2_NATIVE_ESRT_OBJ) $(CDK2_NATIVE_ESRT_ENTRY_OBJ)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_QEMU_TEST_FMP_TEST): $(CDK2_DIR)/tests/qemu_test_fmp_core_test.c \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/core.c \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/sha256.c \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/rsa2048.c \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/protocol.c \
		$(CDK2_DIR)/include/cdk2/qemu_test_fmp.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		$^ -o "$@"

$(CDK2_NATIVE_QEMU_TEST_FMP_ENTRY_TEST): \
		$(CDK2_DIR)/tests/qemu_test_fmp_entry_test.c \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/core.c \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/sha256.c \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/rsa2048.c \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/protocol.c \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/entry.c \
		$(CDK2_DIR)/include/cdk2/qemu_test_fmp.h $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) $^ -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/qemu-test-fmp-%.o: \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/%.c \
		$(CDK2_DIR)/include/cdk2/qemu_test_fmp.h $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) \
		-c "$<" -o "$@"

$(CDK2_NATIVE_QEMU_TEST_FMP_PE): $(CDK2_NATIVE_QEMU_TEST_FMP_OBJS) \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/qemu_test_fmp.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 \
		--entry cdk2_qemu_test_fmp_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/qemu_test_fmp/qemu_test_fmp.ld" \
		-o "$@" $(CDK2_NATIVE_QEMU_TEST_FMP_OBJS)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_QEMU_TEST_FMP_ARTIFACT_PE): $(CDK2_NATIVE_QEMU_TEST_FMP_OBJS) \
		$(CDK2_DIR)/src/modules/qemu_test_fmp/qemu_test_fmp.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 \
		--entry cdk2_qemu_test_fmp_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/qemu_test_fmp/qemu_test_fmp.ld" \
		-o "$@" $(CDK2_NATIVE_QEMU_TEST_FMP_OBJS)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_DEVICE_PATH_TEST): $(CDK2_DIR)/tests/device_path_test.c $(CDK2_DIR)/src/modules/device_path/device_path.c $(CDK2_DIR)/src/modules/device_path/device_path_to_text.c $(CDK2_DIR)/src/modules/device_path/device_path_from_text.c $(CDK2_DIR)/include/cdk2/device_path.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/device_path_test.c" "$(CDK2_DIR)/src/modules/device_path/device_path.c" "$(CDK2_DIR)/src/modules/device_path/device_path_to_text.c" "$(CDK2_DIR)/src/modules/device_path/device_path_from_text.c"

$(CDK2_NATIVE_DEVICE_PATH_DRIVER_TEST): $(CDK2_DIR)/tests/device_path_driver_test.c $(CDK2_DIR)/src/modules/device_path/driver.c $(CDK2_DIR)/src/modules/device_path/device_path.c $(CDK2_DIR)/src/modules/device_path/device_path_to_text.c $(CDK2_DIR)/src/modules/device_path/device_path_from_text.c $(CDK2_DIR)/include/cdk2/device_path.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/device_path_driver_test.c" "$(CDK2_DIR)/src/modules/device_path/driver.c" "$(CDK2_DIR)/src/modules/device_path/device_path.c" "$(CDK2_DIR)/src/modules/device_path/device_path_to_text.c" "$(CDK2_DIR)/src/modules/device_path/device_path_from_text.c"

$(CDK2_NATIVE_PARTITION_GPT_TEST): $(CDK2_DIR)/tests/partition_gpt_test.c $(CDK2_DIR)/src/modules/partition/partition.c $(CDK2_DIR)/include/cdk2/partition.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<" "$(CDK2_DIR)/src/modules/partition/partition.c"

$(CDK2_NATIVE_PARTITION_MBR_TEST): $(CDK2_DIR)/tests/partition_mbr_test.c $(CDK2_DIR)/src/modules/partition/partition.c $(CDK2_DIR)/include/cdk2/partition.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<" "$(CDK2_DIR)/src/modules/partition/partition.c"

$(CDK2_NATIVE_PARTITION_EL_TORITO_TEST): $(CDK2_DIR)/tests/partition_el_torito_test.c $(CDK2_DIR)/src/modules/partition/partition.c $(CDK2_DIR)/include/cdk2/partition.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<" "$(CDK2_DIR)/src/modules/partition/partition.c"

$(CDK2_NATIVE_PARTITION_UDF_TEST): $(CDK2_DIR)/tests/partition_udf_test.c $(CDK2_DIR)/src/modules/partition/partition.c $(CDK2_DIR)/include/cdk2/partition.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<" "$(CDK2_DIR)/src/modules/partition/partition.c"

$(CDK2_NATIVE_PARTITION_CHILD_TEST): $(CDK2_DIR)/tests/partition_child_test.c $(CDK2_DIR)/src/modules/partition/partition_child.c $(CDK2_DIR)/include/cdk2/partition.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<" "$(CDK2_DIR)/src/modules/partition/partition_child.c"

$(CDK2_NATIVE_PARTITION_DRIVER_TEST): $(CDK2_DIR)/tests/partition_driver_test.c $(CDK2_DIR)/src/modules/partition/partition_driver.c $(CDK2_DIR)/src/modules/partition/partition_child.c $(CDK2_DIR)/src/modules/partition/partition.c $(CDK2_DIR)/src/lib/diagnostic.c $(CDK2_DIR)/include/cdk2/partition.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<" "$(CDK2_DIR)/src/modules/partition/partition_driver.c" "$(CDK2_DIR)/src/modules/partition/partition_child.c" "$(CDK2_DIR)/src/modules/partition/partition.c" "$(CDK2_DIR)/src/lib/diagnostic.c"

$(CDK2_NATIVE_TCG2_DIAGNOSTIC_TEST): $(CDK2_DIR)/tests/tcg2_diagnostic_test.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -o "$@" "$<"

$(CDK2_NATIVE_TCG2_DIAGNOSTIC_COALESCE_TEST): $(CDK2_DIR)/tests/tcg2_diagnostic_coalesce_test.c $(CDK2_DIR)/src/modules/tcg2/diagnostic.c $(CDK2_DIR)/include/cdk2/tcg2_service.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -DCDK2_DIAGNOSTIC -DCDK2_TCG2_DIAG_TEST -o "$@" "$(CDK2_DIR)/tests/tcg2_diagnostic_coalesce_test.c" "$(CDK2_DIR)/src/modules/tcg2/diagnostic.c"

$(CDK2_NATIVE_TCG2_PLATFORM_HOB_TEST): $(CDK2_DIR)/tests/tcg2_platform_hob_test.c $(CDK2_DIR)/src/modules/tcg2/driver.c $(CDK2_DIR)/src/lib/tpm2_acpi_hob.c $(CDK2_DIR)/include/cdk2/hob_payload.h $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -Wno-missing-field-initializers -ffunction-sections -fdata-sections -DCDK2_HOST_TEST $(CDK2_NATIVE_INCLUDES) -Wl,--gc-sections -o "$@" "$(CDK2_DIR)/tests/tcg2_platform_hob_test.c" "$(CDK2_DIR)/src/modules/tcg2/driver.c" "$(CDK2_DIR)/src/lib/tpm2_acpi_hob.c"

$(CDK2_NATIVE_SOFTWARE_HASH_TEST): $(CDK2_DIR)/tests/software_hash_test.c \
		$(CDK2_DIR)/src/lib/tcg_hash/software_hash.c \
		$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha1.c \
		$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha256.c \
		$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha512.c \
		$(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/lib/crypto/sm3.c | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) \
		-I"$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot" \
		-I"$(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/include",\
		"$(CDK2_DIR)/tests/software_hash_test.c" \
		"$(CDK2_DIR)/src/lib/tcg_hash/software_hash.c" \
		"$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha1.c" \
		"$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha256.c" \
		"$(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha512.c" \
		"$(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/lib/crypto/sm3.c")

FORCE:

$(CDK2_NATIVE_SERVICE_TEST): $(CDK2_NATIVE_DIR)/payload.c $(CDK2_NATIVE_DIR)/services.c $(CDK2_NATIVE_DIR)/services.h $(CDK2_NATIVE_DIR)/context.h $(CDK2_NATIVE_DIR)/services_test.c $(CDK2_DIR)/src/lib/diagnostic.c | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES) $(CDK2_NATIVE_HOST_LDFLAGS),\
		"$(CDK2_NATIVE_DIR)/payload.c" "$(CDK2_NATIVE_DIR)/services.c" \
		"$(CDK2_NATIVE_DIR)/services_test.c" "$(CDK2_DIR)/src/lib/diagnostic.c")

$(CDK2_NATIVE_COREBOOT_TEST): $(CDK2_NATIVE_DIR)/coreboot.c \
		$(CDK2_NATIVE_DIR)/coreboot_dma_handoff.c \
		$(CDK2_DIR)/include/cdk2/dma_handoff.h \
		$(CDK2_DIR)/src/lib/boot_private_buffer.c \
		$(CDK2_DIR)/include/cdk2/boot_private_buffer.h \
		$(CDK2_DIR)/src/lib/image_policy_snapshot.c \
		$(CDK2_DIR)/include/cdk2/image_policy_snapshot.h \
		$(CDK2_NATIVE_DIR)/coreboot.h $(CDK2_NATIVE_DIR)/coreboot_checksum.c \
		$(CDK2_NATIVE_DIR)/coreboot_checksum.h \
		$(CDK2_NATIVE_DIR)/coreboot_resource.c \
		$(CDK2_NATIVE_DIR)/coreboot_resource.h \
		$(CDK2_NATIVE_DIR)/coreboot_hobs.c \
		$(CDK2_DIR)/src/modules/pci_host_bridge/model.c \
		$(CDK2_NATIVE_DIR)/coreboot_hobs.h \
		$(CDK2_NATIVE_DIR)/coreboot_handoff.c \
		$(CDK2_DIR)/src/lib/boot_logo.c \
		$(CDK2_NATIVE_DIR)/early_splash.c $(CDK2_NATIVE_DIR)/services.c \
		$(CDK2_NATIVE_DIR)/services.h $(CDK2_NATIVE_DIR)/coreboot_test.c \
		$(CDK2_DIR)/src/lib/linear_boot.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/src/lib/direct_image_table.c $(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/include/cdk2/direct_image_table.h \
		$(CDK2_DIR)/include/coreboot_tables.h \
		$(CDK2_DIR)/include/guid/deadline_tsc_info.h \
		$(CDK2_DIR)/include/cdk2/pci_topology_handoff.h \
		$(CDK2_CONFIG_HEADER) FORCE | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-DCDK2_COREBOOT_BACKEND_TEST -DCDK2_DIAG_UNIT_TEST \
		-ffunction-sections -fdata-sections \
		-Wl,--gc-sections -MMD -MP \
		-MF "$(CDK2_NATIVE_BUILD_DIR)/coreboot-test.d" -MT "$@" \
		$(CDK2_NATIVE_INCLUDES) -I$(CDK2_NATIVE_DIR) \
		$(CDK2_NATIVE_HOST_LDFLAGS) -o "$@" \
		"$(CDK2_NATIVE_DIR)/coreboot.c" \
		"$(CDK2_DIR)/src/lib/boot_private_buffer.c" \
		"$(CDK2_DIR)/src/lib/image_policy_snapshot.c" \
		"$(CDK2_NATIVE_DIR)/coreboot_dma_handoff.c" \
		"$(CDK2_NATIVE_DIR)/coreboot_checksum.c" \
		"$(CDK2_NATIVE_DIR)/coreboot_resource.c" \
		"$(CDK2_NATIVE_DIR)/coreboot_hobs.c" \
		"$(CDK2_DIR)/src/modules/pci_host_bridge/model.c" \
		"$(CDK2_NATIVE_DIR)/coreboot_handoff.c" \
		"$(CDK2_DIR)/src/lib/boot_logo.c" \
		"$(CDK2_NATIVE_DIR)/early_splash.c" \
		"$(CDK2_NATIVE_DIR)/services.c" \
		"$(CDK2_DIR)/src/lib/linear_boot.c" \
		"$(CDK2_DIR)/src/lib/diagnostic.c" \
		"$(CDK2_DIR)/src/lib/direct_image_table.c" \
		$(CDK2_PE_IMAGE_VIEW_SRC) \
		"$(CDK2_NATIVE_DIR)/coreboot_test.c"


$(CDK2_NATIVE_METRONOME_TEST): $(CDK2_DIR)/tests/metronome_test.c \
		$(CDK2_DIR)/src/modules/metronome/metronome.c \
		$(CDK2_DIR)/include/cdk2/metronome.h \
		$(CDK2_DIR)/include/guid/acpi_board_info.h \
		$(CDK2_DIR)/include/guid/tsc_info.h $(CDK2_DIR)/include/pi/hob.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<"

$(CDK2_NATIVE_SCSI_BUS_TEST): $(CDK2_DIR)/tests/scsi_bus_test.c $(CDK2_DIR)/src/modules/scsi_bus/model.c $(CDK2_DIR)/include/cdk2/scsi_bus.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES),\
		$(CDK2_DIR)/tests/scsi_bus_test.c $(CDK2_DIR)/src/modules/scsi_bus/model.c)

$(CDK2_NATIVE_SCSI_DISK_TEST): $(CDK2_DIR)/tests/scsi_disk_model_test.c \
		$(CDK2_DIR)/src/modules/scsi_disk/model.c \
		$(CDK2_DIR)/include/cdk2/scsi_disk.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/scsi_disk_model_test.c \
		$(CDK2_DIR)/src/modules/scsi_disk/model.c

$(CDK2_NATIVE_SCSI_DISK_IO_TEST): $(CDK2_DIR)/tests/scsi_disk_io_test.c \
		$(CDK2_DIR)/src/modules/scsi_disk/model.c \
		$(CDK2_DIR)/src/modules/scsi_disk/io.c \
		$(CDK2_DIR)/include/cdk2/scsi_disk.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/scsi_disk_io_test.c \
		$(CDK2_DIR)/src/modules/scsi_disk/model.c \
		$(CDK2_DIR)/src/modules/scsi_disk/io.c

$(CDK2_NATIVE_SCSI_DISK_ASYNC_TEST): $(CDK2_DIR)/tests/scsi_disk_async_test.c \
		$(CDK2_DIR)/src/modules/scsi_disk/model.c \
		$(CDK2_DIR)/src/modules/scsi_disk/io.c \
		$(CDK2_DIR)/src/modules/scsi_disk/async.c \
		$(CDK2_DIR)/include/cdk2/scsi_disk.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/scsi_disk_async_test.c \
		$(CDK2_DIR)/src/modules/scsi_disk/model.c \
		$(CDK2_DIR)/src/modules/scsi_disk/io.c \
		$(CDK2_DIR)/src/modules/scsi_disk/async.c

$(CDK2_NATIVE_SCSI_DISK_BLOCK_TEST): $(CDK2_DIR)/tests/scsi_disk_block_test.c \
		$(CDK2_DIR)/src/modules/scsi_disk/model.c \
		$(CDK2_DIR)/src/modules/scsi_disk/io.c \
		$(CDK2_DIR)/src/modules/scsi_disk/async.c \
		$(CDK2_DIR)/src/modules/scsi_disk/block.c \
		$(CDK2_DIR)/include/cdk2/scsi_disk.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/scsi_disk_block_test.c \
		$(CDK2_DIR)/src/modules/scsi_disk/model.c \
		$(CDK2_DIR)/src/modules/scsi_disk/io.c \
		$(CDK2_DIR)/src/modules/scsi_disk/async.c \
		$(CDK2_DIR)/src/modules/scsi_disk/block.c

$(CDK2_NATIVE_SCSI_DISK_BINDING_TEST): $(CDK2_DIR)/tests/scsi_disk_binding_test.c \
		$(CDK2_DIR)/src/modules/scsi_disk/model.c \
		$(CDK2_DIR)/src/modules/scsi_disk/async.c \
		$(CDK2_DIR)/src/modules/scsi_disk/block.c \
		$(CDK2_DIR)/src/modules/scsi_disk/disk_info.c \
		$(CDK2_DIR)/src/modules/scsi_disk/io.c \
		$(CDK2_DIR)/src/modules/scsi_disk/binding.c \
		$(CDK2_DIR)/include/cdk2/scsi_disk.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" \
		$(CDK2_DIR)/tests/scsi_disk_binding_test.c \
		$(CDK2_DIR)/src/modules/scsi_disk/model.c \
		$(CDK2_DIR)/src/modules/scsi_disk/async.c \
		$(CDK2_DIR)/src/modules/scsi_disk/block.c \
		$(CDK2_DIR)/src/modules/scsi_disk/disk_info.c \
		$(CDK2_DIR)/src/modules/scsi_disk/io.c \
		$(CDK2_DIR)/src/modules/scsi_disk/binding.c

$(CDK2_NATIVE_SCSI_DISK_BACKEND_TEST): $(CDK2_DIR)/tests/scsi_disk_backend_test.c \
		$(CDK2_DIR)/src/modules/scsi_disk/model.c \
		$(CDK2_DIR)/src/modules/scsi_disk/backend.c \
		$(CDK2_DIR)/include/cdk2/scsi_disk.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" \
		$(CDK2_DIR)/tests/scsi_disk_backend_test.c \
		$(CDK2_DIR)/src/modules/scsi_disk/model.c \
		$(CDK2_DIR)/src/modules/scsi_disk/backend.c

$(CDK2_NATIVE_SCSI_DISK_ENTRY_TEST): $(CDK2_DIR)/tests/scsi_disk_entry_test.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/src/modules/scsi_disk/model.c \
		$(CDK2_DIR)/src/modules/scsi_disk/io.c \
		$(CDK2_DIR)/src/modules/scsi_disk/async.c \
		$(CDK2_DIR)/src/modules/scsi_disk/block.c \
		$(CDK2_DIR)/src/modules/scsi_disk/disk_info.c \
		$(CDK2_DIR)/src/modules/scsi_disk/binding.c \
		$(CDK2_DIR)/src/modules/scsi_disk/backend.c \
		$(CDK2_DIR)/src/modules/scsi_disk/entry.c \
		$(CDK2_DIR)/include/cdk2/scsi_disk.h \
		$(CDK2_DIR)/include/cdk2/scsi_disk_entry.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/scsi_disk_entry_test.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/src/modules/scsi_disk/model.c \
		$(CDK2_DIR)/src/modules/scsi_disk/io.c \
		$(CDK2_DIR)/src/modules/scsi_disk/async.c \
		$(CDK2_DIR)/src/modules/scsi_disk/block.c \
		$(CDK2_DIR)/src/modules/scsi_disk/disk_info.c \
		$(CDK2_DIR)/src/modules/scsi_disk/binding.c \
		$(CDK2_DIR)/src/modules/scsi_disk/backend.c \
		$(CDK2_DIR)/src/modules/scsi_disk/entry.c

$(CDK2_NATIVE_SCSI_BINDING_TEST): $(CDK2_DIR)/tests/scsi_bus_binding_test.c \
		$(CDK2_DIR)/src/modules/scsi_bus/binding.c \
		$(CDK2_DIR)/src/modules/scsi_bus/model.c \
		$(CDK2_DIR)/include/cdk2/scsi_bus_binding.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES),\
		$(CDK2_DIR)/tests/scsi_bus_binding_test.c \
		$(CDK2_DIR)/src/modules/scsi_bus/binding.c \
		$(CDK2_DIR)/src/modules/scsi_bus/model.c)

$(CDK2_NATIVE_SCSI_ENTRY_TEST): $(CDK2_DIR)/tests/scsi_bus_entry_test.c \
		$(CDK2_DIR)/src/modules/scsi_bus/entry.c \
		$(CDK2_DIR)/src/modules/scsi_bus/binding.c \
		$(CDK2_DIR)/src/modules/scsi_bus/model.c | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,$(CDK2_NATIVE_INCLUDES),\
		$(CDK2_DIR)/tests/scsi_bus_entry_test.c \
		$(CDK2_DIR)/src/modules/scsi_bus/entry.c \
		$(CDK2_DIR)/src/modules/scsi_bus/binding.c \
		$(CDK2_DIR)/src/modules/scsi_bus/model.c)


$(CDK2_NATIVE_BUILD_DIR)/scsi-bus-%.o: \
		$(CDK2_DIR)/src/modules/scsi_bus/%.c \
		$(CDK2_DIR)/include/cdk2/scsi_bus.h \
		$(CDK2_DIR)/include/cdk2/scsi_bus_binding.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

$(CDK2_NATIVE_SCSI_BUS_PE): $(CDK2_NATIVE_SCSI_BUS_OBJS) \
		$(CDK2_DIR)/src/modules/scsi_bus/scsi_bus.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_scsi_bus_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/scsi_bus/scsi_bus.ld" \
		-o "$@" $(CDK2_NATIVE_SCSI_BUS_OBJS)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_BUILD_DIR)/scsi-disk-%.o: \
		$(CDK2_DIR)/src/modules/scsi_disk/%.c \
		$(CDK2_DIR)/include/cdk2/scsi_disk.h \
		$(CDK2_DIR)/include/cdk2/scsi_disk_entry.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_SCSI_DISK_PE): $(CDK2_NATIVE_SCSI_DISK_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_SCSI_DISK_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/scsi_disk/scsi_disk.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_scsi_disk_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/scsi_disk/scsi_disk.ld" \
		-o "$@" $(CDK2_NATIVE_SCSI_DISK_OBJS) $(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_SCSI_DISK_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_SCSI_BUS_QEMU_OBJ): $(CDK2_DIR)/tests/scsi_bus_qemu.c \
		$(CDK2_DIR)/include/cdk2/scsi_bus_binding.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

$(CDK2_NATIVE_SCSI_BUS_QEMU_PE): $(CDK2_NATIVE_SCSI_BUS_QEMU_OBJ) \
		$(CDK2_DIR)/tests/scsi_bus_qemu.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry scsi_bus_qemu_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/scsi_bus_qemu.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

native-scsi-bus-oracle: $(CDK2_NATIVE_SCSI_BUS_QEMU_PE)
	@printf '%s\n' "native SCSI bus oracle: $(CDK2_NATIVE_SCSI_BUS_QEMU_PE)"

$(CDK2_NATIVE_SATA_CONTROLLER_TEST): $(CDK2_DIR)/tests/sata_controller_test.c $(CDK2_DIR)/src/modules/sata_controller/model.c $(CDK2_DIR)/include/cdk2/sata_controller.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/sata_controller_test.c $(CDK2_DIR)/src/modules/sata_controller/model.c

$(CDK2_NATIVE_ATA_ATAPI_TEST): $(CDK2_DIR)/tests/ata_atapi_pass_thru_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/model.c \
		$(CDK2_DIR)/include/cdk2/ata_atapi_pass_thru.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/ata_atapi_pass_thru_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/model.c

.PHONY: native-ata-atapi-pass-thru-test
native-ata-atapi-pass-thru-test: $(CDK2_NATIVE_ATA_ATAPI_TEST)
	@"$<"

$(CDK2_NATIVE_ATA_ATAPI_BINDING_TEST): $(CDK2_DIR)/tests/ata_atapi_binding_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/model.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/binding.c \
		$(CDK2_DIR)/include/cdk2/ata_atapi_pass_thru.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/ata_atapi_binding_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/model.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/binding.c

.PHONY: native-ata-atapi-binding-test
native-ata-atapi-binding-test: $(CDK2_NATIVE_ATA_ATAPI_BINDING_TEST)
	@"$<"

$(CDK2_NATIVE_ATA_ATAPI_AHCI_TEST): $(CDK2_DIR)/tests/ata_atapi_ahci_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci_async.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide.c \
		$(CDK2_DIR)/include/cdk2/ata_atapi_pass_thru.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/ata_atapi_ahci_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci_async.c

.PHONY: native-ata-atapi-ahci-test
native-ata-atapi-ahci-test: $(CDK2_NATIVE_ATA_ATAPI_AHCI_TEST)
	@"$<"

$(CDK2_NATIVE_ATA_ATAPI_IDE_TEST): $(CDK2_DIR)/tests/ata_atapi_ide_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide_async.c \
		$(CDK2_DIR)/include/cdk2/ata_atapi_pass_thru.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/ata_atapi_ide_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide_async.c

.PHONY: native-ata-atapi-ide-test
native-ata-atapi-ide-test: $(CDK2_NATIVE_ATA_ATAPI_IDE_TEST)
	@"$<"

$(CDK2_NATIVE_ATA_ATAPI_PCI_TEST): $(CDK2_DIR)/tests/ata_atapi_pci_adapter_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/pci_adapter.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/backend.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci_async.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide_async.c \
		$(CDK2_DIR)/include/cdk2/ata_atapi_pci_adapter.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/ata_atapi_pci_adapter_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/pci_adapter.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/backend.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci_async.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide_async.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/model.c

.PHONY: native-ata-atapi-pci-test
native-ata-atapi-pci-test: $(CDK2_NATIVE_ATA_ATAPI_PCI_TEST)
	@"$<"

$(CDK2_NATIVE_ATA_ATAPI_ENTRY_TEST): $(CDK2_DIR)/tests/ata_atapi_entry_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/entry.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ata_async.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci_async.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/binding.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/model.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ata_protocol.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ext_scsi.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide_async.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/backend.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/pci_adapter.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/ata_atapi_entry.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/ata_atapi_entry_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/entry.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ata_async.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci_async.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/binding.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/model.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ata_protocol.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ext_scsi.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide_async.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/backend.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/pci_adapter.c

.PHONY: native-ata-atapi-entry-test
native-ata-atapi-entry-test: $(CDK2_NATIVE_ATA_ATAPI_ENTRY_TEST)
	@"$<"

$(CDK2_NATIVE_ATA_PROTOCOL_TEST): $(CDK2_DIR)/tests/ata_protocol_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ata_protocol.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci_async.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ext_scsi.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/model.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide_async.c \
		$(CDK2_DIR)/include/cdk2/ata_atapi_pass_thru.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/ata_protocol_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ata_protocol.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci_async.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ext_scsi.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/model.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide_async.c

.PHONY: native-ata-protocol-test
native-ata-protocol-test: $(CDK2_NATIVE_ATA_PROTOCOL_TEST)
	@"$<"

$(CDK2_NATIVE_ATA_ASYNC_TEST): $(CDK2_DIR)/tests/ata_async_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ata_async.c \
		$(CDK2_DIR)/include/cdk2/ata_atapi_pass_thru.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/ata_async_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ata_async.c

.PHONY: native-ata-async-test
native-ata-async-test: $(CDK2_NATIVE_ATA_ASYNC_TEST)
	@"$<"

$(CDK2_NATIVE_ATA_BUS_MODEL_TEST): $(CDK2_DIR)/tests/ata_bus_model_test.c \
		$(CDK2_DIR)/src/modules/ata_bus/model.c \
		$(CDK2_DIR)/include/cdk2/ata_bus.h \
		$(CDK2_DIR)/include/cdk2/ata_atapi_pass_thru.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/ata_bus_model_test.c \
		$(CDK2_DIR)/src/modules/ata_bus/model.c

.PHONY: native-ata-bus-model-test
native-ata-bus-model-test: $(CDK2_NATIVE_ATA_BUS_MODEL_TEST)
	@"$<"

$(CDK2_NATIVE_ATA_BUS_IO_TEST): $(CDK2_DIR)/tests/ata_bus_io_test.c \
		$(CDK2_DIR)/src/modules/ata_bus/io.c \
		$(CDK2_DIR)/include/cdk2/ata_bus.h $(CDK2_DIR)/include/cdk2/disk_io.h \
		$(CDK2_DIR)/include/cdk2/ata_atapi_pass_thru.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/ata_bus_io_test.c \
		$(CDK2_DIR)/src/modules/ata_bus/io.c

.PHONY: native-ata-bus-io-test
native-ata-bus-io-test: $(CDK2_NATIVE_ATA_BUS_IO_TEST)
	@"$<"

$(CDK2_NATIVE_ATA_BUS_BLOCK_TEST): $(CDK2_DIR)/tests/ata_bus_block_test.c \
		$(CDK2_DIR)/src/modules/ata_bus/io.c \
		$(CDK2_DIR)/src/modules/ata_bus/model.c \
		$(CDK2_DIR)/src/modules/ata_bus/block.c \
		$(CDK2_DIR)/include/cdk2/ata_bus.h $(CDK2_DIR)/include/cdk2/disk_io.h \
		$(CDK2_DIR)/include/cdk2/ata_atapi_pass_thru.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/ata_bus_block_test.c \
		$(CDK2_DIR)/src/modules/ata_bus/io.c \
		$(CDK2_DIR)/src/modules/ata_bus/model.c \
		$(CDK2_DIR)/src/modules/ata_bus/block.c

.PHONY: native-ata-bus-block-test
native-ata-bus-block-test: $(CDK2_NATIVE_ATA_BUS_BLOCK_TEST)
	@"$<"

$(CDK2_NATIVE_ATA_BUS_BINDING_TEST): $(CDK2_DIR)/tests/ata_bus_binding_test.c \
		$(CDK2_DIR)/src/modules/ata_bus/model.c $(CDK2_DIR)/src/modules/ata_bus/io.c \
		$(CDK2_DIR)/src/modules/ata_bus/block.c $(CDK2_DIR)/src/modules/ata_bus/binding.c \
		$(CDK2_DIR)/src/modules/ata_bus/disk_security.c \
		$(CDK2_DIR)/include/cdk2/ata_bus.h $(CDK2_DIR)/include/cdk2/disk_io.h \
		$(CDK2_DIR)/include/cdk2/ata_atapi_pass_thru.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/ata_bus_binding_test.c \
		$(CDK2_DIR)/src/modules/ata_bus/model.c $(CDK2_DIR)/src/modules/ata_bus/io.c \
		$(CDK2_DIR)/src/modules/ata_bus/block.c $(CDK2_DIR)/src/modules/ata_bus/binding.c \
		$(CDK2_DIR)/src/modules/ata_bus/disk_security.c

.PHONY: native-ata-bus-binding-test
native-ata-bus-binding-test: $(CDK2_NATIVE_ATA_BUS_BINDING_TEST)
	@"$<"

$(CDK2_NATIVE_ATA_BUS_ENTRY_TEST): $(CDK2_DIR)/tests/ata_bus_entry_test.c \
		$(CDK2_DIR)/src/modules/ata_bus/entry.c \
		$(CDK2_DIR)/src/modules/ata_bus/model.c $(CDK2_DIR)/src/modules/ata_bus/io.c \
		$(CDK2_DIR)/src/modules/ata_bus/block.c $(CDK2_DIR)/src/modules/ata_bus/binding.c \
		$(CDK2_DIR)/src/modules/ata_bus/disk_security.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/ata_bus_entry.h $(CDK2_DIR)/include/cdk2/ata_bus.h \
		$(CDK2_DIR)/include/cdk2/ata_atapi_entry.h \
		$(CDK2_DIR)/include/cdk2/ata_atapi_pass_thru.h \
		$(CDK2_DIR)/include/cdk2/disk_io.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/ata_bus_entry_test.c \
		$(CDK2_DIR)/src/modules/ata_bus/model.c $(CDK2_DIR)/src/modules/ata_bus/io.c \
		$(CDK2_DIR)/src/modules/ata_bus/block.c $(CDK2_DIR)/src/modules/ata_bus/binding.c \
		$(CDK2_DIR)/src/modules/ata_bus/disk_security.c \
		$(CDK2_DIR)/src/lib/diagnostic.c

.PHONY: native-ata-bus-entry-test
native-ata-bus-entry-test: $(CDK2_NATIVE_ATA_BUS_ENTRY_TEST)
	@"$<"


$(CDK2_NATIVE_BUILD_DIR)/ata-bus-%.o: $(CDK2_DIR)/src/modules/ata_bus/%.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-maccumulate-outgoing-args -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_ATA_BUS_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_ATA_BUS_PE): $(CDK2_NATIVE_ATA_BUS_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_ATA_BUS_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/ata_bus/ata_bus.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_ata_bus_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/src/modules/ata_bus/ata_bus.ld" \
		-o "$@" $(CDK2_NATIVE_ATA_BUS_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_ATA_BUS_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"
$(CDK2_NATIVE_ATA_BUS_QEMU_OBJ): $(CDK2_DIR)/tests/ata_bus_qemu.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args \
		-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_ATA_BUS_QEMU_PE): $(CDK2_NATIVE_ATA_BUS_QEMU_OBJ) \
		$(CDK2_DIR)/tests/ata_atapi_qemu.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry ata_bus_qemu_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/ata_atapi_qemu.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

.PHONY: native-ata-bus-oracle
native-ata-bus-oracle: $(CDK2_NATIVE_ATA_BUS_QEMU_PE)

$(CDK2_NATIVE_ATA_BACKEND_IDE_TEST): $(CDK2_DIR)/tests/ata_backend_ide_test.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/backend.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ide_async.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ahci_async.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/pci_adapter.c \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/model.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fsanitize=address,undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" $^

.PHONY: native-ata-backend-ide-test
native-ata-backend-ide-test: $(CDK2_NATIVE_ATA_BACKEND_IDE_TEST)
	@"$<"


$(CDK2_NATIVE_BUILD_DIR)/ata-atapi-%.o: $(CDK2_DIR)/src/modules/ata_atapi_pass_thru/%.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections -Os -Oz,$(CDK2_NATIVE_CFLAGS)) \
		-Oz -maccumulate-outgoing-args -fcf-protection=none \
		-frandom-seed=$* \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_ATA_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_ATA_PE): $(CDK2_NATIVE_ATA_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_ATA_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ata_atapi_pass_thru.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_ata_atapi_pass_thru_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x200 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/ata_atapi_pass_thru/ata_atapi_pass_thru.ld" \
		-o "$@" $(CDK2_NATIVE_ATA_OBJS) $(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_ATA_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --file-alignment 0x200 "$@"
$(CDK2_NATIVE_ATA_QEMU_OBJ): $(CDK2_DIR)/tests/ata_atapi_qemu.c \
		$(CDK2_DIR)/include/cdk2/ata_atapi_pass_thru.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_ATA_QEMU_PE): $(CDK2_NATIVE_ATA_QEMU_OBJ) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-set.o \
		$(CDK2_DIR)/tests/ata_atapi_qemu.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry ata_atapi_qemu_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/ata_atapi_qemu.ld" -o "$@" "$<" \
		"$(CDK2_NATIVE_BUILD_DIR)/mem-set.o"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

.PHONY: native-ata-atapi-oracle
native-ata-atapi-oracle: $(CDK2_NATIVE_ATA_QEMU_PE)
	@printf '%s\n' "native ATA/ATAPI oracle: $<"

$(CDK2_NATIVE_SATA_ENTRY_TEST): $(CDK2_DIR)/tests/sata_entry_test.c $(CDK2_DIR)/src/modules/sata_controller/entry.c $(CDK2_DIR)/src/modules/sata_controller/model.c $(CDK2_DIR)/src/lib/diagnostic.c $(CDK2_DIR)/include/cdk2/sata_controller.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" $(CDK2_DIR)/tests/sata_entry_test.c $(CDK2_DIR)/src/modules/sata_controller/model.c $(CDK2_DIR)/src/lib/diagnostic.c


$(CDK2_NATIVE_BUILD_DIR)/sata-model.o: $(CDK2_DIR)/src/modules/sata_controller/model.c $(CDK2_DIR)/include/cdk2/sata_controller.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -maccumulate-outgoing-args -MMD -MP -MF "$(CDK2_NATIVE_BUILD_DIR)/sata-model.d" -MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/sata-entry.o: $(CDK2_DIR)/src/modules/sata_controller/entry.c $(CDK2_DIR)/include/cdk2/sata_controller.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) $(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) -maccumulate-outgoing-args -MMD -MP -MF "$(CDK2_NATIVE_BUILD_DIR)/sata-entry.d" -MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/sata-diagnostic.o: $(CDK2_DIR)/src/modules/sata_controller/diagnostic.c $(CDK2_DIR)/include/cdk2/sata_controller.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -DCDK2_DEBUG -maccumulate-outgoing-args -MMD -MP -MF "$(CDK2_NATIVE_BUILD_DIR)/sata-diagnostic.d" -MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_SATA_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -maccumulate-outgoing-args -MMD -MP -MF "$(CDK2_NATIVE_BUILD_DIR)/sata-diagnostic-core.d" -MT "$@" $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_SATA_PE): $(CDK2_NATIVE_SATA_OBJS) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_SATA_DIAG_CORE)) $(CDK2_DIR)/src/modules/sata_controller/sata_controller.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_sata_controller_entry --image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 --build-id=none --no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/src/modules/sata_controller/sata_controller.ld" -o "$@" $(CDK2_NATIVE_SATA_OBJS) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_SATA_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" "$@"
.PHONY: native-sata-controller-oracle
native-sata-controller-oracle: $(CDK2_NATIVE_SATA_QEMU_PE)
	@printf '%s\n' "native SATA controller oracle: $<"

$(CDK2_NATIVE_SATA_QEMU_OBJ): $(CDK2_DIR)/tests/sata_qemu.c \
		$(CDK2_DIR)/include/cdk2/sata_controller.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_SATA_QEMU_PE): $(CDK2_NATIVE_SATA_QEMU_OBJ) \
		$(CDK2_DIR)/tests/sata_qemu.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry sata_qemu_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/tests/sata_qemu.ld" \
		-o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

$(CDK2_NATIVE_PCI_HOST_BRIDGE_TEST): $(CDK2_DIR)/tests/pci_host_bridge_test.c \
		$(CDK2_DIR)/src/modules/pci_host_bridge/model.c \
		$(CDK2_DIR)/include/cdk2/pci_host_bridge.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/pci_host_bridge_test.c \
		$(CDK2_DIR)/src/modules/pci_host_bridge/model.c

$(CDK2_NATIVE_PCI_ENUMERATE_TEST): $(CDK2_DIR)/tests/pci_enumerate_test.c \
		$(CDK2_DIR)/src/modules/pci_bus/immutable.c \
		$(CDK2_DIR)/include/cdk2/pci_enumerate.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/pci_enumerate_test.c \
		$(CDK2_DIR)/src/modules/pci_bus/immutable.c

$(CDK2_NATIVE_PCI_IMMUTABLE_ENTRY_TEST): \
		$(CDK2_DIR)/tests/pci_immutable_entry_test.c \
		$(CDK2_DIR)/src/modules/pci_bus/immutable_entry.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/pci_topology_handoff.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/pci_immutable_entry_test.c \
		$(CDK2_DIR)/src/lib/diagnostic.c

$(CDK2_NATIVE_PCI_HOST_BRIDGE_ENTRY_TEST): \
		$(CDK2_DIR)/tests/pci_host_bridge_entry_test.c \
		$(CDK2_DIR)/src/modules/pci_host_bridge/model.c \
		$(CDK2_DIR)/src/modules/pci_host_bridge/entry.c \
		$(CDK2_DIR)/src/modules/pci_host_bridge/root_io.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/pci_host_bridge.h \
		| $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/pci_host_bridge_entry_test.c \
		$(CDK2_DIR)/src/modules/pci_host_bridge/model.c \
		$(CDK2_DIR)/src/modules/pci_host_bridge/entry.c \
		$(CDK2_DIR)/src/modules/pci_host_bridge/root_io.c \
		$(CDK2_DIR)/src/lib/diagnostic.c
$(CDK2_NATIVE_PCI_ROOT_IO_TEST): $(CDK2_DIR)/tests/pci_root_io_test.c \
		$(CDK2_DIR)/src/modules/pci_host_bridge/root_io.c \
		$(CDK2_DIR)/include/cdk2/pci_host_bridge.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/pci_root_io_test.c \
		$(CDK2_DIR)/src/modules/pci_host_bridge/root_io.c

$(CDK2_NATIVE_PCI_BUS_MODEL_TEST): $(CDK2_DIR)/tests/pci_bus_model_test.c \
		$(CDK2_DIR)/src/modules/pci_bus/model.c \
		$(CDK2_DIR)/src/modules/pci_bus/allocator.c \
		$(CDK2_DIR)/src/modules/pci_bus/host.c \
		$(CDK2_DIR)/src/modules/pci_bus/rom.c \
		$(CDK2_DIR)/src/modules/pci_bus/cardbus.c \
		$(CDK2_DIR)/src/modules/pci_bus/pci_io.c \
		$(CDK2_DIR)/src/modules/pci_bus/pci_io_abi.c \
		$(CDK2_DIR)/src/modules/pci_bus/binding.c \
		$(CDK2_DIR)/src/modules/pci_bus/adapter.c \
		$(CDK2_DIR)/src/modules/pci_bus/driver.c \
		$(CDK2_DIR)/src/modules/pci_bus/entry.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/pci_io_model.h \
		$(CDK2_DIR)/include/cdk2/pci_io_abi.h \
		$(CDK2_DIR)/include/cdk2/pci_bus_binding.h \
		$(CDK2_DIR)/include/cdk2/pci_bus_adapter.h \
		$(CDK2_DIR)/include/cdk2/pci_bus_model.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(CDK2_DIR)/tests/pci_bus_model_test.c \
		$(CDK2_DIR)/src/modules/pci_bus/model.c \
		$(CDK2_DIR)/src/modules/pci_bus/allocator.c \
		$(CDK2_DIR)/src/modules/pci_bus/host.c \
		$(CDK2_DIR)/src/modules/pci_bus/rom.c \
		$(CDK2_DIR)/src/modules/pci_bus/cardbus.c \
		$(CDK2_DIR)/src/modules/pci_bus/pci_io.c \
		$(CDK2_DIR)/src/modules/pci_bus/pci_io_abi.c \
		$(CDK2_DIR)/src/modules/pci_bus/binding.c \
		$(CDK2_DIR)/src/modules/pci_bus/adapter.c \
		$(CDK2_DIR)/src/modules/pci_bus/driver.c \
		$(CDK2_DIR)/src/modules/pci_bus/entry.c \
		$(CDK2_DIR)/src/lib/diagnostic.c

$(CDK2_NATIVE_PCI_BUS_ENTRY_TEST): $(CDK2_DIR)/tests/pci_bus_entry_test.c \
		$(CDK2_DIR)/src/modules/pci_bus/model.c \
		$(CDK2_DIR)/src/modules/pci_bus/allocator.c \
		$(CDK2_DIR)/src/modules/pci_bus/host.c \
		$(CDK2_DIR)/src/modules/pci_bus/rom.c \
		$(CDK2_DIR)/src/modules/pci_bus/cardbus.c \
		$(CDK2_DIR)/src/modules/pci_bus/pci_io.c \
		$(CDK2_DIR)/src/modules/pci_bus/pci_io_abi.c \
		$(CDK2_DIR)/src/modules/pci_bus/binding.c \
		$(CDK2_DIR)/src/modules/pci_bus/adapter.c \
		$(CDK2_DIR)/src/modules/pci_bus/driver.c \
		$(CDK2_DIR)/src/modules/pci_bus/entry.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/pci_configuration.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" $(filter %.c,$^)

$(CDK2_NATIVE_PCI_BUS_OBJ_DIR)/pci-bus-%.o: \
		$(CDK2_DIR)/src/modules/pci_bus/%.c \
		$(CDK2_NATIVE_PCI_BUS_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@mkdir -p "$(@D)"
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-builtin \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_PCI_BUS_OBJECT_CFLAGS) \
		$(CDK2_NATIVE_PCI_BUS_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_PCI_BUS_MEM_OBJ): $(CDK2_LIB_DIR)/mem.c \
		$(CDK2_NATIVE_PCI_BUS_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@mkdir -p "$(@D)"
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_PCI_BUS_INCLUDES) \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_PCI_BUS_PE): $(CDK2_NATIVE_PCI_BUS_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG) $(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_PCI_BUS_DIAG_CORE)) \
		$(CDK2_NATIVE_PCI_BUS_MEM_OBJ) \
		$(CDK2_DIR)/src/modules/pci_bus/pci_bus.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_pci_bus_entry \
		-Map "$(CDK2_NATIVE_PCI_BUS_MAP)" \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/pci_bus/pci_bus.ld" -o "$@" \
		$(CDK2_NATIVE_PCI_BUS_OBJS) \
		$(CDK2_NATIVE_PCI_BUS_MEM_OBJ) \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG) $(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_PCI_BUS_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


.PHONY: native-pci-bus-test  native-pci-enumerate-test \
	native-pci-bus-config-toggle-test
native-pci-enumerate-test: $(CDK2_NATIVE_PCI_ENUMERATE_TEST)
	@"$(CDK2_NATIVE_PCI_ENUMERATE_TEST)"

.PHONY: native-pci-immutable-entry-test
native-pci-immutable-entry-test: $(CDK2_NATIVE_PCI_IMMUTABLE_ENTRY_TEST)
	@"$(CDK2_NATIVE_PCI_IMMUTABLE_ENTRY_TEST)"

native-pci-bus-config-toggle-test:
	@"$(CDK2_NATIVE_PCI_CONFIG_TOGGLE_TEST)" "$(MAKE)" "$(CDK2_DIR)"

native-check: native-pci-bus-config-toggle-test

native-pci-bus-test: $(CDK2_NATIVE_PCI_BUS_MODEL_TEST) \
		$(CDK2_NATIVE_PCI_BUS_ENTRY_TEST) native-pci-immutable-entry-test \
		native-pci-bus-config-toggle-test
	@"$(CDK2_NATIVE_PCI_BUS_MODEL_TEST)"
	@"$(CDK2_NATIVE_PCI_BUS_ENTRY_TEST)"
.PHONY: native-pci-host-bridge-test
native-pci-host-bridge-test: $(CDK2_NATIVE_PCI_HOST_BRIDGE_TEST) \
		$(CDK2_NATIVE_PCI_HOST_BRIDGE_ENTRY_TEST) \
		$(CDK2_NATIVE_PCI_ROOT_IO_TEST) \
		$(CDK2_NATIVE_PCI_HOST_BRIDGE_PE) \
		$(CDK2_DIR)/tests/pci_host_bridge_profile_test.sh $(CDK2_CONFIG_HEADER)
	@"$(CDK2_NATIVE_PCI_HOST_BRIDGE_TEST)"
	@"$(CDK2_NATIVE_PCI_HOST_BRIDGE_ENTRY_TEST)"
	@"$(CDK2_NATIVE_PCI_HOST_BRIDGE_ENTRY_TEST)" --strict-hob-only
	@"$(CDK2_NATIVE_PCI_ROOT_IO_TEST)"
	@"$(CDK2_NATIVE_PCI_HOST_BRIDGE_ROLLBACK_MUTATION_TEST)" \
		"$(CDK2_NATIVE_HOST_CC)" "$(CDK2_NATIVE_HOST_CFLAGS)" \
		"$(CDK2_NATIVE_INCLUDES)"

.PHONY: native-pci-host-bridge-oracle
native-pci-host-bridge-oracle: $(CDK2_NATIVE_PCI_HOST_BRIDGE_QEMU_PE)
	@printf '%s\n' "native PCI Host Bridge oracle: $(CDK2_NATIVE_PCI_HOST_BRIDGE_QEMU_PE)"

.PHONY: native-diagnostic-volume-test native-ehci-diagnostic-parity native-uhci-diagnostic-parity \
	native-usb-mass-diagnostic-parity \
	native-pci-bus-diagnostic-parity native-pci-host-bridge-diagnostic-parity
native-diagnostic-volume-test: \
		$(CDK2_NATIVE_BUILD_DIR)/usb-mass-transport-test \
		$(CDK2_NATIVE_BUILD_DIR)/cdk2-dxe-core-memory-test
	@"$(CDK2_NATIVE_BUILD_DIR)/usb-mass-transport-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/cdk2-dxe-core-memory-test"
	@sh "$(CDK2_DIR)/tests/diagnostic_volume_test.sh" "$(CDK2_DIR)"
native-ehci-diagnostic-parity: $(CDK2_NATIVE_EHCI_PE)
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/ehci-diagnostic-parity.tsv" 101 \
		"$(CDK2_DIR)/src/modules/ehci"
	@if [ "$(CONFIG_CDK2_BUILD_DEBUG)" = y ]; then strings -a "$<" | \
		grep -Fq '[CDK2][ehci][verbose] host controller event'; fi
native-uhci-diagnostic-parity: $(CDK2_NATIVE_UHCI_PE)
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/uhci-diagnostic-parity.tsv" 37 \
		"$(CDK2_DIR)/src/modules/uhci"
	@if [ "$(CONFIG_CDK2_BUILD_DEBUG)" = y ]; then strings -a "$<" | \
		grep -Fq '[CDK2][uhci][info] initialize controller'; fi
native-usb-mass-diagnostic-parity: $(CDK2_NATIVE_USB_MASS_PE)
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/usb-mass-diagnostic-parity.tsv" 40 \
		"$(CDK2_DIR)/src/modules/usb_mass"
	@if [ "$(CONFIG_CDK2_BUILD_DEBUG)" = y ]; then ! strings -a "$<" | \
		grep -Fq '[CDK2][usb-mass][verbose] mass storage event'; fi
native-pci-bus-diagnostic-parity: $(CDK2_NATIVE_PCI_BUS_PE)
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/pci-bus-diagnostic-parity.tsv" 43 \
		"$(CDK2_DIR)/src/modules/pci_bus"
	@if [ "$(CONFIG_CDK2_BUILD_DEBUG)" = y ] && \
	    [ "$(CONFIG_CDK2_LINEAR_BOOT)" != y ]; then strings -a "$<" | \
		grep -Fq '[CDK2][pci-bus][verbose] bus resource event'; fi
native-pci-host-bridge-diagnostic-parity: $(CDK2_NATIVE_PCI_HOST_BRIDGE_PE)
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/pci-host-bridge-diagnostic-parity.tsv" 37 \
		"$(CDK2_DIR)/src/modules/pci_host_bridge"
	@if [ "$(CONFIG_CDK2_BUILD_DEBUG)" = y ]; then strings -a "$<" | \
		grep -Fq '[CDK2][pci-host][verbose] resource allocation event'; fi
native-check: native-diagnostic-volume-test native-ehci-diagnostic-parity native-uhci-diagnostic-parity \
	native-usb-mass-diagnostic-parity \
	native-pci-bus-diagnostic-parity native-pci-host-bridge-diagnostic-parity

.PHONY: native-usb-bus-diagnostic-parity
native-usb-bus-diagnostic-parity: $(CDK2_NATIVE_USB_BUS_PE)
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/usb-bus-diagnostic-parity.tsv" 100 \
		"$(CDK2_DIR)/src/modules/usb_bus"
	@if [ "$(CONFIG_CDK2_BUILD_DEBUG)" = y ]; then strings -a "$<" | \
		grep -Fq '[CDK2][usb-bus][verbose] bus enumeration event'; fi

native-check: native-usb-bus-diagnostic-parity

.PHONY: native-usb-bus-linearity-test
native-usb-bus-linearity-test:
	@sh "$(CDK2_NATIVE_USB_BUS_LINEARITY_TEST)" \
		"$(CDK2_DIR)/src/modules/usb_bus/entry.c"

ifeq ($(CONFIG_CDK2_NATIVE_USB_BUS),y)
native-check: native-usb-bus-linearity-test native-relocation-model-test \
	native-pereloccheck-test
.PHONY: native-relocation-model-test
native-relocation-model-test: $(CDK2_DIR)/tests/native_relocation_model_test.sh \
		$(CDK2_DIR)/tests/native_pe_link_test.sh \
		$(CDK2_DIR)/src/boot/Makefile $(CDK2_UTIL_DIR)/native_pe_link.c \
		$(CDK2_NATIVE_PE_LINK)
	@"$(CDK2_DIR)/tests/native_relocation_model_test.sh" \
		"$(CDK2_DIR)/src/boot/Makefile" \
		"$(CDK2_UTIL_DIR)/native_pe_link.c"
	@"$(CDK2_DIR)/tests/native_pe_link_test.sh" "$(CDK2_NATIVE_PE_LINK)"

.PHONY: native-pereloccheck-test
native-pereloccheck-test: $(CDK2_NATIVE_PERELOCCHECK) \
		$(CDK2_NATIVE_PE_EXEC_FIXTURE) \
		$(CDK2_NATIVE_PE_RELOCATION_LOAD_TEST) \
		$(CDK2_NATIVE_ATA_QEMU_PE) \
		$(CDK2_DIR)/tests/pereloccheck_test.sh
	@"$(CDK2_DIR)/tests/pereloccheck_test.sh" \
		"$(CDK2_NATIVE_PERELOCCHECK)" "$(CDK2_NATIVE_PE_EXEC_FIXTURE)"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 \
		--expect-relocations 1 "$(CDK2_NATIVE_ATA_QEMU_PE)"
	@"$(CDK2_NATIVE_PE_RELOCATION_LOAD_TEST)" "$(CDK2_NATIVE_ATA_QEMU_PE)"
endif

$(CDK2_NATIVE_PCI_HOST_BRIDGE_QEMU_OBJ): \
		$(CDK2_DIR)/tests/pci_host_bridge_qemu.c \
		$(CDK2_DIR)/include/cdk2/pci_host_bridge.h \
		$(CDK2_DIR)/include/cdk2/dxe_core_abi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_PCI_HOST_BRIDGE_QEMU_PE): \
		$(CDK2_NATIVE_PCI_HOST_BRIDGE_QEMU_OBJ) \
		$(CDK2_DIR)/tests/pci_host_bridge_qemu.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 \
		--entry pci_host_bridge_qemu_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/pci_host_bridge_qemu.ld" -o "$@" \
		"$(CDK2_NATIVE_PCI_HOST_BRIDGE_QEMU_OBJ)"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

$(CDK2_NATIVE_PCI_HOST_BRIDGE_MODEL_OBJ): \
		$(CDK2_DIR)/src/modules/pci_host_bridge/model.c \
		$(CDK2_DIR)/include/cdk2/pci_host_bridge.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_PCI_HOST_BRIDGE_ENTRY_OBJ): \
		$(CDK2_DIR)/src/modules/pci_host_bridge/entry.c \
		$(CDK2_DIR)/include/cdk2/pci_host_bridge.h \
		| $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_PCI_ROOT_IO_OBJ): \
		$(CDK2_DIR)/src/modules/pci_host_bridge/root_io.c \
		$(CDK2_DIR)/include/cdk2/pci_host_bridge.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_PCI_HOST_BRIDGE_PE): $(CDK2_NATIVE_PCI_HOST_BRIDGE_MODEL_OBJ) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_PCI_HOST_BRIDGE_DIAG_CORE)) \
		$(CDK2_NATIVE_PCI_HOST_BRIDGE_ENTRY_OBJ) \
		$(CDK2_NATIVE_PCI_ROOT_IO_OBJ) $(CDK2_NATIVE_PCI_HOST_BRIDGE_DIAGNOSTIC_OBJ) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o \
		$(CDK2_DIR)/src/modules/pci_host_bridge/pci_host_bridge.ld \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_pci_host_bridge_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/pci_host_bridge/pci_host_bridge.ld" -o "$@" \
		$(CDK2_NATIVE_PCI_HOST_BRIDGE_MODEL_OBJ) \
		$(CDK2_NATIVE_PCI_HOST_BRIDGE_ENTRY_OBJ) \
		$(CDK2_NATIVE_PCI_ROOT_IO_OBJ) \
		$(CDK2_NATIVE_PCI_HOST_BRIDGE_DIAGNOSTIC_OBJ) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_PCI_HOST_BRIDGE_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_WATCHDOG_TEST): $(CDK2_DIR)/tests/watchdog_test.c \
		$(CDK2_DIR)/src/modules/watchdog/watchdog.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/watchdog.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" "$<" $(CDK2_DIR)/src/lib/diagnostic.c

$(CDK2_NATIVE_STATUS_CODE_ROUTER_TEST): $(CDK2_DIR)/tests/status_code_router_test.c $(CDK2_DIR)/src/modules/status_code_router/status_code_router.c $(CDK2_DIR)/include/cdk2/status_code_router.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<"

$(CDK2_NATIVE_STATUS_CODE_HANDLER_TEST): $(CDK2_DIR)/tests/status_code_handler_test.c $(CDK2_DIR)/src/modules/status_code_handler/status_code_handler.c $(CDK2_DIR)/src/lib/diagnostic.c $(CDK2_DIR)/include/cdk2/status_code_router.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<" $(CDK2_DIR)/src/lib/diagnostic.c

$(CDK2_NATIVE_PE_TEST): private override CDK2_NATIVE_HOST_CFLAGS := \
	$(filter-out -O%,$(CDK2_NATIVE_HOST_CFLAGS))
$(CDK2_NATIVE_PE_TEST): $(CDK2_NATIVE_DIR)/pe.c $(CDK2_NATIVE_DIR)/pe.h $(CDK2_NATIVE_DIR)/pe_test.c $(CDK2_PE_IMAGE_VIEW_SRC) $(CDK2_DIR)/include/industry_standard/pe_image.h | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,\
		-O1 -fsanitize=undefined -fno-sanitize-recover=undefined \
		-fno-omit-frame-pointer \
		$(CDK2_NATIVE_INCLUDES) -I$(CDK2_NATIVE_DIR) \
		$(CDK2_NATIVE_HOST_LDFLAGS),"$(CDK2_NATIVE_DIR)/pe.c" \
		"$(CDK2_NATIVE_DIR)/pe_test.c" $(CDK2_PE_IMAGE_VIEW_SRC))

$(CDK2_NATIVE_PE_LOW_STACK_TEST): $(CDK2_NATIVE_DIR)/pe.c \
		$(CDK2_NATIVE_DIR)/pe.h $(CDK2_DIR)/tests/pe_low_stack_test.c \
		$(CDK2_DIR)/tests/pe_low_stack_call.S $(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/include/industry_standard/pe_image.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -I$(CDK2_NATIVE_DIR) \
		$(CDK2_NATIVE_HOST_LDFLAGS) -o "$@" "$(CDK2_NATIVE_DIR)/pe.c" \
		"$(CDK2_DIR)/tests/pe_low_stack_test.c" \
		"$(CDK2_DIR)/tests/pe_low_stack_call.S" $(CDK2_PE_IMAGE_VIEW_SRC)

$(CDK2_NATIVE_ENTRY_TEST): $(CDK2_NATIVE_DIR)/entry.c $(CDK2_NATIVE_DIR)/entry.h \
		$(CDK2_DIR)/tests/native_entry_test.c $(CDK2_NATIVE_DIR)/context.h \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,\
		$(CDK2_NATIVE_INCLUDES) -I$(CDK2_NATIVE_DIR) $(CDK2_NATIVE_HOST_LDFLAGS) \
		,"$(CDK2_NATIVE_DIR)/entry.c" "$(CDK2_DIR)/tests/native_entry_test.c")

-include $(CDK2_NATIVE_DEPFILES)

CDK2_NATIVE_FTW_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ftw-test
CDK2_NATIVE_FTW_PI_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ftw-pi-test
CDK2_NATIVE_FTW_FVB_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ftw-fvb-test
CDK2_NATIVE_FTW_GEOMETRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ftw-geometry-test
CDK2_NATIVE_FTW_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ftw-entry-test
CDK2_NATIVE_FTW_CORE_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/ftw-core.o
CDK2_NATIVE_FTW_PI_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/ftw-pi.o
CDK2_NATIVE_FTW_FVB_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/ftw-fvb.o
CDK2_NATIVE_FTW_GEOMETRY_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/ftw-geometry.o
CDK2_NATIVE_FTW_ENTRY_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/ftw-entry.o
CDK2_NATIVE_FTW_DIAGNOSTIC_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/ftw-diagnostic.o
CDK2_NATIVE_FTW_DIAG_CORE ?= $(CDK2_NATIVE_BUILD_DIR)/ftw-diagnostic-core.o
CDK2_NATIVE_FTW_DIAGNOSTIC_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/ftw-diagnostic-test
CDK2_NATIVE_FTW_PE ?= $(CDK2_NATIVE_BUILD_DIR)/FaultTolerantWriteDxe.efi

$(CDK2_NATIVE_FTW_CORE_OBJ) $(CDK2_NATIVE_FTW_PI_OBJ) \
	$(CDK2_NATIVE_FTW_FVB_OBJ) $(CDK2_NATIVE_FTW_GEOMETRY_OBJ) \
	$(CDK2_NATIVE_FTW_ENTRY_OBJ) $(CDK2_NATIVE_FTW_DIAGNOSTIC_OBJ): \
	$(CDK2_CONFIG_HEADER)

.PHONY: native-ftw-test
native-ftw-test: $(CDK2_NATIVE_FTW_TEST) $(CDK2_NATIVE_FTW_PI_TEST) \
		$(CDK2_NATIVE_FTW_FVB_TEST) $(CDK2_NATIVE_FTW_GEOMETRY_TEST) \
		$(CDK2_NATIVE_FTW_ENTRY_TEST)
	@"$(CDK2_NATIVE_FTW_TEST)"
	@"$(CDK2_NATIVE_FTW_PI_TEST)"
	@"$(CDK2_NATIVE_FTW_FVB_TEST)"
	@"$(CDK2_NATIVE_FTW_GEOMETRY_TEST)"
	@"$(CDK2_NATIVE_FTW_ENTRY_TEST)"
	@HOSTCC="$(CDK2_NATIVE_HOST_CC)" sh "$(CDK2_DIR)/tests/initial_provider_entry_profiles_test.sh"

.PHONY: native-ftw-diagnostic-parity
native-ftw-diagnostic-parity: $(CDK2_NATIVE_FTW_PE) $(CDK2_NATIVE_FTW_DIAGNOSTIC_TEST)
	@if [ "$(CONFIG_CDK2_BUILD_DEBUG)" = y ]; then \
		"$(CDK2_NATIVE_FTW_DIAGNOSTIC_TEST)" "$(CDK2_NATIVE_FTW_PE)"; fi

$(CDK2_NATIVE_FTW_TEST): $(CDK2_DIR)/tests/ftw_test.c $(CDK2_DIR)/src/modules/ftw/ftw.c $(CDK2_DIR)/src/modules/ftw/pi_journal.c $(CDK2_DIR)/include/cdk2/ftw.h $(CDK2_DIR)/include/cdk2/ftw_pi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/ftw_test.c" "$(CDK2_DIR)/src/modules/ftw/ftw.c" "$(CDK2_DIR)/src/modules/ftw/pi_journal.c"
$(CDK2_NATIVE_FTW_PI_TEST): $(CDK2_DIR)/tests/ftw_pi_test.c $(CDK2_DIR)/src/modules/ftw/ftw.c $(CDK2_DIR)/src/modules/ftw/pi_journal.c $(CDK2_DIR)/include/cdk2/ftw_pi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/ftw_pi_test.c" "$(CDK2_DIR)/src/modules/ftw/ftw.c" "$(CDK2_DIR)/src/modules/ftw/pi_journal.c"
$(CDK2_NATIVE_FTW_FVB_TEST): $(CDK2_DIR)/tests/ftw_fvb_test.c $(CDK2_DIR)/src/modules/ftw/ftw.c $(CDK2_DIR)/src/modules/ftw/fvb.c $(CDK2_DIR)/include/cdk2/ftw_fvb.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/ftw_fvb_test.c" "$(CDK2_DIR)/src/modules/ftw/ftw.c" "$(CDK2_DIR)/src/modules/ftw/fvb.c"
$(CDK2_NATIVE_FTW_GEOMETRY_TEST): $(CDK2_DIR)/tests/ftw_geometry_test.c $(CDK2_DIR)/src/modules/ftw/geometry.c $(CDK2_DIR)/include/cdk2/ftw_geometry.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$<" "$(CDK2_DIR)/src/modules/ftw/geometry.c"
$(CDK2_NATIVE_FTW_ENTRY_TEST): $(CDK2_DIR)/tests/ftw_entry_test.c $(CDK2_DIR)/src/modules/ftw/entry.c $(CDK2_DIR)/src/modules/ftw/geometry.c $(CDK2_DIR)/src/modules/ftw/fvb.c $(CDK2_DIR)/src/modules/ftw/ftw.c $(CDK2_DIR)/src/modules/ftw/pi_journal.c $(CDK2_DIR)/include/cdk2/ftw_entry.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) -o "$@" "$(CDK2_DIR)/tests/ftw_entry_test.c" "$(CDK2_DIR)/src/modules/ftw/entry.c" "$(CDK2_DIR)/src/modules/ftw/geometry.c" "$(CDK2_DIR)/src/modules/ftw/fvb.c" "$(CDK2_DIR)/src/modules/ftw/ftw.c" "$(CDK2_DIR)/src/modules/ftw/pi_journal.c"
$(CDK2_NATIVE_FTW_DIAGNOSTIC_TEST): $(CDK2_DIR)/tests/ftw_diagnostic_test.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -o "$@" "$<"

$(CDK2_NATIVE_FTW_CORE_OBJ): $(CDK2_DIR)/src/modules/ftw/ftw.c $(CDK2_DIR)/include/cdk2/ftw.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args $(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"
$(CDK2_NATIVE_FTW_PI_OBJ): $(CDK2_DIR)/src/modules/ftw/pi_journal.c $(CDK2_DIR)/include/cdk2/ftw_pi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"
$(CDK2_NATIVE_FTW_FVB_OBJ): $(CDK2_DIR)/src/modules/ftw/fvb.c $(CDK2_DIR)/include/cdk2/ftw_fvb.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"
$(CDK2_NATIVE_FTW_GEOMETRY_OBJ): $(CDK2_DIR)/src/modules/ftw/geometry.c $(CDK2_DIR)/include/cdk2/ftw_geometry.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"
$(CDK2_NATIVE_FTW_ENTRY_OBJ): $(CDK2_DIR)/src/modules/ftw/entry.c $(CDK2_DIR)/include/cdk2/ftw_entry.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args $(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"
$(CDK2_NATIVE_FTW_DIAGNOSTIC_OBJ): $(CDK2_DIR)/src/modules/ftw/diagnostic.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args $(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"
$(CDK2_NATIVE_BUILD_DIR)/ftw-diagnostic-core.o: \
		$(CDK2_DIR)/src/lib/diagnostic.c $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none -maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"
$(CDK2_NATIVE_FTW_PE): $(CDK2_NATIVE_FTW_CORE_OBJ) $(CDK2_NATIVE_FTW_PI_OBJ) $(CDK2_NATIVE_FTW_FVB_OBJ) $(CDK2_NATIVE_FTW_GEOMETRY_OBJ) $(CDK2_NATIVE_FTW_ENTRY_OBJ) $(CDK2_NATIVE_FTW_DIAGNOSTIC_OBJ) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_FTW_DIAG_CORE)) $(CDK2_DIR)/src/modules/ftw/ftw.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 12 --entry cdk2_ftw_entry --image-base 0 --section-alignment 0x1000 --file-alignment 0x1000 --build-id=none --no-insert-timestamp -s --nxcompat -T "$(CDK2_DIR)/src/modules/ftw/ftw.ld" -o "$@" $(CDK2_NATIVE_FTW_CORE_OBJ) $(CDK2_NATIVE_FTW_PI_OBJ) $(CDK2_NATIVE_FTW_FVB_OBJ) $(CDK2_NATIVE_FTW_GEOMETRY_OBJ) $(CDK2_NATIVE_FTW_ENTRY_OBJ) $(CDK2_NATIVE_FTW_DIAGNOSTIC_OBJ) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_FTW_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 12 "$@"

native-check: native-ftw-test

CDK2_NATIVE_CON_PLATFORM_PATH_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-con-platform-path-test
CDK2_NATIVE_CON_PLATFORM_POLICY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-con-platform-policy-test
CDK2_NATIVE_CON_PLATFORM_BINDING_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-con-platform-binding-test
CDK2_NATIVE_CON_PLATFORM_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-con-platform-entry-test
CDK2_NATIVE_CON_PLATFORM_OBJS := $(CDK2_NATIVE_BUILD_DIR)/con-platform-path.o \
	$(CDK2_NATIVE_BUILD_DIR)/con-platform-policy.o \
	$(CDK2_NATIVE_BUILD_DIR)/con-platform-binding.o \
	$(CDK2_NATIVE_BUILD_DIR)/con-platform-entry.o
CDK2_NATIVE_CON_PLATFORM_PE ?= $(CDK2_NATIVE_BUILD_DIR)/ConPlatformDxe.efi

.PHONY: native-con-platform-test
native-check: native-con-platform-profile-test
.PHONY: native-con-platform-profile-test
native-con-platform-profile-test:
	@python3 "$(CDK2_DIR)/tests/con_platform_profile_test.py"

native-con-platform-test: $(CDK2_NATIVE_CON_PLATFORM_PATH_TEST) \
		$(CDK2_NATIVE_CON_PLATFORM_POLICY_TEST) \
		$(CDK2_NATIVE_CON_PLATFORM_BINDING_TEST) \
		$(CDK2_NATIVE_CON_PLATFORM_ENTRY_TEST)
	@"$(CDK2_NATIVE_CON_PLATFORM_PATH_TEST)"
	@"$(CDK2_NATIVE_CON_PLATFORM_POLICY_TEST)"
	@"$(CDK2_NATIVE_CON_PLATFORM_BINDING_TEST)"
	@"$(CDK2_NATIVE_CON_PLATFORM_ENTRY_TEST)"

$(CDK2_NATIVE_CON_PLATFORM_PATH_TEST): $(CDK2_DIR)/tests/con_platform_path_test.c \
		$(CDK2_DIR)/src/modules/con_platform/path.c \
		$(CDK2_DIR)/include/cdk2/con_platform.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" "$<" "$(CDK2_DIR)/src/modules/con_platform/path.c"

$(CDK2_NATIVE_CON_PLATFORM_POLICY_TEST): $(CDK2_DIR)/tests/con_platform_policy_test.c \
		$(CDK2_DIR)/src/modules/con_platform/policy.c \
		$(CDK2_DIR)/include/cdk2/con_platform.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" "$<" "$(CDK2_DIR)/src/modules/con_platform/policy.c"

$(CDK2_NATIVE_CON_PLATFORM_BINDING_TEST): $(CDK2_DIR)/tests/con_platform_binding_test.c \
		$(CDK2_DIR)/src/modules/con_platform/binding.c \
		$(CDK2_DIR)/include/cdk2/con_platform.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" "$<" "$(CDK2_DIR)/src/modules/con_platform/binding.c"

$(CDK2_NATIVE_CON_PLATFORM_ENTRY_TEST): $(CDK2_DIR)/tests/con_platform_entry_test.c \
		$(CDK2_DIR)/src/modules/con_platform/entry.c \
		$(CDK2_DIR)/src/modules/con_platform/binding.c \
		$(CDK2_DIR)/src/modules/con_platform/policy.c \
		$(CDK2_DIR)/src/modules/con_platform/path.c \
		$(CDK2_DIR)/include/cdk2/con_platform.h \
		$(CDK2_DIR)/include/cdk2/con_platform_entry.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-o "$@" "$<" "$(CDK2_DIR)/src/modules/con_platform/entry.c" \
		"$(CDK2_DIR)/src/modules/con_platform/binding.c" \
		"$(CDK2_DIR)/src/modules/con_platform/policy.c" \
		"$(CDK2_DIR)/src/modules/con_platform/path.c"


$(CDK2_NATIVE_BUILD_DIR)/con-platform-%.o: \
		$(CDK2_DIR)/src/modules/con_platform/%.c \
		$(CDK2_DIR)/include/cdk2/con_platform.h \
		$(CDK2_DIR)/include/cdk2/con_platform_entry.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_CON_PLATFORM_PE): $(CDK2_NATIVE_CON_PLATFORM_OBJS) \
		$(CDK2_DIR)/src/modules/con_platform/con_platform.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_con_platform_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x200 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/con_platform/con_platform.ld" -o "$@" \
		$(CDK2_NATIVE_CON_PLATFORM_OBJS)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


native-check: native-con-platform-test

CDK2_NATIVE_CON_SPLITTER_MODEL_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-con-splitter-model-test
CDK2_NATIVE_CON_SPLITTER_INPUT_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-con-splitter-input-test

$(CDK2_NATIVE_CON_SPLITTER_MODEL_TEST): $(CDK2_DIR)/tests/con_splitter_model_test.c \
		$(CDK2_DIR)/src/modules/con_splitter/model.c \
		$(CDK2_DIR)/include/cdk2/con_splitter.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/con_splitter/model.c"

$(CDK2_NATIVE_CON_SPLITTER_INPUT_TEST): $(CDK2_DIR)/tests/con_splitter_input_test.c \
		$(CDK2_DIR)/src/modules/con_splitter/input.c \
		$(CDK2_DIR)/include/cdk2/con_splitter.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/con_splitter/input.c"

CDK2_NATIVE_CON_SPLITTER_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-con-splitter-entry-test
CDK2_NATIVE_CON_SPLITTER_GOP_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-con-splitter-gop-test
CDK2_NATIVE_CON_SPLITTER_BINDING_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-con-splitter-binding-test
CDK2_NATIVE_CON_SPLITTER_DIAGNOSTIC_TEST ?= \
	$(CDK2_DIR)/tests/con_splitter_first_record_profile_test.sh
CDK2_NATIVE_CON_SPLITTER_OBJS := $(CDK2_NATIVE_BUILD_DIR)/con-splitter-model.o \
	$(CDK2_NATIVE_BUILD_DIR)/con-splitter-input.o \
	$(CDK2_NATIVE_BUILD_DIR)/con-splitter-gop.o \
	$(CDK2_NATIVE_BUILD_DIR)/con-splitter-binding.o \
	$(CDK2_NATIVE_BUILD_DIR)/con-splitter-entry.o
CDK2_NATIVE_CON_SPLITTER_PE ?= $(CDK2_NATIVE_BUILD_DIR)/ConSplitterDxe.efi

.PHONY: native-con-splitter-test
native-con-splitter-test: $(CDK2_NATIVE_CON_SPLITTER_MODEL_TEST) \
		$(CDK2_NATIVE_CON_SPLITTER_INPUT_TEST) $(CDK2_NATIVE_CON_SPLITTER_GOP_TEST) \
		$(CDK2_NATIVE_CON_SPLITTER_BINDING_TEST) \
		$(CDK2_NATIVE_CON_SPLITTER_ENTRY_TEST) \
		$(CDK2_NATIVE_CON_SPLITTER_DIAGNOSTIC_TEST)
	@"$(CDK2_NATIVE_CON_SPLITTER_MODEL_TEST)"
	@"$(CDK2_NATIVE_CON_SPLITTER_INPUT_TEST)"
	@"$(CDK2_NATIVE_CON_SPLITTER_GOP_TEST)"
	@"$(CDK2_NATIVE_CON_SPLITTER_BINDING_TEST)"
	@"$(CDK2_NATIVE_CON_SPLITTER_ENTRY_TEST)"
	@sh "$(CDK2_NATIVE_CON_SPLITTER_DIAGNOSTIC_TEST)" \
		"$(CDK2_NATIVE_HOST_CC)" "$(CDK2_DIR)" \
		"$(CDK2_BUILD_DIR)/include/cdk2/config.h"

$(CDK2_NATIVE_CON_SPLITTER_GOP_TEST): $(CDK2_DIR)/tests/con_splitter_gop_test.c \
		$(CDK2_DIR)/src/modules/con_splitter/gop.c \
		$(CDK2_DIR)/include/cdk2/con_splitter.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/con_splitter/gop.c"

$(CDK2_NATIVE_CON_SPLITTER_BINDING_TEST): \
		$(CDK2_DIR)/tests/con_splitter_binding_test.c \
		$(CDK2_DIR)/src/modules/con_splitter/binding.c \
		$(CDK2_DIR)/include/cdk2/con_splitter_binding.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/con_splitter/binding.c"

$(CDK2_NATIVE_CON_SPLITTER_ENTRY_TEST): $(CDK2_DIR)/tests/con_splitter_entry_test.c \
		$(CDK2_DIR)/src/modules/con_splitter/model.c \
		$(CDK2_DIR)/src/modules/con_splitter/input.c \
		$(CDK2_DIR)/src/modules/con_splitter/gop.c \
		$(CDK2_DIR)/src/modules/con_splitter/entry.c \
		$(CDK2_DIR)/src/modules/con_splitter/binding.c \
		$(CDK2_DIR)/include/cdk2/con_splitter.h \
		$(CDK2_DIR)/include/cdk2/con_splitter_entry.h \
		$(CDK2_DIR)/include/cdk2/con_splitter_binding.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/con_splitter/model.c" \
		"$(CDK2_DIR)/src/modules/con_splitter/input.c" \
		"$(CDK2_DIR)/src/modules/con_splitter/gop.c" \
		"$(CDK2_DIR)/src/modules/con_splitter/binding.c" \
		"$(CDK2_DIR)/src/modules/con_splitter/entry.c"

$(CDK2_NATIVE_BUILD_DIR)/con-splitter-%.o: \
		$(CDK2_DIR)/src/modules/con_splitter/%.c \
		$(CDK2_DIR)/include/cdk2/con_splitter.h \
		$(CDK2_DIR)/include/cdk2/con_splitter_entry.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args -Oz \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_CON_SPLITTER_PE): $(CDK2_NATIVE_CON_SPLITTER_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/con_splitter/con_splitter.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_con_splitter_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x200 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/con_splitter/con_splitter.ld" -o "$@" \
		$(CDK2_NATIVE_CON_SPLITTER_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_MODULE_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


native-check: native-con-splitter-test

CDK2_NATIVE_EC_BATTERY_MODEL_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ec-battery-model-test
CDK2_NATIVE_EC_BATTERY_TRANSPORT_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ec-battery-transport-test
CDK2_NATIVE_EC_BATTERY_CONFIG_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-ec-battery-config-test
CDK2_NATIVE_EC_BATTERY_DIAGNOSTIC_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/ec-battery-diagnostic-test

.PHONY: native-ec-battery-test native-ec-battery-kconfig-test
native-ec-battery-test: $(CDK2_NATIVE_EC_BATTERY_MODEL_TEST) \
		$(CDK2_NATIVE_EC_BATTERY_TRANSPORT_TEST) \
		$(CDK2_NATIVE_EC_BATTERY_CONFIG_TEST) \
		 native-ec-battery-kconfig-test \
		$(CDK2_DIR)/tests/ec_battery_entry_lifecycle_test.c \
		$(CDK2_DIR)/tests/ec_battery_entry_lifecycle_test.sh \
		$(CDK2_DIR)/tests/ec_battery_config_isolation_test.sh
	@"$(CDK2_NATIVE_EC_BATTERY_MODEL_TEST)"
	@"$(CDK2_NATIVE_EC_BATTERY_TRANSPORT_TEST)"
	@"$(CDK2_NATIVE_EC_BATTERY_CONFIG_TEST)"
	@"$(CDK2_DIR)/tests/ec_battery_entry_lifecycle_test.sh" \
		"$(CDK2_CONFIG_HEADER)"
	@sh "$(CDK2_DIR)/tests/ec_battery_config_isolation_test.sh"

native-ec-battery-kconfig-test:
	@$(MAKE) --no-print-directory -f "$(CDK2_ROOT)/Makefile" \
		CDK2_BUILD_DIR="$(CDK2_NATIVE_BUILD_DIR)/ec-battery-sbs-config" \
		CDK2_CONFIG="$(CDK2_NATIVE_BUILD_DIR)/ec-battery-sbs-config/.config" \
		CDK2_CONFIG_HEADER="$(CDK2_NATIVE_BUILD_DIR)/ec-battery-sbs-config/include/cdk2/config.h" \
		CDK2_CONFIG_READY= \
		CDK2_DEFCONFIG="$(CDK2_DIR)/tests/ec_battery_sbs_defconfig" defconfig >/dev/null
	@grep -q '^#define CONFIG_CDK2_EC_ACPI_BATTERY_PROFILE 5$$' \
		"$(CDK2_NATIVE_BUILD_DIR)/ec-battery-sbs-config/include/cdk2/config.h"
	@grep -q '^#define CONFIG_CDK2_EC_BATTERY_DATA_PORT 0x72$$' \
		"$(CDK2_NATIVE_BUILD_DIR)/ec-battery-sbs-config/include/cdk2/config.h"
	@grep -q '^#define CONFIG_CDK2_EC_BATTERY_COMMAND_PORT 0x76$$' \
		"$(CDK2_NATIVE_BUILD_DIR)/ec-battery-sbs-config/include/cdk2/config.h"
	@grep -q '^#define CONFIG_CDK2_EC_BATTERY_SBS_WORD_READ_COMMAND 0x90$$' \
		"$(CDK2_NATIVE_BUILD_DIR)/ec-battery-sbs-config/include/cdk2/config.h"

.PHONY: native-ec-battery-diagnostic-parity
native-ec-battery-diagnostic-parity: $(CDK2_NATIVE_EC_BATTERY_PE) \
		$(CDK2_NATIVE_EC_BATTERY_DIAGNOSTIC_TEST)
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" \
		"$(CDK2_DIR)/migration/ec-battery-diagnostic-parity.tsv" 27 \
		"$(CDK2_DIR)/src/modules/ec_battery"
	@if [ "$(CONFIG_CDK2_BUILD_DEBUG)" = y ]; then \
		"$(CDK2_NATIVE_EC_BATTERY_DIAGNOSTIC_TEST)" "$(CDK2_NATIVE_EC_BATTERY_PE)"; fi

$(CDK2_NATIVE_EC_BATTERY_MODEL_TEST): \
		$(CDK2_DIR)/tests/ec_battery_model_test.c \
		$(CDK2_DIR)/src/modules/ec_battery/model.c \
		$(CDK2_DIR)/include/cdk2/ec_battery.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/ec_battery/model.c"

$(CDK2_NATIVE_EC_BATTERY_TRANSPORT_TEST): \
		$(CDK2_DIR)/tests/ec_battery_transport_test.c \
		$(CDK2_DIR)/src/modules/ec_battery/transport.c \
		$(CDK2_DIR)/include/cdk2/ec_battery.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/ec_battery/transport.c"

$(CDK2_NATIVE_EC_BATTERY_CONFIG_TEST): \
		$(CDK2_DIR)/tests/ec_battery_config_test.c $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<"

$(CDK2_NATIVE_EC_BATTERY_DIAGNOSTIC_TEST): \
		$(CDK2_DIR)/tests/ec_battery_diagnostic_test.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -o "$@" "$<"

native-check: native-ec-battery-test

CDK2_NATIVE_BDS_MODEL_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-bds-model-test
CDK2_NATIVE_BDS_ENTRY_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-bds-entry-test
CDK2_NATIVE_BDS_DISABLED_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-bds-disabled-test
CDK2_NATIVE_BDS_CAPSULE_DISK_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-bds-capsule-disk-test
CDK2_NATIVE_BDS_SETTINGS_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-bds-settings-test
CDK2_SECURE_BOOT_KEY_DIR := $(CDK2_DIR)/src/modules/bds/keys
CDK2_SECURE_BOOT_KEY_FILES := \
	pk_microsoft_oem_2023.der \
	kek_microsoft_2011.der \
	kek_microsoft_2023.der \
	kek_microsoft_uefi_2023.der \
	db_microsoft_uefi_2011.der \
	db_microsoft_uefi_2023.der \
	db_microsoft_win_2011.der \
	db_microsoft_win_uefi_2023.der \
	dbx_microsoft_update.bin
CDK2_NATIVE_SECURE_BOOT_KEY_OBJS := $(addprefix \
	$(CDK2_NATIVE_BUILD_DIR)/secure-boot-key-,$(addsuffix .o, \
	$(CDK2_SECURE_BOOT_KEY_FILES)))
CDK2_NATIVE_BDS_OBJS := $(CDK2_NATIVE_BUILD_DIR)/bds-model.o \
	$(CDK2_NATIVE_BUILD_DIR)/bds-entry.o \
	$(CDK2_NATIVE_BUILD_DIR)/bds-diagnostic.o
ifeq ($(CONFIG_CDK2_NATIVE_LVGL_SETUP),y)
CDK2_NATIVE_BDS_OBJS += \
	$(CDK2_NATIVE_BUILD_DIR)/bds-settings.o \
	$(CDK2_NATIVE_BUILD_DIR)/bds-lvgl-form.o \
	$(CDK2_NATIVE_BUILD_DIR)/bds-lvgl-settings.o
endif
ifeq ($(CONFIG_CDK2_SECURE_BOOT),y)
CDK2_NATIVE_BDS_OBJS += $(CDK2_NATIVE_BUILD_DIR)/bds-secure_boot.o \
	$(CDK2_NATIVE_BUILD_DIR)/signature-database.o \
	$(CDK2_NATIVE_SECURE_BOOT_KEY_OBJS)
endif
CDK2_NATIVE_BDS_PE ?= $(CDK2_NATIVE_BUILD_DIR)/BdsDxe.efi
CDK2_NATIVE_BDS_QEMU_OBJ ?= $(CDK2_NATIVE_BUILD_DIR)/bds-qemu.o
CDK2_NATIVE_BDS_QEMU_PE ?= $(CDK2_NATIVE_BUILD_DIR)/bds-qemu.efi

.PHONY: native-bds-test native-bds-settings-test native-bds-oracle \
	native-bds-setup-contract-test native-bds-setup-mutants-test
native-bds-setup-contract-test: $(CDK2_NATIVE_BDS_PE)
	@sh "$(CDK2_DIR)/tests/bds_native_setup_contract_test.sh" \
		"$(CDK2_NATIVE_BUILD_DIR)/bds-entry.o" "$(CDK2_NATIVE_BDS_PE)" \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_BDS_OBJS)"

native-bds-setup-mutants-test:
	@sh "$(CDK2_DIR)/tests/bds_native_setup_mutants_test.sh"

native-bds-settings-test: $(CDK2_NATIVE_BDS_SETTINGS_TEST)
	@"$(CDK2_NATIVE_BDS_SETTINGS_TEST)"

native-bds-test: native-bds-settings-test native-bds-setup-contract-test \
		native-bds-setup-mutants-test \
		$(CDK2_NATIVE_BDS_MODEL_TEST) $(CDK2_NATIVE_BDS_ENTRY_TEST) \
		$(CDK2_NATIVE_BDS_DISABLED_TEST) $(CDK2_NATIVE_BDS_CAPSULE_DISK_TEST)
	@"$(CDK2_NATIVE_BDS_MODEL_TEST)"
	@"$(CDK2_NATIVE_BDS_ENTRY_TEST)"
	@"$(CDK2_NATIVE_BDS_DISABLED_TEST)"
	@"$(CDK2_NATIVE_BDS_CAPSULE_DISK_TEST)"

$(CDK2_NATIVE_BDS_SETTINGS_TEST): $(CDK2_DIR)/tests/bds_settings_test.c \
		$(CDK2_DIR)/src/modules/bds/settings.c \
		$(CDK2_DIR)/src/modules/lvgl_setup/settings.c \
		$(CDK2_DIR)/src/modules/lvgl_setup/form.c \
		$(CDK2_DIR)/include/cdk2/bds_settings.h \
		$(CDK2_DIR)/include/cdk2/lvgl_settings.h \
		$(CDK2_DIR)/include/cdk2/lvgl_settings_form.h \
		$(CDK2_DIR)/include/cdk2/lvgl_renderer.h \
		$(CDK2_DIR)/include/cdk2/lvgl_ui.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		$(CDK2_DIR)/src/modules/bds/settings.c \
		$(CDK2_DIR)/src/modules/lvgl_setup/settings.c \
		$(CDK2_DIR)/src/modules/lvgl_setup/form.c

$(CDK2_NATIVE_BDS_MODEL_TEST): $(CDK2_DIR)/tests/bds_model_test.c \
		$(CDK2_DIR)/src/modules/bds/model.c \
		$(CDK2_DIR)/include/cdk2/bds.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/bds/model.c"

$(CDK2_NATIVE_BUILD_DIR)/bds-lvgl-settings.o: \
		$(CDK2_DIR)/src/modules/lvgl_setup/settings.c \
		$(CDK2_DIR)/include/cdk2/lvgl_settings.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BDS_ENTRY_TEST): $(CDK2_DIR)/tests/bds_entry_test.c \
		$(CDK2_DIR)/src/modules/bds/entry.c \
		$(CDK2_DIR)/src/modules/bds/model.c \
		$(CDK2_DIR)/src/modules/con_splitter/input.c \
		$(CDK2_DIR)/src/modules/usb_keyboard/model.c \
		$(CDK2_DIR)/src/modules/bds/settings.c \
		$(CDK2_DIR)/src/modules/bds/secure_boot.c \
		$(CDK2_DIR)/src/lib/signature_database.c \
		$(CDK2_DIR)/include/cdk2/signature_database.h \
		$(CDK2_NATIVE_SECURE_BOOT_KEY_OBJS) \
		$(CDK2_DIR)/src/modules/lvgl_setup/settings.c \
		$(CDK2_DIR)/src/modules/lvgl_setup/form.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/bds.h \
		$(CDK2_DIR)/include/cdk2/con_splitter.h \
		$(CDK2_DIR)/include/cdk2/usb_keyboard.h \
		$(CDK2_DIR)/include/cdk2/usb_bus.h \
		$(CDK2_DIR)/include/cdk2/xhci.h \
		$(CDK2_DIR)/include/cdk2/terminal.h \
		$(CDK2_DIR)/include/cdk2/bds_settings.h \
		$(CDK2_DIR)/include/cdk2/lvgl_settings.h \
		$(CDK2_DIR)/include/cdk2/lvgl_settings_form.h \
		$(CDK2_DIR)/include/cdk2/lvgl_renderer.h \
		$(CDK2_DIR)/include/cdk2/lvgl_ui.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/bds/model.c" \
		"$(CDK2_DIR)/src/modules/con_splitter/input.c" \
		"$(CDK2_DIR)/src/modules/usb_keyboard/model.c" \
		"$(CDK2_DIR)/src/lib/diagnostic.c" \
		"$(CDK2_DIR)/src/modules/bds/settings.c" \
		"$(CDK2_DIR)/src/modules/bds/secure_boot.c" \
		"$(CDK2_DIR)/src/lib/signature_database.c" \
		$(CDK2_NATIVE_SECURE_BOOT_KEY_OBJS) \
		"$(CDK2_DIR)/src/modules/lvgl_setup/settings.c" \
		"$(CDK2_DIR)/src/modules/lvgl_setup/form.c"

$(CDK2_NATIVE_BDS_DISABLED_TEST): $(CDK2_NATIVE_BDS_ENTRY_TEST)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		-DCDK2_BDS_TEST_SETUP_DISABLED $(CDK2_NATIVE_INCLUDES) -o "$@" \
		"$(CDK2_DIR)/tests/bds_entry_test.c" \
		"$(CDK2_DIR)/src/modules/bds/model.c" \
		"$(CDK2_DIR)/src/modules/con_splitter/input.c" \
		"$(CDK2_DIR)/src/modules/usb_keyboard/model.c" \
		"$(CDK2_DIR)/src/lib/diagnostic.c" \
		"$(CDK2_DIR)/src/modules/bds/settings.c" \
		"$(CDK2_DIR)/src/modules/bds/secure_boot.c" \
		"$(CDK2_DIR)/src/lib/signature_database.c" \
		$(CDK2_NATIVE_SECURE_BOOT_KEY_OBJS) \
		"$(CDK2_DIR)/src/modules/lvgl_setup/settings.c" \
		"$(CDK2_DIR)/src/modules/lvgl_setup/form.c"

$(CDK2_NATIVE_BDS_CAPSULE_DISK_TEST): $(CDK2_NATIVE_BDS_ENTRY_TEST)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		-DCDK2_BDS_TEST_CAPSULE_DISK $(CDK2_NATIVE_INCLUDES) -o "$@" \
		"$(CDK2_DIR)/tests/bds_entry_test.c" \
		"$(CDK2_DIR)/src/modules/bds/model.c" \
		"$(CDK2_DIR)/src/modules/con_splitter/input.c" \
		"$(CDK2_DIR)/src/modules/usb_keyboard/model.c" \
		"$(CDK2_DIR)/src/lib/diagnostic.c" \
		"$(CDK2_DIR)/src/modules/bds/settings.c" \
		"$(CDK2_DIR)/src/modules/bds/secure_boot.c" \
		"$(CDK2_DIR)/src/lib/signature_database.c" \
		$(CDK2_NATIVE_SECURE_BOOT_KEY_OBJS) \
		"$(CDK2_DIR)/src/modules/lvgl_setup/settings.c" \
		"$(CDK2_DIR)/src/modules/lvgl_setup/form.c"


$(CDK2_NATIVE_BUILD_DIR)/bds-%.o: $(CDK2_DIR)/src/modules/bds/%.c \
		$(CDK2_DIR)/include/cdk2/bds.h $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/signature-database.o: \
		$(CDK2_DIR)/src/lib/signature_database.c \
		$(CDK2_DIR)/include/cdk2/signature_database.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/secure-boot-key-%.o: \
		$(CDK2_SECURE_BOOT_KEY_DIR)/% | $(CDK2_NATIVE_BUILD_DIR)
	@cd "$(CDK2_SECURE_BOOT_KEY_DIR)" && $(CDK2_NATIVE_OBJCOPY) \
		-I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.rodata.secure_boot_defaults,alloc,load,readonly,data,contents \
		"$(notdir $<)" "$@"

$(CDK2_NATIVE_BUILD_DIR)/bds-diagnostic.o: $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/diagnostic.h $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/bds-lvgl-form.o: \
		$(CDK2_DIR)/src/modules/lvgl_setup/form.c \
		$(CDK2_DIR)/include/cdk2/lvgl_settings_form.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BDS_PE): $(CDK2_NATIVE_BDS_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem.o \
		$(CDK2_DIR)/src/modules/bds/bds.ld $(CDK2_DIR)/src/boot/Makefile \
		$(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_bds_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x200 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/bds/bds.ld" -o "$@" \
		$(CDK2_NATIVE_BDS_OBJS) $(CDK2_NATIVE_BUILD_DIR)/mem.o
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


$(CDK2_NATIVE_BDS_QEMU_OBJ): $(CDK2_DIR)/tests/bds_qemu.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_BDS_QEMU_PE): $(CDK2_NATIVE_BDS_QEMU_OBJ) \
		$(CDK2_DIR)/tests/ata_atapi_qemu.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry bds_qemu_entry --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x1000 --build-id=none \
		--no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/tests/ata_atapi_qemu.ld" -o "$@" "$<"
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

native-bds-oracle: $(CDK2_NATIVE_BDS_QEMU_PE)

native-check: native-bds-test

CDK2_NATIVE_SECURE_BOOT_STATE_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-secure-boot-state-test
CDK2_NATIVE_SECURE_BOOT_DEFAULTS_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-secure-boot-defaults-test

.PHONY: native-secure-boot-state-test
native-check: native-secure-boot-state-test
native-secure-boot-state-test: $(CDK2_NATIVE_SECURE_BOOT_STATE_TEST) \
		$(CDK2_NATIVE_SECURE_BOOT_DEFAULTS_TEST)
	@cd "$(CDK2_SECURE_BOOT_KEY_DIR)" && sha256sum -c sha256sums >/dev/null
	@"$(CDK2_NATIVE_SECURE_BOOT_STATE_TEST)"
	@"$(CDK2_NATIVE_SECURE_BOOT_DEFAULTS_TEST)"

$(CDK2_NATIVE_SECURE_BOOT_STATE_TEST): \
		$(CDK2_DIR)/tests/secure_boot_state_test.c \
		$(CDK2_DIR)/src/modules/bds/secure_boot.c \
		$(CDK2_DIR)/src/lib/signature_database.c \
		$(CDK2_DIR)/include/cdk2/signature_database.h \
		$(CDK2_DIR)/include/cdk2/bds_secure_boot.h \
		$(CDK2_NATIVE_SECURE_BOOT_KEY_OBJS) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/bds/secure_boot.c" \
		"$(CDK2_DIR)/src/lib/signature_database.c" \
		$(CDK2_NATIVE_SECURE_BOOT_KEY_OBJS)

$(CDK2_NATIVE_SECURE_BOOT_DEFAULTS_TEST): \
		$(CDK2_DIR)/tests/secure_boot_defaults_test.c \
		$(CDK2_DIR)/src/modules/bds/secure_boot.c \
		$(CDK2_DIR)/src/lib/signature_database.c \
		$(CDK2_DIR)/include/cdk2/signature_database.h \
		$(CDK2_DIR)/include/cdk2/bds_secure_boot.h \
		$(CDK2_NATIVE_SECURE_BOOT_KEY_OBJS) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/bds/secure_boot.c" \
		"$(CDK2_DIR)/src/lib/signature_database.c" \
		$(CDK2_NATIVE_SECURE_BOOT_KEY_OBJS)

.PHONY: native-secure-boot-setup-profile-test
native-secure-boot-setup-profile-test:
	@set -e; build_dir=$$(mktemp -d /tmp/cdk2-secure-setup.XXXXXX); \
	trap 'rm -rf "$$build_dir"' EXIT; \
	env -i PATH="$$PATH" HOME="$$HOME" /usr/bin/make --no-print-directory \
		-C "$(CDK2_DIR)" CDK2_BUILD_DIR="$$build_dir" \
		CDK2_DEFCONFIG="$(CDK2_DIR)/util/qemu/config/cdk2-q35-secure-setup-build.defconfig" \
		native-bds-test native-secure-boot-state-test; \
	grep -qx 'CONFIG_CDK2_SECURE_BOOT_CONFIG=y' "$$build_dir/.config"; \
	grep -qx 'CONFIG_CDK2_NATIVE_LVGL_SETUP=y' "$$build_dir/.config"

CDK2_NATIVE_SHELL_MODEL_TEST ?= $(CDK2_NATIVE_BUILD_DIR)/cdk2-shell-model-test
CDK2_NATIVE_SHELL_OBJS := $(CDK2_NATIVE_BUILD_DIR)/shell-model.o \
	$(CDK2_NATIVE_BUILD_DIR)/shell-entry.o \
	$(CDK2_NATIVE_BUILD_DIR)/shell-diagnostic.o
CDK2_NATIVE_SHELL_DIAG_CORE ?= $(CDK2_NATIVE_BUILD_DIR)/shell-diagnostic-core.o
CDK2_NATIVE_SHELL_PE ?= $(CDK2_NATIVE_BUILD_DIR)/Shell.efi
CDK2_NATIVE_SHELL_GUIDED_GEN ?= $(CDK2_NATIVE_BUILD_DIR)/shell-guided-gen
CDK2_NATIVE_SHELL_GUIDED ?= $(CDK2_NATIVE_BUILD_DIR)/Shell.guided

.PHONY: native-shell-test
native-shell-test: $(CDK2_NATIVE_SHELL_MODEL_TEST)
	@"$(CDK2_NATIVE_SHELL_MODEL_TEST)"

$(CDK2_NATIVE_SHELL_MODEL_TEST): $(CDK2_DIR)/tests/shell_model_test.c \
		$(CDK2_DIR)/src/modules/shell/model.c \
		$(CDK2_DIR)/include/cdk2/shell.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -fshort-wchar \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/shell/model.c"


$(CDK2_NATIVE_BUILD_DIR)/shell-%.o: $(CDK2_DIR)/src/modules/shell/%.c \
		$(CDK2_DIR)/include/cdk2/shell.h \
		$(CDK2_DIR)/include/cdk2/fat_binding.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_SHELL_DIAG_CORE): $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/diagnostic.h $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -c "$<" -o "$@"

$(CDK2_NATIVE_SHELL_PE): $(CDK2_NATIVE_SHELL_OBJS) \
		$(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_SHELL_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/shell/shell.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 10 --entry cdk2_shell_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x200 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/shell/shell.ld" -o "$@" \
		$(CDK2_NATIVE_SHELL_OBJS) $(CDK2_NATIVE_BUILD_DIR)/mem-copy-set.o \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_SHELL_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 10 "$@"

$(CDK2_NATIVE_SHELL_GUIDED_GEN): $(CDK2_DIR)/src/modules/shell/guided.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -o "$@" "$<"

$(CDK2_NATIVE_SHELL_GUIDED): $(CDK2_NATIVE_SHELL_GUIDED_GEN)
	@"$<" "$@"


native-check: native-shell-test

CDK2_NATIVE_DXE_CORE_DATABASE_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-dxe-core-database-test
CDK2_NATIVE_DXE_CORE_EVENT_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-dxe-core-event-test
CDK2_NATIVE_DXE_CORE_MEMORY_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-dxe-core-memory-test
CDK2_NATIVE_DXE_CORE_IMAGE_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-dxe-core-image-test
CDK2_NATIVE_DXE_LIFECYCLE_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-dxe-lifecycle-test
CDK2_NATIVE_DXE_CORE_DISPATCHER_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-dxe-core-dispatcher-test
CDK2_NATIVE_DXE_CORE_ABI_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-dxe-core-abi-test
CDK2_NATIVE_DXE_CORE_CORE_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-dxe-core-core-test
CDK2_NATIVE_PRIVATE_LINKED_PRIMARY_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-private-linked-primary-test
CDK2_NATIVE_DXE_CORE_GCD_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-dxe-core-gcd-test
CDK2_NATIVE_DXE_CORE_ENTRY_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-dxe-core-entry-test
CDK2_NATIVE_DXE_STORAGE_SCOPE_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-dxe-storage-scope-test

ifeq ($(CONFIG_CDK2_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPOSITION),y)
CDK2_NATIVE_DXE_LIFECYCLE_TEST_SOURCES := \
	$(CDK2_DIR)/src/modules/authvar_presence/lifecycle_close_composition.c \
	$(CDK2_DIR)/src/modules/authvar_presence/lifecycle_close_composition_native_x86.c \
	$(CDK2_DIR)/src/modules/authvar_presence/native_handoff.c \
	$(CDK2_DIR)/src/modules/authvar_presence/composition.c \
	$(CDK2_DIR)/src/modules/authvar_presence/bootstrap.c \
	$(CDK2_DIR)/src/modules/authvar_presence/client.c \
	$(CDK2_DIR)/src/lib/payload_mm_authvar_presence.c \
	$(CDK2_DIR)/src/modules/authvar_presence/lifecycle_close_native_x86.c \
	$(CDK2_DIR)/src/modules/authvar_presence/lifecycle_close_boundary.c \
	$(CDK2_DIR)/src/modules/authvar_presence/lifecycle_close_client.c \
	$(CDK2_DIR)/src/lib/payload_mm_authvar_presence_lifecycle_close.c \
	$(CDK2_DIR)/src/boot/coreboot_checksum.c
endif

$(CDK2_NATIVE_DXE_CORE_CORE_TEST) $(CDK2_NATIVE_DXE_CORE_GCD_TEST) \
	$(CDK2_NATIVE_DXE_CORE_ENTRY_TEST): $(CDK2_CONFIG_HEADER)

.PHONY: native-dxe-core-test native-dxe-image-transaction-mutation-test \
	native-dxe-load-image-transaction-mutation-test
native-dxe-image-transaction-mutation-test: $(CDK2_CONFIG_HEADER)
	@"$(CDK2_DIR)/tests/dxe_image_transaction_mutation_test.sh" \
		"$(CDK2_CONFIG_HEADER)"

native-dxe-load-image-transaction-mutation-test: $(CDK2_CONFIG_HEADER)
	@sh "$(CDK2_DIR)/tests/dxe_load_image_transaction_mutation_test.sh" \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_HOST_CC)"

.PHONY: native-dxe-direct-phase-entry-test
native-dxe-direct-phase-entry-test: $(CDK2_CONFIG_HEADER)
	@sh "$(CDK2_DIR)/tests/direct_phase_entry_test.sh" \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_HOST_CC)" \
		"$(CDK2_NATIVE_HOST_CFLAGS)"

native-dxe-core-fixture-family: $(CDK2_NATIVE_DXE_CORE_DATABASE_TEST) \
	$(CDK2_NATIVE_DXE_CORE_EVENT_TEST) $(CDK2_NATIVE_DXE_CORE_MEMORY_TEST) \
	$(CDK2_NATIVE_DXE_CORE_IMAGE_TEST) \
	$(CDK2_NATIVE_DXE_LIFECYCLE_TEST) \
	$(CDK2_NATIVE_DXE_CORE_ABI_TEST) $(CDK2_NATIVE_DXE_CORE_CORE_TEST) \
	$(CDK2_NATIVE_DXE_CORE_GCD_TEST) $(CDK2_NATIVE_DXE_CORE_ENTRY_TEST) \
	$(CDK2_NATIVE_PRIVATE_LINKED_PRIMARY_TEST) \
	 $(CDK2_NATIVE_DXE_STORAGE_SCOPE_TEST) \
	native-dxe-core-entry-mixed-test native-dxe-core-capsule-disk-test \
	native-dxe-image-transaction-mutation-test \
	native-dxe-load-image-transaction-mutation-test \
	native-dxe-direct-phase-entry-test \
	native-dxe-core-framebuffer-test \
	native-dxe-core-disconnect-test
	@"$(CDK2_NATIVE_DXE_STORAGE_SCOPE_TEST)"
	@"$(CDK2_NATIVE_DXE_CORE_ENTRY_TEST)"
	@"$(CDK2_NATIVE_DXE_CORE_GCD_TEST)"
	@"$(CDK2_NATIVE_DXE_CORE_CORE_TEST)"
	@"$(CDK2_NATIVE_PRIVATE_LINKED_PRIMARY_TEST)"
	@"$(CDK2_NATIVE_DXE_CORE_ABI_TEST)"
	@if test "$(CONFIG_CDK2_STRICT_DIRECT_RUNTIME)" != y; then \
		"$(CDK2_NATIVE_DXE_CORE_DISPATCHER_TEST)"; fi
	@"$(CDK2_NATIVE_DXE_LIFECYCLE_TEST)"
	@sh "$(CDK2_DIR)/tests/dxe_lifecycle_profile_test.sh"
	@"$(CDK2_NATIVE_DXE_CORE_IMAGE_TEST)"
	@"$(CDK2_NATIVE_DXE_CORE_DATABASE_TEST)"
	@"$(CDK2_NATIVE_DXE_CORE_EVENT_TEST)"
	@"$(CDK2_NATIVE_DXE_CORE_MEMORY_TEST)"

ifneq ($(CONFIG_CDK2_STRICT_DIRECT_RUNTIME),y)
native-dxe-core-fixture-family: $(CDK2_NATIVE_DXE_CORE_DISPATCHER_TEST)
endif

.PHONY: native-dxe-core-fixture-family native-dxe-core-legacy-fixture-test
ifeq ($(CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME),y)
# The generic family intentionally models legacy Security2 unsigned-image
# positives. Its separate real-Kconfig HOST profile is not protected authority.
# Keep the selected real entry/Core policy counterparts mandatory instead.
native-dxe-core-test: native-dxe-core-legacy-fixture-test \
	native-protected-variable-entry-test
else
native-dxe-core-test: native-dxe-core-fixture-family
endif

native-dxe-core-legacy-fixture-test: $(CDK2_CONFIG_HEADER) \
		$(CDK2_DIR)/tests/dxe_core_legacy_fixture.config \
		$(CDK2_DIR)/tests/dxe_core_legacy_fixture_test.sh
	@HOSTCC="$(CDK2_NATIVE_HOST_CC)" HOSTFLAGS="$(CDK2_NATIVE_HOST_CFLAGS)" \
		MAKE="$(MAKE)" bash "$(CDK2_DIR)/tests/dxe_core_legacy_fixture_test.sh" \
		"$(CDK2_NATIVE_BUILD_DIR)/dxe-core-legacy-fixture"

.PHONY: native-dxe-core-entry-mixed-test
native-dxe-core-entry-mixed-test: $(CDK2_CONFIG_HEADER)
	@sh "$(CDK2_DIR)/tests/dxe_core_entry_mixed_partition_mutation_test.sh" \
		"$(CDK2_CONFIG_HEADER)"

.PHONY: native-dxe-core-capsule-disk-test
native-dxe-core-capsule-disk-test: $(CDK2_CONFIG_HEADER)
	@sh "$(CDK2_DIR)/tests/dxe_core_capsule_disk_handoff_test.sh" \
		"$(CDK2_CONFIG_HEADER)"

.PHONY: native-dxe-core-connect-trace-policy-test
native-dxe-core-connect-trace-policy-test:
	@sh "$(CDK2_DIR)/tests/dxe_core_connect_trace_policy_test.sh"

native-check: native-dxe-core-connect-trace-policy-test

.PHONY: native-linear-apic-timer-boundary-test
native-linear-apic-timer-boundary-test: $(CDK2_CONFIG_HEADER)
	@sh "$(CDK2_DIR)/tests/linear_apic_timer_boundary_test.sh"

native-check: native-linear-apic-timer-boundary-test

.PHONY: native-linear-xhci-poll-boundary-test
native-linear-xhci-poll-boundary-test: $(CDK2_CONFIG_HEADER)
	@sh "$(CDK2_DIR)/tests/linear_xhci_poll_boundary_test.sh" \
		"$(CDK2_CONFIG_HEADER)"

native-check: native-linear-xhci-poll-boundary-test

.PHONY: native-dxe-core-dispatch-policy-test
native-dxe-core-dispatch-policy-test: $(CDK2_CONFIG_HEADER)
	@sh "$(CDK2_DIR)/tests/dxe_core_dispatch_policy_mutation_test.sh" \
		"$(CDK2_CONFIG_HEADER)"

native-check: native-dxe-core-dispatch-policy-test

.PHONY: native-dxe-core-disconnect-test
native-dxe-core-disconnect-test: $(CDK2_CONFIG_HEADER)
	@sh "$(CDK2_DIR)/tests/dxe_core_disconnect_mutation_test.sh" \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_HOST_CC)" "$(CDK2_NATIVE_HOST_CFLAGS)"

.PHONY: native-dxe-core-framebuffer-test
native-dxe-core-framebuffer-test: $(CDK2_CONFIG_HEADER)
	@sh "$(CDK2_DIR)/tests/dxe_framebuffer_owner_mutation_test.sh" \
		"$(CDK2_CONFIG_HEADER)"

$(CDK2_NATIVE_DXE_STORAGE_SCOPE_TEST): \
		$(CDK2_DIR)/tests/dxe_storage_scope_test.c \
		$(CDK2_DIR)/src/modules/dxe_core/database.c \
		$(CDK2_DIR)/src/boot/pe.c \
		$(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/include/cdk2/dxe_image_transaction.h \
		$(CDK2_DIR)/include/cdk2/disk_io.h \
		$(CDK2_DIR)/include/cdk2/fat_binding.h \
		$(CDK2_DIR)/include/cdk2/partition.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/dxe_core/database.c" \
		"$(CDK2_DIR)/src/boot/pe.c" $(CDK2_PE_IMAGE_VIEW_SRC)

$(CDK2_NATIVE_DXE_CORE_DATABASE_TEST): \
		$(CDK2_DIR)/tests/dxe_core_database_test.c \
		$(CDK2_DIR)/src/modules/dxe_core/database.c \
		$(CDK2_DIR)/src/boot/pe.c \
		$(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/include/cdk2/dxe_image_transaction.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/dxe_core/database.c" \
		"$(CDK2_DIR)/src/boot/pe.c" $(CDK2_PE_IMAGE_VIEW_SRC)

.PHONY: native-dxe-core-database-build
native-dxe-core-database-build: $(CDK2_NATIVE_DXE_CORE_DATABASE_TEST)

$(CDK2_NATIVE_DXE_CORE_EVENT_TEST): \
		$(CDK2_DIR)/tests/dxe_core_event_test.c \
		$(CDK2_DIR)/src/modules/dxe_core/event.c \
		$(CDK2_DIR)/include/cdk2/dxe_image_transaction.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/dxe_core/event.c"

$(CDK2_NATIVE_DXE_CORE_MEMORY_TEST): \
		$(CDK2_DIR)/tests/dxe_core_memory_test.c \
		$(CDK2_DIR)/src/modules/dxe_core/memory.c \
		$(CDK2_DIR)/include/cdk2/dxe_image_transaction.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/dxe_core/memory.c"

$(CDK2_NATIVE_DXE_CORE_IMAGE_TEST): \
		$(CDK2_DIR)/tests/dxe_core_image_test.c \
		$(CDK2_DIR)/src/modules/dxe_core/image.c \
		$(CDK2_DIR)/src/modules/dxe_core/memory.c \
		$(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/include/cdk2/dxe_image_transaction.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/dxe_core/image.c" \
		"$(CDK2_DIR)/src/modules/dxe_core/memory.c" \
		$(CDK2_PE_IMAGE_VIEW_SRC)

$(CDK2_NATIVE_DXE_LIFECYCLE_TEST): $(CDK2_DIR)/tests/dxe_lifecycle_test.c \
		$(CDK2_DIR)/src/modules/dxe_core/lifecycle.c \
		$(CDK2_DIR)/src/modules/dxe_core/presence_lifecycle.c \
		$(CDK2_DIR)/include/cdk2/dxe_lifecycle.h \
		$(CDK2_DIR)/include/cdk2/authvar_presence_lifecycle.h \
		| $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-DCDK2_HOST_TEST $(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/dxe_core/lifecycle.c" \
		"$(CDK2_DIR)/src/modules/dxe_core/presence_lifecycle.c"

$(CDK2_NATIVE_DXE_CORE_DISPATCHER_TEST): \
		$(CDK2_DIR)/tests/dxe_core_dispatcher_test.c \
		$(CDK2_DIR)/src/modules/dxe_core/dispatcher.c \
		$(CDK2_DIR)/src/modules/dxe_core/direct_dispatch.c \
		$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_SRC) $(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/dxe_image_transaction.h \
		$(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		"$(CDK2_DIR)/src/modules/dxe_core/dispatcher.c" \
		"$(CDK2_DIR)/src/modules/dxe_core/direct_dispatch.c" \
		"$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_SRC)" "$(CDK2_PE_IMAGE_VIEW_SRC)" \
		"$(CDK2_DIR)/src/lib/diagnostic.c"

$(CDK2_NATIVE_DXE_CORE_ABI_TEST): $(CDK2_DIR)/tests/dxe_core_abi_test.c \
		$(CDK2_DIR)/include/cdk2/dxe_core_abi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<"

CDK2_NATIVE_DXE_CORE_HOST_SOURCES := \
		$(CDK2_DIR)/src/modules/dxe_core/core.c \
		$(CDK2_DIR)/src/modules/dxe_core/presence_lifecycle.c \
		$(CDK2_DIR)/src/modules/dxe_core/database.c \
		$(CDK2_DIR)/src/modules/dxe_core/event.c \
		$(CDK2_DIR)/src/modules/dxe_core/memory.c \
		$(CDK2_DIR)/src/modules/dxe_core/image.c \
		$(CDK2_DIR)/src/modules/dxe_core/gcd.c \
		$(CDK2_DIR)/src/boot/pe.c \
		$(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/src/lib/diagnostic.c

$(CDK2_NATIVE_DXE_CORE_CORE_TEST): $(CDK2_DIR)/tests/dxe_core_core_test.c \
		$(CDK2_NATIVE_DXE_CORE_HOST_SOURCES) \
		$(CDK2_DIR)/include/cdk2/dxe_image_transaction.h \
		$(CDK2_DIR)/include/cdk2/dxe_core_abi.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-DCDK2_HOST_TEST $(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		$(CDK2_NATIVE_DXE_CORE_HOST_SOURCES)

$(CDK2_NATIVE_PRIVATE_LINKED_PRIMARY_TEST): \
		$(CDK2_DIR)/tests/private_linked_primary_test.c \
		$(CDK2_DIR)/tests/dxe_core_core_test.c \
		$(CDK2_NATIVE_DXE_CORE_HOST_SOURCES) \
		$(CDK2_DIR)/src/modules/dxe_core/linked_primary.c \
		$(CDK2_DIR)/src/modules/dxe_core/direct_dispatch.c \
		$(CDK2_DIR)/src/modules/dxe_core/dispatcher.c \
		$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_SRC) \
		$(CDK2_DIR)/src/modules/dxe_core/private_image.h \
		$(CDK2_DIR)/include/cdk2/dxe_image_transaction.h \
		$(CDK2_DIR)/include/cdk2/dxe_core_abi.h $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-DCDK2_HOST_TEST $(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		$(CDK2_NATIVE_DXE_CORE_HOST_SOURCES) \
		$(CDK2_DIR)/src/modules/dxe_core/linked_primary.c \
		$(CDK2_DIR)/src/modules/dxe_core/direct_dispatch.c \
		$(CDK2_DIR)/src/modules/dxe_core/dispatcher.c \
		$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_SRC)

$(CDK2_NATIVE_DXE_CORE_GCD_TEST): $(CDK2_DIR)/tests/dxe_core_gcd_test.c \
		$(CDK2_NATIVE_DXE_LIFECYCLE_TEST_SOURCES) \
		$(CDK2_DIR)/src/modules/dxe_core/gcd.c \
		$(CDK2_DIR)/src/modules/dxe_core/core.c \
		$(CDK2_DIR)/src/modules/dxe_core/presence_lifecycle.c \
		$(CDK2_DIR)/src/modules/dxe_core/database.c \
		$(CDK2_DIR)/src/modules/dxe_core/event.c \
		$(CDK2_DIR)/src/modules/dxe_core/memory.c \
		$(CDK2_DIR)/src/modules/dxe_core/image.c \
		$(CDK2_DIR)/src/boot/pe.c \
		$(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/src/lib/diagnostic.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		$(CDK2_NATIVE_INCLUDES) -o "$@" "$<" \
		$(CDK2_DIR)/src/modules/dxe_core/gcd.c \
		$(CDK2_DIR)/src/modules/dxe_core/core.c \
		$(CDK2_DIR)/src/modules/dxe_core/presence_lifecycle.c \
		$(CDK2_DIR)/src/modules/dxe_core/database.c \
		$(CDK2_DIR)/src/modules/dxe_core/event.c \
		$(CDK2_DIR)/src/modules/dxe_core/memory.c \
		$(CDK2_DIR)/src/modules/dxe_core/image.c \
		$(CDK2_DIR)/src/boot/pe.c \
		$(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_NATIVE_DXE_LIFECYCLE_TEST_SOURCES)

CDK2_NATIVE_DXE_ENTRY_HOST_SOURCES := $(CDK2_DIR)/src/modules/dxe_core/lifecycle.c \
		$(CDK2_DIR)/src/lib/capsule_report.c \
		$(CDK2_DIR)/src/lib/coreboot_hob.c \
		$(CDK2_DIR)/src/boot/coreboot.c \
		$(if $(filter y,$(CONFIG_CDK2_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPOSITION)),,\
			$(CDK2_DIR)/src/boot/coreboot_checksum.c) \
		$(CDK2_DIR)/src/boot/coreboot_resource.c \
		$(CDK2_DIR)/src/modules/system_fmp/transport.c \
		$(CDK2_DIR)/src/modules/authvar_transport/native_x86.c \
		$(CDK2_DIR)/src/modules/dxe_core/presence_lifecycle.c \
		$(CDK2_DIR)/src/modules/dxe_core/core.c \
		$(CDK2_DIR)/src/modules/dxe_core/linked_primary.c \
		$(CDK2_DIR)/src/modules/dxe_core/database.c \
		$(CDK2_DIR)/src/modules/dxe_core/event.c \
		$(CDK2_DIR)/src/modules/dxe_core/memory.c \
		$(CDK2_DIR)/src/modules/dxe_core/image.c \
		$(CDK2_DIR)/src/modules/dxe_core/gcd.c \
		$(if $(filter y,$(CONFIG_CDK2_STRICT_DIRECT_RUNTIME)),,\
			$(CDK2_DIR)/src/modules/dxe_core/fv_protocol.c \
			$(CDK2_DIR)/src/modules/dxe_core/dispatcher.c) \
		$(CDK2_DIR)/src/modules/dxe_core/direct_dispatch.c \
		$(CDK2_DIR)/src/lib/linear_boot.c \
		$(CDK2_DIR)/src/lib/boot_logo.c \
		$(CDK2_DIR)/src/lib/capsule_disk.c \
		$(CDK2_DIR)/src/modules/graphics_output/graphics_output.c \
		$(CDK2_DIR)/src/modules/graphics_output/driver.c \
		$(CDK2_DIR)/src/lib/tpm2_acpi_hob.c \
		$(CDK2_DIR)/src/lib/direct_image_table.c \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/src/lib/deadline.c \
		$(CDK2_DIR)/src/boot/pe.c $(CDK2_PE_IMAGE_VIEW_SRC)
ifeq ($(CONFIG_CDK2_COREBOOT_CAPSULE_PROFILE)$(CONFIG_CDK2_NATIVE_SYSTEM_FMP),yy)
CDK2_NATIVE_DXE_ENTRY_HOST_SOURCES += $(CDK2_SOFTWARE_HASH_SRCS)
endif

$(CDK2_NATIVE_DXE_CORE_ENTRY_TEST): $(CDK2_DIR)/tests/dxe_core_entry_test.c \
		$(CDK2_NATIVE_DXE_LIFECYCLE_TEST_SOURCES) \
		$(CDK2_DIR)/src/modules/dxe_core/entry.c \
		$(CDK2_NATIVE_DXE_ENTRY_HOST_SOURCES) \
		$(CDK2_DIR)/include/guid/deadline_tsc_info.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	$(call cdk2_native_host_test,-DCDK2_HOST_TEST $(CDK2_NATIVE_INCLUDES) $(CDK2_SOFTWARE_HASH_INCLUDES),$(filter %.c,$^))

.PHONY: native-capsule-certified-refusal-test
native-capsule-certified-refusal-test: $(CDK2_CONFIG_HEADER)
	@HOSTCC="$(CDK2_NATIVE_HOST_CC)" sh "$(CDK2_DIR)/tests/capsule_certified_refusal_test.sh" \
		"$(CDK2_CONFIG_HEADER)"

.PHONY: native-splash-status-report-test
native-splash-status-report-test: $(CDK2_CONFIG_HEADER) \
		$(CDK2_NATIVE_DXE_LIFECYCLE_TEST_SOURCES) \
		$(CDK2_DIR)/src/modules/dxe_core/entry.c \
		$(CDK2_DIR)/src/modules/dxe_core/private_control.h \
		$(CDK2_DIR)/include/cdk2/dxe_image_transaction.h \
		$(CDK2_DIR)/include/cdk2/lvgl_ui.h \
		$(CDK2_NATIVE_DXE_ENTRY_HOST_SOURCES) \
		$(CDK2_DIR)/tests/splash_status_report_test.c \
		$(CDK2_DIR)/tests/splash_status_report_test.sh
	@HOSTCC="$(CDK2_NATIVE_HOST_CC)" sh "$(CDK2_DIR)/tests/splash_status_report_test.sh" \
		"$(CDK2_CONFIG_HEADER)" \
		$(CDK2_NATIVE_DXE_LIFECYCLE_TEST_SOURCES) $(CDK2_NATIVE_DXE_ENTRY_HOST_SOURCES)

native-dxe-core-fixture-family: native-splash-status-report-test

.PHONY: native-protected-variable-boot-gate-test
native-protected-variable-boot-gate-test: $(CDK2_CONFIG_HEADER)
	@HOSTCC="$(CDK2_NATIVE_HOST_CC)" sh "$(CDK2_DIR)/tests/protected_variable_boot_gate_test.sh" \
		"$(CDK2_CONFIG_HEADER)" \
		$(CDK2_NATIVE_DXE_LIFECYCLE_TEST_SOURCES) $(CDK2_NATIVE_DXE_ENTRY_HOST_SOURCES)

.PHONY: native-capsule-ram-window-core-test
native-capsule-ram-window-core-test: $(CDK2_CONFIG_HEADER)
	@HOSTCC="$(CDK2_NATIVE_HOST_CC)" \
		sh "$(CDK2_DIR)/tests/capsule_ram_window_core_test.sh" \
		"$(CDK2_CONFIG_HEADER)"

native-dxe-core-test: native-protected-variable-boot-gate-test \
		native-capsule-ram-window-core-test

.PHONY: native-protected-variable-linear-composition-test
native-protected-variable-linear-composition-test:
	@HOSTCC="$(CDK2_NATIVE_HOST_CC)" MAKE="$(MAKE)" \
		bash "$(CDK2_DIR)/tests/protected_variable_linear_composition_test.sh" \
		"$(COREBOOT_CONFIG)" "$(CDK2_NATIVE_BUILD_DIR)/protected-variable-linear-composition"

.PHONY: native-canonical-image-policy-test
native-canonical-image-policy-test: $(CDK2_CONFIG_HEADER)
	@sh "$(CDK2_DIR)/tests/canonical_image_policy_test.sh" \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_HOST_CC)"

native-dxe-core-test: native-canonical-image-policy-test


native-check: native-dxe-core-test

CDK2_NATIVE_DXE_CORE_OBJS := $(CDK2_NATIVE_BUILD_DIR)/dxe-core-entry.o \
	$(CDK2_NATIVE_BUILD_DIR)/coreboot-hob.o \
	$(CDK2_NATIVE_BUILD_DIR)/coreboot.o \
	$(if $(filter y,$(CONFIG_CDK2_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPOSITION)),,\
		$(CDK2_NATIVE_BUILD_DIR)/coreboot_checksum.o) \
	$(CDK2_NATIVE_BUILD_DIR)/coreboot_resource.o \
	$(CDK2_NATIVE_SYSTEM_FMP_TRANSPORT_OBJ) \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-capsule-native-x86.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-core.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-linked_primary.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-lifecycle.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-presence_lifecycle.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-database.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-event.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-memory.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-image.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-gcd.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-direct_dispatch.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-direct-image-table.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-linear_boot.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-boot_logo.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-capsule_disk.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-graphics_output.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-graphics_driver.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-string.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-pe.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-pe_image_view.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-diagnostic.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-deadline.o
ifeq ($(CONFIG_CDK2_COREBOOT_CAPSULE_PROFILE)$(CONFIG_CDK2_NATIVE_SYSTEM_FMP),yy)
CDK2_NATIVE_DXE_CORE_OBJS += $(addprefix $(CDK2_NATIVE_BUILD_DIR)/dxe-core-refusal-hash-,software.o sha1.o sha256.o sha512.o sm3.o)
CDK2_NATIVE_DXE_CORE_OBJS += $(CDK2_NATIVE_BUILD_DIR)/dxe-core-capsule_report.o

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-refusal-hash-software.o: $(CDK2_DIR)/src/lib/tcg_hash/software_hash.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_TCG2_HASH_CFLAGS) -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"
$(CDK2_NATIVE_BUILD_DIR)/dxe-core-refusal-hash-sha1.o: $(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha1.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_TCG2_HASH_CFLAGS) -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"
$(CDK2_NATIVE_BUILD_DIR)/dxe-core-refusal-hash-sha256.o: $(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha256.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_TCG2_HASH_CFLAGS) -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"
$(CDK2_NATIVE_BUILD_DIR)/dxe-core-refusal-hash-sha512.o: $(CDK2_DIR)/src/lib/tcg_hash/vendor/vboot/2sha512.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_TCG2_HASH_CFLAGS) -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"
$(CDK2_NATIVE_BUILD_DIR)/dxe-core-refusal-hash-sm3.o: $(CDK2_DIR)/src/lib/tcg_hash/vendor/linux/lib/crypto/sm3.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_TCG2_HASH_CFLAGS) -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"
endif
ifeq ($(CONFIG_CDK2_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPOSITION),y)
CDK2_NATIVE_DXE_CORE_OBJS += \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-central.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-central-native.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-native-handoff.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-composition.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-bootstrap.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-client.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-abi.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-close-native.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-close-boundary.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-close-client.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-close-abi.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-coreboot-checksum.o
endif
ifneq ($(CONFIG_CDK2_STRICT_DIRECT_RUNTIME),y)
CDK2_NATIVE_DXE_CORE_OBJS += \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-fv_protocol.o \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-dispatcher.o
endif
ifeq ($(CONFIG_CDK2_NATIVE_TCG2),y)
CDK2_NATIVE_DXE_CORE_OBJS += \
	$(CDK2_NATIVE_BUILD_DIR)/dxe-core-tpm2_acpi_hob.o
endif
CDK2_NATIVE_DXE_CORE_PE ?= $(CDK2_NATIVE_BUILD_DIR)/DxeCore.efi
CDK2_NATIVE_DXE_CORE_MAP ?= $(CDK2_NATIVE_BUILD_DIR)/DxeCore.map

# This dependency must follow the image variable definition, since prerequisite
# variables are expanded when Make reads the rule rather than when it runs it.
native-cpu-arch-test: $(CDK2_NATIVE_DXE_CORE_PE)

.PHONY: native-strict-dxe-compat-closure-test
native-check: native-strict-dxe-compat-closure-test
native-strict-dxe-compat-closure-test: $(CDK2_NATIVE_DXE_CORE_PE) \
		$(CDK2_NATIVE_DXE_CORE_MAP) $(CDK2_CONFIG_HEADER)
	@sh "$(CDK2_DIR)/tests/strict_dxe_compat_closure_test.sh" \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_DXE_CORE_MAP)" \
		"$(CDK2_NATIVE_DXE_CORE_PE)" "$(CDK2_DIR)/src/boot/Makefile"
	@sh "$(CDK2_DIR)/tests/strict_dxe_compat_closure_mutation_test.sh" \
		"$(CDK2_DIR)/tests/strict_dxe_compat_closure_test.sh" \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_DXE_CORE_MAP)" \
		"$(CDK2_NATIVE_DXE_CORE_PE)" "$(CDK2_DIR)/src/boot/Makefile"
ifeq ($(CONFIG_CDK2_LINEAR_BOOT),y)

.PHONY: native-linear-smm-bootstrap-test
native-linear-smm-bootstrap-test:
	@sh "$(CDK2_DIR)/tests/linear_smm_bootstrap_test.sh" \
		"$(CDK2_DIR)/src/modules/dxe_core/entry.c" \
		"$(CDK2_DIR)/src/boot/Makefile"

.PHONY: native-linear-pre-capsule-order-test
native-linear-pre-capsule-order-test:
	@sh "$(CDK2_DIR)/tests/linear_pre_capsule_order_test.sh" \
		"$(CDK2_DIR)/src/modules/dxe_core/entry.c"
	@"$(CDK2_DIR)/tests/ram_capsule_order_test.sh" \
		"$(CDK2_DIR)/src/modules/dxe_core/entry.c"

.PHONY: native-linear-security-owner-test
native-linear-security-owner-test:
	@sh "$(CDK2_DIR)/tests/linear_security_owner_test.sh" "$(CDK2_DIR)"

.PHONY: native-linear-retained-tcg2-test
ifeq ($(CONFIG_CDK2_NATIVE_TCG2),y)
native-linear-retained-tcg2-test:
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-I"$(CDK2_DIR)/include" "$(CDK2_DIR)/tests/linear_tcg2_validator_test.c" \
		-o "$(CDK2_NATIVE_BUILD_DIR)/linear-tcg2-validator-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/linear-tcg2-validator-test"
else
native-linear-retained-tcg2-test:
	@sh "$(CDK2_DIR)/tests/linear_retained_tcg2_test.sh" "$(CDK2_DIR)"
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
		-I"$(CDK2_DIR)/include" "$(CDK2_DIR)/tests/linear_tcg2_validator_test.c" \
		-o "$(CDK2_NATIVE_BUILD_DIR)/linear-tcg2-validator-test"
	@"$(CDK2_NATIVE_BUILD_DIR)/linear-tcg2-validator-test"
endif

.PHONY: native-linear-display-provenance-test
native-linear-display-provenance-test:
	@sh "$(CDK2_DIR)/tests/linear_display_provenance_test.sh" "$(CDK2_DIR)"

.PHONY: native-linear-async-ownership-test
native-linear-async-ownership-test:
	@sh "$(CDK2_DIR)/tests/linear_async_ownership_test.sh" \
		"$(CDK2_DIR)/src/modules/dxe_core/entry.c"

.PHONY: native-linear-boot-policy-ownership-test
native-linear-boot-policy-ownership-test: $(CDK2_NATIVE_DXE_CORE_PE)
	@sh "$(CDK2_DIR)/tests/linear_boot_policy_ownership_test.sh" \
		"$(CDK2_DIR)/src/modules/dxe_core/entry.c" \
		"$(CDK2_NATIVE_DXE_CORE_PE)" "$(CDK2_DIR)" "$(CDK2_CONFIG_HEADER)"

endif

.PHONY: native-dxe-core-diagnostic-parity
native-dxe-core-diagnostic-parity:
	@"$(CDK2_DIR)/util/check-dxe-core-diagnostic-plan.sh" \
		"$(CDK2_DIR)/migration/dxe-core-diagnostic-parity.tsv" \
		"$(CDK2_DIR)/include/cdk2/dxe_diagnostic_events.h" \
		"$(CDK2_DIR)/src/modules/dxe_core"

native-check: native-dxe-core-diagnostic-parity

.PHONY: native-dxe-core-graphics-dependency-test
native-dxe-core-graphics-dependency-test: \
		$(CDK2_DIR)/tests/dxe_core_graphics_dependency_test.sh
	@sh "$<" "$(MAKE)" "$(CDK2_DIR)" "$(CDK2_NATIVE_CC)"

native-check: native-dxe-core-graphics-dependency-test

.PHONY: native-dxe-core-link-parallel-test
native-dxe-core-link-parallel-test: $(CDK2_NATIVE_PE_LINK)
	@sh "$(CDK2_DIR)/tests/dxe_core_link_parallel_test.sh" \
		"$(MAKE)" "$(CDK2_DIR)" "$(CDK2_DIR)/src/boot/Makefile" \
		"$(CDK2_NATIVE_PE_LINK)"

native-check: native-dxe-core-link-parallel-test

.PHONY: native-scratch-make-environment-test
native-scratch-make-environment-test:
	@sh "$(CDK2_DIR)/tests/scratch_make_environment_test.sh" \
		"$(CDK2_DIR)/tests/scratch_make_environment.sh"

native-check: native-scratch-make-environment-test

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-entry.o: \
		$(CDK2_DIR)/include/guid/deadline_tsc_info.h

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-capsule-native-x86.o: \
		$(CDK2_DIR)/src/modules/authvar_transport/native_x86.c \
		$(CDK2_DIR)/include/cdk2/authvar_native_x86.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-entry.o \
		$(CDK2_NATIVE_BUILD_DIR)/dxe-core-core.o \
		$(CDK2_NATIVE_BUILD_DIR)/dxe-core-linked_primary.o \
		$(CDK2_NATIVE_DXE_CORE_ENTRY_TEST) $(CDK2_NATIVE_DXE_CORE_CORE_TEST) \
		$(CDK2_NATIVE_DXE_CORE_GCD_TEST) \
		$(CDK2_NATIVE_PRIVATE_LINKED_PRIMARY_TEST): \
		$(CDK2_DIR)/src/modules/dxe_core/private_control.h \
		$(CDK2_DIR)/src/modules/dxe_core/private_image.h \
		$(CDK2_DIR)/include/cdk2/dxe_image_transaction.h \
		$(CDK2_DIR)/include/cdk2/local_apic_timer.h \
		$(CDK2_DIR)/include/cdk2/pci_dma_control.h \
		$(CDK2_DIR)/include/cdk2/xhci_control.h $(CDK2_CONFIG_HEADER)

$(CDK2_NATIVE_BUILD_DIR)/xhci-entry.o: \
		$(CDK2_DIR)/include/cdk2/xhci_control.h $(CDK2_CONFIG_HEADER)

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-%.o: \
		$(CDK2_DIR)/src/modules/dxe_core/%.c \
		$(CDK2_DIR)/include/cdk2/dxe_image_transaction.h \
		$(CDK2_DIR)/include/cdk2/dxe_lifecycle.h \
		$(CDK2_DIR)/include/cdk2/dxe_core_abi.h \
		$(CDK2_DIR)/src/modules/dxe_core/diagnostic.h \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

CDK2_AUTHVAR_PRESENCE_DXE_CFLAGS = \
	$(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
	-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
	$(CDK2_NATIVE_INCLUDES)

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-native-handoff.o: \
		$(CDK2_DIR)/src/modules/authvar_presence/native_handoff.c \
		$(CDK2_DIR)/src/modules/authvar_presence/native_handoff.h \
		$(CDK2_DIR)/include/cdk2/payload_mm_authvar_presence.h \
		$(CDK2_DIR)/include/cdk2/payload_mm_authvar_presence_lifecycle_close.h \
		$(CDK2_DIR)/include/guid/cbmem_table_hob.h \
		$(CDK2_DIR)/include/pi/hob.h \
		$(CDK2_NATIVE_DIR)/coreboot_checksum.h \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_AUTHVAR_PRESENCE_DXE_CFLAGS) -I$(CDK2_NATIVE_DIR) \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-central.o: \
		$(CDK2_DIR)/src/modules/authvar_presence/lifecycle_close_composition.c \
		$(CDK2_DIR)/src/modules/authvar_presence/lifecycle_close_composition_native_x86.h \
		$(CDK2_DIR)/src/modules/authvar_presence/native_handoff.h \
		$(CDK2_DIR)/include/cdk2/authvar_presence_composition.h \
		$(CDK2_DIR)/include/cdk2/authvar_presence_lifecycle_close_boundary.h \
		$(CDK2_DIR)/include/cdk2/authvar_presence_lifecycle_close_composition.h \
		$(CDK2_DIR)/include/cdk2/authvar_presence_lifecycle_close_native_x86.h \
		$(CDK2_DIR)/include/cdk2/dxe_image_transaction.h \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_AUTHVAR_PRESENCE_DXE_CFLAGS) -I$(CDK2_NATIVE_DIR) \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-central-native.o: \
		$(CDK2_DIR)/src/modules/authvar_presence/lifecycle_close_composition_native_x86.c \
		$(CDK2_DIR)/src/modules/authvar_presence/lifecycle_close_composition_native_x86.h \
		$(CDK2_DIR)/src/modules/authvar_presence/bootstrap.h \
		$(CDK2_DIR)/include/cdk2/authvar_presence_composition.h \
		$(CDK2_DIR)/include/cdk2/dxe_core_abi.h \
		$(CDK2_DIR)/include/cdk2/payload_mm_authvar_presence.h \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_AUTHVAR_PRESENCE_DXE_CFLAGS) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-close-native.o: \
		$(CDK2_DIR)/src/modules/authvar_presence/lifecycle_close_native_x86.c \
		$(CDK2_DIR)/include/cdk2/authvar_presence_lifecycle_close_native_x86.h \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_AUTHVAR_PRESENCE_DXE_CFLAGS) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-close-boundary.o: \
		$(CDK2_DIR)/src/modules/authvar_presence/lifecycle_close_boundary.c \
		$(CDK2_DIR)/include/cdk2/authvar_presence_lifecycle_close_boundary.h \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_AUTHVAR_PRESENCE_DXE_CFLAGS) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-close-client.o: \
		$(CDK2_DIR)/src/modules/authvar_presence/lifecycle_close_client.c \
		$(CDK2_DIR)/include/cdk2/payload_mm_authvar_presence_lifecycle_close_client.h \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_AUTHVAR_PRESENCE_DXE_CFLAGS) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-close-abi.o: \
		$(CDK2_DIR)/src/lib/payload_mm_authvar_presence_lifecycle_close.c \
		$(CDK2_DIR)/include/cdk2/payload_mm_authvar_presence_lifecycle_close.h \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_AUTHVAR_PRESENCE_DXE_CFLAGS) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-composition.o: \
		$(CDK2_DIR)/src/modules/authvar_presence/composition.c \
		$(CDK2_DIR)/include/cdk2/authvar_presence_composition.h \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_AUTHVAR_PRESENCE_DXE_CFLAGS) -I$(CDK2_NATIVE_DIR) \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-bootstrap.o: \
		$(CDK2_DIR)/src/modules/authvar_presence/bootstrap.c \
		$(CDK2_DIR)/src/modules/authvar_presence/bootstrap.h \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_AUTHVAR_PRESENCE_DXE_CFLAGS) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-client.o: \
		$(CDK2_DIR)/src/modules/authvar_presence/client.c \
		$(CDK2_DIR)/include/cdk2/payload_mm_authvar_presence_client.h \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_AUTHVAR_PRESENCE_DXE_CFLAGS) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-authvar-presence-abi.o: \
		$(CDK2_DIR)/src/lib/payload_mm_authvar_presence.c \
		$(CDK2_DIR)/include/cdk2/payload_mm_authvar_presence.h \
		$(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_AUTHVAR_PRESENCE_DXE_CFLAGS) -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-coreboot-checksum.o: \
		$(CDK2_NATIVE_DIR)/coreboot_checksum.c \
		$(CDK2_NATIVE_DIR)/coreboot_checksum.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(CDK2_AUTHVAR_PRESENCE_DXE_CFLAGS) -I$(CDK2_NATIVE_DIR) \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-pe.o: $(CDK2_DIR)/src/boot/pe.c \
		$(CDK2_DIR)/src/boot/pe.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-pe_image_view.o: \
		$(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/include/cdk2/pe_image_view.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-direct-image-table.o: \
		$(CDK2_DIR)/src/lib/direct_image_table.c \
		$(CDK2_DIR)/include/cdk2/direct_image_table.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-tpm2_acpi_hob.o: \
		$(CDK2_DIR)/src/lib/tpm2_acpi_hob.c \
		$(CDK2_DIR)/include/cdk2/tpm2_acpi_hob.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-capsule_report.o: \
		$(CDK2_DIR)/src/lib/capsule_report.c \
		$(CDK2_DIR)/include/cdk2/capsule_report.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-linear_boot.o: \
		$(CDK2_DIR)/src/lib/linear_boot.c \
		$(CDK2_DIR)/include/cdk2/linear_boot.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-boot_logo.o: \
		$(CDK2_DIR)/src/lib/boot_logo.c \
		$(CDK2_DIR)/include/cdk2/boot_logo.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-capsule_disk.o: \
		$(CDK2_DIR)/src/lib/capsule_disk.c \
		$(CDK2_DIR)/include/cdk2/capsule_disk.h \
		$(CDK2_DIR)/include/cdk2/english.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" \
		-c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-graphics_output.o: \
		$(CDK2_DIR)/src/modules/graphics_output/graphics_output.c \
		$(CDK2_DIR)/include/cdk2/graphics_output.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-graphics_driver.o: \
		$(CDK2_DIR)/src/modules/graphics_output/driver.c \
		$(CDK2_DIR)/include/cdk2/graphics_output_driver.h \
		$(CDK2_DIR)/include/cdk2/graphics_output.h | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-diagnostic.o: $(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/diagnostic.h $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident \
		-fcf-protection=none -maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) \
		-MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/dxe-core-deadline.o: $(CDK2_DIR)/src/lib/deadline.c \
		$(CDK2_DIR)/include/cdk2/deadline.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,\
		$(CDK2_NATIVE_CFLAGS)) -fno-ident -fcf-protection=none \
		-maccumulate-outgoing-args $(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

# The image and map are outputs of one link, including map-only rebuilds.
$(CDK2_NATIVE_DXE_CORE_PE) $(CDK2_NATIVE_DXE_CORE_MAP) &: \
		$(CDK2_NATIVE_DXE_CORE_OBJS) \
		$(CDK2_DIR)/src/modules/dxe_core/dxe_core.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_dxe_core_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x200 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-Map "$(CDK2_NATIVE_DXE_CORE_MAP)" \
		-T "$(CDK2_DIR)/src/modules/dxe_core/dxe_core.ld" \
		-o "$(CDK2_NATIVE_DXE_CORE_PE)" \
		$(CDK2_NATIVE_DXE_CORE_OBJS)
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$(CDK2_NATIVE_DXE_CORE_PE)"
CDK2_NATIVE_EC_BATTERY_OBJS := $(CDK2_NATIVE_BUILD_DIR)/ec-battery-model.o \
	$(CDK2_NATIVE_BUILD_DIR)/ec-battery-transport.o \
	$(CDK2_NATIVE_BUILD_DIR)/ec-battery-diagnostic.o \
	$(CDK2_NATIVE_BUILD_DIR)/ec-battery-entry.o
CDK2_NATIVE_EC_BATTERY_DIAG_CORE ?= \
	$(CDK2_NATIVE_BUILD_DIR)/ec-battery-diagnostic-core.o
CDK2_NATIVE_EC_BATTERY_PE ?= $(CDK2_NATIVE_BUILD_DIR)/EcAcpiBatteryStatusDxe.efi

$(CDK2_NATIVE_BUILD_DIR)/ec-battery-%.o: \
		$(CDK2_DIR)/src/modules/ec_battery/%.c \
		$(CDK2_DIR)/include/cdk2/ec_battery.h $(CDK2_CONFIG_HEADER) | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(if $(filter y,$(CONFIG_CDK2_BUILD_DEBUG)),-DCDK2_DEBUG) $(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),-DCDK2_DIAGNOSTIC) \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_BUILD_DIR)/ec-battery-diagnostic-core.o: \
		$(CDK2_DIR)/src/lib/diagnostic.c $(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_CC) $(filter-out -fdata-sections -ffunction-sections,$(CDK2_NATIVE_CFLAGS)) \
		-fno-ident -fcf-protection=none -maccumulate-outgoing-args \
		$(CDK2_NATIVE_INCLUDES) -MMD -MP \
		-MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(CDK2_NATIVE_EC_BATTERY_PE): $(CDK2_NATIVE_EC_BATTERY_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_EC_BATTERY_DIAG_CORE)) \
		$(CDK2_DIR)/src/modules/ec_battery/ec_battery.ld $(CDK2_NATIVE_PERELOCCHECK)
	@$(CDK2_NATIVE_PE_LD) --subsystem 11 --entry cdk2_ec_battery_entry \
		--image-base 0 --section-alignment 0x1000 --file-alignment 0x200 \
		--build-id=none --no-insert-timestamp -s --nxcompat \
		-T "$(CDK2_DIR)/src/modules/ec_battery/ec_battery.ld" -o "$@" \
		$(CDK2_NATIVE_EC_BATTERY_OBJS) \
		$(if $(filter y,$(CONFIG_CDK2_DIAGNOSTIC)),$(CDK2_NATIVE_EC_BATTERY_DIAG_CORE))
	@"$(CDK2_NATIVE_PERELOCCHECK)" --subsystem 11 "$@"


# Keep this rule after all module object lists: target names are expanded when
# make parses the rule, so placing it with the early variable declarations
# silently omitted later BDS/shell objects.
$(CDK2_NATIVE_TCG2_OBJS) $(CDK2_NATIVE_ACPI_TABLE_OBJS) \
	$(CDK2_NATIVE_CPU_OBJS) $(CDK2_NATIVE_VARIABLE_RUNTIME_OBJS) \
	$(CDK2_NATIVE_RESET_SYSTEM_OBJ) $(CDK2_NATIVE_RESET_SYSTEM_DIAGNOSTIC_OBJ) \
	$(CDK2_NATIVE_PCAT_RTC_OBJ) $(CDK2_NATIVE_ESRT_OBJ) \
	$(CDK2_NATIVE_TERMINAL_OBJS) $(CDK2_NATIVE_GRAPHICS_OUTPUT_OBJS) \
	$(CDK2_NATIVE_BDS_OBJS) \
	$(CDK2_NATIVE_SHELL_OBJS) \
	$(CDK2_NATIVE_SDMMC_PCI_OBJS) $(CDK2_NATIVE_EMMC_DXE_OBJS) \
	$(CDK2_NATIVE_ATA_OBJS) $(CDK2_NATIVE_ATA_BUS_OBJS) \
	$(CDK2_NATIVE_PARTITION_OBJS) $(CDK2_NATIVE_NVME_OBJS) \
	$(CDK2_NATIVE_SCSI_DISK_OBJS) $(CDK2_NATIVE_FAT_OBJS) \
	$(CDK2_NATIVE_SD_DXE_OBJS) $(CDK2_NATIVE_SATA_OBJS) \
	$(CDK2_NATIVE_DISK_IO_OBJ) \
	$(CDK2_NATIVE_XHCI_OBJS) \
	$(CDK2_NATIVE_UHCI_OBJS) $(CDK2_NATIVE_EHCI_OBJS) \
	$(CDK2_NATIVE_USB_BUS_OBJS) $(CDK2_NATIVE_USB_MASS_OBJS) \
	$(CDK2_NATIVE_PCI_BUS_OBJS) $(CDK2_NATIVE_PCI_HOST_BRIDGE_MODEL_OBJ) \
	$(CDK2_NATIVE_PCI_HOST_BRIDGE_ENTRY_OBJ) \
	$(CDK2_NATIVE_PCI_HOST_BRIDGE_DIAGNOSTIC_OBJ) \
	$(CDK2_NATIVE_GRAPHICS_CONSOLE_OBJS) \
	$(CDK2_NATIVE_ESRT_ENTRY_OBJ): $(CDK2_CONFIG_HEADER)

# Diagnostic inventory gates are deliberately outside per-module Kconfig
# blocks: the ledgers remain reviewable and checkable in every profile.
.PHONY: native-diagnostic-ledgers \
	native-disk-io-diagnostic-parity \
	native-pcat-rtc-diagnostic-parity native-capsule-runtime-diagnostic-parity \
	native-con-splitter-diagnostic-parity native-security-stub-diagnostic-parity \
	native-status-code-handler-diagnostic-parity native-tpm2-acpi-diagnostic-parity \
	native-watchdog-diagnostic-parity native-graphics-console-diagnostic-parity
native-diagnostic-ledgers:
	@sh "$(CDK2_DIR)/util/check-diagnostic-ledgers.sh" \
		"$(CDK2_DIR)/migration/reference-diagnostic-sites.tsv" \
		"$(CDK2_DIR)/migration/diagnostic-ledgers.tsv" \
		"$(CDK2_DIR)/migration"
native-disk-io-diagnostic-parity:
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/disk-io-diagnostic-parity.tsv" 5 "$(CDK2_DIR)/src/modules/disk_io"
native-pcat-rtc-diagnostic-parity:
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/pcat-rtc-diagnostic-parity.tsv" 5 "$(CDK2_DIR)/src/modules/pcat_rtc"
native-capsule-runtime-diagnostic-parity:
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/capsule-runtime-diagnostic-parity.tsv" 4 "$(CDK2_DIR)/src/modules/capsule_runtime"
native-con-splitter-diagnostic-parity:
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/con-splitter-diagnostic-parity.tsv" 4 "$(CDK2_DIR)/src/modules/con_splitter"
native-security-stub-diagnostic-parity:
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/security-stub-diagnostic-parity.tsv" 3 "$(CDK2_DIR)/src/modules/security_stub"
native-status-code-handler-diagnostic-parity:
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/status-code-handler-diagnostic-parity.tsv" 2 "$(CDK2_DIR)/src/modules/status_code_handler"
native-tpm2-acpi-diagnostic-parity:
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/tpm2-acpi-diagnostic-parity.tsv" 4 "$(CDK2_DIR)/src/modules/tpm2_acpi_table"
native-watchdog-diagnostic-parity:
	@sh "$(CDK2_DIR)/util/check-module-diagnostic-parity.sh" "$(CDK2_DIR)/migration/watchdog-diagnostic-parity.tsv" 2 "$(CDK2_DIR)/src/modules/watchdog"
native-check: native-diagnostic-ledgers \
	native-disk-io-diagnostic-parity \
	native-pcat-rtc-diagnostic-parity native-capsule-runtime-diagnostic-parity \
	native-con-splitter-diagnostic-parity native-security-stub-diagnostic-parity \
	native-status-code-handler-diagnostic-parity native-tpm2-acpi-diagnostic-parity \
	native-watchdog-diagnostic-parity native-graphics-console-diagnostic-parity

CDK2_NATIVE_STRICT_DIRECT_RAW_SCANNER ?= \
	$(CDK2_NATIVE_BUILD_DIR)/strict-direct-cbfs-raw-envelope

.PHONY: native-strict-direct-cbfs-admission-test
native-check: native-strict-direct-cbfs-admission-test
native-strict-direct-cbfs-admission-test: \
		$(CDK2_NATIVE_STRICT_DIRECT_RAW_SCANNER)
	@sh "$(CDK2_DIR)/tests/strict_direct_cbfs_admission_test.sh" \
		"$(CDK2_NATIVE_STRICT_DIRECT_RAW_SCANNER)"

.PHONY: native-strict-direct-cbfs-path-test
native-check: native-strict-direct-cbfs-path-test
native-strict-direct-cbfs-path-test: $(CDK2_NATIVE_STRICT_DIRECT_RAW_SCANNER)
	@sh "$(CDK2_DIR)/tests/strict_direct_cbfs_path_test.sh" \
		"$(CDK2_NATIVE_STRICT_DIRECT_RAW_SCANNER)"

.PHONY: native-strict-direct-cbfs-inventory-test
native-check: native-strict-direct-cbfs-inventory-test
native-strict-direct-cbfs-inventory-test: $(CDK2_NATIVE_STRICT_DIRECT_RAW_SCANNER)
	@sh "$(CDK2_DIR)/tests/strict_direct_cbfs_inventory_test.sh" \
		"$(CDK2_NATIVE_STRICT_DIRECT_RAW_SCANNER)"

.PHONY: native-direct-image-parser-source-test
native-check: native-direct-image-parser-source-test
native-direct-image-parser-source-test:
	@sh "$(CDK2_DIR)/tests/dxe_shared_parser_sources_test.sh" \
		"$(patsubst %/,%,$(CDK2_DIR))"
CDK2_NATIVE_MODULE_REGISTRY ?= \
	$(CDK2_DIR)/src/boot/native_modules.tsv
CDK2_NATIVE_DIRECT_IMAGE_VARIABLES ?= \
	$(CDK2_NATIVE_BUILD_DIR)/native-direct-image-variables.tsv
CDK2_NATIVE_DIRECT_IMAGE_INVENTORY ?= \
	$(CDK2_NATIVE_BUILD_DIR)/native-direct-image-inventory.tsv
CDK2_NATIVE_DIRECT_IMAGE_INVENTORY_TOOL := \
	$(CDK2_DIR)/util/direct-image-inventory
CDK2_NATIVE_DIRECT_IMAGE_INVENTORY_TEST := \
	$(CDK2_DIR)/tests/direct_image_inventory_test.sh
CDK2_NATIVE_DIRECT_PCD_CLOSURE_SCRIPT := \
	$(CDK2_DIR)/tests/direct_pcd_closure_test.sh
CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-direct-image-table
CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR_DEP ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-direct-image-table.d
CDK2_NATIVE_DIRECT_IMAGE_TABLE_ASM ?= \
	$(CDK2_NATIVE_BUILD_DIR)/native-direct-image-table.S
CDK2_NATIVE_DIRECT_IMAGE_TABLE_OBJ ?= \
	$(CDK2_NATIVE_BUILD_DIR)/native-direct-image-table.o
CDK2_NATIVE_DIRECT_MTRR_MANIFEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/direct-mtrr-ownership-inputs
CDK2_NATIVE_DIRECT_IMAGE_TABLE_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-direct-image-table-test
CDK2_NATIVE_DIRECT_DISPATCH_TEST ?= \
	$(CDK2_NATIVE_BUILD_DIR)/cdk2-direct-dispatch-test
CDK2_NATIVE_DIRECT_PAIR_PROVENANCE_ASSERT := \
	$(CDK2_DIR)/util/qemu/bin/assert-cdk2-direct-provenance.sh
CDK2_NATIVE_DIRECT_PAIR_PROVENANCE_TEST := \
	$(CDK2_DIR)/tests/direct_pair_provenance_test.sh
CDK2_NATIVE_DIRECT_COMPOSITION_INVENTORY ?= \
	$(CDK2_NATIVE_BUILD_DIR)/native-direct-composition-inventory.tsv
CDK2_NATIVE_DIRECT_COMPOSITION_INVENTORY_TOOL := \
	$(CDK2_DIR)/util/direct-composition-inventory
CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR_TEST := \
	$(CDK2_DIR)/tests/direct_image_table_generator_test.sh
CDK2_NATIVE_DIRECT_IMAGE_TABLE_SRC := \
	$(CDK2_DIR)/src/lib/direct_image_table.c
# Kconfig owns feature selection. Make owns the ordered payload composition;
# the generated response file below is an output, never another policy input.
CDK2_NATIVE_DIRECT_IMAGES := \
	d6a2cb7f-6a18-4e2f-b43b-9920a733700a:CDK2_NATIVE_DXE_CORE_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS := \
	$(CDK2_NATIVE_DXE_CORE_PE)

# This is the explicit linear entry order. DxeCore remains first and neither
# the generator nor the runtime table parser sorts these records.
ifeq ($(CONFIG_CDK2_NATIVE_SECURITY_STUB),y)
CDK2_NATIVE_DIRECT_IMAGES += f80697e9-7fd6-4665-8646-88e33ef71dfc:CDK2_NATIVE_SECURITY_STUB_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_SECURITY_STUB_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_STATUS_CODE_ROUTER),y)
CDK2_NATIVE_DIRECT_IMAGES += d93ce3d8-a7eb-4730-8c8e-cc466a9ecc3c:CDK2_NATIVE_STATUS_CODE_ROUTER_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_STATUS_CODE_ROUTER_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_STATUS_CODE_HANDLER),y)
CDK2_NATIVE_DIRECT_IMAGES += 6c2004ef-4e0e-4be4-b14c-340eb4aa5891:CDK2_NATIVE_STATUS_CODE_HANDLER_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_STATUS_CODE_HANDLER_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_CPU_ARCH),y)
CDK2_NATIVE_DIRECT_IMAGES += 1a1e4886-9517-440e-9fde-3be44cee2136:CDK2_NATIVE_CPU_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_CPU_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_CPU_IO2),y)
CDK2_NATIVE_DIRECT_IMAGES += a19b1fe7-c1bc-49f8-875f-54a5d542443f:CDK2_NATIVE_CPU_IO2_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_CPU_IO2_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_RUNTIME_ARCH),y)
CDK2_NATIVE_DIRECT_IMAGES += b601f8c4-43b7-4784-95b1-f4226cb40cee:CDK2_NATIVE_RUNTIME_ARCH_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_RUNTIME_ARCH_PE)
endif
ifeq ($(CONFIG_CDK2_PAYLOAD),y)
CDK2_NATIVE_DIRECT_IMAGES += c8339973-a563-4561-b858-d8476f9defc4:CDK2_NATIVE_METRONOME_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_METRONOME_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_WATCHDOG),y)
CDK2_NATIVE_DIRECT_IMAGES += f099d67f-71ae-4c36-b2a3-dceb0eb2b7d8:CDK2_NATIVE_WATCHDOG_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_WATCHDOG_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_MONOTONIC_COUNTER),y)
CDK2_NATIVE_DIRECT_IMAGES += ad608272-d07f-4964-801e-7bd3b7888652:CDK2_NATIVE_MONO_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_MONO_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_RESET_SYSTEM),y)
CDK2_NATIVE_DIRECT_IMAGES += 4b28e4c7-ff36-4e10-93cf-a82159e777c5:CDK2_NATIVE_RESET_SYSTEM_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_RESET_SYSTEM_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_PCAT_RTC),y)
CDK2_NATIVE_DIRECT_IMAGES += 378d7b65-8da9-4773-b6e4-a47826a833e1:CDK2_NATIVE_PCAT_RTC_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_PCAT_RTC_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_LOCAL_APIC_TIMER),y)
CDK2_NATIVE_DIRECT_IMAGES += 52fe8196-f9de-4d07-b22f-51f77a0e7c41:CDK2_NATIVE_LOCAL_APIC_TIMER_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_LOCAL_APIC_TIMER_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_SMMSTORE_FVB),y)
CDK2_NATIVE_DIRECT_IMAGES += a0402fca-6b25-4cea-b7dd-c08f99714b29:CDK2_NATIVE_SMMSTORE_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_SMMSTORE_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_FTW),y)
CDK2_NATIVE_DIRECT_IMAGES += fe5cea76-4f72-49e8-986f-2cd899dffe5d:CDK2_NATIVE_FTW_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_FTW_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_VARIABLE_RUNTIME),y)
CDK2_NATIVE_DIRECT_IMAGES += cbd2e4d5-7068-4ff5-b462-9822b4ad8d60:CDK2_NATIVE_VARIABLE_RUNTIME_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_VARIABLE_RUNTIME_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_ACPI_TABLE),y)
CDK2_NATIVE_DIRECT_IMAGES += 9622e42c-8e38-4a08-9e8f-54f784652f6b:CDK2_NATIVE_ACPI_TABLE_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_ACPI_TABLE_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_TCG2_REPLACEMENT),y)
CDK2_NATIVE_DIRECT_IMAGES += fdff263d-5f68-4591-87ba-b768f445a9af:CDK2_NATIVE_TCG2_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_TCG2_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_TPM2_ACPI_TABLE),y)
CDK2_NATIVE_DIRECT_IMAGES += c442a847-892a-4f94-ad6f-60317c317be7:CDK2_NATIVE_TPM2_ACPI_TABLE_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_TPM2_ACPI_TABLE_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_PCI_HOST_BRIDGE),y)
CDK2_NATIVE_DIRECT_IMAGES += 128fb770-5e79-4176-9e51-9bb268a17dd1:CDK2_NATIVE_PCI_HOST_BRIDGE_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_PCI_HOST_BRIDGE_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_PCI_BUS),y)
CDK2_NATIVE_DIRECT_IMAGES += 93b80004-9fb3-11d4-9a3a-0090273fc14d:CDK2_NATIVE_PCI_BUS_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_PCI_BUS_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_NVME),y)
CDK2_NATIVE_DIRECT_IMAGES += 5be3bdf4-53cf-46a3-a6a9-73c34a6e5ee3:CDK2_NATIVE_NVME_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_NVME_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_SATA_CONTROLLER),y)
CDK2_NATIVE_DIRECT_IMAGES += 820c59bb-274c-43b2-83ea-dac673035a59:CDK2_NATIVE_SATA_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_SATA_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_ATA_ATAPI_PASS_THRU),y)
CDK2_NATIVE_DIRECT_IMAGES += 5e523cb4-d397-4986-87bd-a6dd8b22f455:CDK2_NATIVE_ATA_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_ATA_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_ATA_BUS),y)
CDK2_NATIVE_DIRECT_IMAGES += 19df145a-b1d4-453f-8507-38816676d7f6:CDK2_NATIVE_ATA_BUS_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_ATA_BUS_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_SDMMC_PCI),y)
CDK2_NATIVE_DIRECT_IMAGES += 8e325979-3fe1-4927-aae2-8f5c4bd2af0d:CDK2_NATIVE_SDMMC_PCI_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_SDMMC_PCI_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_SD_DXE),y)
CDK2_NATIVE_DIRECT_IMAGES += 430ac2f7-eec6-4093-94f7-9f825a7c1c40:CDK2_NATIVE_SD_DXE_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_SD_DXE_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_EMMC_DXE),y)
CDK2_NATIVE_DIRECT_IMAGES += 2145f72f-e6f1-4440-a828-59dc9aab5f89:CDK2_NATIVE_EMMC_DXE_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_EMMC_DXE_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_XHCI),y)
CDK2_NATIVE_DIRECT_IMAGES += b7f50e91-a759-412c-ade4-dcd03e7f7c28:CDK2_NATIVE_XHCI_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_XHCI_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_EHCI),y)
CDK2_NATIVE_DIRECT_IMAGES += bdfe430e-8f2a-4db0-9991-6f856594777e:CDK2_NATIVE_EHCI_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_EHCI_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_UHCI),y)
CDK2_NATIVE_DIRECT_IMAGES += 2fb92efa-2ee0-4bae-9eb6-7464125e1ef7:CDK2_NATIVE_UHCI_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_UHCI_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_USB_BUS),y)
CDK2_NATIVE_DIRECT_IMAGES += 240612b7-a063-11d4-9a3a-0090273fc14d:CDK2_NATIVE_USB_BUS_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_USB_BUS_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_USB_MASS_STORAGE),y)
CDK2_NATIVE_DIRECT_IMAGES += 9fb4b4a7-42c0-4bcd-8540-9bcc6711f83e:CDK2_NATIVE_USB_MASS_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_USB_MASS_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_SCSI_BUS),y)
CDK2_NATIVE_DIRECT_IMAGES += 0167ccc4-d0f7-4f21-a3ef-9e64b7cdce8b:CDK2_NATIVE_SCSI_BUS_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_SCSI_BUS_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_SCSI_DISK),y)
CDK2_NATIVE_DIRECT_IMAGES += 0a66e322-3740-4cce-ad62-bd172cecca35:CDK2_NATIVE_SCSI_DISK_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_SCSI_DISK_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_DISK_IO),y)
CDK2_NATIVE_DIRECT_IMAGES += 6b38f7b4-ad98-40e9-9093-aca2b5a253c4:CDK2_NATIVE_DISK_IO_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_DISK_IO_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_PARTITION),y)
CDK2_NATIVE_DIRECT_IMAGES += 1fa1f39e-feff-4aae-bd7b-38a070a3b609:CDK2_NATIVE_PARTITION_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_PARTITION_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_FAT),y)
CDK2_NATIVE_DIRECT_IMAGES += 961578fe-b6b7-44c3-af35-6bc705cd2b1f:CDK2_NATIVE_FAT_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_FAT_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_DEVICE_PATH),y)
CDK2_NATIVE_DIRECT_IMAGES += 9b680fce-ad6b-4f3a-b60b-f59899003443:CDK2_NATIVE_DEVICE_PATH_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_DEVICE_PATH_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_GRAPHICS_OUTPUT),y)
CDK2_NATIVE_DIRECT_IMAGES += 0b04b2ed-861c-42cd-a22f-c3aafaccb896:CDK2_NATIVE_GRAPHICS_OUTPUT_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_GRAPHICS_OUTPUT_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_GRAPHICS_CONSOLE),y)
CDK2_NATIVE_DIRECT_IMAGES += cccb0c28-4b24-11d5-9a5a-0090273fc14d:CDK2_NATIVE_GRAPHICS_CONSOLE_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_GRAPHICS_CONSOLE_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_CON_PLATFORM),y)
CDK2_NATIVE_DIRECT_IMAGES += 51ccf399-4fdf-4e55-a45b-e123f84d456a:CDK2_NATIVE_CON_PLATFORM_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_CON_PLATFORM_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_CON_SPLITTER),y)
CDK2_NATIVE_DIRECT_IMAGES += 408edcec-cf6d-477c-a5a8-b4844e3de281:CDK2_NATIVE_CON_SPLITTER_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_CON_SPLITTER_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_SERIAL_IO),y)
CDK2_NATIVE_DIRECT_IMAGES += 9a5163e7-5c29-453f-825c-837a46a81e15:CDK2_NATIVE_SERIAL_IO_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_SERIAL_IO_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_TERMINAL),y)
CDK2_NATIVE_DIRECT_IMAGES += 9e863906-a40f-4875-977f-5b93ff237fc6:CDK2_NATIVE_TERMINAL_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_TERMINAL_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_USB_KEYBOARD),y)
CDK2_NATIVE_DIRECT_IMAGES += 2d2e62cf-9ecf-43b7-8219-94e7fc713dfe:CDK2_NATIVE_USB_KEYBOARD_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_USB_KEYBOARD_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_USB_MOUSE),y)
CDK2_NATIVE_DIRECT_IMAGES += 2d2e62aa-9ecf-43b7-8219-94e7fc713dfe:CDK2_NATIVE_USB_MOUSE_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_USB_MOUSE_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_SIO_BUS),y)
CDK2_NATIVE_DIRECT_IMAGES += 864e1ca8-85eb-4d63-9dcc-6e0fc90ffd55:CDK2_NATIVE_SIO_BUS_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_SIO_BUS_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_PS2_MOUSE),y)
CDK2_NATIVE_DIRECT_IMAGES += 08464531-4c99-4c4c-a887-8d8ba4bbb063:CDK2_NATIVE_PS2_MOUSE_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_PS2_MOUSE_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_EC_BATTERY),y)
CDK2_NATIVE_DIRECT_IMAGES += 3c4d5e6f-7a8b-9c0d-1e2f-3a4b5c6d7e8f:CDK2_NATIVE_EC_BATTERY_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_EC_BATTERY_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_CAPSULE_RUNTIME),y)
CDK2_NATIVE_DIRECT_IMAGES += 42857f0a-13f2-4b21-8a23-53d3f714b840:CDK2_NATIVE_CAPSULE_RUNTIME_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_CAPSULE_RUNTIME_PE)
endif
ifeq ($(CONFIG_CDK2_ESRT),y)
CDK2_NATIVE_DIRECT_IMAGES += 999bd818-7df7-4a9a-a502-9b75033e6a0f:CDK2_NATIVE_ESRT_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_ESRT_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_QEMU_TEST_FMP),y)
CDK2_NATIVE_DIRECT_IMAGES += 18e6b8cc-63d1-447c-b32d-3dbd51d32922:CDK2_NATIVE_QEMU_TEST_FMP_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_QEMU_TEST_FMP_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_SYSTEM_FMP),y)
CDK2_NATIVE_DIRECT_IMAGES += 975cd0e6-c540-4e2b-906c-72c0d0d1e40d:CDK2_NATIVE_SYSTEM_FMP_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_SYSTEM_FMP_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_ENGLISH),y)
CDK2_NATIVE_DIRECT_IMAGES += cd3bafb6-50fb-4fe8-8e4e-ab74d2c1a600:CDK2_NATIVE_ENGLISH_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_ENGLISH_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_LVGL_SETUP),y)
CDK2_NATIVE_DIRECT_IMAGES += 87f4dfc1-a2f1-4c90-952d-4a881ed95231:CDK2_NATIVE_LVGL_UI_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_LVGL_UI_PE)
endif
ifeq ($(CONFIG_CDK2_NATIVE_BDS),y)
CDK2_NATIVE_DIRECT_IMAGES += 6d33944a-ec75-4855-a54d-809c75241f6c:CDK2_NATIVE_BDS_PE
CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS += $(CDK2_NATIVE_BDS_PE)
endif

CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION ?= \
	$(CDK2_NATIVE_BUILD_DIR)/native-direct-images.rsp
CDK2_NATIVE_DIRECT_IMAGE_RUNTIME_DEPS := \
	$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_OBJ) \
	$(CDK2_NATIVE_ELF_CHECK)
$(CDK2_NATIVE_STRICT_DIRECT_RAW_SCANNER): \
		$(CDK2_DIR)/util/strict-direct-cbfs-raw-envelope.c | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O2 -o "$@" "$<"

.PHONY: native-direct-image-inventory-test
native-check: native-direct-image-inventory-test
native-direct-image-inventory-test: $(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY_TOOL) \
		$(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY_TEST)
	@sh "$(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY_TEST)" \
		"$(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY_TOOL)"

$(CDK2_NATIVE_DIRECT_PCD_CLOSURE_TEST): \
		$(CDK2_DIR)/tests/direct_pcd_closure_test.c | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O2 -o "$@" "$<"

.PHONY: native-direct-pcd-closure-test
# Component-only profiles run the scanner's hostile unit fixtures, not an
# unsupported public image. Explicit artifact targets still enforce admission.
ifeq ($(CONFIG_CDK2_LINEAR_BOOT)$(CONFIG_CDK2_STRICT_DIRECT_RUNTIME)$(CONFIG_CDK2_COREBOOT_CAPSULE_PROFILE)$(CONFIG_CDK2_SECURE_BOOT),yyyn)
native-check: native-direct-pcd-closure-test native-direct-pair-provenance-test
else
native-check: native-direct-pcd-scanner-test
endif
.PHONY: native-direct-pcd-scanner-test
native-direct-pcd-scanner-test: $(CDK2_NATIVE_DIRECT_PCD_CLOSURE_TEST)
	@"$(CDK2_NATIVE_DIRECT_PCD_CLOSURE_TEST)" --selftest

native-direct-pcd-closure-test: $(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY) \
		$(CDK2_NATIVE_DIRECT_PCD_CLOSURE_TEST) \
		$(CDK2_NATIVE_DIRECT_PCD_CLOSURE_SCRIPT)
	@"$(CDK2_NATIVE_DIRECT_PCD_CLOSURE_TEST)" --selftest
	@sh "$(CDK2_NATIVE_DIRECT_PCD_CLOSURE_SCRIPT)" \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_MODULE_REGISTRY)" \
		"$(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY)" \
		"$(CDK2_NATIVE_DIRECT_PCD_CLOSURE_TEST)"

$(CDK2_NATIVE_DIRECT_IMAGE_VARIABLES): $(CDK2_NATIVE_MODULE_REGISTRY) \
		$(CDK2_DIR)/Makefile $(CDK2_DIR)/src/boot/Makefile \
		$(CDK2_CONFIG) $(CDK2_CONFIG_HEADER) \
		$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION) \
		$(CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS) FORCE | \
		$(CDK2_NATIVE_BUILD_DIR)
	@set -e; temporary="$@.$$$$.tmp"; trap 'rm -f "$$temporary"' EXIT HUP INT TERM; \
		awk -F '\t' 'NF == 4 && $$1 == "IMAGE" { print $$3 "\t" $$4 }' \
			"$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION)" > "$$temporary"; \
		test -s "$$temporary"; \
		if test -f "$@" && cmp -s "$$temporary" "$@"; then \
			rm -f "$$temporary"; \
		else \
			mv -f "$$temporary" "$@"; \
		fi

$(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY): \
		$(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY_TOOL) \
		$(CDK2_NATIVE_MODULE_REGISTRY) \
		$(CDK2_CONFIG_HEADER) \
		$(CDK2_NATIVE_DIRECT_IMAGE_VARIABLES) \
		$(CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS)
	@sh "$(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY_TOOL)" \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_MODULE_REGISTRY)" \
		"$(CDK2_NATIVE_DIRECT_IMAGE_VARIABLES)" \
		"$(CDK2_NATIVE_BUILD_DIR)" "$@"

.PHONY: native-direct-image-inventory
native-direct-image-inventory: $(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY_TOOL) \
		$(CDK2_NATIVE_MODULE_REGISTRY) \
		$(CDK2_CONFIG_HEADER)
	@sh "$(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY_TOOL)" --check-profile \
		"$(CDK2_CONFIG_HEADER)" "$(CDK2_NATIVE_MODULE_REGISTRY)"
	@$(MAKE) --no-print-directory $(CDK2_RECURSIVE_ARGS) \
		-f "$(CDK2_DIR)/Makefile" CDK2_CONFIG_READY=1 \
		"$(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY)"
	@printf '%s\n' \
		'direct source PE inventory: $(CDK2_NATIVE_DIRECT_IMAGE_INVENTORY)'

$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR): \
		$(CDK2_DIR)/src/boot/Makefile \
		$(CDK2_DIR)/util/direct-image-table.c \
		$(CDK2_DIR)/src/lib/guid_parse.c $(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/include/cdk2/direct_image_table.h \
		$(CDK2_DIR)/include/cdk2/guid_parse.h \
		$(CDK2_DIR)/include/cdk2/pe_image_view.h | $(CDK2_NATIVE_BUILD_DIR)
	@set -e; binary="$@.$$$$.tmp"; \
		dependency="$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR_DEP).$$$$.tmp"; \
		trap 'rm -f "$$binary" "$$dependency"' EXIT HUP INT TERM; \
		$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
			$(CDK2_NATIVE_INCLUDES) \
			-MM -MP -MT "$@" \
			$(CDK2_DIR)/util/direct-image-table.c \
			$(CDK2_DIR)/src/lib/guid_parse.c $(CDK2_PE_IMAGE_VIEW_SRC) \
			> "$$dependency"; \
		$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) \
			$(CDK2_NATIVE_INCLUDES) \
			-o "$$binary" $(CDK2_DIR)/util/direct-image-table.c \
			$(CDK2_DIR)/src/lib/guid_parse.c $(CDK2_PE_IMAGE_VIEW_SRC); \
		mv -f "$$dependency" \
			"$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR_DEP)"; \
		mv -f "$$binary" "$@"

.PHONY: native-direct-image-generator-dependency-test
native-check: native-direct-image-generator-dependency-test
native-direct-image-generator-dependency-test: \
		$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR)
	@sh "$(CDK2_DIR)/tests/direct_image_generator_dependency_test.sh" \
		"$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR_DEP)" \
		"$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR)" \
		"$(patsubst %/,%,$(CDK2_DIR))"

$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_TEST): \
		$(CDK2_DIR)/tests/direct_image_table_test.c \
		$(CDK2_DIR)/tests/guid_parse_test.c \
		$(CDK2_DIR)/src/lib/guid_parse.c $(CDK2_DIR)/include/cdk2/guid_parse.h \
		$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_SRC) $(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/include/cdk2/direct_image_table.h \
		$(CDK2_DIR)/include/cdk2/pe_image_view.h | $(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O1 \
		-fsanitize=undefined -fno-sanitize-recover=undefined \
		$(CDK2_NATIVE_INCLUDES) -o "$@" \
		$(CDK2_DIR)/tests/direct_image_table_test.c \
		$(CDK2_DIR)/tests/guid_parse_test.c \
		$(CDK2_DIR)/src/lib/guid_parse.c \
		$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_SRC) $(CDK2_PE_IMAGE_VIEW_SRC)

.PHONY: native-direct-image-table-test
native-check: native-direct-image-table-test
native-direct-image-table-test: $(CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR) \
		$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_TEST) \
		$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR_TEST)
	@UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_TEST)"
	@UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR_TEST)" \
		"$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR)" \
		"$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_TEST)" \
		"$(CDK2_NATIVE_HOST_CC)" "$(CDK2_NATIVE_OBJCOPY)" \
		"$(CDK2_NATIVE_READELF)" \
		"$(CDK2_NATIVE_NM)"

$(CDK2_NATIVE_DIRECT_DISPATCH_TEST): \
		$(CDK2_DIR)/tests/direct_dispatch_test.c \
		$(CDK2_DIR)/src/modules/dxe_core/direct_dispatch.c \
		$(if $(filter y,$(CONFIG_CDK2_STRICT_DIRECT_RUNTIME)),,\
			$(CDK2_DIR)/src/modules/dxe_core/dispatcher.c) \
		$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_SRC) $(CDK2_PE_IMAGE_VIEW_SRC) \
		$(CDK2_DIR)/src/lib/diagnostic.c \
		$(CDK2_DIR)/include/cdk2/direct_image_table.h \
		$(CDK2_DIR)/include/cdk2/dxe_image_transaction.h \
		$(CDK2_CONFIG_HEADER) | \
		$(CDK2_NATIVE_BUILD_DIR)
	@$(CDK2_NATIVE_HOST_CC) $(CDK2_NATIVE_HOST_CFLAGS) -O1 \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		$(CDK2_NATIVE_INCLUDES) -o "$@" \
		$(filter-out $(CDK2_CONFIG_HEADER),$^)

.PHONY: native-direct-dispatch-test
native-check: native-direct-dispatch-test
native-direct-dispatch-test: $(CDK2_NATIVE_DIRECT_DISPATCH_TEST)
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$<"

$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION): \
		$(CDK2_DIR)/Makefile $(CDK2_DIR)/src/boot/Makefile \
		$(CDK2_CONFIG) $(CDK2_CONFIG_HEADER) \
		$(CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS) FORCE | \
		$(CDK2_NATIVE_BUILD_DIR)
	@set -e; temporary="$@.$$$$.tmp"; trap 'rm -f "$$temporary"' EXIT HUP INT TERM; \
		: > "$$temporary"; \
		set -f; set -- $(CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS); \
		for image in $(CDK2_NATIVE_DIRECT_IMAGES); do \
			test "$$#" -gt 0; artifact=$$1; shift; \
			guid=$${image%%:*}; variable=$${image#*:}; \
			printf '%s\t%s\t%s\t%s\n' IMAGE "$$guid" "$$variable" \
				"$$artifact" >> "$$temporary"; \
		done; \
		test "$$#" -eq 0; \
		if test -f "$@" && cmp -s "$$temporary" "$@"; then \
			rm -f "$$temporary"; \
		else \
			mv -f "$$temporary" "$@"; \
		fi

$(CDK2_NATIVE_DIRECT_COMPOSITION_INVENTORY): \
		$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION) \
		$(CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS) \
		$(CDK2_NATIVE_DIRECT_COMPOSITION_INVENTORY_TOOL) FORCE | \
		$(CDK2_NATIVE_BUILD_DIR)
	@"$(CDK2_NATIVE_DIRECT_COMPOSITION_INVENTORY_TOOL)" \
		"$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION)" \
		"$(CDK2_NATIVE_BUILD_DIR)" "$@"

# These byte-pattern exceptions still require zero decoded WRMSR instructions.
# Compiler-generated displacements can contain 0f 30 without writing an MSR.
CDK2_NATIVE_DIRECT_MTRR_BINDINGS := \
	"CDK2_NATIVE_DXE_CORE_PE|false-positive|auto"
ifeq ($(CONFIG_CDK2_NATIVE_CPU_ARCH),y)
CDK2_NATIVE_DIRECT_MTRR_BINDINGS += \
	"CDK2_NATIVE_CPU_PE|c0000080|4"
endif
ifeq ($(CONFIG_CDK2_NATIVE_LOCAL_APIC_TIMER),y)
CDK2_NATIVE_DIRECT_MTRR_BINDINGS += \
	"CDK2_NATIVE_LOCAL_APIC_TIMER_PE|80b,832,838,83e|4"
endif
ifeq ($(CONFIG_CDK2_NATIVE_CON_SPLITTER),y)
# Derive the raw-byte count from the canonical executable PE sections.  The
# false-positive class still requires zero decoded WRMSR instructions.
CDK2_NATIVE_DIRECT_MTRR_BINDINGS += \
	"CDK2_NATIVE_CON_SPLITTER_PE|false-positive|auto"
endif
ifeq ($(CONFIG_CDK2_NATIVE_CAPSULE_RUNTIME),y)
CDK2_NATIVE_DIRECT_MTRR_BINDINGS += \
	"CDK2_NATIVE_CAPSULE_RUNTIME_PE|false-positive|auto"
endif
ifeq ($(CONFIG_CDK2_NATIVE_PCI_BUS),y)
CDK2_NATIVE_DIRECT_MTRR_BINDINGS += \
	"CDK2_NATIVE_PCI_BUS_PE|false-positive|auto"
endif

$(CDK2_NATIVE_DIRECT_MTRR_MANIFEST): \
		$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION) \
		$(CDK2_NATIVE_COREBOOT_ELF) $(CDK2_NATIVE_COREBOOT_TEST) \
		$(CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS) \
		$(CDK2_NATIVE_PE_EXEC_SECTIONS) \
		$(CDK2_DIR)/tests/direct_mtrr_manifest.sh FORCE | \
		$(CDK2_NATIVE_BUILD_DIR)
	@CDK2_PE_EXEC_SECTIONS="$(CDK2_NATIVE_PE_EXEC_SECTIONS)" \
		sh "$(CDK2_DIR)/tests/direct_mtrr_manifest.sh" \
		"$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION)" "$@" \
		"$(CDK2_NATIVE_COREBOOT_ELF)" "$(CDK2_NATIVE_COREBOOT_TEST)" \
		$(CDK2_NATIVE_DIRECT_MTRR_BINDINGS)

.PHONY: native-direct-mtrr-ownership-audit
native-direct-mtrr-ownership-audit: \
		$(CDK2_NATIVE_DIRECT_MTRR_MANIFEST) \
		$(CDK2_NATIVE_PE_EXEC_SECTIONS) \
		$(CDK2_NATIVE_PE_EXEC_FIXTURE) \
		$(CDK2_DIR)/tests/mtrr_ownership_test.sh \
		$(CDK2_DIR)/tests/direct_mtrr_manifest.sh \
		$(CDK2_DIR)/tests/direct_mtrr_ownership_test.sh
	@OBJCOPY="$(CDK2_NATIVE_OBJCOPY)" OBJDUMP="$(CDK2_NATIVE_OBJDUMP)" \
		CDK2_PE_EXEC_SECTIONS="$(CDK2_NATIVE_PE_EXEC_SECTIONS)" \
		"$(CDK2_DIR)/tests/mtrr_ownership_test.sh" \
		"$(CDK2_NATIVE_DIRECT_MTRR_MANIFEST)" \
		"$(CDK2_NATIVE_COREBOOT_TEST)"
	@OBJCOPY="$(CDK2_NATIVE_OBJCOPY)" OBJDUMP="$(CDK2_NATIVE_OBJDUMP)" \
		sh "$(CDK2_DIR)/tests/direct_mtrr_ownership_test.sh" \
			"$(CDK2_DIR)/tests/mtrr_ownership_test.sh" \
			"$(CDK2_NATIVE_DIRECT_MTRR_MANIFEST)" \
			"$(CDK2_NATIVE_COREBOOT_TEST)" \
			"$(CDK2_NATIVE_PE_EXEC_SECTIONS)" \
			"$(CDK2_NATIVE_DXE_CORE_PE)" \
			"$(CDK2_DIR)/tests/direct_mtrr_manifest.sh" \
			"$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION)" \
			"$(CDK2_NATIVE_COREBOOT_ELF)" \
			"$(CDK2_NATIVE_PE_EXEC_FIXTURE)" CDK2_NATIVE_DXE_CORE_PE
	@if test "$(CONFIG_CDK2_NATIVE_CON_SPLITTER)" = y; then \
		OBJCOPY="$(CDK2_NATIVE_OBJCOPY)" OBJDUMP="$(CDK2_NATIVE_OBJDUMP)" \
		sh "$(CDK2_DIR)/tests/direct_mtrr_ownership_test.sh" \
			"$(CDK2_DIR)/tests/mtrr_ownership_test.sh" \
			"$(CDK2_NATIVE_DIRECT_MTRR_MANIFEST)" \
			"$(CDK2_NATIVE_COREBOOT_TEST)" \
			"$(CDK2_NATIVE_PE_EXEC_SECTIONS)" \
			"$(CDK2_NATIVE_CON_SPLITTER_PE)" \
			"$(CDK2_DIR)/tests/direct_mtrr_manifest.sh" \
			"$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION)" \
			"$(CDK2_NATIVE_COREBOOT_ELF)" \
			"$(CDK2_NATIVE_PE_EXEC_FIXTURE)"; \
	fi
	@if test "$(CONFIG_CDK2_NATIVE_CAPSULE_RUNTIME)" = y; then \
		OBJCOPY="$(CDK2_NATIVE_OBJCOPY)" OBJDUMP="$(CDK2_NATIVE_OBJDUMP)" \
		sh "$(CDK2_DIR)/tests/direct_mtrr_ownership_test.sh" \
			"$(CDK2_DIR)/tests/mtrr_ownership_test.sh" \
			"$(CDK2_NATIVE_DIRECT_MTRR_MANIFEST)" \
			"$(CDK2_NATIVE_COREBOOT_TEST)" \
			"$(CDK2_NATIVE_PE_EXEC_SECTIONS)" \
			"$(CDK2_NATIVE_CAPSULE_RUNTIME_PE)" \
			"$(CDK2_DIR)/tests/direct_mtrr_manifest.sh" \
			"$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION)" \
			"$(CDK2_NATIVE_COREBOOT_ELF)" \
			"$(CDK2_NATIVE_PE_EXEC_FIXTURE)" CDK2_NATIVE_CAPSULE_RUNTIME_PE; \
	fi
	@if test "$(CONFIG_CDK2_NATIVE_PCI_BUS)" = y; then \
		OBJCOPY="$(CDK2_NATIVE_OBJCOPY)" OBJDUMP="$(CDK2_NATIVE_OBJDUMP)" \
		sh "$(CDK2_DIR)/tests/direct_mtrr_ownership_test.sh" \
			"$(CDK2_DIR)/tests/mtrr_ownership_test.sh" \
			"$(CDK2_NATIVE_DIRECT_MTRR_MANIFEST)" \
			"$(CDK2_NATIVE_COREBOOT_TEST)" \
			"$(CDK2_NATIVE_PE_EXEC_SECTIONS)" \
			"$(CDK2_NATIVE_PCI_BUS_PE)" \
			"$(CDK2_DIR)/tests/direct_mtrr_manifest.sh" \
			"$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION)" \
			"$(CDK2_NATIVE_COREBOOT_ELF)" \
			"$(CDK2_NATIVE_PE_EXEC_FIXTURE)" CDK2_NATIVE_PCI_BUS_PE; \
	fi

.PHONY: native-direct-composition-contract-test
native-direct-composition-contract-test: \
		$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION) \
		$(CDK2_DIR)/tests/direct_composition_contract_test.sh
	@sh "$(CDK2_DIR)/tests/direct_composition_contract_test.sh" \
		"$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION)" "$(CDK2_CONFIG_HEADER)"

.PHONY: native-direct-pair-provenance-test
native-direct-pair-provenance-test: $(CDK2_NATIVE_COREBOOT_IMAGE) \
		$(CDK2_NATIVE_DIRECT_COMPOSITION_INVENTORY) \
		$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR) $(CDK2_NATIVE_ELF_CHECK) \
		$(CDK2_NATIVE_DIRECT_PAIR_PROVENANCE_ASSERT) \
		$(CDK2_NATIVE_DIRECT_PAIR_PROVENANCE_TEST)
	@sh "$(CDK2_NATIVE_DIRECT_PAIR_PROVENANCE_TEST)" \
		"$(CDK2_NATIVE_DIRECT_PAIR_PROVENANCE_ASSERT)" \
		"$(CDK2_NATIVE_COREBOOT_IMAGE)" "$(CDK2_CONFIG_HEADER)" \
		"$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION)" \
		"$(CDK2_NATIVE_DIRECT_COMPOSITION_INVENTORY)" \
		"$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR)" \
		"$(CDK2_NATIVE_ELF_CHECK)" \
		"$(CDK2_DIR)/tests/direct_composition_contract_test.sh" \
		"$(CDK2_NATIVE_DIRECT_COMPOSITION_INVENTORY_TOOL)"

$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_ASM): \
		$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR) \
		$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION) \
		$(CDK2_NATIVE_DIRECT_IMAGE_SELECTED_PE_ARTIFACTS)
	@set -e; temporary="$@.$$$$.tmp"; \
		trap 'rm -f "$$temporary"' EXIT HUP INT TERM; \
		"$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_GENERATOR)" \
			"$(CDK2_NATIVE_DIRECT_IMAGE_COMPOSITION)" "$$temporary"; \
		mv -f "$$temporary" "$@"

$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_OBJ): $(CDK2_NATIVE_DIRECT_IMAGE_TABLE_ASM)
	@$(CDK2_NATIVE_CC) $(CDK2_NATIVE_CFLAGS) -c "$<" -o "$@"

.PHONY: native-direct-image-table
native-direct-image-table:
	@$(MAKE) --no-print-directory $(CDK2_RECURSIVE_ARGS) \
		-f "$(CDK2_DIR)/Makefile" CDK2_CONFIG_READY=1 \
		"$(CDK2_NATIVE_DIRECT_IMAGE_TABLE_OBJ)"
	@printf '%s\n' \
		'direct linked PE table: $(CDK2_NATIVE_DIRECT_IMAGE_TABLE_OBJ)'

.PHONY: native-direct-dxe-core-bridge-test
native-check: native-direct-dxe-core-bridge-test
native-direct-dxe-core-bridge-test: \
		$(CDK2_DIR)/tests/direct_dxe_core_bridge_test.sh \
		$(CDK2_DIR)/tests/scratch_make_environment.sh
	@MAKEFLAGS= MFLAGS= MAKEOVERRIDES= \
	sh "$(CDK2_DIR)/tests/direct_dxe_core_bridge_test.sh" \
		"$(patsubst %/,%,$(CDK2_DIR))" "$(MAKE)" \
		"$(CDK2_NATIVE_HOST_CC)" "$(CDK2_NATIVE_CC)" \
		"$(CDK2_NATIVE_OBJCOPY)" "$(CDK2_NATIVE_READELF)" \
		"$(CDK2_NATIVE_NM)"

# Admission remains a phony prerequisite even for an existing output.
native-stage native-coreboot-stage native-coreboot-image \
	$(CDK2_NATIVE_ELF) $(CDK2_NATIVE_OVERRIDE_ELF) \
	$(CDK2_NATIVE_COREBOOT_ELF) $(CDK2_NATIVE_COREBOOT_MAP) \
	$(CDK2_NATIVE_COREBOOT_IMAGE): | \
	native-linear-payload-admission

# A relocation-free zero-base PE gets one private data fixup so the runtime
# loader can admit arbitrary placement without weakening third-party checks.
CDK2_NATIVE_PE_OUTPUTS := $(sort $(foreach variable,$(.VARIABLES),\
	$(if $(filter CDK2_NATIVE_%_PE,$(variable)),$($(variable)))))
CDK2_NATIVE_PE_OUTPUTS += $(CDK2_PUBLIC_FULLGRAPH_APP_PE)
CDK2_NATIVE_PE_OUTPUTS += $(CDK2_PUBLIC_FULLGRAPH_CHILD_PE) $(CDK2_PUBLIC_FULLGRAPH_ENROLLED_PE)
CDK2_NATIVE_PE_OUTPUTS += $(CDK2_PUBLIC_FULLGRAPH_COLD_PE)
CDK2_NATIVE_PE_OUTPUTS += $(CDK2_NORMAL_PRIVATE_APP_PE)
$(CDK2_NATIVE_PE_OUTPUTS): $(CDK2_NATIVE_PE_RELOCATION_MARKER_OBJ) \
	$(CDK2_NATIVE_PERELOCCHECK) $(CDK2_NATIVE_PE_LINK)

# Configuration also controls host-test flags and selected link inputs. Keep
# cached native outputs tied to it without passing the header through $^.
CDK2_NATIVE_CONFIG_OUTPUTS := $(sort $(filter $(CDK2_NATIVE_BUILD_DIR)/%,\
	$(foreach variable,$(filter CDK2_NATIVE_%,$(.VARIABLES)),$($(variable)))))
# These outputs are declared directly in rules or by other source families.
CDK2_NATIVE_CONFIG_OUTPUTS += $(CDK2_PUBLIC_FULLGRAPH_CHILD_OBJ) \
	$(CDK2_PUBLIC_FULLGRAPH_CHILD_LINK) $(CDK2_PUBLIC_FULLGRAPH_CHILD_PE) \
	$(CDK2_PUBLIC_FULLGRAPH_AUTH_OBJ) $(CDK2_PUBLIC_FULLGRAPH_ENROLLED_OBJ) \
	$(CDK2_PUBLIC_FULLGRAPH_ENROLLED_LINK) $(CDK2_PUBLIC_FULLGRAPH_ENROLLED_PE)
CDK2_NATIVE_CONFIG_OUTPUTS += $(CDK2_PUBLIC_FULLGRAPH_APP_OBJS) \
	$(CDK2_PUBLIC_FULLGRAPH_APP_LINK_OBJ) $(CDK2_PUBLIC_FULLGRAPH_APP_PE)
CDK2_NATIVE_CONFIG_OUTPUTS += $(CDK2_PUBLIC_FULLGRAPH_COLD_OBJ) \
	$(CDK2_PUBLIC_FULLGRAPH_COLD_LINK) $(CDK2_PUBLIC_FULLGRAPH_COLD_PE)
CDK2_NATIVE_CONFIG_OUTPUTS += $(CDK2_NORMAL_PRIVATE_APP_OBJ) \
	$(CDK2_NORMAL_PRIVATE_APP_LINK) $(CDK2_NORMAL_PRIVATE_APP_PE) $(CDK2_NORMAL_PRIVATE_AUTH_OBJ)
CDK2_NATIVE_CONFIG_OUTPUTS += $(addprefix $(CDK2_NATIVE_BUILD_DIR)/,\
	usb-bus-model-test usb-bus-io-test usb-bus-binding-test usb-bus-entry-test \
	usb-bus-rollback-test usb-bus-enumeration-test \
	usb-keyboard-model-test usb-keyboard-transport-test usb-keyboard-protocol-test \
	usb-keyboard-binding-test usb-keyboard-entry-test \
	usb-mass-model-test usb-mass-transport-test usb-mass-scsi-test \
	usb-mass-block-test usb-mass-binding-test usb-mass-entry-test \
	smmstore-test smmstore-variable-test smmstore-fvb-test smmstore-driver-test \
	cdk2-fat-binding-test cdk2-fat-protocol-abi-test cdk2-fat-entry-test \
	authenticode-timestamp-test-o0 authenticode-timestamp-test-o2 \
	image-authorization-test-o0 image-authorization-test-o2 \
	mem-set.o mem-copy-set.o signature-database.o bds-lvgl-form.o bds-lvgl-settings.o \
	liblvgl.a lvgl-config/lv_conf.h bearssl-x509-minimal.c \
	sata-diagnostic.o dxe-core-coreboot-checksum.o coreboot_dma_handoff.o)
# Recheck command inputs without rebuilding unchanged outputs. Keep this outside
# the output inventory so the stamp cannot acquire itself as a prerequisite.
CDK2_NATIVE_COMMAND_INPUTS := $(CDK2_NATIVE_BUILD_DIR)/command-inputs
CDK2_NATIVE_COMMAND_VARIABLES := $(sort CC HOSTCC AR \
	CDK2_NATIVE_CC CDK2_NATIVE_HOST_CC CDK2_NATIVE_LD \
	CDK2_NATIVE_OBJCOPY CDK2_NATIVE_OBJDUMP CDK2_NATIVE_READELF CDK2_NATIVE_NM \
	$(filter %FLAGS %INCLUDES %OPTS %OPTIONS,$(filter CDK2_%,$(.VARIABLES))))
$(CDK2_NATIVE_COMMAND_INPUTS): $(CDK2_CONFIG_HEADER) FORCE | $(CDK2_NATIVE_BUILD_DIR)
	@set -e; temporary="$@.$$$$.tmp"; \
		trap 'rm -f "$$temporary"' EXIT HUP INT TERM; \
		printf '%s\n' $(foreach variable,$(CDK2_NATIVE_COMMAND_VARIABLES),\
			'$(variable)=$(subst ','"'"',$($(variable)))') > "$$temporary"; \
		if ! cmp -s "$$temporary" "$@"; then mv -f "$$temporary" "$@"; fi
$(CDK2_NATIVE_CONFIG_OUTPUTS): .EXTRA_PREREQS = $(CDK2_CONFIG_HEADER) \
	$(CDK2_NATIVE_COMMAND_INPUTS) $(CDK2_DIR)/Makefile $(CDK2_DIR)/src/boot/Makefile

.PHONY: native-config-cache-test
native-check: native-config-cache-test
native-config-cache-test:
	@sh "$(CDK2_DIR)/tests/native_config_cache_test.sh"

.PHONY: native-host-dependency-test
native-check: native-host-dependency-test
native-host-dependency-test:
	@sh "$(CDK2_DIR)/tests/native_host_dependency_test.sh"

.PHONY: native-command-cache-test
native-check: native-command-cache-test
native-command-cache-test:
	@sh "$(CDK2_DIR)/tests/native_command_cache_test.sh"
