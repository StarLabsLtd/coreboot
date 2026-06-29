/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Unlike log.c this implements TPM log according to TPM2.0 specification
 * rather then using coreboot-specific log format.
 *
 * First entry is in TPM1.2 format and serves as a header, the rest are in
 * a newer (agile) format which supports SHA256 and multiple hashes, but we
 * store only one hash.
 *
 * This is defined in "TCG EFI Protocol Specification".
 */

#include <endian.h>
#include <console/console.h>
#include <security/tpm/tspi.h>
#include <security/tpm/tspi/crtm.h>
#include <security/tpm/tspi/logs.h>
#include <region_file.h>
#include <string.h>
#include <symbols.h>
#include <cbmem.h>
#include <vb2_sha.h>

static size_t tpm2_log_entry_header_size(void)
{
	return sizeof(struct tpm_2_log_entry) - TPM_20_LOG_DATA_MAX_LENGTH;
}

static size_t tpm2_log_entry_size(const struct tpm_2_log_entry *tce)
{
	uint32_t data_length = le32toh(tce->data_length);

	if (data_length > TPM_20_LOG_DATA_MAX_LENGTH)
		return 0;

	return tpm2_log_entry_header_size() + data_length;
}

static size_t tpm2_log_capacity(const struct tpm_2_log_table *tclt)
{
	return le16toh(tclt->vendor.max_entries) * sizeof(struct tpm_2_log_entry);
}

static const uint8_t *tpm2_log_entry_offset(const struct tpm_2_log_table *tclt,
					    uint16_t entry_idx)
{
	uint16_t i;
	const uint8_t *entry = (const uint8_t *)tclt->entries;
	const uint8_t *end = entry + tpm2_log_capacity(tclt);

	for (i = 0; i < entry_idx; i++) {
		size_t remaining = end - entry;
		size_t entry_size;

		if (remaining < tpm2_log_entry_header_size())
			return NULL;

		entry_size = tpm2_log_entry_size((const struct tpm_2_log_entry *)entry);
		if (!entry_size || entry_size > remaining)
			return NULL;

		entry += entry_size;
	}

	return entry;
}

static struct tpm_2_log_entry *tpm2_log_entry_at(const struct tpm_2_log_table *tclt,
						 uint16_t entry_idx)
{
	const uint8_t *entry = tpm2_log_entry_offset(tclt, entry_idx);
	const uint8_t *end = (const uint8_t *)tclt->entries + tpm2_log_capacity(tclt);
	size_t remaining;
	size_t entry_size;

	if (!entry)
		return NULL;

	remaining = end - entry;
	if (remaining < tpm2_log_entry_header_size())
		return NULL;

	entry_size = tpm2_log_entry_size((const struct tpm_2_log_entry *)entry);
	if (!entry_size || entry_size > remaining)
		return NULL;

	return (struct tpm_2_log_entry *)entry;
}

static size_t tpm2_log_used_size(const struct tpm_2_log_table *tclt)
{
	uint16_t entries = le16toh(tclt->vendor.num_entries);
	const uint8_t *start = (const uint8_t *)tclt->entries;
	const uint8_t *next = tpm2_log_entry_offset(tclt, entries);

	if (!next)
		return tpm2_log_capacity(tclt) + 1;

	return (const uint8_t *)next - start;
}

static uint16_t tpmalg_from_vb2_hash(enum vb2_hash_algorithm hash_type)
{
	switch (hash_type) {
	case VB2_HASH_SHA1:
		return TPM2_ALG_SHA1;
	case VB2_HASH_SHA256:
		return TPM2_ALG_SHA256;
	case VB2_HASH_SHA384:
		return TPM2_ALG_SHA384;
	case VB2_HASH_SHA512:
		return TPM2_ALG_SHA512;

	default:
		return 0xFF;
	}
}

static void tpm2_log_append_event(struct tpm_2_log_table *tclt,
				  const uint32_t pcr, const uint32_t event_type,
				  enum vb2_hash_algorithm digest_algo,
				  const uint8_t *digest, const size_t digest_len,
				  const uint8_t *data, const size_t data_len)
{
	struct tpm_2_log_entry *tce;
	size_t new_entry_size;
	size_t used_size;

	if (!digest) {
		printk(BIOS_WARNING, "TPM LOG: event digest not set\n");
		return;
	}

	if (digest_algo != TPM_MEASURE_ALGO) {
		printk(BIOS_WARNING, "TPM LOG: digest is of unsupported type: %s\n",
		       vb2_get_hash_algorithm_name(digest_algo));
		return;
	}

	if (digest_len != vb2_digest_size(TPM_MEASURE_ALGO)) {
		printk(BIOS_WARNING, "TPM LOG: digest has invalid length: %d\n",
		       (int)digest_len);
		return;
	}

	if (!data) {
		printk(BIOS_WARNING, "TPM LOG: event data not set\n");
		return;
	}

	if (data_len > TPM_20_LOG_DATA_MAX_LENGTH) {
		printk(BIOS_WARNING, "TPM LOG: event data too long\n");
		return;
	}

	if (le16toh(tclt->vendor.num_entries) >= le16toh(tclt->vendor.max_entries)) {
		printk(BIOS_WARNING, "TPM LOG: log table is full\n");
		return;
	}

	new_entry_size = tpm2_log_entry_header_size() + data_len;
	used_size = tpm2_log_used_size(tclt);
	if (used_size > tpm2_log_capacity(tclt) ||
	    used_size + new_entry_size > tpm2_log_capacity(tclt)) {
		printk(BIOS_WARNING, "TPM LOG: log table is full\n");
		return;
	}

	tce = (struct tpm_2_log_entry *)((uint8_t *)tclt->entries + used_size);
	tclt->vendor.num_entries = htole16(le16toh(tclt->vendor.num_entries) + 1);

	memset(tce, 0, new_entry_size);
	tce->pcr = htole32(pcr);
	tce->event_type = htole32(event_type);
	tce->digest_count = htole32(1);
	tce->digest_type = htole16(tpmalg_from_vb2_hash(TPM_MEASURE_ALGO));
	tce->data_length = htole32(data_len);

	memcpy(tce->digest, digest, digest_len);
	memcpy(tce->data, data, data_len);
}

void *tpm2_log_cbmem_init(void)
{
	static struct tpm_2_log_table *tclt;
	if (tclt)
		return tclt;

	if (ENV_HAS_CBMEM) {
		size_t tpm_log_len;
		struct tcg_efi_spec_id_event *hdr;

		tclt = cbmem_find(CBMEM_ID_TPM2_TCG_LOG);
		if (tclt)
			return tclt;

		tpm_log_len = sizeof(struct tpm_2_log_table) +
			MAX_TPM_LOG_ENTRIES * sizeof(struct tpm_2_log_entry);
		tclt = cbmem_add(CBMEM_ID_TPM2_TCG_LOG, tpm_log_len);
		if (!tclt)
			return NULL;

		memset(tclt, 0, tpm_log_len);
		hdr = &tclt->header;

		hdr->event_type = htole32(EV_NO_ACTION);
		hdr->event_size = htole32(33 + sizeof(tclt->vendor));
		strcpy((char *)hdr->signature, TPM_20_SPEC_ID_EVENT_SIGNATURE);
		hdr->platform_class = htole32(0x00); // client platform
		hdr->spec_version_minor = 0x00;
		hdr->spec_version_major = 0x02;
		hdr->spec_errata = 0x00;
		hdr->uintn_size = 0x02; // 64-bit UINT
		hdr->num_of_algorithms = htole32(1);
		hdr->digest_sizes[0].alg_id = htole16(tpmalg_from_vb2_hash(TPM_MEASURE_ALGO));
		hdr->digest_sizes[0].digest_size = htole16(vb2_digest_size(TPM_MEASURE_ALGO));

		tclt->vendor_info_size = sizeof(tclt->vendor);
		tclt->vendor.reserved = 0;
		tclt->vendor.version_major = TPM_20_LOG_VI_MAJOR;
		tclt->vendor.version_minor = TPM_20_LOG_VI_MINOR;
		tclt->vendor.magic = htole32(TPM_20_LOG_VI_MAGIC);
		tclt->vendor.max_entries = htole16(MAX_TPM_LOG_ENTRIES);
		tclt->vendor.num_entries = htole16(0);
		tclt->vendor.entry_size = htole32(sizeof(struct tpm_2_log_entry));
	}

	return tclt;
}

void tpm2_log_dump(void)
{
	int i, j;
	struct tpm_2_log_table *tclt;
	int hash_size;
	const char *alg_name;

	tclt = tpm_log_init();
	if (!tclt)
		return;

	hash_size = vb2_digest_size(TPM_MEASURE_ALGO);
	alg_name = vb2_get_hash_algorithm_name(TPM_MEASURE_ALGO);

	printk(BIOS_INFO, "coreboot TPM 2.0 measurements:\n\n");
	for (i = 0; i < le16toh(tclt->vendor.num_entries); i++) {
		struct tpm_2_log_entry *tce = tpm2_log_entry_at(tclt, i);

		if (!tce) {
			printk(BIOS_WARNING, "TPM LOG: malformed entry\n");
			break;
		}

		printk(BIOS_INFO, " PCR-%u ", le32toh(tce->pcr));

		for (j = 0; j < hash_size; j++)
			printk(BIOS_INFO, "%02x", tce->digest[j]);

		printk(BIOS_INFO, " %s [%s]\n", alg_name, tce->data);
	}
	printk(BIOS_INFO, "\n");
}

void tpm2_log_add_table_entry(const char *name, const uint32_t pcr,
			      enum vb2_hash_algorithm digest_algo,
			      const uint8_t *digest,
			      const size_t digest_len)
{
	struct tpm_2_log_table *tclt;
	uint8_t event_name[TPM_20_LOG_DATA_MAX_LENGTH];
	size_t name_len;

	if (!name) {
		printk(BIOS_WARNING, "TPM LOG: entry name not set\n");
		return;
	}

	tclt = tpm_log_init();
	if (!tclt) {
		printk(BIOS_WARNING, "TPM LOG: non-existent!\n");
		return;
	}

	name_len = strnlen(name, sizeof(event_name) - 1);
	memcpy(event_name, name, name_len);
	event_name[name_len++] = '\0';

	tpm2_log_append_event(tclt, pcr, EV_ACTION, digest_algo, digest, digest_len,
			      event_name, name_len);
}

int tpm2_log_get(int entry_idx, int *pcr, const uint8_t **digest_data,
		 enum vb2_hash_algorithm *digest_algo, const char **event_name)
{
	struct tpm_2_log_table *tclt;
	struct tpm_2_log_entry *tce;

	tclt = tpm_log_init();
	if (!tclt)
		return 1;

	if (entry_idx < 0 || entry_idx >= le16toh(tclt->vendor.num_entries))
		return 1;

	tce = tpm2_log_entry_at(tclt, entry_idx);
	if (!tce)
		return 1;

	*pcr = le32toh(tce->pcr);
	*digest_data = tce->digest;
	*digest_algo = TPM_MEASURE_ALGO; /* We validate algorithm on addition */
	*event_name = (char *)tce->data;
	return 0;
}

uint16_t tpm2_log_get_size(const void *log_table)
{
	const struct tpm_2_log_table *tclt = log_table;
	return le16toh(tclt->vendor.num_entries);
}

void tpm2_preram_log_clear(void)
{
	printk(BIOS_INFO, "TPM LOG: clearing the log\n");
	/*
	 * Pre-RAM log is only for internal use and isn't exported anywhere, hence it's header
	 * is not initialized.
	 */
	struct tpm_2_log_table *tclt = (struct tpm_2_log_table *)_tpm_log;
	tclt->vendor.max_entries = htole16(MAX_PRERAM_TPM_LOG_ENTRIES);
	tclt->vendor.num_entries = htole16(0);
}

void tpm2_log_copy_entries(const void *from, void *to)
{
	const struct tpm_2_log_table *from_log = from;
	struct tpm_2_log_table *to_log = to;
	int i;

	for (i = 0; i < le16toh(from_log->vendor.num_entries); i++) {
		const struct tpm_2_log_entry *from_entry = tpm2_log_entry_at(from_log, i);

		if (!from_entry) {
			printk(BIOS_WARNING, "TPM LOG: malformed entry\n");
			return;
		}

		tpm2_log_append_event(to_log, le32toh(from_entry->pcr),
				      le32toh(from_entry->event_type), TPM_MEASURE_ALGO,
				      from_entry->digest, vb2_digest_size(TPM_MEASURE_ALGO),
				      from_entry->data, le32toh(from_entry->data_length));
	}
}
