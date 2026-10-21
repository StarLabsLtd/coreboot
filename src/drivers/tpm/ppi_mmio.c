/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/endian.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <device/fw_cfg.h>
#include <security/tpm/tss.h>

#include "tpm_ppi.h"

#define TPM_PPI_MMIO_SIZE (4 * KiB)

/* QEMU's six-byte etc/tpm/config wire record. PPI version 1 denotes 1.3. */
struct qemu_tpm_config {
	uint32_t ppi_address;
	uint8_t tpm_version;
	uint8_t ppi_version;
} __packed;

_Static_assert(sizeof(struct qemu_tpm_config) == 6, "QEMU TPM configuration layout");

static struct {
	const struct resource *resource;
	const struct device *device;
	uint8_t tpm_version;
} mmio_ppi;

void tpm_ppi_add_resources(struct device *dev, unsigned long index)
{
	struct qemu_tpm_config config;
	FWCfgFile file;
	uintptr_t tolud;

	if (fw_cfg_check_file(&file, "etc/tpm/config"))
		return;
	if (file.size != sizeof(config)) {
		printk(BIOS_ERR, "PPI: Invalid QEMU configuration size %u\n", file.size);
		return;
	}
	fw_cfg_get(file.select, &config, sizeof(config));
	config.ppi_address = le32_to_cpu(config.ppi_address);
	if (!config.ppi_version)
		return;
	tolud = fw_cfg_tolud();
	if (config.ppi_version != 1 ||
	    (config.tpm_version != LB_TPM_VERSION_TPM_VERSION_1_2 &&
	     config.tpm_version != LB_TPM_VERSION_TPM_VERSION_2) ||
	    !tolud || config.ppi_address < tolud ||
	    (config.ppi_address & (TPM_PPI_MMIO_SIZE - 1)) ||
	    config.ppi_address > UINT32_MAX - (TPM_PPI_MMIO_SIZE - 1)) {
		printk(BIOS_ERR, "PPI: Unsupported QEMU mailbox configuration\n");
		return;
	}
	mmio_ppi.resource = mmio_range(dev, index, config.ppi_address, TPM_PPI_MMIO_SIZE);
	mmio_ppi.device = dev;
	mmio_ppi.tpm_version = config.tpm_version;
}

/* Recheck after resource assignment; discovery cannot see later devices yet. */
static bool tpm_ppi_mmio_available(void)
{
	const struct resource *mailbox = mmio_ppi.resource;

	if (!mailbox || mailbox->size != TPM_PPI_MMIO_SIZE ||
	    (mailbox->flags & (IORESOURCE_MEM | IORESOURCE_FIXED | IORESOURCE_RESERVE |
			       IORESOURCE_STORED | IORESOURCE_CACHEABLE)) !=
	    (IORESOURCE_MEM | IORESOURCE_FIXED | IORESOURCE_RESERVE | IORESOURCE_STORED))
		return false;
	for (const struct device *dev = all_devices; dev; dev = dev->next) {
		for (const struct resource *res = dev->resource_list; res; res = res->next) {
			bool ancestor_window = false;

			if (res == mailbox || !(res->flags & IORESOURCE_MEM) || !res->size ||
			    !(res->flags & (IORESOURCE_FIXED | IORESOURCE_ASSIGNED)))
				continue;
			if (res->flags & (IORESOURCE_BRIDGE | IORESOURCE_SUBTRACTIVE)) {
				for (const struct device *parent = mmio_ppi.device; parent;
				     parent = parent->upstream && !is_root_device(parent) ?
					parent->upstream->dev : NULL)
					if (parent == dev)
						ancestor_window = true;
			}
			if (ancestor_window)
				continue;
			if (res->base > UINT64_MAX - res->size ||
			    (res->base < mailbox->base + mailbox->size &&
			     mailbox->base < res->base + res->size)) {
				printk(BIOS_ERR, "PPI: QEMU mailbox conflicts with %s resource %lx\n",
				       dev_path(dev), res->index);
				return false;
			}
		}
	}
	return true;
}

void tpm_ppi_add_mmio_handoff(struct lb_header *header)
{
	struct lb_tpm_physical_presence_mmio *mmio;
	enum tpm_family family;
	uint8_t version;

	if (!tpm_ppi_mmio_available() || tpm_is_expected_absent() ||
	    tlcl_lib_init() != TPM_SUCCESS)
		return;
	family = tlcl_get_family();
	if (family == TPM_UNKNOWN)
		return;
	version = family == TPM_1 ? LB_TPM_VERSION_TPM_VERSION_1_2 :
		LB_TPM_VERSION_TPM_VERSION_2;
	if (version != mmio_ppi.tpm_version) {
		printk(BIOS_ERR, "PPI: QEMU TPM version does not match the device\n");
		return;
	}
	mmio = (void *)lb_new_record(header);
	*mmio = (struct lb_tpm_physical_presence_mmio){
		.common = {
			.tag = LB_TAG_TPM_PPI_HANDOFF,
			.size = sizeof(*mmio),
			.ppi_address = mmio_ppi.resource->base,
			.tpm_version = version,
			.ppi_version = 0x13,
		},
		.storage = LB_TPM_PPI_STORAGE_MMIO,
		.buffer_size = sizeof(struct cb_tpm_ppi_payload_handshake),
	};
}

void tpm_ppi_disable_mmio(void)
{
	volatile struct cb_tpm_ppi_payload_handshake *ppi;

	if (!tpm_ppi_mmio_available())
		return;
	ppi = (void *)(uintptr_t)mmio_ppi.resource->base;
	/* Withdraw capabilities without discarding a retained request or result. */
	for (size_t index = 0; index < ARRAY_SIZE(ppi->func); index++)
		ppi->func[index] = 0;
}
