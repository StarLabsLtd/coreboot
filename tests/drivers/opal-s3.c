/* SPDX-License-Identifier: GPL-2.0-only */
#include <tests/test.h>
#include "../../src/security/tcg/opal_s3/opal_s3_smm.c"
#include "../../src/security/tcg/opal_s3/opal_nvme.c"

/* One PCI configuration page; the production PCI accessors use this mapping. */
static uint32_t config_space[1024];
u8 *const pci_mmconf = (void *)config_space;
static struct smm_pci_resource_info trusted[1];
static struct opal_s3_state retained;
static uint8_t scratch[OPAL_S3_SCRATCH_MIN_BYTES] __aligned(4096);
static bool boot_access;
static unsigned int unlocks;
static u64 request_rax, request_rbx;
static uintptr_t rejected_base;
static struct opal_s3_smm_ctx *mutable_request;

bool smm_region_overlaps_handler(const struct region *region)
{
	return rejected_base && region_offset(region) == rejected_base;
}
int get_apmc_node(u8 cmd)
{
	assert_int_equal(cmd, APM_CNT_OPAL_SVC);
	return 0;
}
int get_save_state_reg(const enum cpu_reg reg, const int node, void *out, const uint8_t length)
{
	assert_int_equal(node, 0);
	assert_int_equal(length, sizeof(u64));
	assert_true(reg == RAX || reg == RBX);
	memcpy(out, reg == RAX ? &request_rax : &request_rbx, length);
	return 0;
}
int set_save_state_reg(const enum cpu_reg reg, const int node, void *in, const uint8_t length)
{
	assert_int_equal(node, 0);
	assert_int_equal(reg, RAX);
	assert_int_equal(length, sizeof(request_rax));
	memcpy(&request_rax, in, length);
	return 0;
}
void smm_get_opal_s3_state_buffer(uintptr_t *base, size_t *size)
{
	*base = (uintptr_t)&retained;
	*size = sizeof(retained);
}
void smm_get_opal_s3_scratch_buffer(uintptr_t *base, size_t *size)
{
	if (mutable_request) {
		/* The SMI has copied its request before it asks for scratch storage. */
		mutable_request->dev = 31;
		mutable_request->password[0] ^= 0xff;
		mutable_request = NULL;
	}
	*base = (uintptr_t)scratch;
	*size = sizeof(scratch);
}
void smm_pci_get_stored_resources(const volatile struct smm_pci_resource_info **slots,
				 size_t *count)
{
	*slots = trusted;
	*count = ARRAY_SIZE(trusted);
}
bool efi_variable_service_boot_access_allowed(void)
{
	return boot_access;
}
u16 pci_s_find_capability(pci_devfn_t dev, u16 capability)
{
	return 0;
}
void udelay(unsigned int microseconds)
{
	fail_msg("Unexpected power-state wait");
}
void timer_monotonic_get(struct mono_time *time)
{
	fail_msg("Unexpected MMIO/poll before trusted-base validation");
}
void mdelay(unsigned int milliseconds)
{
	fail_msg("Unexpected unlock retry");
}
u32 opal_nvme_opal_unlock(pci_devfn_t dev, u16 comid, const u8 *password,
			  u8 length, void *buffer, size_t size, u64 mmio_base, size_t mmio_size)
{
	assert_int_equal(dev, PCI_DEV(0, 0, 0));
	assert_int_equal(pci_read_config32(dev, PCI_BASE_ADDRESS_0), 0xe0000000);
	assert_int_equal(mmio_base, 0xe0000000);
	assert_int_equal(mmio_size, 0x4000);
	assert_int_equal(length, 1);
	assert_int_equal(password[0], 0x5a);
	unlocks++;
	return 0;
}
static struct opal_s3_smm_ctx request(void)
{
	return (struct opal_s3_smm_ctx) {
		.signature = OPAL_S3_SMM_CTX_SIGNATURE,
		.version = OPAL_S3_SMM_CTX_VERSION,
		.size = sizeof(struct opal_s3_smm_ctx),
		.password_len = 1, .password = {0x5a},
	};
}
static int setup(void **unused)
{
	memset(&retained, 0, sizeof(retained));
	memset(config_space, 0, sizeof(config_space));
	trusted[0] = (struct smm_pci_resource_info) {
		.pci_addr = PCI_DEV(0, 0, 0), .vendor_id = 0x1234, .device_id = 0x5678,
		.class_device = 0x0108, .class_prog = 2,
		.resources = {{.index = PCI_BASE_ADDRESS_0, .base = 0xe0000000,
			.size = 0x4000, .flags = IORESOURCE_MEM | IORESOURCE_ASSIGNED}},
	};
	config_space[PCI_VENDOR_ID / 4] = 0x56781234;
	config_space[PCI_CLASS_REVISION / 4] = OPAL_NVME_CLASS << 8;
	config_space[PCI_BASE_ADDRESS_0 / 4] = 0xe0000000;
	boot_access = true;
	unlocks = 0;
	request_rax = 0;
	request_rbx = 0;
	rejected_base = 0;
	mutable_request = NULL;
	return 0;
}
static void trusted_enrollment(void **unused)
{
	struct opal_s3_smm_ctx ctx = request();

	assert_int_equal(opal_s3_set_secret_snapshot(&ctx), 0);
	assert_true(retained.secret[0].valid);
	assert_int_equal(retained.secret[0].nvme_id, 0x56781234);
	assert_int_equal(retained.secret[0].nvme_mmio_size, 0x4000);
	boot_access = false;
	ctx.dev = 1;
	assert_int_equal(opal_s3_set_secret_snapshot(&ctx), 9);
	ctx.dev = 0;
	assert_int_equal(opal_s3_set_secret_snapshot(&ctx), 10);
	opal_s3_clear_secret();
	assert_int_equal(opal_s3_set_secret_snapshot(&ctx), 10);
	assert_false(retained.secret[0].valid);
}
static void reject_unowned_resources(void **unused)
{
	struct opal_s3_smm_ctx ctx = request();

	trusted[0].vendor_id ^= 1;
	assert_int_equal(opal_s3_set_secret_snapshot(&ctx), 9);
	trusted[0].vendor_id ^= 1;
	trusted[0].class_prog = 1;
	assert_int_equal(opal_s3_set_secret_snapshot(&ctx), 9);
	trusted[0].class_prog = 2;
	config_space[PCI_BASE_ADDRESS_0 / 4] = 0x100000;
	assert_int_equal(opal_s3_set_secret_snapshot(&ctx), 9);
	config_space[PCI_BASE_ADDRESS_0 / 4] = 0xe0000000;
	trusted[0].resources[0].size = 0x1000;
	assert_int_equal(opal_s3_set_secret_snapshot(&ctx), 9);
	assert_false(retained.secret[0].valid);
}
static void restore_owned_resume_resources(void **unused)
{
	struct opal_s3_smm_ctx ctx = request();

	assert_int_equal(opal_s3_set_secret_snapshot(&ctx), 0);
	opal_s3_arm_for_s3();
	/* A different live BAR must not redirect the privileged transport. */
	config_space[PCI_BASE_ADDRESS_0 / 4] = 0x100000;
	assert_int_equal(opal_s3_unlock_if_armed(), 0);
	assert_int_equal(unlocks, 1);
	assert_int_equal(config_space[PCI_BASE_ADDRESS_0 / 4], 0xe0000000);
	opal_s3_arm_for_s3();
	config_space[PCI_VENDOR_ID / 4] ^= 1;
	config_space[PCI_BASE_ADDRESS_0 / 4] = 0x100000;
	assert_int_equal(opal_s3_unlock_if_armed(), 9);
	assert_int_equal(unlocks, 1);
	assert_int_equal(config_space[PCI_BASE_ADDRESS_0 / 4], 0x100000);
}
static void restore_high_bar(void **unused)
{
	config_space[PCI_BASE_ADDRESS_0 / 4] = 0;
	assert_true(opal_s3_restore_nvme_bar0_if_needed(PCI_DEV(0, 0, 0),
						      PCI_BASE_ADDRESS_MEM_LIMIT_64, 1));
	assert_int_equal(config_space[PCI_BASE_ADDRESS_0 / 4], PCI_BASE_ADDRESS_MEM_LIMIT_64);
	assert_int_equal(config_space[(PCI_BASE_ADDRESS_0 + 4) / 4], 1);
}
static void reject_changed_base_before_mmio(void **unused)
{
	struct opal_nvme nvme;

	config_space[PCI_COMMAND / 4] = PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	assert_int_equal(opal_nvme_init(&nvme, PCI_DEV(0, 0, 0), scratch, sizeof(scratch),
				       0xf0000000, 0x4000), -1);
	assert_int_equal(config_space[PCI_COMMAND / 4] &
			 (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER), 0);
}

static u64 submit_secret(uintptr_t address)
{
	request_rax = APM_CNT_OPAL_SVC | (u64)OPAL_SMM_SUBCMD_SET_SECRET << 8;
	request_rbx = address;
	assert_int_equal(opal_s3_smi_apmc(APM_CNT_OPAL_SVC), 1);
	return request_rax;
}

static void service_validates_and_snapshots_request(void **unused)
{
	struct opal_s3_smm_ctx ctx = request();

	assert_int_equal(submit_secret(0), 1);
	assert_int_equal(submit_secret(UINTPTR_MAX - sizeof(ctx) + 2), 1);
	rejected_base = (uintptr_t)&ctx;
	assert_int_equal(submit_secret((uintptr_t)&ctx), 2);
	assert_false(retained.secret[0].valid);
	rejected_base = 0;
	mutable_request = &ctx;
	assert_int_equal(submit_secret((uintptr_t)&ctx), 0);
	assert_int_equal(ctx.dev, 31);
	assert_int_equal(ctx.password[0], 0xa5);
	assert_true(retained.secret[0].valid);
	assert_int_equal(retained.secret[0].dev, 0);
	assert_int_equal(retained.secret[0].password[0], 0x5a);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(trusted_enrollment, setup),
		cmocka_unit_test_setup(reject_unowned_resources, setup),
		cmocka_unit_test_setup(restore_owned_resume_resources, setup),
		cmocka_unit_test_setup(reject_changed_base_before_mmio, setup),
		cmocka_unit_test_setup(restore_high_bar, setup),
		cmocka_unit_test_setup(service_validates_and_snapshots_request, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
