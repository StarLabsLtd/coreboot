# SPDX-License-Identifier: GPL-2.0-only

function add_edge(from, to)
{
	if (!edge_seen[from SUBSEP to]++)
		edge[from, ++edge_count[from]] = to
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
			print "ERROR: ambiguous node " source ":" name > "/dev/stderr"
			failed = 1
			return ""
		}
		found = node
	}
	if (found == "") {
		print "ERROR: missing node " source ":" name > "/dev/stderr"
		failed = 1
	}
	return found
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
			print "ERROR: ambiguous external node " name > "/dev/stderr"
			failed = 1
			return node
		}
		found = candidate
	}
	return found != "" ? found : node
}

function stack_bound(node,    child, child_bound, edge_index, largest)
{
	node = canonical(node)
	if (done[node])
		return total[node]
	if (active[node]) {
		print "ERROR: recursive private-boundary path at " node > "/dev/stderr"
		failed = 1
		return 0
	}
	if (!(node in stack_bytes)) {
		print "ERROR: reachable node lacks compiler stack data: " node \
			> "/dev/stderr"
		failed = 1
		return 0
	}
	if (frame_kind[node] ~ /dynamic/ && frame_kind[node] !~ /bounded/) {
		print "ERROR: reachable unbounded frame: " node > "/dev/stderr"
		failed = 1
	}
	active[node] = 1
	for (edge_index = 1; edge_index <= edge_count[node]; edge_index++) {
		child = edge[node, edge_index]
		if (child == "__indirect_call") {
			print "ERROR: reachable unresolved indirect call in " node \
				> "/dev/stderr"
			failed = 1
			continue
		}
		child_bound = call_return_bytes + stack_bound(child)
		if (child_bound > largest)
			largest = child_bound
	}
	active[node] = 0
	done[node] = 1
	total[node] = stack_bytes[node] + largest
	return total[node]
}

function graph_reaches(root, target,    child, edge_index, key)
{
	root = canonical(root)
	target = canonical(target)
	key = root SUBSEP target
	if (reach_done[key])
		return reach_result[key]
	if (reach_active[key])
		return 0
	if (root == target) {
		reach_done[key] = 1
		reach_result[key] = function_name[root] != disconnect_name
		return reach_result[key]
	}
	reach_active[key] = 1
	for (edge_index = 1; edge_index <= edge_count[root]; edge_index++) {
		child = canonical(edge[root, edge_index])
		if (child == "__indirect_call" ||
		    function_name[child] == disconnect_name)
			continue
		if (graph_reaches(child, target)) {
			delete reach_active[key]
			reach_done[key] = 1
			reach_result[key] = 1
			return 1
		}
	}
	delete reach_active[key]
	reach_done[key] = 1
	reach_result[key] = 0
	return 0
}

function require_rooted(root, name, source,    found, reached, target)
{
	for (target in function_name) {
		if (function_name[target] != name &&
		    index(function_name[target], name ".") != 1)
			continue
		if (index(location[target], source) == 0 &&
		    index(target, source) == 0)
			continue
		found = 1
		if (graph_reaches(root, target))
			reached = 1
	}
	if (!found || !reached) {
		print "ERROR: concrete private root does not reach " source ":" name \
			> "/dev/stderr"
		failed = 1
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
	next
}

END {
	call_return_bytes = 4
	stack_bytes["__divdi3"] = runtime_div_bytes
	stack_bytes["__udivdi3"] = runtime_udiv_bytes
	stack_bytes["__udivmoddi4"] = runtime_udivmod_bytes
	stack_bytes["__umoddi3"] = runtime_umod_bytes
	function_name["__divdi3"] = "__divdi3"
	function_name["__udivdi3"] = "__udivdi3"
	function_name["__udivmoddi4"] = "__udivmoddi4"
	function_name["__umoddi3"] = "__umoddi3"

	seed_node = find_node("seed", "mor_private_boundary.c")
	register_node = find_node("reservations_register", "mor_private_boundary.c")
	resolve_node = find_node("resolve", "mor_private_boundary.c")
	complete_node = find_node("complete", "mor_private_boundary.c")
	close_node = find_node("close", "mor_private_boundary.c")
	loader_seed = find_node("platform_payload_mm_authvar_mor_private_smi_seed",
		"mor_private_boundary.c")

	seed_bound = stack_bound(seed_node)
	register_bound = stack_bound(register_node)
	resolve_bound = stack_bound(resolve_node)
	complete_bound = stack_bound(complete_node)
	close_bound = stack_bound(close_node)
	loader_seed_bound = stack_bound(loader_seed)
	for (root_index = 1; root_index <= 2; root_index++) {
		root = root_index == 1 ? complete_node : close_node
		require_rooted(root, "send", "payload_mm_authvar_mor_private_smi_sender.c")
		require_rooted(root, "bootmem_aligned_reservation_receipt_emit", "bootmem.c")
		require_rooted(root, "sign_resolved_reservation", "bootmem.c")
		require_rooted(root, "bootmem_reservation_receipt_mac",
			"bootmem_reservation_receipt.c")
		require_rooted(root, "sha_update", "bootmem_reservation_receipt.c")
		require_rooted(root, "transform", "bootmem_reservation_receipt.c")
	}
	maximum = seed_bound
	if (register_bound > maximum) maximum = register_bound
	if (resolve_bound > maximum) maximum = resolve_bound
	if (complete_bound > maximum) maximum = complete_bound
	if (close_bound > maximum) maximum = close_bound
	if (loader_seed_bound > maximum) maximum = loader_seed_bound
	if (maximum > limit) {
		print "ERROR: concrete private boundary exceeds " limit > "/dev/stderr"
		failed = 1
	}
	if (failed)
		exit 1
	printf "seed %u; register %u; resolve %u; complete %u; close %u; loader-seed %u; maximum %u\n", \
		seed_bound, register_bound, resolve_bound, complete_bound, close_bound, \
		loader_seed_bound, maximum
}
