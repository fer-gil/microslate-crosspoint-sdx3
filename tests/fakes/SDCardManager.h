#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>
constexpr int O_RDONLY=0, O_WRONLY=1, O_CREAT=2, O_TRUNC=4;
struct FakeSD;
struct FakeFile {
  FakeSD* sd=nullptr;
  std::string path;
  size_t pos=0, entry=0;
  std::vector<std::string> entries;
  explicit operator bool() const { return sd!=nullptr; }
  bool isDirectory() const { return path=="/notes"; }
  void close() { sd=nullptr; }
  void rewindDirectory() { entry=0; }
  void getName(char* out,size_t n) { snprintf(out,n,"%s",path.substr(path.find_last_of('/')+1).c_str()); }
  size_t size() const;
  int read(void*,size_t);
  size_t write(const uint8_t*,size_t);
  FakeFile openNextFile();
};
struct FakeSD {
  std::map<std::string,std::string> files;
  bool shortWrite=false, corruptRead=false, failOpen=false;
  std::string failRenameFrom,failRemove;
  bool begin() { return true; }
  void sleep() {}
  bool mkdir(const char*) { return true; }
  bool exists(const char* p) { return std::string(p)=="/notes" || files.count(p); }
  bool remove(const char* p) { if(failRemove==p) return false; return files.erase(p)>0; }
  bool rename(const char* a,const char* b) {
    if(failRenameFrom==a || !files.count(a) || files.count(b)) return false;
    files[b]=files[a]; files.erase(a); return true;
  }
  FakeFile open(const char* p,int mode=0) {
    if(failOpen) return {};
    FakeFile f; f.path=p;
    if(f.path=="/notes") {
      f.sd=this;
      for(auto& kv:files) if(kv.first.rfind("/notes/",0)==0) f.entries.push_back(kv.first);
      return f;
    }
    if(mode&O_CREAT) files[p]="";
    if(!files.count(p)) return {};
    f.sd=this; return f;
  }
};
inline size_t FakeFile::size() const { return sd->files[path].size(); }
inline int FakeFile::read(void* out,size_t n) {
  auto& data=sd->files[path]; n=std::min(n,data.size()-pos);
  memcpy(out,data.data()+pos,n); pos+=n;
  if(sd->corruptRead && n) static_cast<char*>(out)[0]^=1;
  return n;
}
inline size_t FakeFile::write(const uint8_t* in,size_t n) {
  if(sd->shortWrite && n) --n;
  sd->files[path].assign(reinterpret_cast<const char*>(in),n); return n;
}
inline FakeFile FakeFile::openNextFile() {
  if(entry==entries.size()) return {};
  return sd->open(entries[entry++].c_str());
}
extern FakeSD SdMan;
