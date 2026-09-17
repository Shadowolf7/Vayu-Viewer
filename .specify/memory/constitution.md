# Vayu-Viewer Constitution

## Core Principles

### I. Strict Launch & Build Prohibitions (NON-NEGOTIABLE)
* **NEVER initiate ANY build or compilation command without explicit, turn-by-turn user instruction.**
  - Strictly forbidden: autonomous invocation of `ninja`, `cmake --build`, `make`, test runners, or compilation sub-commands.
  - General verification instructions ("make sure code builds") must NEVER be interpreted as permission to build. Always request user authorization first.
  - Full binary links (`vayu-bin`) and package targets consume massive CPU/RAM and can freeze the developer's workstation.
* **NEVER launch interactive GUI applications (`vayu`, `vayu-bin`) without explicit user instruction.**
* **Pre-Build Process Guard**: Even with explicit user authorization, the agent MUST verify that no viewer process (`vayu`, `vayu-bin`) or competing heavy task is active before starting any compilation. Building while a viewer is running corrupts build tree assets and causes immediate session crashes.
* If a benchmark, capture, or test fails, stop immediately, report the exact error, and never autonomously retry or relaunch.

### II. Minimal Diff & Anti-Scope Creep Principle (No "Weird Additions")
* **Strict Plan Adherence:** Changes must be confined strictly to the files and functions explicitly authorized in `plan.md` and `tasks.md`.
* **No Unsolicited Refactoring:** Never clean up adjacent code, add speculative abstractions, or introduce helper utilities that were not explicitly specified in `spec.md`.
* **No Unilateral Reverts or Knee-Jerk Deletions:** Never discard in-progress or experimental code (`git restore`, `git checkout`, `git reset`) without explicit clearance. Diagnose and solve problems iteratively rather than throwing away work.

### III. Task Completeness & Convergence (No "Skipped Tasks")
* Every work item in `tasks.md` is an atomic, verifiable contract.
* An agent may not declare a feature or bug fix completed until all checkboxes in `tasks.md` are accounted for and `/speckit-converge` confirms convergence against `spec.md`.
* If context or session limits are reached, the agent must record the exact progress state in `tasks.md` before stopping.

### IV. C++ Engineering & Zero-Copy Standards
* **Data-Oriented & High-Performance:** Prioritize memory locality, cache friendliness, and low latency across all hardware tiers.
* **No Hidden Allocations:** Never attach dynamic memory allocations or fat SIMD structures to high-frequency scene graph objects (`LLCamera`, `LLDrawable`, `LLSpatialGroup`). Use transient thread-local scratch buffers and SoA streams.
* **Zero-Copy Verification (Poison-Copy Protocol):** When refactoring data pipelines from value-passing to pointers or move semantics (`std::move`, `std::unique_ptr`), temporarily poison copy constructors (`= delete`) to ensure zero hidden copies in loops.
* **Declaration Audits:** Before modifying a typedef or struct name in `indra/`, verify all definitions across the entire repository with `rg` to eliminate local duplicate forward declarations.

### V. Clear Communication & Native Tooling
* **Language & Tone:** Use clear, concise explanations geared toward a first-year Computer Science major. Communicate intent explicitly before taking action.
* **Search Standard:** Always use native ripgrep (`rg` / `grep_search`) for code and text searches. Never invoke legacy GNU `grep` in shell commands.

## Governance

* This Constitution supersedes all ad-hoc agent practices and speculative implementation choices.
* All specifications (`spec.md`), technical blueprints (`plan.md`), and checklists (`tasks.md`) must verify compliance with this Constitution.
* Amendments to this Constitution require explicit developer review and documented rationale.

**Version**: 1.0.0 | **Ratified**: 2026-09-10 | **Last Amended**: 2026-09-10
