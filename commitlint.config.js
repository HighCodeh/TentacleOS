module.exports = {
  extends: ['@commitlint/config-conventional'],
  rules: {
    'type-enum': [2, 'always', [
      'feat', 'fix', 'docs', 'style', 'refactor',
      'perf', 'test', 'chore', 'ci', 'build', 'revert',
      'delete', 'deleted', 'remove', 'removed'
    ]],
    'scope-case': [0],
    'subject-case': [0],
    'header-max-length': [2, 'always', 200],
  },
};
