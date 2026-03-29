# Rule: Documentation
[Status: Active | Updated: 2026-03-09]
**Context:** Project Root | **Goal:** Centralized, version-controlled technical and hardware documentation

---

## 1. Core Rules
- **Location:** All documentation must reside in the `/docs` folder at the project root.
- **Format:** Use Markdown (`.md`) for all text-based documentation.
- **Core Documents:** Maintain these files:
    1. `implemented-features.md`: Functionality contributing to project goal (e.g. Streaming, Display, Light-Sensor, Web-Config)
    2. `hardware-specs.md`: Hardware characteristics (e.g. ESP32-S3 or better)
    3. `tested-setups.md`: Reproducible environments (explicitely tested hardware)
    4. `pinout.md`: Exact GPIO assignments and peripheral configurations
- **Maintenance:** Update documentation immediately after changing code that alter functionality (e.g. after completing user story)
- **Style:** Keep it concise and actionable; include examples or commands when relevant

## 2. Constraints & Exceptions
- **Never:** Store technical documentation in the `src/` or `include/` folders
- **Never:** Document features not contributing to end user experience (e.g. tests)
- **Exception:** The root `README.md` is the only documentation file allowed outside the `/docs` folder
- **Reference:** Use `requirements/user-stories/_template.md` as the canonical template for creating and updating user story files

## 3. Reference Pattern

```
docs/
├── implemented-features.md
├── hardware-specs.md
├── tested-setups.md
└── pinout.md
```