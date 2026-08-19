# `installed-tree/` — a stand-in for `vcpkg_installed/<triplet>/`

`license-audit --vcpkg-installed <dir>` reads `share/<port>/vcpkg.spdx.json` for the exact version
and looks for `share/<port>/NOTICE` (or `.txt` / `.md`). This tree supplies both, for two ports and
deliberately not a third:

| Port | In this tree | NOTICE | Renders as |
|---|---|---|---|
| `fixture-notice-present` | yes | yes | `yes` |
| `fixture-notice-absent` | yes | **no** | `**MISSING**` — the release blocker |
| `fixture-notice-unbuilt` | **no** | — | `not checked` |
| `fixture-lgpl-lib` | no, and requires none | — | `n/a` |

Those four rows are the point (**I-17**). `**MISSING**` used to mean both "a required notice was
looked for and is absent" and "this configuration does not build the component, so nothing was
looked for" — a blocker and a routine state wearing the same label, in the one document an auditor
reads. `manifest-notice-states.json` declares all four so a single emitted file contains every state.

Nothing here is a real component: the names are `fixture-*` and the upstreams are `example.invalid`,
so this data can never be mistaken for something we ship. Real components live in
`tools/license-audit/spdx-map.json`.
