#pragma once
#include <cstddef>
#include <cstdint>

// One byte per source byte; source offsets never refer to a rewritten buffer.
namespace markdown {
enum : uint8_t { Hidden=1, Bold=2, Italic=4, Heading=8, Bullet=16, Note=32, Literal=64 };
struct Range { size_t begin, end; };
size_t next(const char* text, size_t length, size_t at);
size_t previous(const char* text, size_t at);
Range paragraph(const char* text, size_t length, size_t cursor);
void parse(const char* text, size_t length, uint8_t* flags);
}
