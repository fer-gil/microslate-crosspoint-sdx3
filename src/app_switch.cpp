#include "app_switch.h"

#include <Arduino.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "config.h"
#include "ota_boot_switch.h"

namespace {

const esp_partition_t* crossPointPartition = nullptr;

bool hasAppImageHeader(const esp_partition_t* partition) {
  if (partition == nullptr) return false;

  esp_image_header_t header = {};
  if (esp_partition_read(partition, 0, &header, sizeof(header)) != ESP_OK) return false;
  return header.magic == ESP_IMAGE_HEADER_MAGIC && header.segment_count > 0 && header.segment_count <= 16;
}

}  // namespace

void appSwitchSetup() {
  crossPointPartition = nullptr;

  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* slotZero =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);

  // The combined image always places CrossPoint in ota_0 and MicroSlate in
  // ota_1. Do not expose a generic "other slot" entry: stale firmware in a
  // standalone installation must never look like a valid menu destination.
  if (slotZero != nullptr && running != nullptr && slotZero->address != running->address &&
      hasAppImageHeader(slotZero)) {
    crossPointPartition = slotZero;
    DBG_PRINTLN("[OTA] CrossPoint detected in ota_0");
  } else {
    DBG_PRINTLN("[OTA] CrossPoint slot not available");
  }
}

bool isCrossPointAvailable() { return crossPointPartition != nullptr; }

bool switchToCrossPoint() {
  if (crossPointPartition == nullptr) return false;
  if (!ota_boot::switchTo(crossPointPartition)) return false;

  delay(100);
  ESP.restart();
  return true;
}
