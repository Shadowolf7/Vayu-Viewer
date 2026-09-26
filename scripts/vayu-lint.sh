#!/usr/bin/env python3
"""
vayu-lint.sh - Clean static analysis and linting runner for Vayu-Viewer.

Runs clang-tidy on specified files (or all git-modified C++ files if none specified)
without colliding with CMake precompiled headers (PCH).

Usage:
    scripts/vayu-lint.sh [options] [file1.cpp file2.h ...]
    scripts/vayu-lint.sh --help

Options:
    --cppcheck               Run cppcheck analysis in addition to clang-tidy
    --ast-grep               Run ast-grep architectural rules check
    --semgrep                Run semgrep architectural rules check (.semgrep.yml)
    --community              Include official Semgrep community rules (p/cpp) with --semgrep
    --skip-clang-tidy        Skip clang-tidy analysis
    --skip-intrinsic-check   Skip SIMD/vector intrinsic check (AlIntrinsicCheck.cmake)
"""

import sys
import os
import json
import tempfile
import subprocess
import shutil

def find_compile_commands():
    candidates = [
        os.path.abspath("compile_commands.json"),
        os.path.abspath("build-Linux-ninja-perf/compile_commands.json"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return None

def get_git_modified_files():
    try:
        res = subprocess.run(
            ["git", "status", "--porcelain"],
            capture_output=True,
            text=True,
            check=True
        )
        files = []
        for line in res.stdout.splitlines():
            if len(line) < 4:
                continue
            status = line[:2]
            path = line[3:].strip()
            if " -> " in path:
                path = path.split(" -> ")[1].strip()
            if path.endswith((".cpp", ".c", ".h", ".hpp", ".inl")):
                if os.path.exists(path):
                    files.append(os.path.abspath(path))
        return files
    except Exception:
        return []

def strip_pch_flags(cmd_str):
    parts = cmd_str.split()
    cleaned = []
    skip_next = False
    for p in parts:
        if skip_next:
            skip_next = False
            continue
        if p in ("-include-pch", "-Winvalid-pch"):
            skip_next = True
            continue
        if p.endswith(".pch"):
            continue
        cleaned.append(p)
    return " ".join(cleaned)

def run_intrinsic_check():
    indra_dir = os.path.abspath("indra")
    script_path = os.path.join(indra_dir, "cmake", "AlIntrinsicCheck.cmake")
    if not os.path.exists(script_path):
        return 0
    cmake_bin = shutil.which("cmake")
    if not cmake_bin:
        print("Warning: cmake not found; skipping SIMD intrinsic check.", file=sys.stderr)
        return 0

    print("Checking for raw SIMD intrinsics and GLM usage (AlIntrinsicCheck)...")
    res = subprocess.run(
        [cmake_bin, f"-DSOURCE_DIR={indra_dir}", "-P", script_path],
        capture_output=True,
        text=True
    )
    if res.returncode != 0:
        print("\n--- SIMD Intrinsic Check Failed ---", file=sys.stderr)
        if res.stderr.strip():
            print(res.stderr.strip(), file=sys.stderr)
        if res.stdout.strip():
            print(res.stdout.strip(), file=sys.stderr)
        return res.returncode

    print("SIMD intrinsic check passed cleanly.")
    return 0

def run_ast_grep(target_files):
    ast_grep_bin = shutil.which("ast-grep")
    if not ast_grep_bin:
        print("Warning: ast-grep not installed or not in PATH.", file=sys.stderr)
        return 0
    if not os.path.exists("sgconfig.yml"):
        print("Warning: sgconfig.yml not found.", file=sys.stderr)
        return 0

    cpp_targets = [f for f in target_files if f.endswith((".cpp", ".c", ".h", ".hpp", ".inl"))]
    if not cpp_targets:
        return 0

    print(f"\n--- Running ast-grep architectural rules on {len(cpp_targets)} file(s) ---")
    res = subprocess.run([ast_grep_bin, "scan"] + cpp_targets)
    if res.returncode == 0:
        print("ast-grep: all files passed architectural rules cleanly.")
    return res.returncode

def run_semgrep(target_files, use_community=False):
    semgrep_bin = shutil.which("semgrep")
    if not semgrep_bin:
        print("Warning: semgrep not installed or not in PATH.", file=sys.stderr)
        return 0

    cpp_targets = [f for f in target_files if f.endswith((".cpp", ".c", ".h", ".hpp", ".inl"))]
    if not cpp_targets:
        return 0

    configs = []
    if os.path.exists(".semgrep.yml"):
        configs.extend(["--config", ".semgrep.yml"])
    if use_community:
        configs.extend(["--config", "p/cpp"])

    if not configs:
        print("Warning: No semgrep configuration found.", file=sys.stderr)
        return 0

    mode_str = "with community p/cpp rules" if use_community else "with .semgrep.yml"
    print(f"\n--- Running semgrep on {len(cpp_targets)} file(s) ({mode_str}) ---")
    res = subprocess.run([semgrep_bin, "scan"] + configs + cpp_targets)
    if res.returncode == 0:
        print("semgrep: all files passed rules cleanly.")
    return res.returncode

def main():
    if "--help" in sys.argv or "-h" in sys.argv:
        print(__doc__.strip())
        sys.exit(0)

    do_cppcheck = False
    do_ast_grep = False
    do_semgrep = False
    use_community = False
    skip_clang_tidy = False
    skip_intrinsic_check = False
    raw_args = sys.argv[1:]
    if "--cppcheck" in raw_args:
        do_cppcheck = True
        raw_args.remove("--cppcheck")
    if "--ast-grep" in raw_args:
        do_ast_grep = True
        raw_args.remove("--ast-grep")
    if "--semgrep" in raw_args:
        do_semgrep = True
        raw_args.remove("--semgrep")
    if "--community" in raw_args:
        do_semgrep = True
        use_community = True
        raw_args.remove("--community")
    if "--skip-clang-tidy" in raw_args:
        skip_clang_tidy = True
        raw_args.remove("--skip-clang-tidy")
    if "--skip-intrinsic-check" in raw_args:
        skip_intrinsic_check = True
        raw_args.remove("--skip-intrinsic-check")

    if not skip_intrinsic_check:
        intrinsic_rc = run_intrinsic_check()
        if intrinsic_rc != 0:
            print("Linting aborted due to SIMD intrinsic violations.", file=sys.stderr)
            sys.exit(intrinsic_rc)

    target_files = []
    if raw_args:
        for arg in raw_args:
            p = os.path.abspath(arg)
            if os.path.exists(p):
                target_files.append(p)
            else:
                print(f"Warning: File not found: {arg}", file=sys.stderr)
    else:
        target_files = get_git_modified_files()

    if not target_files:
        print("No target files found to lint.")
        sys.exit(0)

    if do_ast_grep:
        ast_rc = run_ast_grep(target_files)
        if ast_rc != 0:
            print("Linting aborted due to ast-grep architectural rule violations.", file=sys.stderr)
            sys.exit(ast_rc)

    if do_semgrep:
        sg_rc = run_semgrep(target_files, use_community=use_community)
        if sg_rc != 0:
            print("Linting aborted due to semgrep rule violations.", file=sys.stderr)
            sys.exit(sg_rc)

    if skip_clang_tidy:
        print("\nAll requested pre-compilation checks passed cleanly.")
        sys.exit(0)

    clang_tidy_bin = shutil.which("clang-tidy")
    if not clang_tidy_bin:
        print("Error: clang-tidy is not installed or not in PATH.", file=sys.stderr)
        sys.exit(1)

    cc_path = find_compile_commands()
    if not cc_path:
        print("Error: compile_commands.json not found.", file=sys.stderr)
        sys.exit(1)

    with open(cc_path, "r") as f:
        all_commands = json.load(f)

    # Command lookup by normalized file path
    cmd_map = {}
    for entry in all_commands:
        f_norm = os.path.abspath(entry.get("file", ""))
        # Prefer Release target if multiple targets exist
        if f_norm not in cmd_map or "Release" in entry.get("command", ""):
            cmd_map[f_norm] = entry

    print(f"Running clang-tidy on {len(target_files)} file(s)...")

    # Filter compilation commands and strip PCH
    filtered_cmds = []
    for tf in target_files:
        if tf in cmd_map:
            entry = dict(cmd_map[tf])
            entry["command"] = strip_pch_flags(entry["command"])
            filtered_cmds.append(entry)
        else:
            print(f"Warning: No compile command entry for {tf}", file=sys.stderr)

    if not filtered_cmds:
        print("No matching compilation commands found.", file=sys.stderr)
        sys.exit(1)

    with tempfile.TemporaryDirectory() as td:
        temp_cc = os.path.join(td, "compile_commands.json")
        with open(temp_cc, "w") as f:
            json.dump(filtered_cmds, f)

        overall_rc = 0
        from concurrent.futures import ThreadPoolExecutor

        def lint_one(entry):
            f_path = entry["file"]
            r_path = os.path.relpath(f_path, os.getcwd())
            res = subprocess.run(
                [clang_tidy_bin, f_path, "-p", td],
                capture_output=True,
                text=True
            )
            out_lines = []
            if res.stdout.strip():
                out_lines.append(f"\n--- Linting {r_path} ---")
                out_lines.append(res.stdout.strip())
            if res.stderr.strip():
                err_lines = [l for l in res.stderr.splitlines() if "warnings generated" not in l]
                if err_lines:
                    out_lines.append(f"\n--- Stderr ({r_path}) ---")
                    out_lines.append("\n".join(err_lines))
            return res.returncode, "\n".join(out_lines)

        workers = min(os.cpu_count() or 4, 8)
        print(f"Linting {len(filtered_cmds)} files in parallel ({workers} workers)...")
        with ThreadPoolExecutor(max_workers=workers) as executor:
            for rc, out in executor.map(lint_one, filtered_cmds):
                if out.strip():
                    print(out)
                if rc != 0:
                    overall_rc = rc

        if do_cppcheck and shutil.which("cppcheck"):
            print("\n--- Running cppcheck analysis ---")
            cppcheck_cmd = [
                "cppcheck",
                "--enable=warning,performance,portability",
                "--inline-suppr",
                "--suppress=syntaxError",
            ] + [e["file"] for e in filtered_cmds]
            cp_res = subprocess.run(cppcheck_cmd, capture_output=True, text=True)
            if cp_res.stderr.strip():
                print(cp_res.stderr)
            if cp_res.stdout.strip():
                print(cp_res.stdout)

    if overall_rc == 0:
        print("\nAll files passed static analysis cleanly!")
    sys.exit(overall_rc)

if __name__ == "__main__":
    main()
