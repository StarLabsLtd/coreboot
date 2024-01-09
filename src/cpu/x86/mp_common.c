/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/cpu.h>
#include <cpu/x86/mp.h>
#include <smp/atomic.h>
#include <string.h>
#include <timer.h>
#include "mp_internal.h"

struct mp_callback {
	void (*func)(void *);
	void *arg;
	int logical_cpu_number;
};

static struct mp_callback *ap_callbacks[CONFIG_MAX_CPUS];

enum AP_STATUS {
	/* AP takes the task but not yet finishes */
	AP_BUSY = 1,
	/* AP finishes the task or no task to run yet */
	AP_NOT_BUSY
};

static atomic_t ap_status[CONFIG_MAX_CPUS];

static struct mp_callback *read_callback(struct mp_callback **slot)
{
	struct mp_callback *ret;

	asm volatile ("mov	%1, %0\n"
		: "=r" (ret)
		: "m" (*slot)
		: "memory"
	);
	return ret;
}

static void store_callback(struct mp_callback **slot, struct mp_callback *val)
{
	asm volatile ("mov	%1, %0\n"
		: "=m" (*slot)
		: "r" (val)
		: "memory"
	);
}

static enum cb_err run_ap_work(struct mp_callback *val, long expire_us, bool wait_ap_finish)
{
	int i;
	int cpus_accepted, cpus_finish;
	struct stopwatch sw;
	int cur_cpu;

	if (ENV_RAMSTAGE && !CONFIG(PARALLEL_MP_AP_WORK)) {
		printk(BIOS_ERR, "APs already parked. PARALLEL_MP_AP_WORK not selected.\n");
		return CB_ERR;
	}

	cur_cpu = cpu_index();

	if (cur_cpu < 0) {
		printk(BIOS_ERR, "Invalid CPU index.\n");
		return CB_ERR;
	}

	/* Signal to all the APs to run the func. */
	for (i = 0; i < ARRAY_SIZE(ap_callbacks); i++) {
		if (cur_cpu == i)
			continue;
		store_callback(&ap_callbacks[i], val);
	}
	mfence();

	/* Wait for all the APs to signal back that call has been accepted. */
	if (expire_us > 0)
		stopwatch_init_usecs_expire(&sw, expire_us);

	do {
		cpus_accepted = 0;
		cpus_finish = 0;

		for (i = 0; i < ARRAY_SIZE(ap_callbacks); i++) {
			if (cur_cpu == i)
				continue;

			if (read_callback(&ap_callbacks[i]) == NULL) {
				cpus_accepted++;
				/* Only increase cpus_finish if AP took the task and not busy */
				if (atomic_read(&ap_status[i]) == AP_NOT_BUSY)
					cpus_finish++;
			}
		}

		/*
		 * if wait_ap_finish is true, need to make sure all CPUs finish task and return
		 * else just need to make sure all CPUs take task
		 */
		if (cpus_accepted == mp_internal_get_num_cpus() - 1)
			if (!wait_ap_finish || (cpus_finish == mp_internal_get_num_cpus() - 1))
				return CB_SUCCESS;

	} while (expire_us <= 0 || !stopwatch_expired(&sw));

	printk(BIOS_CRIT, "CRITICAL ERROR: AP call expired. %d/%d CPUs accepted.\n",
		cpus_accepted, mp_internal_get_num_cpus() - 1);
	return CB_ERR;
}

void mp_internal_ap_ready_for_instruction(int cur_cpu)
{
	/* Init ap_status[cur_cpu] to AP_NOT_BUSY and ready to take job */
	atomic_set(&ap_status[cur_cpu], AP_NOT_BUSY);
}

void mp_internal_ap_check_for_instruction(int cur_cpu)
{
	struct mp_callback lcb;
	struct mp_callback **per_cpu_slot;

	per_cpu_slot = &ap_callbacks[cur_cpu];

	struct mp_callback *cb = read_callback(per_cpu_slot);
	if (cb == NULL)
		return;

	/*
	 * Set ap_status to AP_BUSY before store_callback(per_cpu_slot, NULL).
	 * it's to let BSP know APs take tasks and busy to avoid race condition.
	 */
	atomic_set(&ap_status[cur_cpu], AP_BUSY);

	/* Copy to local variable before signaling consumption. */
	memcpy(&lcb, cb, sizeof(lcb));
	mfence();
	store_callback(per_cpu_slot, NULL);

	if (lcb.logical_cpu_number == MP_RUN_ON_ALL_CPUS ||
			(cur_cpu == lcb.logical_cpu_number))
		lcb.func(lcb.arg);

	atomic_set(&ap_status[cur_cpu], AP_NOT_BUSY);
}

enum cb_err mp_run_on_aps(void (*func)(void *), void *arg, int logical_cpu_num,
		long expire_us)
{
	struct mp_callback lcb = { .func = func, .arg = arg,
				.logical_cpu_number = logical_cpu_num};
	return run_ap_work(&lcb, expire_us, false);
}

static enum cb_err mp_run_on_aps_and_wait_for_complete(void (*func)(void *), void *arg,
		int logical_cpu_num, long expire_us)
{
	struct mp_callback lcb = { .func = func, .arg = arg,
				.logical_cpu_number = logical_cpu_num};
	return run_ap_work(&lcb, expire_us, true);
}

enum cb_err mp_run_on_all_aps(void (*func)(void *), void *arg, long expire_us,
			      bool run_parallel)
{
	int ap_index, bsp_index;

	if (run_parallel)
		return mp_run_on_aps(func, arg, MP_RUN_ON_ALL_CPUS, expire_us);

	bsp_index = cpu_index();

	for (ap_index = 0; ap_index < mp_internal_get_num_cpus(); ap_index++) {
		/* skip if BSP */
		if (ap_index == bsp_index)
			continue;
		if (mp_run_on_aps(func, arg, ap_index, expire_us) != CB_SUCCESS)
			return CB_ERR;
	}

	return CB_SUCCESS;
}

enum cb_err mp_run_on_all_cpus(void (*func)(void *), void *arg)
{
	/* Run on BSP first. */
	func(arg);

	/* For up to 1 second for AP to finish previous work. */
	return mp_run_on_aps(func, arg, MP_RUN_ON_ALL_CPUS, 1000 * USECS_PER_MSEC);
}

enum cb_err mp_run_on_all_cpus_synchronously(void (*func)(void *), void *arg)
{
	/* Run on BSP first. */
	func(arg);

	/* For up to 1 second per AP (console can be slow) to finish previous work. */
	return mp_run_on_aps_and_wait_for_complete(func, arg, MP_RUN_ON_ALL_CPUS,
						   1000 * USECS_PER_MSEC * mp_internal_get_num_cpus() - 1);
}
