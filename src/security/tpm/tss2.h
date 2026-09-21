/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef TSS2_H_
#define TSS2_H_

#include <types.h>
#include <vb2_sha.h>

#include <security/tpm/tss/tcg-2.0/tss_structures.h>
#include <security/tpm/tss_errors.h>

struct tlcl2_transport {
	tpm_result_t (*sendrecv)(void *context, const uint8_t *request,
		size_t request_size, uint8_t *response, size_t *response_size);
	void *context;
};

/*
 * TPM2-specific
 *
 * Some operations don't have counterparts in standard and are directly exposed
 * here.
 *
 * Other operations are applicable to both TPM versions and have wrappers which
 * pick the implementation based on version determined during initialization via
 * tlcl_lib_init().
 */

/*
 * Define a TPM2 space. The define space command TPM command used by the tlcl
 * layer offers the ability to use custom nv attributes and policies.
 */
tpm_result_t tlcl2_define_space(uint32_t space_index, size_t space_size,
				const TPMA_NV nv_attributes,
				const uint8_t *nv_policy, size_t nv_policy_size);

/*
 * Issue TPM2_GetCapability command
 */
tpm_result_t tlcl2_get_capability(TPM_CAP capability, uint32_t property,
				  uint32_t property_count,
				  TPMS_CAPABILITY_DATA *capability_data);

/* Canonical, bounded metadata returned by TPM2_NV_ReadPublic. */
struct tlcl2_nv_public {
	uint32_t index;
	uint16_t name_alg;
	uint32_t attributes;
	uint16_t auth_policy_size;
	uint8_t auth_policy[SHA512_DIGEST_SIZE];
	uint16_t data_size;
};

/*
 * Read an existing NV index's public metadata without authorizing access to
 * its contents. The caller-visible output is cleared on every failure.
 */
tpm_result_t tlcl2_read_public(uint32_t index, struct tlcl2_nv_public *public);
tpm_result_t tlcl2_read_public_on(const struct tlcl2_transport *transport,
	uint32_t index, struct tlcl2_nv_public *public);

/* Issue TPM2_NV_SetBits command */
tpm_result_t tlcl2_set_bits(uint32_t index, uint64_t bits);

/*
 * Makes tlcl2_process_command available for on top implementations of
 * custom tpm standards like cr50
 */
void *tlcl2_process_command(TPM_CC command, void *command_body);

/* Return digest size of hash algorithm */
uint16_t tlcl2_get_hash_size_from_algo(TPMI_ALG_HASH hash_algo);

/**
 * Set Clear Control. The TPM error code is returned.
 */
tpm_result_t tlcl2_clear_control(bool disable);

/**
 * Make an NV Ram location read_only.  The TPM error code is returned.
 */
tpm_result_t tlcl2_lock_nv_write(uint32_t index);

/**
 * Disable platform hierarchy. Specific to TPM2. The TPM error code is returned.
 */
tpm_result_t tlcl2_disable_platform_hierarchy(void);

/*
 * Declarations for "private" functions which are dispatched to by tss/tss.c
 * based on TPM family.
 */

tpm_result_t tlcl2_save_state(void);
tpm_result_t tlcl2_resume(void);
tpm_result_t tlcl2_startup(void);
tpm_result_t tlcl2_self_test_full(void);
tpm_result_t tlcl2_read(uint32_t index, void *data, uint32_t length);
tpm_result_t tlcl2_read_on(const struct tlcl2_transport *transport,
	uint32_t index, void *data, uint32_t length);
/* Read an AUTHREAD index whose authValue is empty. */
tpm_result_t tlcl2_read_auth_on(const struct tlcl2_transport *transport,
	uint32_t index, void *data, uint32_t length);
tpm_result_t tlcl2_write(uint32_t index, const void *data, uint32_t length);
tpm_result_t tlcl2_assert_physical_presence(void);
tpm_result_t tlcl2_physical_presence_cmd_enable(void);
tpm_result_t tlcl2_finalize_physical_presence(void);
tpm_result_t tlcl2_force_clear(void);
tpm_result_t tlcl2_extend(int pcr_num, const uint8_t *digest_data,
			  enum vb2_hash_algorithm digest_algo);

#endif /* TSS2_H_ */
