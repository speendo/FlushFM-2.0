# CLAUDE.md – Project Rules for AI Assistants

This file contains rules and context that AI assistants should follow when working on this project.
It is read automatically at the start of each session.

---

## Project Overview

**FlushFM 2.0** is an ESP32-based internet radio with:
- Audio streaming from internet radio stations
- A display (type TBD) for station/track info
- A light intensity sensor to switch the device on/off automatically

---

## General Rules

- Write all code, comments, commit messages, and documentation in **English**
- Implementation follows user stories defined in `requirements/user-stories/`
- Coding conventions and architectural decisions are defined in `requirements/guidelines/`
- Always check relevant guidelines before writing code
- Implement one user story at a time; mark it as done when all acceptance criteria are met
- Do not introduce libraries or frameworks that are not approved in `requirements/guidelines/coding.md`
- Never touch or read any files outside of the root of the project (FlushFM 2.0)
- Never install anything without explicit permission
- Try to aim for solutions that use sparse resources on an ESP32 efficiently - only head for less efficient solutions if and only if this would vastly improve readibality and/or clarity of the code
- When unsure about requirements and/or implementation details: ask user before implementing and if possible suggest possible solutions
- Whenever the functionality changes (e.g. new features implemented, changes in the pinout, etc.), update the documentation according to `requirements/guidelines/documentation.md`

---

## Folder Structure

```
FlushFM 2.0/
├── CLAUDE.md                          ← This file
├── README.md                          ← Project overview
├── requirements/
│   ├── guidelines/                    ← Coding rules, architecture decisions
│   │   ├── _template.md
│   │   ├── documentation.md
│   │   ├── hardware.md
│   │   ├── software-architecture.md
│   │   ├── modularity.md
│   │   ├── concurrency.md
│   │   ├── state-management.md
│   │   ├── testing.md
│   │   └── debug.md
│   └── user-stories/                  ← One file per user story
│       └── _template.md
└── docs/                              ← Technical documentation (features, hardware, pinout, tested setups)
```

---

## Working Process

1. Read the relevant user story before implementing
2. Read the guidelines before choosing libraries or patterns
3. Ask for clarification if acceptance criteria are ambiguous
4. Keep changes small and focused on one story at a time
