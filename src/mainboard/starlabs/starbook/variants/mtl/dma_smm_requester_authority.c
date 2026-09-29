/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_smm_requester_authority.h"

#include "payload_resource_policy.h"

#include <commonlib/helpers.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "MTL DMA requester authority is SMM-only"
#endif

#define PCI_VENDOR_DEVICE 0x00U
#define PCI_CLASS_REVISION 0x08U
#define PCI_HEADER_TYPE 0x0cU
#define PCI_PRIMARY_BUS 0x18U
#define PCI_CLASS_NVME 0x010802U
#define PCI_CLASS_XHCI 0x0c0330U
#define PCI_CLASS_PCI_BRIDGE 0x060400U
#define PCI_HEADER_NORMAL 0x00U
#define PCI_HEADER_BRIDGE 0x01U
#define PCI_HEADER_TYPE_MASK 0x7fU
#define PCI_VENDOR_INTEL 0x8086U

struct topology_observation {
	struct starbook_mtl_dma_requester bridge;
	struct starbook_mtl_dma_requester requester[
		STARBOOK_MTL_DMA_REQUESTER_ROLE_COUNT];
};

static bool binding_valid(
	const struct starbook_mtl_dma_requester_binding *binding)
{
	return binding &&
		!smm_invocation_loader_instance_nonce_is_zero(
			binding->loader_instance_nonce) &&
		binding->invocation_generation &&
		(binding->loader_lifecycle == SMM_INVOCATION_LOADER_NON_S3_LOAD ||
		 binding->loader_lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD) &&
		!binding->reserved;
}

static bool binding_equal(
	const struct starbook_mtl_dma_requester_binding *first,
	const struct starbook_mtl_dma_requester_binding *second)
{
	return binding_valid(first) && binding_valid(second) &&
		smm_invocation_loader_instance_nonce_equal(
			first->loader_instance_nonce,
			second->loader_instance_nonce) &&
		first->invocation_generation == second->invocation_generation &&
		first->loader_lifecycle == second->loader_lifecycle;
}

static enum cb_err config_read(
	const struct starbook_mtl_dma_requester_authority_io *io,
	uint8_t bus, uint8_t devfn, uint16_t offset, uint32_t *value)
{
	if (!value || (offset & 3U) || offset > 0xffcU)
		return CB_ERR;
	return io->read_config32(io->context, bus, devfn, offset, value);
}

static enum cb_err endpoint_read(
	const struct starbook_mtl_dma_requester_authority_io *io,
	uint8_t bus, uint8_t devfn, uint16_t expected_vendor,
	uint32_t expected_class,
	struct starbook_mtl_dma_requester *requester)
{
	uint32_t identity;
	uint32_t class_revision;
	uint32_t header;

	if (config_read(io, bus, devfn, PCI_VENDOR_DEVICE, &identity) ||
	    identity == UINT32_MAX || (identity & 0xffffU) == 0xffffU ||
	    (expected_vendor && (uint16_t)identity != expected_vendor) ||
	    config_read(io, bus, devfn, PCI_CLASS_REVISION, &class_revision) ||
	    config_read(io, bus, devfn, PCI_HEADER_TYPE, &header) ||
	    (class_revision >> 8) != expected_class ||
	    ((header >> 16) & PCI_HEADER_TYPE_MASK) != PCI_HEADER_NORMAL)
		return CB_ERR;
	*requester = (struct starbook_mtl_dma_requester) {
		.bdf = (uint16_t)((uint16_t)bus << 8 | devfn),
		.vendor = (uint16_t)identity,
		.device = (uint16_t)(identity >> 16),
		.class = expected_class,
	};
	return CB_SUCCESS;
}

static enum cb_err observe(
	const struct starbook_mtl_dma_requester_authority_io *io,
	struct topology_observation *observation)
{
	struct starbook_mtl_dma_requester nvme = { 0 };
	uint32_t identity;
	uint32_t class_revision;
	uint32_t header;
	uint32_t buses;
	uint8_t secondary;
	size_t nvme_count = 0;

	if (config_read(io, 0, STARBOOK_MTL_RP10_DEVFN,
		PCI_VENDOR_DEVICE, &identity) || identity == UINT32_MAX ||
	    (uint16_t)identity != PCI_VENDOR_INTEL ||
	    config_read(io, 0, STARBOOK_MTL_RP10_DEVFN,
		PCI_CLASS_REVISION, &class_revision) ||
	    config_read(io, 0, STARBOOK_MTL_RP10_DEVFN,
		PCI_HEADER_TYPE, &header) ||
	    config_read(io, 0, STARBOOK_MTL_RP10_DEVFN,
		PCI_PRIMARY_BUS, &buses) ||
	    (class_revision >> 8) != PCI_CLASS_PCI_BRIDGE ||
	    ((header >> 16) & PCI_HEADER_TYPE_MASK) != PCI_HEADER_BRIDGE ||
	    (uint8_t)buses != 0 || !(uint8_t)(buses >> 8) ||
	    (uint8_t)(buses >> 8) != (uint8_t)(buses >> 16))
		return CB_ERR;
	secondary = (uint8_t)(buses >> 8);
	observation->bridge = (struct starbook_mtl_dma_requester) {
		.bdf = STARBOOK_MTL_RP10_DEVFN,
		.vendor = (uint16_t)identity,
		.device = (uint16_t)(identity >> 16),
		.class = PCI_CLASS_PCI_BRIDGE,
	};

	for (uint16_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
		struct starbook_mtl_dma_requester candidate;

		if (config_read(io, secondary, (uint8_t)devfn,
			PCI_VENDOR_DEVICE, &identity))
			return CB_ERR;
		if (identity == UINT32_MAX || (identity & 0xffffU) == 0xffffU)
			continue;
		if (config_read(io, secondary, (uint8_t)devfn,
			PCI_CLASS_REVISION, &class_revision))
			return CB_ERR;
		if ((class_revision >> 8) != PCI_CLASS_NVME)
			continue;
		if (endpoint_read(io, secondary, (uint8_t)devfn, 0,
			PCI_CLASS_NVME, &candidate) || ++nvme_count != 1)
			return CB_ERR;
		nvme = candidate;
	}
	if (nvme_count != 1 || endpoint_read(io, 0,
		STARBOOK_MTL_PCH_XHCI_DEVFN, PCI_VENDOR_INTEL, PCI_CLASS_XHCI,
		&observation->requester[STARBOOK_MTL_DMA_REQUESTER_PCH_XHCI]) ||
	    endpoint_read(io, 0, STARBOOK_MTL_TCSS_XHCI_DEVFN,
		PCI_VENDOR_INTEL, PCI_CLASS_XHCI,
		&observation->requester[STARBOOK_MTL_DMA_REQUESTER_TCSS_XHCI]))
		return CB_ERR;
	observation->requester[STARBOOK_MTL_DMA_REQUESTER_NVME] = nvme;
	observation->requester[STARBOOK_MTL_DMA_REQUESTER_NVME].domain = 1;
	observation->requester[STARBOOK_MTL_DMA_REQUESTER_PCH_XHCI].domain = 2;
	observation->requester[STARBOOK_MTL_DMA_REQUESTER_TCSS_XHCI].domain = 3;
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_dma_requester_authority_derive(
	const struct starbook_mtl_dma_requester_authority_io *io,
	const struct starbook_mtl_dma_requester_binding *expected_binding,
	struct starbook_mtl_dma_requester_authority *authority)
{
	struct starbook_mtl_dma_requester_binding before;
	struct starbook_mtl_dma_requester_binding between;
	struct starbook_mtl_dma_requester_binding after;
	struct starbook_mtl_dma_requester_binding expected;
	struct topology_observation first;
	struct topology_observation second;
	struct starbook_mtl_dma_requester_authority result;

	if (!io || !io->read_config32 || !io->read_binding ||
	    !expected_binding || !authority)
		return CB_ERR;
	expected = *expected_binding;
	if (!binding_valid(&expected) ||
	    io->read_binding(io->context, &before) ||
	    !binding_equal(&before, &expected) ||
	    observe(io, &first) ||
	    io->read_binding(io->context, &between) ||
	    !binding_equal(&between, &expected) ||
	    observe(io, &second) ||
	    io->read_binding(io->context, &after) ||
	    !binding_equal(&after, &expected) ||
	    !binding_equal(expected_binding, &expected) ||
	    memcmp(&first, &second, sizeof(first)))
		return CB_ERR;
	memset(&result, 0, sizeof(result));
	result.binding = after;
	memcpy(result.requester, second.requester, sizeof(result.requester));
	*authority = result;
	return CB_SUCCESS;
}

_Static_assert(STARBOOK_MTL_DMA_REQUESTER_NVME == 0,
	"requester role and output index diverged");
_Static_assert(STARBOOK_MTL_DMA_REQUESTER_PCH_XHCI == 1,
	"requester role and output index diverged");
_Static_assert(STARBOOK_MTL_DMA_REQUESTER_TCSS_XHCI == 2,
	"requester role and output index diverged");
