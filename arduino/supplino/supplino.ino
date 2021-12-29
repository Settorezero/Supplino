/*
 SUPPLINO
 quick&dirty PSU
 by @cyb3rn0id and @mrloba81
 https://www.github.com/settorezero/supplino
*/

#include <Arduino.h>
#include <SPI.h>
#include "Ucglib.h" // Ucglib by Oliver
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
// other macros
#define RELAYON      LOW // relay module used is active low => turns on when low level is sent
#define IREADINGS   200  // number of analog readings for current
#define VREADINGS    20  // number of analog readings for voltage
#define CURRENTMAX    5  // A
#define VOLTAGEMAX   30  // V
#define DIVIDERCONST 0.12795F // voltage from divider to voltage on divider input (R1=68K, R2=10K)
#define ADCCONV      0.00488F // 5/1024, used for conversion from ADC to voltage
#define ACSCONST     0.1F  // used 20A type that gives 100mV (0.1V) per 1 Ampere
// Inverse formula for having ADC value from CURRENTMAX value
// currentCalibration value has to be added to this
// Current= [(ADCvalue-currentCalibration) * (5/1024)] / 0.1
// ADCvalue = [(Current * 0.1) / (5/1024)] + currentCalibration
#define ADCCURRENTALARM int(CURRENTMAX*ACSCONST/ADCCONV)
// Inverse formula for having ADC value from VOLTAGEMAX value
#define ADCVOLTAGEALARM int(VOLTAGEMAX*DIVIDERCONST/ADCCONV)

// constants used for the first gauge (voltage)
#define G1_X 56
#define G1_Y 64
#define G1_ARC 120
#define G1_RADIUS 30
Gauge voltGauge(&ucg);

// constants used for the second gauge (current)
#define G2_X 56
#define G2_Y 127
#define G2_ARC 120
#define G2_RADIUS 30
Gauge ampGauge(&ucg);

enum alarmType {
  no_alarm,
  over_load,
  over_voltage,
  short_circuit,
  };

struct readData {
  float voltage=0;
  float current=0;
  int8_t currentDecimals=1;
  float currentCalibration=512; // ADC zero offset for current sensor (2.5V)
} data;

alarmType alarm=no_alarm;
volatile bool outputEnabled = false;

// compute readings fuction
// calibration=true => don't read voltage and store zero current value (calibration)
// calibration=false =>  read voltage too and use current calibration value to adjust the current value
void computeReadings(readData *data, bool calibration) 
  {
  float i_val = 0.0; // current value (Ampere)
  float v_val = 0.0 ; // voltage value (Volt)
  uint16_t anValue = 0; // ADC raw readings from ACS sensor and from voltage divider
  uint8_t i=0; // generic counter
  
  if (calibration) outputDisable(); // disable output if in current calibration
  
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
    if (i_val<0) i_val *=-1;
    // compute current value in Ampere
    i_val *= ADCCONV; // ADC to voltage in V
    i_val /= ACSCONST; // Voltage to Ampere using the sensor constant
    data->current = i_val;
    // set 1 decimal if current >1A, 2 decimals if lower
    data->currentDecimals=i_val>1?1:2;
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
    data->voltage = v_val;
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
  if (alarm!=no_alarm)
  {
    alarm = no_alarm; // reset alarm status...
    lastPress = millis();
    interrupts();
    return; // ... but remain with relay off
  }
  outputEnabled ? outputDisable() : outputEnable();
  while (!digitalRead(BUTTON)); // eventually stay stuck until button released
  lastPress = millis();
  interrupts();
}

void setAlarm(alarmType alm)
{
  outputDisable();
  alarm = alm; // alarm is global
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

  // draw the two gauges
  // void drawGauge(uint8_t x, uint8_t y, uint8_t arc, uint8_t radius, uint8_t stp, uint8_t tickl, float gaugemin, float gaugemax, uint8_t decimals, float gz, float yz)
  voltGauge.drawGauge(G1_X, G1_Y, G1_ARC, G1_RADIUS, 5, 15, 0, VOLTAGEMAX, 0, 0, 0);
  ampGauge.drawGauge(G2_X, G2_Y, G2_ARC, G2_RADIUS, 5, 15, 0, CURRENTMAX, 0, CURRENTMAX - 1, CURRENTMAX - 1);

  // calibrate zero current value and store in currentCalibration variable
  computeReadings(&data, true);
      
  // set font
  ucg.setColor(0, 255, 255, 255); // foreground color
  ucg.setColor(1, 0, 0, 0); // background color
  ucg.setFontMode(UCG_FONT_MODE_SOLID); // solid: background will painted
  ucg.setFont(ucg_font_logisoso16_hr); // font (https://github.com/olikraus/ucglib/wiki/fontsize)
  ucg.setPrintDir(0);
   
  // enable interrupt on button pressing
  attachInterrupt(digitalPinToInterrupt(BUTTON), buttonPress, FALLING);
}

void loop(void)
{
  // update values only if not in alarm mode
  if (alarm==no_alarm)
    {
    computeReadings(&data, false);
    }
  writeVoltage();
  writeCurrent();
}

void writeVoltage(void)
  {
  // pointer
  voltGauge.drawPointer(G1_X, G1_Y, G1_ARC, G1_RADIUS, data.voltage, 0, VOLTAGEMAX);
  // numeric value
  if (outputEnabled){
     ucg.setColor(0, 0, 255, 0); // green : power output on
     }
  else {
    if ((alarm==over_voltage) || (alarm==short_circuit)) {
       ucg.setColor(0, 255, 0, 0); // red : alarm on voltage
       }
    else {
      ucg.setColor(0, 90, 90, 90); // grey : power output off
      }
     }
  ucg.setPrintPos(G1_X + G1_RADIUS + 27, G1_Y - 33); // 23
  ucg.print(data.voltage, 1);
  ucg.print("  ");
  ucg.setPrintPos(G1_X + G1_RADIUS + 27, G1_Y - 10);
  ucg.print("V");  
  }

void writeCurrent(void)
  {
  // pointer
  ampGauge.drawPointer(G2_X, G2_Y, G2_ARC, G2_RADIUS, data.current, 0, CURRENTMAX);
  // numeric value
  if (outputEnabled){
     ucg.setColor(0, 0, 255, 0); // green : power output on
     }
  else {
    if (alarm==over_load) {
       ucg.setColor(0, 255, 0, 0); // red : alarm on current
       }
    else {
      ucg.setColor(0, 90, 90, 90); // grey : power output off
      }
     }
  ucg.setPrintPos(G2_X + G2_RADIUS + 27, G2_Y - 33);
  ucg.print(data.current, data.currentDecimals);
  ucg.print("  ");
  ucg.setPrintPos(G2_X + G2_RADIUS + 27, G2_Y - 10);
  ucg.print("A"); 
  }
