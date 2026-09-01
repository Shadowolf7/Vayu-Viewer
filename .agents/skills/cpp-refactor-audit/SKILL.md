---
name: cpp-refactor-audit
description: Comprehensive audit procedures for C++ container refactorings, zero-copy pointer pipelines, and shadow declaration discovery in Vayu-Viewer.
---

# C++ Refactor & Pipeline Audit Skill

Use this skill when refactoring core data structures, migrating containers (e.g. `std::map`/`std::set` -> `fast_hmap`/`safe_hset`), or converting data queues from by-value to pointer/move semantics.

## Step-by-Step Audit Procedure

### 1. Shadow & Redundant Typedef Discovery
Run ripgrep queries specifically targeting declaration syntax:
```bash
# Find all typedef re-declarations of Symbol
git grep -E "typedef[[:space:]]+.*[[:space:]]+Symbol;"
git grep -E "using[[:space:]]+Symbol[[:space:]]*="
```
Verify each match. Delete any local forward declarations that collide with canonical headers.

### 2. Container Invariant Verification
Before swapping a sorted container (`std::set`, `std::map`) for an unordered/hash container (`safe_hset`, `fast_hmap`):
- Check all iteration loops for order dependencies (e.g., deterministic serialization, diff algorithms).
- Check for binary search or range methods (`lower_bound`, `upper_bound`, `equal_range`).
- Ensure `safe_hmap`/`safe_hset` is selected if element pointer stability across deletions is required.

### 3. Poison Copy Ctor Test for Move/Pointer Pipelines
When converting a data pipeline to zero-copy:
```cpp
// On the payload struct:
Payload(const Payload&) = delete;
Payload& operator=(const Payload&) = delete;
Payload(Payload&&) = default;
```
Compile the target. Address every compiler diagnostic where a copy was inadvertently attempted.

### 4. Ranged-For Copy Linting
Scan modified functions for accidental value copies in loops:
```bash
git grep -n -E "for[[:space:]]*\([[:space:]]*auto[[:space:]]+[a-zA-Z0-9_]+[[:space:]]*:" <modified_files>
```
Ensure all loops use `const auto&` or `auto&&`.
