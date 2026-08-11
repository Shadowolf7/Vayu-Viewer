# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Interaction Guidelines

Prefer clear, concise language geared towards a first year Computer Science major. This is a hobby project and a learning exercise, teach.

Communicate explicitly what you want to do, keep your dev in the loop and stay aligned.

## Build System

CMake with vcpkg for dependency management. The source root for CMake is `indra/` (not the repo root) — prerequisites, presets, configure/build/test commands, and troubleshooting are all in [doc/BUILD.md](doc/BUILD.md).

## Architecture

All source code lives under `indra/`, organized as a set of libraries the main viewer application (`newview`) links against — dependency flows downward. Full breakdown: [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md).
