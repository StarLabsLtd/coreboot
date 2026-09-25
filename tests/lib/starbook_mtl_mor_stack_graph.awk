# SPDX-License-Identifier: GPL-2.0-only

function add_edge(from, to)
{
	if (!edge_seen[from SUBSEP to]++)
		edge[from, ++edge_count[from]] = to
}

function find_node(name, source, optional,    node, found)
{
	for (node in function_name) {
		if (function_name[node] != name &&
		    index(function_name[node], name ".") != 1)
			continue
		if (source != "" && index(location[node], source) == 0 &&
		    index(node, source) == 0)
			continue
		if (found != "" && found != node) {
			print "ERROR: ambiguous stack node " source ":" name > "/dev/stderr"
			failed = 1
			return ""
		}
		found = node
	}
	if (found == "" && !optional) {
		print "ERROR: missing stack node " source ":" name > "/dev/stderr"
		failed = 1
	}
	return found
}

function bind(caller, caller_source, target, target_source, optional,
	caller_node, target_node)
{
	if (caller == omit_caller)
		return
	if (target == omit_target)
		return
	caller_node = find_node(caller, caller_source, optional)
	target_node = find_node(target, target_source, optional)
	if (caller_node == "" || target_node == "")
		return
	add_edge(caller_node, target_node)
	manifest_target_name[caller_node, target] = 1
	if (!manifest_target[caller_node, target_node]++)
		manifest_target_count[caller_node]++
}

function bind_cap(caller, caller_source, cap, optional, caller_node)
{
	if (caller == omit_caller)
		return
	if (cap == omit_target)
		return
	caller_node = find_node(caller, caller_source, optional)
	if (caller_node == "")
		return
	add_edge(caller_node, cap)
	manifest_target_name[caller_node, cap] = 1
	if (!manifest_target[caller_node, cap]++)
		manifest_target_count[caller_node]++
}

function bind_direct(caller, caller_source, target, target_source,
	caller_node, target_node, edge_index, found)
{
	caller_node = find_node(caller, caller_source)
	target_node = find_node(target, target_source)
	if (caller_node == "" || target_node == "")
		return
	for (edge_index = 1; edge_index <= edge_count[caller_node]; edge_index++)
		if (function_name[canonical(edge[caller_node, edge_index])] == target)
			found++
	if (found != 1) {
		print "ERROR: strong provider direct-edge contract mismatch" > "/dev/stderr"
		failed = 1
	}
	# The selected artifact edge names the weak definition. Add the separately
	# compiled strong provider as the conservative contract target.
	add_edge(caller_node, target_node)
}

function permit_sites(caller, source, positions, target_set, optional,
	caller_node, position_count, positions_array, position_index, site_index,
	wanted, found, target_count, targets, target_index, target)
{
	caller_node = find_node(caller, source, optional)
	if (caller_node == "")
		return
	if (target_set == "") {
		print "ERROR: indirect site has no permitted target set" > "/dev/stderr"
		failed = 1
	}
	if (caller == mutate_site_caller)
		target_set = mutate_site_target
	target_count = split(target_set, targets, "\\|")
	for (target_index = 1; target_index <= target_count; target_index++) {
		target = targets[target_index]
		if (!manifest_target_name[caller_node, target]) {
			print "ERROR: indirect site permits unbound target " target \
				" in " caller_node > "/dev/stderr"
			failed = 1
		}
		permitted_target_name[caller_node, target] = 1
	}
	position_count = split(positions, positions_array, ",")
	for (position_index = 1; position_index <= position_count; position_index++) {
		wanted = source ":" positions_array[position_index]
		found = 0
		for (site_index = 1; site_index <= indirect_count[caller_node]; site_index++) {
			if (indirect_site[caller_node, site_index] != wanted)
				continue
			if (resolved_site[caller_node, site_index])
				continue
			resolved_site[caller_node, site_index] = 1
			permitted_site_targets[caller_node, site_index] = target_set
			found = 1
			break
		}
		if (found != 1) {
			print "ERROR: missing/substituted indirect-site manifest " wanted > "/dev/stderr"
			failed = 1
		}
	}
}

function expect_indirect(caller, source, count, targets, optional, caller_node)
{
	caller_node = find_node(caller, source, optional)
	if (caller_node != "")
		expected_indirect[caller_node] = count
		expected_targets[caller_node] = targets ? targets : 1
}

function canonical(node,    candidate, found, name)
{
	if (node in stack_bytes)
		return node
	name = function_name[node]
	for (candidate in function_name) {
		if (function_name[candidate] != name || !(candidate in stack_bytes))
			continue
		if (found != "" && found != candidate) {
			print "ERROR: ambiguous external stack node " name > "/dev/stderr"
			failed = 1
			return node
		}
		found = candidate
	}
	return found != "" ? found : node
}

# Bound a selected concrete path without following an indirect callback. Add
# the i386 return address at every direct edge; .ci stack bytes describe the
# function frame, not the caller's CALL push. This is used to derive a
# context-specific cap where the region-device graph deliberately dispatches
# through rdev_mmap twice.
function direct_bound(node,    child, child_bound, edge_index, largest)
{
	node = canonical(node)
	if (direct_done[node])
		return direct_total[node]
	if (direct_active[node]) {
		print "ERROR: recursive concrete region path at " node > "/dev/stderr"
		failed = 1
		return 0
	}
	if (!(node in stack_bytes)) {
		print "ERROR: concrete region node has no audited stack frame: " node \
			> "/dev/stderr"
		failed = 1
		return 0
	}
	if (frame_kind[node] ~ /dynamic/ && frame_kind[node] !~ /bounded/) {
		print "ERROR: concrete region path has unbounded frame: " node \
			> "/dev/stderr"
		failed = 1
	}
	direct_active[node] = 1
	for (edge_index = 1; edge_index <= edge_count[node]; edge_index++) {
		child = edge[node, edge_index]
		if (child == "__indirect_call")
			continue
		child_bound = call_return_bytes + direct_bound(child)
		if (child_bound > largest)
			largest = child_bound
	}
	direct_active[node] = 0
	direct_done[node] = 1
	direct_total[node] = stack_bytes[node] + largest
	return direct_total[node]
}

function stack_bound(node,    child, child_bound, edge_index, largest, site_index,
	target_key, target_parts)
{
	node = canonical(node)
	if (done[node])
		return total[node]
	if (active[node]) {
		print "ERROR: recursive reachable stack graph at " node > "/dev/stderr"
		failed = 1
		return 0
	}
	if (!(node in stack_bytes)) {
		print "ERROR: reachable node has no audited stack frame: " node > "/dev/stderr"
		failed = 1
		return 0
	}
	if (frame_kind[node] ~ /dynamic/ && frame_kind[node] !~ /bounded/) {
		print "ERROR: reachable dynamic/unbounded stack frame: " node > "/dev/stderr"
		failed = 1
	}
	if (indirect_count[node] != expected_indirect[node]) {
		print "ERROR: indirect-call contract mismatch in " node \
			" (artifact " indirect_count[node] ", manifest " \
			expected_indirect[node] ")" > "/dev/stderr"
		failed = 1
	}
	if (manifest_target_count[node] != expected_targets[node]) {
		print "ERROR: indirect target-set mismatch in " node \
			" (manifest " manifest_target_count[node] ", expected " \
			expected_targets[node] ")" > "/dev/stderr"
		failed = 1
	}
	for (target_key in manifest_target_name) {
		split(target_key, target_parts, SUBSEP)
		if (target_parts[1] == node &&
		    !permitted_target_name[node, target_parts[2]]) {
			print "ERROR: bound indirect target has no permitted site: " \
				target_parts[2] " in " node > "/dev/stderr"
			failed = 1
		}
	}
	for (site_index = 1; site_index <= indirect_count[node]; site_index++) {
		if (!resolved_site[node, site_index]) {
			print "ERROR: unresolved indirect-call site " \
				indirect_site[node, site_index] " in " node > "/dev/stderr"
			failed = 1
		}
	}
	active[node] = 1
	for (edge_index = 1; edge_index <= edge_count[node]; edge_index++) {
		child = edge[node, edge_index]
		if (child == "__indirect_call")
			continue
		# The selected final ELF supplies only the verified weak provider,
		# which returns false unconditionally. Its success successor is not
		# feasible in the selected fail-closed composition.
		if (!provider_contract && function_name[node] == "mor_before_bootmem" &&
		    function_name[canonical(child)] == "payload_mm_authvar_mor_linear_before_bootmem")
			continue
		child_bound = stack_bound(child)
		if (child_bound > largest)
			largest = child_bound
	}
	active[node] = 0
	done[node] = 1
	total[node] = stack_bytes[node] + largest
	return total[node]
}

/^node: / {
	node = $0
	sub(/^node: \{ title: "/, "", node)
	sub(/".*/, "", node)
	label = $0
	sub(/^.*label: "/, "", label)
	name = label
	sub(/\\n.*/, "", name)
	where = label
	sub(/^[^\\]*\\n/, "", where)
	sub(/\\n.*/, "", where)
	bytes = $0
	if (bytes ~ /\\n[0-9]+ bytes/) {
		sub(/^.*\\n/, "", bytes)
		sub(/ bytes.*/, "", bytes)
		if (!(node in stack_bytes) || bytes + 0 > stack_bytes[node]) {
			stack_bytes[node] = bytes + 0
			function_name[node] = name
			location[node] = where
			kind = $0
			sub(/^.* bytes \(/, "", kind)
			sub(/\).*/, "", kind)
			frame_kind[node] = kind
		}
	} else if (!(node in function_name)) {
		function_name[node] = name
		location[node] = where
	}
	next
}

/^edge: / {
	from = $0
	sub(/^.*sourcename: "/, "", from)
	sub(/".*/, "", from)
	to = $0
	sub(/^.*targetname: "/, "", to)
	sub(/".*/, "", to)
	add_edge(from, to)
	if (to == "__indirect_call") {
		site = $0
		sub(/^.*label: "/, "", site)
		sub(/".*/, "", site)
		indirect_count[from]++
		indirect_site[from, indirect_count[from]] = site
	}
	next
}

END {
	call_return_bytes = 4
	private_cap = "@private_callback"
	stack_bytes[private_cap] = private_callback_bytes + 0
	function_name[private_cap] = private_cap
	location[private_cap] = "audited private-boundary contract"
	region_cap = "@selected_boot_region_mmap_callback"
	xlate_node = find_node("xlate_mmap", "region.c")
	rdev_node = find_node("rdev_mmap", "region.c")
	mem_node = find_node("mdev_mmap", "region.c")
	# The selected fast-SPI path is outer rdev -> xlate -> nested rdev ->
	# memory device. direct_bound(xlate) follows every compiler-retained direct
	# helper, including the nested rdev and xlate_find_window descendants. Add
	# both indirect CALL pushes and the final memory callback path explicitly;
	# this remains sound if either selected tail jump becomes a normal call.
	stack_bytes[region_cap] = call_return_bytes + direct_bound(xlate_node) + \
		call_return_bytes + direct_bound(mem_node)
	function_name[region_cap] = region_cap
	location[region_cap] = "Intel fast-SPI xlate-to-memory mmap boundary"
	region_unmap_cap = "@selected_boot_region_munmap_callback"
	xlate_unmap_node = find_node("xlate_munmap", "region.c")
	stack_bytes[region_unmap_cap] = call_return_bytes + \
		direct_bound(xlate_unmap_node)
	function_name[region_unmap_cap] = region_unmap_cap
	location[region_unmap_cap] = "Intel fast-SPI xlate munmap boundary"

	# Provider glue cannot enter the selected image until a private provider is
	# supplied. Audit it separately from the selected fail-closed image.
	if (provider_contract) {
	bind_direct("mor_before_bootmem", "payload_mm_authvar_mor_linear.c",
		"platform_payload_mm_authvar_mor_linear_ops", "mor_platform.c")
	bind("payload_mm_authvar_mor_linear_before_bootmem", "payload_mm_authvar_mor_linear.c",
		"classify_guard", "mor_platform.c")
	expect_indirect("payload_mm_authvar_mor_linear_before_bootmem",
		"payload_mm_authvar_mor_linear.c", 2, 2)
	bind("payload_mm_authvar_mor_linear_before_bootmem", "payload_mm_authvar_mor_linear.c",
		"reservations_register", "mor_platform.c")
	bind("payload_mm_authvar_mor_linear_after_bootmem", "payload_mm_authvar_mor_linear.c",
		"resolve_binding", "mor_platform.c")
	expect_indirect("payload_mm_authvar_mor_linear_after_bootmem",
		"payload_mm_authvar_mor_linear.c", 2, 2)
	bind("payload_mm_authvar_mor_linear_after_bootmem", "payload_mm_authvar_mor_linear.c",
		"private_complete", "mor_platform.c")
	bind("close_retained_authority", "payload_mm_authvar_mor_linear.c",
		"private_close", "mor_platform.c")
	expect_indirect("close_retained_authority", "payload_mm_authvar_mor_linear.c", 1)
	}

	bind("call_inventory", "payload_mm_authvar_mor_clear_executor.c",
		"executor_inventory_validate", "mor_clear_x86.c")
	expect_indirect("call_inventory", "payload_mm_authvar_mor_clear_executor.c", 1)
	bind("call_dma", "payload_mm_authvar_mor_clear_executor.c",
		"executor_dma_snapshot", "mor_clear_x86.c")
	expect_indirect("call_dma", "payload_mm_authvar_mor_clear_executor.c", 1)
	bind("call_map", "payload_mm_authvar_mor_clear_executor.c",
		"map_window", "payload_mm_authvar_mor_clear_x86.c", 1)
	bind("call_cache", "payload_mm_authvar_mor_clear_executor.c",
		"cache_writeback_invalidate", "payload_mm_authvar_mor_clear_x86.c", 1)
	bind("call_fence", "payload_mm_authvar_mor_clear_executor.c",
		"fence", "payload_mm_authvar_mor_clear_x86.c")
	expect_indirect("call_fence", "payload_mm_authvar_mor_clear_executor.c", 1)
	bind("call_unmap", "payload_mm_authvar_mor_clear_executor.c",
		"unmap_window", "payload_mm_authvar_mor_clear_x86.c", 1)
	bind("payload_mm_authvar_mor_clear_execute", "payload_mm_authvar_mor_clear_executor.c",
		"map_window", "payload_mm_authvar_mor_clear_x86.c")
	bind("payload_mm_authvar_mor_clear_execute", "payload_mm_authvar_mor_clear_executor.c",
		"cache_writeback_invalidate", "payload_mm_authvar_mor_clear_x86.c")
	bind("payload_mm_authvar_mor_clear_execute", "payload_mm_authvar_mor_clear_executor.c",
		"unmap_window", "payload_mm_authvar_mor_clear_x86.c")
	expect_indirect("payload_mm_authvar_mor_clear_execute",
		"payload_mm_authvar_mor_clear_executor.c", 7, 3)

	bind("disable_and_verify", "payload_mm_authvar_mor_clear_x86.c",
		"paging_disable_pae", "pgtbl.c")
	bind("disable_and_verify", "payload_mm_authvar_mor_clear_x86.c",
		"production_paging_active", "payload_mm_authvar_mor_clear_x86.c")
	expect_indirect("disable_and_verify", "payload_mm_authvar_mor_clear_x86.c", 2, 2)
	bind("prepare_with_ops", "payload_mm_authvar_mor_clear_x86.c",
		"clflush_supported", "cache.c")
	expect_indirect("prepare_with_ops", "payload_mm_authvar_mor_clear_x86.c", 1)
	bind("map_window", "payload_mm_authvar_mor_clear_x86.c",
		"production_page_tables_init", "payload_mm_authvar_mor_clear_x86.c")
	bind("map_window", "payload_mm_authvar_mor_clear_x86.c",
		"production_map_2m", "payload_mm_authvar_mor_clear_x86.c")
	bind("map_window", "payload_mm_authvar_mor_clear_x86.c",
		"production_paging_active", "payload_mm_authvar_mor_clear_x86.c")
	expect_indirect("map_window", "payload_mm_authvar_mor_clear_x86.c", 4, 3)
	bind("cache_writeback_invalidate", "payload_mm_authvar_mor_clear_x86.c",
		"clflush_supported", "cache.c")
	bind("cache_writeback_invalidate", "payload_mm_authvar_mor_clear_x86.c",
		"clflush_region", "cache.c")
	expect_indirect("cache_writeback_invalidate",
		"payload_mm_authvar_mor_clear_x86.c", 2, 2)
	bind("fence", "payload_mm_authvar_mor_clear_x86.c",
		"production_memory_fence", "payload_mm_authvar_mor_clear_x86.c")
	expect_indirect("fence", "payload_mm_authvar_mor_clear_x86.c", 1)

	bind("guard_poison", "dma_guard.c", "platform_poison", "dma_live_platform.c")
	expect_indirect("guard_poison", "dma_guard.c", 1)
	bind("starbook_mtl_dma_guard_prepare_with_ops", "dma_guard.c",
		"platform_ensure", "dma_live_platform.c")
	bind("starbook_mtl_dma_guard_prepare_with_ops", "dma_guard.c",
		"platform_observe", "dma_live_platform.c")
	bind("starbook_mtl_dma_guard_prepare_with_ops", "dma_guard.c",
		"platform_random64", "dma_live_platform.c")
	expect_indirect("starbook_mtl_dma_guard_prepare_with_ops", "dma_guard.c", 4, 3)
	bind("starbook_mtl_dma_guard_bind_with_ops_owned", "dma_guard.c",
		"platform_ensure", "dma_live_platform.c")
	bind("starbook_mtl_dma_guard_bind_with_ops_owned", "dma_guard.c",
		"platform_observe", "dma_live_platform.c")
	expect_indirect("starbook_mtl_dma_guard_bind_with_ops_owned", "dma_guard.c", 2, 2)

	bind("read64", "vtd_transition.c", "engine_read32", "dma_live_platform.c")
	bind("wait32", "vtd_transition.c", "engine_read32", "dma_live_platform.c")
	bind("vtd_transition_probe", "vtd_transition.c", "engine_read32", "dma_live_platform.c")
	bind("write64", "vtd_transition.c", "engine_write32", "dma_live_platform.c")
	bind("vtd_transition_from_pmr", "vtd_transition.c", "engine_read32", "dma_live_platform.c")
	bind("vtd_transition_from_pmr", "vtd_transition.c", "engine_write32", "dma_live_platform.c")
	bind("vtd_transition_from_pmr", "vtd_transition.c", "commit_tables", "dma_live_platform.c")
	expect_indirect("vtd_transition_probe", "vtd_transition.c", 3, 1)
	expect_indirect("read64", "vtd_transition.c", 2, 1)
	expect_indirect("vtd_transition_from_pmr", "vtd_transition.c", 7, 3)
	expect_indirect("write64", "vtd_transition.c", 2, 1)
	expect_indirect("wait32", "vtd_transition.c", 1, 1)
	bind("pci_bme_quiesce", "pci_bme_quiesce.c", "pci_read32",
		"dma_live_platform.c")
	bind("pci_bme_quiesce", "pci_bme_quiesce.c", "pci_write16",
		"dma_live_platform.c")
	expect_indirect("pci_bme_quiesce", "pci_bme_quiesce.c", 5, 2)
	bind("pci_bme_quiesce_revalidate", "pci_bme_quiesce.c", "pci_read32",
		"dma_live_platform.c")
	expect_indirect("pci_bme_quiesce_revalidate", "pci_bme_quiesce.c", 3, 1)
	bind("pci_bme_quiesce_terminal", "pci_bme_quiesce.c", "pci_read32",
		"dma_live_platform.c")
	bind("pci_bme_quiesce_terminal", "pci_bme_quiesce.c", "pci_write16",
		"dma_live_platform.c")
	expect_indirect("pci_bme_quiesce_terminal", "pci_bme_quiesce.c", 4, 2)
	bind("bootmem_walk_dram", "bootmem.c", "collect_range",
		"payload_mm_authvar_mor_live_inventory.c")
	expect_indirect("bootmem_walk_dram", "bootmem.c", 1)
	bind("bootmem_walk", "bootmem.c", "collect_range",
		"payload_mm_authvar_mor_live_inventory.c", 1)

	if (provider_contract) {
	bind_cap("close_private_seed", "mor_platform.c", private_cap)
	expect_indirect("close_private_seed", "mor_platform.c", 1)
	bind_cap("ensure_seed", "mor_platform.c", private_cap)
	expect_indirect("ensure_seed", "mor_platform.c", 1)
	bind_cap("reservations_register", "mor_platform.c", private_cap)
	expect_indirect("reservations_register", "mor_platform.c", 1)
	bind_cap("resolve_binding", "mor_platform.c", private_cap)
	expect_indirect("resolve_binding", "mor_platform.c", 1)
	bind_cap("private_complete", "mor_platform.c", private_cap)
	expect_indirect("private_complete", "mor_platform.c", 1)
	bind_cap("private_close", "mor_platform.c", private_cap)
	expect_indirect("private_close", "mor_platform.c", 1)
	}

	# Region-device and console callbacks are selected coreboot implementation
	# boundaries, not unaudited arbitrary callbacks. Include every selected
	# concrete target conservatively.
	bind_cap("rdev_mmap", "region.c", region_cap)
	expect_indirect("rdev_mmap", "region.c", 1)
	bind_cap("rdev_munmap", "region.c", region_unmap_cap)
	expect_indirect("rdev_munmap", "region.c", 1)
	bind("vtxprintf", "vtxprintf.c", "console_interactive_tx_byte", "console.c")
	bind("vtxprintf", "vtxprintf.c", "wrap_putchar", "printk.c")
	expect_indirect("vtxprintf", "vtxprintf.c", 10, 2)
	bind("number", "vtxprintf.c", "console_interactive_tx_byte", "console.c")
	bind("number", "vtxprintf.c", "wrap_putchar", "printk.c")
	expect_indirect("number", "vtxprintf.c", 10, 2)

	# Exact GCC call-site identities. The source:line:column multiset is part of
	# the manifest, so a same-count substitution is rejected.
	if (provider_contract) {
		permit_sites("payload_mm_authvar_mor_linear_before_bootmem",
			"src/lib/payload_mm_authvar_mor_linear.c", "309:3", "classify_guard")
		permit_sites("payload_mm_authvar_mor_linear_before_bootmem",
			"src/lib/payload_mm_authvar_mor_linear.c", "345:3", "reservations_register")
		permit_sites("payload_mm_authvar_mor_linear_after_bootmem",
			"src/lib/payload_mm_authvar_mor_linear.c", "429:6", "resolve_binding")
		permit_sites("payload_mm_authvar_mor_linear_after_bootmem",
			"src/lib/payload_mm_authvar_mor_linear.c", "462:18", "private_complete")
		permit_sites("close_retained_authority",
			"src/lib/payload_mm_authvar_mor_linear.c", "169:9", "private_close")
		permit_sites("close_private_seed", "src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c", "355:8", "@private_callback")
		permit_sites("ensure_seed", "src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c", "408:6", "@private_callback")
		permit_sites("reservations_register", "src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c", "545:6", "@private_callback")
		permit_sites("resolve_binding", "src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c", "616:6", "@private_callback")
		permit_sites("private_complete", "src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c", "668:11", "@private_callback")
		permit_sites("private_close", "src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c", "703:11", "@private_callback")
	}
	permit_sites("call_inventory", "src/lib/payload_mm_authvar_mor_clear_executor.c", "280:29", "executor_inventory_validate")
	permit_sites("call_dma", "src/lib/payload_mm_authvar_mor_clear_executor.c", "297:29", "executor_dma_snapshot")
	permit_sites("call_fence", "src/lib/payload_mm_authvar_mor_clear_executor.c", "347:29", "fence")
	permit_sites("payload_mm_authvar_mor_clear_execute",
		"src/lib/payload_mm_authvar_mor_clear_executor.c",
		"314:29,314:29", "map_window")
	permit_sites("payload_mm_authvar_mor_clear_execute", "src/lib/payload_mm_authvar_mor_clear_executor.c", "332:29,332:29", "cache_writeback_invalidate")
	permit_sites("payload_mm_authvar_mor_clear_execute", "src/lib/payload_mm_authvar_mor_clear_executor.c", "364:29,364:29", "unmap_window")
	permit_sites("payload_mm_authvar_mor_clear_execute", "src/lib/payload_mm_authvar_mor_clear_executor.c", "716:9", "unmap_window")
	permit_sites("disable_and_verify", "src/lib/payload_mm_authvar_mor_clear_x86.c", "132:2", "paging_disable_pae")
	permit_sites("disable_and_verify", "src/lib/payload_mm_authvar_mor_clear_x86.c", "133:11", "production_paging_active")
	permit_sites("prepare_with_ops", "src/lib/payload_mm_authvar_mor_clear_x86.c", "330:31", "clflush_supported")
	permit_sites("map_window", "src/lib/payload_mm_authvar_mor_clear_x86.c", "185:16", "production_page_tables_init")
	permit_sites("map_window", "src/lib/payload_mm_authvar_mor_clear_x86.c", "186:11,194:11", "production_paging_active")
	permit_sites("map_window", "src/lib/payload_mm_authvar_mor_clear_x86.c", "193:2", "production_map_2m")
	permit_sites("cache_writeback_invalidate", "src/lib/payload_mm_authvar_mor_clear_x86.c", "229:14", "clflush_supported")
	permit_sites("cache_writeback_invalidate", "src/lib/payload_mm_authvar_mor_clear_x86.c", "235:2", "clflush_region")
	permit_sites("fence", "src/lib/payload_mm_authvar_mor_clear_x86.c", "253:2", "production_memory_fence")
	permit_sites("guard_poison", "src/mainboard/starlabs/starbook/variants/mtl/dma_guard.c", "229:2", "platform_poison")
	permit_sites("starbook_mtl_dma_guard_prepare_with_ops",
		"src/mainboard/starlabs/starbook/variants/mtl/dma_guard.c", "342:6", "platform_ensure")
	permit_sites("starbook_mtl_dma_guard_prepare_with_ops",
		"src/mainboard/starlabs/starbook/variants/mtl/dma_guard.c", "343:6,373:6", "platform_observe")
	permit_sites("starbook_mtl_dma_guard_prepare_with_ops",
		"src/mainboard/starlabs/starbook/variants/mtl/dma_guard.c", "356:8", "platform_random64")
	permit_sites("starbook_mtl_dma_guard_bind_with_ops_owned",
		"src/mainboard/starlabs/starbook/variants/mtl/dma_guard.c", "452:6", "platform_ensure")
	permit_sites("starbook_mtl_dma_guard_bind_with_ops_owned",
		"src/mainboard/starlabs/starbook/variants/mtl/dma_guard.c", "455:6", "platform_observe")
	permit_sites("read64", "src/soc/intel/common/block/vtd/vtd_transition.c", "29:9,30:14", "engine_read32")
	permit_sites("write64", "src/soc/intel/common/block/vtd/vtd_transition.c", "36:2,37:2", "engine_write32")
	permit_sites("wait32", "src/soc/intel/common/block/vtd/vtd_transition.c", "44:8", "engine_read32")
	permit_sites("vtd_transition_probe", "src/soc/intel/common/block/vtd/vtd_transition.c", "57:14,60:13,62:30", "engine_read32")
	permit_sites("vtd_transition_from_pmr", "src/soc/intel/common/block/vtd/vtd_transition.c", "81:7,87:7,124:7", "engine_read32")
	permit_sites("vtd_transition_from_pmr", "src/soc/intel/common/block/vtd/vtd_transition.c", "113:2,119:2,132:2", "engine_write32")
	permit_sites("vtd_transition_from_pmr", "src/soc/intel/common/block/vtd/vtd_transition.c", "109:2", "commit_tables")
	permit_sites("pci_bme_quiesce", "src/lib/pci_bme_quiesce.c", "59:24,67:12,69:14,74:19", "pci_read32")
	permit_sites("pci_bme_quiesce", "src/lib/pci_bme_quiesce.c", "72:5", "pci_write16")
	permit_sites("pci_bme_quiesce_revalidate", "src/lib/pci_bme_quiesce.c", "115:24,127:16,129:14", "pci_read32")
	permit_sites("pci_bme_quiesce_terminal", "src/lib/pci_bme_quiesce.c", "222:25,228:15,231:11", "pci_read32")
	permit_sites("pci_bme_quiesce_terminal", "src/lib/pci_bme_quiesce.c", "229:5", "pci_write16")
	permit_sites("bootmem_walk_dram", "src/lib/bootmem.c", "694:9", "collect_range")
	permit_sites("rdev_mmap", "src/commonlib/region.c", "63:9", "@selected_boot_region_mmap_callback")
	permit_sites("rdev_munmap", "src/commonlib/region.c", "75:9", "@selected_boot_region_munmap_callback")
	permit_sites("number", "src/console/vtxprintf.c", "72:4,75:3,79:4,81:4,83:5,85:5,90:4,93:3,95:3,97:3", "wrap_putchar|console_interactive_tx_byte")
	permit_sites("vtxprintf", "src/console/vtxprintf.c", "120:4,188:6,189:4,191:5,203:6,206:5,208:5,235:4,258:4,260:5", "wrap_putchar|console_interactive_tx_byte")

	# Fixed compiler-runtime/boot-device assembly boundaries from the selected
	# final ELF. Their conservative caps exceed their disassembled stack use.
	stack_bytes["__divdi3"] = runtime_div_bytes
	stack_bytes["__udivdi3"] = runtime_udiv_bytes
	stack_bytes["__udivmoddi4"] = runtime_udivmod_bytes
	stack_bytes["__umoddi3"] = runtime_umod_bytes

	binding = find_node("starbook_mtl_mor_clear_x86_prepare", "mor_clear_x86.c")
	executor = find_node("payload_mm_authvar_mor_clear_execute",
		"payload_mm_authvar_mor_clear_executor.c")
	early_dma = find_node("starbook_mtl_dma_guard_prepare", "dma_live_platform.c")
	binding_bound = stack_bound(binding)
	executor_bound = stack_bound(executor)
	early_dma_bound = stack_bound(early_dma)
	core_bound = binding_bound > executor_bound ? binding_bound : executor_bound
	if (early_dma_bound > core_bound)
		core_bound = early_dma_bound
	if (provider_contract) {
		provider = find_node("platform_payload_mm_authvar_mor_linear_ops",
			"mor_platform.c")
		provider_bound = stack_bound(provider)
		linear_before = find_node("payload_mm_authvar_mor_linear_before_bootmem",
			"payload_mm_authvar_mor_linear.c")
		linear_after = find_node("payload_mm_authvar_mor_linear_after_bootmem",
			"payload_mm_authvar_mor_linear.c")
		linear_before_bound = stack_bound(linear_before)
		linear_after_bound = stack_bound(linear_after)
		pre_wrapper = find_node("mor_before_bootmem",
			"payload_mm_authvar_mor_linear.c")
		post_wrapper = find_node("mor_after_bootmem",
			"payload_mm_authvar_mor_linear.c")
		pre_wrapper_bound = stack_bound(pre_wrapper)
		post_wrapper_bound = stack_bound(post_wrapper)
		if (provider_bound > core_bound)
			core_bound = provider_bound
		if (linear_before_bound > core_bound)
			core_bound = linear_before_bound
		if (linear_after_bound > core_bound)
			core_bound = linear_after_bound
		if (pre_wrapper_bound > core_bound)
			core_bound = pre_wrapper_bound
		if (post_wrapper_bound > core_bound)
			core_bound = post_wrapper_bound
	}
	if (!provider_contract) {
		fail_closed = find_node("mor_before_bootmem",
			"payload_mm_authvar_mor_linear.c")
		weak_provider = find_node("platform_payload_mm_authvar_mor_linear_ops",
			"payload_mm_authvar_mor_linear.c")
		fail_closed_bound = stack_bound(fail_closed)
		weak_provider_bound = stack_bound(weak_provider)
		if (fail_closed_bound > core_bound)
			core_bound = fail_closed_bound
		if (weak_provider_bound > core_bound)
			core_bound = weak_provider_bound
	}
	if (core_bound > limit) {
		print "ERROR: selected production stack exceeds " limit > "/dev/stderr"
		failed = 1
	}
	if (failed)
		exit 1
	if (provider_contract) {
		if (stack_bytes[pre_wrapper] != 64 || stack_bytes[post_wrapper] != 32) {
			print "ERROR: wrapper frame contract drift" > "/dev/stderr"
			exit 1
		}
		printf "binding %u; executor %u; early-dma %u; provider-glue %u; linear-pre %u; linear-post %u; wrapper-pre %u (frame %u); wrapper-post %u (frame %u); contract-maximum %u\n", \
			binding_bound, executor_bound, early_dma_bound, provider_bound, \
			linear_before_bound, linear_after_bound, pre_wrapper_bound, \
			stack_bytes[pre_wrapper], post_wrapper_bound, stack_bytes[post_wrapper], \
			core_bound
	} else
		printf "binding %u; executor %u; early-dma %u; fail-closed-entry %u; weak-provider %u; selected-maximum %u\n", \
			binding_bound, executor_bound, early_dma_bound, fail_closed_bound, \
			weak_provider_bound, core_bound
}
