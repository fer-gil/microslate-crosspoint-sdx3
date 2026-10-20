#include "markdown.h"
#include <cstring>
#include <cctype>

namespace markdown {
size_t next(const char* s, size_t n, size_t p) {
  if (p >= n) return n;
  ++p;
  while (p < n && (static_cast<unsigned char>(s[p]) & 0xc0) == 0x80) ++p;
  return p;
}
size_t previous(const char* s, size_t p) {
  if (!p) return 0;
  --p;
  while (p && (static_cast<unsigned char>(s[p]) & 0xc0) == 0x80) --p;
  return p;
}
static bool space(char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n'; }
static size_t lineEnd(const char* s, size_t n, size_t p) {
  while (p<n && s[p]!='\n') ++p;
  return p;
}
static size_t content(const char* s, size_t b, size_t e) {
  while (b<e && (s[b]==' ' || s[b]=='\t' || s[b]=='\r')) ++b;
  return b;
}
static size_t noteEnd(const char* s, size_t b, size_t e) {
  if (b+3>=e || s[b]!='[' || s[b+1]!='^') return b;
  size_t p=b+2;
  while (p<e && !space(s[p]) && s[p]!=']' && s[p]!='[' && s[p]!='^') ++p;
  return p>b+2 && p<e && s[p]==']' ? p+1 : b;
}
static bool block(const char* s, size_t b, size_t e) {
  size_t p=content(s,b,e);
  if (p==e) return true;
  if (s[p]=='#' || s[p]=='`' || s[p]=='~') return true;
  if (p+1<e && (s[p]=='-' || s[p]=='+' || s[p]=='*') && space(s[p+1])) return true;
  if (noteEnd(s,p,e)>p && noteEnd(s,p,e)<e && s[noteEnd(s,p,e)]==':') return true;
  size_t digits=p;
  while (p<e && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
  return p>digits && p+1<e && (s[p]=='.' || s[p]==')') && space(s[p+1]);
}
static bool atomicLine(const char* s, size_t b, size_t e) {
  size_t p=content(s,b,e);
  return p<e && (s[p]=='#' || s[p]=='`' || s[p]=='~');
}
Range paragraph(const char* s, size_t n, size_t cursor) {
  if (cursor>n) cursor=n;
  size_t b=cursor;
  while (b && s[b-1]!='\n') --b;
  size_t e=lineEnd(s,n,b);
  // A structural line is its own block. Ordinary soft line breaks share a paragraph.
  if (content(s,b,e)==e || atomicLine(s,b,e)) return {b,e};
  while (b && !block(s,b,lineEnd(s,n,b))) {
    size_t prev=b-1;
    while (prev && s[prev-1]!='\n') --prev;
    if (content(s,prev,b-1)==b-1 || atomicLine(s,prev,b-1)) break;
    b=prev;
  }
  while (e<n) {
    size_t nb=e+1, ne=lineEnd(s,n,nb);
    if (block(s,nb,ne)) break;
    e=ne;
  }
  return {b,e};
}
static void mark(uint8_t* f, size_t b, size_t e, uint8_t bits) {
  for (; b<e; ++b) f[b]|=bits;
}
static bool word(char c) {
  return static_cast<unsigned char>(c)>=128 || std::isalnum(static_cast<unsigned char>(c));
}
static void inlineParse(const char* s, size_t b, size_t e, uint8_t* f, int depth) {
  if (depth>8) return;
  for (size_t p=b;p<e;) {
    if (f[p]&Literal) { ++p; continue; }
    size_t q=noteEnd(s,p,e);
    if (q>p) {
      f[p+1]|=Hidden;
      mark(f,p,q,Note);
      p=q; continue;
    }
    char c=s[p];
    if (c!='*' && c!='_') { ++p; continue; }
    size_t run=1;
    while (p+run<e && s[p+run]==c) ++run;
    if (run>3 || p+run==e || space(s[p+run]) || (c=='_' && p>b && word(s[p-1]))) {
      p+=run; continue;
    }
    size_t close=p+run;
    for (;close<e;++close) {
      if (s[close]!=c || (f[close]&Literal) || space(s[close-1])) continue;
      size_t cr=1;
      while (close+cr<e && s[close+cr]==c) ++cr;
      if (cr!=run || (c=='_' && close+cr<e && word(s[close+cr]))) { close+=cr-1; continue; }
      break;
    }
    if (close==e) { p+=run; continue; }
    mark(f,p,p+run,Hidden);
    mark(f,close,close+run,Hidden);
    uint8_t style=run==1 ? Italic : run==2 ? Bold : Bold|Italic;
    mark(f,p+run,close,style);
    inlineParse(s,p+run,close,f,depth+1);
    p=close+run;
  }
}
void parse(const char* s, size_t n, uint8_t* f) {
  std::memset(f,0,n);
  char fence=0; size_t fenceRun=0;
  for (size_t b=0;b<n;) {
    size_t e=lineEnd(s,n,b), p=content(s,b,e);
    size_t run=0;
    if (p<e && (s[p]=='`' || s[p]=='~')) {
      while (p+run<e && s[p+run]==s[p]) ++run;
    }
    if (fence || run>=3) {
      mark(f,b,e,Literal);
      if (!fence) { fence=s[p]; fenceRun=run; }
      else if (p<e && s[p]==fence && run>=fenceRun && content(s,p+run,e)==e) fence=0;
      b=e<n ? e+1 : n; continue;
    }
    // Protect escapes and inline code before matching emphasis/footnotes.
    for (size_t i=b;i<e;++i) {
      if (s[i]=='\\' && i+1<e && std::ispunct(static_cast<unsigned char>(s[i+1]))) {
        f[i]|=Hidden|Literal; f[++i]|=Literal; continue;
      }
      if (s[i]=='`') {
        size_t r=1; while(i+r<e && s[i+r]=='`') ++r;
        size_t j=i+r;
        for (;j<e;++j) {
          if (s[j]!='`') continue;
          size_t k=1; while(j+k<e && s[j+k]=='`') ++k;
          if(k==r) break;
          j+=k-1;
        }
        if(j<e) { mark(f,i,j+r,Literal); i=j+r-1; }
        else i+=r-1;
      }
    }
    if (p-b<=3 && p<e && !(f[p]&Literal)) {
      size_t q=p;
      while(q<e && s[q]=='#') ++q;
      if(q>p && q-p<=6 && (q==e || space(s[q]))) {
        while(q<e && space(s[q])) ++q;
        mark(f,p,q,Hidden); mark(f,q,e,Heading|Bold);
      } else if(p+1<e && (s[p]=='-' || s[p]=='+' || s[p]=='*') && space(s[p+1])) {
        // Horizontal rules remain literal.
        bool rule=true; size_t count=0;
        for(size_t i=p;i<e;++i) { if(s[i]==s[p]) ++count; else if(!space(s[i])) rule=false; }
        if(!(rule && count>=3)) f[p]|=Bullet;
      }
      q=noteEnd(s,p,e);
      if(q>p && q<e && s[q]==':') { f[p+1]|=Hidden; f[q]|=Hidden; mark(f,p,q,Note); }
    }
    b=e<n ? e+1 : n;
  }
  // Inline spans may cross soft source line breaks, but never paragraphs or blocks.
  for (size_t b=0;b<n;) {
    Range r=paragraph(s,n,b);
    inlineParse(s,b,r.end,f,0);
    b=r.end<n ? r.end+1 : n;
  }
}
}
