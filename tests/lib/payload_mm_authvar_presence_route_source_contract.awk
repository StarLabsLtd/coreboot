# SPDX-License-Identifier: GPL-2.0-only

function scrub_line(line,    character, cursor, next_character, output)
{
	output = ""
	for (cursor = 1; cursor <= length(line); ++cursor) {
		character = substr(line, cursor, 1)
		next_character = substr(line, cursor + 1, 1)
		if (block_comment) {
			if (character == "*" && next_character == "/") {
				block_comment = 0
				++cursor
			}
			continue
		}
		if (character == "/" && next_character == "*") {
			block_comment = 1
			++cursor
			continue
		}
		if (character == "/" && next_character == "/")
			break
		if (character == "\"") {
			++cursor
			while (cursor <= length(line)) {
				character = substr(line, cursor, 1)
				if (character == "\\")
					++cursor
				else if (character == "\"")
					break
				++cursor
			}
			continue
		}
		output = output character
	}
	return output
}

function compact(text)
{
	gsub(/[[:space:]]+/, "", text)
	return text
}

function balanced_after(text, anchor,    cursor, depth, output, start)
{
	start = index(text, anchor)
	if (!start)
		return ""
	start += length(anchor)
	while (substr(text, start, 1) != "{" && start <= length(text))
		++start
	if (start > length(text))
		return ""
	for (cursor = start; cursor <= length(text); ++cursor) {
		output = output substr(text, cursor, 1)
		if (substr(text, cursor, 1) == "{")
			++depth
		else if (substr(text, cursor, 1) == "}" && !--depth)
			return output
	}
	return ""
}

function occurrences(text, needle,    count, offset)
{
	while ((offset = index(text, needle))) {
		++count
		text = substr(text, offset + length(needle))
	}
	return count
}

function require_count(text, needle, expected, description)
{
	if (occurrences(text, needle) != expected) {
		print description " occurrence mismatch" > "/dev/stderr"
		failed = 1
	}
}

function require_initializer(initializer, name, target)
{
	require_count(initializer, "." name "=" target ",", 1,
		name " initializer")
}

{
	cleaned[FILENAME] = cleaned[FILENAME] scrub_line($0) "\n"
}

END {
	for (file in cleaned) {
		if (file ~ /payload_mm_authvar_presence_arm\.c$/ ||
		    file ~ /arm-target-decoy\.c$/)
			arm = compact(cleaned[file])
		else
			route = compact(cleaned[file])
	}
	if (route == "" || arm == "") {
		print "missing route source contract input" > "/dev/stderr"
		exit 1
	}

	dispatch = balanced_after(route,
		"payload_mm_authvar_presence_route_session_dispatch_locked(")
	claim = balanced_after(route, "staticenumcb_errroute_claim(")
	complete = balanced_after(route, "staticenumcb_errroute_complete(")
	authority_context = balanced_after(route, "staticvoid*authority_context(")
	prepare = balanced_after(route, "staticenumcb_errroute_prepare(")
	decide = balanced_after(route, "staticenumcb_errroute_decide(")
	dma = balanced_after(route, "staticboolroute_dma_protected(")
	provision = balanced_after(route,
		"payload_mm_authvar_presence_route_session_provision(")
	validation_bind = balanced_after(route,
		"static__noinlineenumcb_errroute_session_validate_and_bind(")
	require_count(dispatch, "smm_apmc_command_consume(", 1,
		"route consume")
	require_count(dispatch,
		"payload_mm_authvar_presence_transaction_dispatch(", 1,
		"route dispatch")
	require_count(claim, "smm_invocation_evidence_claim(", 1,
		"route evidence claim")
	require_count(complete,
		"smm_invocation_evidence_publish_and_request_close(", 1,
		"route strong completion")
	require_count(authority_context,
		"returnsession->authority_policy.context_size?" \
		"session->authority_context:NULL;", 1,
		"authority context selection")
	require_count(prepare,
		"session->authority_policy.prepare(authority_context(session),seed," \
		"generation)", 1, "prepare authority context forwarding")
	require_count(decide,
		"session->authority_policy.commit(authority_context(session),generation)",
		1, "commit authority context forwarding")
	require_count(decide,
		"session->authority_policy.abort(authority_context(session),generation)",
		1, "abort authority context forwarding")
	require_count(dma,
		"session->authority_policy.dma_protected(" \
		"authority_context(session),base,size)", 1,
		"DMA authority context forwarding")
	require_count(route, "fail_stop(context_size?context:NULL);", 2,
		"fail-stop context forwarding")
	require_count(route, "smm_invocation_evidence_publish(", 0,
		"route weak evidence publication")
	require_count(validation_bind,
		"payload_mm_authvar_presence_arm_protection_delegate_bind(", 1,
		"route delegate bind")
	require_count(provision,
		"payload_mm_authvar_presence_arm_transaction_provision(", 1,
		"route arm transaction provision")

	adjacent = "if(smm_apmc_command_consume(SMM_APMC_AUTHVAR_PRESENCE," \
		"SMM_APMC_OWNER_AUTHVAR_PRESENCE,selection)!=" \
		"SMM_APMC_CONSUMED_SUCCESS)route_fail_stop_untrusted(session);" \
		"status=payload_mm_authvar_presence_transaction_dispatch(" \
		"session->slot);"
	require_count(dispatch, adjacent, 1, "consume-dispatch adjacency")

	route_initializer = balanced_after(provision,
		"composed_policy=")
	arm_provision = balanced_after(arm,
		"payload_mm_authvar_presence_arm_transaction_provision(")
	arm_bind = balanced_after(arm,
		"payload_mm_authvar_presence_arm_protection_delegate_bind(")
	effective = balanced_after(arm, "staticbooleffective_proof_closure(")
	arm_initializer = balanced_after(arm_provision,
		"arm->wrapped_policy=(structpayload_mm_authvar_presence_transaction_policy)")
	if (route_initializer == "" || arm_initializer == "") {
		print "missing policy initializer" > "/dev/stderr"
		failed = 1
	}
	require_initializer(route_initializer, "revision",
		"PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_POLICY_REVISION")
	require_initializer(route_initializer, "size", "sizeof(composed_policy)")
	require_initializer(route_initializer, "prepare", "route_prepare")
	require_initializer(route_initializer, "commit", "route_commit")
	require_initializer(route_initializer, "abort", "route_abort")
	require_initializer(route_initializer, "dma_protected", "route_dma_protected")
	require_initializer(route_initializer, "claim_invocation", "route_claim")
	require_initializer(route_initializer, "complete_invocation", "route_complete")
	require_initializer(route_initializer, "fail_stop", "route_policy_fail_stop")
	require_initializer(route_initializer, "context", "&session->callback_context")
	require_initializer(route_initializer, "context_size",
		"sizeof(session->callback_context)")
	require_initializer(arm_initializer, "revision",
		"PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_POLICY_REVISION")
	require_initializer(arm_initializer, "size", "sizeof(arm->wrapped_policy)")
	require_initializer(arm_initializer, "prepare", "wrapped_prepare")
	require_initializer(arm_initializer, "commit", "wrapped_commit")
	require_initializer(arm_initializer, "abort", "wrapped_abort")
	require_initializer(arm_initializer, "dma_protected", "wrapped_dma_protected")
	require_initializer(arm_initializer, "claim_invocation", "wrapped_claim")
	require_initializer(arm_initializer, "complete_invocation", "wrapped_complete")
	require_initializer(arm_initializer, "fail_stop", "wrapped_fail_stop")
	require_initializer(arm_initializer, "context", "&arm->callback_context")
	require_initializer(arm_initializer, "context_size",
		"sizeof(arm->callback_context)")
	require_count(validation_bind,
		"payload_mm_authvar_presence_arm_protection_delegate_bind(arm," \
		"protected_storage,protected_storage_context," \
		"delegated_protected_storage,&session->callback_context," \
		"sizeof(session->callback_context))", 1, "exact delegate bind")
	require_count(provision,
		"payload_mm_authvar_presence_arm_transaction_provision(arm,slot," \
		"&composed_policy,&session->binding,page_verifier,page_receipt," \
		"delegated_protected_storage,&session->callback_context)", 1,
		"exact arm provision")
	require_count(arm_bind,
		"arm->delegated_protected_storage=delegated_protected_storage;", 1,
		"active delegated proof")
	require_count(arm_bind,
		"arm->delegated_protected_storage_context=" \
		"delegated_protected_storage_context;", 1,
		"active delegated context")
	require_count(arm_bind,
		"arm->delegated_protected_storage_context_size=" \
		"delegated_protected_storage_context_size;", 1,
		"active delegated context size")
	require_count(arm_bind,
		"arm->sealed_delegated_protected_storage=delegated_protected_storage;",
		1, "sealed delegated proof")
	require_count(arm_bind,
		"arm->sealed_delegated_protected_storage_context=" \
		"delegated_protected_storage_context;", 1,
		"sealed delegated context")
	require_count(arm_bind,
		"arm->sealed_delegated_protected_storage_context_size=" \
		"delegated_protected_storage_context_size;", 1,
		"sealed delegated context size")
	require_count(effective,
		"*proof=arm->delegated_protected_storage;", 1,
		"effective delegated proof")
	require_count(effective,
		"*context=arm->delegated_protected_storage_context;", 1,
		"effective delegated context")
	require_count(arm_provision,
		"payload_mm_authvar_presence_transaction_provision(slot," \
		"&arm->wrapped_policy,&arm->binding,&verifier,&receipt," \
		"wrapped_protected_storage,arm)", 1,
		"arm receiver proof delegation")
	if (failed)
		exit 1
}
