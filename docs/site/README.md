# Versioned HTML documentation

Each prepared or released version has its own directory, such as `0.0.1/`.
Keep published directories stable so links and installed archives continue to describe the
compiler they shipped with. The root `index.html` points to the latest version;
`versions.js` supplies the version selector on every page.

`development/` is a moving preview for the source tree after the latest release.
It is labeled as unreleased, and the root page continues to open the latest
published version. When preparing a release, copy the development guide to the
new numbered directory, replace development wording with that version, verify
every example against the release build, then update `versions.js` and the root
redirect. The old numbered pages remain unchanged.

`0.1.0/` is currently a numbered **candidate** page. It appears in the selector,
but the root redirect and `latestVersion` remain at published 0.0.4. On release,
remove candidate wording, set `latestVersion` to 0.1.0, and update the root
redirect after final package validation and publication approval.

For a new patch, minor, or major release, update its title, feature boundary,
examples, and version selector's selected value, then append the version to
`versions.js` and change the root redirect. Keep links relative so the site works from a source checkout, an
installed package, and an extracted release archive without a server.

Run `python3 scripts/check-doc-links.py` from the repository root after editing
the site. Each versioned page links to the installed Markdown API guides for exact
function signatures, ownership, costs, and errors.
