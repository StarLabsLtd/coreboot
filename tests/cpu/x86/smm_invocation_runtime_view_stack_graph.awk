# SPDX-License-Identifier: GPL-2.0-only

function quoted(line, key,    start, rest, end)
{
	start = index(line, key " \"")
	if (!start)
		return ""
	rest = substr(line, start + length(key) + 2)
	end = index(rest, "\"")
	if (!end)
		return ""
	return substr(rest, 1, end - 1)
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
			print "ERROR: ambiguous runtime-view node " name > "/dev/stderr"
			failed = 1
			return node
		}
		found = candidate
	}
	if (found == "") {
		print "ERROR: unresolved runtime-view node " name > "/dev/stderr"
		failed = 1
		return node
	}
	return found
}

function find_node(name,    node, found)
{
	for (node in function_name) {
		if (function_name[node] != name &&
		    index(function_name[node], name ".") != 1)
			continue
		if (found != "" && canonical(found) != canonical(node)) {
			print "ERROR: ambiguous runtime-view root " name > "/dev/stderr"
			failed = 1
			return ""
		}
		found = node
	}
	if (found == "") {
		print "ERROR: missing runtime-view root " name > "/dev/stderr"
		failed = 1
	}
	return canonical(found)
}

function saw_function(name,    candidate)
{
	for (candidate in seen_name)
		if (candidate == name || index(candidate, name ".") == 1)
			return 1
	return 0
}

function require_edge(from_name, to_name,    count, from, i, target, to)
{
	from = find_node(from_name)
	to = find_node(to_name)
	for (i = 1; i <= edge_count[from]; i++) {
		target = canonical(edge[from, i])
		if (target == to)
			count++
	}
	if (count != 1) {
		print "ERROR: runtime-view edge contract " from_name " -> " to_name > "/dev/stderr"
		failed = 1
	}
}

function walk(node, depth,    child, target, i, maximum)
{
	node = canonical(node)
	if (!(node in stack_bytes))
		return 0
	if (visiting[node]) {
		print "ERROR: recursive runtime-view callgraph at " function_name[node] > "/dev/stderr"
		failed = 1
		return 0
	}
	if (unbounded[node]) {
		print "ERROR: unbounded runtime-view frame " function_name[node] > "/dev/stderr"
		failed = 1
		return 0
	}
	visiting[node] = 1
	depth += stack_bytes[node]
	maximum = depth
	if (depth > limit) {
		print "ERROR: runtime-view stack bound exceeded" > "/dev/stderr"
		failed = 1
	}
	seen_name[function_name[node]] = 1
	for (i = 1; i <= edge_count[node]; i++) {
		target = edge[node, i]
		if (function_name[target] == "__indirect_call") {
			print "ERROR: indirect runtime-view edge from " function_name[node] > "/dev/stderr"
			failed = 1
			continue
		}
		child = walk(target, depth)
		if (child > maximum)
			maximum = child
	}
	visiting[node] = 0
	return maximum
}

/^node:/ {
	node = quoted($0, "title:")
	label = quoted($0, "label:")
	if (node == "" || label == "")
		next
	split(label, parts, /\\n/)
	function_name[node] = parts[1]
	if (match(label, /\\n[0-9]+ bytes \((static|dynamic,bounded)\)/)) {
		frame = substr(label, RSTART + 2, RLENGTH - 2)
		sub(/ bytes.*/, "", frame)
		stack_bytes[node] = frame + 0
	} else if (label ~ /\\n[0-9]+ bytes \(dynamic\)/) {
		unbounded[node] = 1
	}
	next
}

/^edge:/ {
	from = quoted($0, "sourcename:")
	to = quoted($0, "targetname:")
	if (from != "" && to != "")
		edge[from, ++edge_count[from]] = to
}

END {
	require_edge("smm_invocation_runtime_view_get", "runtime_geometry_snapshot")
	require_edge("smm_invocation_runtime_cpu_count", "runtime_geometry_snapshot")
	require_edge("smm_invocation_runtime_save_state_span", "runtime_geometry_snapshot")
	require_edge("runtime_geometry_snapshot", "runtime_topology_matches")
	require_edge("runtime_geometry_snapshot", "runtime_composition_matches")
	require_edge("runtime_geometry_snapshot", "runtime_geometry_valid")
	require_edge("runtime_geometry_valid", "smm_save_state_native_span")
	root[1] = "smm_invocation_runtime_view_get"
	root[2] = "smm_invocation_runtime_cpu_count"
	root[3] = "smm_invocation_runtime_save_state_span"
	maximum = 0
	for (i = 1; i <= 3; i++) {
		value = walk(find_node(root[i]), 0)
		if (value > maximum)
			maximum = value
	}
	required[1] = "runtime_geometry_snapshot"
	required[2] = "runtime_geometry_valid"
	required[3] = "runtime_topology_matches"
	required[4] = "runtime_composition_matches"
	required[5] = "smm_save_state_native_span"
	for (i = 1; i <= 5; i++)
		if (!saw_function(required[i])) {
			print "ERROR: missing reachable runtime-view function " required[i] > "/dev/stderr"
			failed = 1
		}
	if (failed)
		exit 1
	print maximum
}
