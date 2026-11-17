---
name: refactor-phase
description: Execute one scoped SDL Viewer refactoring task or phase while preserving existing behavior, following the current refactoring progress, macOS gap analysis, platform boundaries, and phase acceptance criteria. Use when implementing or continuing an approved refactoring task.
---

# Purpose

Use this skill to perform an actual SDL Viewer refactoring change.

The goal is incremental refactoring with controlled scope, not a broad rewrite.

Perform only the current task or phase requested by the user.

Do not automatically continue to the next phase.

# Source of truth

Use information in the following priority order:

1. Current repository code and build configuration.
2. `docs/REFACTORING_PROGRESS.md`, especially:
   - Current Status
   - Current Blockers
   - Do Not Touch Yet
   - Next Session Start Point
3. `docs/MACOS_GAP_ANALYSIS.md` for macOS architecture, platform differences, risks, and phase definitions.
4. The latest platform/refactoring guide relevant to the current task.
5. `docs/CROSS_PLATFORM_PROJECT_STRUCTURE.md` for target module and platform boundaries.
6. Older roadmaps only as historical context.

Do not assume an older document is correct when it conflicts with the current repository or newer analysis.

If a conflict materially changes the current architecture or phase plan, stop implementation and report:

- document assumption
- repository reality
- affected architecture
- recommended resolution

Do not silently rewrite the architecture.

# Before changing code

Determine the exact current task.

Read only the documents and code necessary for that task.

Inspect:

- declaration
- construction site
- call sites
- ownership
- destruction path
- relevant thread access
- relevant CMake/source ownership

when they materially affect the requested change.

Do not modify code until the current dependency path is understood.

# Refactoring rules

Preserve existing externally observable behavior unless the current task explicitly changes it.

Prefer minimal changes.

One major concern per pass.

Do not perform unrelated cleanup.

Do not rename unrelated symbols.

Do not reformat unrelated files.

Do not add design patterns only for architectural purity.

Do not add generic interfaces or factories until there is a demonstrated need.

Do not convert raw pointers to smart pointers without first determining actual ownership semantics.

Do not introduce new global state or singleton dependencies.

# Platform boundary

Common application code should not acquire new dependencies on:

- `Windows.h`
- `HWND`
- D3D11
- DXGI
- DirectComposition
- WebView2 concrete types
- Objective-C/AppKit concrete types
- OpenGL resource handles

Platform-specific construction belongs at the platform composition boundary whenever practical.

Prefer:

```text
main_windows
    -> Windows concrete objects
    -> shared application runner
    -> Application/common interfaces

main_macos
    -> macOS concrete objects
    -> shared application runner
    -> Application/common interfaces
```

Avoid spreading `_WIN32` or `__APPLE__` through common subsystems.

CMake/platform-specific translation units should select platform implementations when that produces a cleaner boundary.

# macOS refactoring principles

Windows Refactored is the current structural baseline.

macOS Legacy is a behavior reference, not a class-structure template.

Reuse macOS Legacy behavior where validated, especially when relevant to:

- OpenGL behavior
- mpv OpenGL rendering
- VideoToolbox options
- Retina/high-DPI behavior
- fullscreen transitions
- native lifecycle
- display changes

Do not blindly copy:

- singleton/global manager structure
- worker-thread OpenGL ownership
- diagnostic-only timers
- hard-coded fullscreen dimensions
- legacy overlay-window structure
- local absolute dependency paths

# Size and display semantics

When working on display-related code, distinguish:

- logical window size
- drawable pixel size
- pixel density
- display/content scale

Do not use one size value for all four meanings.

UI layout, input, native view bounds, and video destination use logical coordinates unless the current contract explicitly says otherwise.

GPU framebuffer, viewport, and video surface allocation use physical pixel dimensions.

# Threading

Do not change thread ownership, affinity, lock ordering, or scheduling as an incidental cleanup.

When a task changes renderer scheduling or thread ownership:

- identify the old owner thread
- identify the new owner thread
- inspect callbacks crossing the boundary
- inspect shutdown ordering
- inspect synchronous waits
- check for self-deadlock
- check API thread-affinity requirements

Do not assume "currently works" means an API usage is supported.

# mpv

Keep common playback/control behavior platform-neutral where practical.

Prefer a narrow platform option hook rather than platform conditionals spread through media-control code.

Do not copy an mpv option from Legacy solely because it exists there.

Classify platform options based on observed need and verify return values where the current task touches option initialization.

Rendering callbacks should remain lightweight and must not perform unsafe rendering work directly unless explicitly permitted by the relevant API contract.

# Implementation

Implement the smallest coherent change that completes the current task.

If the current task can be represented as one logical commit, prefer that scope.

Do not begin the next task just because the current one finished early.

# Verification

Run the tests and builds that are relevant and available for the current task.

For Windows-affecting changes, preserve the current Windows baseline.

When appropriate verify:

- Debug build
- Release build
- runtime launch
- current phase acceptance criteria
- affected UI behavior
- playback
- resize
- fullscreen
- shutdown
- platform dependency leakage

Use these result states:

- PASS: actually verified
- FAIL: verification failed
- UNKNOWN: cannot establish from available evidence
- NOT TESTED: test was not run
- NOT APPLICABLE: irrelevant to this task

Never mark a test PASS based only on expectation.

# Scope check before completion

Before finishing, inspect the diff.

Remove or report unrelated changes.

Confirm that no later-phase work was accidentally introduced.

Check `Do Not Touch Yet` from `REFACTORING_PROGRESS.md`.

# Completion report

Report:

## Changed Files

Files changed and why.

## Before

Relevant previous architecture or behavior.

## After

Relevant new architecture or behavior.

## Behavior Change

Normally `NONE`.

Explicitly describe any intentional behavior change.

## Verification

Actual build/test/runtime results.

## Remaining Risks

Known risks relevant to this change.

## Deferred

Issues deliberately left to later phases.

## Next Step

State the next planned task, but do not execute it.

After code and verification are complete, use the `refactor-progress` skill when the progress document should be updated.
