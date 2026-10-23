# Versioned HTML documentation

Each prepared or released version has its own directory, such as `0.0.1/`.
Keep published directories stable so links and installed archives continue to describe the
compiler they shipped with. The root `index.html` points to the latest version;
`versions.js` supplies the version selector on every page.

`development/` is a moving preview for the source tree after the latest release.
It is labeled as unreleased, and the root page continues to open the latest
published version. When preparing a release, copy the development guide to the
new numbered directory, label it as a candidate and verify its examples against
the release build. Add it to the version selector. At publication, remove the
candidate label, select it as `latestVersion` and update the root redirect.
The old numbered pages remain unchanged.

`0.2.1/` describes the latest release. The root redirect and `latestVersion`
select 0.2.1; older numbered pages retain their release content. Platform
validation is recorded in [the release guide](../release-0.2.1.md).

For a new patch, minor, or major release, update its title, feature boundary,
examples, and version selector's selected value, then append the version to
`versions.js` and change the root redirect. Keep links relative so the site works from a source checkout, an
installed package, and an extracted release archive without a server.
Version navigation uses explicit `index.html` files: directory URLs can show a
file listing when opened through `file://`. The shared `versions.js` handles
selection before older pages' inline handlers, preserving their release text.

Build `gloin_checks`, then run `build/gloin-check-docs` from the repository root after editing
the site. Each versioned page links to the installed Markdown API guides for exact
function signatures, ownership, costs, and errors.
