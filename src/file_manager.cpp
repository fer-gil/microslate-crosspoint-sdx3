#include "file_manager.h"
#include "text_editor.h"
#include "note_format.h"
#include <Arduino.h>
#include <SDCardManager.h>
#include <cstring>
#include <algorithm>

// --- File list ---
static FileInfo fileList[MAX_FILES];
static int fileCount = 0;
static bool saveFailed = false;
bool fileManagerSaveFailed() { return saveFailed; }

// Shared state
extern UIState currentState;

// Convert filename to a readable display title.
// "my_note_2.txt" -> "My Note 2"
static void filenameToTitle(const char* filename, char* out, int maxLen) {
  int j = 0;
  bool capitalizeNext = true;
  for (int i = 0; filename[i] != '\0' && filename[i] != '.' && j < maxLen - 1; i++) {
    char c = filename[i];
    if (c == '_') {
      if (j > 0) out[j++] = ' ';
      capitalizeNext = true;
    } else {
      if (capitalizeNext && c >= 'a' && c <= 'z') c -= 32;
      capitalizeNext = false;
      out[j++] = c;
    }
  }
  out[j] = '\0';
  if (j == 0) strncpy(out, "Untitled", maxLen - 1);
}

// Convert a title to a valid FAT filename (lowercase, spaces->underscores,
// non-alphanumeric stripped, requested extension appended).
static void titleToFilename(const char* title, char* out, int maxLen, const char* extension) {
  int maxBase = maxLen - static_cast<int>(strlen(extension)) - 1; // room for extension + null
  int j = 0;
  for (int i = 0; title[i] != '\0' && j < maxBase; i++) {
    char c = title[i];
    if (c >= 'A' && c <= 'Z') c += 32;
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out[j++] = c;
    } else if (c == ' ' || c == '_' || c == '-') {
      if (j > 0 && out[j - 1] != '_') out[j++] = '_';
    }
  }
  while (j > 0 && out[j - 1] == '_') j--;
  if (j == 0) { strncpy(out, "note", maxLen - 1); j = 4; }
  strcpy(out + j, extension);
}

// Derive a unique /notes/ filename from a title, handling collisions with _2, _3 suffix.
void deriveUniqueFilename(const char* title, char* out, int maxLen, const char* extension, const char* existing) {
  titleToFilename(title, out, maxLen, extension);
  char base[MAX_FILENAME_LEN];
  strncpy(base, out, sizeof(base)-1); base[sizeof(base)-1]='\0';
  base[strlen(base)-strlen(extension)]='\0';
  for (int suffix=1; suffix<=9999; ++suffix) {
    if (suffix>1) snprintf(out,maxLen,"%.*s_%d%s",maxLen-static_cast<int>(strlen(extension))-7,base,suffix,extension);
    if (existing && strcasecmp(existing,out)==0) return;
    char path[320]; snprintf(path,sizeof(path),"/notes/%s",out);
    if (!SdMan.exists(path)) return;
  }
  out[0]='\0';
}

void fileManagerSetup() {
  if (!SdMan.begin()) {
    DBG_PRINTLN("SD Card mount failed!");
    return;
  }

  if (!SdMan.exists("/notes")) {
    SdMan.mkdir("/notes");
  }

  // If power failed between original -> .bak and .tmp -> original, restore
  // the previous committed note. Uncommitted .tmp files remain for manual recovery.
  auto recovery = SdMan.open("/notes");
  if (recovery && recovery.isDirectory()) {
    char name[256];
    for (auto file=recovery.openNextFile(); file; file=recovery.openNextFile()) {
      file.getName(name,sizeof(name));
      bool regular = !file.isDirectory(); file.close();
      size_t len=strlen(name);
      if (!regular || len<=4 || strcmp(name+len-4,".bak")!=0) continue;
      name[len-4]='\0';
      if (!isNoteFile(name)) continue;
      char original[320], backup[336];
      snprintf(original,sizeof(original),"/notes/%s",name);
      snprintf(backup,sizeof(backup),"%s.bak",original);
      if (!SdMan.exists(original)) SdMan.rename(backup,original);
    }
    recovery.close();
  }
  DBG_PRINTLN("SD Card initialized");
  refreshFileList();
}

void refreshFileList() {
  fileCount = 0;

  auto root = SdMan.open("/notes");
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  root.rewindDirectory();
  char name[256];

  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if (name[0] == '.' || fileCount >= MAX_FILES) {
      file.close();
      if (fileCount >= MAX_FILES) break;
      continue;
    }

    if (!file.isDirectory() && isNoteFile(name) && strlen(name) < MAX_FILENAME_LEN) {
      strncpy(fileList[fileCount].filename, name, MAX_FILENAME_LEN - 1);
      fileList[fileCount].filename[MAX_FILENAME_LEN - 1] = '\0';

      filenameToTitle(name, fileList[fileCount].title, MAX_TITLE_LEN);
      fileList[fileCount].modTime = 0;
      fileCount++;
    }
    file.close();
  }
  root.close();
  SdMan.sleep();

  DBG_PRINTF("File listing: %d files found\n", fileCount);
}

int getFileCount() { return fileCount; }
FileInfo* getFileList() { return fileList; }

void loadFile(const char* filename) {
  char path[320];
  snprintf(path, sizeof(path), "/notes/%s", filename);

  auto file = SdMan.open(path, O_RDONLY);
  if (!file) {
    DBG_PRINTF("Could not open: %s\n", path);
    return;
  }

  if (file.size() >= TEXT_BUFFER_SIZE) {
    DBG_PRINTLN("Note too large: refused to open to avoid truncation");
    file.close(); SdMan.sleep(); return;
  }
  size_t expected = file.size();
  char* buf = editorGetBuffer();
  int readResult = file.read(buf, TEXT_BUFFER_SIZE - 1);
  size_t bytesRead = (readResult > 0) ? (size_t)readResult : 0;
  if (readResult < 0 || bytesRead != expected) { file.close(); SdMan.sleep(); return; }
  buf[bytesRead] = '\0';
  file.close();

  editorSetCurrentFile(filename);
  editorLoadBuffer(bytesRead);

  // Title comes from the filename, not the file content
  char title[MAX_TITLE_LEN];
  filenameToTitle(filename, title, MAX_TITLE_LEN);
  editorSetCurrentTitle(title);
  editorSetUnsavedChanges(false);

  currentState = UIState::TEXT_EDITOR;
  SdMan.sleep();
  DBG_PRINTF("Loaded: %s (%d bytes)\n", filename, (int)bytesRead);
}

void saveCurrentFile(bool refreshList) {
  saveFailed = true;
  const char* filename = editorGetCurrentFile();
  if (filename[0] == '\0') return;

  char path[320], tmpPath[336], bakPath[336];
  snprintf(path, sizeof(path), "/notes/%s", filename);
  snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
  snprintf(bakPath, sizeof(bakPath), "%s.bak", path);

  // Step 1: Write new content to .tmp
  auto file = SdMan.open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    DBG_PRINTF("saveCurrentFile: could not create tmp: %s\n", tmpPath);
    return;
  }

  size_t toWrite = editorGetLength();
  size_t written = file.write((const uint8_t*)editorGetBuffer(), toWrite);
  file.close();

  // Step 2: Verify bytes written match expected length
  if (written != toWrite) {
    DBG_PRINTF("saveCurrentFile: write mismatch (%d/%d) — aborting\n", (int)written, (int)toWrite);
    SdMan.remove(tmpPath);
    return;
  }

  // Read back before rotating the previous version. A short or corrupt write
  // must leave both the original and the dirty editor buffer intact.
  auto check = SdMan.open(tmpPath, O_RDONLY);
  bool verified = check && check.size() == toWrite;
  uint8_t chunk[128]; size_t offset = 0;
  while (verified && offset < toWrite) {
    size_t count = std::min(sizeof(chunk), toWrite-offset);
    int got = check.read(chunk, count);
    verified = got == static_cast<int>(count) && memcmp(chunk, editorGetBuffer()+offset, count)==0;
    offset += count;
  }
  if (check) check.close();
  if (!verified) { SdMan.remove(tmpPath); SdMan.sleep(); return; }
  bool hadOriginal = SdMan.exists(path);
  if (hadOriginal) {
    if (SdMan.exists(bakPath) && !SdMan.remove(bakPath)) { SdMan.sleep(); return; }
    if (!SdMan.rename(path, bakPath)) { SdMan.sleep(); return; }
  }
  if (!SdMan.rename(tmpPath, path)) {
    if (hadOriginal) SdMan.rename(bakPath, path);
    SdMan.sleep(); return;
  }

  saveFailed = false;
  editorSetUnsavedChanges(false);
  if (refreshList) refreshFileList();
  SdMan.sleep();
  DBG_PRINTF("Saved: %s\n", filename);
}

void createNewFile() {
  editorClear();
  editorSetCurrentFile("");       // filename derived from title when user confirms
  editorSetCurrentTitle("Untitled");
  editorSetUnsavedChanges(true);
}

// Rename a file on disk to match a new title, updating editor state if needed.
void updateFileTitle(const char* filename, const char* newTitle) {
  char newFilename[MAX_FILENAME_LEN];
  deriveUniqueFilename(newTitle, newFilename, MAX_FILENAME_LEN, isLegacyNote(filename) ? ".txt" : ".md", filename);
  if (!newFilename[0]) return;

  if (strcmp(newFilename, filename) != 0) {
    char oldPath[320], newPath[320];
    snprintf(oldPath, sizeof(oldPath), "/notes/%s", filename);
    snprintf(newPath, sizeof(newPath), "/notes/%s", newFilename);
    if (!SdMan.rename(oldPath, newPath)) { SdMan.sleep(); return; }

    if (strcmp(editorGetCurrentFile(), filename) == 0) {
      editorSetCurrentFile(newFilename);
    }
  }

  refreshFileList();
  SdMan.sleep();
}

void deleteFile(const char* filename) {
  char path[320], bakPath[336];
  snprintf(path, sizeof(path), "/notes/%s", filename);
  snprintf(bakPath, sizeof(bakPath), "%s.bak", path);
  SdMan.remove(path);
  SdMan.remove(bakPath);
  refreshFileList();
  SdMan.sleep();
  DBG_PRINTF("Deleted: %s\n", filename);
}
