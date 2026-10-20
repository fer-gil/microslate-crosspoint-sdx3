#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
// HOGP normally sends the eight-byte input report without its report ID.
// Do not mistake Ctrl (modifier 0x01) for report ID 1 and shift the report.
inline bool normalizeKeyboardReport(const uint8_t* data, size_t length, uint8_t reportId, uint8_t out[8]) {
  if (length==9 && reportId && data[0]==reportId) { ++data; --length; }
  if (length==15 && reportId && data[0]==reportId) { ++data; --length; }
  if (length==14) { // Existing Corsair K70 BLE layout: modifier + 13 keycodes.
    std::memset(out,0,8); out[0]=data[0];
    size_t target=2;
    for (size_t i=1;i<length && target<8;++i) if(data[i]) out[target++]=data[i];
    return true;
  }
  if (length!=7 && length!=8) return false;
  if (length==8) { std::memcpy(out,data,8); return true; }
  out[0]=data[0]; out[1]=0; std::memcpy(out+2,data+1,6); return true;
}
