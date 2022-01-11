/*
 * SUPPLINO : A Quick & Dirty PSU
 * Copyright (c) 2022 Giovanni Bernardo, Paolo Loberto.
 * 
 * https://www.github.com/settorezero/supplino
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
#define SUPPLINO_VERSION "1.0"

#include <Arduino.h>
#include <SPI.h>
#include "Ucglib.h" // Ucglib by Oliver (reference: https://github.com/olikraus/ucglib/wiki/reference)
#include "gauge.hpp"
#include "acs712.hpp"

// ST7735 display to Arduino nano connections:
// CLK    : 13 (or SCL)
// SDA    : 11
// RS     :  9 (or CD or A0)
// CS     : 10
// RST    :  8 (or RES)

// display instance using software SPI (first) or hardware SPI (second)
//Ucglib_ST7735_18x128x160_SWSPI ucg(/*sclk=*/ 13, /*data=*/ 11, /*cd=*/ 9, /*cs=*/ 10, /*reset=*/ 8); // for software SPI
//Ucglib_ST7735_18x128x160_HWSPI ucg(/*cd=*/ 9, /*cs=*/ 10, /*reset=*/ 8); // for hardware SPI
Ucglib_ST7735_18x128x160_HWSPI ucg(9, 10, 8);

// Used GPIOs
// 8,9,10,11,13 not available since used by display
#define VOLTAGE_SENSE A0 // voltage divider on A0 for reading voltage output from switching regulator
#define CURRENT_SENSE A1 // analog output from ACS712 current sensor
#define RELAY          4 // relay attached on D4
#define BUTTON         3 // button attached on D3 (interrupt used)

// constants
// note: XL4016E1 IC accept from 4 to 40V at input, and outputs from 1.25 to 36V
#define RELAYON     LOW // relay module used is active low => turns on when low level is sent
#define CURRENTMAX    2 // A
#define VOLTAGEMAX   33 // V
#define VOLTAGEMIN    1 // V - used for undervoltage alarm, see "ADC_UNDERVOLTAGEALARM" below
#define POWERMAX      CURRENTMAX*VOLTAGEMAX // W
#define DIVIDERCONST  0.12821F // conversion from voltage out from divider to voltage on divider input R1=68K, R2=10K => R1/(R1+R2)
#define ADCCONV       0.00488F // 5/1024, used for conversion from ADC to voltage
#define ACSCONST      0.1F  // used 20A type that gives 100mV (0.1V) per 1 Ampere

// alarms
// Inverse formula for having ADC value from CURRENTMAX value
// currentCalibration value has to be added to this
// Current= [(ADCvalue-currentCalibration) * (5/1024)] / 0.1
// ADCvalue = [(Current * 0.1) / (5/1024)] + currentCalibration
#define ADC_CURRENTALARM int(CURRENTMAX*ACSCONST/ADCCONV)
// Inverse formula for having ADC value from VOLTAGEMAX value
#define ADC_OVERVOLTAGEALARM int(VOLTAGEMAX*DIVIDERCONST/ADCCONV)
// Inverse formula for having ADC value from VOLTAGEMIN value (used eventually for short-circuit detect)
// put since buck-converter module used outputs a minimun of 1.25V, so a voltage less than
// 1V can be interpreted as short circuit. If you don't need that, put "VOLTAGEMIN" to 0
#define ADC_UNDERVOLTAGEALARM int(VOLTAGEMIN*DIVIDERCONST/ADCCONV)

// colors used for Voltage, Current and Power (background=1/foreground=0, R, G, B)
#define SET_V_COLOR ucg.setColor(0, 0, 250, 0)
#define SET_I_COLOR ucg.setColor(0, 0, 150, 255)
#define SET_W_COLOR ucg.setColor(0, 204, 204, 0)

// voltage adjustment
//#define VOLT_ADJ(V)  V // for no correction
#define VOLT_ADJ(V) V+0.2F // your own formula

// current adjustment
//#define CURR_ADJ(I) I // for no correction
#define CURR_ADJ(I) I-0.015F // your own formula

Gauge gauge(&ucg); // analog gauge instance
// constants used for the analog gauge
#define G_X 59
#define G_Y 123
#define G_ARC 120
#define G_RADIUS 30

enum gaugeType {
  V,
  I,
  W
};

enum alarmType {
  no_alarm,
  over_load,
  over_voltage,
  under_voltage,
};

alarmType alarm = no_alarm;

gaugeType showedGauge = V;
volatile bool outputEnabled = false; // output relay detached
volatile bool skipInterrupt = true;

ACS712 acs712(VOLTAGE_SENSE, CURRENT_SENSE, ADCCONV, ACSCONST);
//------------------------------------------------------------------------------------------------------------

// toggle relay allowing power output
void outputEnable(void)
{
  outputEnabled = true;
  digitalWrite(RELAY, RELAYON);
}

// toggle relay disabling power output
void outputDisable(void)
{
  outputEnabled = false;
  digitalWrite(RELAY, !RELAYON);
}

// Interrupt Service Routine at button state change
// from low to high (button release)
void ISR_buttonPress(void)
{
  static long lastPress = 0;
  
  if (skipInterrupt) // routine in the main on the long press said to skip this interrupt
  {
    skipInterrupt = false;
    interrupts();
    return;
  }

  noInterrupts(); // disable interrupts
  
  if ((millis() - lastPress) < 200)
  {
    // exit if <200mS press
    lastPress = millis();
    interrupts(); // re-enable interrupts
    return;
  }
  // in an alarm status, first press reset the alarm
  // second press will re-enable the power output
  if (alarm != no_alarm)
  {
    alarm = no_alarm; // reset alarm status...
    lastPress = millis();
    interrupts();
    return; // ... but remain with relay off
  }
  outputEnabled ? outputDisable() : outputEnable(); // toggle
  //while (!digitalRead(BUTTON)); // eventually stay stuck until button released
  lastPress = millis();
  interrupts(); // re-enable interrupts
}

// set alarm disabling the output
void setAlarm(alarmType alm)
{
  alarm = alm; // alarm is global
  outputDisable();
  outputIcon();
}

void setup(void)
{
  noInterrupts(); // disable interrupts
  pinMode(RELAY, OUTPUT);
  outputDisable();
  pinMode(BUTTON, INPUT_PULLUP);
  Serial.begin(9600);
  delay(1000);

  ucg.begin(UCG_FONT_MODE_TRANSPARENT);
  ucg.clearScreen();
  ucg.setRotate270(); // 90=display horizontal (landscape) with data header on the left

  // paint screen in black
  ucg.setColor(0, 0, 0, 0);
  ucg.drawBox(0, 0, ucg.getWidth(), ucg.getHeight());

  // set font
  ucg.setColor(0, 255, 255, 255); // foreground color
  ucg.setColor(1, 0, 0, 0); // background color
  ucg.setFontMode(UCG_FONT_MODE_SOLID); // solid: background will painted
  ucg.setFont(ucg_font_logisoso16_hr); // font (https://github.com/olikraus/ucglib/wiki/fontsize)
  ucg.setPrintDir(0);
  ucg.drawHLine(10, 35, 140);
  ucg.setPrintPos(23, 25);
  ucg.print("SUPPLINO ");
  ucg.print(SUPPLINO_VERSION);
  ucg.setFont(ucg_font_ncenR14_hr);
  ucg.setPrintPos(34, 65);
  ucg.print("CyB3rn0id");
  ucg.setPrintPos(35, 95);
  ucg.print("MrLoba81");

  acs712.setVoltageAdjustCB([](float &v_val)
  {
    // voltage from divider output to voltage on divider input
    // and user adjustment
    return VOLT_ADJ(v_val / DIVIDERCONST);
  });

  acs712.setCurrentAdjustCB([](float &i_val)
  {
    // user adjustment
    return CURR_ADJ(i_val);
  });

  acs712.setVoltageCheckCB([](uint16_t &v_val)
  {
    // quickly check a short-circuit
    if (v_val < ADC_UNDERVOLTAGEALARM)
    {
      setAlarm(under_voltage);
      return false;
    }
    // quickly check an over-voltage. Dunno if useful or not, maybe a broken switching module can output voltage unregulated?
    if (v_val > ADC_OVERVOLTAGEALARM)
    {
      setAlarm(over_voltage);
      return false;
    }
    return true;
  });

  acs712.setCurrentCheckCB([](uint16_t &i_val)
  {
    if (i_val > acs712.getCurrentCalibration() + ADC_CURRENTALARM)
    {
      setAlarm(over_load);
      return false;
    }

    return true;
  });

  // calibrate zero current value
  outputDisable(); // disable output in calibration mode
  acs712.computeReadings(true);

  delay(1500);

  // paint screen in black
  ucg.setColor(0, 0, 0, 0);
  ucg.drawBox(0, 0, ucg.getWidth(), ucg.getHeight());

  // draw gauge scale
  drawGaugeScale();

  // draw decorations
  ucg.setColor(0, 255, 255, 255); // foreground color
  ucg.setColor(1, 0, 0, 0); // background color
  // frame & separation lines
  ucg.drawFrame(0, 0, 160, 128);
  ucg.drawHLine(0, 60, 160);
  ucg.drawVLine(53, 0, 60);
  ucg.drawVLine(107, 0, 60);
  // led border
  ucg.drawDisc(135, 94, 17, UCG_DRAW_ALL);

  // enable interrupt on button release
  attachInterrupt(digitalPinToInterrupt(BUTTON), ISR_buttonPress, RISING);
  delay(500);
  interrupts();
}

void loop(void)
{
  static unsigned long lastCheck = millis();
  if (digitalRead(BUTTON) == LOW) // interrupt not happened: ISR will fire after button will be released
  {
    if (millis() - lastCheck >= 1000) { // button pressed for more than a second
      skipInterrupt = true;
      showedGauge = showedGauge == V ? I : (showedGauge == I ? W : V);
      drawGaugeScale();
      updateGauge();
      lastCheck = millis();
    }
  } else { // button not pressed
    lastCheck = millis();
  }
  // re-set font since gauge uses another kind of font
  ucg.setFontMode(UCG_FONT_MODE_SOLID); // solid: background will painted
  ucg.setFont(ucg_font_logisoso16_hr); // font (https://github.com/olikraus/ucglib/wiki/fontsize)
  ucg.setPrintDir(0);

  // update values
  acs712.computeReadings(false);
  printValues();
  updateGauge();
  outputIcon();
}

// draw Analog gauge scale
void drawGaugeScale(void)
{
  ucg.setColor(1, 0, 0, 0);
  ucg.setColor(0, 0, 0, 0);
  ucg.drawBox(1, 61, 115, 62);
  switch (showedGauge)
  {
    case I:
      gauge.drawGauge(G_X, G_Y, G_ARC, G_RADIUS, 5, 15, 0, CURRENTMAX, 0, CURRENTMAX - 1, CURRENTMAX - 1);
      gauge.drawPointer(G_X, G_Y, G_ARC, G_RADIUS, -1, 0, CURRENTMAX);
      break;
    case W:
      gauge.drawGauge(G_X, G_Y, G_ARC, G_RADIUS, 5, 15, 0, POWERMAX, 0, POWERMAX - 1, POWERMAX - 1);
      gauge.drawPointer(G_X, G_Y, G_ARC, G_RADIUS, -1, 0, POWERMAX);
      break;
    default:
      gauge.drawGauge(G_X, G_Y, G_ARC, G_RADIUS, 5, 15, 0, VOLTAGEMAX, 0, 0, 0);
      gauge.drawPointer(G_X, G_Y, G_ARC, G_RADIUS, -1, 0, VOLTAGEMAX);
      break;
  }
}

// update analog gauge pointer
void updateGauge(void)
{
  switch (showedGauge)
  {
    case I:
      gauge.drawPointer(G_X, G_Y, G_ARC, G_RADIUS, acs712.getCurrent(), 0, CURRENTMAX);
      ucg.setFontMode(UCG_FONT_MODE_SOLID);
      ucg.setFont(ucg_font_orgv01_hf);
      ucg.setPrintPos(G_X - 16, G_Y);
      outputEnabled ? (SET_I_COLOR) : ucg.setColor(0, 90, 90, 90);
      ucg.print("AMPERE");
      break;

    case W:
      gauge.drawPointer(G_X, G_Y, G_ARC, G_RADIUS, acs712.getPower(), 0, POWERMAX);
      ucg.setFontMode(UCG_FONT_MODE_SOLID);
      ucg.setFont(ucg_font_orgv01_hf);
      ucg.setPrintPos(G_X - 13, G_Y);
      outputEnabled ? (SET_W_COLOR) : ucg.setColor(0, 90, 90, 90);
      ucg.print("WATTS");
      break;

    default:
      gauge.drawPointer(G_X, G_Y, G_ARC, G_RADIUS, acs712.getVoltage(), 0, VOLTAGEMAX);
      ucg.setFontMode(UCG_FONT_MODE_SOLID);
      ucg.setFont(ucg_font_orgv01_hf);
      ucg.setPrintPos(G_X - 13, G_Y);
      outputEnabled ? (SET_V_COLOR) : ucg.setColor(0, 90, 90, 90);
      ucg.print("VOLTS");
      break;
  }
}

// print digital values
void printValues()
{
  static float preV = 0;
  static float preI = 0;
  static float preW = 0;

  // draw a black box passing from a value >10 to a value<10
  ucg.setColor(1, 0, 0, 0);
  ucg.setColor(0, 0, 0, 0);
  if (preV >= 10.0 && acs712.getVoltage() < 10.0) ucg.drawBox(1, 12, 51, 18);
  outputEnabled ? SET_V_COLOR : ucg.setColor(0, 90, 90, 90); // default color is gray
  acs712.getVoltage() < 10.0 ? ucg.setPrintPos(14, 29) : ucg.setPrintPos(8, 29);
  ucg.print(acs712.getVoltage(), 1);
  ucg.setPrintPos(22, 50);
  ucg.print("V");

  // draw a black box passing from a value >10 to a value<10
  ucg.setColor(1, 0, 0, 0);
  ucg.setColor(0, 0, 0, 0);
  if (preI >= 10.0 && acs712.getCurrent() < 10.0) ucg.drawBox(54, 12, 52, 18);
  outputEnabled ? SET_I_COLOR : ucg.setColor(0, 90, 90, 90); // default color is gray
  acs712.getCurrent() < 10.0 ? ucg.setPrintPos(63, 29) : ucg.setPrintPos(60, 29);
  ucg.print(acs712.getCurrent(), acs712.getCurrent() >= 10.0 ? 1 : 2);
  if (acs712.getCurrent() >= 10.0) ucg.print(" ");
  ucg.setPrintPos(75, 50);
  ucg.print("A");

  // draw a black box passing from a value >10/>20 to a value<10/<20
  // not tested with values over 99.9
  ucg.setColor(1, 0, 0, 0);
  ucg.setColor(0, 0, 0, 0);
  if ((preW >= 10.0 && acs712.getPower() < 10.0) || (preW >= 20.0 && acs712.getPower() < 20.0) || (preW < 10.0 && acs712.getPower() >= 10.0))
  {
    ucg.drawBox(108, 12, 51, 18);
  }
  outputEnabled ? SET_W_COLOR : ucg.setColor(0, 90, 90, 90); // default color is gray
  acs712.getPower() < 10.0 ? ucg.setPrintPos(116, 29) : (acs712.getPower() >= 20.0 ? ucg.setPrintPos(115, 29) : ucg.setPrintPos(113, 29));
  ucg.print(acs712.getPower(), acs712.getPower() <= 10.0 ? 2 : (acs712.getPower() < 100.0 ? 1 : 0));
  ucg.setPrintPos(128, 50);
  ucg.print("W");

  preV = acs712.getVoltage();
  preI = acs712.getCurrent();
  preW = acs712.getPower();
}

// draw round icon indicating power output status
void outputIcon(void)
{
  if (outputEnabled)
  {
    ucg.setColor(0, 0, 255, 0); // green
  }
  else
  {
    if (alarm == no_alarm)
    {
      ucg.setColor(0, 90, 90, 90); // gray
    }
    else
    {
      ucg.setColor(0, 255, 0, 0); // red
    }
  }
  ucg.drawDisc(135, 94, 13, UCG_DRAW_ALL);
}
