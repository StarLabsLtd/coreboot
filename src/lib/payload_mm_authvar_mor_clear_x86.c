/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_clear_x86.h>
#include <commonlib/helpers.h>
#include <cpu/x86/cache.h>
#include <cpu/x86/cr.h>
#include <limits.h>
#include <string.h>

#if !ENV_X86
#error "The MOR clear x86 backend requires x86"
#endif

#define X86_CR0_PG_BIT ((uintptr_t)1U << 31)
#define X86_CR4_PAE_BIT ((uintptr_t)1U << 5)

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool ranges_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (!object_valid(first, first_size, 1) ||
	    !object_valid(second, second_size, 1))
		return true;
	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return !combined;
}

static bool paging_active_bits(uintptr_t cr0, uintptr_t cr4)
{
	return (cr0 & X86_CR0_PG_BIT) || (cr4 & X86_CR4_PAE_BIT);
}

static bool excluded_range(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	uintptr_t base, size_t size)
{
	if (!size || base > UINTPTR_MAX - (size - 1U))
		return false;
	for (size_t index = 0; index < plan->span_count; index++) {
		const struct payload_mm_authvar_mor_grant_span *span =
			&plan->spans[index];

		if (span->span_class == PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED &&
		    base >= span->base && size <= span->size &&
		    base - span->base <= span->size - size)
			return true;
	}
	return false;
}

static bool arch_valid(
	const struct payload_mm_authvar_mor_clear_x86_arch_ops *arch)
{
	return arch->page_tables_init && arch->map_2m && arch->paging_disable &&
		arch->paging_active && arch->clflush_available &&
		arch->clflush_range && arch->memory_fence;
}

static bool configuration_matches(
	const struct payload_mm_authvar_mor_clear_x86_backend *backend,
	uintptr_t page_tables, uintptr_t aperture,
	const struct payload_mm_authvar_mor_clear_x86_arch_ops *arch)
{
	return backend->revision == PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_REVISION &&
		backend->size == sizeof(*backend) &&
		backend->page_tables == page_tables && backend->aperture == aperture &&
		!memcmp(&backend->arch, arch, sizeof(*arch)) && backend->prepared &&
		bytes_zero(backend->reserved, sizeof(backend->reserved));
}

static void restore_configuration(
	struct payload_mm_authvar_mor_clear_x86_backend *backend,
	uintptr_t page_tables, uintptr_t aperture,
	const struct payload_mm_authvar_mor_clear_x86_arch_ops *arch)
{
	backend->revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_REVISION;
	backend->size = sizeof(*backend);
	backend->page_tables = page_tables;
	backend->aperture = aperture;
	backend->arch = *arch;
	backend->prepared = true;
	memset(backend->reserved, 0, sizeof(backend->reserved));
}

static bool unmapped_healthy(
	const struct payload_mm_authvar_mor_clear_x86_backend *backend)
{
	return !backend->mapped && !backend->poisoned &&
		!backend->mapped_physical && !backend->mapping &&
		!backend->mapped_size;
}

static enum cb_err disable_and_verify(
	struct payload_mm_authvar_mor_clear_x86_backend *backend)
{
	const struct payload_mm_authvar_mor_clear_x86_arch_ops arch = backend->arch;
	const uintptr_t page_tables = backend->page_tables;
	const uintptr_t aperture = backend->aperture;
	const uint64_t mapped_physical = backend->mapped_physical;
	const uintptr_t mapping = backend->mapping;
	const size_t mapped_size = backend->mapped_size;
	const bool mapped = backend->mapped;
	const bool poisoned = backend->poisoned;
	bool active;
	bool changed;

	arch.paging_disable();
	active = arch.paging_active();
	changed = !configuration_matches(backend, page_tables, aperture, &arch) ||
		backend->mapped_physical != mapped_physical ||
		backend->mapping != mapping || backend->mapped_size != mapped_size ||
		backend->mapped != mapped || backend->poisoned != poisoned;
	restore_configuration(backend, page_tables, aperture, &arch);
	backend->poisoned = poisoned;
	backend->mapped = false;
	backend->mapped_physical = 0;
	backend->mapping = 0;
	backend->mapped_size = 0;
	if (active || changed) {
		backend->poisoned = true;
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static enum cb_err backend_fail(
	struct payload_mm_authvar_mor_clear_x86_backend *backend)
{
	backend->poisoned = true;
	(void)disable_and_verify(backend);
	backend->poisoned = true;
	return CB_ERR;
}

static enum cb_err map_window(void *context, uint64_t physical, size_t size,
	void **mapping)
{
	struct payload_mm_authvar_mor_clear_x86_backend *backend = context;
	const uint64_t page_base = ALIGN_DOWN(physical, PAE_VMEM_SIZE);
	const size_t offset = physical - page_base;
	const struct payload_mm_authvar_mor_clear_x86_arch_ops arch = backend->arch;
	const uintptr_t page_tables = backend->page_tables;
	const uintptr_t aperture = backend->aperture;
	enum cb_err initialized;
	bool active;

	if (!object_valid(mapping, sizeof(*mapping), _Alignof(*mapping)))
		return backend_fail(backend);
	if (ranges_overlap(mapping, sizeof(*mapping), backend, sizeof(*backend)) ||
	    ranges_overlap(mapping, sizeof(*mapping), (void *)page_tables,
		PAE_PGTL_SIZE) ||
	    ranges_overlap(mapping, sizeof(*mapping), (void *)aperture,
		PAE_VMEM_SIZE))
		return backend_fail(backend);
	*mapping = NULL;
	if (!backend->prepared || backend->mapped || backend->poisoned || !size ||
	    physical > UINT64_MAX - (size - 1U) ||
	    offset > PAE_VMEM_SIZE || size > PAE_VMEM_SIZE - offset)
		return backend_fail(backend);
	initialized = arch.page_tables_init((void *)page_tables);
	active = arch.paging_active();
	if (initialized != CB_SUCCESS || !active ||
	    !configuration_matches(backend, page_tables, aperture, &arch) ||
	    !unmapped_healthy(backend)) {
		restore_configuration(backend, page_tables, aperture, &arch);
		return backend_fail(backend);
	}
	arch.map_2m((void *)page_tables, page_base, (void *)aperture);
	active = arch.paging_active();
	if (!active ||
	    !configuration_matches(backend, page_tables, aperture, &arch) ||
	    !unmapped_healthy(backend)) {
		restore_configuration(backend, page_tables, aperture, &arch);
		return backend_fail(backend);
	}
	backend->mapped_physical = physical;
	backend->mapping = backend->aperture + offset;
	backend->mapped_size = size;
	backend->mapped = true;
	*mapping = (void *)backend->mapping;
	return CB_SUCCESS;
}

static bool exact_mapping(
	const struct payload_mm_authvar_mor_clear_x86_backend *backend,
	uint64_t physical, const volatile void *mapping, size_t size)
{
	return backend->prepared && backend->mapped && !backend->poisoned &&
		physical == backend->mapped_physical &&
		(uintptr_t)mapping == backend->mapping && size == backend->mapped_size;
}

static enum cb_err cache_writeback_invalidate(void *context, uint64_t physical,
	const volatile void *mapping, size_t size)
{
	struct payload_mm_authvar_mor_clear_x86_backend *backend = context;
	const struct payload_mm_authvar_mor_clear_x86_arch_ops arch = backend->arch;
	const uintptr_t page_tables = backend->page_tables;
	const uintptr_t aperture = backend->aperture;
	bool available;

	if (!exact_mapping(backend, physical, mapping, size))
		return backend_fail(backend);
	available = arch.clflush_available();
	if (!available ||
	    !configuration_matches(backend, page_tables, aperture, &arch)) {
		restore_configuration(backend, page_tables, aperture, &arch);
		return backend_fail(backend);
	}
	arch.clflush_range((uintptr_t)mapping, size);
	if (!exact_mapping(backend, physical, mapping, size) ||
	    !configuration_matches(backend, page_tables, aperture, &arch)) {
		restore_configuration(backend, page_tables, aperture, &arch);
		return backend_fail(backend);
	}
	return CB_SUCCESS;
}

static enum cb_err fence(void *context)
{
	struct payload_mm_authvar_mor_clear_x86_backend *backend = context;
	const struct payload_mm_authvar_mor_clear_x86_arch_ops arch = backend->arch;
	const uintptr_t page_tables = backend->page_tables;
	const uintptr_t aperture = backend->aperture;

	if (!backend->prepared || !backend->mapped || backend->poisoned)
		return backend_fail(backend);
	arch.memory_fence();
	if (!backend->mapped || backend->poisoned ||
	    !configuration_matches(backend, page_tables, aperture, &arch)) {
		restore_configuration(backend, page_tables, aperture, &arch);
		return backend_fail(backend);
	}
	return CB_SUCCESS;
}

static enum cb_err unmap_window(void *context, uint64_t physical, void *mapping,
	size_t size)
{
	struct payload_mm_authvar_mor_clear_x86_backend *backend = context;
	const bool exact = exact_mapping(backend, physical, mapping, size);
	const enum cb_err disabled = disable_and_verify(backend);

	if (!exact || disabled != CB_SUCCESS) {
		backend->poisoned = true;
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static enum cb_err prepare_with_ops(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	void *page_tables, void *aperture,
	struct payload_mm_authvar_mor_clear_x86_backend *backend,
	struct payload_mm_authvar_mor_clear_executor_ops *ops,
	const struct payload_mm_authvar_mor_clear_x86_arch_ops *arch)
{
	struct payload_mm_authvar_mor_clear_plan plan_copy;
	struct payload_mm_authvar_mor_clear_x86_arch_ops arch_copy;
	const bool backend_valid = object_valid(backend, sizeof(*backend),
		_Alignof(*backend));
	const bool ops_valid = object_valid(ops, sizeof(*ops), _Alignof(*ops));

	if (backend_valid)
		memset(backend, 0, sizeof(*backend));
	if (ops_valid)
		memset(ops, 0, sizeof(*ops));
	if (!backend_valid || !ops_valid ||
	    !object_valid(plan, sizeof(*plan), _Alignof(*plan)) ||
	    !object_valid(page_tables, PAE_PGTL_SIZE, PAE_PGTL_ALIGN) ||
	    !object_valid(aperture, PAE_VMEM_SIZE, PAE_VMEM_ALIGN) ||
	    !object_valid(arch, sizeof(*arch), _Alignof(*arch)))
		return CB_ERR_ARG;
	if (ranges_overlap(plan, sizeof(*plan), backend, sizeof(*backend)) ||
	    ranges_overlap(plan, sizeof(*plan), ops, sizeof(*ops)) ||
	    ranges_overlap(backend, sizeof(*backend), ops, sizeof(*ops)) ||
	    ranges_overlap(page_tables, PAE_PGTL_SIZE, aperture, PAE_VMEM_SIZE) ||
	    ranges_overlap(page_tables, PAE_PGTL_SIZE, backend, sizeof(*backend)) ||
	    ranges_overlap(page_tables, PAE_PGTL_SIZE, ops, sizeof(*ops)) ||
	    ranges_overlap(aperture, PAE_VMEM_SIZE, backend, sizeof(*backend)) ||
	    ranges_overlap(aperture, PAE_VMEM_SIZE, ops, sizeof(*ops)))
		return CB_ERR_ARG;
	memcpy(&plan_copy, plan, sizeof(plan_copy));
	memcpy(&arch_copy, arch, sizeof(arch_copy));
	if (payload_mm_authvar_mor_clear_plan_validate(&plan_copy) != CB_SUCCESS ||
	    !arch_valid(&arch_copy) ||
	    !excluded_range(&plan_copy, (uintptr_t)page_tables,
		PAE_PGTL_SIZE) ||
	    !excluded_range(&plan_copy, (uintptr_t)aperture,
		PAE_VMEM_SIZE) ||
	    !excluded_range(&plan_copy, (uintptr_t)backend,
		sizeof(*backend)) ||
	    !excluded_range(&plan_copy, (uintptr_t)ops, sizeof(*ops)) ||
	    memcmp(&plan_copy, plan, sizeof(plan_copy)) ||
	    memcmp(&arch_copy, arch, sizeof(arch_copy)))
		return CB_ERR;
	backend->revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_REVISION;
	backend->size = sizeof(*backend);
	backend->page_tables = (uintptr_t)page_tables;
	backend->aperture = (uintptr_t)aperture;
	backend->arch = arch_copy;
	backend->prepared = true;
	if (disable_and_verify(backend) != CB_SUCCESS)
		return backend_fail(backend);
	const bool flush_available = arch_copy.clflush_available();
	if (!flush_available ||
	    !configuration_matches(backend, (uintptr_t)page_tables,
		(uintptr_t)aperture, &arch_copy) ||
	    !unmapped_healthy(backend) ||
	    memcmp(&plan_copy, plan, sizeof(plan_copy)) ||
	    memcmp(&arch_copy, arch, sizeof(arch_copy)) ||
	    !bytes_zero(ops, sizeof(*ops)))
		return backend_fail(backend);
	ops->context = backend;
	ops->window_bytes = PAE_VMEM_SIZE;
	ops->map_window = map_window;
	ops->cache_writeback_invalidate = cache_writeback_invalidate;
	ops->fence = fence;
	ops->unmap_window = unmap_window;
	return CB_SUCCESS;
}

#if ENV_TEST
bool payload_mm_authvar_mor_clear_x86_paging_active_test(
	uintptr_t cr0, uintptr_t cr4)
{
	return paging_active_bits(cr0, cr4);
}

enum cb_err payload_mm_authvar_mor_clear_x86_prepare_with_ops(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	void *page_tables, void *aperture,
	struct payload_mm_authvar_mor_clear_x86_backend *backend,
	struct payload_mm_authvar_mor_clear_executor_ops *ops,
	const struct payload_mm_authvar_mor_clear_x86_arch_ops *arch)
{
	return prepare_with_ops(plan, page_tables, aperture, backend, ops, arch);
}
#endif

#if !ENV_TEST
static enum cb_err production_page_tables_init(void *page_tables)
{
	return init_pae_pagetables(page_tables) ? CB_ERR : CB_SUCCESS;
}

static void production_map_2m(void *page_tables, uint64_t physical,
	void *aperture)
{
	pae_map_2M_page(page_tables, physical, aperture);
}

static bool production_paging_active(void)
{
	return paging_active_bits(read_cr0(), read_cr4());
}

static void production_memory_fence(void)
{
	asm volatile("mfence" ::: "memory");
}

static const struct payload_mm_authvar_mor_clear_x86_arch_ops production_arch = {
	.page_tables_init = production_page_tables_init,
	.map_2m = production_map_2m,
	.paging_disable = paging_disable_pae,
	.paging_active = production_paging_active,
	.clflush_available = clflush_supported,
	.clflush_range = clflush_region,
	.memory_fence = production_memory_fence,
};

enum cb_err payload_mm_authvar_mor_clear_x86_prepare(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	void *page_tables, void *aperture,
	struct payload_mm_authvar_mor_clear_x86_backend *backend,
	struct payload_mm_authvar_mor_clear_executor_ops *ops)
{
	return prepare_with_ops(plan, page_tables, aperture, backend, ops,
		&production_arch);
}
#endif
