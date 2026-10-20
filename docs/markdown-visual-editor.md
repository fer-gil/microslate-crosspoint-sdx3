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
4. Parse a limited subset first: ATX headings, emphasis/strong, unordered lists, and Markdown footnote references/definitions.
5. Escape or unmatched delimiters remain literal text. No destructive transformations.
6. Render only changed lines where feasible; e-paper full refresh policy must remain safe.
7. Keep existing scroll, typewriter, pagination, autosave, backup and CrossPoint dual boot behavior.
8. Add `.md` support without losing access to legacy `.txt` notes. New notes should use `.md`; existing notes retain their extension unless explicitly converted.

## Footnotes (required in first release)
- Accept Pandoc/Python-Markdown-style footnotes: `Texto[^1]` and `[^1]: Contenido de la nota.`
- In the visual editor, show a small superscript-like note marker after the reference; keep the raw `[^id]` syntax in the file.
- Render the note body in a footnotes section (at document end initially). Selecting or navigating to a reference should permit editing its source; avoid unsupported interactive popups in v1.
- Preserve stable identifiers (including named references), multiple references to the same note, and multiline definitions.
- Ensure exported `.md` is compatible with Pandoc. Footnote parsing must not mistake `[^id]` for an ordinary link.
- On a constrained e-ink display, superscript may initially use a compact baseline marker if fonts do not support raised glyph positioning.

## Milestones
- [ ] Build a standalone, testable inline/block Markdown tokenizer, including footnotes.
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
- `Texto[^1]` renders a compact footnote marker, with `[^1]: Nota al pie.` shown in the note section; the saved Markdown remains unchanged.
- Legacy `.txt` notes remain readable and are not silently rewritten.

**Status:** planning scaffold only; no firmware changes or device tests yet.
