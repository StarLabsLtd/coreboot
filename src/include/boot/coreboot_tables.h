/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef COREBOOT_TABLES_H
#define COREBOOT_TABLES_H

#include <commonlib/coreboot_tables.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct device;

/* function prototypes for building the coreboot table */

/*
 * Write forwarding table of target address at entry address returning size
 * of table written.
 */
size_t write_coreboot_forwarding_table(uintptr_t entry, uintptr_t target);
uintptr_t write_coreboot_table(uintptr_t rom_table_end);

void fill_lb_gpios(struct lb_gpios *gpios);
void lb_add_gpios(struct lb_gpios *gpios, const struct lb_gpio *gpio_table,
		  size_t count);

enum cb_err fill_lb_serial(struct lb_serial *serial);
void lb_add_console(uint16_t consoletype, void *data);

enum cb_err fill_lb_pcie(struct lb_pcie *pcie);

/* Adds an authoritative payload-resource PCI root-bridge handoff. */
enum cb_err lb_add_payload_resource_handoff(struct lb_header *header);
uint16_t payload_resource_read_command(const struct device *device);
void payload_resource_write_command(const struct device *device, uint16_t command);
uint32_t payload_resource_read_bar(const struct device *device, uint8_t bar);
bool payload_resource_firmware_owned(const struct device *device);
/* Board policy: opt in only when the enumerated tree and boot intent are authoritative. */
bool payload_resource_revision4_ready(void);
/* Return true for a boot controller; lower priorities are serialized first. */
bool payload_resource_boot_controller(const struct device *device, uint16_t *priority);

void lb_string_platform_blob_version(struct lb_header *header);

/* Define this in mainboard.c to add board-specific table entries. */
void lb_board(struct lb_header *header);

/* Adds LB_TAG_EFI_FW_INFO table entry. */
void lb_efi_fw_info(struct lb_header *header);

/* Adds LB_TAG_CAPSULE table entries. */
void lb_efi_capsules(struct lb_header *header);

/* Define this function to get the frame buffer returning lb_framebuffer object
   on success and NULL on error. */
const struct lb_framebuffer *get_lb_framebuffer(void);
const struct lb_framebuffer *payload_resource_framebuffer(void);

/* Allow arch to add records. */
void lb_arch_add_records(struct lb_header *header);

/*
 * Function to retrieve MAC address(es) from the VPD and store them in the
 * coreboot table.
 */
void lb_table_add_macs_from_vpd(struct lb_header *header);

void lb_table_add_serialno_from_vpd(struct lb_header *header);

struct lb_record *lb_new_record(struct lb_header *header);
void lb_add_local_apic_timer_info(struct lb_header *header, uint64_t frequency_hz);
uint64_t soc_local_apic_timer_frequency_hz(void);

/* Add VBOOT VBNV offsets. */
void lb_table_add_vbnv_cmos(struct lb_header *header);

/* Register a non-PCI SDHCI controller */
void lb_add_sdhci_nonpci(uint32_t mmio_base, uint32_t mmio_size,
			 uint8_t slot, uint8_t flags);

/* Define this in mainboard.c to add board specific CFR entries */
void mb_cfr_setup_menu(struct lb_cfr *cfr_root);

#endif /* COREBOOT_TABLES_H */
