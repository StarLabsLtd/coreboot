# SPDX-License-Identifier: LicenseRef-LICENSE-TBC

# Shared by the protected SMM owner and the payload-owned composition.
authvar_libraries := coordinator candidate authority authority_provider controlled_mode \
	set_preflight bundle certdb mode format route fv ftw store store_semantics \
	record writer view private_trust private_binding trust_store trust_anchor \
	signature_db default_store
authvar_sources := $(addprefix src/lib/payload_mm_authvar_,$(addsuffix .c,$(authvar_libraries))) \
	src/lib/authvar_media.c src/lib/authvar_owner.c src/lib/authvar_policy.c \
	src/lib/payload_mm_crypto/cms.c src/lib/payload_mm_crypto/crypto.c \
	src/lib/payload_mm_crypto/owner.c \
	src/lib/payload_mm_crypto/mbedtls_verify_wrap.c
authvar_mbedtls := asn1parse bignum bignum_core bignum_mod bignum_mod_raw \
	constant_time md oid pk pkparse platform_util rsa rsa_alt_helpers sha256 \
	sha512 x509 x509_crt
authvar_sources += $(addprefix 3rdparty/mbedtls/library/,$(addsuffix .c,$(authvar_mbedtls)))
ifeq ($(CONFIG_CDK2_SECURE_BOOT_FIXED_KEYS),y)
authvar_sources += src/lib/authvar_fixed_policy.c
endif
