#include <InputManager.h>
#include <cassert>
#include <iostream>
unsigned long testNow=0;
int testAdc[3]={4095,4095,4095};
int testPower=1;
static void pollFor(InputManager& input,int milliseconds) {
  for(int i=0;i<milliseconds;i+=10) { testNow+=10; input.poll(); }
}
int main() {
  InputManager input; input.begin(); pollFor(input,20); input.update();
  assert(!input.wasAnyPressed());
  // A complete tap happens while the display blocks the main consumer for 500ms.
  testAdc[1]=1500; pollFor(input,80);
  testAdc[1]=4095; pollFor(input,420);
  input.update();
  assert(input.wasPressed(InputManager::BTN_LEFT));
  assert(input.wasReleased(InputManager::BTN_LEFT));
  assert(!input.isPressed(InputManager::BTN_LEFT));
  input.update(); assert(!input.wasAnyPressed());
  // Invalid ADC results must not become phantom Right/Down presses.
  testAdc[1]=testAdc[2]=-1; pollFor(input,40); input.update();
  assert(!input.wasAnyPressed());
  testAdc[1]=testAdc[2]=4095; pollFor(input,20); input.update();
  // Noise on the second ladder must not indefinitely cancel debounce on the first.
  testAdc[1]=2700;
  for(int i=0;i<10;++i) { testAdc[2]=i%2 ? 4095 : 2200; pollFor(input,10); }
  input.update(); assert(input.wasPressed(InputManager::BTN_CONFIRM));
  assert(input.isPressed(InputManager::BTN_CONFIRM));
  testAdc[1]=testAdc[2]=4095; pollFor(input,30); input.update();
  testPower=0; pollFor(input,30); input.update(); assert(input.wasPressed(InputManager::BTN_POWER));
  testPower=1; pollFor(input,30); input.update(); assert(input.wasReleased(InputManager::BTN_POWER));
  std::cout<<"Physical taps survive blocked rendering; ADC errors and independent debounce passed.\n";
}
