# SPDX-License-Identifier: GPL-2.0-only

function add_edge(from, to)
{
	if (!edge_seen[from SUBSEP to]++)
		edge[from, ++edge_count[from]] = to
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
			printf "ERROR: ambiguous external node %s\n", name > "/dev/stderr"
			failed = 1
			return node
		}
		found = candidate
	}
	return found != "" ? found : node
}

function find_node(name, source,    node, found)
{
	for (node in function_name) {
		if (function_name[node] != name &&
		    index(function_name[node], name ".") != 1)
			continue
		if (source != "" && index(location[node], source) == 0 &&
		    index(node, source) == 0)
			continue
		if (found != "" && found != node) {
			printf "ERROR: ambiguous node %s:%s\n", source, name > "/dev/stderr"
			failed = 1
			return ""
		}
		found = node
	}
	if (found == "") {
		printf "ERROR: missing node %s:%s\n", source, name > "/dev/stderr"
		failed = 1
	}
	return found
}

function bind(caller, caller_source, target, target_source,    from, to)
{
	from = find_node(caller, caller_source)
	to = find_node(target, target_source)
	if (from != "" && to != "")
		add_edge(from, to)
}

function resolve_site(caller, source, position,    node, wanted, i, found)
{
	node = find_node(caller, source)
	wanted = source ":" position
	for (i = 1; i <= indirect_count[node]; i++) {
		if (indirect_site[node, i] == wanted && !resolved[node, i]) {
			resolved[node, i] = 1
			found++
		}
	}
	if (found != 1) {
		printf "ERROR: expected one indirect site %s in %s, found %u\n", \
			wanted, caller, found > "/dev/stderr"
		failed = 1
	}
}

function skip_selected_edge(from, to)
{
	# fast_spi_flash_probe installs fast_spi_flash_ops and always returns zero,
	# so spi_flash_probe cannot enter the generic JEDEC fallback on bus zero.
	return edge_disabled(from, to) ||
		(function_name[from] == "spi_flash_probe" &&
		function_name[canonical(to)] == "spi_flash_generic_probe") ||
		(runtime_div_absent && function_name[to] == "__divdi3")
}

function edge_disabled(from, to,    key)
{
	key = function_name[from] "|" function_name[canonical(to)]
	return omit_edge != "" && key == omit_edge
}

function require_edge(caller, caller_source, target, target_source,
	from, to, i, found)
{
	from = find_node(caller, caller_source)
	to = canonical(find_node(target, target_source))
	for (i = 1; i <= edge_count[from]; i++)
		if (canonical(edge[from, i]) == to && !edge_disabled(from, edge[from, i]))
			found++
	if (found != 1) {
		printf "ERROR: required rooted edge %s -> %s count %u\n", caller, target, \
			found > "/dev/stderr"
		failed = 1
	}
}

function stack_bound(node,    child, child_bound, i, largest)
{
	node = canonical(node)
	if (bound_done[node])
		return bound_total[node]
	if (bound_active[node]) {
		printf "ERROR: recursive reachable graph at %s\n", node > "/dev/stderr"
		failed = 1
		return 0
	}
	if (!(node in stack_bytes)) {
		printf "ERROR: reachable node has no frame: %s (%s)\n", \
			function_name[node], location[node] > "/dev/stderr"
		failed = 1
		return 0
	}
	if (frame_kind[node] ~ /dynamic/ && frame_kind[node] !~ /bounded/) {
		printf "ERROR: unbounded dynamic frame: %s\n", function_name[node] \
			> "/dev/stderr"
		failed = 1
	}
	for (i = 1; i <= indirect_count[node]; i++)
		if (!resolved[node, i]) {
			printf "ERROR: unbound indirect %s in %s\n", indirect_site[node, i], \
				function_name[node] > "/dev/stderr"
			failed = 1
		}
	bound_active[node] = 1
	for (i = 1; i <= edge_count[node]; i++) {
		child = edge[node, i]
		if (child == "__indirect_call" || skip_selected_edge(node, child))
			continue
		child_bound = 4 + stack_bound(child)
		if (child_bound > largest) {
			largest = child_bound
			deepest_child[node] = canonical(child)
		}
	}
	bound_active[node] = 0
	bound_done[node] = 1
	bound_total[node] = stack_bytes[node] + largest
	return bound_total[node]
}

function print_path(node,    separator)
{
	node = canonical(node)
	separator = ""
	while (node != "") {
		printf "%s%s[%u]", separator, function_name[node], stack_bytes[node]
		separator = " -> "
		node = deepest_child[node]
	}
	printf "\n"
}

function discover(node,    child, i)
{
	node = canonical(node)
	if (visited[node]++)
		return
	if (!(node in stack_bytes)) {
		printf "NOFRAME %s %s\n", function_name[node], location[node]
		return
	}
	for (i = 1; i <= indirect_count[node]; i++)
		if (!resolved[node, i])
			printf "UNBOUND %s|%s|%s\n", function_name[node], location[node], \
				indirect_site[node, i]
	for (i = 1; i <= edge_count[node]; i++) {
		child = edge[node, i]
		if (child != "__indirect_call" && !skip_selected_edge(node, child))
			discover(child)
	}
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
		indirect_site[from, ++indirect_count[from]] = site
	}
	next
}

END {
	bind("payload_mm_authvar_media_begin", "payload_mm_authvar_media.c",
		"begin", "payload_mm_authvar_smmstore.c")
	bind("read_backend", "payload_mm_authvar_media.c",
		"read_media", "payload_mm_authvar_smmstore.c")
	bind("sync_backend", "payload_mm_authvar_media.c",
		"sync_media", "payload_mm_authvar_smmstore.c")
	bind("sealed_sync_readback", "payload_mm_authvar_media.c",
		"sync_media", "payload_mm_authvar_smmstore.c")
	bind("sealed_sync_readback", "payload_mm_authvar_media.c",
		"read_media", "payload_mm_authvar_smmstore.c")
	bind("end_backend", "payload_mm_authvar_media.c",
		"end", "payload_mm_authvar_smmstore.c")
	bind("payload_mm_authvar_media_program", "payload_mm_authvar_media.c",
		"program", "payload_mm_authvar_smmstore.c")
	bind("payload_mm_authvar_media_erase", "payload_mm_authvar_media.c",
		"erase", "payload_mm_authvar_smmstore.c")

	# Bus zero is the immutable selected fast-SPI controller. Its claim and
	# release callbacks are deliberately NULL; setup and probe are exact.
	bind("spi_setup_slave", "spi-generic.c", "fast_spi_flash_ctrlr_setup",
		"fast_spi_flash.c")
	bind("spi_flash_probe", "spi_flash.c", "fast_spi_flash_probe",
		"fast_spi_flash.c")
	bind("spi_flash_volatile_lease_read", "spi_flash.c",
		"fast_spi_flash_read", "fast_spi_flash.c")
	bind("spi_flash_volatile_lease_write", "spi_flash.c",
		"fast_spi_flash_write", "fast_spi_flash.c")
	bind("spi_flash_volatile_lease_erase", "spi_flash.c",
		"fast_spi_flash_erase", "fast_spi_flash.c")
	bind("spi_flash_volatile_lease_sync", "spi_flash.c",
		"fast_spi_flash_status", "fast_spi_flash.c")

	media_begin_node = find_node("payload_mm_authvar_media_begin",
		"payload_mm_authvar_media.c")
	if (mutate_unknown_indirect)
		indirect_site[media_begin_node, ++indirect_count[media_begin_node]] = \
			"src/lib/payload_mm_authvar_media.c:9999:1"
	if (mutate_shifted_indirect)
		for (mutation_index = 1;
		     mutation_index <= indirect_count[media_begin_node];
		     mutation_index++)
			if (indirect_site[media_begin_node, mutation_index] == \
			    "src/lib/payload_mm_authvar_media.c:535:11")
				indirect_site[media_begin_node, mutation_index] = \
					"src/lib/payload_mm_authvar_media.c:535:12"
	if (mutate_duplicate_indirect)
		indirect_site[media_begin_node, ++indirect_count[media_begin_node]] = \
			"src/lib/payload_mm_authvar_media.c:535:11"

	resolve_site("payload_mm_authvar_media_begin", "src/lib/payload_mm_authvar_media.c", "535:11")
	resolve_site("read_backend", "src/lib/payload_mm_authvar_media.c", "319:11")
	resolve_site("sync_backend", "src/lib/payload_mm_authvar_media.c", "342:11")
	resolve_site("sealed_sync_readback", "src/lib/payload_mm_authvar_media.c", "377:11")
	resolve_site("sealed_sync_readback", "src/lib/payload_mm_authvar_media.c", "398:11")
	resolve_site("end_backend", "src/lib/payload_mm_authvar_media.c", "430:11")
	resolve_site("payload_mm_authvar_media_program", "src/lib/payload_mm_authvar_media.c", "612:11")
	resolve_site("payload_mm_authvar_media_erase", "src/lib/payload_mm_authvar_media.c", "692:11")
	resolve_site("spi_setup_slave", "src/drivers/spi/spi-generic.c", "145:10")
	resolve_site("spi_flash_probe", "src/drivers/spi/spi_flash.c", "611:9")
	resolve_site("spi_flash_volatile_lease_read", "src/drivers/spi/spi_flash.c", "1116:8")
	resolve_site("spi_flash_volatile_lease_write", "src/drivers/spi/spi_flash.c", "1129:8")
	resolve_site("spi_flash_volatile_lease_erase", "src/drivers/spi/spi_flash.c", "1141:8")
	resolve_site("spi_flash_volatile_lease_sync", "src/drivers/spi/spi_flash.c", "1154:9")

	stack_bytes["__udivdi3"] = runtime_udiv_bytes
	stack_bytes["__udivmoddi4"] = runtime_udivmod_bytes
	stack_bytes["__umoddi3"] = runtime_umod_bytes
	function_name["__udivdi3"] = "__udivdi3"
	function_name["__udivmoddi4"] = "__udivmoddi4"
	function_name["__umoddi3"] = "__umoddi3"

	root = find_node("payload_mm_authvar_fmp_state_transaction",
		"payload_mm_authvar_executor.c")
	if (mutate_root_frame)
		stack_bytes[root] = stack_size
	if (mutate_control_frame) {
		control_node = find_node("control_unchanged",
			"payload_mm_authvar_executor.c")
		stack_bytes[control_node] = stack_size
	}
	if (mutate_deep_direct) {
		deep_node = find_node("memcpy", "string.h")
		add_edge(deep_node, root)
	}
	if (omit_root) {
		printf "ERROR: required FMP transaction root omitted\n" > "/dev/stderr"
		failed = 1
	}
	require_edge("payload_mm_authvar_fmp_state_transaction", "payload_mm_authvar_executor.c",
		"media_begin", "payload_mm_authvar_executor.c")
	require_edge("payload_mm_authvar_fmp_state_transaction", "payload_mm_authvar_executor.c",
		"recover_session", "payload_mm_authvar_executor.c")
	require_edge("payload_mm_authvar_fmp_state_transaction", "payload_mm_authvar_executor.c",
		"execute_direct", "payload_mm_authvar_executor.c")
	require_edge("payload_mm_authvar_fmp_state_transaction", "payload_mm_authvar_executor.c",
		"execute_reclaim", "payload_mm_authvar_executor.c")
	require_edge("payload_mm_authvar_fmp_state_transaction", "payload_mm_authvar_executor.c",
		"verify_media", "payload_mm_authvar_executor.c")
	require_edge("payload_mm_authvar_fmp_state_transaction", "payload_mm_authvar_executor.c",
		"payload_mm_authvar_store_scan", "payload_mm_authvar_store.c")
	require_edge("payload_mm_authvar_media_begin", "payload_mm_authvar_media.c",
		"begin", "payload_mm_authvar_smmstore.c")
	require_edge("begin", "payload_mm_authvar_smmstore.c",
		"intel_smm_spi_window_begin", "smm_spi_window.c")
	require_edge("begin", "payload_mm_authvar_smmstore.c",
		"intel_smm_spi_window_prove", "smm_spi_window.c")
	require_edge("end", "payload_mm_authvar_smmstore.c",
		"intel_smm_spi_window_end", "smm_spi_window.c")
	discover(root)
	maximum = stack_bound(root)
	printf "FMP transaction maximum %u; caller reserve %u; stack %u; headroom %u\n", \
		maximum, caller_reserve, stack_size, stack_size - maximum - caller_reserve
	printf "deepest path: "
	print_path(root)
	if (maximum + caller_reserve > stack_size) {
		printf "ERROR: FMP transaction plus caller reserve exceeds stack\n" \
			> "/dev/stderr"
		failed = 1
	}
	if (maximum_limit && maximum > maximum_limit) {
		printf "ERROR: transaction maximum %u exceeds mutant limit %u\n", \
			maximum, maximum_limit > "/dev/stderr"
		failed = 1
	}
	if (total_limit && maximum + caller_reserve > total_limit) {
		printf "ERROR: transaction plus reserve %u exceeds mutant limit %u\n", \
			maximum + caller_reserve, total_limit > "/dev/stderr"
		failed = 1
	}
	if (failed)
		exit 1
}
