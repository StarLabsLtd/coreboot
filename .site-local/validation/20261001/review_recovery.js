// SPDX-License-Identifier: GPL-2.0-only
// Optional recovery diagnostic: writes artifacts, never modifies source trees.
'use strict';
const fs = require('fs');
const path = require('path');
const child = require('child_process');
const crypto = require('crypto');
const trees = [
 '/home/sean/.review-worktrees/failstop-v2.RnX0RD',
 '/home/sean/.review-worktrees/failstop.dBEhZz',
 '/home/sean/.review-worktrees/smm-fail-stop',
 '/home/sean/.review-worktrees/smm-final-review.u1SXQm/tree',
 '/home/sean/.review-worktrees/smm-topology-final',
 '/home/sean/.review-worktrees/smm-topology-v2',
 '/home/sean/Documents/.coreboot-worktrees/review-failstop-v3.IHAxmZ',
 '/home/sean/Documents/.coreboot-worktrees/review-failstop-v4.lVzHZi',
 '/home/sean/Documents/.coreboot-worktrees/review-generic-workspace-v2',
 '/home/sean/Documents/.coreboot-worktrees/review-generic-workspace-v3',
 '/home/sean/Documents/.coreboot-worktrees/review-mtl-loader-v5.KgipEi',
 '/home/sean/Documents/.coreboot-worktrees/review-mtl-loader-v6.fIjJPy',
 '/home/sean/Documents/.coreboot-worktrees/review-mtl-loader-v7.D4ZDBt',
 '/home/sean/Documents/.coreboot-worktrees/review-mtl-provider.20832.18937',
];
const output = path.resolve(process.argv[2] || '');
const resume = process.argv[3] === '--resume';
if (!process.argv[2] || (fs.existsSync(output) && !resume))
 throw new Error('Supply a new, nonexistent recovery artifact directory');
function run(command, args, cwd, input) {
 return child.execFileSync(command, args, {cwd, input, maxBuffer: 128 * 1024 * 1024,
  stdio: ['pipe', 'pipe', 'pipe']});
}
function git(repo, args) {
 return run('git', ['-c', 'core.quotePath=true', '-C', repo, ...args]);
}
function hash(bytes) {
 return crypto.createHash('sha256').update(bytes).digest('hex');
}
function preflight(bytes, label) {
 if (/-----BEGIN (?:[A-Z ]+ )?PRIVATE KEY-----|github_pat_[A-Za-z0-9_]{20,}|gh[pousr]_[A-Za-z0-9]{30,}|sk-proj-[A-Za-z0-9_-]{20,}/.test(bytes.toString('utf8')))
  throw new Error(`HOLD: credential/private-key-like marker in ${label}`);
}
const diffArgs = ['diff', '--no-ext-diff', '--no-textconv', '--binary', '--full-index',
 '--ignore-submodules=all'];
function fileState(repo, relative) {
 const full = path.join(repo, relative);
 const stat = fs.lstatSync(full);
 if (stat.isSymbolicLink())
  return {path: relative, kind: 'symlink', value: fs.readlinkSync(full)};
 if (!stat.isFile()) throw new Error(`HOLD: unsupported untracked object ${full}`);
 const bytes = fs.readFileSync(full);
 preflight(bytes, full);
 return {path: relative, kind: 'file', sha256: hash(bytes), bytes: bytes.length,
  executable: Boolean(stat.mode & 0o111)};
}
function snapshot(repo) {
 const index = git(repo, [...diffArgs, '--cached']);
 const worktree = git(repo, diffArgs);
 preflight(worktree, `${repo} worktree patch`);
 const head = git(repo, ['rev-parse', 'HEAD']).toString().trim();
 const names = git(repo, ['ls-files', '--others', '--exclude-standard', '-z'])
  .toString().split('\0').filter(Boolean).sort();
 const ignoredNames = git(repo, ['ls-files', '--others', '--ignored', '--exclude-standard', '-z'])
  .toString().split('\0').filter(name => /\.(rom|log|txt|json|sh|py|js|c|h)$|(^|\/)\.config(\.old)?$|(^|\/)full\.config$/.test(name)).sort();
 for (const name of names)
  if (path.isAbsolute(name) || name.split('/').includes('..'))
   throw new Error(`HOLD: unsafe untracked path ${name}`);
 let publicDeletionReference = false;
 try { preflight(index, `${repo} index patch`); } catch (error) {
  if (!repo.endsWith('/util/nvidia/cbootimage') ||
      head !== '80c499ebbe8a64aa2ee4222e6a8f342f465f6680' || worktree.length || names.length ||
      git(repo, ['ls-files', '--stage']).length ||
      git(repo, ['rev-list', 'HEAD', '--not', '--remotes']).length)
   throw error;
  // The known upstream sample key is NOT copied; preserve this state by names.
  publicDeletionReference = true;
 }
 let remotes = '';
 try { remotes = git(repo, ['config', '--get-regexp', '^remote\\..*\\.url$']).toString(); }
 catch (error) { if (error.status !== 1) throw error; }
 const publicRemotes = remotes.trim().split('\n').filter(Boolean).map(line =>
  line.replace(/(https?:\/\/)[^\/@]+@/g, '$1'));
 preflight(Buffer.from(publicRemotes.join('\n')), `${repo} public remote metadata`);
 const publicSourceReference = repo.endsWith('/contrib/mbed-tls') &&
  head === '0bebf8b8c7f07abe3571ded48a11aa907a1ffb20' && !index.length &&
  !worktree.length && !names.length && !ignoredNames.length;
 return {head, publicRemotes, publicDeletionReference, publicSourceReference,
  indexTree: git(repo, ['write-tree']).toString().trim(),
  refs: git(repo, ['for-each-ref', '--points-at', 'HEAD', '--format=%(refname)'])
   .toString().trim().split('\n').filter(Boolean).sort(),
  indexHash: hash(index), worktreeHash: hash(worktree),
  untracked: names.map(name => fileState(repo, name)),
  ignoredRecovery: ignoredNames.map(name => fileState(repo, name)), index, worktree};
}
function comparable(state) {
 const {index, worktree, ...metadata} = state;
 return metadata;
}
function capture(repo, directory) {
 const before = snapshot(repo);
 const raw = git(repo, ['diff', '--cached', '--raw']).toString();
 if (/^:160000 |^:[0-9]+ 160000 /m.test(raw))
  throw new Error(`HOLD: staged gitlink modification requires separate handling: ${repo}`);
 const unique = before.publicSourceReference ? [] : git(repo, ['rev-list', 'HEAD', '--not', '--remotes'])
  .toString().trim().split('\n').filter(Boolean);
 if (unique.length) {
  const objects = git(repo, ['rev-list', '--objects', 'HEAD', '--not', '--remotes'])
   .toString().trim().split('\n').filter(Boolean);
  for (const object of objects) {
   const oid = object.split(' ')[0];
   if (git(repo, ['cat-file', '-t', oid]).toString().trim() === 'blob')
    preflight(git(repo, ['cat-file', 'blob', oid]), `${repo} unique blob ${oid}`);
  }
 }
 fs.mkdirSync(directory, {recursive: true});
 fs.writeFileSync(path.join(directory, 'index.patch'), before.publicDeletionReference ? '' : before.index);
 if (before.publicDeletionReference)
  fs.writeFileSync(path.join(directory, 'index-deletions.json'), JSON.stringify(
   git(repo, ['ls-tree', '-r', '--name-only', 'HEAD']).toString().trim().split('\n')) + '\n');
 fs.writeFileSync(path.join(directory, 'worktree.patch'), before.worktree);
 if (before.untracked.length)
  run('tar', ['--null', '-czf', path.join(directory, 'untracked.tar.gz'), '-C', repo,
   '-T', '-'], undefined, Buffer.from(before.untracked.map(item => item.path).join('\0') + '\0'));
 if (before.ignoredRecovery.length)
  run('tar', ['--null', '-czf', path.join(directory, 'ignored-recovery.tar.gz'), '-C', repo,
   '-T', '-'], undefined, Buffer.from(before.ignoredRecovery.map(item => item.path).join('\0') + '\0'));
 if (unique.length) {
  git(repo, ['bundle', 'create', path.join(directory, 'unique.bundle'), 'HEAD', '--not', '--remotes']);
  git(repo, ['bundle', 'verify', path.join(directory, 'unique.bundle')]);
 }
 const after = snapshot(repo);
 if (JSON.stringify(comparable(before)) !== JSON.stringify(comparable(after)))
  throw new Error(`HOLD: source changed while capturing ${repo}`);
 return {...comparable(before), uniqueCommits: unique,
  artifacts: fs.readdirSync(directory).sort().map(name => ({name,
   sha256: hash(fs.readFileSync(path.join(directory, name)))}))};
}
function restore(repo, directory, metadata, restored) {
 if (metadata.publicSourceReference) {
  run('git', ['init', '--quiet', restored]);
  git(restored, ['fetch', '--quiet', '--depth=1',
   'https://github.com/Mbed-TLS/mbedtls.git', metadata.head]);
 } else {
  run('git', ['clone', '--quiet', '--shared', '--no-checkout', repo, restored]);
 }
 git(restored, ['checkout', '--quiet', '--detach', metadata.head]);
 if (metadata.publicDeletionReference) {
  const names = JSON.parse(fs.readFileSync(path.join(directory, 'index-deletions.json')));
  const actual = git(restored, ['ls-tree', '-r', '--name-only', 'HEAD']).toString().trim().split('\n');
  if (JSON.stringify(names) !== JSON.stringify(actual))
   throw new Error('HOLD: public deletion-only names do not match exact source HEAD');
  for (const name of names) {
   if (path.isAbsolute(name) || name.split('/').includes('..')) throw new Error('Unsafe deletion name');
   fs.rmSync(path.join(restored, name));
  }
  git(restored, ['read-tree', '--empty']);
 }
 if (fs.statSync(path.join(directory, 'index.patch')).size)
  git(restored, ['apply', '--index', '--binary', path.join(directory, 'index.patch')]);
 if (fs.statSync(path.join(directory, 'worktree.patch')).size)
  git(restored, ['apply', '--binary', path.join(directory, 'worktree.patch')]);
 if (metadata.untracked.length)
  run('tar', ['-xzf', path.join(directory, 'untracked.tar.gz'), '-C', restored]);
 if (metadata.ignoredRecovery.length)
  run('tar', ['-xzf', path.join(directory, 'ignored-recovery.tar.gz'), '-C', restored]);
 const state = snapshot(restored);
 for (const key of ['head', 'indexTree', 'indexHash', 'worktreeHash', 'untracked', 'ignoredRecovery'])
  if (JSON.stringify(state[key]) !== JSON.stringify(metadata[key]))
   throw new Error(`HOLD: restored ${key} differs in ${repo}`);
}
fs.mkdirSync(output, {recursive: true});
const manifest = resume ? JSON.parse(fs.readFileSync(path.join(output, 'manifest.json'))) :
 {format: 1, qualification: 'Recovery snapshots, not build/boot validation',
 credentialPreflight: 'Reject private-key PEM and known GitHub/OpenAI token markers; no secret contents printed',
 sourceRecovery: 'Incremental bundles require their recorded public-parent prerequisites; main source refs retained',
 trees: []};
for (let index = 0; index < trees.length; index++) {
 const repo = trees[index];
 const id = `${String(index + 1).padStart(2, '0')}-${path.basename(repo === trees[3] ? path.dirname(repo) : repo)}`;
 if (index < manifest.trees.length) {
  const previous = manifest.trees[index];
  if (previous.source !== repo || previous.id !== id)
   throw new Error('HOLD: resume manifest does not match fixed source inventory');
  for (const [source, metadata] of [[repo, previous.main], ...previous.submodules.map(
   module => [path.join(repo, module.path), module.state])]) {
   const current = comparable(snapshot(source));
   if (metadata.publicSourceReference === undefined)
    metadata.publicSourceReference = false;
   for (const key of Object.keys(current))
    if (JSON.stringify(current[key]) !== JSON.stringify(metadata[key]))
     throw new Error(`HOLD: completed source changed before resume: ${source}/${key}`);
  }
  console.log(`${id}: unchanged prior verified capture retained`);
  continue;
 }
 const directory = path.join(output, id);
 const main = capture(repo, path.join(directory, 'main'));
 const modules = git(repo, ['ls-files', '--stage']).toString().split('\n')
  .filter(line => line.startsWith('160000 ')).map(line => line.split('\t')[1]);
 const nested = run('find', [repo, '-name', '.git', '-print', '-prune']).toString()
  .trim().split('\n').filter(Boolean).map(name => path.relative(repo, path.dirname(name)))
  .filter(name => name && !modules.includes(name));
 const entry = {id, source: repo, main, submodules: []};
 for (const module of [...modules, ...nested]) {
  const source = path.join(repo, module);
  if (!fs.existsSync(path.join(source, '.git'))) continue;
  const state = snapshot(source);
  const unique = git(source, ['rev-list', 'HEAD', '--not', '--remotes']).toString().trim();
  if (!state.index.length && !state.worktree.length && !state.untracked.length && !unique)
   continue;
  const subDirectory = path.join(directory, 'submodules', module);
  entry.submodules.push({path: module, state: capture(source, subDirectory)});
 }
 const scratch = fs.mkdtempSync('/tmp/coreboot-review-restore-');
 try {
  const restored = path.join(scratch, 'main');
  restore(repo, path.join(directory, 'main'), main, restored);
  for (const module of entry.submodules) {
   const target = path.join(restored, module.path);
   fs.rmSync(target, {recursive: true, force: true});
   restore(path.join(repo, module.path), path.join(directory, 'submodules', module.path),
    module.state, target);
  }
  entry.restoreVerified = true;
 } finally {
  fs.rmSync(scratch, {recursive: true, force: true});
 }
 manifest.trees.push(entry);
 fs.writeFileSync(path.join(output, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
 console.log(`${id}: main/submodule index, worktree and untracked restoration PASS`);
}
console.log('Fourteen review recovery snapshots: PASS; no source tree changed or deleted');
