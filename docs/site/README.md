# Versioned HTML documentation

Each released version has its own directory, such as `0.0.1/`. Keep published
directories stable so links and installed archives continue to describe the
compiler they shipped with. The root `index.html` points to the latest version;
`versions.js` supplies the version selector on every page.

For a new minor or major release, copy the previous directory to the new version
name, update its title, feature boundary, examples, and version selector's
selected value, then append the version to `versions.js` and change the root
redirect. Keep links relative so the site works from a source checkout, an
installed package, and an extracted release archive without a server.

Run `python3 scripts/check-doc-links.py` from the repository root after editing
the site. The 0.0.1 page links to the installed Markdown API guides for exact
function signatures, ownership, costs, and errors.
