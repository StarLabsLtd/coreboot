# SPDX-License-Identifier: GPL-2.0-only

function quoted(line, key,    start, rest, end)
{
	start = index(line, key " \"")
	if (!start)
		return ""
	rest = substr(line, start + length(key) + 2)
	end = index(rest, "\"")
	return end ? substr(rest, 1, end - 1) : ""
}

function short_name(name,    count, parts)
{
	count = split(name, parts, /:/)
	return parts[count]
}

function matches(candidate, name,    short)
{
	short = short_name(candidate)
	return short == name || index(short, name ".") == 1
}

function static_node(node)
{
	return index(node, ":") != 0
}

function translation_unit(node,    count, parts, result, i)
{
	count = split(node, parts, /:/)
	for (i = 1; i < count; i++)
		result = result (i == 1 ? "" : ":") parts[i]
	return result
}

function canonical(node,    candidate, found, known_weak, name)
{
	if (node in stack_bytes || node in unbounded)
		return node
	if (!(node in function_name)) {
		print "ERROR: unknown provider node " node > "/dev/stderr"
		failed = 1
		return node
	}
	name = function_name[node]
	for (candidate in stack_bytes) {
		if (static_node(node)) {
			if (!static_node(candidate) ||
			    translation_unit(candidate) != translation_unit(node) ||
			    !matches(function_name[candidate], short_name(name)))
				continue
		} else {
			# GCC models this one known weak definition as TU-local.
			known_weak = node == "get_smm_save_state_ops" && candidate == \
				"src/soc/intel/common/block/smm/smihandler.c:get_smm_save_state_ops"
			if (!known_weak && (static_node(candidate) ||
			    (candidate != node && index(candidate, node ".") != 1)))
				continue
		}
		if (found != "" && found != candidate) {
			print "ERROR: ambiguous provider node " name > "/dev/stderr"
			failed = 1
			return node
		}
		found = candidate
	}
	if (found == "") {
		print "ERROR: unresolved provider node " name > "/dev/stderr"
		failed = 1
		return node
	}
	return found
}

function find_node(name,    node, found, resolved)
{
	for (node in function_name) {
		if (!matches(function_name[node], name))
			continue
		resolved = canonical(node)
		if (found != "" && found != resolved) {
			print "ERROR: ambiguous provider root " name > "/dev/stderr"
			failed = 1
			return ""
		}
		found = resolved
	}
	if (found == "") {
		print "ERROR: missing provider root " name > "/dev/stderr"
		failed = 1
	}
	return found
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
		print "ERROR: provider edge contract " from_name " -> " to_name > "/dev/stderr"
		failed = 1
	}
}

function walk(node, depth,    child, target, i, maximum, name)
{
	node = canonical(node)
	name = short_name(function_name[node])
	if (unbounded[node]) {
		print "ERROR: unbounded provider frame " name > "/dev/stderr"
		failed = 1
		return 0
	}
	if (!(node in stack_bytes))
		return 0
	if (visiting[node]) {
		print "ERROR: recursive provider callgraph at " name > "/dev/stderr"
		failed = 1
		return 0
	}
	visiting[node] = 1
	depth += stack_bytes[node]
	maximum = depth
	if (depth > limit) {
		print "ERROR: provider cumulative stack bound exceeded" > "/dev/stderr"
		failed = 1
	}
	for (i = 1; i <= edge_count[node]; i++) {
		target = edge[node, i]
		if (target == "__indirect_call" ||
		    short_name(function_name[target]) == "Indirect Call Placeholder") {
			if (name != "intel_smm_invocation_adapter_init_spans" ||
			    ++borrower_edges != 1) {
				print "ERROR: unresolved provider indirect edge from " name > "/dev/stderr"
				failed = 1
				continue
			}
			target = find_node("runtime_span")
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
	require_edge("intel_smm_invocation_adapter_provider_provision",
		"smm_invocation_runtime_view_get")
	require_edge("intel_smm_invocation_adapter_provider_provision",
		"smm_invocation_runtime_cpu_count")
	require_edge("intel_smm_invocation_adapter_provider_provision",
		"intel_smm_invocation_adapter_init_spans")
	require_edge("intel_smm_invocation_adapter_provider_arm",
		"intel_smm_invocation_adapter_begin")
	require_edge("intel_smm_invocation_adapter_provider_retire",
		"intel_smm_invocation_adapter_end")
	require_edge("runtime_span", "smm_invocation_runtime_save_state_span")
	root[1] = "intel_smm_invocation_adapter_provider_provision"
	root[2] = "intel_smm_invocation_adapter_provider_arm"
	root[3] = "intel_smm_invocation_adapter_provider_retire"
	for (i = 1; i <= 3; i++) {
		delete visiting
		value = walk(find_node(root[i]), 0)
		if (value > maximum)
			maximum = value
	}
	if (borrower_edges != 1) {
		print "ERROR: provider borrower edge cardinality" > "/dev/stderr"
		failed = 1
	}
	if (failed)
		exit 1
	print maximum
}
