#pragma once
using adc1_channel_t = int;
constexpr int ADC1_CHANNEL_0=0, ADC1_CHANNEL_1=1, ADC1_CHANNEL_2=2;
constexpr int ADC_WIDTH_BIT_12=12, ADC_ATTEN_DB_12=12;
extern int testAdc[3];
inline int adc1_get_raw(adc1_channel_t c) { return testAdc[c]; }
inline void adc1_config_width(int) {}
inline void adc1_config_channel_atten(int,int) {}
