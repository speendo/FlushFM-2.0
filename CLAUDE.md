# CLAUDE.md – Project Rules for AI Assistants

**FlushFM 2.0** – ESP32-S3 internet radio with audio streaming, ILI9341 TFT display, and a light sensor for automatic on/off control.

---

## Rules

- All code, comments, commits, and documentation must be written in **English**
- Implement one user story at a time; mark it done only when all acceptance criteria are met
- Always read the relevant user story and guidelines **before** writing code
- Use only libraries approved in `requirements/guidelines/` (→ `software-architecture.md`)
- Prefer resource-efficient solutions; only trade efficiency for readability when the gain is significant
- Ask before implementing when requirements or details are ambiguous; suggest options when possible
- Never read or modify files outside the project root
- Never install anything without explicit permission
- Update `docs/` after any change that affects functionality or pinout (→ `requirements/guidelines/documentation.md`)

---

## Folder Structure

```
FlushFM 2.0/
├── CLAUDE.md
├── README.md
├── requirements/
│   ├── guidelines/        ← All coding rules and architecture decisions
│   └── user-stories/
│       ├── open/          ← Stories with status "To Do"
│       ├── in-progress/   ← Stories with status "In Progress"
│       ├── done/          ← Stories with status "Done"
│       └── _template.md   ← Story template
├── docs/                  ← Technical documentation (features, hardware, pinout, tested setups)
├── lib/                   ← One sub-folder per component library
├── src/                   ← Orchestration only (main.cpp, config.h)
├── test/                  ← Unit and component tests
└── data/                  ← LittleFS assets (Web UI)
```

---

## Working Process

1. Read the user story
2. Read relevant guidelines
3. Ask if acceptance criteria are ambiguous
4. Implement; keep changes small and focused
