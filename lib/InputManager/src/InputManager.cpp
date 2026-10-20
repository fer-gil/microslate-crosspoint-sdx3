#include "InputManager.h"
#include <driver/adc.h>
#include <algorithm>
#if defined(XTEINK_X3_HARDWARE) && !defined(INPUT_MANAGER_TEST)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
static portMUX_TYPE buttonMux = portMUX_INITIALIZER_UNLOCKED;
#define BUTTON_LOCK() portENTER_CRITICAL(&buttonMux)
#define BUTTON_UNLOCK() portEXIT_CRITICAL(&buttonMux)
#else
#define BUTTON_LOCK()
#define BUTTON_UNLOCK()
#endif

// Recorded ADC values from real devices
// BACK CONF LEFT RGHT   UP DOWN
// 3597 2760 1530    6 2300    6
// 3470 2666 1480    6 2222    5
// 3470 2655 1470    3 2205    3

// Averages
// BACK CONF LEFT RGHT   UP DOWN
// 3512 2694 1493    5 2242    5

// Setup ranges, if ADC value is between value `i` and `i + 1`, button `i` is being pressed
// These ranges are based on real world values above, and are much more tolerant of different
// devices than a fixed threshold check
// These values are calculated by taking the midpoint of the pairs of averaged values above
const int InputManager::ADC_RANGES_1[] = {ADC_NO_BUTTON, 3100, 2090, 750, INT32_MIN};
const int InputManager::ADC_RANGES_2[] = {ADC_NO_BUTTON, 1120, INT32_MIN};
const char* InputManager::BUTTON_NAMES[] = {"Back", "Confirm", "Left", "Right", "Up", "Down", "Power"};

InputManager::InputManager()
    : currentState(0),
      pressedEvents(0),
      releasedEvents(0),
      buttonPressStart(0),
      buttonPressFinish(0) {}

void InputManager::begin() {
  // Configure ADC using ESP-IDF API directly to avoid GPIO reconfiguration
  // that Arduino's analogRead() triggers on every call in the dual framework
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_12);  // GPIO 0 (battery)
  adc1_config_channel_atten(ADC1_CHANNEL_1, ADC_ATTEN_DB_12);  // GPIO 1
  adc1_config_channel_atten(ADC1_CHANNEL_2, ADC_ATTEN_DB_12);  // GPIO 2

  // Power button is digital, keep using Arduino API
  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
#if defined(XTEINK_X3_HARDWARE) && !defined(INPUT_MANAGER_TEST)
  // E-paper refreshes can block loop() for longer than a normal button tap.
  // Keep sampling on the single C3 core while rendering waits/yields.
  samplerStarted = xTaskCreate([](void* context) {
    auto* self = static_cast<InputManager*>(context);
    for (;;) { self->poll(); vTaskDelay(pdMS_TO_TICKS(10)); }
  }, "buttons", 2048, this, 2, nullptr) == pdPASS;
#elif defined(INPUT_MANAGER_TEST)
  samplerStarted = true; // Tests drive poll() with a deterministic clock.
#endif
}

int InputManager::getButtonFromADC(const int adcValue, const int ranges[], const int numButtons) {
  if (adcValue < 0 || adcValue > 4095) return -1;
  for (int i = 0; i < numButtons; i++) {
    if (ranges[i + 1] < adcValue && adcValue <= ranges[i]) {
      return i;
    }
  }

  return -1;
}

static int readButtonADC(adc1_channel_t channel) {
  // Discard the first conversion after switching channels, then reject outliers.
  adc1_get_raw(channel);
  int values[5];
  for (int i=0;i<5;++i) values[i]=adc1_get_raw(channel);
  std::sort(values,values+5);
  return values[2];
}
uint8_t InputManager::getState() {
  uint8_t state = 0;

  // Read GPIO1 buttons using ESP-IDF API (no GPIO reconfiguration overhead)
  const int adcValue1 = readButtonADC(ADC1_CHANNEL_1);
  const int button1 = getButtonFromADC(adcValue1, ADC_RANGES_1, NUM_BUTTONS_1);
  if (button1 >= 0) {
    state |= (1 << button1);
  }

  // Read GPIO2 buttons using ESP-IDF API
  const int adcValue2 = readButtonADC(ADC1_CHANNEL_2);
  const int button2 = getButtonFromADC(adcValue2, ADC_RANGES_2, NUM_BUTTONS_2);
  if (button2 >= 0) {
    state |= (1 << (button2 + 4));
  }

  // Read power button (digital, active LOW)
  if (digitalRead(POWER_BUTTON_PIN) == LOW) {
    state |= (1 << BTN_POWER);
  }

  return state;
}

void InputManager::poll() {
  const unsigned long currentTime = millis();
  const uint8_t state = getState();
  BUTTON_LOCK();
  static constexpr uint8_t masks[] = {0x0f,0x30,0x40};
  // Noise on one resistor ladder must not reset the other ladder's debounce.
  for (int group=0;group<3;++group) {
    uint8_t candidate = state & masks[group];
    if (candidate != candidateGroup[group]) {
      candidateGroup[group] = candidate; candidateSince[group] = currentTime;
    }
    uint8_t oldGroup = sampledState & masks[group];
    if (currentTime-candidateSince[group] > DEBOUNCE_DELAY && candidate != oldGroup) {
      uint8_t pressed = candidate & ~oldGroup;
      uint8_t released = oldGroup & ~candidate;
      if (pressed && sampledState==0) buttonPressStart = currentTime;
      sampledState = (sampledState & ~masks[group]) | candidate;
      if (released && sampledState==0) buttonPressFinish = currentTime;
      pendingPressed |= pressed; pendingReleased |= released;
    }
  }
  BUTTON_UNLOCK();
}

void InputManager::update() {
  if (!samplerStarted) poll();
  BUTTON_LOCK();
  currentState = sampledState;
  pressedEvents = pendingPressed; releasedEvents = pendingReleased;
  pendingPressed = pendingReleased = 0;
  BUTTON_UNLOCK();
}

bool InputManager::isPressed(const uint8_t buttonIndex) const {
  return currentState & (1 << buttonIndex);
}

bool InputManager::wasPressed(const uint8_t buttonIndex) const {
  return pressedEvents & (1 << buttonIndex);
}

bool InputManager::wasAnyPressed() const {
  return pressedEvents > 0;
}

bool InputManager::wasReleased(const uint8_t buttonIndex) const {
  return releasedEvents & (1 << buttonIndex);
}

bool InputManager::wasAnyReleased() const {
  return releasedEvents > 0;
}

unsigned long InputManager::getHeldTime() const {
  // Still hold a button
  if (currentState > 0) {
    return millis() - buttonPressStart;
  }

  return buttonPressFinish - buttonPressStart;
}

const char* InputManager::getButtonName(const uint8_t buttonIndex) {
  if (buttonIndex <= BTN_POWER) {
    return BUTTON_NAMES[buttonIndex];
  }
  return "Unknown";
}

bool InputManager::isPowerButtonPressed() const {
  return isPressed(BTN_POWER);
}
