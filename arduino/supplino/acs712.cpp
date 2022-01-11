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
#include "acs712.hpp"

ACS712::ACS712(uint8_t vPin, uint8_t iPin, float adcConv, float voltsAmpere) : voltage(0), current(0), power(0), currentDecimals(1), currentCalibration(512), vPin(vPin), iPin(iPin), adcConv(adcConv), voltsAmpere(voltsAmpere)
{}

float ACS712::getVoltage()
{
  return this->voltage;
}

float ACS712::getCurrent()
{
  return this->current;
}

float ACS712::getPower()
{
  return this->power;
}

void ACS712::setVoltageAdjustCB(ADJUST_VOLTAGE)
{
  this->adjustVotlage = adjustVotlage;
}

void ACS712::setCurrentAdjustCB(ADJUST_CURRENT)
{
  this->adjustCurrent = adjustCurrent;
}

void ACS712::setCurrentCheckCB(CHECK_CURRENT)
{
  this->checkCurrent = checkCurrent;
}

void ACS712::setVoltageCheckCB(CHECK_VOLTAGE)
{
  this->checkVoltage = checkVoltage;
}

float ACS712::getCurrentCalibration()
{
  return this->currentCalibration;
}

// compute readings fuction
// calibration=true => don't read voltage and store zero current value (calibration)
// calibration=false =>  read voltage too and use current calibration value to adjust the current value
bool ACS712::computeReadings(bool calibration)
{
  float i_val = 0.0; // current value (Ampere)
  float v_val = 0.0 ; // voltage value (Volt)
  uint16_t anValue = 0; // ADC raw readings from ACS sensor and from voltage divider
  uint8_t i = 0; // generic counter

  // read current value
  for (i = 0; i < IREADINGS; i++)
  {
    anValue = analogRead(this->iPin);
    // quickly detect an overload. This occurs also in case of a short-circuit
    if (this->checkCurrent && !this->checkCurrent(anValue)) {
      return false;
    }
    i_val += anValue;
  } // end current reading cycle
  i_val /= IREADINGS; // average

  // calibration mode=>store average value from ADC as ZERO (OFFSET) current
  if (calibration)
  {
    this->currentCalibration = i_val;
    return true; // exit from function
  }
  else // is not calibration
  {
    // normally you'll subtract an 2.5V offset, but better read the standby value (previous functions)
    // and then subtract that value (anyway in good conditions would be around 2.5V => around 512)
    i_val = i_val - this->currentCalibration;
    // turn (eventually) negative value in positive since we're working only on DC
    // and can happen we wired the current sensor in inverse way
    if (i_val < 0) i_val *= -1;
    // compute current value in Ampere
    i_val *= this->adcConv; // ADC to voltage in V
    i_val /= this->voltsAmpere; // Voltage to Ampere using the sensor constant

    if (this->adjustCurrent) {
      i_val = this->adjustCurrent(i_val);
    }
    if (i_val < 0) i_val = 0; // if user adjustment makes value less than zero, turn to zero
    this->current = i_val; // store value for gauges
  }

  // read voltage (and compute power) too if not in calibration mode
  if (!calibration)
  {
    for (i = 0; i < VREADINGS; i++)
    {
      anValue = analogRead(this->vPin);
      // quickly check a short-circuit or an over-voltage
      if (this->checkVoltage && !this->checkVoltage(anValue)) {
        return false;
      }
      v_val += anValue;
    } // end voltage reading cycle
    v_val /= VREADINGS; // average
    v_val *= this->adcConv;// ADC to voltage in V

    if (this->adjustVotlage) {
      v_val = this->adjustVotlage(v_val);
    }

    this->voltage = v_val;
    this->power = this->voltage * this->current; // active power

    return true;
  } // not current calibration => read voltage too
} // \computeReadings

void ACS712::calibrate()
{
  this->computeReadings(true);
}
