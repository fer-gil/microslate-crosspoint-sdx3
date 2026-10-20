#pragma once
#include <cstring>
#include <strings.h>
inline bool isMarkdownNote(const char* name) {
  size_t n=std::strlen(name);
  return n>3 && strcasecmp(name+n-3,".md")==0;
}
inline bool isLegacyNote(const char* name) {
  size_t n=std::strlen(name);
  return n>4 && strcasecmp(name+n-4,".txt")==0;
}
inline bool isNoteFile(const char* name) { return isMarkdownNote(name) || isLegacyNote(name); }
