#include "input_handler.h"
#include "text_editor.h"
#include "file_manager.h"
#include "ble_keyboard.h"
#include "wifi_sync.h"

#include <Arduino.h>
#include <SDCardManager.h>

// External variables
extern bool autoReconnectEnabled;
extern bool darkMode;
extern bool cleanMode;
extern bool deleteConfirmPending;
extern WritingMode writingMode;
extern FontSize fontSize;
extern bool showWordCount;

// External functions
void storePairedDevice(const std::string& address, const std::string& name);
bool getStoredDevice(std::string& address, std::string& name);
void clearStoredDevice();
uint32_t getCurrentPasskey();
bool isDeviceScanning();
void refreshScanNow();
void clearAllBluetoothBonds();

// --- Input Queue ---
static KeyEvent inputQueue[INPUT_QUEUE_SIZE];
static int queueHead = 0;
static int queueTail = 0;
static volatile bool queueFull = false;

// --- CapsLock state ---
static bool capsLockOn = false;

// State used by the US-International helpers below.
extern bool screenDirty;
extern char renameBuffer[];
extern int renameBufferLen;

// US-International dead-key state.
static char pendingDeadKey = 0;

static bool isDeadKey(char c) {
  return c == '\'' || c == '"' || c == '`' || c == '~' || c == '^';
}

static const char* composeDeadKey(char dead, char base) {
  switch (dead) {
    case '\'':
      switch (base) {
        case 'a': return "\xC3\xA1"; // á
        case 'A': return "\xC3\x81"; // Á
        case 'e': return "\xC3\xA9"; // é
        case 'E': return "\xC3\x89"; // É
        case 'i': return "\xC3\xAD"; // í
        case 'I': return "\xC3\x8D"; // Í
        case 'o': return "\xC3\xB3"; // ó
        case 'O': return "\xC3\x93"; // Ó
        case 'u': return "\xC3\xBA"; // ú
        case 'U': return "\xC3\x9A"; // Ú
        case 'y': return "\xC3\xBD"; // ý
        case 'Y': return "\xC3\x9D"; // Ý
      }
      break;

    case '"':
      switch (base) {
        case 'a': return "\xC3\xA4"; // ä
        case 'A': return "\xC3\x84"; // Ä
        case 'e': return "\xC3\xAB"; // ë
        case 'E': return "\xC3\x8B"; // Ë
        case 'i': return "\xC3\xAF"; // ï
        case 'I': return "\xC3\x8F"; // Ï
        case 'o': return "\xC3\xB6"; // ö
        case 'O': return "\xC3\x96"; // Ö
        case 'u': return "\xC3\xBC"; // ü
        case 'U': return "\xC3\x9C"; // Ü
        case 'y': return "\xC3\xBF"; // ÿ
      }
      break;

    case '`':
      switch (base) {
        case 'a': return "\xC3\xA0"; // à
        case 'A': return "\xC3\x80"; // À
        case 'e': return "\xC3\xA8"; // è
        case 'E': return "\xC3\x88"; // È
        case 'i': return "\xC3\xAC"; // ì
        case 'I': return "\xC3\x8C"; // Ì
        case 'o': return "\xC3\xB2"; // ò
        case 'O': return "\xC3\x92"; // Ò
        case 'u': return "\xC3\xB9"; // ù
        case 'U': return "\xC3\x99"; // Ù
      }
      break;

    case '^':
      switch (base) {
        case 'a': return "\xC3\xA2"; // â
        case 'A': return "\xC3\x82"; // Â
        case 'e': return "\xC3\xAA"; // ê
        case 'E': return "\xC3\x8A"; // Ê
        case 'i': return "\xC3\xAE"; // î
        case 'I': return "\xC3\x8E"; // Î
        case 'o': return "\xC3\xB4"; // ô
        case 'O': return "\xC3\x94"; // Ô
        case 'u': return "\xC3\xBB"; // û
        case 'U': return "\xC3\x9B"; // Û
      }
      break;

    case '~':
      switch (base) {
        case 'a': return "\xC3\xA3"; // ã
        case 'A': return "\xC3\x83"; // Ã
        case 'n': return "\xC3\xB1"; // ñ
        case 'N': return "\xC3\x91"; // Ñ
        case 'o': return "\xC3\xB5"; // õ
        case 'O': return "\xC3\x95"; // Õ
      }
      break;
  }

  return nullptr;
}

static void insertEditorText(const char* text) {
  editorInsertText(text);
  screenDirty = true;
}

static void handleEditorPrintable(char c) {
  // Pandoc [^id] must be writable without the international dead-key composer
  // turning identifiers beginning with vowels into circumflex characters.
  if (!pendingDeadKey && c == '^' && editorGetCursorPosition() > 0 &&
      editorGetBuffer()[editorGetCursorPosition()-1] == '[') {
    editorInsertChar(c); screenDirty = true; return;
  }
  if (pendingDeadKey) {
    char dead = pendingDeadKey;

    // Dead key + Space or the same dead key prints the accent itself.
    if (c == ' ' || c == dead) {
      char literal[2] = {dead, '\0'};
      pendingDeadKey = 0;
      insertEditorText(literal);
      return;
    }

    // A different dead key commits the old one and starts a new sequence.
    if (isDeadKey(c)) {
      char literal[2] = {dead, '\0'};
      insertEditorText(literal);
      pendingDeadKey = c;
      return;
    }

    pendingDeadKey = 0;

    if (const char* composed = composeDeadKey(dead, c)) {
      insertEditorText(composed);
      return;
    }

    // Unsupported combination: print both characters normally.
    char literal[2] = {dead, '\0'};
    char base[2] = {c, '\0'};
    editorInsertText(literal);
    editorInsertText(base);
    screenDirty = true;
    return;
  }

  if (isDeadKey(c)) {
    pendingDeadKey = c;
    return;
  }

  editorInsertChar(c);
  screenDirty = true;
}

static void appendRenameText(const char* text) {
  size_t n = strlen(text);
  if (n == 0 || renameBufferLen + (int)n >= MAX_TITLE_LEN) return;

  memcpy(renameBuffer + renameBufferLen, text, n);
  renameBufferLen += (int)n;
  renameBuffer[renameBufferLen] = '\0';
  screenDirty = true;
}

static void handleRenamePrintable(char c) {
  if (pendingDeadKey) {
    char dead = pendingDeadKey;

    if (c == ' ' || c == dead) {
      char literal[2] = {dead, '\0'};
      pendingDeadKey = 0;
      appendRenameText(literal);
      return;
    }

    if (isDeadKey(c)) {
      char literal[2] = {dead, '\0'};
      appendRenameText(literal);
      pendingDeadKey = c;
      return;
    }

    pendingDeadKey = 0;

    if (const char* composed = composeDeadKey(dead, c)) {
      appendRenameText(composed);
      return;
    }

    char literal[2] = {dead, '\0'};
    char base[2] = {c, '\0'};
    appendRenameText(literal);
    appendRenameText(base);
    return;
  }

  if (isDeadKey(c)) {
    pendingDeadKey = c;
    return;
  }

  char text[2] = {c, '\0'};
  appendRenameText(text);
}

// Where to return after title edit is confirmed or cancelled
static UIState renameReturnState = UIState::FILE_BROWSER;

// Forward declaration
static void openTitleEdit(const char* currentTitle, UIState returnTo);

// OTA app detection (defined in main.cpp)
extern OtaAppEntry otaApps[];
extern int otaAppCount;
void switchToOtaApp(int index);

// --- Shared UI state (defined in main.cpp) ---
extern UIState currentState;
extern int mainMenuSelection;
extern int selectedFileIndex;
extern int settingsSelection;
extern int bluetoothDeviceSelection;
extern int pairedKeyboardSelection;
extern Orientation currentOrientation;
extern int charsPerLine;
extern bool screenDirty;
extern char renameBuffer[];
extern int renameBufferLen;

void inputSetup() {
  queueHead = 0;
  queueTail = 0;
  queueFull = false;
  capsLockOn = false;
}

static bool isQueueEmpty() {
  return (queueHead == queueTail) && !queueFull;
}

void enqueueKeyEvent(uint8_t keyCode, uint8_t modifiers, bool pressed) {
  noInterrupts();
  if (!queueFull) {
    inputQueue[queueHead].keyCode = keyCode;
    inputQueue[queueHead].modifiers = modifiers;
    inputQueue[queueHead].pressed = pressed;
    queueHead = (queueHead + 1) % INPUT_QUEUE_SIZE;
    if (queueHead == queueTail) queueFull = true;
  }
  interrupts();
}

static KeyEvent dequeueKeyEvent() {
  KeyEvent event = {0, 0, false};
  noInterrupts();
  if (!isQueueEmpty()) {
    event = inputQueue[queueTail];
    queueTail = (queueTail + 1) % INPUT_QUEUE_SIZE;
    queueFull = false;
  }
  interrupts();
  return event;
}

char hidToAscii(uint8_t hid, uint8_t modifiers) {
  bool shifted = isShift(modifiers) ^ capsLockOn;

  // Letters a-z (HID 0x04-0x1D)
  if (hid >= 0x04 && hid <= 0x1D) {
    char base = 'a' + (hid - 0x04);
    return shifted ? (base - 32) : base;
  }

  // Number row (HID 0x1E-0x27)
  static const char unshifted[] = "1234567890";
  static const char shiftedNum[] = "!@#$%^&*()";
  if (hid >= 0x1E && hid <= 0x27) {
    int idx = hid - 0x1E;
    return isShift(modifiers) ? shiftedNum[idx] : unshifted[idx];
  }

  // Special keys
  switch (hid) {
    case 0x28: return '\n';  // Enter
    case 0x2B: return '\t';  // Tab
    case 0x2C: return ' ';   // Space

    // Symbol keys
    case 0x2D: return isShift(modifiers) ? '_' : '-';
    case 0x2E: return isShift(modifiers) ? '+' : '=';
    case 0x2F: return isShift(modifiers) ? '{' : '[';
    case 0x30: return isShift(modifiers) ? '}' : ']';
    case 0x31: return isShift(modifiers) ? '|' : '\\';
    case 0x33: return isShift(modifiers) ? ':' : ';';
    case 0x34: return isShift(modifiers) ? '"' : '\'';
    case 0x35: return isShift(modifiers) ? '~' : '`';
    case 0x36: return isShift(modifiers) ? '<' : ',';
    case 0x37: return isShift(modifiers) ? '>' : '.';
    case 0x38: return isShift(modifiers) ? '?' : '/';

    default: return 0;
  }
}

// Handle text editor input
static void handleEditorKey(uint8_t keyCode, uint8_t modifiers) {
  // Ctrl shortcuts
  if (isCtrl(modifiers)) {
    if (keyCode == HID_KEY_M) { // Ctrl+M: visual/source Markdown
      editorToggleVisual(); screenDirty = true; return;
    }
    if (keyCode == HID_KEY_S) {
      saveCurrentFile();
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_Z) {
      cleanMode = !cleanMode;
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_N) {
      openTitleEdit(editorGetCurrentTitle(), UIState::TEXT_EDITOR);
      return;
    }
    if (keyCode == HID_KEY_T) {
      writingMode = (writingMode == WritingMode::TYPEWRITER) ? WritingMode::NORMAL : WritingMode::TYPEWRITER;
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_F) {
      int v = static_cast<int>(fontSize);
      fontSize = static_cast<FontSize>((v + 1) % 3);
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_W) {
      showWordCount = !showWordCount;
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_P) {
      writingMode = (writingMode == WritingMode::PAGINATION) ? WritingMode::NORMAL : WritingMode::PAGINATION;
      screenDirty = true;
      return;
    }
    // Ctrl+Left/Right: jump pages in pagination mode
    if (writingMode == WritingMode::PAGINATION) {
      int pageSize = editorGetStoredVisibleLines();
      if (keyCode == HID_KEY_LEFT) {
        for (int i = 0; i < pageSize; i++) editorMoveCursorUp();
        screenDirty = true;
        return;
      }
      if (keyCode == HID_KEY_RIGHT) {
        for (int i = 0; i < pageSize; i++) editorMoveCursorDown();
        screenDirty = true;
        return;
      }
    }
    return;
  }

  // ESC = save and return to file browser
  if (keyCode == HID_KEY_ESCAPE) {
    if (editorHasUnsavedChanges()) saveCurrentFile();
    if (editorHasUnsavedChanges()) { screenDirty = true; return; }
    currentState = UIState::FILE_BROWSER;
    screenDirty = true;
    return;
  }

  // Tab cycles writing modes
  if (keyCode == HID_KEY_TAB) {
    int v = static_cast<int>(writingMode);
    writingMode = static_cast<WritingMode>((v + 1) % 3);
    screenDirty = true;
    return;
  }

  // Navigation keys
  switch (keyCode) {
    case HID_KEY_LEFT:      editorMoveCursorLeft();  screenDirty = true; return;
    case HID_KEY_RIGHT:     editorMoveCursorRight(); screenDirty = true; return;
    case HID_KEY_UP:        editorMoveCursorUp();    screenDirty = true; return;
    case HID_KEY_DOWN:      editorMoveCursorDown();  screenDirty = true; return;
    case HID_KEY_HOME:      editorMoveCursorHome();  screenDirty = true; return;
    case HID_KEY_END:       editorMoveCursorEnd();   screenDirty = true; return;
    case HID_KEY_BACKSPACE:
      if (pendingDeadKey) {
        pendingDeadKey = 0;
        screenDirty = true;
      } else {
        editorDeleteChar();
        screenDirty = true;
      }
      return;
    case HID_KEY_DELETE:    editorDeleteForward();   screenDirty = true; return;
  }

  // CapsLock toggle
  if (keyCode == HID_KEY_CAPSLOCK) {
    capsLockOn = !capsLockOn;
    return;
  }

  // Printable character — US International dead-key composition.
  char c = hidToAscii(keyCode, modifiers);
  if (c != 0) {
    handleEditorPrintable(c);
  }
}

// Open the title edit screen, returning to `returnTo` on confirm/cancel
static void openTitleEdit(const char* currentTitle, UIState returnTo) {
  strncpy(renameBuffer, currentTitle, MAX_TITLE_LEN - 1);
  renameBuffer[MAX_TITLE_LEN - 1] = '\0';
  renameBufferLen = strlen(renameBuffer);
  renameReturnState = returnTo;
  currentState = UIState::RENAME_FILE;
  screenDirty = true;
}

// Handle title edit input
static void handleRenameKey(uint8_t keyCode, uint8_t modifiers) {
  if (keyCode == HID_KEY_ENTER) {
    if (renameBufferLen > 0) {
      if (renameReturnState == UIState::TEXT_EDITOR) {
        editorSetCurrentTitle(renameBuffer);
        if (editorGetCurrentFile()[0] == '\0') {
          // New file — derive filename from title
          char filename[MAX_FILENAME_LEN];
          deriveUniqueFilename(renameBuffer, filename, MAX_FILENAME_LEN);
          if (!filename[0]) return; // No free name: never overwrite another note.
          editorSetCurrentFile(filename);
        } else {
          // Existing file — rename on disk to match new title
          updateFileTitle(editorGetCurrentFile(), renameBuffer);
        }
        editorSetUnsavedChanges(true);
        saveCurrentFile();
      } else {
        // Updating title of a file selected in the browser
        FileInfo* files = getFileList();
        updateFileTitle(files[selectedFileIndex].filename, renameBuffer);
      }
    }
    currentState = renameReturnState;
    screenDirty = true;
    return;
  }

  if (keyCode == HID_KEY_ESCAPE) {
    currentState = renameReturnState;
    screenDirty = true;
    return;
  }

  if (keyCode == HID_KEY_BACKSPACE) {
    if (pendingDeadKey) {
      pendingDeadKey = 0;
      screenDirty = true;
      return;
    }

    if (renameBufferLen > 0) {
      renameBufferLen--;
      while (renameBufferLen > 0 &&
             (((unsigned char)renameBuffer[renameBufferLen] & 0xC0) == 0x80)) {
        renameBufferLen--;
      }
      renameBuffer[renameBufferLen] = '\0';
      screenDirty = true;
    }
    return;
  }

  // Allow printable characters in a title, including US International
  // dead-key composition.
  char c = hidToAscii(keyCode, modifiers);
  if (c != 0 && c >= ' ') {
    handleRenamePrintable(c);
  }
}

static void dispatchEvent(const KeyEvent& event) {
  if (!event.pressed) return;

  switch (currentState) {
    case UIState::MAIN_MENU: {
      int menuCount = 4 + otaAppCount;
      if (event.keyCode == HID_KEY_DOWN) {
        mainMenuSelection = (mainMenuSelection + 1) % menuCount;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_UP) {
        mainMenuSelection = (mainMenuSelection - 1 + menuCount) % menuCount;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_ENTER) {
        if (mainMenuSelection == 0) {
          refreshFileList();
          currentState = UIState::FILE_BROWSER;
          screenDirty = true;
        } else if (mainMenuSelection == 1) {
          createNewFile();
          openTitleEdit("Untitled", UIState::TEXT_EDITOR);
        } else if (mainMenuSelection == 2) {
          currentState = UIState::SETTINGS;
          screenDirty = true;
        } else if (mainMenuSelection == 3) {
          wifiSyncStart();
          currentState = UIState::WIFI_SYNC;
          screenDirty = true;
        } else if (mainMenuSelection >= 4) {
          switchToOtaApp(mainMenuSelection - 4);
        }
      }
      break;
    }

    case UIState::FILE_BROWSER: {
      int fc = getFileCount();

      // Delete confirmation pending — stay here until Enter or Esc.
      // Ignore stray keyboard events so they cannot silently cancel deletion.
      if (deleteConfirmPending) {
        if (event.keyCode == HID_KEY_ENTER && fc > 0) {
          FileInfo* files = getFileList();
          deleteFile(files[selectedFileIndex].filename);
          int newFc = getFileCount();
          if (selectedFileIndex >= newFc) selectedFileIndex = newFc - 1;
          if (selectedFileIndex < 0) selectedFileIndex = 0;
          deleteConfirmPending = false;
          screenDirty = true;
        } else if (event.keyCode == HID_KEY_ESCAPE) {
          deleteConfirmPending = false;
          screenDirty = true;
        }
        break;
      }

      if (event.keyCode == HID_KEY_DOWN && fc > 0) {
        selectedFileIndex = (selectedFileIndex + 1) % fc;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_UP && fc > 0) {
        selectedFileIndex = (selectedFileIndex - 1 + fc) % fc;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_ENTER && fc > 0) {
        FileInfo* files = getFileList();
        loadFile(files[selectedFileIndex].filename);
        screenDirty = true;
      } else if (isCtrl(event.modifiers) && event.keyCode == HID_KEY_N) {
        if (fc > 0) {
          FileInfo* files = getFileList();
          openTitleEdit(files[selectedFileIndex].title, UIState::FILE_BROWSER);
        }
      } else if (isCtrl(event.modifiers) && event.keyCode == HID_KEY_D) {
        if (fc > 0) {
          deleteConfirmPending = true;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_ESCAPE) {
        currentState = UIState::MAIN_MENU;
        screenDirty = true;
      }
      break;
    }

    case UIState::TEXT_EDITOR:
      handleEditorKey(event.keyCode, event.modifiers);
      break;

    case UIState::RENAME_FILE:
      handleRenameKey(event.keyCode, event.modifiers);
      break;

    case UIState::SETTINGS: {
      const int SETTINGS_COUNT = 6;  // Orientation, Dark Mode, Writing Mode, Font Size, Bluetooth, Paired Keyboards

      // Up/Down: navigate settings list (physical buttons also map here)
      if (event.keyCode == HID_KEY_DOWN) {
        settingsSelection = (settingsSelection + 1) % SETTINGS_COUNT;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_UP) {
        settingsSelection = (settingsSelection - 1 + SETTINGS_COUNT) % SETTINGS_COUNT;
        screenDirty = true;

      // Enter or Right: cycle setting forward
      } else if (event.keyCode == HID_KEY_ENTER || event.keyCode == HID_KEY_RIGHT) {
        if (settingsSelection == 0) {
          int v = static_cast<int>(currentOrientation);
          currentOrientation = static_cast<Orientation>((v + 1) % 4);
        } else if (settingsSelection == 1) {
          darkMode = !darkMode;
        } else if (settingsSelection == 2) {
          int v = static_cast<int>(writingMode);
          writingMode = static_cast<WritingMode>((v + 1) % 3);
        } else if (settingsSelection == 3) {
          int v = static_cast<int>(fontSize);
          fontSize = static_cast<FontSize>((v + 1) % 3);
        } else if (settingsSelection == 4) {
          currentState = UIState::BLUETOOTH_SETTINGS;
        } else if (settingsSelection == 5) {
          pairedKeyboardSelection = 0;
          currentState = UIState::PAIRED_KEYBOARDS;
        }
        screenDirty = true;

      // Left: cycle setting backward (keyboard only — physical L/R map to Up/Down)
      } else if (event.keyCode == HID_KEY_LEFT) {
        if (settingsSelection == 0) {
          int v = static_cast<int>(currentOrientation);
          currentOrientation = static_cast<Orientation>((v - 1 + 4) % 4);
        } else if (settingsSelection == 1) {
          darkMode = !darkMode;
        } else if (settingsSelection == 2) {
          int v = static_cast<int>(writingMode);
          writingMode = static_cast<WritingMode>((v - 1 + 3) % 3);
        } else if (settingsSelection == 3) {
          int v = static_cast<int>(fontSize);
          fontSize = static_cast<FontSize>((v - 1 + 3) % 3);
        }
        screenDirty = true;

      } else if (event.keyCode == HID_KEY_ESCAPE) {
        currentState = UIState::MAIN_MENU;
        screenDirty = true;
      }
      break;
    }

    case UIState::BLUETOOTH_SETTINGS: {
      int deviceCount = getDiscoveredDeviceCount();

      // Ensure selection is within bounds
      if (bluetoothDeviceSelection >= deviceCount && deviceCount > 0) {
        bluetoothDeviceSelection = deviceCount - 1;
      } else if (deviceCount == 0) {
        bluetoothDeviceSelection = 0; // Reset to 0 when no devices
      }

      if (event.keyCode == HID_KEY_ESCAPE) {
        DBG_PRINTLN("[INPUT] BT: Escape pressed - returning to settings");
        currentState = UIState::SETTINGS;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_DOWN) {
        if (deviceCount > 0) {
          bluetoothDeviceSelection = (bluetoothDeviceSelection + 1) % deviceCount;
          DBG_PRINTF("[INPUT] BT: Down pressed - selection now %d/%d\n", bluetoothDeviceSelection, deviceCount);
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_UP) {
        if (deviceCount > 0) {
          bluetoothDeviceSelection = (bluetoothDeviceSelection - 1 + deviceCount) % deviceCount;
          DBG_PRINTF("[INPUT] BT: Up pressed - selection now %d/%d\n", bluetoothDeviceSelection, deviceCount);
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_ENTER) {
        if (deviceCount > 0 && !isDeviceScanning()) {
          // Connect to the selected device
          connectToDevice(bluetoothDeviceSelection);
        } else if (!isDeviceScanning()) {
          // No devices — start a new scan
          startDeviceScan();
        }
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_RIGHT) {
        // Right button = re-scan for devices
        if (!isDeviceScanning()) {
          startDeviceScan();
        }
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_LEFT) {
        if (isKeyboardConnected()) {
          disconnectCurrentDevice();
          screenDirty = true;
        }
      }
      break;
    }

    case UIState::PAIRED_KEYBOARDS: {
      int count = getPairedKeyboardCount();

      if (event.keyCode == HID_KEY_ESCAPE) {
        currentState = UIState::SETTINGS;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_DOWN) {
        if (count > 0) {
          pairedKeyboardSelection = (pairedKeyboardSelection + 1) % count;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_UP) {
        if (count > 0) {
          pairedKeyboardSelection = (pairedKeyboardSelection - 1 + count) % count;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_ENTER) {
        if (count > 0) {
          connectToPairedKeyboard(pairedKeyboardSelection);
          currentState = UIState::SETTINGS;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_D) {
        if (count > 0) {
          removePairedKeyboard(pairedKeyboardSelection);
          int newCount = getPairedKeyboardCount();
          if (pairedKeyboardSelection >= newCount && newCount > 0)
            pairedKeyboardSelection = newCount - 1;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_LEFT) {
        // Disconnect if this keyboard is the currently connected one
        if (count > 0 && isKeyboardConnected()) {
          std::string addr, name; uint8_t addrType;
          getPairedKeyboard(pairedKeyboardSelection, addr, name, addrType);
          if (getCurrentDeviceAddress() == addr) {
            disconnectCurrentDevice();
            screenDirty = true;
          }
        }
      }
      break;
    }

    case UIState::WIFI_SYNC: {
      syncHandleKey(event.keyCode, event.modifiers);
      break;
    }

    default:
      break;
  }
}

int processAllInput() {
  int processedCount = 0;
  while (!isQueueEmpty()) {
    KeyEvent event = dequeueKeyEvent();
    dispatchEvent(event);
    processedCount++;
  }
  return processedCount;
}
