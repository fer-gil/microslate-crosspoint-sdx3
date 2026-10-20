#pragma once
#include <cstdint>

// Use asynchronous GAP setup: NimBLE 2.3.4's synchronous connect/security paths
// retain stack-local task data that can be released again by a late disconnect.
// K70's 14-byte reports fit the default ATT MTU (23); no exchange is necessary.
template<class Client, class Address, class Now, class Pause, class Failed>
bool connectBleAsync(Client& client, const Address& address, uint32_t timeout,
                     Now now, Pause pause, Failed failed) {
  if (!client.connect(address, true, true, false)) return false;
  const uint32_t started = now();
  while (!client.isConnected()) {
    if (failed() || static_cast<uint32_t>(now()-started) >= timeout) {
      client.cancelConnect();
      return false;
    }
    pause();
  }
  return !failed();
}

// All discovery pointers belong to this attempt. Disconnect callbacks may mark
// the link dead, but must not clear pointers midway through a blocking GATT call.
// The client retains its attribute objects until this attempt has returned.
template<class UUID, class Client, class Callback, class Alive>
bool setupBleHid(Client& client, Callback notify, Alive alive, uint8_t& reportId) {
  reportId = 0;
  if (!alive() || client.getServices(true).empty() || !alive()) return false;
  auto* service = client.getService(UUID(uint16_t(0x1812)));
  if (!service || !alive()) return false;
  auto* protocol = service->getCharacteristic(UUID(uint16_t(0x2a4e)));
  if (!alive()) return false;
  if (protocol) {
    uint8_t mode = 1;
    if (!protocol->writeValue(&mode, 1, true) || !alive()) return false;
  }
  const auto& chars = service->getCharacteristics(true);
  if (!alive()) return false;
  for (auto* chr : chars) {
    if (!alive()) return false;
    if (chr->getUUID() != UUID(uint16_t(0x2a4d)) || !chr->canNotify()) continue;
    for (auto* descriptor : chr->getDescriptors()) {
      if (!alive()) return false;
      if (descriptor->getUUID() != UUID(uint16_t(0x2908))) continue;
      auto ref = descriptor->readValue();
      if (!alive()) return false;
      if (ref.size() >= 2 && uint8_t(ref[1]) == 1) {
        bool ok = chr->subscribe(true, notify);
        if (!alive()) return false;
        if (ok) { reportId = uint8_t(ref[0]); return true; }
      }
    }
  }
  // Preserve the installed K70 path: subscribe to every notifiable report when
  // the keyboard supplies no cached Report Reference descriptor.
  bool subscribed = false;
  for (auto* chr : chars) {
    if (!alive()) return false;
    if (chr->getUUID() == UUID(uint16_t(0x2a4d)) && chr->canNotify()) {
      bool ok = chr->subscribe(true, notify);
      if (!alive()) return false;
      subscribed |= ok;
    }
  }
  if (subscribed) return alive();
  if (!alive()) return false;
  auto* boot = service->getCharacteristic(UUID(uint16_t(0x2a22)));
  return alive() && boot && boot->canNotify() && boot->subscribe(true, notify) && alive();
}
