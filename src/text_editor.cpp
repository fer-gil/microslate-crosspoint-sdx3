#include "text_editor.h"
#include <cstring>
#include <algorithm>

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

// Forward declaration
static void ensureCursorVisible(int visibleLines);

// UTF-8 helpers. Cursor/line positions remain byte offsets, but movement and
// columns operate on complete Unicode codepoints.
static bool isUtf8Continuation(unsigned char c) {
  return (c & 0xC0) == 0x80;
}

static int previousCodepointStart(int pos) {
  if (pos <= 0) return 0;
  int p = pos - 1;
  while (p > 0 && isUtf8Continuation((unsigned char)textBuffer[p])) p--;
  return p;
}

static int nextCodepointEnd(int pos) {
  if (pos >= (int)textLength) return (int)textLength;
  int p = pos + 1;
  while (p < (int)textLength &&
         isUtf8Continuation((unsigned char)textBuffer[p])) {
    p++;
  }
  return p;
}

static int countCodepoints(int start, int end) {
  int count = 0;
  for (int i = start; i < end; i++) {
    if (!isUtf8Continuation((unsigned char)textBuffer[i])) count++;
  }
  return count;
}

static int byteOffsetForColumn(int start, int end, int column) {
  int pos = start;
  int col = 0;
  while (pos < end && col < column) {
    pos = nextCodepointEnd(pos);
    col++;
  }
  return pos > end ? end : pos;
}

// Recalculate line breaks (word wrap) and cursor position.
// The O(textLength) line break loop only runs when the buffer or charsPerLine changed.
// Cursor line/col is always recomputed (cheap O(cursorLine) with early exit).
void editorRecalculateLines() {
  if (lineBreaksDirty) {
    lineCount = 0;
    linePositions[0] = 0;
    lineCount = 1;

    int col = 0;
    int lastSpace = -1;

    for (int i = 0; i < (int)textLength && lineCount < MAX_LINES; i++) {
      // Continuation bytes belong to the codepoint already counted.
      if (isUtf8Continuation((unsigned char)textBuffer[i])) continue;

      if (textBuffer[i] == '\n') {
        // Hard line break
        if (lineCount < MAX_LINES) {
          linePositions[lineCount] = i + 1;
          lineCount++;
        }
        col = 0;
        lastSpace = -1;
        continue;
      }

      if (textBuffer[i] == ' ') {
        lastSpace = i;
      }

      col++;
      if (col >= charsPerLine) {
        // Word wrap, always at a complete UTF-8 codepoint boundary.
        int cpEnd = nextCodepointEnd(i);
        int breakPos;
        if (lastSpace > linePositions[lineCount - 1]) {
          breakPos = lastSpace + 1; // Break after space
        } else {
          breakPos = cpEnd;         // Hard break after this codepoint
        }

        if (lineCount < MAX_LINES) {
          linePositions[lineCount] = breakPos;
          lineCount++;
        }
        col = countCodepoints(breakPos, cpEnd);
        lastSpace = -1;
      }
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
  cursorCol = countCodepoints(linePositions[cursorLine], cursorPosition);
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
  editorRecalculateLines();
}

void editorClear() {
  memset(textBuffer, 0, TEXT_BUFFER_SIZE);
  textLength = 0;
  cursorPosition = 0;
  unsavedChanges = false;
  viewportStartLine = 0;
  lineBreaksDirty = true;
  editorRecalculateLines();
}

void editorLoadBuffer(size_t length) {
  textLength = length;
  textBuffer[textLength] = '\0';
  cursorPosition = (int)textLength;  // Start at end
  viewportStartLine = 0;
  lineBreaksDirty = true;
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

void editorInsertText(const char* text) {
  if (!text || !text[0]) return;

  size_t insertLen = strlen(text);
  if (textLength + insertLen >= TEXT_BUFFER_SIZE) return;

  memmove(textBuffer + cursorPosition + insertLen,
          textBuffer + cursorPosition,
          textLength - cursorPosition + 1);
  memcpy(textBuffer + cursorPosition, text, insertLen);

  cursorPosition += (int)insertLen;
  textLength += insertLen;
  unsavedChanges = true;
  lineBreaksDirty = true;

  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorInsertChar(char c) {
  char text[2] = {c, '\0'};
  editorInsertText(text);
}

void editorDeleteChar() {
  if (cursorPosition <= 0 || textLength == 0) return;

  int start = previousCodepointStart(cursorPosition);
  int removeLen = cursorPosition - start;

  memmove(textBuffer + start,
          textBuffer + cursorPosition,
          textLength - cursorPosition + 1);

  cursorPosition = start;
  textLength -= removeLen;
  unsavedChanges = true;
  lineBreaksDirty = true;

  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorDeleteForward() {
  if (cursorPosition >= (int)textLength) return;

  int end = nextCodepointEnd(cursorPosition);
  int removeLen = end - cursorPosition;

  memmove(textBuffer + cursorPosition,
          textBuffer + end,
          textLength - end + 1);

  textLength -= removeLen;
  unsavedChanges = true;
  lineBreaksDirty = true;

  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorMoveCursorLeft() {
  if (cursorPosition > 0) {
    cursorPosition = previousCodepointStart(cursorPosition);
    editorRecalculateLines();
    ensureCursorVisible(storedVisibleLines);
  }
}

void editorMoveCursorRight() {
  if (cursorPosition < (int)textLength) {
    cursorPosition = nextCodepointEnd(cursorPosition);
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
  // Don't count trailing newline.
  if (lineEnd > lineStart && textBuffer[lineEnd - 1] == '\n') lineEnd--;

  cursorPosition = byteOffsetForColumn(lineStart, lineEnd, cursorCol);
  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorMoveCursorDown() {
  if (cursorLine >= lineCount - 1) return;

  int targetLine = cursorLine + 1;
  int lineStart = linePositions[targetLine];
  int lineEnd = (targetLine + 1 < lineCount) ? linePositions[targetLine + 1] : (int)textLength;
  if (lineEnd > lineStart && textBuffer[lineEnd - 1] == '\n') lineEnd--;

  cursorPosition = byteOffsetForColumn(lineStart, lineEnd, cursorCol);
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
    charsPerLine = cpl;
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
}

void editorSetCurrentTitle(const char* title) {
  strncpy(currentTitle, title, MAX_TITLE_LEN - 1);
  currentTitle[MAX_TITLE_LEN - 1] = '\0';
}

const char* editorGetCurrentFile() { return currentFile; }
const char* editorGetCurrentTitle() { return currentTitle; }
bool editorHasUnsavedChanges() { return unsavedChanges; }
void editorSetUnsavedChanges(bool v) { unsavedChanges = v; }
