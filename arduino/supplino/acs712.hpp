/*
 * ACS712 and voltage divider custom library
 * Copyright (c) 2022 Paolo Loberto
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef ACS712_H
#define ACS712_H
#include <Arduino.h>

#ifndef IREADINGS
#define IREADINGS 200 // number of analog readings for current
#endif

#ifndef VREADINGS
#define VREADINGS 20 // number of analog readings for voltage
#endif

#if defined(ESP8266) || defined(ESP32)
#include <functional>
#define ADJUST_VOLTAGE std::function<float(float &v_val)> adjustVotlage
#define ADJUST_CURRENT std::function<float(float &i_val)> adjustCurrent
#define CHECK_CURRENT std::function<bool(uint16_t &i_val)> checkCurrent
#define CHECK_VOLTAGE std::function<bool(uint16_t &v_val)> checkVoltage
#else
#define ADJUST_VOLTAGE float (*adjustVotlage)(float &v_val)
#define ADJUST_CURRENT float (*adjustCurrent)(float &i_val)
#define CHECK_CURRENT bool (*checkCurrent)(uint16_t &i_val)
#define CHECK_VOLTAGE bool (*checkVoltage)(uint16_t &v_val)
#endif

class ACS712
{
  public:
    ACS712(uint8_t vPin, uint8_t iPin, float adcConv, float voltsAmpere);
    float getVoltage();
    float getCurrent();
    float getPower();
    float getCurrentCalibration();
    bool computeReadings(bool calibration = false);
    void calibrate();
    void setVoltageAdjustCB(ADJUST_VOLTAGE);
    void setCurrentAdjustCB(ADJUST_CURRENT);
    void setCurrentCheckCB(CHECK_CURRENT);
    void setVoltageCheckCB(CHECK_VOLTAGE);
  private:
    float voltage;
    float current;
    float power;
    uint8_t currentDecimals;
    float currentCalibration; // ADC zero offset for current sensor (2.5V)
    uint8_t vPin;
    uint8_t iPin;
    float adcConv;
    float voltsAmpere;
    ADJUST_VOLTAGE;
    ADJUST_CURRENT;
    CHECK_CURRENT;
    CHECK_VOLTAGE;
};
#endif
