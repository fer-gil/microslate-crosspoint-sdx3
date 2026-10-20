#include "../src/input_handler.cpp"
#include <cassert>
#include <iostream>

bool screenDirty=false;
char renameBuffer[MAX_FILENAME_LEN]={0};
int renameBufferLen=0;
static void begin() { editorInit(); editorSetCurrentFile("test.md"); pendingDeadKey=0; }
int main() {
  begin(); handleEditorPrintable('\''); handleEditorPrintable('a');
  assert(std::string(editorGetBuffer())=="á");
  handleEditorPrintable('~'); handleEditorPrintable('n');
  assert(std::string(editorGetBuffer())=="áñ");
  begin();
  for(char c:std::string("[^a]: nota")) handleEditorPrintable(c);
  assert(std::string(editorGetBuffer())=="[^a]: nota");
  begin(); handleEditorPrintable('^'); handleEditorPrintable('a');
  assert(std::string(editorGetBuffer())=="â");
  begin(); handleEditorPrintable('\''); handleEditorPrintable(' ');
  assert(std::string(editorGetBuffer())=="'");
  handleRenamePrintable('~'); handleRenamePrintable('n');
  assert(std::string(renameBuffer)=="ñ");
  assert(hidToAscii(0x1e,MOD_SHIFT_LEFT)=='!');
  std::cout<<"Installed international keyboard composition and literal Pandoc identifiers passed.\n";
}
