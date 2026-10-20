#pragma once

inline void noInterrupts() {}
inline void interrupts() {}
#if defined(INPUT_MANAGER_TEST)
#include <cstdint>
extern unsigned long testNow;
extern int testPower;
constexpr int LOW=0, INPUT_PULLUP=2;
inline unsigned long millis() { return testNow; }
inline int digitalRead(int) { return testPower; }
inline void pinMode(int,int) {}
#endif
