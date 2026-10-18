# Monocypher 4.0.2

Unmodified files from the official repository, tag `4.0.2`, commit
`0d85f98c9d9b0227e42cf795cb527dff372b40a4`.

Source: [LoupVaillant/Monocypher, 4.0.2](https://github.com/LoupVaillant/Monocypher/tree/4.0.2).
License: **BSD-2-Clause OR CC0-1.0**; see `LICENCE.md` and individual file headers.

| Local file | Upstream path |
| --- | --- |
| monocypher.c | src/monocypher.c |
| monocypher.h | src/monocypher.h |
| monocypher-ed25519.c | src/optional/monocypher-ed25519.c |
| monocypher-ed25519.h | src/optional/monocypher-ed25519.h |
| LICENCE.md | LICENCE.md |

The upstream tag sources contain `Monocypher version __git__` in comments; no
version substitution or other source patch was applied. AegisVoice uses the
optional SHA-512/standard Ed25519 code as well as X25519 and XChaCha20-Poly1305.
Vendoring permits an offline build and deterministic dependency review; this
does not mean a newer release should be ignored when maintaining the project.

SHA-256 of the downloaded bytes (also in `SHA256SUMS`):

```text
a5781770269d2516e52ba4863f790c10a16da4089a1e81823aee19ff1e9026b0  LICENCE.md
97d581639dfa72be08a6d57deb7d79b736be001cb416819cab196d22559d242b  monocypher-ed25519.c
3a3035181f991a158d0e1c7567258f0bae8ba0f1f23c5512b4a1db1b3c9730ce  monocypher-ed25519.h
02174117935699d418443c75a558a287deb06ef8cf7c1adced61d9047d2f323d  monocypher.c
fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3  monocypher.h
```
