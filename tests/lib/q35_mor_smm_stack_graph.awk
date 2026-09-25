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
			print "ERROR: ambiguous SMM stack node " source ":" name > "/dev/stderr"
			failed = 1
			return ""
		}
		found = node
	}
	if (found == "" && !optional) {
		print "ERROR: missing SMM stack node " source ":" name > "/dev/stderr"
		failed = 1
	}
	return found
}

function canonical(node,    candidate, found, name, source)
{
	name = function_name[node]
	if (name == "platform_payload_mm_authvar_mor_private_smi_bootstrap")
		source = "mor_platform_smm.c"
	if (node in stack_bytes && (source == "" || index(location[node], source)))
		return node
	for (candidate in stack_bytes) {
		if (function_name[candidate] != name &&
		    index(function_name[candidate], name ".") != 1)
			continue
		if (source != "" && index(location[candidate], source) == 0 &&
		    index(candidate, source) == 0)
			continue
		if (found != "" && found != candidate) {
			print "ERROR: ambiguous external SMM stack node " name > "/dev/stderr"
			failed = 1
			return node
		}
		found = candidate
	}
	if (found == "") {
		print "ERROR: unresolved external SMM stack node " name > "/dev/stderr"
		failed = 1
		return node
	}
	return found
}

function bind_site(site, target_name, target_source)
{
	if (site == omit_site)
		return
	if (site in callback_name) {
		print "ERROR: duplicate selected SMM callback site " site > "/dev/stderr"
		failed = 1
		return
	}
	callback_name[site] = target_name
	callback_source[site] = target_source
}

function resolve_indirects(node,    i, site, target)
{
	for (i = 1; i <= indirect_count[node]; i++) {
		site = indirect_site[node, i]
		if (!(site in callback_name)) {
			print "ERROR: unbound selected SMM indirect site " site > "/dev/stderr"
			failed = 1
			continue
		}
		used_site[site]++
		if (used_site[site] != 1) {
			print "ERROR: duplicate compiler edge at selected SMM callback site " site > "/dev/stderr"
			failed = 1
		}
		target = find_node(callback_name[site], callback_source[site], 0)
		if (target != "")
			add_edge(node, target)
	}
}

function require_edge(from, to, description,    i)
{
	for (i = 1; i <= edge_count[from]; i++)
		if (canonical(edge[from, i]) == canonical(to))
			return
	print "ERROR: missing selected SMM edge " description > "/dev/stderr"
	failed = 1
}

function require_reachable(name, source, description,    node)
{
	node = find_node(name, source, 0)
	if (node == "" || !reachable[canonical(node)]) {
		print "ERROR: selected SMM graph does not reach " description \
			> "/dev/stderr"
		failed = 1
	}
}

function frame(node, description)
{
	node = canonical(node)
	if (!(node in stack_bytes)) {
		print "ERROR: selected SMM " description " lacks stack evidence" > "/dev/stderr"
		failed = 1
		return 0
	}
	if (frame_kind[node] ~ /dynamic/ && frame_kind[node] !~ /bounded/) {
		print "ERROR: unbounded selected SMM " description " frame" > "/dev/stderr"
		failed = 1
	}
	return stack_bytes[node]
}

function bound(node,    child, child_bound, edge_index, largest, node_frame)
{
	node = canonical(node)
	if (done[node])
		return total[node]
	if (active[node]) {
		print "ERROR: recursive selected SMM bootstrap graph at " node > "/dev/stderr"
		failed = 1
		return 0
	}
	node_frame = frame(node, "reachable node")
	resolve_indirects(node)
	active[node] = 1
	reachable[node] = 1
	for (edge_index = 1; edge_index <= edge_count[node]; edge_index++) {
		child = edge[node, edge_index]
		if (child == "__indirect_call")
			continue
		if (runtime_div_absent && function_name[child] == "__divdi3")
			continue
		if (function_name[canonical(child)] == omit_edge_name)
			continue
		# The selected Intel fast-SPI probe is total and returns success. Its
		# generic JEDEC fallback successor is therefore infeasible on MTL.
		if (mtl && function_name[node] == "spi_flash_probe" &&
		    function_name[canonical(child)] == "spi_flash_generic_probe")
			continue
		child_bound = call_return_bytes + bound(child)
		if (child_bound > largest)
			largest = child_bound
	}
	active[node] = 0
	done[node] = 1
	total[node] = node_frame + largest
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
		indirect_site[from, ++indirect_count[from]] = site
	}
	next
}

END {
	call_return_bytes = 4

	# Bind the exact compiler-observed callback sites for this selected path.
	bootstrap_media_source = mtl ? "payload_mm_authvar_smm_media_spi.c" : \
		"payload_mm_authvar_smm_media_qemu.c"
	bind_site("src/lib/payload_mm_authvar_smm_bootstrap.c:128:10", "media_facts",
		bootstrap_media_source)
	bind_site("src/lib/payload_mm_authvar_smm_bootstrap.c:206:15", "writes_are_private", "mor_platform_smm.c")
	bind_site("src/lib/payload_mm_authvar_smm_bootstrap.c:367:6", "media_facts",
		bootstrap_media_source)
	bind_site("src/lib/payload_mm_authvar_smm_bootstrap.c:466:6", "media_install",
		bootstrap_media_source)
	bind_site("src/lib/payload_mm_authvar_mor_seal.c:223:7", "fixed_transport", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_mor_seal.c:131:14", "protected_storage", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_mor_grant.c:187:14", "grant_storage_is_protected", "payload_mm_authvar_mor_seal.c")
	bind_site("src/lib/payload_mm_authvar_mor_grant.c:289:3", "mor_grant_output_protected", "payload_mm_authvar_executor.c")
	bind_site("src/lib/payload_mm_authvar_mor_grant.c:291:3", "mor_grant_output_protected", "payload_mm_authvar_executor.c")
	bind_site("src/lib/payload_mm_authvar_mor_grant.c:292:21", "mor_grant_output_protected", "payload_mm_authvar_executor.c")
	bind_site("src/lib/payload_mm_authvar_mor_grant.c:388:10", "grant_storage_protected", "payload_mm_authvar_mor_private_smi_receiver.c")
	bind_site("src/lib/payload_mm_authvar_mor_grant.c:389:19", "grant_storage_protected", "payload_mm_authvar_mor_private_smi_receiver.c")
	bind_site("src/lib/payload_mm_authvar_mor_grant.c:390:21", "grant_storage_protected", "payload_mm_authvar_mor_private_smi_receiver.c")
	bind_site("src/lib/payload_mm_authvar_mor_private_smi_receiver.c:173:3", "protected_storage", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_mor_private_smi_receiver.c:162:3", "protected_storage", "payload_mm_authvar_smm_bootstrap.c")
	if (!mtl)
		bind_site("src/mainboard/emulation/qemu-i440fx/rom_media.c:473:11",
			"mdev_readat", "commonlib/region.c")
	bind_site("src/commonlib/region.c:63:9", "mdev_mmap", "commonlib/region.c")
	bind_site("src/commonlib/region.c:75:9", "mdev_munmap", "commonlib/region.c")
	bind_site("src/lib/payload_mm_authvar.c:116:7", "smm_entry_owned", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar.c:117:7", "spi_writes_restricted", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar.c:118:7", "raw_flash_transport_absent", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar.c:119:7", "communication_reserved", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar.c:121:7", "store_owned", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_runtime.c:76:7", "protected_storage", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_mor_private_smi_receiver.c:263:7", "protected_storage", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_mor_private_smi_receiver.c:266:7", "protected_storage", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_mor_private_smi_receiver.c:269:7", "protected_storage", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_mor_private_smi_receiver.c:272:7", "protected_storage", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_mor_private_smi_receiver.c:323:7", "protected_storage", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_mor_private_smi_receiver.c:326:7", "protected_storage", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_mor_seal.c:178:14", "protected_storage", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_mor_seal.c:179:3", "protected_storage", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_mor_seal.c:180:3", "protected_storage", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_mor_seal.c:181:3", "protected_storage", "payload_mm_authvar_smm_bootstrap.c")
	bind_site("src/lib/payload_mm_authvar_mor_seal.c:182:11", "fixed_transport", "payload_mm_authvar_smm_bootstrap.c")
	media_source = mtl ? "payload_mm_authvar_smmstore.c" : \
		"payload_mm_authvar_qemu_pflash.c"
	bind_site("src/lib/payload_mm_authvar_media.c:319:11", "read_media", media_source)
	bind_site("src/lib/payload_mm_authvar_media.c:342:11", "sync_media", media_source)
	bind_site("src/lib/payload_mm_authvar_media.c:377:11", "sync_media", media_source)
	bind_site("src/lib/payload_mm_authvar_media.c:398:11", "read_media", media_source)
	bind_site("src/lib/payload_mm_authvar_media.c:430:11", "end", media_source)
	bind_site("src/lib/payload_mm_authvar_media.c:535:11", "begin", media_source)
	bind_site("src/lib/payload_mm_authvar_media.c:612:11", "program", media_source)
	bind_site("src/lib/payload_mm_authvar_media.c:692:11", "erase", media_source)
	if (mtl) {
		bind_site("src/drivers/spi/spi_flash.c:611:9", "fast_spi_flash_probe",
			"fast_spi_flash.c")
		bind_site("src/drivers/spi/spi-generic.c:145:10",
			"fast_spi_flash_ctrlr_setup", "fast_spi_flash.c")
		bind_site("src/drivers/spi/spi_flash.c:1116:8", "fast_spi_flash_read",
			"fast_spi_flash.c")
		bind_site("src/drivers/spi/spi_flash.c:1129:8", "fast_spi_flash_write",
			"fast_spi_flash.c")
		bind_site("src/drivers/spi/spi_flash.c:1141:8", "fast_spi_flash_erase",
			"fast_spi_flash.c")
		bind_site("src/drivers/spi/spi_flash.c:1154:9", "fast_spi_flash_status",
			"fast_spi_flash.c")
	}

	if (runtime_div_bytes) runtime_bytes["__divdi3"] = runtime_div_bytes
	if (runtime_udiv_bytes) runtime_bytes["__udivdi3"] = runtime_udiv_bytes
	if (runtime_udivmod_bytes) runtime_bytes["__udivmoddi4"] = runtime_udivmod_bytes
	if (runtime_umod_bytes) runtime_bytes["__umoddi3"] = runtime_umod_bytes
	for (runtime in runtime_bytes) {
		runtime_node = find_node(runtime, "", 0)
		stack_bytes[runtime_node] = runtime_bytes[runtime]
		frame_kind[runtime_node] = "audited selected ELF"
	}

	entry = find_node("smm_handler_start", "smm_module_handler.c", 0)
	dispatch = find_node("payload_mm_authvar_mor_private_smi_dispatch",
		"payload_mm_authvar_mor_private_smi_receiver.c", 0)
	receive = find_node("bootstrap_receive",
		"payload_mm_authvar_mor_private_smi_receiver.c", 0)
	platform = find_node("platform_payload_mm_authvar_mor_private_smi_bootstrap",
		"mor_platform_smm.c", 0)
	bootstrap = find_node("payload_mm_authvar_smm_bootstrap_install",
		"payload_mm_authvar_smm_bootstrap.c", 0)
	require_edge(entry, dispatch, "handler -> private dispatch")
	require_edge(dispatch, receive, "private dispatch -> bootstrap receive")

	bootstrap_bound = bound(receive)
	if (strict_sites)
		for (site in callback_name)
			if (used_site[site] != 1) {
				print "ERROR: selected SMM callback site not consumed " site > "/dev/stderr"
				failed = 1
			}
	selected_bound = frame(entry, "handler entry") + call_return_bytes
	selected_bound += frame(dispatch, "private dispatch") + call_return_bytes
	selected_bound += bootstrap_bound
	if (!reachable[platform] || !reachable[bootstrap]) {
		print "ERROR: selected SMM bootstrap spine is incomplete (platform=" \
			reachable[platform] ", bootstrap=" reachable[bootstrap] ")" > "/dev/stderr"
		failed = 1
	}
	if (mtl) {
		require_reachable("payload_mm_authvar_smmstore_install",
			"payload_mm_authvar_smmstore.c", "authenticated SMMSTORE install")
		require_reachable("intel_smm_spi_window_begin", "smm_spi_window.c",
			"Intel SPI window begin")
		require_reachable("intel_smm_spi_window_prove", "smm_spi_window.c",
			"Intel SPI window proof")
		require_reachable("intel_smm_spi_window_end", "smm_spi_window.c",
			"Intel SPI window close")
	}
	if (selected_bound > limit) {
		print "ERROR: selected SMM bootstrap exceeds stack budget (" \
			selected_bound " > " limit ")" > "/dev/stderr"
		failed = 1
	}
	if (failed)
		exit 1
	print selected_bound
}
