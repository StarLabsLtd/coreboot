/* SPDX-License-Identifier: GPL-2.0-only */

/* TODO: Update for Phoenix */

#include <amdblocks/cpu.h>
#include <cpu/amd/msr.h>
#include <cpu/cpu.h>
#include <device/device.h>
#include <halt.h>
#include <smp/node.h>
#include <soc/amd/common/block/psp/psp_def.h>
#include <soc/cpu.h>

static_assert(CONFIG_MAX_CPUS == 16, "Do not override MAX_CPUS. To reduce the number of "
	"available cores, use the downcore_mode and disable_smt devicetree settings instead.");

static void phoenix_cpu_init(struct device *dev)
{
	if (CONFIG(SOC_AMD_PHOENIX_OPENSIL) && boot_cpu()) {
		msr_t base = rdmsr(PSP_ADDR_MSR);
		if (!base.raw) {
			base.raw = get_ccp_mmio_base();
			if (!base.raw)
				die("No enabled, locked CCP base for PSP_ADDR_MSR\n");
			wrmsr(PSP_ADDR_MSR, base);
			if (rdmsr(PSP_ADDR_MSR).raw != base.raw)
				die("PSP_ADDR_MSR readback failed\n");
		}
	}
	amd_cpu_init(dev);
}

static struct device_operations cpu_dev_ops = {
	.init = phoenix_cpu_init,
};

static struct cpu_device_id cpu_table[] = {
	{ X86_VENDOR_AMD, PHOENIX_A0_CPUID, CPUID_ALL_STEPPINGS_MASK },
	{ X86_VENDOR_AMD, PHOENIX_HP1_CPUID, CPUID_ALL_STEPPINGS_MASK },
	{ X86_VENDOR_AMD, PHOENIX2_A0_CPUID, CPUID_ALL_STEPPINGS_MASK },
	CPU_TABLE_END
};

static const struct cpu_driver zen_2_3 __cpu_driver = {
	.ops      = &cpu_dev_ops,
	.id_table = cpu_table,
};
