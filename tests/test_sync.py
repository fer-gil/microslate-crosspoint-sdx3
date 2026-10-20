"""Exercise real sync functions without running its startup/login integration."""
import ast
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace

source = Path(__file__).parents[1] / "sync" / "microslate_sync.py"
module = ast.parse(source.read_text())
functions = [n for n in module.body if isinstance(n, ast.FunctionDef) and n.name in {"get_local_files", "download_file"}]
with tempfile.TemporaryDirectory() as folder:
    payload = "# Introducción\r\n\r\nTexto[^ñ].\n".encode()
    class Response:
        content = payload
        def raise_for_status(self): pass
    context = {"os": os, "LOCAL_DIR": folder, "DEVICE_URL": "http://example.invalid",
               "log": SimpleNamespace(info=lambda *args: None),
               "requests": SimpleNamespace(get=lambda *args, **kwargs: Response())}
    exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), "exec"), context)
    Path(folder,"old.TXT").write_bytes(b"old")
    Path(folder,"ignore.md.tmp").write_bytes(b"ignore")
    context["download_file"]("new.md")
    assert Path(folder,"new.md").read_bytes() == payload
    assert context["get_local_files"]() == {"old.TXT": 3, "new.md": len(payload)}
    assert not Path(folder,"new.md.part").exists()
print("Wi-Fi sync preserves Markdown, legacy files, UTF-8 and exact newline bytes.")
