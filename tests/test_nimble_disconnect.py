"""Compile the real pinned SDK disconnect handler against a small fake host.

Only unrelated GAP cases are excluded. The tested prefix, client resolution,
disconnect body and completion release are extracted from NimBLEClient.cpp.
"""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("nimble_patch", root / "scripts/patch_nimble.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
source_path = root / ".pio/libdeps/xteink_x3/esp-nimble-cpp/src/NimBLEClient.cpp"
source = source_path.read_text()
patched = patch.patch_text(source)
if "--require-applied" in sys.argv:
    assert source == patched, "Firmware dependency was not patched by the build hook"
assert patch.patch_text(patched) == patched
try:
    patch.patch_text(source + "\n// unreviewed upstream change")
except RuntimeError:
    pass
else:
    raise AssertionError("Unexpected library version was accepted")

def disconnect_handler(text):
    start = text.index("int NimBLEClient::handleGapEvent(")
    next_case = text.index("        case BLE_GAP_EVENT_CONNECT:", start)
    tail = text.index("    if (pTaskData != nullptr)", next_case)
    end = text.index("} // handleGapEvent", tail) + 1
    return text[start:next_case] + "default: return 0;\n}\n" + text[tail:end]

original = patched.replace(patch.NEW_SNAPSHOT, patch.OLD_SNAPSHOT).replace(patch.NEW_REASON, patch.OLD_REASON)

fake_host = r'''
#include <cassert>
#include <cstdint>
#define NIMBLE_LOGD(...)
#define NIMBLE_LOGE(...)
constexpr int BLE_GAP_EVENT_DISCONNECT=1;
constexpr int BLE_HS_ECONTROLLER=10, BLE_HS_ETIMEOUT_HCI=11, BLE_HS_ENOTSYNCED=12, BLE_HS_EOS=13;
constexpr int BLE_HS_ERR_HCI_BASE=512, BLE_ERR_CONN_ESTABLISHMENT=62, BLE_HS_CONN_HANDLE_NONE=0xffff;
struct ble_gap_event {
  int type=BLE_GAP_EVENT_DISCONNECT;
  struct { struct { int peer_ota_addr=1,peer_id_addr=2,conn_handle=3; } conn; int reason=19; } disconnect;
};
struct NimBLETaskData { int owner=123; } validTask, wrongTask;
static int callbacks=0,releases=0,deleted=0;
struct Callbacks {
  template<class C> void onDisconnect(C*,int){++callbacks;}
  template<class C> void onConnectFail(C*,int){++callbacks;}
};
class NimBLEClient {
public:
  NimBLETaskData* m_pTaskData=nullptr;
  int m_terminateFailCount=0,m_asyncSecureAttempt=0,m_connHandle=3;
  struct { bool asyncConnect=false,deleteOnDisconnect=false,deleteOnConnectFail=false; } m_config;
  Callbacks cb;
  Callbacks* m_pClientCallbacks=&cb;
  static int handleGapEvent(ble_gap_event*,void*);
};
static NimBLEClient* resolved=nullptr;
struct NimBLEDevice {
  static NimBLEClient* getClientByPeerAddress(int){return resolved;}
  static NimBLEClient* getClientByHandle(int){return resolved;}
  static void onReset(int){}
  static void deleteClient(NimBLEClient*){++deleted;}
};
struct NimBLEUtils {
  static void taskRelease(const NimBLETaskData& data,int){assert(&data==&validTask);++releases;}
};
'''

cases = r'''
int main() {
  ble_gap_event event;
  NimBLEClient actual,wrong;
  resolved=&actual;
  // Exact fault condition: an invalid disconnect arg must never be dereferenced.
  NimBLEClient::handleGapEvent(&event,reinterpret_cast<void*>(uintptr_t(1)));
  assert(callbacks==1 && releases==0 && actual.m_connHandle==BLE_HS_CONN_HANDLE_NONE);
  // Another valid object's completion must not be used for this connection.
  actual.m_pTaskData=&validTask;
  wrong.m_pTaskData=&wrongTask;
  NimBLEClient::handleGapEvent(&event,&wrong);
  assert(callbacks==2 && releases==1);
  // Unknown disconnected peer: return without touching the unreliable arg.
  resolved=nullptr;
  NimBLEClient::handleGapEvent(&event,reinterpret_cast<void*>(uintptr_t(1)));
  assert(callbacks==2 && releases==1);
  // Async connection-establishment failure has no waiting task to notify.
  resolved=&actual; actual.m_pTaskData=nullptr;actual.m_config.asyncConnect=true;
  event.disconnect.reason=BLE_HS_ERR_HCI_BASE+BLE_ERR_CONN_ESTABLISHMENT;
  NimBLEClient::handleGapEvent(&event,reinterpret_cast<void*>(uintptr_t(1)));
  assert(callbacks==3 && releases==1 && deleted==0);
}
'''
with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    for fixed, text in [(False, original), (True, patched)]:
        cpp = folder / "disconnect.cpp"
        cpp.write_text(fake_host + disconnect_handler(text) + cases)
        binary = folder / "disconnect"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(cpp), "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        if fixed:
            assert result.returncode == 0, result.stderr
        else:
            assert result.returncode != 0 and ("AddressSanitizer" in result.stderr or "runtime error" in result.stderr)
print("Real NimBLE disconnect callback: old fault reproduced; invalid arg, wrong client, missing peer and async failure pass after patch; idempotence/version guard passed.")
