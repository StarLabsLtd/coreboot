/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cdk2/capsule_runtime.h>
#include "capsule_delivery_policy_fixture.h"

#undef assert
#define assert(condition)                  \
	do {                               \
		if (!(condition))          \
			__builtin_abort(); \
	} while (0)

static EFI_STATUS supported(const struct cdk2_capsule_header *capsule, void *context)
{
	(void)capsule;
	(void)context;
	return EFI_SUCCESS;
}

int main(void)
{
	struct test_capsule_delivery_hobs hobs;
	struct cdk2_capsule_delivery_policy delivery;
	struct cdk2_capsule_policy runtime;
	struct cdk2_capsule_header capsule = {
		.header_size = sizeof(capsule),
		.image_size = 1024,
		.flags = CDK2_CAPSULE_PERSIST | CDK2_CAPSULE_POPULATE,
	};
	const struct cdk2_capsule_header *capsules[] = {&capsule};
	UINT64 maximum;
	uint32_t reset;
	EFI_STATUS status;

	for (uint32_t transports = 0; transports <= 3; transports++) {
		test_capsule_delivery_hobs_init(&hobs, transports, transports ? 4096 : 0,
						transports ? 8192 : 0);
		assert(cdk2_capsule_delivery_policy_from_hobs(&hobs, &delivery) == EFI_SUCCESS);
		assert(delivery.allowed_transports == transports);
		assert(delivery.max_nonpopulate == (transports ? 4096U : 0U));
		assert(delivery.max_populate == (transports ? 8192U : 0U));
		runtime = (struct cdk2_capsule_policy){
			.max_nonpopulate = delivery.max_nonpopulate,
			.max_populate = delivery.max_populate,
			.in_ram = (transports & CB_CAPSULE_DELIVERY_RAM) != 0,
			.persist = transports != 0,
		};
		status = cdk2_capsule_query(capsules, 1, &runtime, supported, NULL, &maximum,
					    &reset);
		assert(status == ((transports & CB_CAPSULE_DELIVERY_RAM) ? EFI_SUCCESS :
									   EFI_UNSUPPORTED));
		if (!EFI_ERROR(status))
			assert(maximum == 8192 && reset == 1);
	}
	for (unsigned int mutation = 0; mutation < 12; mutation++) {
		test_capsule_delivery_hobs_init(&hobs, 3, 4096, 8192);
		switch (mutation) {
		case 0:
			hobs.policy.tag++;
			break;
		case 1:
			hobs.policy.size--;
			break;
		case 2:
			hobs.policy.revision++;
			break;
		case 3:
			hobs.policy.header_size--;
			break;
		case 4:
			hobs.policy.allowed_transports |= 4;
			break;
		case 5:
			hobs.policy.reserved[0] = 1;
			break;
		case 6:
			hobs.policy.reserved[1] = 1;
			break;
		case 7:
			hobs.policy.max_nonpopulate.lo = 0;
			break;
		case 8:
			hobs.policy.max_nonpopulate.hi = 1;
			break;
		case 9:
			hobs.policy.max_populate.hi = 1;
			break;
		case 10:
			hobs.policy.allowed_transports = 0;
			break;
		case 11:
			hobs.policy_hob.header.hob_length -= 8;
			break;
		}
		delivery = (struct cdk2_capsule_delivery_policy){3, 4096, 8192};
		assert(cdk2_capsule_delivery_policy_from_hobs(&hobs, &delivery) ==
		       EFI_COMPROMISED_DATA);
		assert(!delivery.allowed_transports && !delivery.max_nonpopulate &&
		       !delivery.max_populate);
	}
	assert(cdk2_capsule_delivery_policy_from_hobs(NULL, &delivery) == EFI_NOT_FOUND);
	assert(!delivery.allowed_transports && !delivery.max_nonpopulate &&
	       !delivery.max_populate);
	test_capsule_delivery_hobs_init(&hobs, 3, 4096, 0);
	assert(cdk2_capsule_delivery_policy_from_hobs(&hobs, &delivery) == EFI_SUCCESS);
	assert(delivery.max_populate == 0);
	test_capsule_delivery_hobs_init(&hobs, CB_CAPSULE_DELIVERY_RAM, 1, UINT32_MAX);
	assert(cdk2_capsule_delivery_policy_from_hobs(&hobs, &delivery) == EFI_SUCCESS);
	assert(delivery.max_nonpopulate == 1 && delivery.max_populate == UINT32_MAX);
	hobs.policy_hob.name.data1++;
	assert(cdk2_capsule_delivery_policy_from_hobs(&hobs, &delivery) == EFI_NOT_FOUND);
	{
		struct {
			EFI_HOB_HANDOFF_INFO_TABLE handoff;
			EFI_HOB_GUID_TYPE first;
			struct cb_capsule_delivery_policy first_policy;
			EFI_HOB_GUID_TYPE second;
			struct cb_capsule_delivery_policy second_policy;
			EFI_HOB_GENERIC_HEADER end;
		} duplicate __aligned(8);

		test_capsule_delivery_hobs_init(&hobs, 3, 4096, 8192);
		memset(&duplicate, 0, sizeof(duplicate));
		duplicate.handoff = hobs.handoff;
		duplicate.handoff.efi_memory_bottom = (UINTN)&duplicate;
		duplicate.handoff.efi_memory_top = (UINTN)(&duplicate + 1);
		duplicate.handoff.efi_free_memory_bottom = (UINTN)(&duplicate + 1);
		duplicate.handoff.efi_free_memory_top = (UINTN)(&duplicate + 1);
		duplicate.handoff.efi_end_of_hob_list = (UINTN)&duplicate.end;
		duplicate.first = duplicate.second = hobs.policy_hob;
		duplicate.first_policy = duplicate.second_policy = hobs.policy;
		duplicate.end = hobs.end;
		assert(cdk2_capsule_delivery_policy_from_hobs(&duplicate, &delivery) ==
		       EFI_COMPROMISED_DATA);
		assert(!delivery.allowed_transports && !delivery.max_nonpopulate &&
		       !delivery.max_populate);
	}
	return 0;
}
