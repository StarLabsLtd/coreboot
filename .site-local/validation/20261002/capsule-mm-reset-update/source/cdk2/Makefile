# SPDX-License-Identifier: GPL-2.0-only

# Native cdk2 build entry point. This file deliberately builds only source that
# is owned by cdk2; it does not dispatch to external package metadata or
# consume an already-built firmware volume.

SHELL := /bin/bash
.DEFAULT_GOAL := all
.PHONY: FORCE

.PHONY: checkpatch-allowlist-check checkpatch-filter-test executable-bit-lint-test
.PHONY: license-header-lint-test
.PHONY: native-boundary-style-lint-test
.PHONY: authvar-transport-test authvar-runtime-test
.PHONY: authvar-presence-lifecycle-test authvar-presence-boundary-test \
	authvar-presence-bootstrap-test authvar-presence-composition-test \
	authvar-presence-composition-config-test \
	authvar-presence-lifecycle-close-client-test \
	authvar-presence-lifecycle-close-native-x86-test \
	authvar-presence-lifecycle-close-boundary-test \
	authvar-presence-lifecycle-close-composition-test \
	authvar-presence-lifecycle-close-wiring-test \
	authvar-presence-lifecycle-close-start-unwind-test
.PHONY: review-profile review-profile-check review-profile-update

checkpatch-allowlist-check:
	@util/lint/checkpatch-allowlist-check
.PHONY: legacy-boundary-inventory-test
legacy-boundary-inventory-test:
	@"$(CDK2_ROOT)/tests/legacy_boundary_inventory_test.sh"

check: legacy-boundary-inventory-test

.PHONY: pe-authenticode-test
pe-authenticode-test:
	@sh "$(CDK2_ROOT)/tests/pe_authenticode_test.sh"

check: pe-authenticode-test

.PHONY: native-fv-protocol-abi-test
native-fv-protocol-abi-test:
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		sh "$(CDK2_ROOT)/tests/native_fv_protocol_abi_test.sh"

check: native-fv-protocol-abi-test

.PHONY: authenticode-spc-test
authenticode-spc-test:
	@sh $(CDK2_ROOT)/tests/authenticode_spc_test.sh

check: authenticode-spc-test

.PHONY: signature-database-test
signature-database-test:
	@sh $(CDK2_ROOT)/tests/signature_database_test.sh

check: signature-database-test

.PHONY: image-policy-snapshot-test
image-policy-snapshot-test:
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		sh "$(CDK2_ROOT)/tests/image_policy_snapshot_copy_test.sh"
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		sh "$(CDK2_ROOT)/tests/image_policy_snapshot_test.sh"

check: image-policy-snapshot-test

.PHONY: config-header-cache-test
config-header-cache-test:
	@sh "$(CDK2_ROOT)/tests/config_header_cache_test.sh"

check: config-header-cache-test

.PHONY: config-header-input-selection-test
config-header-input-selection-test:
	@sh "$(CDK2_ROOT)/tests/config_header_input_selection_test.sh"

check: config-header-input-selection-test

.PHONY: bgrt-oracle-test
bgrt-oracle-test:
	@python3 "$(CDK2_ROOT)/util/qemu/bin/run-bgrt-oracle-selftest.py"

check: bgrt-oracle-test

.PHONY: authvar-service-abi-test authvar-service-parity-test \
	authvar-presence-abi-test authvar-presence-parity-test \
	authvar-presence-lifecycle-close-abi-test \
	authvar-presence-lifecycle-close-parity-test
authvar-service-abi-test:
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		"$(CDK2_ROOT)/tests/payload_mm_authvar_service_abi_test.sh"

authvar-service-parity-test:
	@test -n "$(COREBOOT_TREE)" || { \
		echo "COREBOOT_TREE is required" >&2; exit 2; }
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		"$(CDK2_ROOT)/tests/payload_mm_authvar_service_parity_test.sh" \
		"$(COREBOOT_TREE)"
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		sh "$(CDK2_ROOT)/tests/payload_mm_authvar_service_behavior_parity_test.sh" \
		"$(COREBOOT_TREE)"

check: authvar-service-abi-test

.PHONY: authvar-mailbox-hob-test
authvar-mailbox-hob-test:
	@sh "$(CDK2_ROOT)/tests/authvar_mailbox_hob_test.sh" \
		"$(if $(HOSTCC),$(HOSTCC),cc)" \
		"-I$(CDK2_BUILD_DIR)/include -I$(CDK2_ROOT)/include"

check: authvar-mailbox-hob-test

.PHONY: authvar-native-x86-test
authvar-native-x86-test:
	@sh "$(CDK2_ROOT)/tests/authvar_native_x86_test.sh"

check: authvar-native-x86-test

authvar-presence-abi-test:
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		"$(CDK2_ROOT)/tests/payload_mm_authvar_presence_test.sh"

authvar-presence-parity-test:
	@test -n "$(COREBOOT_TREE)" || { \
		echo "COREBOOT_TREE is required" >&2; exit 2; }
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		"$(CDK2_ROOT)/tests/payload_mm_authvar_presence_parity_test.sh" \
		"$(COREBOOT_TREE)"

check: authvar-presence-abi-test

authvar-presence-lifecycle-close-abi-test:
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		"$(CDK2_ROOT)/tests/payload_mm_authvar_presence_lifecycle_close_test.sh"

authvar-presence-lifecycle-close-parity-test:
	@test -n "$(COREBOOT_TREE)" || { \
		echo "COREBOOT_TREE is required" >&2; exit 2; }
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		"$(CDK2_ROOT)/tests/payload_mm_authvar_presence_lifecycle_close_parity_test.sh" \
		"$(COREBOOT_TREE)"

check: authvar-presence-lifecycle-close-abi-test

authvar-presence-lifecycle-close-client-test:
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		"$(CDK2_ROOT)/tests/payload_mm_authvar_presence_lifecycle_close_client_test.sh"

check: authvar-presence-lifecycle-close-client-test

authvar-presence-lifecycle-close-native-x86-test:
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		"$(CDK2_ROOT)/tests/authvar_presence_lifecycle_close_native_x86_test.sh"

check: authvar-presence-lifecycle-close-native-x86-test

authvar-presence-lifecycle-close-boundary-test:
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		"$(CDK2_ROOT)/tests/authvar_presence_lifecycle_close_boundary_test.sh"

check: authvar-presence-lifecycle-close-boundary-test

authvar-presence-lifecycle-close-composition-test:
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		"$(CDK2_ROOT)/tests/authvar_presence_lifecycle_close_composition_test.sh"

check: authvar-presence-lifecycle-close-composition-test

authvar-presence-lifecycle-close-wiring-test:
	@sh "$(CDK2_ROOT)/tests/authvar_presence_lifecycle_close_wiring_test.sh"

check: authvar-presence-lifecycle-close-wiring-test

authvar-presence-lifecycle-close-start-unwind-test:
	@CDK2_CONFIG_HEADER="$(CDK2_CONFIG_HEADER)" \
		sh "$(CDK2_ROOT)/tests/authvar_presence_lifecycle_close_start_unwind_test.sh"

check: authvar-presence-lifecycle-close-start-unwind-test

authvar-presence-bootstrap-test:
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		"$(CDK2_ROOT)/tests/payload_mm_authvar_presence_bootstrap_test.sh"

check: authvar-presence-bootstrap-test

authvar-presence-composition-test:
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		"$(CDK2_ROOT)/tests/authvar_presence_composition_test.sh"

check: authvar-presence-composition-test

authvar-presence-composition-config-test:
	@"$(CDK2_ROOT)/tests/authvar_presence_composition_config_test.sh"

check: authvar-presence-composition-config-test

.PHONY: system-fmp-test-key-prototype-test
system-fmp-test-key-prototype-test:
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		sh "$(CDK2_ROOT)/tests/system_fmp_test_key_test.sh"

.PHONY: system-fmp-migration-audit-test
system-fmp-migration-audit-test: system-fmp-test-key-prototype-test
	@sh "$(CDK2_ROOT)/tests/system_fmp_source_boundary_selftest.sh"
	@sh "$(CDK2_ROOT)/tests/system_fmp_source_boundary_test.sh"

.PHONY: lvgl-source-boundary-test
lvgl-source-boundary-test:
	@"$(CDK2_ROOT)/tests/lvgl_source_boundary_test.sh"

check: lvgl-source-boundary-test

authvar-transport-test:
	@sh "$(CDK2_ROOT)/tests/authvar_transport_test.sh"

check: authvar-transport-test

authvar-runtime-test:
	@sh "$(CDK2_ROOT)/tests/authvar_runtime_test.sh"

check: authvar-runtime-test

.PHONY: authvar-namespace-test
authvar-namespace-test:
	@sh "$(CDK2_ROOT)/tests/authvar_namespace_test.sh"

check: authvar-namespace-test

.PHONY: authvar-state-predicate-namespace-test
authvar-state-predicate-namespace-test:
	@sh "$(CDK2_ROOT)/tests/authvar_state_predicate_namespace_test.sh"

check: authvar-state-predicate-namespace-test

.PHONY: boot-private-buffer-test
boot-private-buffer-test:
	@sh tests/boot_private_buffer_test.sh

check: boot-private-buffer-test

.PHONY: authvar-namespace-join-test
authvar-namespace-join-test:
	@sh "$(CDK2_ROOT)/tests/authvar_namespace_join_test.sh"

.PHONY: protected-variable-config-test protected-variable-entry-test protected-variable-test
protected-variable-config-test:
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" MAKE="$(MAKE)" \
		sh "$(CDK2_ROOT)/tests/protected_variable_config_test.sh"

check: protected-variable-config-test

protected-variable-entry-test:
	@test -n "$(COREBOOT_TREE)" || { \
		echo "COREBOOT_TREE is required" >&2; exit 2; }
	@$(MAKE) --no-print-directory native-protected-variable-entry-test \
		COREBOOT_TREE="$(abspath $(COREBOOT_TREE))" MBEDTLS_SOURCE="$(MBEDTLS_SOURCE)" \
		HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)"

protected-variable-test: protected-variable-config-test protected-variable-entry-test

.PHONY: authvar-fresh-policy-join-test
authvar-fresh-policy-join-test:
	@test -n "$(COREBOOT_TREE)" || { \
		echo "COREBOOT_TREE is required" >&2; exit 2; }
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" COREBOOT_TREE="$(abspath $(COREBOOT_TREE))" \
		MBEDTLS_SOURCE="$(MBEDTLS_SOURCE)" \
		sh "$(CDK2_ROOT)/tests/authvar_fresh_policy_join_test.sh"

.PHONY: authvar-runtime-owner-test
authvar-runtime-owner-test:
	@sh "$(CDK2_ROOT)/tests/authvar_runtime_owner_test.sh"

check: authvar-runtime-owner-test

authvar-presence-lifecycle-test:
	@sh "$(CDK2_ROOT)/tests/authvar_presence_lifecycle_test.sh"

check: authvar-presence-lifecycle-test

authvar-presence-boundary-test:
	@sh "$(CDK2_ROOT)/tests/authvar_presence_boundary_mutation_test.sh"

check: authvar-presence-boundary-test

.PHONY: usb-bus-superspeed-reset-mutation-test
usb-bus-superspeed-reset-mutation-test: config
	@"$(CDK2_ROOT)/tests/usb_bus_superspeed_reset_mutation_test.sh" \
		"$(CDK2_CONFIG_HEADER)"

check: usb-bus-superspeed-reset-mutation-test


.PHONY: lvgl-dependency-provenance-test
lvgl-dependency-provenance-test:
	@HOSTCC="$(if $(HOSTCC),$(HOSTCC),cc)" \
		"$(CDK2_ROOT)/tests/lvgl_dependency_provenance_test.sh"

.PHONY: review-profile-test
checkpatch-filter-test:
	@set -e; binary=$$(mktemp "$${TMPDIR:-/tmp}/cdk2-checkpatch-filter-test.XXXXXX"); \
	trap 'rm -f "$$binary"' EXIT HUP INT TERM; \
	"$${HOSTCC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 \
		"$(CDK2_ROOT)/tests/checkpatch_filter_test.c" -o "$$binary"; \
	"$$binary"; \
	sh "$(CDK2_ROOT)/tests/checkpatch_filter_test.sh"; \
	sh "$(CDK2_ROOT)/tests/checkpatch_allowlist_paths_test.sh"

native-boundary-style-lint-test:
	@sh "$(CDK2_ROOT)/tests/native_boundary_style_lint_test.sh"

license-header-lint-test:
	@sh "$(CDK2_ROOT)/tests/license_header_lint_test.sh"

executable-bit-lint-test:
	@sh "$(CDK2_ROOT)/tests/executable_bit_lint_test.sh"

check: executable-bit-lint-test license-header-lint-test \
	native-boundary-style-lint-test

review-profile:
	"$(CDK2_ROOT)/util/review_profile/coreboot-review-profile" verify

review-profile-check: review-profile-test
	"$(CDK2_ROOT)/util/review_profile/coreboot-review-profile" verify

review-profile-update:
	"$(CDK2_ROOT)/util/review_profile/coreboot-review-profile" update

review-profile-test:
	"$(CDK2_ROOT)/tests/review-profile-test"

override CDK2_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
override CDK2_ROOT := $(patsubst %/,%,$(CDK2_DIR))
CDK2_BUILD_DIR ?= $(CDK2_ROOT)/build/cdk2
override CDK2_BUILD_DIR := $(abspath $(CDK2_BUILD_DIR))
CDK2_CONFIG ?= $(CDK2_BUILD_DIR)/.config
CDK2_DEFCONFIG ?= $(CDK2_ROOT)/defconfig
CDK2_KCONFIG ?= $(CDK2_ROOT)/Kconfig
CDK2_CHECKPATCH_GATE := $(if $(wildcard \
	$(CDK2_ROOT)/util/lint/checkpatch-allowlist-check),checkpatch-allowlist-check)
CDK2_REVIEW_PROFILE_GATE := $(if $(wildcard \
	$(CDK2_ROOT)/review-profile/coreboot/generated/checkpoint.jsonl),review-profile-check)
CDK2_KCONFIG_TOOL ?= kconfig-conf
CDK2_KCONFIG_WORKDIR := $(CDK2_BUILD_DIR)/kconfig
CDK2_KCONFIG_INPUT := $(CDK2_KCONFIG_WORKDIR)/Kconfig
CDK2_DEFCONFIG_INPUT := $(CDK2_KCONFIG_WORKDIR)/defconfig
CDK2_COREBOOT_INPUT_IDENTITY := $(CDK2_KCONFIG_WORKDIR)/coreboot-input.identity
CDK2_CONFIG_HEADER ?= $(CDK2_BUILD_DIR)/include/cdk2/config.h
usb-bus-superspeed-reset-mutation-test: $(CDK2_CONFIG_HEADER)
authvar-mailbox-hob-test: $(CDK2_CONFIG_HEADER)

authvar-presence-lifecycle-close-start-unwind-test: $(CDK2_CONFIG_HEADER)
CDK2_KCONFIG_AUTOCONFIG ?= $(CDK2_BUILD_DIR)/auto.conf
CDK2_KCONFIG_AUTOHEADER ?= $(CDK2_BUILD_DIR)/include/generated/autoconf.h
CDK2_KCONFIG_RUSTCCFG ?= $(CDK2_BUILD_DIR)/include/generated/rustc_cfg
CDK2_KCONFIG_SPLITCONFIG ?= $(CDK2_BUILD_DIR)/config/
CDK2_MANIFEST ?= $(CDK2_BUILD_DIR)/cdk2-native-sources.txt
override CDK2_SYSTEM_FMP_PROVENANCE := \
	$(CDK2_ROOT)/migration/system-fmp-provenance.tsv
override CDK2_SYSTEM_FMP_PROVENANCE_SHA256 := \
	d74060d687b989d80369afd6d9ccbff317ee3540557d3dd8db7b2d8ce2d3ee5a
override CDK2_SYSTEM_FMP_PROVENANCE_SOURCE := \
	$(CDK2_ROOT)/util/system_fmp_provenance.c
override CDK2_SYSTEM_FMP_PROVENANCE_VERIFIER := \
	$(CDK2_BUILD_DIR)/system-fmp-provenance
CDK2_SYSTEM_FMP_HOST_CC ?= $(if $(strip $(HOSTCC)),$(HOSTCC),cc)
COREBOOT_CONFIG ?=
COREBOOT_CDK2_PROFILE ?=
COREBOOT_OUTPUT_DIR ?=
override CDK2_NATIVE_DIR := $(CDK2_ROOT)/src/boot
CDK2_NATIVE_SOURCE_DATE_PATHS := Kconfig Makefile defconfig src include util tests configs
CDK2_SOURCE_DATE_EPOCH ?= $(shell epoch=$$(git -C "$(CDK2_ROOT)" log -1 --format=%ct -- $(CDK2_NATIVE_SOURCE_DATE_PATHS) 2>/dev/null); test -n "$$epoch" || epoch=0; printf '%s' "$$epoch")
SOURCE_DATE_EPOCH ?= $(CDK2_SOURCE_DATE_EPOCH)
export SOURCE_DATE_EPOCH

define cdk2_validate_config
	@if grep -qx 'CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME=y' "$(CDK2_CONFIG)"; then \
		test -n "$(COREBOOT_CONFIG)" && \
		cmp -s "$(COREBOOT_CONFIG)" "$(CDK2_KCONFIG_WORKDIR)/coreboot-source.tmp" && \
		grep -qx 'CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED=y' \
			"$(CDK2_KCONFIG_WORKDIR)/coreboot-source.tmp" || { \
			printf '%s\n' 'protected variables require the resolved coreboot service route source' >&2; \
			exit 1; \
		}; \
	fi
	@awk '\
		function fail(message) { print message > "/dev/stderr"; failed = 1 } \
		function record(name, value) { \
			if (seen[name]++) \
				fail("duplicate " name " assignment in $(CDK2_CONFIG)"); \
			values[name] = value; \
		} \
		/^CONFIG_CDK2_LINEAR_BOOT=[yn]$$/ { \
			record("CONFIG_CDK2_LINEAR_BOOT", substr($$0, index($$0, "=") + 1)); \
			next; \
		} \
		/^# CONFIG_CDK2_LINEAR_BOOT is not set$$/ { \
			record("CONFIG_CDK2_LINEAR_BOOT", "n"); next; \
		} \
		/^CONFIG_CDK2_LINEAR_SETUP_HOTKEY=[yn]$$/ { \
			record("CONFIG_CDK2_LINEAR_SETUP_HOTKEY", substr($$0, index($$0, "=") + 1)); \
			next; \
		} \
		/^# CONFIG_CDK2_LINEAR_SETUP_HOTKEY is not set$$/ { \
			record("CONFIG_CDK2_LINEAR_SETUP_HOTKEY", "n"); next; \
		} \
		/^CONFIG_CDK2_BOOT_TIMEOUT=(0|[1-9][0-9]?)$$/ { \
			line = substr($$0, index($$0, "=") + 1); \
			if (line > 60) \
				fail("CONFIG_CDK2_BOOT_TIMEOUT is outside range 0..60 in $(CDK2_CONFIG)"); \
			record("CONFIG_CDK2_BOOT_TIMEOUT", line); next; \
		} \
		/CONFIG_CDK2_LINEAR_BOOT/ { \
			if ($$0 !~ /^[[:space:]]*#/) \
				fail("malformed CONFIG_CDK2_LINEAR_BOOT assignment in $(CDK2_CONFIG)"); \
			next; \
		} \
		/CONFIG_CDK2_BOOT_TIMEOUT/ { \
			if ($$0 !~ /^[[:space:]]*#/) \
				fail("malformed CONFIG_CDK2_BOOT_TIMEOUT assignment in $(CDK2_CONFIG)"); \
		} \
		/CONFIG_CDK2_LINEAR_SETUP_HOTKEY/ { \
			if ($$0 !~ /^[[:space:]]*#/) \
				fail("malformed CONFIG_CDK2_LINEAR_SETUP_HOTKEY assignment in $(CDK2_CONFIG)"); \
			next; \
		} \
		END { \
			if (values["CONFIG_CDK2_LINEAR_BOOT"] == "y" && \
			    values["CONFIG_CDK2_BOOT_TIMEOUT"] != "0" && \
			    values["CONFIG_CDK2_LINEAR_SETUP_HOTKEY"] != "y") \
				fail("CDK2 linear boot requires CDK2_BOOT_TIMEOUT=0 unless CDK2_LINEAR_SETUP_HOTKEY=y"); \
			exit failed; \
		}' "$(CDK2_CONFIG)"
endef

CDK2_KCONFIG_ENV = \
	KCONFIG_CONFIG="$(abspath $(CDK2_CONFIG))" \
	KCONFIG_AUTOCONFIG="$(abspath $(CDK2_KCONFIG_AUTOCONFIG))" \
	KCONFIG_AUTOHEADER="$(abspath $(CDK2_KCONFIG_AUTOHEADER))" \
	KCONFIG_RUSTCCFG="$(abspath $(CDK2_KCONFIG_RUSTCCFG))" \
	KCONFIG_SPLITCONFIG="$(abspath $(CDK2_KCONFIG_SPLITCONFIG))"

CDK2_CONFIG_TARGETS := build build-image check manifest modules native-stage \
	native-coreboot-stage native-coreboot-image native-check native-service-test \
	native-linear-boot-test native-capsule-disk native-capsule-disk-config-test \
	native-early-splash-test native-mtrr-ownership-audit native-direct-mtrr-ownership-audit \
	native-direct-composition-contract-test native-mtrr-parallel-test \
	native-sata-controller-oracle native-coreboot-test native-cbmem-console-test \
	native-spi-console-budget-test native-diagnostic-literal-profile-test \
	native-diagnostic-legacy-profile-test native-diagnostic-core-dependency-test \
	native-diagnostic-image-inventory native-pe-test native-null-memory-test-test \
	native-entry-test native-elfcheck-test native-security-stub-test \
	native-watchdog-diagnostic-parity native-monotonic-counter-test native-runtime-arch-test \
	native-uhci-test native-ehci-test native-nvme-test native-sdmmc-pci-test native-sd-dxe-test \
	native-sd-dxe-oracle native-emmc-dxe-test native-emmc-dxe-oracle native-variable-runtime-test \
	native-variable-runtime-oracle native-variable-runtime-diagnostic-parity \
	native-diagnostic-ledgers native-cpu-arch-test native-cpu-diagnostic-parity \
	native-status-code-router-test native-cpu-io2-test native-english-test \
	native-status-code-handler-test native-status-code-handler-diagnostic-parity \
	native-disk-io-test native-disk-io-diagnostic-parity native-serial-io-test \
	native-reset-system-test native-reset-system-diagnostic-parity native-pcat-rtc-test \
	native-pcat-rtc-diagnostic-parity native-local-apic-timer-test native-graphics-console-test \
	native-graphics-console-diagnostic-parity native-con-platform-test native-con-splitter-test \
	native-con-splitter-diagnostic-parity native-ec-battery-test native-lvgl-renderer-test \
	native-scsi-bus-test native-scsi-disk-test native-scsi-disk-oracle native-xhci-test \
	native-usb-bus-test native-usb-mass-test native-usb-mass-qemu-oracle native-usb-keyboard-test \
	native-usb-mouse-test native-sio-bus-test native-ps2-mouse-test native-terminal-test \
	native-graphics-output-test native-graphics-output-diagnostic-parity native-acpi-table-test \
	native-smmstore-test native-smmstore-diagnostic-parity native-ftw-test native-scsi-bus-oracle \
	native-bds-diagnostic-parity native-capsule-runtime-test native-capsule-disk-test \
	native-capsule-delivery-policy-test \
	native-capsule-runtime-diagnostic-parity native-tcg2-transport-test native-tcg2-commands-test \
	native-tcg2-event-log-test native-tcg2-measure-test native-tcg2-service-test \
	native-tcg2-entry-test native-tpm2-acpi-table-test native-tpm2-acpi-diagnostic-parity \
	native-security-stub-diagnostic-parity native-esrt-test native-esrt-diagnostic-parity \
	native-device-path-test native-partition-test native-pci-host-bridge-oracle native-fat-test \
	native-fat-oracle native-pci-bus-test native-pci-enumerate-test \
	native-ata-atapi-pass-thru-test native-ata-atapi-binding-test native-ata-atapi-ahci-test \
	native-ata-atapi-ide-test native-ata-atapi-pci-test native-ata-atapi-entry-test \
	native-ata-protocol-test native-ata-async-test native-ata-backend-ide-test \
	native-ata-bus-model-test native-ata-bus-io-test native-ata-bus-block-test \
	native-ata-bus-binding-test native-ata-bus-entry-test native-ata-bus-oracle \
	native-ata-atapi-oracle native-acpi-table-diagnostic-parity native-bds-test native-bds-oracle \
	native-dxe-core-test native-dxe-core-diagnostic-parity native-splash-status-report-test \
	native-protected-variable-boot-gate-test \
	native-canonical-image-policy-test native-protected-variable-entry-test \
	native-protected-variable-component-test \
	native-protected-variable-service-component-test native-protected-variable-service-compile-test \
	native-protected-variable-public-service-component-test native-protected-variable-public-service-compile-test \
	native-capsule-mm-reset-component-test \
	native-capsule-mm-update-component-test \
	native-dxe-core-graphics-dependency-test native-ec-battery-diagnostic-parity \
	native-ec-battery-kconfig-test native-ftw-diagnostic-parity native-nvme-diagnostic-parity \
	native-sdmmc-pci-diagnostic-parity native-shell-test native-tcg2-diagnostic-parity \
	native-terminal-diagnostic-parity native-xhci-diagnostic-parity native-ehci-diagnostic-parity \
	native-uhci-diagnostic-parity native-usb-bus-diagnostic-parity \
	native-usb-mass-diagnostic-parity native-pci-bus-diagnostic-parity \
	native-pci-host-bridge-diagnostic-parity manifest-check print
CDK2_CONFIG_TARGETS += native-strict-dxe-compat-closure-test
CDK2_CONFIG_TARGETS += native-pci-host-bridge-test
CDK2_CONFIG_TARGETS += native-pci-bus-config-toggle-test
CDK2_CONFIG_TARGETS += native-direct-pair-provenance-test

CDK2_CONFIG_TARGETS += native-variable-runtime-malformed-test
CDK2_CONFIG_TARGETS += native-dma-handoff-test
CDK2_CONFIG_TARGETS += native-hii-retirement-test
CDK2_CONFIG_TARGETS += native-efi-loader-probe native-efi-loader-matrix-test
CDK2_CONFIG_TARGETS += native-secure-boot-qemu-fixtures \
	native-secure-boot-test-pki-test native-secure-boot-loader-matrix-test \
	native-secure-boot-qemu-config-test native-secure-boot-state-test \
	native-secure-boot-setup-profile-test
CDK2_CONFIG_TARGETS += native-dma-handoff-integration-test
CDK2_CONFIG_TARGETS += native-system-fmp-handoff-test
CDK2_CONFIG_TARGETS += native-system-fmp-policy native-system-fmp-policy-test
CDK2_CONFIG_TARGETS += native-system-fmp-auth native-system-fmp-auth-test
CDK2_CONFIG_TARGETS += native-system-fmp-crypto native-system-fmp-crypto-test
CDK2_CONFIG_TARGETS += native-authenticode-crypto-test
CDK2_CONFIG_TARGETS += native-authenticode-timestamp-test
CDK2_CONFIG_TARGETS += native-protected-image-policy-join-test
CDK2_CONFIG_TARGETS += native-protected-image-policy-consumer-test
CDK2_CONFIG_TARGETS += native-protected-variable-linear-composition-test
CDK2_CONFIG_TARGETS += native-protected-variable-fullgraph-runtime-app \
	native-protected-variable-fullgraph-diagnostic-test
CDK2_CONFIG_TARGETS += native-image-authorization-test
CDK2_CONFIG_TARGETS += native-system-fmp-payload native-system-fmp-payload-test
CDK2_CONFIG_TARGETS += native-system-fmp-state native-system-fmp-state-test
CDK2_CONFIG_TARGETS += native-system-fmp-board-binding \
	native-system-fmp-board-binding-test
CDK2_CONFIG_TARGETS += native-system-fmp-coordinator \
	native-system-fmp-coordinator-test
CDK2_CONFIG_TARGETS += native-system-fmp-session \
	native-system-fmp-session-test
CDK2_CONFIG_TARGETS += native-system-fmp-provider-test \
	native-system-fmp-entry-test \
	native-system-fmp-production-closure-test
CDK2_CONFIG_TARGETS += native-con-platform-profile-test
CDK2_CONFIG_TARGETS += native-direct-image-inventory \
	native-direct-image-inventory-test native-direct-image-table \
	native-direct-image-table-test \
	native-direct-image-generator-dependency-test \
	native-direct-image-parser-source-test \
	native-strict-direct-cbfs-admission-test \
	native-direct-dispatch-test \
	native-direct-pcd-closure-test \
	native-direct-dxe-core-bridge-test \
	native-strict-direct-cbfs-path-test \
	native-strict-direct-cbfs-inventory-test
CDK2_CONFIG_TARGETS += native-linear-payload-admission \
	native-linear-payload-admission-test
CDK2_CONFIG_TARGETS += native-bl-support-retirement-test
CDK2_CONFIG_TARGETS += native-linear-connect-ownership-test
CDK2_CONFIG_TARGETS += native-lvgl-settings-test native-bds-settings-test \
	native-bds-setup-contract-test native-bds-setup-mutants-test
CDK2_CONFIG_TARGETS += native-dxe-core-link-parallel-test
CDK2_CONFIG_TARGETS += native-dxe-core-connect-trace-policy-test
CDK2_CONFIG_TARGETS += native-dxe-core-dispatch-policy-test
CDK2_CONFIG_TARGETS += native-dxe-core-disconnect-test
CDK2_CONFIG_TARGETS += native-linear-apic-timer-boundary-test
CDK2_CONFIG_TARGETS += native-linear-xhci-poll-boundary-test
CDK2_CONFIG_TARGETS += native-usb-bus-linearity-test
CDK2_CONFIG_TARGETS += native-scratch-make-environment-test
CDK2_CONFIG_TARGETS += native-deadline-test native-deadline-provenance-test \
	native-deadline-cbmem-mutation-test native-deadline-tsc-isolation-test
CDK2_CONFIG_TARGETS += native-diagnostic-smm-classification-test \
	native-diagnostic-smm-artifact-test

CDK2_RECURSIVE_ARGS := \
	CDK2_DIR="$(CDK2_DIR)" \
	CDK2_ROOT="$(CDK2_ROOT)" \
	CDK2_BUILD_DIR="$(CDK2_BUILD_DIR)" \
	CDK2_CONFIG="$(CDK2_CONFIG)" \
	CDK2_DEFCONFIG="$(CDK2_DEFCONFIG)" \
	CDK2_KCONFIG="$(CDK2_KCONFIG)" \
	CDK2_KCONFIG_TOOL="$(CDK2_KCONFIG_TOOL)" \
	$(if $(COREBOOT_CONFIG),COREBOOT_CONFIG="$(abspath $(COREBOOT_CONFIG))") \
	CDK2_NATIVE_DIR="$(CDK2_NATIVE_DIR)" \
	$(if $(HOSTCC),HOSTCC="$(HOSTCC)") \
	$(if $(CC),CC="$(CC)") \
	$(if $(CDK2_NATIVE_CC),CDK2_NATIVE_CC="$(CDK2_NATIVE_CC)") \
	$(if $(CDK2_NATIVE_HOST_CC),CDK2_NATIVE_HOST_CC="$(CDK2_NATIVE_HOST_CC)") \
	$(if $(CDK2_NATIVE_OBJCOPY),CDK2_NATIVE_OBJCOPY="$(CDK2_NATIVE_OBJCOPY)") \
	$(if $(CDK2_NATIVE_READELF),CDK2_NATIVE_READELF="$(CDK2_NATIVE_READELF)") \
	$(if $(CDK2_NATIVE_NM),CDK2_NATIVE_NM="$(CDK2_NATIVE_NM)")

.PHONY: all build build-image config defconfig olddefconfig menuconfig prepare-kconfig check \
	manifest modules native-stage native-coreboot-stage native-coreboot-image native-check \
	native-service-test native-coreboot-test native-linear-boot-test \
	native-diagnostic-literal-profile-test native-early-splash-test native-pe-test \
	native-entry-test native-security-stub-test native-null-memory-test-test \
	native-watchdog-diagnostic-parity native-monotonic-counter-test native-runtime-arch-test \
	native-uhci-test native-ehci-test native-nvme-test native-sdmmc-pci-test native-sd-dxe-test \
	native-sd-dxe-oracle native-emmc-dxe-test native-emmc-dxe-oracle native-variable-runtime-test \
	native-variable-runtime-oracle native-variable-runtime-diagnostic-parity \
	native-diagnostic-ledgers native-cpu-arch-test native-cpu-diagnostic-parity \
	native-status-code-router-test native-cpu-io2-test native-english-test \
	native-status-code-handler-test native-status-code-handler-diagnostic-parity \
	native-disk-io-test native-disk-io-diagnostic-parity native-serial-io-test \
	native-reset-system-test native-reset-system-diagnostic-parity native-pcat-rtc-test \
	native-pcat-rtc-diagnostic-parity native-local-apic-timer-test native-graphics-console-test \
	native-graphics-console-diagnostic-parity native-con-platform-test native-con-splitter-test \
	native-con-splitter-diagnostic-parity native-ec-battery-test native-lvgl-renderer-test \
	native-scsi-bus-test native-scsi-disk-test native-scsi-disk-oracle native-xhci-test \
	native-usb-bus-test native-usb-mass-test native-usb-mass-qemu-oracle native-usb-keyboard-test \
	native-usb-mouse-test native-sio-bus-test native-ps2-mouse-test native-terminal-test \
	native-graphics-output-test native-graphics-output-diagnostic-parity native-acpi-table-test \
	native-smmstore-test native-smmstore-diagnostic-parity native-ftw-test native-scsi-bus-oracle \
	native-capsule-runtime-test native-capsule-disk-test native-capsule-disk \
	native-capsule-disk-config-test native-capsule-runtime-diagnostic-parity \
	native-tcg2-transport-test native-tcg2-commands-test native-tcg2-event-log-test \
	native-tcg2-measure-test native-tcg2-service-test native-tcg2-entry-test \
	native-tpm2-acpi-table-test native-tpm2-acpi-diagnostic-parity \
	native-security-stub-diagnostic-parity native-esrt-test native-esrt-diagnostic-parity \
	native-device-path-test native-partition-test native-fat-test native-fat-oracle \
	native-elfcheck-test manifest-check print lint lint-stable lint-extended test-lint jenkins \
	what-jenkins-does retained-fv-check wave1-map-check coreboot-stage clean FORCE
.PHONY: native-direct-image-inventory native-direct-image-inventory-test \
	native-direct-image-table native-direct-image-table-test \
	native-direct-image-generator-dependency-test \
	native-direct-image-parser-source-test \
	native-direct-dispatch-test \
	native-direct-dxe-core-bridge-test \
	native-strict-direct-cbfs-admission-test \
	native-strict-direct-cbfs-path-test \
	native-strict-direct-cbfs-inventory-test
.PHONY: native-linear-payload-admission native-linear-payload-admission-test
.PHONY: native-variable-runtime-malformed-test
.PHONY: system-fmp-provenance-check system-fmp-provenance-test \\
	system-fmp-provenance-reproduce
.DELETE_ON_ERROR: $(CDK2_CONFIG) $(CDK2_CONFIG_HEADER) $(CDK2_MANIFEST)

all: build

.PHONY: system-fmp-provenance-host-tool
system-fmp-provenance-host-tool: $(CDK2_SYSTEM_FMP_PROVENANCE_VERIFIER)

$(CDK2_SYSTEM_FMP_PROVENANCE_VERIFIER): \
		$(CDK2_SYSTEM_FMP_PROVENANCE_SOURCE) FORCE
	@mkdir -p "$(dir $@)"
	@$(CDK2_SYSTEM_FMP_HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror \
		"$<" -o "$@"

system-fmp-provenance-check: system-fmp-provenance-host-tool
	@test -f "$(CDK2_SYSTEM_FMP_FFS_SOURCE)" || { \
		printf '%s\n' 'CDK2_SYSTEM_FMP_FFS_SOURCE is required' >&2; exit 1; \
	}
	@test -f "$(CDK2_SYSTEM_FMP_PE_SOURCE)" || { \
		printf '%s\n' 'CDK2_SYSTEM_FMP_PE_SOURCE is required' >&2; exit 1; \
	}
	@"$(CDK2_SYSTEM_FMP_PROVENANCE_VERIFIER)" verify \
		--manifest "$(CDK2_SYSTEM_FMP_PROVENANCE)" \
		--manifest-sha256 "$(CDK2_SYSTEM_FMP_PROVENANCE_SHA256)" \
		--ffs "$(CDK2_SYSTEM_FMP_FFS_SOURCE)" \
		--pe "$(CDK2_SYSTEM_FMP_PE_SOURCE)"

system-fmp-provenance-test: system-fmp-provenance-check
	@"$(CDK2_ROOT)/tests/system_fmp_provenance_test.sh" \
		"$(CDK2_SYSTEM_FMP_PROVENANCE_VERIFIER)" \
		"$(CDK2_SYSTEM_FMP_PROVENANCE)" \
		"$(CDK2_SYSTEM_FMP_PROVENANCE_SHA256)" \
		"$(CDK2_SYSTEM_FMP_FFS_SOURCE)" \
		"$(CDK2_SYSTEM_FMP_PE_SOURCE)"

system-fmp-provenance-reproduce: system-fmp-provenance-host-tool
	@"$(CDK2_SYSTEM_FMP_PROVENANCE_VERIFIER)" reproduce \
		--manifest "$(CDK2_SYSTEM_FMP_PROVENANCE)" \
		--manifest-sha256 "$(CDK2_SYSTEM_FMP_PROVENANCE_SHA256)"

lint-stable:
	@util/lint/lint lint-stable

lint: $(CDK2_CHECKPATCH_GATE)
	@util/lint/lint lint-stable
	@util/lint/lint lint

lint-extended:
	@util/lint/lint lint-extended

test-lint: lint

what-jenkins-does: lint check manifest-check retained-fv-check wave1-map-check \
	native-capsule-disk-profile-artifact-test profile-config-test \
	qemu-acceptance-profile-config-test efi-loader-matrix-test \
	linear-config-test cbmem-console-config-test
	@set -e; log="$$(mktemp "$${TMPDIR:-/tmp}/cdk2-build-image.XXXXXX")"; \
	if $(MAKE) --no-print-directory $(CDK2_RECURSIVE_ARGS) -f "$(CDK2_ROOT)/Makefile" build-image >"$$log" 2>&1; then \
		cat "$$log"; rm -f "$$log"; \
		printf '%s\n' 'build-image unexpectedly succeeded before payload modules are native' >&2; exit 1; \
	fi; \
	grep -q 'no native payload-image target' "$$log" || { cat "$$log"; rm -f "$$log"; \
		printf '%s\n' 'build-image did not fail with the native-boundary message' >&2; exit 1; }; \
	rm -f "$$log"; printf '%s\n' 'build-image: expected native-boundary failure confirmed'

.PHONY: profile-config-test native-capsule-disk-profile-artifact-test cbmem-console-config-test
profile-config-test:
	@"$(CDK2_ROOT)/tests/profile_config_test.sh"

.PHONY: qemu-acceptance-profile-config-test
qemu-acceptance-profile-config-test:
	@"$(CDK2_ROOT)/tests/qemu_acceptance_profile_config_test.sh"

.PHONY: efi-loader-matrix-test
efi-loader-matrix-test:
	@"$(CDK2_ROOT)/tests/efi_loader_matrix_test.sh"

native-capsule-disk-profile-artifact-test:
	@"$(CDK2_ROOT)/tests/capsule_disk_profile_artifact_test.sh" \
		"$(MAKE)" "$(CDK2_ROOT)"

.PHONY: linear-config-test
linear-config-test:
	@"$(CDK2_ROOT)/tests/linear_config_test.sh"

cbmem-console-config-test:
	@"$(CDK2_ROOT)/tests/cbmem_console_config_test.sh"

jenkins: what-jenkins-does

coreboot-stage:
	@test -n "$(COREBOOT_CONFIG)" || { \
		printf '%s\n' 'COREBOOT_CONFIG is required' >&2; \
		exit 1; \
	}
	@test -f "$(COREBOOT_CONFIG)" || { \
		printf '%s\n' 'COREBOOT_CONFIG does not exist: $(COREBOOT_CONFIG)' >&2; \
		exit 1; \
	}
	@test -n "$(COREBOOT_OUTPUT_DIR)" || { \
		printf '%s\n' 'COREBOOT_OUTPUT_DIR is required' >&2; \
		exit 1; \
	}
	@case "$(abspath $(COREBOOT_CONFIG))" in \
		"$(abspath $(COREBOOT_OUTPUT_DIR))"|"$(abspath $(COREBOOT_OUTPUT_DIR))"/*) \
			printf '%s\n' 'COREBOOT_OUTPUT_DIR must not contain COREBOOT_CONFIG' >&2; \
			exit 1; \
			;; \
	esac
	@mkdir -p "$(abspath $(COREBOOT_OUTPUT_DIR))"
	@awk '\
		function fail(message) { print message > "/dev/stderr"; failed = 1 } \
		/^CONFIG_PAYLOAD_DMA_HANDOFF=y$$/ { \
			if (dma_seen++) fail("duplicate CONFIG_PAYLOAD_DMA_HANDOFF"); \
			dma_enabled = 1; next; \
		} \
		/^CONFIG_PAYLOAD_DMA_HANDOFF=n$$/ || \
		/^# CONFIG_PAYLOAD_DMA_HANDOFF is not set$$/ { \
			if (dma_seen++) fail("duplicate CONFIG_PAYLOAD_DMA_HANDOFF"); \
			dma_enabled = 0; next; \
		} \
		/CONFIG_PAYLOAD_DMA_HANDOFF/ { \
			fail("malformed CONFIG_PAYLOAD_DMA_HANDOFF"); next; \
		} \
		/^CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED=y$$/ { \
			if (service_seen++) fail("duplicate CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED"); \
			service_enabled = 1; next; \
		} \
		/^CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED=n$$/ || \
		/^# CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED is not set$$/ { \
			if (service_seen++) fail("duplicate CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED"); \
			service_enabled = 0; next; \
		} \
		/CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED/ { \
			fail("malformed CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED"); next; \
		} \
		!/^CONFIG_CDK2_RETAINED_FV(_SHA256)?=/ && \
		!/^# CONFIG_CDK2_RETAINED_FV(_SHA256)? is not set$$/ && \
		!/^CONFIG_CDK2_SYSTEM_FMP_FFS(_SHA256)?=/ && \
		!/^# CONFIG_CDK2_SYSTEM_FMP_FFS(_SHA256)? is not set$$/ && \
		!/^CONFIG_CDK2_SYSTEM_FMP_PE(_SHA256)?=/ && \
		!/^# CONFIG_CDK2_SYSTEM_FMP_PE(_SHA256)? is not set$$/ && \
		(/^CONFIG_CDK2_[A-Za-z0-9_]+=/ || \
		 /^# CONFIG_CDK2_[A-Za-z0-9_]+ is not set$$/ || \
		 /^CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=/ || \
		 /^# CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT is not set$$/) { \
			if ($$0 ~ /^CONFIG_CDK2_/ || $$0 ~ /^# CONFIG_CDK2_/) \
				cdk2_seen = 1; \
			print; \
		} \
		END { \
			if (!cdk2_seen) \
				fail("COREBOOT_CONFIG contains no CONFIG_CDK2 options"); \
			if (dma_seen == 1 && dma_enabled) \
				print "CONFIG_PAYLOAD_DMA_HANDOFF=y"; \
			else \
				print "# CONFIG_PAYLOAD_DMA_HANDOFF is not set"; \
			if (service_seen == 1 && service_enabled) \
				print "CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED=y"; \
			else \
				print "# CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED is not set"; \
			exit failed; \
		}' "$(abspath $(COREBOOT_CONFIG))" \
		> "$(abspath $(COREBOOT_OUTPUT_DIR))/.config.tmp" || { \
		rm -f "$(abspath $(COREBOOT_OUTPUT_DIR))/.config.tmp"; \
		exit 1; \
	}
	@if test -n "$(COREBOOT_CDK2_PROFILE)"; then \
		test -f "$(COREBOOT_CDK2_PROFILE)" || { \
			printf '%s\n' 'COREBOOT_CDK2_PROFILE does not exist: $(COREBOOT_CDK2_PROFILE)' >&2; \
			exit 1; \
		}; \
		awk '/^CONFIG_CDK2_[A-Za-z0-9_]+=/ || /^# CONFIG_CDK2_[A-Za-z0-9_]+ is not set$$/' \
			"$(COREBOOT_CDK2_PROFILE)" >> "$(abspath $(COREBOOT_OUTPUT_DIR))/.config.tmp"; \
	fi
	@test -s "$(abspath $(COREBOOT_OUTPUT_DIR))/.config.tmp" || { \
		rm -f "$(abspath $(COREBOOT_OUTPUT_DIR))/.config.tmp"; \
		printf '%s\n' 'COREBOOT_CONFIG contains no CONFIG_CDK2 options' >&2; \
		exit 1; \
	}
	@mv "$(abspath $(COREBOOT_OUTPUT_DIR))/.config.tmp" \
		"$(abspath $(COREBOOT_OUTPUT_DIR))/coreboot.defconfig"
	@$(MAKE) --no-print-directory -f "$(CDK2_ROOT)/Makefile" \
		CDK2_BUILD_DIR="$(abspath $(COREBOOT_OUTPUT_DIR))" \
		CDK2_CONFIG="$(abspath $(COREBOOT_OUTPUT_DIR))/.config" \
		CDK2_DEFCONFIG="$(abspath $(COREBOOT_OUTPUT_DIR))/coreboot.defconfig" \
		COREBOOT_CONFIG="$(abspath $(COREBOOT_CONFIG))" \
		CDK2_KCONFIG_TOOL="$(CDK2_KCONFIG_TOOL)" defconfig
	@grep -q '^CONFIG_CDK2_NATIVE_STAGE=y$$' \
		"$(abspath $(COREBOOT_OUTPUT_DIR))/.config" || { \
		printf '%s\n' 'resolved CDK2 configuration must enable CONFIG_CDK2_NATIVE_STAGE' >&2; \
		exit 1; \
	}
	@grep -q '^CONFIG_CDK2_STRICT_DIRECT_RUNTIME=y$$' \
		"$(abspath $(COREBOOT_OUTPUT_DIR))/.config" || { \
		printf '%s\n' 'coreboot payload integration requires the strict direct no-FV composition' >&2; \
		exit 1; \
	}
	@$(MAKE) --no-print-directory -f "$(CDK2_ROOT)/Makefile" \
		CDK2_BUILD_DIR="$(abspath $(COREBOOT_OUTPUT_DIR))" \
		CDK2_CONFIG="$(abspath $(COREBOOT_OUTPUT_DIR))/.config" \
		CDK2_DEFCONFIG="$(abspath $(COREBOOT_OUTPUT_DIR))/coreboot.defconfig" \
		COREBOOT_CONFIG="$(abspath $(COREBOOT_CONFIG))" \
		CDK2_KCONFIG_TOOL="$(CDK2_KCONFIG_TOOL)" \
		$(if $(HOSTCC),HOSTCC="$(HOSTCC)") \
		$(if $(CC),CC="$(CC)") \
		$(if $(LD),CDK2_NATIVE_LD="$(LD)") \
		$(if $(OBJCOPY),CDK2_NATIVE_OBJCOPY="$(OBJCOPY)") \
		$(if $(NM),CDK2_NATIVE_NM="$(NM)") \
		$(if $(READELF),CDK2_NATIVE_READELF="$(READELF)") \
		native-coreboot-image
	@printf '%s\n' "$(abspath $(COREBOOT_OUTPUT_DIR))/native/cdk2-coreboot-image.elf"

retained-fv-check:
	@awk -F '\t' '\
		/^# baseline-retained=/ { split($$0, field, "="); baseline = field[2] + 0; next } \
		/^# retained=/ { split($$0, field, "="); declared = field[2] + 0; next } \
		/^#/ || /^$$/ { next } \
		NF != 3 { print "malformed retained-FV entry at line " NR > "/dev/stderr"; failed = 1; next } \
		$$2 == "" || $$3 == "" { print "empty retained-FV field at line " NR > "/dev/stderr"; failed = 1; next } \
		$$1 !~ /^(retain|native|remove)$$/ { print "invalid retained-FV status at line " NR > "/dev/stderr"; failed = 1; next } \
		{ inventoried++ } \
		seen[$$3]++ { print "duplicate retained-FV module: " $$3 > "/dev/stderr"; failed = 1 } \
		previous != "" && $$3 < previous { print "retained-FV modules are not sorted at line " NR > "/dev/stderr"; failed = 1 } \
		{ previous = $$3; if ($$1 == "retain") retained++ } \
		END { \
			if (baseline == 0) { print "missing retained-FV baseline" > "/dev/stderr"; failed = 1 } \
			if (inventoried != baseline) { print "retained-FV inventory does not match baseline" > "/dev/stderr"; failed = 1 } \
			if (retained != declared) { print "declared retained-FV count does not match inventory" > "/dev/stderr"; failed = 1 } \
			if (retained > baseline) { print "retained-FV count increased above baseline" > "/dev/stderr"; failed = 1 } \
			exit failed \
		}' migration/retained-fv.tsv
	@awk -F '\t' '\
		FNR == NR { \
			if ($$0 !~ /^#/ && $$0 != "" && $$1 == "retain") expected[$$3] = 1; \
			next; \
		} \
		/^#/ || /^$$/ { next } \
		NF != 2 { print "malformed readiness entry at line " FNR > "/dev/stderr"; failed = 1; next } \
		$$1 !~ /^(intentional-dependency|hardware-gated|subsystem-sized|unsafe-standalone)$$/ { \
			print "invalid readiness class at line " FNR > "/dev/stderr"; failed = 1 \
		} \
		seen[$$2]++ { print "duplicate readiness module: " $$2 > "/dev/stderr"; failed = 1 } \
		!($$2 in expected) { print "readiness entry is not retained: " $$2 > "/dev/stderr"; failed = 1 } \
		{ classified[$$2] = 1 } \
		END { \
			for (module in expected) \
				if (!(module in classified)) { \
					print "retained module lacks readiness class: " module > "/dev/stderr"; failed = 1 \
				} \
			exit failed \
		}' migration/retained-fv.tsv migration/retained-readiness.tsv

wave1-map-check:
	@sh util/check-wave1-map

prepare-kconfig:
	@mkdir -p "$(CDK2_KCONFIG_WORKDIR)"
	@set -euo pipefail; \
	exec 8>"$(CDK2_KCONFIG_WORKDIR)/.kconfig-prepare.lock"; flock 8; \
	temporary=$$(mktemp -d "$(CDK2_KCONFIG_WORKDIR)/prepare.XXXXXX"); \
	trap 'rm -f "$$temporary/source" "$$temporary/Kconfig" \
		"$$temporary/identity" "$$temporary/defconfig"; rmdir "$$temporary"' EXIT; \
	trap 'exit 1' HUP INT TERM; \
	: > "$$temporary/source"; \
	if test -n "$(COREBOOT_CONFIG)"; then \
		test -f "$(COREBOOT_CONFIG)" || { \
			printf '%s\n' 'COREBOOT_CONFIG does not exist: $(COREBOOT_CONFIG)' >&2; \
			exit 1; \
		}; \
		cp "$(COREBOOT_CONFIG)" "$$temporary/source"; \
		awk ' \
			function fail(message) { print message > "/dev/stderr"; failed = 1; } \
			/^CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED=y$$/ { \
				if (seen++) fail("duplicate CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED"); \
				enabled = 1; next; \
			} \
			/^CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED=n$$/ || \
			/^# CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED is not set$$/ { \
				if (seen++) fail("duplicate CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED"); \
				next; \
			} \
			/CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED/ { \
				fail("malformed CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED"); \
			} \
			END { \
				if (failed) exit 1; \
				print "config PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED"; \
				print "\tbool"; \
				print "\tdefault " (enabled ? "y" : "n"); \
				print ""; \
			}' "$$temporary/source" > "$$temporary/Kconfig"; \
	else \
		: > "$$temporary/Kconfig"; \
	fi; \
	tr -d '\r' < "$(CDK2_KCONFIG)" >> "$$temporary/Kconfig"; \
	{ printf '%s\n' '$(if $(COREBOOT_CONFIG),$(abspath $(COREBOOT_CONFIG)),standalone)'; \
		sha256sum "$$temporary/source" | cut -d ' ' -f 1; \
	} > "$$temporary/identity"; \
	tr -d '\r' < "$(CDK2_DEFCONFIG)" > "$$temporary/defconfig"; \
	if ! cmp -s "$$temporary/source" "$(CDK2_KCONFIG_WORKDIR)/coreboot-source.tmp"; then \
		mv "$$temporary/source" "$(CDK2_KCONFIG_WORKDIR)/coreboot-source.tmp"; fi; \
	if ! cmp -s "$$temporary/Kconfig" "$(CDK2_KCONFIG_INPUT)"; then \
		mv "$$temporary/Kconfig" "$(CDK2_KCONFIG_INPUT)"; fi; \
	if ! cmp -s "$$temporary/defconfig" "$(CDK2_DEFCONFIG_INPUT)"; then \
		mv "$$temporary/defconfig" "$(CDK2_DEFCONFIG_INPUT)"; fi; \
	if ! cmp -s "$$temporary/identity" "$(CDK2_COREBOOT_INPUT_IDENTITY)"; then \
		mv "$$temporary/identity" "$(CDK2_COREBOOT_INPUT_IDENTITY)"; fi

$(CDK2_COREBOOT_INPUT_IDENTITY): FORCE | prepare-kconfig
	@test -s "$@"

$(CDK2_CONFIG): $(CDK2_KCONFIG) $(CDK2_DEFCONFIG) $(CDK2_COREBOOT_INPUT_IDENTITY)
	@mkdir -p "$(CDK2_BUILD_DIR)" "$(dir $(CDK2_CONFIG))"
	@cd "$(CDK2_BUILD_DIR)" && \
		if test -s "$(CDK2_CONFIG)"; then \
			$(CDK2_KCONFIG_ENV) $(CDK2_KCONFIG_TOOL) --olddefconfig kconfig/Kconfig; \
		else \
			$(CDK2_KCONFIG_ENV) $(CDK2_KCONFIG_TOOL) --defconfig=kconfig/defconfig kconfig/Kconfig; \
		fi
	$(cdk2_validate_config)

config: $(CDK2_CONFIG) $(CDK2_CONFIG_HEADER)
	$(cdk2_validate_config)

defconfig: prepare-kconfig
	@mkdir -p "$(CDK2_BUILD_DIR)" "$(dir $(CDK2_CONFIG))"
	@cd "$(CDK2_BUILD_DIR)" && \
		$(CDK2_KCONFIG_ENV) $(CDK2_KCONFIG_TOOL) --defconfig=kconfig/defconfig kconfig/Kconfig
	$(cdk2_validate_config)
	@$(MAKE) --no-print-directory $(CDK2_RECURSIVE_ARGS) -f "$(CDK2_ROOT)/Makefile" "$(CDK2_CONFIG_HEADER)"

olddefconfig: prepare-kconfig
	@mkdir -p "$(CDK2_BUILD_DIR)" "$(dir $(CDK2_CONFIG))"
	@cd "$(CDK2_BUILD_DIR)" && \
		if test -s "$(CDK2_CONFIG)"; then \
			$(CDK2_KCONFIG_ENV) $(CDK2_KCONFIG_TOOL) --olddefconfig kconfig/Kconfig; \
		else \
			$(CDK2_KCONFIG_ENV) $(CDK2_KCONFIG_TOOL) --defconfig=kconfig/defconfig kconfig/Kconfig; \
		fi
	$(cdk2_validate_config)
	@$(MAKE) --no-print-directory $(CDK2_RECURSIVE_ARGS) -f "$(CDK2_ROOT)/Makefile" "$(CDK2_CONFIG_HEADER)"

menuconfig: prepare-kconfig
	@command -v kconfig-mconf >/dev/null || { echo 'kconfig-mconf is required for menuconfig' >&2; exit 1; }
	@mkdir -p "$(dir $(CDK2_CONFIG))"
	@cd "$(CDK2_BUILD_DIR)" && $(CDK2_KCONFIG_ENV) kconfig-mconf kconfig/Kconfig
	$(cdk2_validate_config)
	@$(MAKE) --no-print-directory $(CDK2_RECURSIVE_ARGS) -f "$(CDK2_ROOT)/Makefile" "$(CDK2_CONFIG_HEADER)"

# The selected input path can change to an older file in the same build root.
# Revalidate its contents; the comparison below preserves unchanged timestamps.
$(CDK2_CONFIG_HEADER): $(CDK2_CONFIG) $(CDK2_KCONFIG) $(CDK2_ROOT)/Makefile FORCE
	$(cdk2_validate_config)
	@mkdir -p "$(dir $@)"
	@set -e; tmp="$$(mktemp "$@.tmp.XXXXXX")"; \
		trap 'rm -f "$$tmp"' EXIT HUP INT TERM; { \
		printf '%s\n' '/* SPDX-License-Identifier: GPL-2.0-only */' '#ifndef CDK2_CONFIG_H' '#define CDK2_CONFIG_H'; \
		awk ' \
			FILENAME == ARGV[1] { \
				if ($$1 == "config" && ($$2 ~ /^CDK2_[A-Za-z0-9_]+$$/ || \
				    $$2 == "PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED")) { \
					config_name = "CONFIG_" $$2; \
					if (!(config_name in seen_config)) { \
						config_order[++config_count] = config_name; \
						seen_config[config_name] = 1; \
					} \
				} \
				next; \
			} \
			/^CONFIG_[A-Za-z0-9_]+=y$$/ { \
				config_name = $$1; \
				sub(/=y$$/, "", config_name); \
				config_value[config_name] = "1"; \
				next; \
			} \
			/^CONFIG_[A-Za-z0-9_]+=n$$/ { \
				config_name = $$1; \
				sub(/=n$$/, "", config_name); \
				config_value[config_name] = "0"; \
				next; \
			} \
			/^# CONFIG_[A-Za-z0-9_]+ is not set$$/ { \
				config_value[$$2] = "0"; \
				next; \
			} \
			/^CONFIG_[A-Za-z0-9_]+=/ { \
				config_name = $$0; \
				sub(/=.*/, "", config_name); \
				config_value[config_name] = $$0; \
				sub(/^[^=]*=/, "", config_value[config_name]); \
				next; \
			} \
			END { \
				for (order_index = 1; order_index <= config_count; order_index++) { \
					config_name = config_order[order_index]; \
					if (!(config_name in config_value)) { \
						config_value[config_name] = "0"; \
					} \
					print "#define " config_name " " config_value[config_name]; \
					printed[config_name] = 1; \
				} \
				for (config_name in config_value) { \
					if (!(config_name in printed)) { \
						print "#define " config_name " " config_value[config_name]; \
					} \
				} \
			} \
		' "$(CDK2_KCONFIG)" "$(CDK2_CONFIG)"; \
		printf '%s\n' '#endif'; \
	} > "$$tmp"; \
	if test -f "$@" && cmp -s "$$tmp" "$@"; then \
		rm -f "$$tmp"; \
	else mv "$$tmp" "$@"; fi; trap - EXIT HUP INT TERM

ifeq ($(CDK2_CONFIG_READY),1)
include $(CDK2_CONFIG)
include $(CDK2_NATIVE_DIR)/Makefile

ifeq ($(CONFIG_CDK2_PAYLOAD),y)
else
$(error CONFIG_CDK2_PAYLOAD must be enabled in $(CDK2_CONFIG))
endif

ifneq ($(filter-out "",$(strip $(CONFIG_CDK2_CAPSULE_MAIN_FW_GUID))),)
ifeq ($(shell printf '%s\n' '$(CONFIG_CDK2_CAPSULE_MAIN_FW_GUID)' | grep -Ec '^"[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}"$$'),0)
$(error CDK2_CAPSULE_MAIN_FW_GUID must be empty or canonical)
endif
endif

ifeq ($(CONFIG_CDK2_LINEAR_BOOT),y)
ifneq ($(CONFIG_CDK2_BOOT_TIMEOUT),0)
ifneq ($(CONFIG_CDK2_LINEAR_SETUP_HOTKEY),y)
$(error CDK2 linear boot requires CDK2_BOOT_TIMEOUT=0 unless CDK2_LINEAR_SETUP_HOTKEY=y)
endif
endif
endif

ifeq ($(CONFIG_CDK2_BUILD_DEBUG),y)
CDK2_TARGET := DEBUG
else
CDK2_TARGET := RELEASE
endif

build: native-stage native-coreboot-stage native-check manifest-check

build-image:
	@printf '%s\n' 'cdk2 has no native payload-image target until the retained payload modules are ported to C/Make/Kconfig' >&2
	@exit 1

check: native-check $(CDK2_REVIEW_PROFILE_GATE)

manifest: $(CDK2_MANIFEST)

$(CDK2_MANIFEST): $(CDK2_CONFIG_HEADER) FORCE
	@mkdir -p "$(dir $@)"
	@set -e; tmp="$@.tmp"; { \
		printf '%s\n' '# Native cdk2 source manifest'; \
		cd "$(CDK2_ROOT)" && \
			find src include util tests -type f \( -name '*.[chS]' -o -name '*.ld' \) -print | sort; \
	} > "$$tmp"; mv "$$tmp" "$@"

manifest-check: $(CDK2_MANIFEST)
	@while IFS= read -r path; do \
		case "$$path" in ''|\#*) continue ;; esac; \
		test -f "$(CDK2_ROOT)/$$path" || { \
			printf '%s\n' "manifest entry does not exist: $$path" >&2; \
			exit 1; \
		}; \
	done < "$(CDK2_MANIFEST)"

modules: manifest-check
	@sed '/^#/d' "$(CDK2_MANIFEST)"

print:
	$(info target=$(CDK2_TARGET))
	$(info config=$(CDK2_CONFIG))
	$(info config-header=$(CDK2_CONFIG_HEADER))
	$(info native-stage=$(CDK2_NATIVE_ELF))
	$(info native-coreboot-stage=$(CDK2_NATIVE_COREBOOT_ELF))
	$(info native-build-dir=$(CDK2_NATIVE_BUILD_DIR))
	$(info modules=$(CDK2_MANIFEST))
	$(info source-date-epoch=$(SOURCE_DATE_EPOCH))
	@true

clean:
	rm -rf "$(CDK2_BUILD_DIR)"
else
$(CDK2_CONFIG_TARGETS): $(CDK2_CONFIG) $(CDK2_CONFIG_HEADER)
	@if fd='$(CDK2_CONFIG_LOCK_FD)'; \
		test -n "$$fd" && { \
			case "$$fd" in *[!0-9]*|'') exit 1;; esac; \
			test -e "/proc/self/fd/$$fd" && \
				test "/proc/self/fd/$$fd" -ef "$(CDK2_BUILD_DIR)/.config-target.lock" && \
				flock -n "$$fd"; \
		}; then \
		$(MAKE) --no-print-directory $(CDK2_RECURSIVE_ARGS) \
			-f "$(CDK2_ROOT)/Makefile" config && \
		$(MAKE) --no-print-directory $(CDK2_RECURSIVE_ARGS) \
			-f "$(CDK2_ROOT)/Makefile" CDK2_CONFIG_READY=1 $@; \
	else \
		exec 9>"$(CDK2_BUILD_DIR)/.config-target.lock"; flock 9; \
		CDK2_CONFIG_LOCK_FD=9 $(MAKE) --no-print-directory \
			$(CDK2_RECURSIVE_ARGS) -f "$(CDK2_ROOT)/Makefile" config && \
		CDK2_CONFIG_LOCK_FD=9 $(MAKE) --no-print-directory $(CDK2_RECURSIVE_ARGS) \
			-f "$(CDK2_ROOT)/Makefile" CDK2_CONFIG_READY=1 \
			$@; \
	fi

clean:
	rm -rf "$(CDK2_BUILD_DIR)"
endif
