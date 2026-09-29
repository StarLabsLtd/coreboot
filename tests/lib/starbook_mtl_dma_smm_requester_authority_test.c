/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_smm_requester_authority.h"
#include "payload_resource_policy.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define VENDOR_DEVICE(vendor, device) ((uint32_t)(device) << 16 | (vendor))
#define CLASS_REVISION(class) ((uint32_t)(class) << 8)
#define HEADER(type) ((uint32_t)(type) << 16)
#define BUSES(primary, secondary, subordinate) \
	((uint32_t)(primary) | (uint32_t)(secondary) << 8 | \
	 (uint32_t)(subordinate) << 16)

enum {
	REG_ID,
	REG_CLASS,
	REG_HEADER,
	REG_BUSES,
	REG_COUNT,
};

struct model {
	uint32_t config[256][256][REG_COUNT];
	struct starbook_mtl_dma_requester_binding binding;
	size_t config_reads;
	size_t binding_reads;
	size_t mutate_config_at;
	size_t mutate_binding_at;
	size_t fail_config_at;
	size_t fail_binding_at;
	bool mutate_bridge;
};

static size_t register_index(uint16_t offset)
{
	switch (offset) {
	case 0x00:
		return REG_ID;
	case 0x08:
		return REG_CLASS;
	case 0x0c:
		return REG_HEADER;
	case 0x18:
		return REG_BUSES;
	default:
		abort();
	}
}

static enum cb_err read_config32(void *context, uint8_t bus, uint8_t devfn,
	uint16_t offset, uint32_t *value)
{
	struct model *model = context;

	model->config_reads++;
	if (model->fail_config_at &&
	    model->config_reads == model->fail_config_at)
		return CB_ERR;
	if (model->mutate_config_at &&
	    model->config_reads == model->mutate_config_at) {
		const uint8_t target_devfn = model->mutate_bridge ?
			STARBOOK_MTL_RP10_DEVFN : STARBOOK_MTL_PCH_XHCI_DEVFN;

		model->config[0][target_devfn][REG_ID] =
			VENDOR_DEVICE(0x8086, 0x7e7e);
	}
	*value = model->config[bus][devfn][register_index(offset)];
	return CB_SUCCESS;
}

static enum cb_err read_binding(void *context,
	struct starbook_mtl_dma_requester_binding *binding)
{
	struct model *model = context;

	model->binding_reads++;
	if (model->fail_binding_at &&
	    model->binding_reads == model->fail_binding_at)
		return CB_ERR;
	if (model->mutate_binding_at &&
	    model->binding_reads == model->mutate_binding_at)
		model->binding.invocation_generation++;
	*binding = model->binding;
	return CB_SUCCESS;
}

static void endpoint(struct model *model, uint8_t bus, uint8_t devfn,
	uint16_t vendor, uint16_t device, uint32_t class)
{
	model->config[bus][devfn][REG_ID] = VENDOR_DEVICE(vendor, device);
	model->config[bus][devfn][REG_CLASS] = CLASS_REVISION(class);
	model->config[bus][devfn][REG_HEADER] = HEADER(0);
}

static void model_init(struct model *model)
{
	memset(model, 0xff, sizeof(*model));
	model->binding = (struct starbook_mtl_dma_requester_binding) {
		.loader_instance_nonce = { .low = 0x1234, .high = 0x5678 },
		.invocation_generation = 9,
		.loader_lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	model->config_reads = 0;
	model->binding_reads = 0;
	model->mutate_config_at = 0;
	model->mutate_binding_at = 0;
	model->fail_config_at = 0;
	model->fail_binding_at = 0;
	model->mutate_bridge = false;
	model->config[0][STARBOOK_MTL_RP10_DEVFN][REG_ID] =
		VENDOR_DEVICE(0x8086, 0x7e4d);
	model->config[0][STARBOOK_MTL_RP10_DEVFN][REG_CLASS] =
		CLASS_REVISION(0x060400);
	model->config[0][STARBOOK_MTL_RP10_DEVFN][REG_HEADER] = HEADER(1);
	model->config[0][STARBOOK_MTL_RP10_DEVFN][REG_BUSES] = BUSES(0, 2, 2);
	endpoint(model, 2, 0, 0x144d, 0xa80b, 0x010802);
	endpoint(model, 0, STARBOOK_MTL_PCH_XHCI_DEVFN,
		0x8086, 0x7e7d, 0x0c0330);
	endpoint(model, 0, STARBOOK_MTL_TCSS_XHCI_DEVFN,
		0x8086, 0x7ec0, 0x0c0330);
}

static enum cb_err derive(struct model *model,
	const struct starbook_mtl_dma_requester_binding *expected,
	struct starbook_mtl_dma_requester_authority *authority)
{
	const struct starbook_mtl_dma_requester_authority_io io = {
		.context = model,
		.read_config32 = read_config32,
		.read_binding = read_binding,
	};

	return starbook_mtl_dma_requester_authority_derive(&io, expected,
		authority);
}

static void expect_rejected(struct model *model)
{
	struct starbook_mtl_dma_requester_authority authority;
	const struct starbook_mtl_dma_requester_binding expected = model->binding;

	memset(&authority, 0xa5, sizeof(authority));
	assert(derive(model, &expected, &authority) == CB_ERR);
	for (size_t index = 0; index < sizeof(authority); index++)
		assert(((const uint8_t *)&authority)[index] == 0xa5);
}

static void expect_binding_rejected(struct model *model,
	const struct starbook_mtl_dma_requester_binding *expected)
{
	struct starbook_mtl_dma_requester_authority authority;

	memset(&authority, 0xa5, sizeof(authority));
	assert(derive(model, expected, &authority) == CB_ERR);
	for (size_t index = 0; index < sizeof(authority); index++)
		assert(((const uint8_t *)&authority)[index] == 0xa5);
}

static void test_valid(void)
{
	struct model model;
	struct starbook_mtl_dma_requester_authority authority;

	model_init(&model);
	assert(derive(&model, &model.binding, &authority) == CB_SUCCESS);
	assert(authority.requester[0].bdf == 0x200);
	assert(authority.requester[0].domain == 1);
	assert(authority.requester[0].vendor == 0x144d);
	assert(authority.requester[0].device == 0xa80b);
	assert(authority.requester[0].class == 0x010802);
	assert(authority.requester[1].bdf == STARBOOK_MTL_PCH_XHCI_DEVFN);
	assert(authority.requester[1].domain == 2);
	assert(authority.requester[2].bdf == STARBOOK_MTL_TCSS_XHCI_DEVFN);
	assert(authority.requester[2].domain == 3);
	assert(model.binding_reads == 3);
	assert(model.config_reads > 512);
}

static void test_binding_failures(void)
{
	struct model model;
	struct starbook_mtl_dma_requester_binding expected;

	model_init(&model);
	expected = model.binding;
	expected.invocation_generation++;
	expect_binding_rejected(&model, &expected);

#define REJECT_BINDING(member, value) do { \
	model_init(&model); \
	model.binding.member = (value); \
	expect_rejected(&model); \
} while (0)
	REJECT_BINDING(invocation_generation, 0);
	REJECT_BINDING(loader_lifecycle, 0);
	REJECT_BINDING(loader_lifecycle, 3);
	REJECT_BINDING(reserved, 1);
#undef REJECT_BINDING
	model_init(&model);
	model.binding.loader_instance_nonce.low = 0;
	model.binding.loader_instance_nonce.high = 0;
	expect_rejected(&model);
	for (size_t read = 1; read <= 3; read++) {
		model_init(&model);
		model.mutate_binding_at = read;
		expect_rejected(&model);
	}
}

static void test_bridge_failures(void)
{
	struct model model;

#define REJECT_BRIDGE(reg, value) do { \
	model_init(&model); \
	model.config[0][STARBOOK_MTL_RP10_DEVFN][reg] = (value); \
	expect_rejected(&model); \
} while (0)
	REJECT_BRIDGE(REG_ID, UINT32_MAX);
	REJECT_BRIDGE(REG_ID, VENDOR_DEVICE(0xffff, 1));
	REJECT_BRIDGE(REG_ID, VENDOR_DEVICE(0x1234, 1));
	REJECT_BRIDGE(REG_CLASS, CLASS_REVISION(0x060401));
	REJECT_BRIDGE(REG_HEADER, HEADER(0));
	REJECT_BRIDGE(REG_BUSES, BUSES(1, 2, 2));
	REJECT_BRIDGE(REG_BUSES, BUSES(0, 0, 0));
	REJECT_BRIDGE(REG_BUSES, BUSES(0, 2, 3));
#undef REJECT_BRIDGE
}

static void test_endpoint_failures(void)
{
	struct model model;

	model_init(&model);
	model.config[2][0][REG_ID] = UINT32_MAX;
	expect_rejected(&model);
	model_init(&model);
	endpoint(&model, 2, 8, 0x1234, 0x5678, 0x010802);
	expect_rejected(&model);
	model_init(&model);
	model.config[2][0][REG_HEADER] = HEADER(1);
	expect_rejected(&model);
	model_init(&model);
	model.config[2][0][REG_CLASS] = CLASS_REVISION(0x010801);
	expect_rejected(&model);
	model_init(&model);
	model.config[0][STARBOOK_MTL_PCH_XHCI_DEVFN][REG_ID] = UINT32_MAX;
	expect_rejected(&model);
	model_init(&model);
	model.config[0][STARBOOK_MTL_PCH_XHCI_DEVFN][REG_ID] =
		VENDOR_DEVICE(0x1234, 1);
	expect_rejected(&model);
	model_init(&model);
	model.config[0][STARBOOK_MTL_PCH_XHCI_DEVFN][REG_CLASS] =
		CLASS_REVISION(0x0c0320);
	expect_rejected(&model);
	model_init(&model);
	model.config[0][STARBOOK_MTL_TCSS_XHCI_DEVFN][REG_HEADER] = HEADER(1);
	expect_rejected(&model);
	model_init(&model);
	model.config[0][STARBOOK_MTL_TCSS_XHCI_DEVFN][REG_ID] =
		VENDOR_DEVICE(0x1234, 1);
	expect_rejected(&model);
	model_init(&model);
	model.config[0][STARBOOK_MTL_TCSS_XHCI_DEVFN][REG_CLASS] =
		CLASS_REVISION(0x0c0320);
	expect_rejected(&model);
}

static void test_drift_and_io_failures(void)
{
	struct model baseline;
	struct model model;
	struct starbook_mtl_dma_requester_authority authority;
	size_t first_observation_reads;
	size_t config_reads;
	size_t binding_reads;

	model_init(&baseline);
	assert(derive(&baseline, &baseline.binding, &authority) == CB_SUCCESS);
	assert((baseline.config_reads & 1U) == 0);
	first_observation_reads = baseline.config_reads / 2;
	config_reads = baseline.config_reads;
	binding_reads = baseline.binding_reads;
	model_init(&model);
	model.mutate_config_at = first_observation_reads + 1;
	expect_rejected(&model);
	model_init(&model);
	model.mutate_config_at = first_observation_reads + 1;
	model.mutate_bridge = true;
	expect_rejected(&model);
	for (size_t read = 1; read <= config_reads; read++) {
		model_init(&model);
		model.fail_config_at = read;
		expect_rejected(&model);
	}
	for (size_t read = 1; read <= binding_reads; read++) {
		model_init(&model);
		model.fail_binding_at = read;
		expect_rejected(&model);
	}
}

static void test_arguments(void)
{
	struct model model;
	struct starbook_mtl_dma_requester_authority authority;
	struct starbook_mtl_dma_requester_authority_io io;

	model_init(&model);
	io = (struct starbook_mtl_dma_requester_authority_io) {
		.context = &model,
		.read_config32 = read_config32,
		.read_binding = read_binding,
	};
	assert(starbook_mtl_dma_requester_authority_derive(NULL, &model.binding,
		&authority) == CB_ERR);
	assert(starbook_mtl_dma_requester_authority_derive(&io, NULL,
		&authority) == CB_ERR);
	assert(starbook_mtl_dma_requester_authority_derive(&io, &model.binding,
		NULL) == CB_ERR);
	io.read_config32 = NULL;
	assert(starbook_mtl_dma_requester_authority_derive(&io, &model.binding,
		&authority) == CB_ERR);
	io.read_config32 = read_config32;
	io.read_binding = NULL;
	assert(starbook_mtl_dma_requester_authority_derive(&io, &model.binding,
		&authority) == CB_ERR);
}

int main(void)
{
	test_valid();
	test_binding_failures();
	test_bridge_failures();
	test_endpoint_failures();
	test_drift_and_io_failures();
	test_arguments();
	return 0;
}
