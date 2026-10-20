#include "text_editor.h"
#include <cstring>
#include <algorithm>
#include "markdown.h"
#include "note_format.h"

// --- Text buffer ---
static char textBuffer[TEXT_BUFFER_SIZE];
static size_t textLength = 0;
static int cursorPosition = 0;

// --- File metadata ---
static char currentFile[MAX_FILENAME_LEN] = "";
static char currentTitle[MAX_TITLE_LEN] = "Untitled";
static bool unsavedChanges = false;

// --- Line management ---
static int linePositions[MAX_LINES];  // Index into textBuffer for start of each line
static int lineCount = 0;
static int cursorLine = 0;
static int cursorCol = 0;
static int viewportStartLine = 0;
static int charsPerLine = 40;
static int storedVisibleLines = 20;  // Updated by renderer each frame
static bool lineBreaksDirty = true;  // Only recompute line breaks when buffer/charsPerLine changes

static uint8_t markdownFlags[TEXT_BUFFER_SIZE];
static bool parseDirty = true;
static bool visualMode = true;
static int layoutWidth = 0;
static int layoutEpoch = -1;
static EditorMeasure measureGlyph = nullptr;
static markdown::Range activeRange{0, 0};

// Forward declaration
static void ensureCursorVisible(int visibleLines);

// Recalculate line breaks (word wrap) and cursor position.
// The O(textLength) line break loop only runs when the buffer or charsPerLine changed.
// Cursor line/col is always recomputed (cheap O(cursorLine) with early exit).
void editorRecalculateLines() {
  if (parseDirty) {
    markdown::parse(textBuffer, textLength, markdownFlags);
    parseDirty = false;
    lineBreaksDirty = true;
  }
  auto active = markdown::paragraph(textBuffer, textLength, cursorPosition);
  if (active.begin != activeRange.begin || active.end != activeRange.end) {
    activeRange = active;
    lineBreaksDirty = true;
  }
  if (lineBreaksDirty) {
    lineCount = 1;
    linePositions[0] = 0;
    int width = 0;
    int lastSpace = -1;
    for (size_t i = 0; i < textLength;) {
      if (textBuffer[i] == '\n') {
        if (lineCount < MAX_LINES) linePositions[lineCount++] = i + 1;
        width = 0; lastSpace = -1; ++i; continue;
      }
      char glyph[5]; uint8_t style;
      size_t end = editorGetGlyph(i, glyph, style);
      int w = glyph[0] ? (measureGlyph ? measureGlyph(glyph, style) : 1) : 0;
      int limit = layoutWidth > 0 ? layoutWidth : charsPerLine;
      if (width + w > limit && width > 0 && lineCount < MAX_LINES) {
        int split = lastSpace > linePositions[lineCount-1] ? lastSpace : static_cast<int>(i);
        linePositions[lineCount++] = split;
        i = split; width = 0; lastSpace = -1;
        continue;
      }
      width += w;
      if (glyph[0] == ' ') lastSpace = end;
      i = end;
    }
    lineBreaksDirty = false;
  }

  // Compute cursor line and column (always — cheap O(cursorLine) with early exit)
  cursorLine = 0;
  for (int i = 1; i < lineCount; i++) {
    if (cursorPosition >= linePositions[i]) {
      cursorLine = i;
    } else {
      break;
    }
  }
  cursorCol = 0;
  for (size_t p = linePositions[cursorLine]; p < static_cast<size_t>(cursorPosition); p = markdown::next(textBuffer, textLength, p)) ++cursorCol;
}

// Ensure cursor is visible by adjusting viewport
static void ensureCursorVisible(int visibleLines) {
  if (visibleLines <= 0) visibleLines = 20; // fallback

  if (cursorLine < viewportStartLine) {
    viewportStartLine = cursorLine;
  } else if (cursorLine >= viewportStartLine + visibleLines) {
    viewportStartLine = cursorLine - visibleLines + 1;
  }

  if (viewportStartLine < 0) viewportStartLine = 0;
  if (viewportStartLine >= lineCount) viewportStartLine = std::max(0, lineCount - 1);
}

void editorInit() {
  memset(textBuffer, 0, TEXT_BUFFER_SIZE);
  textLength = 0;
  cursorPosition = 0;
  currentFile[0] = '\0';
  strncpy(currentTitle, "Untitled", MAX_TITLE_LEN - 1);
  unsavedChanges = false;
  viewportStartLine = 0;
  lineBreaksDirty = true;
  parseDirty = true;
  editorRecalculateLines();
}

void editorClear() {
  memset(textBuffer, 0, TEXT_BUFFER_SIZE);
  textLength = 0;
  cursorPosition = 0;
  unsavedChanges = false;
  viewportStartLine = 0;
  lineBreaksDirty = true;
  parseDirty = true;
  editorRecalculateLines();
}

void editorLoadBuffer(size_t length) {
  textLength = std::min(length, TEXT_BUFFER_SIZE - 1);
  textBuffer[textLength] = '\0';
  cursorPosition = (int)textLength;  // Start at end
  viewportStartLine = 0;
  lineBreaksDirty = true;
  parseDirty = true;
  editorRecalculateLines();
  // Scroll to show cursor
  ensureCursorVisible(storedVisibleLines);
}

char* editorGetBuffer() { return textBuffer; }
size_t editorGetLength() { return textLength; }
int editorGetCursorPosition() { return cursorPosition; }

int editorGetWordCount() {
  int count = 0;
  bool inWord = false;
  for (size_t i = 0; i < textLength; i++) {
    char c = textBuffer[i];
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
      inWord = false;
    } else {
      if (!inWord) { count++; inWord = true; }
    }
  }
  return count;
}

void editorInsertChar(char c) {
  if (textLength >= TEXT_BUFFER_SIZE - 1) return;

  // Shift text right
  for (int i = (int)textLength; i > cursorPosition; i--) {
    textBuffer[i] = textBuffer[i - 1];
  }
  textBuffer[cursorPosition] = c;
  cursorPosition++;
  textLength++;
  textBuffer[textLength] = '\0';
  unsavedChanges = true;
  lineBreaksDirty = true;
  parseDirty = true;

  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorDeleteChar() {
  if (cursorPosition <= 0 || textLength == 0) return;

  int start = markdown::previous(textBuffer, cursorPosition);
  int count = cursorPosition - start;
  memmove(textBuffer + start, textBuffer + cursorPosition, textLength - cursorPosition);
  cursorPosition = start;
  textLength -= count;
  textBuffer[textLength] = '\0';
  unsavedChanges = true;
  lineBreaksDirty = true;
  parseDirty = true;

  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorDeleteForward() {
  if (cursorPosition >= (int)textLength) return;

  size_t end = markdown::next(textBuffer, textLength, cursorPosition);
  memmove(textBuffer + cursorPosition, textBuffer + end, textLength - end);
  textLength -= end - cursorPosition;
  textBuffer[textLength] = '\0';
  unsavedChanges = true;
  lineBreaksDirty = true;
  parseDirty = true;

  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorMoveCursorLeft() {
  if (cursorPosition > 0) {
    cursorPosition = markdown::previous(textBuffer, cursorPosition);
    editorRecalculateLines();
    ensureCursorVisible(storedVisibleLines);
  }
}

void editorMoveCursorRight() {
  if (cursorPosition < (int)textLength) {
    cursorPosition = markdown::next(textBuffer, textLength, cursorPosition);
    editorRecalculateLines();
    ensureCursorVisible(storedVisibleLines);
  }
}

void editorMoveCursorUp() {
  // cursorLine/cursorCol are already valid from the previous operation
  if (cursorLine <= 0) return;

  int targetLine = cursorLine - 1;
  int lineStart = linePositions[targetLine];
  int lineEnd = (targetLine + 1 < lineCount) ? linePositions[targetLine + 1] : (int)textLength;
  int lineLen = lineEnd - lineStart;
  // Don't count trailing newline
  if (lineLen > 0 && textBuffer[lineStart + lineLen - 1] == '\n') lineLen--;

  size_t target = lineStart;
  for (int col=0; col<cursorCol && target<static_cast<size_t>(lineStart+lineLen); ++col)
    target = markdown::next(textBuffer, textLength, target);
  cursorPosition = target;
  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorMoveCursorDown() {
  if (cursorLine >= lineCount - 1) return;

  int targetLine = cursorLine + 1;
  int lineStart = linePositions[targetLine];
  int lineEnd = (targetLine + 1 < lineCount) ? linePositions[targetLine + 1] : (int)textLength;
  int lineLen = lineEnd - lineStart;
  if (lineLen > 0 && textBuffer[lineStart + lineLen - 1] == '\n') lineLen--;

  size_t target = lineStart;
  for (int col=0; col<cursorCol && target<static_cast<size_t>(lineStart+lineLen); ++col)
    target = markdown::next(textBuffer, textLength, target);
  cursorPosition = target;
  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorMoveCursorHome() {
  cursorPosition = linePositions[cursorLine];
  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorMoveCursorEnd() {
  int lineEnd;
  if (cursorLine + 1 < lineCount) {
    lineEnd = linePositions[cursorLine + 1];
    // Step back over newline if present
    if (lineEnd > 0 && textBuffer[lineEnd - 1] == '\n') lineEnd--;
  } else {
    lineEnd = (int)textLength;
  }
  cursorPosition = lineEnd;
  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorSetCharsPerLine(int cpl) {
  if (cpl != charsPerLine) {
    charsPerLine = std::max(1, cpl);
    lineBreaksDirty = true;
  }
  editorRecalculateLines();
}

void editorSetVisibleLines(int n) {
  if (n > 0) storedVisibleLines = n;
}

int editorGetStoredVisibleLines() {
  return storedVisibleLines;
}

int editorGetVisibleLines(int lineHeight, int textAreaHeight) {
  if (lineHeight <= 0) return 20;
  return textAreaHeight / lineHeight;
}

int editorGetViewportStart() { return viewportStartLine; }
int editorGetCursorLine() { return cursorLine; }
int editorGetCursorCol() { return cursorCol; }
int editorGetLineCount() { return lineCount; }

int editorGetLinePosition(int lineIndex) {
  if (lineIndex < 0 || lineIndex >= lineCount) return 0;
  return linePositions[lineIndex];
}

void editorSetCurrentFile(const char* filename) {
  strncpy(currentFile, filename, MAX_FILENAME_LEN - 1);
  currentFile[MAX_FILENAME_LEN - 1] = '\0';
  lineBreaksDirty = true;
}

void editorSetCurrentTitle(const char* title) {
  strncpy(currentTitle, title, MAX_TITLE_LEN - 1);
  currentTitle[MAX_TITLE_LEN - 1] = '\0';
}

const char* editorGetCurrentFile() { return currentFile; }
const char* editorGetCurrentTitle() { return currentTitle; }
bool editorHasUnsavedChanges() { return unsavedChanges; }
void editorSetUnsavedChanges(bool v) { unsavedChanges = v; }

void editorSetLayout(int width, EditorMeasure measure, int epoch) {
  if (width != layoutWidth || measure != measureGlyph || epoch != layoutEpoch) {
    layoutWidth = std::max(1, width); measureGlyph = measure; layoutEpoch = epoch;
    lineBreaksDirty = true;
  }
  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}
void editorToggleVisual() {
  visualMode = !visualMode;
  lineBreaksDirty = true;
  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}
bool editorIsVisual() { return visualMode && isMarkdownNote(currentFile); }
size_t editorGetGlyph(size_t offset, char out[5], uint8_t& style) {
  style = 0; out[0] = '\0';
  if (offset >= textLength) return textLength;
  size_t end = markdown::next(textBuffer, textLength, offset);
  if (editorIsVisual() && !(offset >= activeRange.begin && offset < activeRange.end))
    style = markdownFlags[offset];
  if (style & markdown::Hidden) return end;
  if (style & markdown::Bullet) { strcpy(out, "•"); return end; }
  if (textBuffer[offset] == '\t') { strcpy(out, " "); return end; }
  size_t count = std::min<size_t>(4, end-offset);
  memcpy(out, textBuffer+offset, count); out[count] = '\0';
  return end;
}
