/*
  SUPPLINO
  quick&dirty PSU
  by @cyb3rn0id and @mrloba81
  https://www.github.com/settorezero/supplino
*/
#define SUPPLINO_VERSION "1.0"

#include <Arduino.h>
#include <SPI.h>
#include "Ucglib.h" // Ucglib by Oliver (reference: https://github.com/olikraus/ucglib/wiki/reference)
#include "gauge.hpp"

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
#define RELAYON      LOW // relay module used is active low => turns on when low level is sent
#define IREADINGS   200  // number of analog readings for current
#define VREADINGS    20  // number of analog readings for voltage
#define CURRENTMAX    5  // A
#define VOLTAGEMAX   30  // V
#define POWERMAX     CURRENTMAX*VOLTAGEMAX // W
#define DIVIDERCONST 0.12795F // voltage from divider to voltage on divider input (R1=68K, R2=10K)
#define ADCCONV      0.00488F // 5/1024, used for conversion from ADC to voltage
#define ACSCONST     0.1F  // used 20A type that gives 100mV (0.1V) per 1 Ampere

// alarms
// Inverse formula for having ADC value from CURRENTMAX value
// currentCalibration value has to be added to this
// Current= [(ADCvalue-currentCalibration) * (5/1024)] / 0.1
// ADCvalue = [(Current * 0.1) / (5/1024)] + currentCalibration
#define ADCCURRENTALARM int(CURRENTMAX*ACSCONST/ADCCONV)
// Inverse formula for having ADC value from VOLTAGEMAX value
#define ADCVOLTAGEALARM int(VOLTAGEMAX*DIVIDERCONST/ADCCONV)

// de-comment only one for showing the analog gauge
//#define GAUGE_V // shows voltage analog gauge
#define GAUGE_I // shows current analog gauge
//#define GAUGE_W // shows power analog gauge

// color for Voltage, Current and Power
#define SET_V_COLOR ucg.setColor(0, 0, 250, 0);
#define SET_I_COLOR ucg.setColor(0, 0, 150, 255);
#define SET_W_COLOR ucg.setColor(0, 204, 204, 0);

// user adjustments for voltage and current readings
//#define VOLT_ADJ(V) V-0.2
#define VOLT_ADJ(V)  V // for no correction
#define CURR_ADJ(I) I-0.09
// #define CURR_ADJ(I) I // for no correction

Gauge gauge(&ucg);
// constants used for the gauge
#define G_X 59 
#define G_Y 123
#define G_ARC 120
#define G_RADIUS 30

enum alarmType {
  no_alarm,
  over_load,
  over_voltage,
  short_circuit,
};

struct readData {
  float voltage = 0;
  float current = 0;
  float power = 0;
  float currentCalibration = 512; // ADC zero offset for ACS current sensor (=2.5V)
} data;

alarmType alarm = no_alarm;
volatile bool outputEnabled = false; // output relay detached

// compute readings fuction
// calibration=true => don't read voltage and store zero current value (calibration)
// calibration=false =>  read voltage too and use current calibration value to adjust the current value
void computeReadings(readData *data, bool calibration)
{
  float i_val = 0.0; // current value (Ampere)
  float v_val = 0.0 ; // voltage value (Volt)
  uint16_t anValue = 0; // ADC raw readings from ACS sensor and from voltage divider
  uint8_t i = 0; // generic counter

  if (calibration) outputDisable(); // disable output if in calibration mode

  // read current value
  for (i = 0; i < IREADINGS; i++)
  {
    anValue = analogRead(CURRENT_SENSE);
    // quickly detect an overload
    if (anValue > data->currentCalibration + ADCCURRENTALARM)
    {
      setAlarm(over_load);
      return;
    }
    i_val += anValue;
  } // end current reading cycle
  i_val /= IREADINGS; // average

  // calibration mode=>store this value as ZERO current
  if (calibration)
  {
    data->currentCalibration = i_val;
    return; // exit from function
  }
  else // not calibration
  {
    // normally you subtract an 2.5V offset, we prefer read the standby value and subtract this
    // that would be around 2.5V
    i_val = i_val - data->currentCalibration;
    // turn negative value in positive since we're working only on DC
    // and can happen we wired the current sensor in inverse way
    if (i_val < 0) i_val *= -1;
    // compute current value in Ampere
    i_val *= ADCCONV; // ADC to voltage in V
    i_val /= ACSCONST; // Voltage to Ampere using the sensor constant
    i_val = CURR_ADJ(i_val); // user adjustment
    if (i_val < 0) i_val = 0; // if user adjustment makes value less than zero, turn to zero
    data->current = i_val;
  }

  // read voltage too if not in calibration mode
  if (!calibration)
  {
    for (i = 0; i < VREADINGS; i++)
    {
      anValue = analogRead(VOLTAGE_SENSE);
      // quickly check a short-circuit
      // module can't output less than 1.25V (27 from ADC ~= 1V)
      if (anValue < 27)
      {
        setAlarm(short_circuit);
        return;
      }
      // quickly check an over-voltage. Dunno if useful or not, maybe a broken switching module can output voltage unregulated?
      if (anValue > ADCVOLTAGEALARM)
      {
        setAlarm(over_voltage);
        return;
      }
      v_val += anValue;
    } // end voltage reading cycle
    v_val /= VREADINGS; // average
    v_val *= ADCCONV;// ADC to voltage in V
    v_val /= DIVIDERCONST; // voltage from divider output to voltage on divider input
    data->voltage = VOLT_ADJ(v_val); // user adjustment
    data->power = data->voltage * data->current; // active power
  } // not current calibration => read voltage too
} // \computeReadings

void outputEnable(void)
{
  // toggle relay allowing power output
  outputEnabled = true;
  digitalWrite(RELAY, RELAYON);
}

void outputDisable(void)
{
  // toggle relay disabling power output
  outputEnabled = false;
  digitalWrite(RELAY, !RELAYON);
}

void outputIcon(void)
  {
  if (outputEnabled)
    {
    ucg.setColor(0, 15, 255, 15); // green  
    }
  else
    {
    if (alarm==no_alarm)
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
  
void buttonPress(void)
{
  // ISR at button pressing
  noInterrupts();
  static long lastPress = 0;
  if ((millis() - lastPress) < 200)
  {
    // exit if <200mS press
    lastPress = millis();
    interrupts();
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
  outputEnabled ? outputDisable() : outputEnable();
  while (!digitalRead(BUTTON)); // eventually stay stuck until button released
  outputIcon();
  lastPress = millis();
  interrupts();
}

void setAlarm(alarmType alm)
{
  alarm = alm; // alarm is global
  outputDisable();
  outputIcon();
}

void setup(void)
{
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
  ucg.setPrintPos(10, 25);
  ucg.print("SUPPLINO ");
  ucg.print(SUPPLINO_VERSION);
  ucg.setPrintPos(10, 47);
  ucg.print("CyB3rn0id");
  ucg.setPrintPos(10, 69);
  ucg.print("MrLoba81");

  // calibrate zero current value and store in currentCalibration variable
  computeReadings(&data, true);

  delay(1500);

  // paint screen in black
  ucg.setColor(0, 0, 0, 0);
  ucg.drawBox(0, 0, ucg.getWidth(), ucg.getHeight());
  
  #ifdef GAUGE_I
   gauge.drawGauge(G_X, G_Y, G_ARC, G_RADIUS, 5, 15, 0, CURRENTMAX, 0, CURRENTMAX - 1, CURRENTMAX - 1);
  #else
   #ifdef GAUGE_W
     gauge.drawGauge(G_X, G_Y, G_ARC, G_RADIUS, 5, 15, 0, POWERMAX, 0, POWERMAX-1, POWERMAX-1); 
   #else // default : voltage
     gauge.drawGauge(G_X, G_Y, G_ARC, G_RADIUS, 5, 15, 0, VOLTAGEMAX, 0, 0, 0);  
    #endif
  #endif
  
  ucg.setColor(0, 255, 255, 255); // foreground color
  ucg.setColor(1, 0, 0, 0); // background color
  // lines 
  ucg.drawHLine(0, 60, 160);
  ucg.drawVLine(53, 0, 60);
  ucg.drawVLine(107, 0, 60);
  // border 
  ucg.drawHLine(0, 0, 160);
  ucg.drawHLine(0, 127, 160);
  ucg.drawVLine(0, 0, 128);
  ucg.drawVLine(159, 0, 128);
  // led border
  ucg.setColor(0, 255, 255, 255); 
  ucg.drawDisc(135, 94, 17, UCG_DRAW_ALL);
  // enable interrupt on button pressing
  attachInterrupt(digitalPinToInterrupt(BUTTON), buttonPress, FALLING);
}

void loop(void)
{
  // re-set font since gauge uses another kind of font
  ucg.setFontMode(UCG_FONT_MODE_SOLID); // solid: background will painted
  ucg.setFont(ucg_font_logisoso16_hr); // font (https://github.com/olikraus/ucglib/wiki/fontsize)
  ucg.setPrintDir(0);
  
  // update values only if not in alarm mode
  if (alarm == no_alarm) computeReadings(&data, false);

  printValues();
  updateGauge();
}

void updateGauge(void)
{
  ucg.setColor(0, 90, 90, 90); // default color is gray
#ifdef GAUGE_I
   gauge.drawPointer(G_X, G_Y, G_ARC, G_RADIUS, data.current, 0, CURRENTMAX);
   ucg.setFontMode(UCG_FONT_MODE_TRANSPARENT);
   ucg.setFont(ucg_font_orgv01_hf);
   ucg.setPrintPos(G_X-16, G_Y);
   if (outputEnabled) SET_I_COLOR
   ucg.print("AMPERE");
  #else
   #ifdef GAUGE_W
     gauge.drawPointer(G_X, G_Y, G_ARC, G_RADIUS, data.power, 0, POWERMAX);
     ucg.setFontMode(UCG_FONT_MODE_TRANSPARENT);
     ucg.setFont(ucg_font_orgv01_hf);
     ucg.setPrintPos(G_X-14, G_Y);
     if (outputEnabled) SET_W_COLOR
     ucg.print("WATTS");
   #else // default : voltageè piu da 
     gauge.drawPointer(G_X, G_Y, G_ARC, G_RADIUS, data.voltage, 0, VOLTAGEMAX);
     ucg.setFontMode(UCG_FONT_MODE_TRANSPARENT);
     ucg.setFont(ucg_font_orgv01_hf);
     ucg.setPrintPos(G_X-14, G_Y);
     if (outputEnabled) SET_V_COLOR
     ucg.print("VOLTS");
   #endif
  #endif
}

void printValues()
{ 
  ucg.setColor(0, 90, 90, 90); // default color is gray
  
  if (outputEnabled) SET_V_COLOR
  ucg.setPrintPos(9, 29);
  ucg.print(data.voltage, data.voltage>=10?1:2);
  if (data.voltage>=10) ucg.print(" ");
  ucg.setPrintPos(22, 50);
  ucg.print("V");

  if (outputEnabled) SET_I_COLOR
  ucg.setPrintPos(63, 29);
  ucg.print(data.current, data.current>=10?1:2);
  if (data.current>=10) ucg.print(" ");
  ucg.setPrintPos(75, 50);
  ucg.print("A");

  if (outputEnabled) SET_W_COLOR
  ucg.setPrintPos(117, 29);
  ucg.print(data.power, data.power>=10?1:2);
  if (data.power>=10) ucg.print(" ");
  ucg.setPrintPos(128, 50);
  ucg.print("W");
}
