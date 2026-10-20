#include "markdown.h"
#include "note_format.h"
#include "text_editor.h"
#include "file_manager.h"
#include "autosave.h"
#include "hid_report.h"
#include <SDCardManager.h>
#include <cassert>
#include <iostream>
#include <random>

FakeSD SdMan;
UIState currentState=UIState::MAIN_MENU;
static void load(const std::string& s,const char* name="test.md") {
  editorInit(); editorSetCurrentFile(name);
  memcpy(editorGetBuffer(),s.c_str(),s.size()+1); editorLoadBuffer(s.size());
}
static std::string view() {
  std::string out;
  for(size_t p=0;p<editorGetLength();) {
    char g[5]; uint8_t style; size_t end=editorGetGlyph(p,g,style);
    assert(end>p); out+=g; p=end;
  }
  return out;
}
static int measure(const char* g,uint8_t f) {
  return (g[0]=='W' ? 3 : 1) + ((f&markdown::Bold) ? 1 : 0);
}
static void parserAndEditor() {
  std::string styled="# Title\n\n**bold** *italic* ***both***\n\n- item\n";
  std::vector<uint8_t> flags(styled.size());
  markdown::parse(styled.data(),styled.size(),flags.data());
  assert(flags[styled.find("Title")] & markdown::Heading);
  assert(flags[styled.find("bold")] & markdown::Bold);
  assert(flags[styled.find("italic")] & markdown::Italic);
  assert((flags[styled.find("both")] & (markdown::Bold|markdown::Italic)) == (markdown::Bold|markdown::Italic));
  assert(flags[styled.find("- item")] & markdown::Bullet);

  std::string raw="# Introducción\n\n**fuerte** *suave* ***ambas***\n\n- café[^nota]\n\n[^nota]: pie\n    continuación\n\n";
  load(raw);
  assert(view()=="Introducción\n\nfuerte suave ambas\n\n• café[nota]\n\n[nota] pie\n    continuación\n\n");
  assert(std::string(editorGetBuffer())==raw);
  editorToggleVisual(); assert(view()==raw); editorToggleVisual();
  while(editorGetCursorPosition()) editorMoveCursorLeft();
  assert(view().rfind("# Introducción",0)==0);
  assert(!editorHasUnsavedChanges());
  load("*una\nlínea*\n\n"); assert(view()=="una\nlínea\n\n");
  editorMoveCursorUp(); editorMoveCursorUp();
  assert(view()=="*una\nlínea*\n\n");
  load("# Título\npárrafo\n\n");
  while(editorGetCursorPosition()) editorMoveCursorLeft();
  editorMoveCursorDown(); assert(view().rfind("Título\n",0)==0);
  load(raw,"old.TXT"); assert(view()==raw);
  load("\\*literal* a_b_c **sin cerrar `**code**`\n\n");
  assert(view()=="*literal* a_b_c **sin cerrar `**code**`\n\n");
  load("```md\n**literal**\n```\n\n");
  assert(view()=="```md\n**literal**\n```\n\n");
  load(""); editorInsertText("áñ🙂"); assert(std::string(editorGetBuffer())=="áñ🙂");
  load("áñ🙂"); editorMoveCursorLeft(); assert(editorGetCursorPosition()==4);
  editorDeleteChar(); assert(std::string(editorGetBuffer())=="á🙂");
  editorDeleteForward(); assert(std::string(editorGetBuffer())=="á");
  editorDeleteChar(); assert(editorGetLength()==0);
  load("WWW abc áñ🙂 def\n\n"); editorSetLayout(7,measure,1);
  for(int i=0;i<editorGetLineCount();++i) {
    int start=editorGetLinePosition(i);
    assert(start==0 || (static_cast<unsigned char>(editorGetBuffer()[start])&0xc0)!=0x80);
  }
  while(editorGetCursorPosition()) editorMoveCursorLeft();
  for(int i=0;i<10;++i) editorMoveCursorDown();
  for(int i=0;i<10;++i) editorMoveCursorUp();
  assert(std::string(editorGetBuffer())=="WWW abc áñ🙂 def\n\n");
  assert(isNoteFile("a.MD") && isNoteFile("a.TxT") && !isNoteFile("a.md.tmp"));
  // Arbitrary bounded input: no out-of-range writes, monotonic UTF-8 offsets,
  // terminating parser even with thousands of malformed delimiters.
  std::mt19937 rng(42);
  for(int trial=0;trial<300;++trial) {
    std::string s; const char chars[]="*_`~[]^:# abc\\\n";
    for(int i=0;i<1000;++i) s+=chars[rng()%(sizeof(chars)-1)];
    std::vector<uint8_t> flags(s.size()+2,0xa5);
    markdown::parse(s.data(),s.size(),flags.data()+1);
    assert(flags.front()==0xa5 && flags.back()==0xa5);
    auto r=markdown::paragraph(s.data(),s.size(),rng()%(s.size()+1));
    assert(r.begin<=r.end && r.end<=s.size());
  }
}
static void filesAndFailures() {
  for(int failure=0;failure<7;++failure) {
    SdMan=FakeSD{}; SdMan.files["/notes/test.md"]="old";
    load("**nuevo**\n"); editorSetUnsavedChanges(true);
    if(failure==1) SdMan.shortWrite=true;
    if(failure==2) SdMan.corruptRead=true;
    if(failure==3) SdMan.failRenameFrom="/notes/test.md";
    if(failure==4) SdMan.failRenameFrom="/notes/test.md.tmp";
    if(failure==5) SdMan.failOpen=true;
    if(failure==6) { SdMan.files["/notes/test.md.bak"]="older"; SdMan.failRemove="/notes/test.md.bak"; }
    saveCurrentFile(false);
    if(failure==0) {
      assert(!editorHasUnsavedChanges()); assert(!fileManagerSaveFailed());
      assert(SdMan.files["/notes/test.md"]=="**nuevo**\n");
      assert(SdMan.files["/notes/test.md.bak"]=="old");
    } else {
      assert(editorHasUnsavedChanges()); assert(fileManagerSaveFailed()); assert(SdMan.files["/notes/test.md"]=="old");
    }
  }
  SdMan=FakeSD{};
  SdMan.files["/notes/old.txt"]="legacy";
  SdMan.files["/notes/fresh.md"]="markdown";
  SdMan.files["/notes/fresh.md.tmp"]="ignore";
  refreshFileList(); assert(getFileCount()==2);
  loadFile("old.txt"); assert(!editorIsVisual());
  updateFileTitle("old.txt","Renamed");
  assert(SdMan.files["/notes/renamed.txt"]=="legacy");
  SdMan.files["/notes/recover.md.bak"]="last committed";
  SdMan.files["/notes/recover.md.tmp"]="uncommitted";
  fileManagerSetup(); assert(SdMan.files["/notes/recover.md"]=="last committed");
  assert(SdMan.files["/notes/recover.md.tmp"]=="uncommitted");
  char name[64]; deriveUniqueFilename("New Note",name,64); assert(std::string(name)=="new_note.md");
  SdMan.files["/notes/new_note.md"]="one";
  deriveUniqueFilename("New Note",name,64); assert(std::string(name)=="new_note_2.md");
  updateFileTitle("renamed.txt","Renamed"); assert(SdMan.files.count("/notes/renamed.txt"));
  SdMan.files["/notes/huge.md"]=std::string(TEXT_BUFFER_SIZE,'x');
  currentState=UIState::FILE_BROWSER; loadFile("huge.md"); assert(currentState==UIState::FILE_BROWSER);
}
int main() {
  assert(!autosaveDue(10000,0,0)); assert(autosaveDue(10001,0,0));
  assert(!autosaveDue(120000,119999,0)); assert(autosaveDue(120001,120000,0));
  assert(autosaveDue(5000,UINT32_MAX-6000,UINT32_MAX-6000));
  uint8_t out[8], ctrl[8]={1,0,0x16,0,0,0,0,0};
  assert(normalizeKeyboardReport(ctrl,8,1,out) && memcmp(out,ctrl,8)==0);
  uint8_t prefixed[9]={1,1,0,0x16,0,0,0,0,0};
  assert(normalizeKeyboardReport(prefixed,9,1,out) && memcmp(out,ctrl,8)==0);
  uint8_t compact[7]={1,0x16,0,0,0,0,0};
  assert(normalizeKeyboardReport(compact,7,1,out) && memcmp(out,ctrl,8)==0);
  assert(!normalizeKeyboardReport(compact,6,1,out));
  uint8_t k70[14]={1,0,0,0x16,0,0,0,0,0,0,0,0,0,0};
  assert(normalizeKeyboardReport(k70,14,1,out) && memcmp(out,ctrl,8)==0);
  uint8_t k70release[14]={0};
  assert(normalizeKeyboardReport(k70release,14,1,out) && out[0]==0 && out[2]==0);
  parserAndEditor(); filesAndFailures(); std::cout<<"Parser, source mapping, UTF-8, wrapping, formats and save failure tests passed.\n"; }
