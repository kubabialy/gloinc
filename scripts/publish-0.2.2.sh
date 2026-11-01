#!/usr/bin/env bash
# Publish only the packages produced by this commit's successful platform jobs.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
if ! grep -Eq '^project\(gloinc VERSION 0\.2\.2([ )]|$)' CMakeLists.txt; then
  echo '0.2.2 publication does not apply to this source version.'
  exit 0
fi
[[ "${GITHUB_EVENT_NAME:-}" == push && "${GITHUB_REF:-}" == refs/heads/master ]]
[[ "${GITHUB_REPOSITORY:-}" == kubabialy/gloinc ]]
[[ "$(git rev-parse HEAD)" == "$GITHUB_SHA" ]]
tag=v0.2.2
mkdir -p release-evidence release-assets
for artifact in compiler-linux-x86_64-ci-reports compiler-linux-aarch64-ci-reports compiler-ci-reports; do
  gh run download "$GITHUB_RUN_ID" --repo "$GITHUB_REPOSITORY" \
    --name "$artifact" --dir "release-evidence/$artifact"
done
for platform in linux-x86_64 linux-aarch64 macos-arm64; do
  name="gloinc-0.2.2-$platform.tar.gz"
  # check-package.sh copies the validated archive to the report root only on success.
  mapfile -t archives < <(find release-evidence -path "*/package/$name" -type f)
  [[ ${#archives[@]} == 1 ]]
  archive=${archives[0]}
  (cd "$(dirname "$archive")" && sha256sum -c "$name.sha256")
  cp "$archive" "$archive.sha256" release-assets/
done
# Keep logs, XML, environment and build metadata, without duplicate install trees.
find release-evidence -type d -name 'work.*' -prune -exec rm -rf -- {} +
find release-evidence -type f -name '*.tar.gz*' -delete
tar -czf release-assets/gloinc-0.2.2-validation.tar.gz release-evidence
git archive --format=tar.gz --prefix=gloinc-0.2.2-source/ HEAD \
  > release-assets/gloinc-0.2.2-source.tar.gz
git rev-parse 'HEAD^{tree}' > release-evidence/source-tree.txt
node <<'NODE'
const fs = require('node:fs');
const crypto = require('node:crypto');
const files = fs.readdirSync('release-assets').filter(f => f.endsWith('.tar.gz')).sort();
const hashes = Object.fromEntries(files.map(f => [f, crypto.createHash('sha256')
  .update(fs.readFileSync('release-assets/' + f)).digest('hex')]));
for (const [file, hash] of Object.entries(hashes)) {
  fs.writeFileSync('release-assets/' + file + '.sha256', `${hash}  ${file}\n`);
}
fs.writeFileSync('release-assets/gloinc-0.2.2-provenance.json', JSON.stringify({
  version: '0.2.2', commit: process.env.GITHUB_SHA,
  tree: fs.readFileSync('release-evidence/source-tree.txt', 'utf8').trim(),
  run: `https://github.com/${process.env.GITHUB_REPOSITORY}/actions/runs/${process.env.GITHUB_RUN_ID}`,
  attempt: Number(process.env.GITHUB_RUN_ATTEMPT),
  platforms: ['Ubuntu 24.04 x86_64 / LLVM 21.1.8', 'Ubuntu 24.04 ARM64 / LLVM 21.1.8', 'macOS 15 ARM64 / LLVM 21.1.6'],
  gates: ['core', 'installed and relocated packages', 'serial/parallel complete-suite classification',
    'formatting', 'documentation', 'macOS compiler-only build', 'macOS ASan/UBSan'],
  artifacts: hashes
}, null, 2) + '\n');
const notes = fs.readFileSync('docs/release-notes-0.2.2.md', 'utf8').replace(
  /\]\((?!https?:\/\/|#)([^)]+)\)/g,
  (_, target) => `](https://github.com/${process.env.GITHUB_REPOSITORY}/blob/${process.env.GITHUB_SHA}/docs/${target})`);
fs.writeFileSync('release-evidence/release-notes.md', notes);
NODE
(cd release-assets && sha256sum gloinc-0.2.2-provenance.json > gloinc-0.2.2-provenance.json.sha256 && sha256sum -c ./*.sha256)
# Preserve an existing published release; a rerun must never replace its assets.
gh api "repos/$GITHUB_REPOSITORY/releases?per_page=100" > release-evidence/releases.json
release_state=$(node -e 'const r=require("./release-evidence/releases.json").find(r=>r.tag_name==="v0.2.2");process.stdout.write(r?(r.draft?"draft":"published"):"missing")')
# Pin the tag explicitly: a draft release may not create its tag until published.
tag_count=$(gh api "repos/$GITHUB_REPOSITORY/git/matching-refs/tags/$tag" \
  --jq '[.[] | select(.ref == "refs/tags/v0.2.2")] | length')
[[ "$tag_count" == 0 || "$tag_count" == 1 ]]
if [[ "$tag_count" == 0 ]]; then
  gh api --method POST "repos/$GITHUB_REPOSITORY/git/refs" \
    -f ref="refs/tags/$tag" -f sha="$GITHUB_SHA" > /dev/null
fi
tag_commit=$(gh api "repos/$GITHUB_REPOSITORY/commits/$tag" --jq .sha)
[[ "$tag_commit" == "$GITHUB_SHA" ]]
if [[ "$release_state" != missing ]]; then
  if [[ "$release_state" == published ]]; then
    echo '0.2.2 is already published from this commit; preserving its assets.'
    exit 0
  fi
else
  gh release create "$tag" --repo "$GITHUB_REPOSITORY" --target "$GITHUB_SHA" \
    --verify-tag --draft --title 'Gloin 0.2.2' --notes-file release-evidence/release-notes.md
fi
gh release upload "$tag" release-assets/* --repo "$GITHUB_REPOSITORY" --clobber
gh release edit "$tag" --repo "$GITHUB_REPOSITORY" --draft=false --latest
