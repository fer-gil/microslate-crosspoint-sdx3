#include "ble_connection.h"
#include <cassert>
#include <vector>
#include <functional>
#include <iostream>

struct UUID {
  uint16_t value;
  explicit UUID(uint16_t v):value(v){}
  bool operator!=(UUID other) const { return value!=other.value; }
  bool operator==(UUID other) const { return value==other.value; }
};
static bool alive;
static int step, dropAt;
static void operation() { if (++step==dropAt) alive=false; }
struct Descriptor {
  UUID getUUID() { return UUID(0x2908); }
  std::vector<uint8_t> readValue() { operation(); return {1,1}; }
};
struct Characteristic {
  uint16_t uuid=0x2a4d;
  bool notify=true, succeeds=true;
  int subscriptions=0;
  std::vector<Descriptor*> descriptors;
  explicit Characteristic(uint16_t id=0x2a4d):uuid(id){}
  UUID getUUID() const { return UUID(uuid); }
  bool canNotify() const { return notify; }
  bool writeValue(uint8_t*,int,bool) { operation(); return succeeds; }
  const std::vector<Descriptor*>& getDescriptors() { return descriptors; }
  bool subscribe(bool,void(*)()) { operation();++subscriptions;return succeeds; }
};
struct Service {
  Characteristic protocol{0x2a4e},boot{0x2a22};
  std::vector<Characteristic*> chars;
  bool hasBoot=false;
  Characteristic* getCharacteristic(UUID id) {
    operation();
    return id.value==0x2a4e ? &protocol : (hasBoot ? &boot : nullptr);
  }
  const std::vector<Characteristic*>& getCharacteristics(bool) { operation();return chars; }
};
struct Client {
  Service service;
  bool hasService=true, startOK=true, connected=false, cancelled=false;
  int connectedAt=2, pauses=0;
  bool connect(int,bool discard,bool async,bool mtu) {
    assert(discard && async && !mtu); // Regression: never publish sync GAP stack task data.
    return startOK;
  }
  bool isConnected() const { return connected; }
  void cancelConnect() { cancelled=true; }
  std::vector<int> getServices(bool) { operation();return hasService?std::vector<int>{1}:std::vector<int>{}; }
  Service* getService(UUID) { operation();return hasService?&service:nullptr; }
};
static void notify() {}
static void discovery() {
  for (int drop=0;drop<=9;++drop) {
    Client c; Characteristic reports[4];
    for (auto& r:reports)c.service.chars.push_back(&r);
    alive=true;step=0;dropAt=drop;uint8_t reportId=99;
    bool ok=setupBleHid<UUID>(c,notify,[]{return alive;},reportId);
    assert(ok==(drop==0)); // Drop during every operation including last subscribe.
    assert(reportId==0);
    if(drop==0) for(auto& r:reports)assert(r.subscriptions==1);
    if(drop)assert(step==drop); // Never perform another GATT operation after disconnection.
  }
  Client c;Characteristic input;Descriptor ref; input.descriptors={&ref};c.service.chars={&input};
  alive=true;step=0;dropAt=0;uint8_t id;
  assert(setupBleHid<UUID>(c,notify,[]{return alive;},id) && id==1);
  for(int drop=1;drop<=7;++drop) {
    alive=true;step=0;dropAt=drop;
    assert(!setupBleHid<UUID>(c,notify,[]{return alive;},id));
    assert(step==drop);
  }
  c.service.chars.clear();c.service.hasBoot=true;alive=true;step=0;dropAt=0;
  assert(setupBleHid<UUID>(c,notify,[]{return alive;},id));
  c.service.hasBoot=false;step=0;
  assert(!setupBleHid<UUID>(c,notify,[]{return alive;},id));
  c.hasService=false;step=0;
  assert(!setupBleHid<UUID>(c,notify,[]{return alive;},id));
  c.hasService=true;alive=false;step=0;
  assert(!setupBleHid<UUID>(c,notify,[]{return alive;},id) && step==0);
  alive=true;step=0;
  Characteristic failed,working;failed.succeeds=false;
  c.service.chars={&failed,&working};
  assert(setupBleHid<UUID>(c,notify,[]{return alive;},id));
  working.succeeds=false;step=0;
  assert(!setupBleHid<UUID>(c,notify,[]{return alive;},id));
}
static void transport() {
  for (int scenario=0;scenario<4;++scenario) {
    Client c;uint32_t t=0;
    if(scenario==1)c.startOK=false;
    if(scenario==2)c.connectedAt=100;
    bool ok=connectBleAsync(c,42,50,[&]{return t;},[&]{t+=10;if(++c.pauses==c.connectedAt)c.connected=true;},[&]{return scenario==3 && t>=10;});
    assert(ok==(scenario==0));
    assert(c.cancelled==(scenario==2 || scenario==3));
    assert(t<=50);
  }
}
int main() {
  discovery();transport();
  std::cout<<"BLE async GAP, connection timeout/failure and disconnect during every HID discovery/subscription step passed.\n";
}
