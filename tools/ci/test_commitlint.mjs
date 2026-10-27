import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const repo = fileURLToPath(new URL('../../', import.meta.url));
const lint = path.join(repo, 'node_modules/.bin/commitlint');
const config = path.join(repo, 'commitlint.config.js');
const rangeScript = path.join(repo, 'tools/ci/lint_commits.sh');
const zeroSha = '0'.repeat(40);

function checkMessage(message) {
  return spawnSync(lint, ['--config', config], { input: message, encoding: 'utf8' });
}

test('commit types, breaking changes and header limit follow project rules', () => {
  for (const type of ['feat', 'fix', 'docs', 'style', 'refactor', 'perf', 'test',
    'chore', 'ci', 'build', 'revert', 'delete', 'deleted', 'remove', 'removed', 'diag', 'tweak']) {
    assert.equal(checkMessage(`${type}(ci): validate firmware`).status, 0, type);
  }
  assert.equal(checkMessage('fix(ci)!: change validation contract').status, 0);
  assert.equal(checkMessage(`fix(ci): ${'x'.repeat(191)}`).status, 0);
  for (const message of ['unknown(ci): invalid type', 'invalid header',
    `fix(ci): ${'x'.repeat(192)}`]) {
    const result = checkMessage(message);
    assert.equal(result.status, 1, `${message}: ${result.stderr}`);
  }
});

test('PR and push ranges catch invalid intermediate commits; first push checks tip', () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'tentacleos-commitlint-'));
  const env = { ...process.env, GIT_AUTHOR_NAME: 'CI test', GIT_AUTHOR_EMAIL: 'ci@example.invalid',
    GIT_COMMITTER_NAME: 'CI test', GIT_COMMITTER_EMAIL: 'ci@example.invalid' };
  function git(...args) {
    const result = spawnSync('git', ['-c', 'core.hooksPath=/dev/null',
      '-c', 'commit.gpgsign=false', ...args], { cwd: directory, env, encoding: 'utf8' });
    assert.equal(result.status, 0, result.stderr);
    return result.stdout.trim();
  }
  function checkRange(base, head) {
    return spawnSync('bash', [rangeScript, base, head],
      { cwd: directory, env, encoding: 'utf8' });
  }
  try {
    git('init', '--template=');
    git('commit', '--allow-empty', '-m', 'chore(ci): initialize');
    const base = git('rev-parse', 'HEAD');
    assert.equal(checkRange(zeroSha, base).status, 0);
    git('commit', '--allow-empty', '-m', 'fix(ci): validate settings');
    assert.equal(checkRange(base, git('rev-parse', 'HEAD')).status, 0);
    git('commit', '--allow-empty', '-m', 'unknown(ci): invalid type');
    assert.equal(checkRange(zeroSha, git('rev-parse', 'HEAD')).status, 1);
    git('commit', '--allow-empty', '-m', 'test(ci): cover ranges');
    const head = git('rev-parse', 'HEAD');
    assert.equal(checkRange(base, head).status, 1);
    assert.equal(checkRange(zeroSha, head).status, 0);
    assert.notEqual(checkRange('missing-base', head).status, 0);
  } finally {
    rmSync(directory, { recursive: true, force: true });
  }
});
