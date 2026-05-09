# US-0032b: Deduplicate handleEvent by Normalizing Legacy Events to STATE_REQUESTED

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Remove 50+ lines of duplicated transition logic in `handleEvent()` by converting legacy events (`ENTER_SLEEP`, `STOP_REQUESTED`, `PLAY_REQUESTED`, `COMPONENT_SETUP_FAILED`) into `STATE_REQUESTED` via thin aliases at the top of the function.

**Architecture:** Single-function refactor. Four if-statements at the entry of `handleEvent()` set `mailbox_.targetState` and reassign `event = SystemEvent::STATE_REQUESTED`, causing all paths to converge on the existing `STATE_REQUESTED` switch. The legacy if-chain is then deleted entirely. Zero behavior change — the same `requestStateTransition` lambda and the same switch cases drive all transitions.

**Tech Stack:** C++17, PlatformIO, Unity test framework

---

**Files:**
- Modify: `src/state_machine/supervisor.cpp:476-562`

### Step 1: Read the current handleEvent

Current handleEvent has this structure (lines ~476-562):

```
STATE_REQUESTED switch     → LIVE/READY/SLEEP/ERROR/FATAL cases
ENTER_SLEEP if             → sets SLEEP, requestTransition(SLEEP)   ← DUPLICATE of STATE_REQUESTED(SLEEP)
STOP_REQUESTED if          → sets SLEEP, requestTransition(READY)   ← DUPLICATE of STATE_REQUESTED(READY)
PLAY_REQUESTED if          → complex LIVE logic                     ← DUPLICATE of STATE_REQUESTED(LIVE)
COMPONENT_SETUP_FAILED if  → transitionTo(ERROR/FATAL)              ← DUPLICATE of STATE_REQUESTED(ERROR)
```

### Step 2: Insert normalization block after the requestStateTransition lambda

After line 474 (closing `};` of `requestStateTransition`), insert:

```cpp
    // Normalize legacy events to STATE_REQUESTED so all paths converge on a single switch.
    if (event == SystemEvent::ENTER_SLEEP) {
        mailbox_.targetState = SystemState::SLEEP;
        event = SystemEvent::STATE_REQUESTED;
    }
    if (event == SystemEvent::STOP_REQUESTED) {
        mailbox_.targetState = SystemState::READY;
        event = SystemEvent::STATE_REQUESTED;
    }
    if (event == SystemEvent::PLAY_REQUESTED) {
        mailbox_.targetState = SystemState::LIVE;
        event = SystemEvent::STATE_REQUESTED;
    }
    if (event == SystemEvent::COMPONENT_SETUP_FAILED) {
        mailbox_.targetState = observedState_ == SystemState::ERROR ? SystemState::FATAL : SystemState::ERROR;
        event = SystemEvent::STATE_REQUESTED;
    }
```

After conversion, `COMPONENT_SETUP_FAILED` with observedState_ == ERROR maps to `STATE_REQUESTED(FATAL)` and `transitionTo` handles it in the FATAL case (which also sets `targetMode_ = FATAL`). This preserves the existing behavior: `COMPONENT_SETUP_FAILED` in ERROR state → FATAL, otherwise → ERROR.

### Step 3: Delete the legacy if-chain

Delete lines 522-562:

```cpp
    // User intents are state-independent and map directly to target states.
    if (event == SystemEvent::ENTER_SLEEP) {
        ...
    }
    ...
    if (event == SystemEvent::COMPONENT_SETUP_FAILED) {
        ...
    }
```

The state-dependent switch (RECOVER handling) stays untouched.

### Step 4: Build and run tests

Run: `~/.platformio/penv/bin/platformio run -e production`
Expected: SUCCESS

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: All 68 passing tests still pass. Legacy 4 ERROR unchanged.
