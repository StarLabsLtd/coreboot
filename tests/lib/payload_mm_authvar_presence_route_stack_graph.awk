# SPDX-License-Identifier: GPL-2.0-only

BEGIN {
	expected_indirect["arm_fail_stop"] = 1
	expected_indirect["arm_fail_stop_snapshot"] = 1
	expected_indirect["callback_protected"] = 1
	expected_indirect["closure_fail_stop"] = 1
	expected_indirect["complete_invocation"] = 1
	expected_indirect["delegated_protected_storage"] = 1
	expected_indirect["dispatch_fail_stop"] = 1
	expected_indirect["payload_mm_authvar_presence_transaction_dispatch"] = 7
	expected_indirect["payload_mm_authvar_presence_transaction_provision"] = 7
	expected_indirect["protected_exact"] = 1
	expected_indirect["protected_range_bootstrap"] = 1
	expected_indirect["protected_range_exact.part.0"] = 1
	expected_indirect["provisioning_fail_stop"] = 1
	expected_indirect["restore_or_fail_stop"] = 2
	expected_indirect["route_decide"] = 2
	expected_indirect["route_dma_protected"] = 1
	expected_indirect["route_fail_stop_snapshot"] = 1
	expected_indirect["route_fail_stop_untrusted"] = 1
	expected_indirect["route_prepare"] = 1
	expected_indirect["smm_invocation_evidence_claim"] = 4
	expected_indirect["smm_invocation_evidence_publish"] = 3
	expected_indirect["smm_invocation_evidence_publish_and_request_close"] = 3
	expected_indirect["terminal_fail_stop"] = 2
	expected_indirect["wrapped_abort"] = 1
	expected_indirect["wrapped_claim"] = 1
	expected_indirect["wrapped_commit"] = 1
	expected_indirect["wrapped_complete"] = 1
	expected_indirect["wrapped_dma_protected"] = 1
	expected_indirect["wrapped_prepare"] = 1
	expected_indirect["wrapped_protected_storage.part.0"] = 1
	allowed_external["Indirect Call Placeholder"] = 1
	allowed_external["smm_invocation_platform_fail_stop"] = 1
	allowed_external["platform_payload_mm_authvar_presence_arm"] = 1
	register_sites("protected_range_bootstrap", "379:14")
	register_sites("arm_fail_stop_snapshot", "347:2")
	register_sites("arm_fail_stop", "313:2")
	register_sites("protected_range_exact.part.0", "361:14")
	register_sites("wrapped_dma_protected", "685:14")
	register_sites("wrapped_abort", "640:11")
	register_sites("wrapped_complete", "804:11")
	register_sites("wrapped_claim", "725:11")
	register_sites("wrapped_commit", "579:11")
	register_sites("wrapped_prepare", "535:11")
	register_sites("wrapped_protected_storage.part.0", "908:14")
	register_sites("callback_protected", "91:21")
	register_sites("dispatch_fail_stop", "457:2")
	register_sites("closure_fail_stop", "157:3")
	register_sites("provisioning_fail_stop", "142:2")
	register_sites("complete_invocation", "206:11")
	register_sites("terminal_fail_stop", "513:9,523:2")
	register_sites("payload_mm_authvar_presence_transaction_provision",
		"325:7,327:7,328:7,329:7,330:7,332:7,349:9")
	register_sites("payload_mm_authvar_presence_transaction_dispatch",
		"445:6,731:7,756:13,554:25,819:12,554:25,554:25")
	register_sites("route_fail_stop_snapshot", "217:2")
	register_sites("protected_exact", "340:14")
	register_sites("delegated_protected_storage", "378:14")
	register_sites("route_fail_stop_untrusted", "184:2")
	register_sites("route_dma_protected", "530:14")
	register_sites("route_decide", "491:3,492:3")
	register_sites("route_prepare", "459:11")
	register_sites("restore_or_fail_stop", "1298:6,1303:6")
	register_sites("smm_invocation_evidence_claim",
		"1535:11,1550:6,1557:6,1563:6")
	register_sites("smm_invocation_evidence_publish",
		"1644:6,1661:6,1664:6")
	register_sites("smm_invocation_evidence_publish_and_request_close",
		"1776:6,1787:6,1796:6")
	register_direct("payload_mm_authvar_presence_route_session_dispatch_locked",
		"smm_apmc_command_consume", "1231:6")
	register_direct("payload_mm_authvar_presence_route_session_dispatch_locked",
		"payload_mm_authvar_presence_transaction_dispatch", "1235:11")
	register_direct("route_claim", "smm_invocation_evidence_claim", "565:6")
	register_direct("route_complete",
		"smm_invocation_evidence_publish_and_request_close", "648:11")
	register_direct("route_session_validate_and_bind",
		"payload_mm_authvar_presence_arm_protection_delegate_bind", "1045:11")
	register_direct("payload_mm_authvar_presence_route_session_provision",
		"payload_mm_authvar_presence_arm_transaction_provision", "1123:11")
	register_direct("payload_mm_authvar_presence_arm_transaction_provision",
		"payload_mm_authvar_presence_transaction_provision", "1469:6")
}

function register_sites(caller, positions,    count, items, item)
{
	count = split(positions, items, ",")
	for (item = 1; item <= count; ++item)
		expected_site[caller SUBSEP items[item]]++
}

function register_direct(caller, callee, site,    key)
{
	key = caller SUBSEP callee
	expected_direct[key]++
	expected_direct_site[key SUBSEP site]++
}

function node_function(node,    name)
{
	if (node in function_name)
		return function_name[node]
	name = node
	sub(/^.*:/, "", name)
	return name
}

function add_edge(from, to)
{
	if (from != "" && to != "" && !seen_edge[from SUBSEP to]++)
		edge[from, ++edge_count[from]] = to
}

function find(name,    node, found)
{
	for (node in function_name) {
		if (function_name[node] != name)
			continue
		if (found != "" && found != node) {
			print "ambiguous stack node: " name > "/dev/stderr"
			failed = 1
			return ""
		}
		found = node
	}
	if (found == "") {
		print "missing stack node: " name > "/dev/stderr"
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
			print "ambiguous external stack node: " name > "/dev/stderr"
			failed = 1
			return node
		}
		found = candidate
	}
	return found != "" ? found : node
}

function bound(node,    child, child_bound, edge_index, largest)
{
	node = canonical(node)
	if (done[node])
		return total[node]
	if (active[node]) {
		print "recursive route stack path: " node > "/dev/stderr"
		failed = 1
		return 0
	}
	if (!(node in stack_bytes)) {
		if (allowed_external[function_name[node]])
			return 0
		print "unclassified reachable external stack edge: " \
			function_name[node] > "/dev/stderr"
		failed = 1
		return 0
	}
	if (unmanifested_indirect[node]) {
		print "unmanifested reachable indirect route edge: " \
			function_name[node] > "/dev/stderr"
		failed = 1
	}
	if (frame_kind[node] ~ /dynamic/ && frame_kind[node] !~ /bounded/) {
		print "unbounded route stack frame: " function_name[node] > "/dev/stderr"
		failed = 1
	}
	active[node] = 1
	for (edge_index = 1; edge_index <= edge_count[node]; ++edge_index) {
		child = edge[node, edge_index]
		child_bound = 4 + bound(child)
		if (child_bound > largest)
		{
			largest = child_bound
			largest_child[node] = child
		}
	}
	active[node] = 0
	done[node] = 1
	total[node] = stack_bytes[node] + largest
	return total[node]
}

function report_stack_path(node)
{
	node = canonical(node)
	printf "%s[%u]", function_name[node], stack_bytes[node] > "/dev/stderr"
	if (largest_child[node] != "") {
		printf " -> " > "/dev/stderr"
		report_stack_path(largest_child[node])
	}
}

/^node: / {
	node = $0
	sub(/^.*title: "/, "", node)
	sub(/".*/, "", node)
	label = $0
	sub(/^.*label: "/, "", label)
	sub(/".*/, "", label)
	name = label
	sub(/\\n.*/, "", name)
	bytes = label
	sub(/^.*\\n/, "", bytes)
	sub(/ bytes.*/, "", bytes)
	function_name[node] = name
	if (bytes ~ /^[0-9]+$/)
		stack_bytes[node] = bytes + 0
	kind = label
	sub(/^.* bytes \(/, "", kind)
	sub(/\).*/, "", kind)
	frame_kind[node] = kind
	next
}

/^edge: / {
	from = $0
	sub(/^.*sourcename: "/, "", from)
	sub(/".*/, "", from)
	to = $0
	sub(/^.*targetname: "/, "", to)
	sub(/".*/, "", to)
	site = $0
	sub(/^.*label: "/, "", site)
	sub(/".*/, "", site)
	sub(/^.*\.c:/, "", site)
	if (to == "__indirect_call") {
		caller = from
		sub(/^.*:/, "", caller)
		if (caller in expected_indirect) {
			seen_indirect[caller]++
			seen_site[caller SUBSEP site]++
		} else {
			unmanifested_indirect[from] = 1
		}
	}
	raw_from[++raw_edge_count] = from
	raw_to[raw_edge_count] = to
	raw_site[raw_edge_count] = site
	add_edge(from, to)
	next
}

END {
	for (raw_index = 1; raw_index <= raw_edge_count; ++raw_index) {
		caller = node_function(raw_from[raw_index])
		callee = node_function(raw_to[raw_index])
		direct_key = caller SUBSEP callee
		if (direct_key in expected_direct) {
			seen_direct[direct_key]++
			seen_direct_site[direct_key SUBSEP raw_site[raw_index]]++
		}
	}
	for (direct_key in expected_direct) {
		if (seen_direct[direct_key] != expected_direct[direct_key]) {
			split(direct_key, direct_parts, SUBSEP)
			print "direct route edge mismatch: " direct_parts[1] " -> " \
				direct_parts[2] > "/dev/stderr"
			failed = 1
		}
	}
	for (direct_site_key in expected_direct_site)
		if (seen_direct_site[direct_site_key] != \
		    expected_direct_site[direct_site_key]) {
			print "direct route edge site mismatch" > "/dev/stderr"
			failed = 1
		}
	for (caller in expected_indirect) {
		if (seen_indirect[caller] != expected_indirect[caller]) {
			print "indirect route edge count mismatch: " caller > "/dev/stderr"
			failed = 1
		}
	}
	for (site_key in expected_site) {
		if (seen_site[site_key] != expected_site[site_key]) {
			split(site_key, site_parts, SUBSEP)
			print "indirect route site mismatch: " site_parts[1] ":" \
				site_parts[2] > "/dev/stderr"
			failed = 1
		}
	}
	for (site_key in seen_site) {
		if (!(site_key in expected_site)) {
			split(site_key, site_parts, SUBSEP)
			print "unmanifested indirect route site: " site_parts[1] ":" \
				site_parts[2] > "/dev/stderr"
			failed = 1
		}
	}
	receiver = find("payload_mm_authvar_presence_transaction_dispatch")
	provision = find("payload_mm_authvar_presence_transaction_provision")
	add_edge(receiver, find("wrapped_prepare"))
	add_edge(receiver, find("wrapped_commit"))
	add_edge(receiver, find("wrapped_abort"))
	add_edge(receiver, find("wrapped_dma_protected"))
	add_edge(receiver, find("wrapped_claim"))
	add_edge(receiver, find("wrapped_complete"))
	add_edge(provision, find("wrapped_protected_storage"))
	add_edge(find("complete_invocation"), find("wrapped_complete"))
	add_edge(find("callback_protected"), find("wrapped_protected_storage"))
	add_edge(find("wrapped_protected_storage"),
		find("delegated_protected_storage"))
	add_edge(find("protected_range_exact.part.0"),
		find("delegated_protected_storage"))
	add_edge(find("protected_range_bootstrap"),
		find("delegated_protected_storage"))
	add_edge(find("closure_fail_stop"), find("wrapped_fail_stop"))
	add_edge(find("dispatch_fail_stop"), find("wrapped_fail_stop"))
	add_edge(find("provisioning_fail_stop"), find("wrapped_fail_stop"))
	add_edge(find("terminal_fail_stop"), find("wrapped_fail_stop"))
	add_edge(find("wrapped_prepare"), find("route_prepare"))
	add_edge(find("wrapped_commit"), find("route_decide"))
	add_edge(find("wrapped_abort"), find("route_decide"))
	add_edge(find("wrapped_dma_protected"), find("route_dma_protected"))
	add_edge(find("wrapped_claim"), find("route_claim"))
	add_edge(find("wrapped_complete"), find("route_complete"))
	add_edge(find("arm_fail_stop"), find("route_policy_fail_stop"))
	add_edge(find("arm_fail_stop_snapshot"), find("route_policy_fail_stop"))

	roots[1] = find("payload_mm_authvar_presence_route_session_provision")
	roots[2] = find("payload_mm_authvar_presence_route_session_arrive")
	roots[3] = find("payload_mm_authvar_presence_route_session_dispatch_locked")
	roots[4] = find("payload_mm_authvar_presence_route_session_prepare_lock_release")
	roots[5] = find("payload_mm_authvar_presence_route_session_depart")
	for (root_index = 1; root_index <= 5; ++root_index) {
		value = bound(roots[root_index])
		if (value > maximum) {
			maximum = value
			maximum_root = roots[root_index]
		}
	}
	if (trace) {
		report_stack_path(maximum_root)
		printf "\n" > "/dev/stderr"
	}
	if (maximum > limit) {
		print "resolved route stack exceeds limit: " maximum > "/dev/stderr"
		failed = 1
	}
	if (failed)
		exit 1
	printf "resolved route stack maximum %u bytes; unresolved authority, proof and save-state callbacks deferred\n", maximum
}
