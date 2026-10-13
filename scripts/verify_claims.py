#!/usr/bin/env python3
"""Verify that documented claims still match the tree.

Three checks:

  1. FEATURES.md anchors   -- ``anchor: indra/foo.cpp::someSymbol`` must resolve to a
                              file that exists and still contains that symbol. This is
                              the check that catches a feature dying in an upstream merge
                              while the document keeps advertising it.
  2. FEATURES.md commits   -- every ``commit/<hash>`` link must resolve to a real commit.
  3. Vault note references -- ``indra/foo.cpp:123`` and bare ``foo.cpp`` references in the
                              Obsidian vault must resolve to something in the tree.

Severity model: checks 1 and 2 are FAILURES (FEATURES.md is structured and machine-owned).
Check 3 is a WARNING by default, because notes are prose and legitimately reference files
that do not exist yet ("create indra/newview/skins/adwaita/"). Use --strict to fail on
those too.

Exit codes: 0 clean (warnings allowed), 1 failure or --strict warning.
"""

import argparse
import os
import re
import subprocess
import sys

DEFAULT_VAULT = "/home/roger/Nextcloud/Documents/Obsidian/Roger's Vault/Vayu Viewer"

# Directories never worth scanning as source references.
SKIP_DIRS = {
    ".git", "build-Linux-ninja-perf", "vcpkg", ".venv", "captures",
    "node_modules", ".cache", ".build-tmp", ".claude", "fmodstudioapi20314linux",
}

SOURCE_EXTS = (".cpp", ".hpp", ".cxx", ".cmake", ".h", ".c", ".inl", ".cuh", ".mm", ".xml", ".py", ".sh", ".glsl", ".md")

# Longest extension first: alternation is ordered, so ".c" would otherwise swallow
# ".cmake" (00-Common.cmake -> 00-Common.c) and ".h" would swallow ".hpp".
_EXT_ALT = "|".join(e[1:] for e in sorted(SOURCE_EXTS, key=len, reverse=True))

ANCHOR_RE = re.compile(r"anchor:\s*([\w./+-]+\.\w+)(?:::([\w:]+))?")
COMMIT_RE = re.compile(r"commit/([0-9a-f]{7,40})")
PATHLINE_RE = re.compile(r"\b((?:[\w.-]+/)*[\w.-]+\.(?:" + _EXT_ALT + r"))(?::(\d+)(?:-(\d+))?)?")
BARE_RE = re.compile(r"\b([\w.-]+\.(?:" + _EXT_ALT + r"))(?::(\d+)(?:-(\d+))?)?")

# Notes write indra-relative paths ("llcommon/llcoros.h"), so try these prefixes too.
REL_PREFIXES = ("", "indra/", "scripts/", "doc/")

# Token shapes that are never repo files even though they look like one.
_URLISH = re.compile(r"^[a-z0-9-]+\.[a-z0-9-]+\.[a-z]{2,}$")
# Strip URLs before scanning so "github.com/.../FEATURES.md" and
# "asset-cdn.glb.agni.lindenlab.c" are not mistaken for repo paths.
_URL_RE = re.compile(r"(?:https?://|www\.)\S+|github\.com/\S+")
# Notes abbreviate shader pairs as "materialF/V.glsl"; the captured token is just
# "V.glsl", which is never a real path.
_SHADER_PAIR = re.compile(r"^(?:[\w]+/)?[A-Z]\.(?:glsl|cpp|h)$")


class Failures:
    def __init__(self):
        self.items = []

    def add(self, where, message):
        self.items.append((where, message))


def git(args, cwd):
    try:
        return subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True, timeout=30).stdout
    except Exception:
        return ""


def commit_exists(repo, sha):
    return subprocess.run(
        ["git", "cat-file", "-e", sha + "^{commit}"],
        cwd=repo, capture_output=True,
    ).returncode == 0


def build_source_index(repo):
    """basename -> list of repo-relative paths."""
    index = {}
    for root, dirs, files in os.walk(repo):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        rel_root = os.path.relpath(root, repo)
        if rel_root.startswith(".") and rel_root != ".":
            dirs[:] = []
            continue
        for name in files:
            if name.endswith(SOURCE_EXTS):
                index.setdefault(name, []).append(os.path.normpath(os.path.join(rel_root, name)))
    return index


def check_features(repo, failures, warnings):
    path = os.path.join(repo, "FEATURES.md")
    if not os.path.exists(path):
        warnings.append(("FEATURES.md", "not found"))
        return 0, 0

    anchors = 0
    unanchored = 0
    with open(path, encoding="utf-8") as fh:
        for lineno, line in enumerate(fh, 1):
            if line.startswith("- **"):
                found = ANCHOR_RE.search(line)
                if found:
                    anchors += 1
                else:
                    unanchored += 1

            for anchor_path, symbol in ANCHOR_RE.findall(line):
                full = os.path.join(repo, anchor_path)
                if not os.path.exists(full):
                    failures.add(f"FEATURES.md:{lineno}", f"anchor file missing: {anchor_path}")
                elif symbol:
                    with open(full, encoding="utf-8", errors="replace") as fh2:
                        if symbol not in fh2.read():
                            failures.add(
                                f"FEATURES.md:{lineno}",
                                f"anchor symbol missing: {anchor_path}::{symbol}",
                            )

            for sha in COMMIT_RE.findall(line):
                if not commit_exists(repo, sha):
                    failures.add(f"FEATURES.md:{lineno}", f"commit not found: {sha}")
    return anchors, unanchored


WIKILINK_RE = re.compile(r"\[\[([^\]]+)\]\]")
# ``[[x]]`` inside backticks is quoted prose, not a link.
CODE_SPAN_RE = re.compile(r"`[^`]*`")


def check_vault_links(vault, failures, warnings):
    if not os.path.isdir(vault):
        return
    for name in sorted(os.listdir(vault)):
        if not name.endswith(".md"):
            continue
        with open(os.path.join(vault, name), encoding="utf-8", errors="replace") as fh:
            for lineno, line in enumerate(fh, 1):
                stripped = CODE_SPAN_RE.sub("", line)
                for target in WIKILINK_RE.findall(stripped):
                    target = target.split("|")[0].split("#")[0].strip()
                    if target and not os.path.exists(os.path.join(vault, target + ".md")):
                        warnings.add(f"{name}:{lineno}", f"broken wikilink: [[{target}]]")


def resolve(repo, index, ref):
    """Return ('ok', abspath) | ('missing', None) | ('skip', None)."""
    if _URLISH.match(ref) or _SHADER_PAIR.match(ref):
        return "skip", None
    if os.path.exists(os.path.join(repo, ref)):
        return "ok", os.path.join(repo, ref)
    for prefix in REL_PREFIXES[1:]:
        cand = os.path.join(repo, prefix + ref)
        if os.path.exists(cand):
            return "ok", cand
    if "/" not in ref:
        hits = index.get(ref)
        if hits:
            return "ok", os.path.join(repo, hits[0])
    return "missing", None


def scan_vault(vault, repo, index, warnings):
    if not os.path.isdir(vault):
        return 0
    checked = 0
    seen = set()
    for name in sorted(os.listdir(vault)):
        if not name.endswith(".md"):
            continue
        with open(os.path.join(vault, name), encoding="utf-8", errors="replace") as fh:
            for lineno, line in enumerate(fh, 1):
                if line.lstrip().startswith("```") or line.startswith(("    ", "\t")):
                    continue
                for ref, start, end in PATHLINE_RE.findall(_URL_RE.sub(" ", line)):
                    key = (name, lineno, ref)
                    if key in seen:
                        continue
                    seen.add(key)
                    status, full = resolve(repo, index, ref)
                    if status == "skip":
                        continue
                    checked += 1
                    if status == "missing":
                        warnings.add(f"{name}:{lineno}", f"missing: {ref}")
                        continue
                    if start:
                        try:
                            with open(full, encoding="utf-8", errors="replace") as fh2:
                                total = sum(1 for _ in fh2)
                        except OSError:
                            continue
                        if int(end or start) > total:
                            warnings.add(
                                f"{name}:{lineno}",
                                f"line beyond EOF: {ref}:{start}-{end or start} (file has {total} lines)",
                            )
    return checked


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.getcwd())
    ap.add_argument("--vault", default=os.environ.get("VAYU_VAULT", DEFAULT_VAULT))
    ap.add_argument("--skip-vault", action="store_true", help="features only (fast, for pre-commit)")
    ap.add_argument("--strict", action="store_true", help="treat vault warnings as failures")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    repo = args.repo
    failures, warnings = Failures(), Failures()
    anchors, unanchored = check_features(repo, failures, warnings)

    checked = 0
    if not args.skip_vault:
        index = build_source_index(repo)
        check_vault_links(args.vault, failures, warnings)
        checked = scan_vault(args.vault, repo, index, warnings)

    if not args.quiet:
        print(f"claims: {anchors} anchored, {unanchored} unanchored features, "
              f"{checked} vault references checked")

    for where, msg in failures.items:
        print(f"FAIL  {where}: {msg}")
    for where, msg in warnings.items:
        print(f"WARN  {where}: {msg}")

    if failures.items or (args.strict and warnings.items):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())