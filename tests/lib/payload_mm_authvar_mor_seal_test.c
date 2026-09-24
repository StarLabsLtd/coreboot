/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_seal.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

static struct payload_mm_authvar_mor_seal_request transport;
static struct payload_mm_authvar_mor_seal_channel *source_channel;
static struct payload_mm_authvar_mor_grant *source_grant;
static uint64_t trigger_caller = 0x1122334455667788ULL;
static uint64_t trigger_context = 0x8877665544332211ULL;
static const struct payload_mm_authvar_mor_seal_request *trigger_transport =
	&transport;
static size_t trigger_size = sizeof(transport);
static bool protected_ok = true;
static bool shared_ok = true;
static bool mutate_channel;
static bool mutate_grant;
static bool rewrite_transport;
static bool trigger_fail;
static unsigned int triggers;
static uint8_t decoy_transport[sizeof(transport)];
static bool late_redirect;
static bool late_callback_mutation;
static bool install_callback_mutation;
static struct {
	struct payload_mm_authvar_mor_seal_channel channel;
	struct payload_mm_authvar_mor_seal_request candidate;
	payload_mm_authvar_mor_seal_range_check protected_storage;
	payload_mm_authvar_mor_seal_range_check fixed_transport;
	bool channel_attempted;
	bool channel_installed;
	bool request_attempted;
	bool poisoned;
} *private_authority;

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return !combined;
}

static struct payload_mm_authvar_mor_grant valid_grant(void)
{
	struct payload_mm_authvar_mor_grant grant = {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_GRANT_REVISION,
		.size = sizeof(grant),
		.cold_boot_generation = 7,
		.entry = { .present = 1, .value = 1 },
		.flags = PAYLOAD_MM_AUTHVAR_MOR_GRANT_REQUIRED_FLAGS,
		.dma_policy_generation = 9,
		.inventory_generation = 12,
		.total_bytes = 0x3000,
		.cleared_bytes = 0x2000,
		.excluded_bytes = 0x1000,
		.total_spans = 2,
		.cleared_spans = 1,
		.excluded_spans = 1,
		.spans = {
			{ .base = 0x1000, .size = 0x2000,
			  .span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED },
			{ .base = 0x4000, .size = 0x1000,
			  .span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
			  .exclusion_reason =
				PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE },
		},
	};

	for (size_t index = 0; index < 32; index++) {
		grant.dma_policy_identity[index] = index + 1U;
		grant.inventory_identity[index] = 0x80U + index;
	}
	return grant;
}

static struct payload_mm_authvar_mor_seal_channel valid_channel(void)
{
	struct payload_mm_authvar_mor_seal_channel channel = {
		.transport_base = (uintptr_t)&transport,
		.transport_size = sizeof(transport),
		.caller = 0x1122334455667788ULL,
		.caller_context = 0x8877665544332211ULL,
	};

	for (size_t index = 0; index < sizeof(channel.capability); index++)
		channel.capability[index] = 0x40U + index;
	return channel;
}

static bool protected_range(const void *base, size_t size)
{
	CHECK(base && size);
	if (!private_authority &&
	    size > sizeof(struct payload_mm_authvar_mor_seal_request))
		private_authority = (void *)base;
	else if (late_redirect && private_authority)
		private_authority->channel.transport_base = (uintptr_t)decoy_transport;
	else if (late_callback_mutation && private_authority)
		private_authority->fixed_transport = NULL;
	if (mutate_channel && source_channel) {
		source_channel->caller++;
		mutate_channel = false;
	}
	if (mutate_grant && source_grant) {
		source_grant->cold_boot_generation++;
		mutate_grant = false;
	}
	return protected_ok;
}

static bool shared_range(const void *base, size_t size)
{
	CHECK(base == &transport && size == sizeof(transport));
	if (install_callback_mutation && private_authority)
		private_authority->protected_storage = NULL;
	return shared_ok;
}

static bool private_secrets_zero(void)
{
	CHECK(private_authority);
	return bytes_zero(&private_authority->channel,
			sizeof(private_authority->channel)) &&
		bytes_zero(&private_authority->candidate,
			sizeof(private_authority->candidate)) &&
		!private_authority->protected_storage &&
		!private_authority->fixed_transport;
}

static enum cb_err trigger(void *unused)
{
	enum cb_err status;

	(void)unused;
	triggers++;
	status = payload_mm_authvar_mor_seal_receive(trigger_transport,
		trigger_size, trigger_caller, trigger_context);
	if (rewrite_transport)
		memset(&transport, 0xa5, sizeof(transport));
	if (mutate_channel && source_channel)
		source_channel->caller++;
	if (mutate_grant && source_grant)
		source_grant->cold_boot_generation++;
	return trigger_fail ? CB_ERR : status;
}

static void install_channel(struct payload_mm_authvar_mor_seal_channel *channel)
{
	source_channel = channel;
	CHECK(payload_mm_authvar_mor_seal_channel_install(channel,
		protected_range, shared_range) == CB_SUCCESS);
}

static void fill_request(const struct payload_mm_authvar_mor_seal_channel *channel,
	uint32_t command, const struct payload_mm_authvar_mor_grant *grant)
{
	memset(&transport, 0, sizeof(transport));
	transport.revision = PAYLOAD_MM_AUTHVAR_MOR_SEAL_REVISION;
	transport.size = sizeof(transport);
	transport.command = command;
	memcpy(transport.capability, channel->capability,
		sizeof(transport.capability));
	if (grant)
		transport.grant = *grant;
}

static void expect_closed(void)
{
	struct payload_mm_authvar_mor_grant grant = valid_grant();

	CHECK(bytes_zero(&transport, sizeof(transport)));
	CHECK(!payload_mm_authvar_mor_grant_ready());
	CHECK(payload_mm_authvar_mor_grant_install(&grant, NULL, NULL) == CB_ERR);
}

static void receiver_failure(const char *name)
{
	struct payload_mm_authvar_mor_seal_channel channel = valid_channel();
	struct payload_mm_authvar_mor_grant grant = valid_grant();

	install_channel(&channel);
	fill_request(&channel, PAYLOAD_MM_AUTHVAR_MOR_SEAL_INSTALL, &grant);
	if (!strcmp(name, "bad-revision"))
		transport.revision++;
	else if (!strcmp(name, "bad-size"))
		transport.size--;
	else if (!strcmp(name, "unknown-command"))
		transport.command = 99;
	else if (!strcmp(name, "bad-reserved"))
		transport.reserved = 1;
	else if (!strcmp(name, "bad-capability"))
		transport.capability[0] ^= 1;
	else if (!strcmp(name, "bad-grant"))
		transport.grant.revision++;
	else if (!strcmp(name, "wrong-pointer"))
		trigger_transport = (const void *)((uintptr_t)&transport + 8U);
	else if (!strcmp(name, "wrong-size"))
		trigger_size--;
	else if (!strcmp(name, "wrong-caller"))
		trigger_caller++;
	else if (!strcmp(name, "wrong-context"))
		trigger_context++;
	else if (!strcmp(name, "range-recheck"))
		shared_ok = false;
	else
		CHECK(false);
	CHECK(trigger(NULL) != CB_SUCCESS);
	expect_closed();
	CHECK(payload_mm_authvar_mor_seal_receive(&transport, sizeof(transport),
		channel.caller, channel.caller_context) != CB_SUCCESS);
}

int main(int argc, char **argv)
{
	struct payload_mm_authvar_mor_seal_channel channel = valid_channel();
	struct payload_mm_authvar_mor_grant grant = valid_grant();

	CHECK(argc == 2);
	if (!strcmp(argv[1], "install")) {
		install_channel(&channel);
		source_grant = &grant;
		CHECK(payload_mm_authvar_mor_seal_send_install(&channel, &grant,
			trigger, NULL) == CB_SUCCESS);
		CHECK(triggers == 1 && bytes_zero(&channel, sizeof(channel)) &&
			bytes_zero(&transport, sizeof(transport)));
		CHECK(payload_mm_authvar_mor_grant_ready());
		CHECK(payload_mm_authvar_mor_grant_consume(&grant) == CB_SUCCESS);
		CHECK(private_secrets_zero());
		return 0;
	}
	if (!strcmp(argv[1], "close")) {
		install_channel(&channel);
		CHECK(payload_mm_authvar_mor_seal_send_close(&channel, trigger, NULL) ==
			CB_SUCCESS);
		expect_closed();
		return 0;
	}
	if (!strcmp(argv[1], "close-nonzero-grant")) {
		install_channel(&channel);
		fill_request(&channel, PAYLOAD_MM_AUTHVAR_MOR_SEAL_CLOSE, &grant);
		CHECK(trigger(NULL) != CB_SUCCESS);
		expect_closed();
		return 0;
	}
	if (!strncmp(argv[1], "bad-", 4) ||
	    !strncmp(argv[1], "wrong-", 6) ||
	    !strcmp(argv[1], "unknown-command") ||
	    !strcmp(argv[1], "range-recheck")) {
		receiver_failure(argv[1]);
		return 0;
	}
	if (!strcmp(argv[1], "channel-one-shot")) {
		install_channel(&channel);
		CHECK(payload_mm_authvar_mor_seal_channel_install(&channel,
			protected_range, shared_range) != CB_SUCCESS);
		return 0;
	}
	if (!strcmp(argv[1], "channel-null")) {
		CHECK(payload_mm_authvar_mor_seal_channel_install(NULL,
			protected_range, shared_range) != CB_SUCCESS);
		expect_closed();
		return 0;
	}
	if (!strcmp(argv[1], "channel-stored-callback-mutation")) {
		source_channel = &channel;
		install_callback_mutation = true;
		CHECK(payload_mm_authvar_mor_seal_channel_install(&channel,
			protected_range, shared_range) != CB_SUCCESS);
		expect_closed();
		CHECK(private_secrets_zero());
		return 0;
	}
	if (!strcmp(argv[1], "channel-transport-alias")) {
		union {
			struct payload_mm_authvar_mor_seal_channel channel;
			struct payload_mm_authvar_mor_seal_request request;
		} alias = { .channel = valid_channel() };

		alias.channel.transport_base = (uintptr_t)&alias.request;
		CHECK(payload_mm_authvar_mor_seal_channel_install(&alias.channel,
			protected_range, shared_range) != CB_SUCCESS);
		expect_closed();
		return 0;
	}
	if (!strcmp(argv[1], "channel-unprotected") ||
	    !strcmp(argv[1], "channel-unshared") ||
	    !strcmp(argv[1], "channel-mutation") ||
	    !strcmp(argv[1], "channel-size") ||
	    !strcmp(argv[1], "channel-wrap") ||
	    !strcmp(argv[1], "channel-zero-capability") ||
	    !strcmp(argv[1], "channel-zero-caller") ||
	    !strcmp(argv[1], "channel-zero-context")) {
		source_channel = &channel;
		if (!strcmp(argv[1], "channel-unprotected"))
			protected_ok = false;
		else if (!strcmp(argv[1], "channel-unshared"))
			shared_ok = false;
		else if (!strcmp(argv[1], "channel-mutation"))
			mutate_channel = true;
		else if (!strcmp(argv[1], "channel-size"))
			channel.transport_size--;
		else if (!strcmp(argv[1], "channel-wrap"))
			channel.transport_base = UINT64_MAX - 7U;
		else if (!strcmp(argv[1], "channel-zero-capability"))
			memset(channel.capability, 0, sizeof(channel.capability));
		else if (!strcmp(argv[1], "channel-zero-caller"))
			channel.caller = 0;
		else
			channel.caller_context = 0;
		CHECK(payload_mm_authvar_mor_seal_channel_install(&channel,
			protected_range, shared_range) != CB_SUCCESS);
		expect_closed();
		return 0;
	}
	if (!strcmp(argv[1], "sender-trigger-failure") ||
	    !strcmp(argv[1], "sender-rewrite") ||
	    !strcmp(argv[1], "sender-channel-mutation") ||
	    !strcmp(argv[1], "sender-grant-mutation")) {
		install_channel(&channel);
		source_grant = &grant;
		trigger_fail = !strcmp(argv[1], "sender-trigger-failure");
		rewrite_transport = !strcmp(argv[1], "sender-rewrite");
		mutate_channel = !strcmp(argv[1], "sender-channel-mutation");
		mutate_grant = !strcmp(argv[1], "sender-grant-mutation");
		CHECK(payload_mm_authvar_mor_seal_send_install(&channel, &grant,
			trigger, NULL) != CB_SUCCESS);
		CHECK(bytes_zero(&channel, sizeof(channel)) &&
			bytes_zero(&transport, sizeof(transport)));
		return 0;
	}
	if (!strcmp(argv[1], "late-authority-redirect") ||
	    !strcmp(argv[1], "late-authority-callback")) {
		install_channel(&channel);
		memset(decoy_transport, 0xa5, sizeof(decoy_transport));
		late_redirect = !strcmp(argv[1], "late-authority-redirect");
		late_callback_mutation = !strcmp(argv[1],
			"late-authority-callback");
		CHECK(payload_mm_authvar_mor_seal_send_install(&channel, &grant,
			trigger, NULL) != CB_SUCCESS);
		CHECK(!payload_mm_authvar_mor_grant_ready());
		CHECK(bytes_zero(&transport, sizeof(transport)));
		for (size_t index = 0; index < sizeof(decoy_transport); index++)
			CHECK(decoy_transport[index] == 0xa5);
		CHECK(private_secrets_zero());
		return 0;
	}
	if (!strcmp(argv[1], "sender-null") ||
	    !strcmp(argv[1], "sender-misaligned")) {
		struct payload_mm_authvar_mor_seal_channel *input =
			!strcmp(argv[1], "sender-null") ? NULL :
			(void *)((uintptr_t)&channel + 1U);
		CHECK(payload_mm_authvar_mor_seal_send_close(input, trigger, NULL) ==
			CB_ERR_ARG);
		CHECK(!triggers);
		return 0;
	}
	if (!strcmp(argv[1], "sender-channel-transport-alias")) {
		union {
			struct payload_mm_authvar_mor_seal_channel channel;
			struct payload_mm_authvar_mor_seal_request request;
		} alias = { .channel = valid_channel() };

		alias.channel.transport_base = (uintptr_t)&alias.request;
		CHECK(payload_mm_authvar_mor_seal_send_close(&alias.channel, trigger,
			NULL) != CB_SUCCESS);
		CHECK(bytes_zero(&alias.channel, sizeof(alias.channel)) && !triggers);
		return 0;
	}
	if (!strcmp(argv[1], "sender-grant-channel-alias")) {
		union {
			struct payload_mm_authvar_mor_seal_channel channel;
			struct payload_mm_authvar_mor_grant grant;
		} alias = { .channel = valid_channel() };

		CHECK(payload_mm_authvar_mor_seal_send_install(&alias.channel,
			&alias.grant, trigger, NULL) != CB_SUCCESS);
		CHECK(bytes_zero(&alias.channel, sizeof(alias.channel)) && !triggers);
		return 0;
	}
	CHECK(false);
	return 0;
}
