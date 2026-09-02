---
description: Core project mission, accessibility principles, and rigorous industry-standard engineering guidelines for Vayu-Viewer.
globs: ["**/*"]
always_on: true
---

# Vayu-Viewer Mission & Engineering Principles

## 1. Project Purpose & Social Mission
* **Public Service & Accessibility:** Vayu-Viewer is an open public-service project dedicated to delivering a fast, accessible, and rock-solid Second Life viewer. For many users—especially those in vulnerable circumstances or with disabilities—Second Life provides a vital social lifeline, independence, and safe community space.
* **Zero Compromise on Stability & Predictability:** Stutters, memory bloat, hitching, and regressions directly harm user accessibility. Every change must prioritize smoothness, low latency, and rock-solid reliability across diverse hardware tiers.

## 2. Engineering Standards & Architecture
* **Academic & Industry Standards:** Always follow established industry best practices (Data-Oriented Design, SPMD/ISPC for batch SIMD, memory locality, clean interfaces). Never implement speculative hacks, raw unportable intrinsics scattered in business logic, or fragile ad-hoc shortcuts.
* **Proper Dependencies & Tooling:** Use modern, battle-tested libraries and packages (e.g., ISPC, libyuv, simdjson, meshoptimizer, Tracy) whenever they provide clean, maintainable, and verified leverage.
* **Zero Hidden Allocations & Object Bloat:** Never attach fat SIMD structures or dynamic memory allocations to high-frequency scene graph objects (e.g., `LLCamera`, `LLDrawable`, `LLSpatialGroup`). Use transient thread-local scratch buffers and SoA streams at the algorithm level.
* **Telemetry & Profiling Verification:** Every performance optimization must be validated against real Tracy telemetry benchmarks with clean cold/warm baselines and frame-time distribution checks.
