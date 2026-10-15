#!/usr/bin/env python3
"""Reports where our tree has fallen behind upstream without a merge saying so.

A merge keeps our side of a path silently whenever we had already changed it:
git sees "ours differs from the base, theirs equals it" and takes ours, with no
conflict and nothing in the merge commit to review. Upstream's rewrite of that
path is then not a casualty of the merge, it is a non-event, and the next merge
brings in the infrastructure around it -- the shader manager, the uniforms, the
test framework -- with the file that is supposed to use it left behind. That is
how this fork ended up carrying a pre-GPU-water draw pool beside the GPU-water
shaders, and a TUT test file that includes a header the TUT-removal commit had
already deleted.

So: for every path where our content differs from upstream's, ask which of us
touched it last. Upstream having touched it more recently than we have means we
have not taken a version we could have had, and nobody chose that -- it is the
shape of the silent drop. Ours being newer means a commit of ours came after and
deliberately overtook it, which is a fork's ordinary work.

    scripts/upstream-drift.py                     # text report
    scripts/upstream-drift.py --json              # machine-readable
    scripts/upstream-drift.py --explain indra/newview/lldrawpoolwater.cpp

Exits 1 when anything is reported, so it can gate a merge.
"""

import argparse
import json
import os
import subprocess
import sys

UPSTREAM = "upstream/develop"
ACK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "upstream-drift.ack")


def git(*args, **kwargs):
    result = subprocess.run(("git",) + args, capture_output=True, text=True, check=False)
    if result.returncode != 0 and not kwargs.get("allow_fail"):
        sys.exit(f"git {' '.join(args)}: {result.stderr.strip()}")
    return result.stdout


def newest_touch(revision, paths, first_parent=False, budget=4000):
    """The most recent commit time per path, as git saw it.

    One pass over the revision's history with no pathspec: a pathspec makes git
    diff every commit it walks, and on a viewer-sized history that does not
    finish. git log is newest-first, so a path's first sighting in the walk is
    its newest touch. Merge commits report no paths of their own under
    --name-only, so a merge's changes are attributed to the commits that carry
    them, which is the same on both sides and leaves the comparison honest.
    Commits past the budget are not read: a path none of them touches is
    reported as undated rather than guessed at.

    first_parent is what makes "ours" mean ours. Upstream's commits are our
    ancestors -- that is what a merge is -- so walking HEAD reaches them, and
    the water pool's last touch would read as upstream's own and the path would
    never look behind. Our line is our first-parent chain.
    """
    args = ["log", "-n", str(budget), "--format=%ct", "--name-only"]
    if first_parent:
        args.append("--first-parent")
    newest = {}
    current = None
    for line in git(*(args + [revision]), allow_fail=True).splitlines():
        if not line:
            continue
        if line.isdigit():
            current = int(line)
        elif current and line in paths and line not in newest:
            newest[line] = current
            if len(newest) == len(paths):
                break
    return newest


def load_acknowledgements(path):
    """Paths whose divergence from upstream is deliberate, and why.

    One `path  reason` per line, `#` for a comment. The list is the fork's
    written-down divergences: a path on it is meant to differ, and a reason
    nobody can write down is a path that should be realigned instead.
    """
    ack = {}
    if not path or not os.path.exists(path):
        return ack
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(None, 1)
            ack[parts[0]] = parts[1] if len(parts) > 1 else ""
    return ack


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--upstream", default=UPSTREAM, help="the upstream ref (default %(default)s)")
    parser.add_argument("--ack", metavar="FILE", default=ACK,
                        help="paths whose divergence is deliberate (default %(default)s if it exists)")
    parser.add_argument("--json", action="store_true", help="report as JSON")
    parser.add_argument("--explain", metavar="PATH", help="show how one path came to differ")
    args = parser.parse_args()

    if args.explain:
        explain(args.explain, args.upstream)
        return 0

    if not git("rev-parse", "--verify", "-q", args.upstream, allow_fail=True).strip():
        sys.exit(f"{args.upstream} is not a ref here; fetch it first")

    base = git("merge-base", "HEAD", args.upstream, allow_fail=True).strip()
    if not base:
        sys.exit(f"no common ancestor with {args.upstream}: the histories are unrelated")

    # Only the paths both trees have can be "behind": one we alone have is the
    # fork's own work, and one they alone have we have never seen.
    upstream_paths = set(git("ls-tree", "-r", "--name-only", args.upstream).splitlines())
    our_paths = set(git("ls-tree", "-r", "--name-only", "HEAD").splitlines())
    differing = [p for p in git("diff", "--name-only", args.upstream, "HEAD").splitlines() if p]
    both = [p for p in differing if p in upstream_paths and p in our_paths]
    only_ours = sorted(our_paths - upstream_paths)
    only_theirs = sorted(upstream_paths - our_paths)

    ours = newest_touch("HEAD", both, first_parent=True)
    theirs = newest_touch(args.upstream, both)

    behind, ahead, unknown = [], [], []
    for path in both:
        mine, theirs_at = ours.get(path), theirs.get(path)
        if not mine or not theirs_at:
            unknown.append(path)
        elif theirs_at > mine:
            behind.append((path, theirs_at, mine))
        else:
            ahead.append((path, theirs_at, mine))

    acknowledged = load_acknowledgements(args.ack)
    unacked = [entry for entry in behind if entry[0] not in acknowledged]
    acked = [entry for entry in behind if entry[0] in acknowledged]

    report = {
        "upstream": args.upstream,
        "base": base,
        "differ": len(differing),
        "in_both": len(both),
        "behind": [{"path": p, "theirs": t, "ours": o, "ack": acknowledged.get(p, "")}
                   for p, t, o in sorted(behind, key=lambda r: -r[1])],
        "unacknowledged": [p for p, _, _ in unacked],
        "ahead": len(ahead),
        "undated": unknown,
        "only_ours": only_ours,
        "only_theirs": only_theirs,
    }

    if args.json:
        json.dump(report, sys.stdout, indent=1)
        sys.stdout.write("\n")
        return 1 if unacked else 0

    print(f"upstream {args.upstream}, merge base {base[:9]}")
    print(f"{len(differing)} path(s) differ: {len(both)} in both trees, "
          f"{len(only_ours)} only ours, {len(only_theirs)} only theirs")
    if unacked:
        print(f"\n{len(unacked)} path(s) upstream changed after we last touched ours:")
        for path, theirs_at, mine in unacked:
            print(f"  {path}")
            print(f"      upstream {date(theirs_at)}, ours {date(mine)}  (--explain {path})")
    if acked:
        print(f"\n{len(acked)} acknowledged as deliberate:")
        for path, theirs_at, mine in acked:
            print(f"  {path}\n      {acknowledged[path] or '(no reason given)'}")
    if unknown:
        print(f"\n{len(unknown)} path(s) with no history on one side")
    if not unacked:
        print("\nnothing unacknowledged behind.")
    return 1 if unacked else 0


def date(stamp):
    return subprocess.run(("date", "-d", f"@{stamp}", "+%Y-%m-%d"),
                          capture_output=True, text=True).stdout.strip()


def explain(path, upstream):
    """How one path came to differ: who touched it last, on which side."""
    for label, revision in (("ours", "HEAD"), ("upstream", upstream)):
        args = ["log", "--no-merges" if label == "ours" else "--format=%H %ad %s", "--date=short",
                "--format=%H %ad %s", "--date=short", "-3", revision, "--", path]
        print(f"{label}:")
        out = git(*args, allow_fail=True).strip()
        print("  " + ("\n  ".join(out.splitlines()) if out else "(no history)"))


if __name__ == "__main__":
    sys.exit(main() or 0)
