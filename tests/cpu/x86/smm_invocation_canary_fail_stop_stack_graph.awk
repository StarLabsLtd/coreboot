# SPDX-License-Identifier: GPL-2.0-only

function add_edge(from, to)
{
	if (!edge_seen[from SUBSEP to]++)
		edge[from, ++edge_count[from]] = to
}

function find_node(name, source, optional,    node, found)
{
	for (node in function_name) {
		if (function_name[node] != name && index(function_name[node], name ".") != 1)
			continue
		if (source != "" && index(location[node], source) == 0 &&
		    index(node, source) == 0)
			continue
		if (found != "" && found != node) {
			print "ERROR: ambiguous canary fail-stop node " source ":" name > "/dev/stderr"
			failed = 1
			return ""
		}
		found = node
	}
	if (found == "" && !optional) {
		print "ERROR: missing canary fail-stop node " source ":" name > "/dev/stderr"
		failed = 1
	}
	return found
}

function canonical(node,    candidate, found, name)
{
	if (node in stack_bytes)
		return node
	name = function_name[node]
	for (candidate in stack_bytes) {
		if (function_name[candidate] != name &&
		    index(function_name[candidate], name ".") != 1)
			continue
		if (found != "" && found != candidate) {
			print "ERROR: ambiguous external canary fail-stop node " name > "/dev/stderr"
			failed = 1
			return node
		}
		found = candidate
	}
	if (found == "") {
		print "ERROR: unresolved external canary fail-stop node " name > "/dev/stderr"
		failed = 1
		return node
	}
	return found
}

function require_edge(from, to, description,    i)
{
	for (i = 1; i <= edge_count[from]; i++)
		if (canonical(edge[from, i]) == canonical(to))
			return
	print "ERROR: missing canary fail-stop edge " description > "/dev/stderr"
	failed = 1
}

function frame(node)
{
	node = canonical(node)
	if (!(node in stack_bytes)) {
		print "ERROR: reachable canary fail-stop node lacks stack evidence " node > "/dev/stderr"
		failed = 1
		return 0
	}
	if (frame_kind[node] ~ /dynamic/ && frame_kind[node] !~ /bounded/) {
		print "ERROR: unbounded canary fail-stop frame " node > "/dev/stderr"
		failed = 1
	}
	return stack_bytes[node]
}

function bound(node,    child, child_bound, i, largest)
{
	node = canonical(node)
	if (done[node])
		return total[node]
	if (active[node]) {
		print "ERROR: recursive canary fail-stop graph at " node > "/dev/stderr"
		failed = 1
		return 0
	}
	active[node] = 1
	reachable[node] = 1
	for (i = 1; i <= edge_count[node]; i++) {
		child = edge[node, i]
		if (child == "__indirect_call") {
			print "ERROR: indirect canary fail-stop edge from " node > "/dev/stderr"
			failed = 1
			continue
		}
		child = canonical(child)
		child_bound = call_return_bytes + bound(child)
		if (child_bound > largest)
			largest = child_bound
	}
	active[node] = 0
	done[node] = 1
	total[node] = frame(node) + largest
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
	function_name[node] = name
	location[node] = where
	bytes = $0
	if (bytes ~ /\\n[0-9]+ bytes/) {
		sub(/^.*\\n/, "", bytes)
		sub(/ bytes.*/, "", bytes)
		if (!(node in stack_bytes) || bytes + 0 > stack_bytes[node]) {
			stack_bytes[node] = bytes + 0
			kind = $0
			sub(/^.* bytes \(/, "", kind)
			sub(/\).*/, "", kind)
			frame_kind[node] = kind
		}
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
	entry = find_node("smm_handler_start", "smm_module_handler.c", 0)
	provider = find_node("smm_invocation_platform_fail_stop",
		"smm_invocation_fail_stop.c", 0)
	require_edge(entry, provider, "handler -> platform provider")
	provider_bound = bound(provider)
	selected_bound = frame(entry) + call_return_bytes + provider_bound

	required["do_system_reset"] = 1
	required["abort"] = 1
	if (mtl) {
		required["tco_read_reg"] = 1
		required["tco_write_reg"] = 1
		required["tco_get_timer_min_value"] = 1
	} else {
		required["do_full_reset"] = 1
	}
	for (name in required) {
		node = find_node(name, "", 0)
		if (node == "" || !reachable[canonical(node)]) {
			print "ERROR: canary fail-stop graph does not reach " name > "/dev/stderr"
			failed = 1
		}
	}
	if (selected_bound > limit) {
		print "ERROR: canary fail-stop path exceeds stack budget (" selected_bound \
			" > " limit ")" > "/dev/stderr"
		failed = 1
	}
	if (failed)
		exit 1
	print selected_bound
}
