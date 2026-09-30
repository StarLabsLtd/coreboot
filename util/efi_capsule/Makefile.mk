# SPDX-License-Identifier: GPL-2.0-only

capsule_edk2_arch := X64
ifeq ($(CONFIG_EDK2_UNIVERSAL_PAYLOAD),y)
capsule_edk2_arch := IA32
endif
capsule_edk2_release := RELEASE
ifeq ($(CONFIG_EDK2_DEBUG),y)
capsule_edk2_release := DEBUG
endif
capsule_edk2_build_dir := payloads/external/edk2/workspace/Build/\
	UefiPayloadPkg$(capsule_edk2_arch)/$(capsule_edk2_release)_GCC
CAPSULE_LEGACY_FMP_DXE ?= \
	$(capsule_edk2_build_dir)/$(capsule_edk2_arch)/FmpDxe.efi
CAPSULE_LEGACY_DXE_FV ?= $(capsule_edk2_build_dir)/FV/DXEFV.Fv

finalised_rom:: $(obj)/coreboot.cap
capsule:: $(obj)/coreboot.cap

$(obj)/coreboot.cap: $(obj)/coreboot.rom $(DOTCONFIG) \
		util/efi_capsule/append_rmap.py util/efi_capsule/crypto_policy.py \
		util/efi_capsule/generate_capsule.py \
		util/efi_capsule/validate_capsule.py | files_added
	set -euf; \
		tmpdir=$$(mktemp -d "$(obj)/.capsule.XXXXXX"); \
		trap 'rm -rf "$$tmpdir"' EXIT HUP INT TERM; \
		capsule_image="$$tmpdir/coreboot.rom"; \
		region_args=; rmap_arg=; fmap_arg=; \
		if [ "$(CONFIG_DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT)" = y ]; then \
			regions='$(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_REGIONS))'; \
			test -n "$$regions"; \
			for region in $$regions; do \
				region_args="$$region_args --region $$region"; done; \
			python3 util/efi_capsule/append_rmap.py --output "$$capsule_image" \
				$$region_args "$<"; rmap_arg=--rmap; \
		elif [ "$(CONFIG_DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT)" = y ]; then \
			cp "$<" "$$capsule_image"; fmap_arg=--require-fmap; \
		else \
			echo 'No capsule update transport is selected' >&2; exit 1; \
		fi; \
		version=$$(( $(CONFIG_DRIVERS_EFI_MAIN_FW_VERSION) )); \
		if [ $$version -eq 0 ]; then \
			version=$$(printf '%s\n' \
				'$(call strip_quotes,$(CONFIG_LOCALVERSION))' | \
				awk '{ sub(/^[^0-9]*/, ""); \
				if (match($$0, /^[0-9]+\.[0-9]+/)) { \
					split(substr($$0, RSTART, RLENGTH), v, "."); \
					print v[1] * 65536 + v[2] } }'); \
			test -n "$$version"; \
		fi; \
		lsv=$$(( $(CONFIG_DRIVERS_EFI_MAIN_FW_LSV) )); \
		if [ $$lsv -eq 0 ]; then lsv=$$version; fi; \
		signer='$(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_SIGNER_PRIVATE_CERT))'; \
		intermediate='$(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_OTHER_PUBLIC_CERT))'; \
		trusted='$(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT))'; \
		if [ "$(CONFIG_DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT)" = y ]; then \
			edk2_repository='$(call strip_quotes,$(CONFIG_EDK2_REPOSITORY))'; \
			edk2_repository=$${edk2_repository%/}; \
			edk2_name=$${edk2_repository##*/}; edk2_name=$${edk2_name%.git}; \
			test -n "$$edk2_name"; \
			edk2_path="payloads/external/edk2/workspace/$$edk2_name"; \
			case "$$signer" in /*) ;; *) signer="$$edk2_path/$$signer";; esac; \
			case "$$intermediate" in ''|/*) ;; *) intermediate="$$edk2_path/$$intermediate";; esac; \
			case "$$trusted" in /*) ;; *) trusted="$$edk2_path/$$trusted";; esac; \
		fi; \
		if [ -z "$$signer" ] || [ ! -f "$$signer" ]; then \
			printf 'Capsule signer certificate is missing: %s\n' \
				"$$signer" >&2; exit 1; fi; \
		if [ -n "$$intermediate" ] && [ ! -f "$$intermediate" ]; then \
			printf 'Capsule intermediate certificate is missing: %s\n' \
				"$$intermediate" >&2; exit 1; fi; \
		if [ -z "$$trusted" ] || [ ! -f "$$trusted" ]; then \
			printf 'Capsule trust certificate is missing: %s\n' \
				"$$trusted" >&2; exit 1; fi; \
		reset_arg=; if [ "$(CONFIG_DRIVERS_EFI_CAPSULE_INITIATE_RESET)" = y ]; then \
			reset_arg=--initiate-reset; fi; \
		set --; embedded_count=0; \
		if [ "$(CONFIG_DRIVERS_EFI_CAPSULE_EMBED_FMP_DXE)" = y ]; then \
			if [ "$(CONFIG_DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT)" != y ]; then \
				echo 'Typed capsules cannot embed FMP drivers' >&2; exit 1; fi; \
			embedded_driver='$(CAPSULE_LEGACY_FMP_DXE)'; \
			test -f "$$embedded_driver"; \
			test -f '$(CAPSULE_LEGACY_DXE_FV)'; \
			set -- --embedded-driver "$$embedded_driver"; embedded_count=1; \
		fi; \
		capsule_output="$$tmpdir/coreboot.cap"; \
		python3 util/efi_capsule/generate_capsule.py --output "$$capsule_output" \
			--guid '$(call strip_quotes,$(CONFIG_DRIVERS_EFI_MAIN_FW_GUID))' \
			--fw-version $$version --lsv $$lsv $$reset_arg \
			--signer-private-cert "$$signer" \
			--other-public-cert "$$intermediate" \
			--trusted-public-cert "$$trusted" \
			"$$@" "$$capsule_image"; \
		set --; \
		if [ $$embedded_count -eq 1 ]; then \
			set -- --firmware-volume '$(CAPSULE_LEGACY_DXE_FV)'; fi; \
		python3 util/efi_capsule/validate_capsule.py --capsule "$$capsule_output" \
			--guid '$(call strip_quotes,$(CONFIG_DRIVERS_EFI_MAIN_FW_GUID))' \
			--embedded-drivers $$embedded_count --fw-version $$version --lsv $$lsv \
			--image "$$capsule_image" "$$@" $$region_args $$rmap_arg $$fmap_arg \
			$$reset_arg \
			--trusted-public-cert "$$trusted"; \
		mv "$$capsule_output" "$@"
