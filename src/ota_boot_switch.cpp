#include "ota_boot_switch.h"

#include <Arduino.h>
#include <esp_rom_crc.h>
#include <esp_spi_flash.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "config.h"

namespace ota_boot {
namespace {

struct __attribute__((packed)) SelectEntry {
  uint32_t otaSequence;
  uint8_t sequenceLabel[20];
  uint32_t otaState;
  uint32_t crc;
};

static_assert(sizeof(SelectEntry) == 32, "OTA select entry must be 32 bytes");

constexpr uint32_t OTA_IMAGE_NEW = 0;
constexpr uint32_t OTA_IMAGE_INVALID = 3;
constexpr uint32_t OTA_IMAGE_ABORTED = 4;

uint32_t sequenceCrc(uint32_t sequence) {
  return esp_rom_crc32_le(UINT32_MAX, reinterpret_cast<const uint8_t*>(&sequence), sizeof(sequence));
}

}  // namespace

bool switchTo(const esp_partition_t* destination) {
  if (destination == nullptr) return false;

  const esp_partition_t* otaData =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (otaData == nullptr || otaData->size < 2 * SPI_FLASH_SEC_SIZE) {
    DBG_PRINTLN("[OTA] otadata partition missing or too small");
    return false;
  }

  SelectEntry entries[2] = {};
  if (esp_partition_read(otaData, 0, &entries[0], sizeof(SelectEntry)) != ESP_OK ||
      esp_partition_read(otaData, SPI_FLASH_SEC_SIZE, &entries[1], sizeof(SelectEntry)) != ESP_OK) {
    DBG_PRINTLN("[OTA] Failed to read otadata");
    return false;
  }

  int activeEntry = -1;
  uint32_t activeSequence = 0;
  for (int i = 0; i < 2; ++i) {
    if (entries[i].otaSequence == UINT32_MAX) continue;
    if (entries[i].crc != sequenceCrc(entries[i].otaSequence)) continue;
    if (entries[i].otaState == OTA_IMAGE_INVALID || entries[i].otaState == OTA_IMAGE_ABORTED) continue;
    if (activeEntry < 0 || entries[i].otaSequence > activeSequence) {
      activeEntry = i;
      activeSequence = entries[i].otaSequence;
    }
  }

  const uint32_t destinationIndex =
      static_cast<uint32_t>(destination->subtype) - static_cast<uint32_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0);
  if (destinationIndex > 15) {
    DBG_PRINTLN("[OTA] Destination is not an OTA app partition");
    return false;
  }

  // This project deliberately uses exactly two OTA app slots. ESP-IDF selects
  // slot (sequence - 1) % slotCount.
  uint32_t nextSequence = activeSequence + 1;
  while (((nextSequence - 1u) % 2u) != (destinationIndex % 2u)) ++nextSequence;

  SelectEntry next = {};
  next.otaSequence = nextSequence;
  std::memset(next.sequenceLabel, 0xFF, sizeof(next.sequenceLabel));
  next.otaState = OTA_IMAGE_NEW;
  next.crc = sequenceCrc(next.otaSequence);

  const int targetEntry = (activeEntry == 0) ? 1 : 0;
  const size_t targetOffset = static_cast<size_t>(targetEntry) * SPI_FLASH_SEC_SIZE;
  if (esp_partition_erase_range(otaData, targetOffset, SPI_FLASH_SEC_SIZE) != ESP_OK) {
    DBG_PRINTLN("[OTA] Failed to erase inactive otadata entry");
    return false;
  }
  if (esp_partition_write(otaData, targetOffset, &next, sizeof(next)) != ESP_OK) {
    DBG_PRINTLN("[OTA] Failed to write otadata entry");
    return false;
  }

  DBG_PRINTF("[OTA] Boot target set to %s (sequence %lu)\n", destination->label,
             static_cast<unsigned long>(nextSequence));
  return true;
}

}  // namespace ota_boot
