## SPDX-License-Identifier: GPL-2.0-only

all-$(CONFIG_SMMSTORE) += store.c

ramstage-$(CONFIG_SMMSTORE) += ramstage.c communication.c

smm-$(CONFIG_SMMSTORE) += store.c smi.c

smm_crypto_sources :=
smm_crypto_flags :=

ifeq ($(CONFIG_SMM_AUTHVAR),y)
include $(src)/lib/authvar_sources.mk
smm-y += authvar.c authvar_dispatch.c authvar_endpoint.c
ramstage-y += authvar_ramstage.c authvar_endpoint.c
smm-srcs += src/lib/payload_mm_authvar_service.c \
	src/lib/authvar_image_policy.c
ramstage-srcs += src/lib/payload_mm_authvar_service.c src/lib/authvar_image_policy.c
smm_crypto_sources += $(authvar_sources)
smm_crypto_flags += -DPAYLOAD_MM_AUTHVAR_CRYPTO
$(call src-to-obj,smm,3rdparty/mbedtls/library/x509_crt.c): CFLAGS_smm += \
	-DMBEDTLS_TEST_SW_INET_PTON
$(obj)/smm/smm.elf: LDFLAGS_rmodules_x86_32 += --wrap=mbedtls_rsa_parse_pubkey
$(obj)/smm/smm.elf: LDFLAGS_rmodules_x86_64 += --wrap=mbedtls_rsa_parse_pubkey
endif

ifeq ($(CONFIG_DAVE_MODE)$(CONFIG_TCG_OPAL_S3_UNLOCK),yy)
ifneq ($(CONFIG_TPM2)$(CONFIG_CRB_TPM)$(CONFIG_HAVE_INTEL_PTT),yyy)
$(error DAVE S3 admission requires the bounded Intel PTT CRB transport)
endif
smm_crypto_sources += src/lib/payload_mm_crypto/owner.c \
	src/security/tpm/dave/session.c src/security/tpm/dave/resume.c
dave_smm_mbedtls := aes bignum bignum_core bignum_mod bignum_mod_raw \
	constant_time ecdh ecp ecp_curves hmac_drbg md platform_util sha256
smm_crypto_sources += $(addprefix 3rdparty/mbedtls/library/,$(addsuffix .c,$(dave_smm_mbedtls)))
smm_crypto_flags += -DDAVE_TPM_SMM_CRYPTO
endif

ifneq ($(strip $(smm_crypto_sources)),)
smm-srcs += $(sort $(smm_crypto_sources))
smm_crypto_objects := $(foreach source,$(sort $(smm_crypto_sources)),$(call src-to-obj,smm,$(source)))
$(smm_crypto_objects): .EXTRA_PREREQS += $(src)/drivers/smmstore/Makefile.mk \
	$(src)/lib/authvar_sources.mk
$(obj)/smm/smm.elf: .EXTRA_PREREQS += $(src)/drivers/smmstore/Makefile.mk \
	$(src)/lib/authvar_sources.mk
$(smm_crypto_objects): CFLAGS_smm += -Wno-redundant-decls $(smm_crypto_flags) \
	-I$(src)/lib -I$(src)/lib/payload_mm_crypto \
	-I$(src)/../3rdparty/mbedtls/include -I$(src)/../3rdparty/mbedtls/library \
	-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"'
endif

ifeq ($(CONFIG_DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT),y)
smm-y += capsule.c capsule_image.c capsule_policy.c capsule_layout.c
smm-srcs += src/lib/uuid.c src/lib/hexstrtobin.c
ramstage-y += capsule_ramstage.c capsule_layout.c
$(call src-to-obj,smm,src/drivers/smmstore/capsule_image.c): CFLAGS_smm += \
	$(smm_crypto_flags) \
	-I$(src)/../3rdparty/mbedtls/include -I$(src)/lib/payload_mm_crypto \
	-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"'
endif
