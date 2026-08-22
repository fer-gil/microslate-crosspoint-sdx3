#pragma once

#include <esp_partition.h>

namespace ota_boot {

// Select an already-installed OTA application without asking the running IDF
// to verify it. CrossPoint uses the same raw otadata update because Xteink's
// factory bootloader accepts images that the runtime verifier can reject with
// a bogus eFuse revision error.
bool switchTo(const esp_partition_t* destination);

}  // namespace ota_boot
