#pragma once
#include "config.h"
inline bool autosaveDue(uint32_t now, uint32_t lastInput, uint32_t lastSave) {
  return ((now-lastInput)>AUTO_SAVE_IDLE_MS && (now-lastSave)>AUTO_SAVE_IDLE_MS)
      || (now-lastSave)>AUTO_SAVE_MAX_MS;
}
