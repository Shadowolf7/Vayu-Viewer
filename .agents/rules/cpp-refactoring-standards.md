---
description: Critical C++ refactoring standards, container safety, zero-copy verification, and declaration audits for Vayu-Viewer.
globs: ["**/*.cpp", "**/*.h", "**/*.inl"]
always_on: true
---

# Vayu-Viewer C++ Refactoring Standards

Whenever modifying data structures, container typedefs, queues, or algorithmic pipelines in Vayu-Viewer, strictly follow these protocols:

## 1. Container & Typedef Conversions (Declaration Audits)
* **Never assume a typedef is defined in only one place.** Legacy code in `indra/newview/` frequently re-declares local forward typedefs (e.g., `typedef std::set<LLUUID> uuid_list_t;`).
* Before modifying a typedef or struct name `T`:
  1. Search explicitly for all definitions across the entire repository:
     `git grep -E "typedef[[:space:]]+.*[[:space:]]+T;"`
     `git grep -E "using[[:space:]]+T[[:space:]]*="`
  2. Purge all local duplicate declarations so every consumer strictly derives the type from its single source of truth.

## 2. Zero-Copy & Pointer Pipeline Verification (Poison-Copy Protocol)
* When refactoring data pipelines from value-passing to pointers or move semantics (`std::move`, `std::unique_ptr`):
  1. **Poison the copy constructor:** Temporarily declare `Type(const Type&) = delete;` and `Type& operator=(const Type&) = delete;` on the payload struct.
  2. Run a dry compile to have Clang/GCC mathematically prove that zero hidden copies or unintended `.clone()` operations remain in producer or consumer loops.
  3. Verify consumption loops use references (`for (const auto& item : ...)` or `for (auto&& item : ...)`) rather than copying (`for (auto item : ...)`).
