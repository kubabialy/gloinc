# fast_float

Vendored from [fastfloat/fast_float v8.3.0](https://github.com/fastfloat/fast_float/tree/b0ab987b3dfdde13fa1915f65ef2a5c068d9208c),
immutable commit `b0ab987b3dfdde13fa1915f65ef2a5c068d9208c`. Only the public headers
and MIT license are included, unmodified; `SHA256SUMS` records their hashes.
Gloin selects the upstream MIT license, included as `LICENSE-MIT` and installed
with release documentation. No build-time download or shared-library dependency
is added.

The numeric runtime uses direct-width, correctly rounded, allocation-free decimal
parsing. Gloin validates its own strict whole-input grammar and finite/range rules
before exposing results. Formatting uses platform `std::to_chars`; this dependency
does not control public APIs, allocation policy, or error statuses.
