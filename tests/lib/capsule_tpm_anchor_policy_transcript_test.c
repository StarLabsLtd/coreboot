/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <security/tpm/capsule_anchor_policy.h>
#include <security/tpm/tss.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int fflush(void *stream);

#define CHECK(condition) do { \
	if (!(condition)) { \
		__builtin_printf("check failed at %s:%d: %s\n", __FILE__, __LINE__, \
			#condition); \
		fflush(NULL); \
		abort(); \
	} \
} while (0)

#define SESSION_HANDLE 0x03000007U
#define OBJECT_HANDLE 0x80000005U
#define TRANSCRIPT_BUFFER_SIZE 640U

enum transcript_kind {
	TRANSCRIPT_READ,
	TRANSCRIPT_WRITE,
	TRANSCRIPT_LOCK,
};

enum transcript_fault {
	FAULT_NONE,
	FAULT_TERMINAL_ERROR,
	FAULT_MALFORMED_REPLY,
	FAULT_TRANSPORT_LOSS,
	FAULT_OBJECT_FLUSH,
	FAULT_SESSION_FLUSH,
};

struct bytes {
	uint8_t data[TRANSCRIPT_BUFFER_SIZE];
	size_t size;
};

struct transcript {
	enum transcript_kind kind;
	enum transcript_fault fault;
	size_t step;
};

static struct capsule_tpm_anchor_binding binding;
static struct capsule_tpm_anchor_authorization authorization;

tis_sendrecv_fn tlcl_tis_sendrecv;

/*
 * The RSA fixture is vboot's tests/testkeys/key_rsa2048.pem at vboot commit
 * 5c360ef458b0a013d8a6d47724bb0fffb5accbcf.  Its SHA-256 is
 * 5a4f2f60a80db767878bca5fd4d89947560ce59f4f7d153a8120b9f75cf93331.
 * The signatures below are valid PKCS#1 v1.5 SHA-256 signatures over each
 * approvedPolicy concatenated with the eight-byte policyRef.
 */

int printk(int level, const char *format, ...)
{
	(void)level;
	(void)format;
	return 0;
}

vb2_error_t vb2_hash_calculate(bool allow_hwcrypto, const void *data,
	uint32_t size, enum vb2_hash_algorithm algorithm, struct vb2_hash *hash)
{
	struct vb2_sha256_context context;

	(void)allow_hwcrypto;
	CHECK(algorithm == VB2_HASH_SHA256);
	hash->algo = algorithm;
	vb2_sha256_init(&context, algorithm);
	vb2_sha256_update(&context, data, size);
	vb2_sha256_finalize(&context, hash->raw, algorithm);
	return VB2_SUCCESS;
}

size_t vb2_digest_size(enum vb2_hash_algorithm algorithm)
{
	return algorithm == VB2_HASH_SHA256 ? VB2_SHA256_DIGEST_SIZE : 0;
}

static uint8_t hex_nibble(char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	CHECK(false);
	return 0;
}

static void append(struct bytes *bytes, const void *data, size_t size)
{
	CHECK(data || !size);
	CHECK(size <= sizeof(bytes->data) - bytes->size);
	memcpy(bytes->data + bytes->size, data, size);
	bytes->size += size;
}

static void append_hex(struct bytes *bytes, const char *hex)
{
	while (*hex) {
		const uint8_t value = (hex_nibble(hex[0]) << 4) |
			hex_nibble(hex[1]);

		append(bytes, &value, sizeof(value));
		hex += 2;
	}
}

static void append_repeat(struct bytes *bytes, uint8_t value, size_t count)
{
	while (count--)
		append(bytes, &value, sizeof(value));
}

static void append_be32(struct bytes *bytes, uint32_t value)
{
	const uint8_t encoded[] = {
		value >> 24, value >> 16, value >> 8, value,
	};

	append(bytes, encoded, sizeof(encoded));
}

static void append_le64(struct bytes *bytes, uint64_t value)
{
	for (size_t i = 0; i < sizeof(value); i++) {
		const uint8_t encoded = value >> (8 * i);

		append(bytes, &encoded, sizeof(encoded));
	}
}

static struct bytes from_hex(const char *hex)
{
	struct bytes bytes = { 0 };

	append_hex(&bytes, hex);
	return bytes;
}

static void finish_command(struct bytes *bytes)
{
	CHECK(bytes->size <= UINT32_MAX);
	bytes->data[2] = bytes->size >> 24;
	bytes->data[3] = bytes->size >> 16;
	bytes->data[4] = bytes->size >> 8;
	bytes->data[5] = bytes->size;
}

static struct bytes public_request(void)
{
	return from_hex("80010000000e0000016901150020");
}

static struct bytes read_request(void)
{
	return from_hex("8002000000230000014e011500200115002000000009"
		"40000009000000000000280000");
}

static struct bytes start_request(bool write)
{
	struct bytes bytes = from_hex("8001000000000000017640000007400000070020");

	append_repeat(&bytes, write ? 0x87 : 0xba, 32);
	append_hex(&bytes, "0000010010000b");
	finish_command(&bytes);
	CHECK(bytes.size == 59);
	return bytes;
}

static struct bytes written_request(void)
{
	return from_hex("80010000000f0000018f0300000701");
}

static struct bytes policy_nv_request(bool write)
{
	struct bytes bytes = from_hex("80020000000000000149011500200115002003000007"
		"000000094000000900000000000028");

	append_le64(&bytes, write ? 10 : 11);
	append_repeat(&bytes, write ? 0x43 : 0x54, 32);
	append_hex(&bytes, "00000000");
	finish_command(&bytes);
	CHECK(bytes.size == 81);
	return bytes;
}

static struct bytes cp_hash_request(bool write)
{
	struct bytes bytes = from_hex("8001000000300000016e030000070020");

	append_hex(&bytes, write ?
		"e53833b8b9ce9024ccc9e320467c34311512e5c8fdad6562c4180650d02df747" :
		"5ae3be8ed8906a290fe962e8fd55c011d1db304ffcdcfddb38ee5c0200cd3c43");
	return bytes;
}

static void append_modulus(struct bytes *bytes)
{
	append_hex(bytes,
		"945dca159a3d916d9861a5094dc7b5d41604abd9d3b0ff04d3e7be4f0e49509d"
		"4ce39b3c5094b33a02907922d1df7f7d4d2ab4592aac49af1016f0400afecb98"
		"cec2824808e26323d9997de97255c993f6fb673a860bf046e077786e89e4adac"
		"5b9630888cfee5241c3e8969e45eabd7bb4c673822760231acddf656f519b7956"
		"99482d353887b27d64d6a2a681b725d165a31a8ea8e75ae4b92c7fc0a272c84"
		"626a4a2aa0a6f771db354d5fd72328da6acc4bd9acd28f2ca0ed785ce884faab"
		"33b9f683e9ca8006f058b6bdcc42c2446f21d6a7db2ce5ce8eb815832fe0f2d"
		"e62c563ab441adb6399d00afede78a11eb47f2bbb764c36d3a0796cceba9d10d5");
}

static struct bytes load_request(void)
{
	struct bytes bytes = from_hex("8001000000000000016700000118"
		"0001000b00040400000000100014000b0800000000000100");

	append_modulus(&bytes);
	append_hex(&bytes, "40000001");
	finish_command(&bytes);
	CHECK(bytes.size == 298);
	return bytes;
}

static void append_signature(struct bytes *bytes, bool write)
{
	append_hex(bytes, write ?
		"41dd4d855aab84e8e708fd0c36cb72c72a9f01a12687a13898c705efd41db828"
		"c1cc6ad1ea60090c2b34b9d14fc81222cb02e229c4768fb73a7e65f0d5eb886e"
		"cb5469d3d95f1cb58476021bccd63a3827b37003d3ce68fe9927ceca3f45fc4e"
		"25cd1473b468507d9f1ff90fc1e272cd4d17af88e0052064b6b8d85235729436b"
		"2cb6e05f55fa3528137452111c47f7a94cc10593ad0c248d94ed73d2fc64ed2d"
		"0e66293ef90d319e1ad72fe771dcf64a7b931f3617e90a758f31ff8c9c597b6d"
		"7f327af48436ba99358d76d894cfe8898f2ed99fec01530311175ea197087444d"
		"3b34d0e7345bcd7b28d78287637fd3d23afb0db6e2d3092c66c3cd5ad7e849" :
		"41cad6db64bb39fd7394a0dd864414e117b823fea9628e3348ddc3ec80c188857"
		"a97c2c2afd15c6a0054c6fb84d99e26e75c5339d219d855e096a8c54fb861001"
		"eb8ddf51dd80e2d22d856cdc5f65ba9b3d522fd453718adc677410a2ae84fa6d"
		"c05f47ac0e9938fa93ccccd775f7780180ae00e2df8e829c12e8451ed6d52d2c"
		"ac2ba05870dccfb86d1623207d93e1277b38f70c6b4ecfe8a287d61497ad39ee"
		"0458bb90d4a2af7ca7dc1c96bf86962d936d83609f97cf5b8253cc5030a25e5f"
		"e885870aa29cb35c6a99feaf3519239427c070b913f9168e473a475996b64dd38"
		"c70ba176ddc4d7622e82bd08f80b9b44f3abecf93db51b7cd39f5a4e5de4ff");
}

static struct bytes verify_request(bool write)
{
	struct bytes bytes = from_hex("80010000000000000177800000050020");

	append_hex(&bytes, write ?
		"a2aa3a06c3fac7b2641f759a0513006e9bec9cdac8ff381d357d2c80212b2609" :
		"0489d1f57b08257022091f308ab2f137ff44b7f35a2fafdde0c6164fdd94ed97");
	append_hex(&bytes, "0014000b0100");
	append_signature(&bytes, write);
	finish_command(&bytes);
	CHECK(bytes.size == 310);
	return bytes;
}

static struct bytes authorize_request(bool write)
{
	struct bytes bytes = from_hex("8001000000000000016a030000070020");

	append_hex(&bytes, write ?
		"42716e0d9e48875861eb9000e04a69a1e96d63149a8e0ae3813e0199136626a2" :
		"582e4e4bf128bd322a7b6249e0069663d28379e2900e3d122a152b9dc0baa286");
	append_hex(&bytes, "000832323232323232320022"
		"000bcbf25b8723ed0d16e66260271b6ab25e9dabebaae43846bccb7768d889f410ce"
		"8022400000010020");
	for (uint8_t value = 0xc0; value <= 0xdf; value++)
		append(&bytes, &value, sizeof(value));
	finish_command(&bytes);
	CHECK(bytes.size == 134);
	return bytes;
}

static struct bytes command_code_request(bool write)
{
	return from_hex(write ?
		"8001000000120000016c0300000700000137" :
		"8001000000120000016c0300000700000138");
}

static struct bytes or_request(void)
{
	return from_hex("8001000000560000017103000007000000020020"
		"38600bf71f7d19dd23ac910d73f93a3a1484c45c3f7aecc78c41ff581c8e4d83"
		"002059a98dddce5f179bbc9d55dc6b58c213c965b0f7d23793b6948b01248cccf54d");
}

static struct bytes write_request(void)
{
	struct bytes bytes = from_hex("800200000000000001370115002001150020"
		"000000090300000700000100000028");

	append_le64(&bytes, 11);
	append_repeat(&bytes, 0x54, 32);
	append_hex(&bytes, "0000");
	finish_command(&bytes);
	CHECK(bytes.size == 75);
	return bytes;
}

static struct bytes lock_request(void)
{
	return from_hex("80020000001f000001380115002001150020"
		"00000009030000070000010000");
}

static struct bytes flush_request(bool object)
{
	struct bytes bytes = from_hex("80010000000e00000165");

	append_be32(&bytes, object ? OBJECT_HANDLE : SESSION_HANDLE);
	return bytes;
}

static struct bytes public_response(void)
{
	struct bytes bytes = from_hex("80010000000000000000002e01150020000b62045008"
		"0020707b1f7fecc6b45fcde3d93252e71495be1f8265968d14bf2d966587735375ee"
		"00280022"
		"000b34fd5e8d6681a6a09a6498953612874b787b27cbf07fa7356db351addfd59c6f");

	bytes.data[2] = bytes.size >> 24;
	bytes.data[3] = bytes.size >> 16;
	bytes.data[4] = bytes.size >> 8;
	bytes.data[5] = bytes.size;
	CHECK(bytes.size == 94);
	return bytes;
}

static struct bytes read_response(void)
{
	struct bytes bytes = from_hex("800200000000000000000000002a0028");

	append_le64(&bytes, 10);
	append_repeat(&bytes, 0x43, 32);
	append_hex(&bytes, "0000000000");
	bytes.data[2] = bytes.size >> 24;
	bytes.data[3] = bytes.size >> 16;
	bytes.data[4] = bytes.size >> 8;
	bytes.data[5] = bytes.size;
	CHECK(bytes.size == 61);
	return bytes;
}

static struct bytes empty_response(bool sessions)
{
	return from_hex(sessions ?
		"80020000001300000000000000000000000000" :
		"80010000000a00000000");
}

static struct bytes start_response(void)
{
	struct bytes bytes = from_hex("80010000003000000000030000070020");

	for (uint8_t value = 0xa0; value <= 0xbf; value++)
		append(&bytes, &value, sizeof(value));
	return bytes;
}

static struct bytes load_response(void)
{
	return from_hex("80010000003200000000800000050022"
		"000bcbf25b8723ed0d16e66260271b6ab25e9dabebaae43846bccb7768d889f410ce");
}

static struct bytes verify_response(void)
{
	struct bytes bytes = from_hex("800100000032000000008022400000010020");

	for (uint8_t value = 0xc0; value <= 0xdf; value++)
		append(&bytes, &value, sizeof(value));
	return bytes;
}

static struct bytes write_response(void)
{
	struct bytes bytes = from_hex("80020000003300000000000000000020");

	for (uint8_t value = 0xd0; value <= 0xef; value++)
		append(&bytes, &value, sizeof(value));
	append_hex(&bytes, "010000");
	return bytes;
}

static struct bytes expected_request(enum transcript_kind kind, size_t step)
{
	const bool write = kind == TRANSCRIPT_WRITE;

	if (kind == TRANSCRIPT_READ)
		return step == 0 ? public_request() : read_request();
	switch (step) {
	case 0:
		return public_request();
	case 1:
		return start_request(write);
	case 2:
		return written_request();
	case 3:
		return policy_nv_request(write);
	case 4:
		return cp_hash_request(write);
	case 5:
		return load_request();
	case 6:
		return verify_request(write);
	case 7:
		return authorize_request(write);
	case 8:
		return command_code_request(write);
	case 9:
		return or_request();
	case 10:
		return write ? write_request() : lock_request();
	case 11:
		return flush_request(true);
	case 12:
		return flush_request(false);
	default:
		CHECK(false);
		return (struct bytes) { 0 };
	}
}

static struct bytes expected_response(enum transcript_kind kind, size_t step)
{
	if (kind == TRANSCRIPT_READ)
		return step == 0 ? public_response() : read_response();
	switch (step) {
	case 0:
		return public_response();
	case 1:
		return start_response();
	case 2:
		return empty_response(false);
	case 3:
		return empty_response(true);
	case 4:
		return empty_response(false);
	case 5:
		return load_response();
	case 6:
		return verify_response();
	case 7:
	case 8:
	case 9:
		return empty_response(false);
	case 10:
		return write_response();
	case 11:
	case 12:
		return empty_response(false);
	default:
		CHECK(false);
		return (struct bytes) { 0 };
	}
}

static enum cb_err transmit(void *opaque, const uint8_t *request,
	size_t request_size, uint8_t *response, size_t *response_size)
{
	struct transcript *transcript = opaque;
	const struct bytes expected = expected_request(transcript->kind,
		transcript->step);
	struct bytes reply = expected_response(transcript->kind,
		transcript->step);

	CHECK(request_size == expected.size);
	if (memcmp(request, expected.data, expected.size)) {
		for (size_t i = 0; i < expected.size; i++)
			if (request[i] != expected.data[i]) {
				__builtin_printf("transcript %u step %zu byte %zu: %02x != %02x\n",
					transcript->kind, transcript->step, i,
					request[i], expected.data[i]);
				fflush(NULL);
				break;
			}
		CHECK(false);
	}
	if (transcript->fault == FAULT_TRANSPORT_LOSS &&
	    transcript->step == 10) {
		transcript->step++;
		return CB_ERR;
	}
	if ((transcript->fault == FAULT_TERMINAL_ERROR &&
	     transcript->step == 10) ||
	    (transcript->fault == FAULT_OBJECT_FLUSH &&
	     transcript->step == 11) ||
	    (transcript->fault == FAULT_SESSION_FLUSH &&
	     transcript->step == 12))
		reply = from_hex("80010000000a00000101");
	if (transcript->fault == FAULT_MALFORMED_REPLY &&
	    transcript->step == 10)
		reply.data[5] ^= 1;
	CHECK(*response_size >= reply.size);
	memcpy(response, reply.data, reply.size);
	*response_size = reply.size;
	transcript->step++;
	return CB_SUCCESS;
}

static enum cb_err prepared_ok(const void *context,
	const struct capsule_tpm_anchor_grant *grant)
{
	(void)context;
	CHECK(grant != NULL);
	return CB_SUCCESS;
}

static enum cb_err install_ok(const void *context,
	const struct capsule_tpm_anchor_grant *grant,
	const struct capsule_tpm_anchor_binding *installed_binding)
{
	(void)context;
	CHECK(grant != NULL);
	CHECK(installed_binding != NULL);
	return CB_SUCCESS;
}

static void decode(void *output, size_t size, const char *hex)
{
	const struct bytes bytes = from_hex(hex);

	CHECK(bytes.size == size);
	memcpy(output, bytes.data, size);
}

static void initialize_inputs(void)
{
	memset(&binding, 0, sizeof(binding));
	binding.policy_revision = CAPSULE_TPM_ANCHOR_POLICY_REVISION;
	binding.nv_index = HR_NV_INDEX | 0x150020;
	decode(binding.authority_name, sizeof(binding.authority_name),
		"000bcbf25b8723ed0d16e66260271b6ab25e9dabebaae43846bccb7768d889f410ce");
	binding.policy_ref_size = 8;
	memset(binding.policy_ref, 0x32, binding.policy_ref_size);

	memset(&authorization, 0, sizeof(authorization));
	authorization.revision = CAPSULE_TPM_ANCHOR_AUTHORIZATION_REVISION;
	authorization.size = sizeof(authorization);
	authorization.policy_revision = binding.policy_revision;
	authorization.nv_index = binding.nv_index;
	authorization.generation = 7;
	authorization.transaction = 9;
	authorization.current.epoch = 10;
	memset(authorization.current.digest, 0x43,
		sizeof(authorization.current.digest));
	authorization.candidate.epoch = 11;
	memset(authorization.candidate.digest, 0x54,
		sizeof(authorization.candidate.digest));
	memcpy(authorization.authority_name, binding.authority_name,
		sizeof(binding.authority_name));
	authorization.policy_ref_size = binding.policy_ref_size;
	memcpy(authorization.policy_ref, binding.policy_ref,
		sizeof(binding.policy_ref));
	authorization.modulus_size = 256;
	{
		struct bytes modulus = { 0 };

		append_modulus(&modulus);
		memcpy(authorization.modulus, modulus.data, modulus.size);
	}
	decode(authorization.write.approved_policy,
		sizeof(authorization.write.approved_policy),
		"42716e0d9e48875861eb9000e04a69a1e96d63149a8e0ae3813e0199136626a2");
	decode(authorization.write.cp_hash, sizeof(authorization.write.cp_hash),
		"e53833b8b9ce9024ccc9e320467c34311512e5c8fdad6562c4180650d02df747");
	memset(authorization.write.nonce, 0x87,
		sizeof(authorization.write.nonce));
	authorization.write.signature_size = 256;
	{
		struct bytes signature = { 0 };

		append_signature(&signature, true);
		memcpy(authorization.write.signature, signature.data,
			signature.size);
	}
	decode(authorization.lock.approved_policy,
		sizeof(authorization.lock.approved_policy),
		"582e4e4bf128bd322a7b6249e0069663d28379e2900e3d122a152b9dc0baa286");
	decode(authorization.lock.cp_hash, sizeof(authorization.lock.cp_hash),
		"5ae3be8ed8906a290fe962e8fd55c011d1db304ffcdcfddb38ee5c0200cd3c43");
	memset(authorization.lock.nonce, 0xba,
		sizeof(authorization.lock.nonce));
	authorization.lock.signature_size = 256;
	{
		struct bytes signature = { 0 };

		append_signature(&signature, false);
		memcpy(authorization.lock.signature, signature.data,
			signature.size);
	}
}

static void initialize_provider(
	struct capsule_tpm_anchor_policy_provider *state,
	struct capsule_tpm_anchor_transition_provider *provider)
{
	const struct capsule_tpm_anchor_policy_callbacks callbacks = {
		.prepared = prepared_ok,
		.install = install_ok,
	};
	const struct capsule_tpm_anchor_grant grant = { 0 };

	memset(state, 0, sizeof(*state));
	CHECK(capsule_tpm_anchor_policy_provider_init(state, &callbacks,
		provider) == CB_SUCCESS);
	CHECK(provider->prepared(provider->context, &grant) == CB_SUCCESS);
}

static void test_read_transcript(void)
{
	struct capsule_tpm_anchor_policy_provider state;
	struct capsule_tpm_anchor_transition_provider provider;
	struct capsule_tpm_anchor_binding observed;
	struct capsule_tpm_anchor_value value;
	struct transcript transcript = { .kind = TRANSCRIPT_READ };

	initialize_provider(&state, &provider);
	CHECK(provider.read(provider.context, transmit, &transcript, &binding,
		&observed, &value) == CB_SUCCESS);
	CHECK(transcript.step == 2);
	CHECK(!memcmp(&observed, &binding, sizeof(observed)));
	CHECK(!memcmp(&value, &authorization.current, sizeof(value)));
}

static void test_operation_transcript(enum transcript_kind kind)
{
	struct capsule_tpm_anchor_policy_provider state;
	struct capsule_tpm_anchor_transition_provider provider;
	struct transcript transcript = { .kind = kind };

	initialize_provider(&state, &provider);
	if (kind == TRANSCRIPT_WRITE)
		CHECK(provider.write(provider.context, transmit, &transcript,
			&binding, &authorization) == CB_SUCCESS);
	else
		CHECK(provider.lock(provider.context, transmit, &transcript,
			&binding, &authorization) == CB_SUCCESS);
	CHECK(transcript.step == 13);
}

static bool provider_can_read(
	struct capsule_tpm_anchor_transition_provider *provider)
{
	struct capsule_tpm_anchor_binding observed;
	struct capsule_tpm_anchor_value value;
	struct transcript transcript = { .kind = TRANSCRIPT_READ };

	return provider->read(provider->context, transmit, &transcript, &binding,
		&observed, &value) == CB_SUCCESS && transcript.step == 2;
}

static void test_terminal_error_is_advisory(void)
{
	struct capsule_tpm_anchor_policy_provider state;
	struct capsule_tpm_anchor_transition_provider provider;
	struct transcript transcript = {
		.kind = TRANSCRIPT_WRITE,
		.fault = FAULT_TERMINAL_ERROR,
	};

	initialize_provider(&state, &provider);
	CHECK(provider.write(provider.context, transmit, &transcript, &binding,
		&authorization) == CB_ERR);
	CHECK(transcript.step == 13);
	CHECK(provider_can_read(&provider));
}

static void test_terminal_fault(enum transcript_fault fault,
	size_t expected_steps)
{
	struct capsule_tpm_anchor_policy_provider state;
	struct capsule_tpm_anchor_transition_provider provider;
	struct transcript transcript = {
		.kind = TRANSCRIPT_WRITE,
		.fault = fault,
	};

	initialize_provider(&state, &provider);
	CHECK(provider.write(provider.context, transmit, &transcript, &binding,
		&authorization) == CB_ERR);
	CHECK(transcript.step == expected_steps);
	CHECK(!provider_can_read(&provider));
}

int main(void)
{
	initialize_inputs();
	test_read_transcript();
	test_operation_transcript(TRANSCRIPT_WRITE);
	test_operation_transcript(TRANSCRIPT_LOCK);
	test_terminal_error_is_advisory();
	test_terminal_fault(FAULT_MALFORMED_REPLY, 13);
	test_terminal_fault(FAULT_TRANSPORT_LOSS, 13);
	test_terminal_fault(FAULT_OBJECT_FLUSH, 13);
	test_terminal_fault(FAULT_SESSION_FLUSH, 13);
	return 0;
}
