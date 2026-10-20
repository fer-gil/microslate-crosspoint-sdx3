#include "../src/input_handler.cpp"
#include <cassert>
#include <iostream>
#include <SDCardManager.h>

bool screenDirty=false;
bool cleanMode=false, deleteConfirmPending=false, showWordCount=true;
WritingMode writingMode=WritingMode::NORMAL;
FontSize fontSize=FontSize::MEDIUM;
UIState currentState=UIState::MAIN_MENU;
int selectedFileIndex=0;
FakeSD SdMan;
char renameBuffer[MAX_FILENAME_LEN]={0};
int renameBufferLen=0;
static void begin() { editorInit(); editorSetCurrentFile("test.md"); pendingDeadKey=0; }
static void noteWorkflows() {
  // Delete the last note through the same file manager used by the menu.
  SdMan=FakeSD{};
  SdMan.files["/notes/last.md"]="old note";
  refreshFileList(); assert(getFileCount()==1);
  deleteFile(getFileList()[0].filename);
  assert(getFileCount()==0);
  editorInit(); currentState=UIState::FILE_BROWSER;
  assert(handleNoteShortcut(HID_KEY_N,MOD_CTRL_LEFT));
  assert(currentState==UIState::RENAME_FILE);
  // Cancelling the title still allows typing, autosave/manual save and exit.
  handleRenameKey(HID_KEY_ESCAPE,0);
  assert(currentState==UIState::TEXT_EDITOR);
  assert(editorGetCurrentFile()[0]=='\0');
  handleEditorPrintable('a');
  handleEditorKey(HID_KEY_S,MOD_CTRL_LEFT);
  assert(!fileManagerSaveFailed() && !editorHasUnsavedChanges());
  assert(SdMan.files["/notes/untitled.md"]=="a");
  handleEditorKey(HID_KEY_ESCAPE,0);
  assert(currentState==UIState::FILE_BROWSER);

  // A blank title confirmed with Enter also gets a default name at save time.
  assert(handleNoteShortcut(HID_KEY_N,MOD_CTRL_RIGHT));
  renameBuffer[0]='\0'; renameBufferLen=0;
  handleRenameKey(HID_KEY_ENTER,0);
  handleEditorPrintable('b'); saveCurrentFile(false);
  assert(!editorHasUnsavedChanges());
  assert(SdMan.files["/notes/untitled_2.md"]=="b");
  assert(SdMan.files["/notes/untitled.md"]=="a");

  // A real storage failure must never trap Escape or silently throw away text.
  handleEditorPrintable('c'); SdMan.failOpen=true;
  handleEditorKey(HID_KEY_ESCAPE,0);
  assert(currentState==UIState::SAVE_ERROR && editorHasUnsavedChanges());
  assert(getSaveErrorSelection()==1);
  handleSaveErrorKey(HID_KEY_ESCAPE);
  assert(currentState==UIState::TEXT_EDITOR && std::string(editorGetBuffer())=="bc");
  requestEditorExit(UIState::MAIN_MENU); // physical Power follows the same path
  assert(currentState==UIState::SAVE_ERROR);
  handleSaveErrorKey(HID_KEY_UP); // retry
  SdMan.failOpen=false; handleSaveErrorKey(HID_KEY_ENTER);
  assert(currentState==UIState::MAIN_MENU && !editorHasUnsavedChanges());
  assert(SdMan.files["/notes/untitled_2.md"]=="bc");

  currentState=UIState::TEXT_EDITOR; handleEditorPrintable('d');
  SdMan.failOpen=true;
  handleEditorKey(HID_KEY_N,MOD_CTRL_LEFT); // cannot replace unsaved text
  assert(currentState==UIState::SAVE_ERROR && std::string(editorGetBuffer())=="bcd");
  handleSaveErrorKey(HID_KEY_ENTER); // default: keep editing
  assert(currentState==UIState::TEXT_EDITOR && editorHasUnsavedChanges());
  requestEditorExit(UIState::FILE_BROWSER);
  handleSaveErrorKey(HID_KEY_DOWN); handleSaveErrorKey(HID_KEY_ENTER); // explicit discard
  assert(currentState==UIState::FILE_BROWSER && !editorHasUnsavedChanges());
  assert(SdMan.files["/notes/untitled_2.md"]=="bc");
  SdMan.failOpen=false;
  assert(handleNoteShortcut(HID_KEY_N,MOD_CTRL_LEFT));
  handleRenameKey(HID_KEY_ENTER,0); // named creation still saves immediately
  assert(currentState==UIState::TEXT_EDITOR && !fileManagerSaveFailed());
  handleEditorKey(HID_KEY_N,MOD_CTRL_LEFT); // create from editor without Ctrl+R confusion
  assert(currentState==UIState::RENAME_FILE && editorGetLength()==0);
  std::cout<<"Delete-last-note, Ctrl+N, cancelled/empty titles, save failure recovery and explicit discard passed.\n";
}
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
  noteWorkflows();
  std::cout<<"Installed international keyboard composition and literal Pandoc identifiers passed.\n";
}
