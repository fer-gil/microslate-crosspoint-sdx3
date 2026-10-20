# MicroSlate — X3 Markdown branch

Read `docs/X3-INSTALL-ES.md` before proposing a device update.

- Target: ESP32-C3, X3 UC8253, 792×528, 16 MB. Do not assume UC8279 compatibility.
- `feature/markdown-visual-editor`, PR #1, implements a Markdown preview.
- Source is authoritative; never strip markers on save. New notes are `.md`,
  legacy `.txt` notes keep their extension, including on rename.
- Keep the 10-second idle / two-minute continuous autosave policy and safe writes.
- Host validation: `sh tests/run.sh`, `python3 tests/test_sync.py`,
  `python3 tests/test_pandoc.py` (requires Pandoc).
- Build: `pio run -e xteink_x3`. Contrary to older notes, this was successfully
  built on the owner's ARM Mac with PlatformIO 6.1.19 and espressif32 6.12.0.
- Do not run the default upload target: its offset is app0 (`0x10000`).
- On the inspected unit, CrossPoint boots from app0 and detects MicroSlate in
  app1 (`0x650000`, size `0x640000`). Keep CrossPoint and boot data intact.
- Historical notes claimed USB was locked and recommended SD update.bin.
  USB readback of all 16 MB succeeded; never assume SD updates target the correct
  slot or that a generic merged image preserves this dual boot.
- Current CrossPoint reports X3; that alone does not distinguish panel variants.
- The owner confirmed pairing and typing in the previously installed MicroSlate.
  This does not validate BLE or the renderer in the new build.
- Device backups and build outputs are ignored in `/backups` and `/artifacts`.
  Never commit/upload backups: they can contain private settings and credentials.
- No flashing, device success claim or stable release without verifying exact
  hardware, compiled image, backups, partition assignment and user intent.
