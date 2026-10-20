# Markdown visual editor — implementation plan

Goal: Typora-style editing on the Xteink, preserving raw Markdown on disk.

## Current baseline
- `src/file_manager.cpp` uses `.txt` for note names.
- `src/text_editor.cpp` maintains the editable raw text buffer.
- `src/ui_renderer.cpp` draws the current text without Markdown formatting.
- The README targets X4 hardware. X3 hardware compatibility needs verification before flashing.

## Design constraints
1. The editable buffer is the **source of truth**. Never strip Markdown markers from the saved content.
2. The active paragraph shows its raw source, including markers; inactive paragraphs are rendered with markers hidden.
3. Maintain a source-offset ↔ visual-position map to preserve cursor placement and selection.
4. Parse a limited subset first: ATX headings, emphasis/strong, and unordered lists.
5. Escape or unmatched delimiters remain literal text. No destructive transformations.
6. Render only changed lines where feasible; e-paper full refresh policy must remain safe.
7. Keep existing scroll, typewriter, pagination, autosave, backup and CrossPoint dual boot behavior.
8. Add `.md` support without losing access to legacy `.txt` notes. New notes should use `.md`; existing notes retain their extension unless explicitly converted.

## Milestones
- [ ] Build a standalone, testable inline/block Markdown tokenizer.
- [ ] Implement source-offset mapping and visual rendering for inactive paragraphs.
- [ ] Integrate with editor and renderer; toggle visual/plain modes.
- [ ] Update file management and Wi-Fi sync for `.md` while preserving `.txt`.
- [ ] Test cursor movement, editing delimiters, Unicode, wrap, autosave and backup.
- [ ] Build with PlatformIO and validate on the exact X3 hardware variant.
- [ ] Only then prepare a flashable firmware release.

## Acceptance examples
- Typing `# Introducción` and leaving the line renders a heading without `# `.
- Re-entering that line reveals `# ` and permits editing.
- `**Importante**` appears bold outside the active paragraph; the saved bytes still include asterisks.
- `- Elemento` renders as a bullet outside the active paragraph.
- Legacy `.txt` notes remain readable and are not silently rewritten.

**Status:** planning scaffold only; no firmware changes or device tests yet.
