## SPDX-License-Identifier: GPL-2.0-only

all-$(CONFIG_SMMSTORE) += store.c

ramstage-$(CONFIG_SMMSTORE) += ramstage.c

smm-$(CONFIG_SMMSTORE) += store.c smi.c

ifeq ($(CONFIG_SMM_AUTHVAR),y)
include $(src)/lib/authvar_sources.mk
smm-y += authvar.c authvar_dispatch.c authvar_endpoint.c
ramstage-y += authvar_ramstage.c authvar_endpoint.c
smm-srcs += $(authvar_sources) src/lib/payload_mm_authvar_service.c \
	src/lib/authvar_image_policy.c
ramstage-srcs += src/lib/payload_mm_authvar_service.c src/lib/authvar_image_policy.c

authvar_crypto_objects := $(foreach source,$(authvar_sources),$(call src-to-obj,smm,$(source)))
$(authvar_crypto_objects): .EXTRA_PREREQS += $(src)/drivers/smmstore/Makefile.mk \
	$(src)/lib/authvar_sources.mk
$(obj)/smm/smm.elf: .EXTRA_PREREQS += $(src)/drivers/smmstore/Makefile.mk \
	$(src)/lib/authvar_sources.mk
$(authvar_crypto_objects): CFLAGS_smm += -Wno-redundant-decls \
	-I$(src)/lib -I$(src)/lib/payload_mm_crypto \
	-I$(src)/../3rdparty/mbedtls/include -I$(src)/../3rdparty/mbedtls/library \
	-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"'
$(call src-to-obj,smm,3rdparty/mbedtls/library/x509_crt.c): CFLAGS_smm += \
	-DMBEDTLS_TEST_SW_INET_PTON
$(obj)/smm/smm.elf: LDFLAGS_rmodules_x86_32 += --wrap=mbedtls_rsa_parse_pubkey
$(obj)/smm/smm.elf: LDFLAGS_rmodules_x86_64 += --wrap=mbedtls_rsa_parse_pubkey
endif

ifeq ($(CONFIG_DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT),y)
smm-y += capsule.c capsule_image.c capsule_policy.c capsule_layout.c
smm-srcs += src/lib/uuid.c
ramstage-y += capsule_ramstage.c capsule_layout.c
$(call src-to-obj,smm,src/drivers/smmstore/capsule_image.c): CFLAGS_smm += \
	-I$(src)/../3rdparty/mbedtls/include -I$(src)/lib/payload_mm_crypto \
	-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"'
endif
