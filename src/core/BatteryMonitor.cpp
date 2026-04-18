// BatteryMonitor.cpp
#include "BatteryMonitor.h"

#include <Arduino.h>

BatteryMonitor::BatteryMonitor(uint8_t adcPin, float dividerMultiplier)
    : _adcPin(adcPin), _dividerMultiplier(dividerMultiplier) {}

uint16_t BatteryMonitor::readPercentage() const {
  return percentageFromMillivolts(readMillivolts());
}

uint16_t BatteryMonitor::readMillivolts() const {
  // ESP-IDF 5.x and Arduino-ESP32 3.x+ support analogReadMilliVolts
  const uint16_t mv = analogReadMilliVolts(_adcPin);
  return static_cast<uint16_t>(mv * _dividerMultiplier);
}

double BatteryMonitor::readVolts() const {
  return static_cast<double>(readMillivolts()) / 1000.0;
}

uint16_t BatteryMonitor::percentageFromMillivolts(uint16_t millivolts) {
  double volts = millivolts / 1000.0;
  // Polynomial derived from LiPo samples
  double y = -144.9390 * volts * volts * volts + 1655.8629 * volts * volts - 6158.8520 * volts + 7501.3202;

  // Clamp to [0,100] and round
  y = std::max(y, 0.0);
  y = std::min(y, 100.0);
  y = round(y);
  return static_cast<uint16_t>(y);
}
