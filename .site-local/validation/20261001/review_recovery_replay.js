// SPDX-License-Identifier: GPL-2.0-only
// Optional archive replay; only its fresh scratch clones are modified.
'use strict';
const fs = require('fs');
const path = require('path');
const child = require('child_process');
const crypto = require('crypto');
const directory = path.resolve(process.argv[2] || '');
if (!process.argv[2]) throw new Error('Supply the extracted artifact directory');
const manifest = JSON.parse(fs.readFileSync(path.join(directory, 'manifest.json')));
if (manifest.format !== 1 || manifest.trees.length !== 14)
 throw new Error('Unexpected recovery manifest');
function run(command, args, cwd) {
 return child.execFileSync(command, args, {cwd, maxBuffer: 128 * 1024 * 1024,
  stdio: ['ignore', 'pipe', 'pipe']});
}
function git(repo, args) {
 return run('git', ['-c', 'core.quotePath=true', '-C', repo, ...args]);
}
function hash(bytes) {
 return crypto.createHash('sha256').update(bytes).digest('hex');
}
function safe(name) {
 if (path.isAbsolute(name) || name.split('/').includes('..'))
  throw new Error(`Unsafe artifact-relative path: ${name}`);
 return name;
}
function files(repo, expected) {
 return expected.map(item => {
  const filename = path.join(repo, safe(item.path));
  const stat = fs.lstatSync(filename);
  if (stat.isSymbolicLink())
   return {path: item.path, kind: 'symlink', value: fs.readlinkSync(filename)};
  if (!stat.isFile()) throw new Error('Unexpected restored file kind');
  const bytes = fs.readFileSync(filename);
  return {path: item.path, kind: 'file', sha256: hash(bytes), bytes: bytes.length,
   executable: Boolean(stat.mode & 0o111)};
 });
}
function restore(source, artifacts, metadata, destination) {
 for (const item of metadata.artifacts)
  if (hash(fs.readFileSync(path.join(artifacts, safe(item.name)))) !== item.sha256)
   throw new Error(`Artifact hash mismatch: ${artifacts}/${item.name}`);
 // Local source caches are optional accelerators, not content authority.
 let repository = source;
 if (!fs.existsSync(path.join(source, '.git'))) {
  const remote = metadata.publicRemotes.find(line => /^remote\.[^.]+\.url /.test(line));
  if (!remote) throw new Error(`No recorded public recovery remote: ${source}`);
  repository = remote.slice(remote.indexOf(' ') + 1);
 }
 if (metadata.publicSourceReference) {
  run('git', ['init', '--quiet', destination]);
  git(destination, ['fetch', '--quiet', '--depth=1',
   'https://github.com/Mbed-TLS/mbedtls.git', metadata.head]);
 } else {
  run('git', ['clone', '--quiet', '--no-checkout', repository, destination]);
 }
 const bundle = path.join(artifacts, 'unique.bundle');
 if (fs.existsSync(bundle)) {
  git(destination, ['bundle', 'verify', bundle]);
  git(destination, ['fetch', '--quiet', bundle, 'HEAD']);
 }
 git(destination, ['checkout', '--quiet', '--detach', metadata.head]);
 if (metadata.publicDeletionReference) {
  const names = JSON.parse(fs.readFileSync(path.join(artifacts, 'index-deletions.json')));
  const actual = git(destination, ['ls-tree', '-r', '--name-only', 'HEAD'])
   .toString().trim().split('\n');
  if (JSON.stringify(names) !== JSON.stringify(actual))
   throw new Error('Public deletion reference differs from exact public HEAD');
  for (const name of names) fs.rmSync(path.join(destination, safe(name)));
  git(destination, ['read-tree', '--empty']);
 }
 for (const [name, args] of [['index.patch', ['apply', '--index', '--binary']],
                            ['worktree.patch', ['apply', '--binary']]]) {
  const filename = path.join(artifacts, name);
  if (fs.statSync(filename).size) git(destination, [...args, filename]);
 }
 for (const name of ['untracked', 'ignoredRecovery']) {
  if (!metadata[name].length) continue;
  const archive = path.join(artifacts,
   name === 'untracked' ? 'untracked.tar.gz' : 'ignored-recovery.tar.gz');
  for (const member of run('tar', ['-tzf', archive]).toString().trim().split('\n')) safe(member);
  run('tar', ['-xzf', archive, '-C', destination]);
 }
 const diff = ['diff', '--no-ext-diff', '--no-textconv', '--binary', '--full-index',
  '--ignore-submodules=all'];
 const checks = {
  head: git(destination, ['rev-parse', 'HEAD']).toString().trim(),
  indexTree: git(destination, ['write-tree']).toString().trim(),
  indexHash: hash(git(destination, [...diff, '--cached'])),
  worktreeHash: hash(git(destination, diff)),
  untracked: files(destination, metadata.untracked),
  ignoredRecovery: files(destination, metadata.ignoredRecovery),
 };
 for (const [key, value] of Object.entries(checks))
  if (JSON.stringify(value) !== JSON.stringify(metadata[key]))
   throw new Error(`Restored ${key} mismatch: ${source}`);
 const names = git(destination, ['ls-files', '--others', '--exclude-standard', '-z'])
  .toString().split('\0').filter(Boolean).sort();
 if (JSON.stringify(names) !== JSON.stringify(metadata.untracked.map(item => item.path)))
  throw new Error(`Unexpected restored untracked names: ${source}`);
}
for (const entry of manifest.trees) {
 const scratch = fs.mkdtempSync('/tmp/coreboot-review-independent-replay-');
 try {
  const base = path.join(directory, safe(entry.id));
  restore(entry.source, path.join(base, 'main'), entry.main, path.join(scratch, 'main'));
  for (let index = 0; index < entry.submodules.length; index++) {
   const module = entry.submodules[index];
   restore(path.join(entry.source, safe(module.path)),
    path.join(base, 'submodules', module.path), module.state,
    path.join(scratch, `module-${index}`));
  }
  console.log(`${entry.id}: independent artifact/index/worktree/untracked replay PASS`);
 } finally {
  fs.rmSync(scratch, {recursive: true, force: true});
 }
}
console.log('Fourteen recovery states replay PASS; originals unchanged');
