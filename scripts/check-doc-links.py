#!/usr/bin/env python3
"""Check local links in the versioned HTML docs without a web server."""

from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.links = []

    def handle_starttag(self, tag, attributes):
        attrs = dict(attributes)
        if "id" in attrs:
            self.ids.add(attrs["id"])
        for key in ("href", "src"):
            if key in attrs:
                self.links.append(attrs[key])


site = Path("docs/site")
pages = {}
for path in site.rglob("*.html"):
    parser = Page()
    parser.feed(path.read_text(encoding="utf-8"))
    pages[path.resolve()] = parser

failures = []
for path, page in pages.items():
    for raw in page.links:
        url = urlsplit(raw)
        if url.scheme or url.netloc or raw.startswith("/"):
            continue
        target = (path.parent / unquote(url.path)).resolve() if url.path else path
        if target.is_dir():
            target /= "index.html"
        if not target.is_file():
            failures.append(f"{path}: missing {raw}")
        elif url.fragment and target in pages and unquote(url.fragment) not in pages[target].ids:
            failures.append(f"{path}: missing anchor {raw}")

for failure in failures:
    print(failure)
if failures:
    raise SystemExit(1)
print(f"Checked {len(pages)} HTML pages and their local links")
