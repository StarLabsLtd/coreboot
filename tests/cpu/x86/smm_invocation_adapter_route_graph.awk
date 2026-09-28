# SPDX-License-Identifier: GPL-2.0-only

BEGIN {
	root = "intel_smm_invocation_adapter_route_provision"
	provider = "intel_smm_invocation_adapter_provider_provision"
	route = "payload_mm_authvar_presence_route_session_provision"
}

/^node:/ {
	line = $0
	if (line ~ ("title: \"" root "\"")) {
		root_nodes++
		if (line ~ /\\n96 bytes \(dynamic,bounded\)"/)
			frame = 96
	}
}

/^edge:/ {
	all_edges++
	if ($0 ~ ("sourcename: \"" root "\"")) {
		edges++
		if ($0 ~ ("targetname: \"" provider "\""))
			provider_edges++
		else if ($0 ~ ("targetname: \"" route "\""))
			route_edges++
		else
			bad_edges++
	} else {
		bad_edges++
	}
}

END {
	if (root_nodes != 1 || frame != 96 || frame > limit || edges != 2 ||
	    all_edges != 2 || provider_edges != 1 || route_edges != 1 || bad_edges)
		exit 1
	print frame
}
