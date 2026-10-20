"""Validate the saved interchange syntax with Pandoc's actual Markdown reader."""
import json
from pathlib import Path
import subprocess

fixture = Path(__file__).with_name("acceptance.md")
original = fixture.read_bytes()
ast = json.loads(subprocess.check_output(["pandoc", "-f", "markdown+footnotes", "-t", "json", str(fixture)]))
counts = {}
notes = []
def walk(value):
    if isinstance(value, dict):
        tag = value.get("t")
        if tag:
            counts[tag] = counts.get(tag, 0) + 1
        if tag == "Note":
            notes.append(value["c"])
        for item in value.values():
            walk(item)
    elif isinstance(value, list):
        for item in value:
            walk(item)
walk(ast)
for tag in ["Header", "Strong", "Emph", "BulletList", "OrderedList"]:
    assert counts.get(tag, 0) > 0, tag
assert len(notes) == 3
assert notes[0] == notes[1], "Named repeated reference must resolve to the same body"
assert len(notes[0]) == 2, "Indented continuation must preserve both footnote paragraphs"
assert fixture.read_bytes() == original
print("Pandoc accepts headings, emphasis, lists and repeated/multiline footnotes.")
