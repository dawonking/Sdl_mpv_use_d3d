---
name: refactor-audit
description: Audit an SDL Viewer refactoring change or completed phase against the repository, current progress state, gap analysis, platform boundaries, behavior compatibility, ownership, threading, and phase acceptance criteria. Use for verification only; do not modify code.
---

# Purpose

Use this skill after a refactoring task or phase to determine whether the change is safe and consistent with the current refactoring plan.

This is an audit workflow.

Do not modify source code, build files, or documentation unless the user explicitly asks for fixes after the audit.

# Sources

Use this priority:

1. Current repository and actual git diff.
2. `docs/REFACTORING_PROGRESS.md`.
3. `docs/MACOS_GAP_ANALYSIS.md`.
4. Relevant current refactoring/platform guide.
5. `docs/CROSS_PLATFORM_PROJECT_STRUCTURE.md`.
6. Older roadmaps only as historical context.

# Audit scope

Identify the task or phase being audited.

Do not audit unrelated future architecture unless it creates a current regression risk.

# Check 1 — Scope

Classify changes as:

- REQUIRED_AND_IMPLEMENTED
- REQUIRED_BUT_MISSING
- IMPLEMENTED_TOO_EARLY
- UNRELATED_CHANGE

Check `Do Not Touch Yet` in the progress document.

# Check 2 — Architecture

Verify:

- common/platform boundary
- dependency direction
- concrete platform leakage
- unnecessary abstraction
- circular dependency
- duplicated platform wiring
- CMake source ownership

Do not require abstraction merely for stylistic purity.

# Check 3 — Behavior

Compare relevant current behavior with the pre-refactoring baseline or Legacy reference when applicable.

Classify differences:

- SAME
- INTENTIONAL_DIFFERENCE
- POTENTIAL_REGRESSION
- UNKNOWN

Focus on observable behavior and resource/thread semantics rather than textual similarity.

# Check 4 — Ownership and lifetime

For changed objects determine:

- creator
- owner
- destruction responsibility
- lifetime dependency
- non-owning references
- callback lifetime
- shutdown order

Report ambiguous ownership explicitly.

# Check 5 — Threading

When relevant check:

- creation thread
- execution thread
- destruction thread
- shared state
- lock order
- synchronous waits
- callback crossings
- shutdown races
- self-deadlock potential
- API thread-affinity requirements

# Check 6 — Platform-specific concerns

For macOS-related work check as applicable:

- logical vs pixel size
- pixel density
- display scale
- OpenGL context ownership
- Retina resource sizes
- fullscreen completion
- monitor/display changes
- mpv OpenGL context/FBO lifecycle
- VideoToolbox option behavior
- native view lifetime

For Windows-affecting changes check current behavior has not regressed.

# Check 7 — Acceptance criteria

Read the current phase/task acceptance criteria.

For every criterion report:

- PASS
- FAIL
- UNKNOWN
- NOT TESTED
- NOT APPLICABLE

Do not infer PASS from code appearance.

# Check 8 — Regression risk

Classify meaningful risks:

- CRITICAL
- HIGH
- MEDIUM
- LOW

Do not treat naming or formatting as runtime regression.

# Final report

## Phase Status

`PASS`, `PASS WITH ISSUES`, or `FAIL`.

## Blocking Issues

Issues that must be fixed before continuing.

## Non-blocking Issues

Issues that should be addressed but do not block the next task.

## Deferred Issues

Correctly deferred work.

## Architecture Compliance

`PASS`, `FAIL`, or `UNKNOWN`.

## Behavior Compatibility

`PASS`, `FAIL`, `PARTIALLY VERIFIED`, or `UNKNOWN`.

## Acceptance Criteria

Per-criterion result.

## Next Phase Ready

`YES` or `NO`, with reason.

Do not change code during this workflow.
