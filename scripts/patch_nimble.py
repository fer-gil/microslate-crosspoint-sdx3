"""Narrow, version-checked fix for esp-nimble-cpp 2.3.4 / ESP-IDF 4.4.

The library already resolves disconnect clients by address/handle because the
incoming callback context can be wrong. Its task snapshot must happen AFTER
that resolution as well, otherwise a random pointer reaches xTaskNotify.
Upstream: https://github.com/h2zero/esp-nimble-cpp/blob/2.3.4/src/NimBLEClient.cpp
"""
from pathlib import Path
import hashlib

ORIGINAL_SHA256 = "9b44b65333325602a3089384e2ef515852548d1164e1284ab3aa380040520681"
MARKER = "MicroSlate: resolve disconnect client before task snapshot"
OLD_SNAPSHOT = "    NimBLETaskData* pTaskData = pClient->m_pTaskData; // save a copy in case client is deleted"
NEW_SNAPSHOT = """    // MicroSlate: resolve disconnect client before task snapshot.
    NimBLETaskData* pTaskData = nullptr;
    if (event->type != BLE_GAP_EVENT_DISCONNECT && pClient != nullptr) {
        pTaskData = pClient->m_pTaskData;
    }"""
OLD_REASON = "            rc = event->disconnect.reason;"
NEW_REASON = """            // Client has now been resolved from the event, not the unreliable arg.
            pTaskData = pClient->m_pTaskData;
            rc = event->disconnect.reason;"""


def patch_text(source):
    if MARKER in source:
        # Validate the underlying pinned source even on an incremental build.
        original = source.replace(NEW_SNAPSHOT, OLD_SNAPSHOT).replace(NEW_REASON, OLD_REASON)
        if hashlib.sha256(original.encode()).hexdigest() != ORIGINAL_SHA256:
            raise RuntimeError("Unexpected modified NimBLE source; refusing to build")
        return source
    if hashlib.sha256(source.encode()).hexdigest() != ORIGINAL_SHA256:
        raise RuntimeError("Expected esp-nimble-cpp 2.3.4 NimBLEClient.cpp; refusing to patch another version")
    if source.count(OLD_SNAPSHOT) != 1 or source.count(OLD_REASON) != 1:
        raise RuntimeError("NimBLE disconnect patch context mismatch")
    return source.replace(OLD_SNAPSHOT, NEW_SNAPSHOT).replace(OLD_REASON, NEW_REASON)


def apply_patch(path):
    source = path.read_text()
    patched = patch_text(source)
    if source != patched:
        path.write_text(patched)
    print("MicroSlate NimBLE 2.3.4 disconnect-context fix verified")


# PlatformIO post scripts run after dependency discovery, before compilation.
if "Import" in globals():
    Import("env")
    apply_patch(Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env["PIOENV"] /
                "esp-nimble-cpp" / "src" / "NimBLEClient.cpp")
