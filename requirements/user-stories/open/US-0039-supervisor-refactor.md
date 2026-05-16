# US-0039: Extract StateMachine and Orchestrator from SupervisorV2

> **Status:** To Do | **Priority:** Low | **Created:** 2026-05-17

## User Story
As a **developer**, I want **StateMachine and Orchestrator extracted into separate classes**, so that **SupervisorV2 is no longer a god class with 22 members and 25 methods**.

## Acceptance Criteria
- [ ] `StateMachine` class exists with pure transition logic (getNextState, setObservedState, setTargetState, stepTowardTarget, determineRecoveryTarget, consumeErrorEvent, consumeStateRequest) — no FreeRTOS dependencies
- [ ] `Orchestrator` class exists owning the worker task, event group, order/response mailboxes, component mailboxes, and orchestration lifecycle
- [ ] `SupervisorV2` composes `StateMachine` and `Orchestrator`, exposing the public API surface (postStateRequest, postErrorEvent, completeTransition, run, etc.)
- [ ] `state_machine.h` is a real header (not a 3-line forward)
- [ ] All 139+ tests pass
- [ ] `StateMachine` is fully unit-testable without FreeRTOS

## Notes
The file split is currently cosmetic — `state_machine.cpp` and `orchestrator.cpp` contain `SupervisorV2::method()` implementations. A real class split would:
- Make `StateMachine` testable without FreeRTOS stubs
- Clarify ownership boundaries (especially mailbox I/O vs. orchestration logic)
- Reduce compile-time coupling

## Related
- Audit: `docs/superpowers/specs/2026-05-16-supervisor-v2-audit.md` D1, D2
