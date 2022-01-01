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
// note: XL4016E1 IC accept from 4 to 40V at input, and outputs from 1.25 to 36V
#define RELAYON     LOW // relay module used is active low => turns on when low level is sent
#define IREADINGS   200 // number of analog readings for current
#define VREADINGS    20 // number of analog readings for voltage
#define CURRENTMAX    5 // A
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
#define VOLT_ADJ(V)  V // for no correction
//#define VOLT_ADJ(V) V+0.1 // your own formula

// current adjustment
// #define CURR_ADJ(I) I // for no correction
#define CURR_ADJ(I) I-0.09 // your own formula

Gauge gauge(&ucg); // analog gauge instance
// constants used for the analog gauge
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

enum gaugeType {
   V, 
   I, 
   W
   };

struct readData {
  float voltage = 0;
  float current = 0;
  float power = 0;
  float currentCalibration = 512; // ADC zero offset for ACS current sensor (=2.5V)
} data;

alarmType alarm = no_alarm;
gaugeType showedGauge=V;
volatile bool outputEnabled = false; // output relay detached

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

// Interrupt Service Routine at button low level
void ISR_buttonPress(void)
{
  noInterrupts(); // disable interrupts
  static long lastPress = 0;
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
  while (!digitalRead(BUTTON)); // eventually stay stuck until button released
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

  // calibrate zero current value and store in currentCalibration struct variable
  computeReadings(&data, true);

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
  
  // enable interrupt on button pressing
  attachInterrupt(digitalPinToInterrupt(BUTTON), ISR_buttonPress, FALLING);
}


void loop(void)
{
  // re-set font since gauge uses another kind of font
  ucg.setFontMode(UCG_FONT_MODE_SOLID); // solid: background will painted
  ucg.setFont(ucg_font_logisoso16_hr); // font (https://github.com/olikraus/ucglib/wiki/fontsize)
  ucg.setPrintDir(0);

  // update values 
  computeReadings(&data, false);
  printValues();
  updateGauge();
  outputIcon();
}

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
    // quickly detect an overload. This occurs also in case of a short-circuit
    if (anValue > data->currentCalibration + ADC_CURRENTALARM)
    {
      setAlarm(over_load);
      return; // exit from function
    }
    i_val += anValue;
  } // end current reading cycle
  i_val /= IREADINGS; // average

  // calibration mode=>store average value from ADC as ZERO (OFFSET) current
  if (calibration)
  {
    data->currentCalibration = i_val;
    return; // exit from function
  }
  else // is not calibration
  {
    // normally you'll subtract an 2.5V offset, but better read the standby value (previous functions) 
    // and then subtract that value (anyway in good conditions would be around 2.5V => around 512)
    i_val = i_val - data->currentCalibration;
    // turn (eventually) negative value in positive since we're working only on DC
    // and can happen we wired the current sensor in inverse way
    if (i_val < 0) i_val *= -1;
    // compute current value in Ampere
    i_val *= ADCCONV; // ADC to voltage in V
    i_val /= ACSCONST; // Voltage to Ampere using the sensor constant
    i_val = CURR_ADJ(i_val); // user adjustment
    if (i_val < 0) i_val = 0; // if user adjustment makes value less than zero, turn to zero
    data->current = i_val; // store value for gauges
  }

  // read voltage (and compute power) too if not in calibration mode
  if (!calibration)
  {
    for (i = 0; i < VREADINGS; i++)
    {
      anValue = analogRead(VOLTAGE_SENSE);
      // quickly check a short-circuit
      if (anValue < ADC_UNDERVOLTAGEALARM)
      {
        setAlarm(short_circuit);
        return;
      }
      // quickly check an over-voltage. Dunno if useful or not, maybe a broken switching module can output voltage unregulated?
      if (anValue > ADC_OVERVOLTAGEALARM)
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

// draw Analog gauge scale
void drawGaugeScale(void)
  {
  switch (showedGauge)
    {
    case I:
      gauge.drawGauge(G_X, G_Y, G_ARC, G_RADIUS, 5, 15, 0, CURRENTMAX, 0, CURRENTMAX - 1, CURRENTMAX - 1);
      break;
    case W:
      gauge.drawGauge(G_X, G_Y, G_ARC, G_RADIUS, 5, 15, 0, POWERMAX, 0, POWERMAX - 1, POWERMAX - 1);
      break;
    default:
      gauge.drawGauge(G_X, G_Y, G_ARC, G_RADIUS, 5, 15, 0, VOLTAGEMAX, 0, 0, 0);
      break;
    }
  }

// update analog gauge pointer
void updateGauge(void)
{
  switch (showedGauge)
    {
    case I:
      gauge.drawPointer(G_X, G_Y, G_ARC, G_RADIUS, data.current, 0, CURRENTMAX);
      ucg.setFontMode(UCG_FONT_MODE_SOLID);
      ucg.setFont(ucg_font_orgv01_hf);
      ucg.setPrintPos(G_X - 16, G_Y);
      outputEnabled?(SET_I_COLOR):ucg.setColor(0, 90, 90, 90);
      ucg.print("AMPERE");
      break;
    
    case W:
      gauge.drawPointer(G_X, G_Y, G_ARC, G_RADIUS, data.power, 0, POWERMAX);
      ucg.setFontMode(UCG_FONT_MODE_SOLID);
      ucg.setFont(ucg_font_orgv01_hf);
      ucg.setPrintPos(G_X - 14, G_Y);
      outputEnabled?(SET_W_COLOR):ucg.setColor(0, 90, 90, 90);
      ucg.print("WATTS");
      break;
      
    default:
      gauge.drawPointer(G_X, G_Y, G_ARC, G_RADIUS, data.voltage, 0, VOLTAGEMAX);
      ucg.setFontMode(UCG_FONT_MODE_SOLID);
      ucg.setFont(ucg_font_orgv01_hf);
      ucg.setPrintPos(G_X - 14, G_Y);
      outputEnabled?(SET_V_COLOR):ucg.setColor(0, 90, 90, 90);
      ucg.print("VOLTS");
      break;
    }
}

// print digital values
void printValues()
{
  static float preV=0;
  // draw a black box passing from a value >10 to a value<10
  ucg.setColor(0, 0, 0, 0);
  ucg.setColor(1, 0, 0, 0);
  if (preV>=10.0 && data.voltage<10.0) ucg.drawBox(1, 12, 51, 18);
  
  ucg.setColor(0, 90, 90, 90); // default color is gray
  if (outputEnabled) SET_V_COLOR;
  data.voltage<10.0?ucg.setPrintPos(14,29):ucg.setPrintPos(8, 29);
  ucg.print(data.voltage, 1);
  ucg.setPrintPos(22, 50);
  ucg.print("V");
  preV=data.voltage;

  if (outputEnabled) SET_I_COLOR;
  data.current<10.0?ucg.setPrintPos(63, 29):ucg.setPrintPos(60, 29);
  ucg.print(data.current, data.current >= 10.0 ? 1 : 2);
  if (data.current >= 10.0) ucg.print(" ");
  ucg.setPrintPos(75, 50);
  ucg.print("A");

  if (outputEnabled) SET_W_COLOR;
  data.power<10.0?ucg.setPrintPos(116, 29):ucg.setPrintPos(113, 29);
  ucg.print(data.power, data.power >= 10.0 ? 1 : 2);
  if (data.power >= 10.0) ucg.print(" ");
  ucg.setPrintPos(128, 50);
  ucg.print("W");
}

// draw round icon indicating power output status
void outputIcon(void)
{
  if (outputEnabled)
  {
    ucg.setColor(0, 15, 255, 15); // green
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
