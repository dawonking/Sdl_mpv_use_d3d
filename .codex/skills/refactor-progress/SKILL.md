---
name: refactor-progress
description: Update the SDL Viewer refactoring progress document after an implementation or audit so the next Codex session can accurately resume from the current phase, verified state, blockers, decisions, deferred work, and exact next task.
---

# Purpose

Update `docs/REFACTORING_PROGRESS.md`.

This document is the handoff state between refactoring sessions.

It is not the architecture guide and should not duplicate large design documents.

The next Codex session should be able to read this document and determine:

- current phase
- what was actually completed
- what was verified
- what remains unresolved
- what decisions were made
- what must not be changed yet
- the exact next task

# Evidence

Base the update on:

- actual repository state
- current git diff
- build/test results from the session
- current phase definition
- audit result, when available

Do not mark planned work as completed.

Do not mark unexecuted tests as PASS.

Do not convert inference into fact.

# Status values

Use:

- DONE
- IMPLEMENTED_NOT_VERIFIED
- DECIDED
- IN_PROGRESS
- BLOCKED
- DEFERRED
- REJECTED
- UNKNOWN

# Maintain current-state sections

Keep these sections near the beginning of the document and update them in place:

## Current Status

Include:

- Current Phase
- Phase Status
- Last Completed Task
- Last Verified State
- Current Blocker
- Next Task
- Next Phase
- Do Not Start Yet

Do not append outdated copies of Current Status.

# Append the daily/session log

For the current date add:

## Session Goal

What this session intended to do.

## Completed

Only work actually completed.

## Changed Files

Changed files and concise reason.

Do not paste full source code.

## Architecture Changes

Only architecture changes actually implemented.

Show short Before/After diagrams when useful.

## Decisions

For significant decisions record:

- Decision
- Reason
- Alternatives Considered
- Why Not

Do not invent rationale not established during the session.

## Verification

Record only tests actually run.

Use:

- PASS
- FAIL
- UNKNOWN
- NOT TESTED
- NOT APPLICABLE

## Issues Found

Classify as:

- BLOCKER
- FIX NOW
- DEFERRED
- EXISTING

Include related files/classes and target phase when known.

## Deferred Work

Record work deliberately postponed and the intended later phase.

## Regression Risk

Record only meaningful risks introduced or exposed by the current change.

## Document Conflicts

When the current repository contradicts an existing design document, record:

- Document
- Expected
- Actual
- Resolution

Do not silently rewrite historical decisions.

# Maintain unresolved state

Keep unresolved blockers in `Current Blockers`.

Remove resolved blockers from the current blocker section, but preserve their resolution in the session history.

Maintain cumulative deferred/technical-debt items until resolved or rejected.

# Next Session Start Point

Always finish with an explicit handoff:

## Read First

Only the documents and files directly relevant to the next task.

Do not require the next session to reread every project document.

## Current Goal

Current phase objective.

## Exact Next Task

One specific next action.

## Why

Why this is the next dependency-safe action.

## Expected Files

Files likely to change.

## Acceptance Criteria

Concrete completion criteria.

## Do Not Do

Work outside the next task's scope.

# Corrections

Do not silently rewrite historical session logs.

If an older entry is discovered to be wrong, add a correction in the current session log.

Current-status summaries may be updated normally.

# Completion

After updating the document, report:

- current phase
- current status
- next task
- blockers
- verification status

Do not start the next implementation task.
