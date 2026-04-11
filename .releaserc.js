const { execSync } = require("child_process");

const loginCache = {};

function resolveGitHubLogin(hash, email, fallback) {
  if (email && loginCache[email]) return loginCache[email];

  try {
    const repo = process.env.GITHUB_REPOSITORY;
    const token = process.env.GITHUB_TOKEN;
    if (!repo || !token || !hash) return fallback;

    const result = execSync(
      `curl -sf -H "Authorization: token ${token}" "https://api.github.com/repos/${repo}/commits/${hash}"`,
      { encoding: "utf-8", timeout: 10000 },
    );
    const login = JSON.parse(result).author?.login;
    if (login) {
      if (email) loginCache[email] = login;
      return login;
    }
  } catch {
    // API failed, use fallback
  }
  return fallback;
}

module.exports = {
  branches: ["main"],
  plugins: [
    [
      "@semantic-release/commit-analyzer",
      {
        preset: "conventionalcommits",
        releaseRules: [
          { type: "feat", release: "minor" },
          { type: "fix", release: "patch" },
          { type: "perf", release: "patch" },
          { type: "refactor", release: "patch" },
          { type: "delete", release: "patch" },
          { type: "deleted", release: "patch" },
          { type: "remove", release: "patch" },
          { type: "removed", release: "patch" },
          { breaking: true, release: "major" },
        ],
      },
    ],
    [
      "@semantic-release/release-notes-generator",
      {
        preset: "conventionalcommits",
        writerOpts: {
          commitPartial:
            "* {{#if scope}}**{{scope}}:** {{/if}}{{subject}} ({{hash}}) - [@{{authorLogin}}](https://github.com/{{authorLogin}})\n",
          finalizeContext: (context) => {
            if (context.commitGroups) {
              for (const group of context.commitGroups) {
                for (const commit of group.commits) {
                  commit.authorLogin = resolveGitHubLogin(
                    commit.hash,
                    commit.authorEmail,
                    commit.authorName || "unknown",
                  );
                }
              }
            }
            return context;
          },
        },
      },
    ],
    [
      "@semantic-release/changelog",
      {
        changelogFile: "CHANGELOG.md",
      },
    ],
    [
      "@semantic-release/exec",
      {
        prepareCmd:
          "sed -i 's/\"version\": *\"[^\"]*\"/\"version\": \"${nextRelease.version}\"/' firmware_p4/assets/config/OTA/firmware.json firmware_c5/assets/config/OTA/firmware.json && sed -i 's/#define FIRMWARE_VERSION \"[^\"]*\"/#define FIRMWARE_VERSION \"${nextRelease.version}\"/' firmware_p4/components/Service/ota/include/ota_version.h",
      },
    ],
    [
      "@semantic-release/git",
      {
        assets: [
          "CHANGELOG.md",
          "firmware_p4/assets/config/OTA/firmware.json",
          "firmware_c5/assets/config/OTA/firmware.json",
          "firmware_p4/components/Service/ota/include/ota_version.h",
        ],
        message: "chore(release): v${nextRelease.version}",
      },
    ],
    "@semantic-release/github",
  ],
};
