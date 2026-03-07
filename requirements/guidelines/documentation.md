# Guideline: Documentation

> **Status:** Active  
> **Last updated:** 2026-03-07

---

## Purpose

Defines where and how project documentation should be stored and maintained for FlushFM 2.0.

---

## Rules

1. **Store all project documentation in the `docs/` folder** at the project root.

2. **Maintain four core documentation types:**
   - Implemented features (what the device can do, user-facing functionality) that align with the overall goal of the project (e.g. internet radio streaming, display output, light-sensor-controlled auto on/off, configuration)
   - Hardware specifications (generic electrical and performance characteristics)
   - Tested setups (specific, reproducible test environments that were validated)
   - Pinout and hardware configuration (exact GPIO assignments and peripheral config)

3. **Use Markdown format for all documentation** in `docs/`.

4. **Update documentation when completing user stories** that change hardware usage or add features.

5. **Keep documentation concise and actionable** – provide examples and commands when helpful.

---

## Rationale

Centralized documentation in `docs/` keeps technical information close to the code while remaining separate from implementation guidelines. The four core types cover the essential information needed to reproduce builds, understand hardware requirements, and track implemented features.

---

## Exceptions

None. All project documentation goes in `docs/`.

---

## Examples

```
docs/
├── implemented-features.md
├── hardware-specs.md  
├── tested-setups.md
├── pinout.md
└── assets/
    └── schematic.png
```
