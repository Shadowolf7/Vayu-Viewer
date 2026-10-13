# Vault Note Conventions

Rules for notes under the Obsidian vault's `Vayu Viewer/` directory. These exist because
the vault accumulated a large number of confidently-wrong claims, and because hand-editing
drift out of stale claims does not fix stale claims.

## 1. Every claim carries evidence, or is marked unverified

A claim about the codebase is one of:

- **Verified** — followed by `path/to/file.cpp:123` (or `path::symbol`), re-checked against
  the tree. Line numbers may drift; that is what check 3 of `scripts/verify_claims.py`
  exists to catch.
- **Unverified** — explicitly labelled `UNVERIFIED`. Say why (external URL, third-party
  source, needs a crash log, etc.).

Never write a claim with no evidence and no label. This is the single rule. Most of the
vault's rot came from assertions that read as verified but had never been checked —
"exhaustive grep, zero hits" on BC7 support that was in fact present in 24 places.

## 2. Name the removal when something is gone

When a commit deletes or supersedes something a note describes, record the commit hash,
its date, and whether it was a deliberate change or an accident. Both matter, and the
distinction is the useful part:

- `21121b338e` (Linden release/26.4 merge, 2026-09-14) was long blamed for dropping the
  notecard width-ruler. It did not: the code is absent from **both** its parents, so it
  cannot have been lost there. The demonstrable loss is `093ecb3f2b` (upstream merge,
  2026-10-11), whose first parent still carries the code and whose result does not.
- `be1773b12f` (2026-08-10) removed the specular-AA read-time mechanism in favour of
  Rye's G-buffer write-time filter — a deliberate replacement, with reasoning in the
  commit body.

"Absent from the tree" is not an explanation. A future reader needs to know whether to
restore it, reimplement it, or accept it.

## 3. Status claims require a check

Before writing that something ships, works, is fixed, or is enabled, confirm it in the
tree. The failure this prevents is the expensive one: a feature lands, is promoted into
`FEATURES.md`, then dies in a later upstream merge, and the document keeps advertising it
indefinitely. That happened to the width-ruler and to four specular settings.

`FEATURES.md` bullets carry an `` `anchor: path::symbol` `` that `scripts/verify_claims.py`
resolves. Anchors are what make this mechanically checkable; prose is not.

## 4. Never link a note that does not exist

Several notes linked `[[project_login_oom_bug_fixed]]`, `[[project_vcpkg_staleness]]`, and
`[[Speculative Clustered Lighting]]` — none of which existed. Either create the note or
use plain prose. Run `rg -o '\[\[[^]]+\]\]'` over the vault directory to catch this.

## 5. Date the note and say what changed

Header carries a last-verified date. When revising, say what was corrected and what was
verified, so a later reader can tell a fresh claim from an inherited one.

## 6. Distinguish "not investigated" from "not implemented"

"Not investigated" is an honest gap and worth recording. "Not implemented" is a claim
about the tree. Several notes conflated the two and were later read as claims about
implementation.

## 7. Verify before publishing

Run `python3 scripts/verify_claims.py` before trusting or publishing any documentation
claim. It resolves `FEATURES.md` anchors and commit hashes, and checks every file/line
citation in the vault. It is wired into pre-commit and runs automatically on every
commit. See the `verify-claims` skill for full usage.