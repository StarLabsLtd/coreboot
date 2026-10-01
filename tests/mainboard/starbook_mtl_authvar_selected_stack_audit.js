/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Optional diagnostic for an exact recorded artifact, not a build
 * dependency or a release-toolchain/hardware admission gate. Fixed callback
 * bindings and assembly frames below require source review before reuse.
 */
const fs = require('fs');
const path = require('path');
const child = require('child_process');
const crypto = require('crypto');
const heldDelivery = process.argv[2] === '--release-held-op9-recorded';
const privateDelivery = process.argv[2] === '--release-private-delivery-recorded';
const postClassifier = process.argv[2] === '--release-post-classifier-recorded';
const release = heldDelivery || privateDelivery || postClassifier || process.argv[2] === '--release-recorded';
const arguments_ = process.argv.slice(release ? 3 : 2);
const base = arguments_[0];
if (!base) throw new Error('Usage: node starbook_mtl_authvar_selected_stack_audit.js [--release-recorded|--release-post-classifier-recorded|--release-private-delivery-recorded|--release-held-op9-recorded] ON/smm [ROOT...]');
const record = heldDelivery ? {
	elf: '62b897781711fe574d601668e29630be083a0c4d602afc9bb604a0272d55fe5c',
	config: '0b5be5aa9b706d8f29e73b3815522022bf3378100d347578ebc5f51497ef8a33',
	annotations: '4fadc1727ba8d8112f261f05f35813e724b1427f0c7d38c1d50d60e4c9f95b4b',
	umodFrame: 44,
} : privateDelivery ? {
	elf: '4b339979a62be5b5d248b6ea92d42df43792ec826aa7f4f0b0a390256ff97893',
	config: 'a3123ad1f869777c1c8f7c7e57f0e2b1d5a1564c11f7c5d8ead5336631f61ee4',
	annotations: '5c6e51feadfe605d3199fa7aa2b2be1a2100c12860ac385e6ab867652130ce7f',
	umodFrame: 44,
} : postClassifier ? {
	elf: '5f839e8b6bfdf477651c4e91c04e314979ed57bc58eddb35a9339eb371e6d64a',
	config: '6016a34ab0c6957a05a3a5935876b8c17556e291e33ec67f450d1530ca385aa2',
	annotations: 'f430443957b5852d4a32abcc42682bb9a1e59a54b368d3bcba061b40239a8d8c',
	umodFrame: 44,
} : release ? {
	elf: '5cfcf28ca58500c2e637fff7d3f2870ec443f70f622d19c97f81de48b3242723',
	config: '6016a34ab0c6957a05a3a5935876b8c17556e291e33ec67f450d1530ca385aa2',
	annotations: 'fd0dac259e6b45701ee183eba642190bc2293f53e168f1e031359b65fb3bd50e',
	umodFrame: 44,
} : {
	elf: 'a4ff69f7391f8c749432b4b573681c1ed5905a991d8c12c9a0625cbc4b4e6f9f',
	config: '94e021c7b7f3cf591594121c32f70f3bd64f862cd84711b8b2ec2ba02af175f0',
	annotations: '12daff6718ab9c8acb24ff822beb438998158311558b5d35fa8df08e9cd26642',
	umodFrame: 60,
};
const hash = filename => crypto.createHash('sha256').update(fs.readFileSync(filename)).digest('hex');
if (hash(path.join(base, 'smm.elf')) !== record.elf ||
    hash(path.join(path.dirname(base), 'full.config')) !== record.config)
	throw new Error('Unreviewed artifact/configuration: revise the source/assembly manifest first');
const nodes = new Map(), edges = new Map(), byName = new Map(), aliases = new Set();
const annotationPaths = [];
const normalize = n => n.replace(/\.(constprop|isra|part)\.\d+/g, '.$1');
function walk(p) {
	for (const f of fs.readdirSync(p, {withFileTypes: true})) {
		const q = path.join(p, f.name);
		if (f.isDirectory()) walk(q);
		else if (f.name.endsWith('.ci') || f.name.endsWith('.su')) annotationPaths.push(q);
	}
}
function read(p, data) {
	for (const l of data.toString('utf8').split('\n')) {
		let m = l.match(/^node: \{ title: "([^"]+)" label: "([^\\"]+)/);
		if (m) {
			if (l.includes('shape : triangle')) aliases.add(m[1]);
			const s = l.match(/\\n(\d+) bytes \(([^)]+)\)/);
			if (s) {
				const name = normalize(m[2]);
				const old = nodes.get(m[1]);
				if (!old || old.size < +s[1])
					nodes.set(m[1], {name, size: +s[1], kind: s[2],
						source: l.match(/\\n([^\\]+):\d+:\d+/)?.[1]});
				if (!byName.has(name)) byName.set(name, new Set());
				byName.get(name).add(m[1]);
			}
		}
		m = l.match(/^edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"(?: label: "([^"]+)")?/);
		if (m) {
			if (!edges.has(m[1])) edges.set(m[1], []);
			edges.get(m[1]).push({to: m[2], site: m[3] || 'compiler-generated split/alias'});
		}
	}
}
walk(base);
const annotations = annotationPaths.sort().map(filename => ({
	filename, data: fs.readFileSync(filename),
}));
const annotationHash = crypto.createHash('sha256');
for (const {filename, data} of annotations) {
	annotationHash.update(path.relative(base, filename) + '\0' + data.length + '\0');
	annotationHash.update(data);
}
if (annotationHash.digest('hex') !== record.annotations)
	throw new Error('Unreviewed compiler annotation names/contents');
for (const {filename, data} of annotations)
	if (filename.endsWith('.ci')) read(filename, data);
// Actual linked i386 libgcc leaf prologues: four saved registers plus 28/44
// bytes of local reservation, no further stack adjustment/calls/recursion.
// Release __umoddi3 reserves 28 bytes; the native recorded version reserves 44.
for (const [name, size] of [['__udivdi3', 44], ['__umoddi3', record.umodFrame],
	['__udivmoddi4', 60], ['__divdi3', 0]]) {
	nodes.set(name, {name, size, kind: 'static'});
	byName.set(name, new Set([name]));
}
const retained = new Set(child.execFileSync('nm', ['--defined-only', base + '/smm.elf'],
	{encoding: 'utf8'}).split('\n').filter(l => /^\S+ [TtWw] /.test(l))
	.map(l => l.trim().split(/\s+/)[2].replace(/\.(constprop|isra|part)\.\d+/g, '.$1')));
// Source-backed callback supplied by the selected executor at lines 4162/3962
// and invoked by the retained authority callback at line 362.
edges.get('payload_mm_authvar_authority_decide').push({
	to: 'payload_mm_authvar_authority_provider_verify', site: 'explicit-selected-provider'
});
// The linked verification-only RSA vtable is fixed in mbedtls_verify_wrap.c.
for (const [from, to] of [
	['mbedtls_pk_verify_restartable', 'rsa_verify'],
	['mbedtls_pk_get_bitlen', 'rsa_get_bitlen'],
	['mbedtls_pk_can_do', 'rsa_can_do'],
	['mbedtls_pk_setup', 'rsa_alloc'],
	['mbedtls_pk_free', 'rsa_free'],
]) edges.get(from).push({to, site: 'explicit-selected-rsa-vtable'});
for (const [from, targets] of [
	['payload_mm_authvar_media_begin', ['begin']],
	['src/lib/payload_mm_authvar_media.c:read_backend', ['read_media']],
	['src/lib/payload_mm_authvar_media.c:sync_backend', ['sync_media']],
	['src/lib/payload_mm_authvar_media.c:end_backend', ['end']],
	['src/lib/payload_mm_authvar_media.c:sealed_sync_readback.isra.0', ['read_media', 'sync_media']],
	['payload_mm_authvar_media_program', ['program']],
	['payload_mm_authvar_media_erase', ['erase']],
]) for (const name of targets) edges.get(from).push({
	to: 'src/lib/payload_mm_authvar_smmstore.c:' + name, site: 'explicit-selected-media'
});
edges.get('src/lib/payload_mm_authvar_smm_bootstrap.c:spi_writes_restricted').push({
	to: 'src/mainboard/starlabs/starbook/variants/mtl/authvar_platform_smm.c:writes_are_private',
	site: 'explicit-selected-spi-proof'
});
edges.get('src/lib/payload_mm_authvar_presence_route_session.c:protected_exact').push({
	to: 'src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_receiver.c:protected_storage',
	site: 'explicit-selected-bootstrap-storage'
});
edges.get('src/lib/payload_mm_authvar_presence_arm.c:protected_range_bootstrap').push({
	to: 'src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_receiver.c:protected_storage',
	site: 'explicit-selected-bootstrap-storage'
});
function callbacks(from, targets, proof) {
	const list = edges.get(from);
	if (!list) throw new Error('Missing callback owner: ' + from);
	edges.set(from, list.filter(e => e.to !== '__indirect_call'));
	for (const to of targets) edges.get(from).push({to, site: proof});
}
// Remove unresolved placeholders only where an earlier explicit binding above
// already supplies the complete sealed callback set.
for (const [from, list] of edges)
	if (list.some(e => e.site.startsWith('explicit-selected-')))
		edges.set(from, list.filter(e => e.to !== '__indirect_call'));
const receipt = 'src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_receiver.c:';
callbacks(receipt + 'receipt_ranges_allowed', [receipt + 'ordinary_dram_range'],
	'receipt_receiver.c:388-393');
for (const name of ['verify_engine', 'read64_stable', 'verify_pci'])
	callbacks('src/mainboard/starlabs/starbook/variants/mtl/dma_smm_policy.c:' + name,
		[receipt + 'observer_read32'], 'receipt_receiver.c:643-648');
callbacks('src/mainboard/starlabs/starbook/variants/mtl/dma_smm_policy.c:verify_table_copies',
	[receipt + 'observer_sha256'], 'receipt_receiver.c:646');
callbacks('starbook_mtl_dma_smm_verify', heldDelivery ?
	[receipt + 'observer_verify_translation', receipt + 'observer_verify_translation_boot_private'] :
	[receipt + 'observer_verify_translation'],
	heldDelivery ? 'receipt_receiver.c:629/1430 cold observer or fixed held-fc override' :
		'receipt_receiver.c:647');
if (heldDelivery)
	callbacks('platform_payload_mm_authvar_service_delivery_held',
		[receipt + 'ordinary_dram_range'],
		'receipt_receiver.c:439/1513/1533 fixed native receipt-owner dependency');
const authority = 'src/mainboard/starlabs/starbook/variants/mtl/dma_smm_authority.c:';
callbacks('starbook_mtl_dma_requester_authority_derive', [authority + 'binding_read'],
	'dma_smm_authority.c:160-164');
callbacks('src/mainboard/starlabs/starbook/variants/mtl/dma_smm_requester_authority.c:config_read',
	[authority + 'ecam_read32'], 'dma_smm_authority.c:162');
callbacks('src/soc/intel/common/block/vtd/vtd_translation_verify.c:read_entry',
	[authority + 'table_read64'], 'dma_smm_authority.c:194');
const bootstrap = 'src/lib/payload_mm_authvar_smm_bootstrap.c:';
const mediaSpi = 'src/lib/payload_mm_authvar_smm_media_spi.c:';
callbacks(bootstrap + 'bootstrap_install', [mediaSpi + 'media_facts', mediaSpi + 'media_install'],
	'smm_media_spi.c:48-52');
callbacks(bootstrap + 'media_revalidate', [mediaSpi + 'media_facts'], 'smm_media_spi.c:51');
callbacks('payload_mm_authvar_contract_build',
	['smm_entry_owned', 'spi_writes_restricted', 'raw_flash_transport_absent',
	 'communication_reserved', 'store_owned'].map(n => bootstrap + n),
	'smm_bootstrap.c:676-680');
callbacks('payload_mm_authvar_authority_install', [bootstrap + 'authority_storage'],
	'smm_bootstrap.c:687');
callbacks('payload_mm_authvar_mor_seal_channel_install',
	[bootstrap + 'protected_storage', bootstrap + 'fixed_transport'], 'smm_bootstrap.c:714');
const route = 'src/lib/payload_mm_authvar_presence_route_session.c:';
const arm = 'src/lib/payload_mm_authvar_presence_arm.c:';
const policy = 'src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_authority_policy.c:';
callbacks(route + 'delegated_protected_storage',
	['src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_receiver.c:protected_storage'],
	'route_session.c:954-958 retained ORIGINAL callback');
for (const from of [route + 'route_fail_stop_untrusted', route + 'route_fail_stop_snapshot',
	arm + 'arm_fail_stop']) callbacks(from, [policy + 'fail_stop'],
	'route_session.c:960-961 sealed original authority failure callback');
for (const from of ['payload_mm_authvar_presence_transaction_provision',
	'src/lib/payload_mm_authvar_presence_transaction_receiver.c:callback_protected'])
	callbacks(from, [arm + 'wrapped_protected_storage'], 'presence_arm.c:1471');
callbacks(arm + 'wrapped_protected_storage', [route + 'delegated_protected_storage'],
	'presence_arm.c:987-1000 sealed effective delegated callback');
callbacks(arm + 'wrapped_protected_storage.part.0', [route + 'delegated_protected_storage'],
	'presence_arm.c:987-1000 sealed effective delegated callback');
callbacks(arm + 'protected_range_exact.part.0',
	['src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_receiver.c:protected_storage'],
	'arm original proof remains receiver callback after delegation');
callbacks(arm + 'arm_fail_stop_snapshot', [policy + 'fail_stop'],
	'original sealed failure callback');
callbacks('src/lib/payload_mm_authvar_presence_transaction_receiver.c:provisioning_fail_stop',
	[arm + 'wrapped_fail_stop'], 'presence_arm.c:1421');
// Actual retained boot_device_ro is fast_spi/mmap_boot.c, not the weak x86
// boot device. The xlate window points to a mem_region_device, never another
// xlate region. Context-specialize the second wrapper invocation, preserving
// its real compiler frame and direct callees without inventing a new C frame.
nodes.set('rdev_mmap_mem_leaf', {...nodes.get('rdev_mmap')});
edges.set('rdev_mmap_mem_leaf', (edges.get('rdev_mmap') || []).filter(e =>
	e.to !== '__indirect_call').concat({to: 'src/commonlib/region.c:mdev_mmap',
		site: 'fast_spi/mmap_boot.c:33-38 fixed window mem backend'}));
edges.set('src/commonlib/region.c:xlate_mmap',
	edges.get('src/commonlib/region.c:xlate_mmap').map(e =>
		e.to === 'rdev_mmap' ? {...e, to: 'rdev_mmap_mem_leaf'} : e));
callbacks('rdev_mmap', ['src/commonlib/region.c:xlate_mmap'],
	'fast_spi/mmap_boot.c:138-150 actual retained boot device');
callbacks('rdev_munmap', ['src/commonlib/region.c:xlate_munmap'],
	'fast_spi/mmap_boot.c:138-150 actual retained boot device');
const disassembly = child.execFileSync('objdump', ['-d', base + '/smm.elf'],
	{encoding: 'utf8', maxBuffer: 32 * 1024 * 1024}).split('\n');
const headers = disassembly.map(l => l.match(/^([0-9a-f]+) <([^>]+)>:/)).filter(Boolean);
const locations = child.execFileSync('addr2line', ['-f', '-e', base + '/smm.elf',
	...headers.map(m => '0x' + m[1])], {encoding: 'utf8', maxBuffer: 8 * 1024 * 1024})
	.trim().split('\n');
const nativeNodes = new Map(), addressNodes = new Map();
for (let i = 0; i < headers.length; i++) {
	const name = normalize(headers[i][2]);
	const file = locations[i * 2 + 1].replace(/:\d+.*$/, '').replace(/^.*\/src\//, 'src/');
	let candidates = [...(byName.get(name) || [])];
	const local = candidates.filter(k => nodes.get(k).source === file);
	if (local.length) candidates = local;
	else if (aliases.has(file + ':' + headers[i][2]))
		candidates = [resolveAlias(file + ':' + headers[i][2])];
	nativeNodes.set(headers[i][2], candidates);
	addressNodes.set(parseInt(headers[i][1], 16), candidates);
}
const nativeUnknown = [];
let nativeOwner;
for (const line of disassembly) {
	let m = line.match(/^([0-9a-f]+) <([^>]+)>:/);
	if (m) nativeOwner = addressNodes.get(parseInt(m[1], 16));
	m = line.match(/\b(?:call|jmp)\s+([0-9a-f]+) <([^>]+)>/);
	if (!m || !nativeOwner?.length) continue;
	const targetName = normalize(m[2].replace(/\+0x[0-9a-f]+$/, ''));
	const targets = addressNodes.get(parseInt(m[1], 16)) || [];
	for (const from of nativeOwner) {
		if (nodes.get(from).name === targetName) continue;
		if (from === 'src/commonlib/region.c:xlate_mmap' && targetName === 'rdev_mmap') continue;
		if (!targets.length && byName.get(targetName)?.has(from)) continue;
		if (!targets.length && [...aliases].some(a => normalize(a.split(':').pop()) ===
			targetName && resolveAlias(a) === from)) continue;
		if (!targets.length) nativeUnknown.push({from, to: targetName, line});
		for (const to of targets) {
			if (!edges.has(from)) edges.set(from, []);
			edges.get(from).push({to, site: 'actual-retained-ELF direct/tail'});
		}
	}
}
const adapter = 'src/soc/intel/common/block/smm/invocation_adapter.c:';
// Both provider_bind and fresh init install exactly these three callbacks.
for (const from of ['smm_invocation_evidence_claim',
	'src/cpu/x86/smm_invocation_evidence.c:restore_or_fail_stop',
	'smm_invocation_evidence_publish_and_request_close',
	'starbook_mtl_authvar_service_runtime_dispatch',
	'src/mainboard/starlabs/starbook/variants/mtl/authvar_service_runtime_dispatch.c:claim_current',
	'starbook_mtl_presence_bootstrap_receive',
	'starbook_mtl_presence_bootstrap_response_stage'])
	callbacks(from, ['match_apmc_write', 'read_value', 'write_value'].map(n => adapter + n),
		'fixed-provider-ops invocation_adapter.c:463,487');
callbacks('intel_smm_invocation_adapter_init_spans',
	['src/soc/intel/common/block/smm/invocation_adapter_provider.c:runtime_span'],
	'provider.c:160');
callbacks('mbedtls_asn1_traverse_sequence_of',
	['3rdparty/mbedtls/library/asn1parse.c:asn1_get_sequence_of_cb'], 'asn1parse.c:346');
callbacks('3rdparty/mbedtls/library/x509_crt.c:mbedtls_x509_crt_parse_der_internal', [],
	'only nocopy entry supplies NULL extension callback x509_crt.c:1370');
callbacks('3rdparty/mbedtls/library/x509_crt.c:x509_crt_verify_restartable_ca_cb.constprop.0.isra.0', [],
	'crypto.c:568,631 NULL verification callbacks');
callbacks('src/lib/payload_mm_authvar_presence_authority.c:page_guard',
	['dma_protected', 'cpu_rendezvous_active'].map(n =>
		'src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_authority_policy.c:' + n),
	'authority_policy.c:167');
callbacks('src/lib/payload_mm_authvar_presence_authority.c:cleanup_fail_stop',
	['src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_authority_policy.c:fail_stop'],
	'authority_policy.c:170');
for (const [from, target] of [
	['spi_setup_slave', 'fast_spi_flash_ctrlr_setup'],
	['spi_flash_probe', 'fast_spi_flash_probe'],
	['spi_flash_volatile_lease_read', 'fast_spi_flash_read'],
	['spi_flash_volatile_lease_write', 'fast_spi_flash_write'],
	['spi_flash_volatile_lease_erase', 'fast_spi_flash_erase'],
	['spi_flash_volatile_lease_sync', 'fast_spi_flash_status'],
]) callbacks(from, ['src/soc/intel/common/block/fast_spi/fast_spi_flash.c:' + target],
	'fixed MTL bus0 controller/flash ops fast_spi_flash.c:323,465 spi.c:21');
// The hardware sequencer has no generic transfer, claim, or release callbacks.
for (const from of ['spi_claim_bus', 'spi_release_bus', 'spi_xfer_vector'])
	callbacks(from, [], 'fixed hardware sequencer NULL generic hooks');
callbacks('spi_flash_generic_probe', [],
	'FastSPI NULL xfer forces failed RDID before any vendor after_probe callback');
let unknown, recursive, cache, reached;
function resolveAlias(k) {
	const seen = new Set();
	while (aliases.has(k)) {
		if (seen.has(k) || (edges.get(k) || []).length !== 1)
			throw new Error('Invalid compiler alias: ' + k);
		seen.add(k);
		k = edges.get(k)[0].to;
	}
	return k;
}
function bound(k, active = new Set(), derDepth = 0) {
	k = resolveAlias(k);
	reached.add(k);
	const der = k === 'src/lib/payload_mm_crypto/cms.c:der_validate';
	const vtd = k === 'src/soc/intel/common/block/vtd/vtd_translation_verify.c:verify_level';
	const recursionLimit = der ? 12 : vtd ? 3 : -1;
	const cacheKey = k + ':' + derDepth;
	if (cache.has(cacheKey)) return cache.get(cacheKey);
	if (active.has(k)) {
		if (recursionLimit >= 0 && derDepth <= recursionLimit)
			active = new Set([...active].filter(x => x !== k));
		else {
		recursive.add(k);
		return {size: 0, chain: ['RECURSION:' + k]};
		}
	}
	const n = nodes.get(k);
	if (!n) {
		unknown.add(k);
		return {size: 0, chain: ['UNKNOWN:' + k]};
	}
	if (n.kind !== 'static' && n.kind !== 'dynamic,bounded')
		throw new Error('Unbounded retained stack node: ' + k + ' (' + n.kind + ')');
	active = new Set(active);
	active.add(k);
	let best = {size: 0, chain: []};
	for (const e of edges.get(k) || []) {
		const target = e.to === 'mbedtls_rsa_parse_pubkey' ?
			'__wrap_mbedtls_rsa_parse_pubkey' : e.to;
		if (recursionLimit >= 0 && resolveAlias(target) === k &&
		    derDepth === recursionLimit) continue;
		const resolved = resolveAlias(target);
		const candidates = nodes.has(resolved) ? [resolved] : [...(byName.get(resolved) || [])];
		const choices = candidates.filter(c => {
			const name = nodes.get(c).name;
			return retained.has(name) || [...retained].some(s => s.startsWith(name + '.'));
		});
		if (candidates.length && !choices.length) continue;
		if (!choices.length) {
			unknown.add(k + ' -> ' + e.to + ' @' + e.site);
			continue;
		}
		for (const c of choices) {
			const b = bound(c, active, recursionLimit >= 0 && c === k ?
				derDepth + 1 : derDepth);
			if (b.size + 4 > best.size) best = {size: b.size + 4, chain: b.chain};
		}
	}
	const r = {size: n.size + best.size, chain: [n.name + '[' + n.size + ']', ...best.chain]};
	cache.set(cacheKey, r);
	return r;
}
const roots = arguments_.length > 1 ? arguments_.slice(1) :
	['service_bootstrap_dispatch', 'starbook_mtl_authvar_service_runtime_dispatch'];
for (const name of roots) {
	if (!byName.has(name)) throw new Error('Unknown audit root: ' + name);
	unknown = new Set(); recursive = new Set(); cache = new Map(); reached = new Set();
	// Prefer the actual global definition, as direct-edge resolution does. A
	// same-named discarded weak stub can also appear in compiler annotations.
	for (const k of nodes.has(name) ? [name] : byName.get(name) || []) {
		const r = bound(k);
		console.log(JSON.stringify({root: k, candidate_nested_c_bytes: r.size, chain: r.chain,
			unresolved: [...unknown], recursions: [...recursive],
			unresolved_native_targets: nativeUnknown.filter(e => reached.has(e.from))}));
		if (unknown.size || recursive.size || nativeUnknown.some(e => reached.has(e.from)))
			process.exitCode = 1;
	}
}
