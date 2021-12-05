/*
 SUPPLINO
 quick&dirty PSU
 by @cyb3rn0id and @mrloba81
 https://www.github.com/settorezero/supplino

  WORK IN PROGRESS, CODE NOT WORKING
*/

#include <SPI.h>
#include "Ucglib.h" // Ucglib by Oliver
#include "ACS712.h" // ACS712 by Rob Tillaart, Pete Thompson

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

#define VOLTAGE_SENSE A0 // voltage divider on A0 for reading voltage output from switching regulator
#define CURRENT_SENSE A1 // analog output from ACS712 current sensor
#define RELAY 4 // relay attached on D4
#define BUTTON 3 // button attached on D3 (interrupt)
bool alarm=false;

#define READINGS   10   // number of analog readings
#define CURRENTMAX 5000 // mA
#define VOLTAGEMAX 40   // V

// constants used for the first gauge
#define G1_X 56
#define G1_Y 64
#define G1_ARC 120
#define G1_RADIUS 30

// constants used for the second gauge
#define G2_X 56
#define G2_Y 127
#define G2_ARC 120
#define G2_RADIUS 30

#define c 0.0174532925F // pi/180 (for grad to rad conversion)

ACS712  ACS(CURRENT_SENSE, 5.0, 1023, 100); // current sensor on A1, voltage reference 5V, 1023 max from adc, 100mV/A sensor used

struct DrawContext 
  {
  float old_val;
  float ltx; // Saved x coord of bottom of pointer
  uint16_t osx;
  uint16_t osy;
  bool first_start;
  } voltageCtx, currentCtx;

void setup(void)
  {
  Serial.begin(9600);
  voltageCtx.old_val = -999;
  voltageCtx.ltx = 0; // Saved x coord of bottom of pointer
  voltageCtx.osx = 0;
  voltageCtx.osy = 0;
  voltageCtx.first_start = true;

  currentCtx.old_val = -999;
  currentCtx.ltx = 0; // Saved x coord of bottom of pointer
  currentCtx.osx = 0;
  currentCtx.osy = 0;
  currentCtx.first_start = true;

  // measure zero current
  ACS.autoMidPoint();

  delay(1000);
  ucg.begin(UCG_FONT_MODE_TRANSPARENT);
  ucg.clearScreen();
  ucg.setRotate270(); // 90=display horizontal (landscape) with data header on the left

  // paint screen in black
  ucg.setColor(0, 0, 0, 0);
  ucg.drawBox(0, 0, ucg.getWidth(), ucg.getHeight());

  // draw the two gauges
  // void drawGauge(uint8_t x, uint8_t y, uint8_t arc, uint8_t radius, uint8_t stp, uint8_t tickl, float gaugemin, float gaugemax, uint8_t decimals, float gz, float yz)
  drawGauge(G1_X, G1_Y, G1_ARC, G1_RADIUS, 5, 15, 0, VOLTAGEMAX, 0, 0, 0);
  drawGauge(G2_X, G2_Y, G2_ARC, G2_RADIUS, 5, 15, 0, CURRENTMAX/1000, 0, CURRENTMAX-1000, CURRENTMAX-1000);
  }

void loop(void)
  {
  float voltage = 0;
  float current = 0;
  uint8_t i = 0;

  for (i = 0; i < READINGS; i++)
    {
    voltage += analogRead(A0);
    current += ACS.mA_DC();
    delay(5);
    }

  voltage /= READINGS;
  current /= READINGS;

  voltage *= .00488; // ADC to voltage (5/1023)
  voltage /= .12795; // voltage from divider to voltage on divider input (R1=68K, R2=10K)
  voltage /= 1.121;  // linear regression I made for correct some drift on my circuit

  //current *= .0488; // ADC to voltage (5/1023) *10
  //current -= 25; // voltage to current *10 (ACS712 2.5V offset, 20A model has 100mV/A resolution)
  if (current < 0) current *= -1; // turn to positive

  drawPointer(voltageCtx, G1_X, G1_Y, G1_ARC, G1_RADIUS, voltage, 0, VOLTAGEMAX);
  drawPointer(currentCtx, G2_X, G2_Y, G2_ARC, G2_RADIUS, current/1000, 0, CURRENTMAX);

  // set font
  ucg.setColor(0, 255, 255, 255); // foreground color
  ucg.setColor(1, 0, 0, 0); // background color
  ucg.setFontMode(UCG_FONT_MODE_SOLID); // solid: background will painted
  ucg.setFont(ucg_font_logisoso18_hr); // font (https://github.com/olikraus/ucglib/wiki/fontsize)
  ucg.setPrintDir(0);

  // write values
  ucg.setPrintPos(G1_X + G1_RADIUS + 27, G1_Y - 33); // 23
  ucg.print(voltage, 1);
  ucg.print("  ");
  ucg.setPrintPos(G1_X + G1_RADIUS + 27, G1_Y - 10);
  ucg.print("V");


  ucg.setPrintPos(G2_X + G2_RADIUS + 27, G2_Y - 33);
  if (current > CURRENTMAX)
    {
    ucg.setColor(0, 255, 0, 0); // foreground color
    ucg.print("HIGH ");
    ucg.setPrintPos(G2_X + G2_RADIUS + 27, G2_Y - 10);
    ucg.print("    ");
    }
  else
    {
    ucg.setColor(0, 255, 255, 255); // foreground color
    ucg.print(current, 0);
    ucg.print("  ");
    ucg.setPrintPos(G2_X + G2_RADIUS + 27, G2_Y - 10);
    ucg.print("mA");
    }
  }

void drawGauge(uint8_t x, uint8_t y, uint8_t arc, uint8_t radius, uint8_t stp, uint8_t tickl, float gaugemin, float gaugemax, uint8_t decimals, float gz, float yz)
  {
  int amin = -((int)arc / 2);
  int amax = (arc / 2) + 1;
  // Draw ticks every 'stp' degrees from -(arc/2) to (arc/2) degrees
  for (int i = amin; i < amax; i += stp)
    {
    // Calculating coodinates of tick to draw
    // at this time will be used only for drawing the green/yellow/red zones
    float sx = cos((i - 90) * c);
    float sy = sin((i - 90) * c);
    uint16_t x0 = sx * (radius + tickl) + x;
    uint16_t y0 = sy * (radius + tickl) + y;
    uint16_t x1 = sx * radius + x;
    uint16_t y1 = sy * radius + y;

    // Coordinates of next tick for zone fill
    float sx2 = cos((i + stp - 90) * c);
    float sy2 = sin((i + stp - 90) * c);
    int x2 = sx2 * (radius + tickl) + x;
    int y2 = sy2 * (radius + tickl) + y;
    int x3 = sx2 * radius + x;
    int y3 = sy2 * radius + y;

    // calculate angles for green and yellow zones
    int ga=0;
    int ya=0;
    if (gz>gaugemin) ga=(int)lmap(gz, gaugemin, gaugemax, amin, amax); // green zone will be between gaugemin and GZ
    if (yz>gaugemin) ya=(int)lmap(yz, gaugemin, gaugemax, amin, amax); // yellow zone will be between GZ and YZ
    // red zone will be between YZ and gaugemax

    // green zone
    if ((gz>=gaugemin) && (gz<yz))
      {
      ucg.setColor(0, 0, 255, 0);
      if (i >= amin && i < ga)
        {
        ucg.drawTriangle(x0, y0, x1, y1, x2, y2);
        ucg.drawTriangle(x1, y1, x2, y2, x3, y3);
        }
      }
    // yellow zone
    if ((yz>gz) && (yz<=gaugemax))
      {
      ucg.setColor(0, 255, 255, 0);
      if (i >= ga && i <ya)
        {
        ucg.drawTriangle(x0, y0, x1, y1, x2, y2);
        ucg.drawTriangle(x1, y1, x2, y2, x3, y3);
        }
      }
      
    // red zone
    if ((yz>gaugemin) && (yz>=gz) && (yz<=gaugemax))
      {
      ucg.setColor(0, 255, 0, 0);
      if (i >= ya && i < amax-1)
        {
        ucg.drawTriangle(x0, y0, x1, y1, x2, y2);
        ucg.drawTriangle(x1, y1, x2, y2, x3, y3);
        }
      }
    // now we can draw ticks, above colored zones
  
    // Check if this is a "Short" scale tick
    uint8_t tl = tickl;
    if (i % (arc / 4) != 0) tl = (tickl / 2) + 1;

    // Recalculate coords in case tick length is changed
    x0 = sx * (radius + tl) + x;
    y0 = sy * (radius + tl) + y;
    x1 = sx * radius + x;
    y1 = sy * radius + y;

    // Draw tick
    ucg.setColor(0, 255, 255, 255);
    ucg.drawLine(x0, y0, x1, y1);

    // Check if labels should be drawn
    if (i % (arc / 4) == 0) // gauge will have 5 main ticks at 0, 25, 50, 75 and 100% of the scale
      {
      // Calculate label positions
      x0 = sx * (radius + tl + 10) + x - 5; // 10, 5 are my offset for the font I've used for labels
      y0 = sy * (radius + tl + 10) + y + 5;

      // print label positions
      ucg.setFontMode(UCG_FONT_MODE_TRANSPARENT);
      ucg.setFont(ucg_font_orgv01_hf);
      ucg.setColor(0, 255, 255, 255);
      ucg.setPrintPos(x0, y0);
      float cent = (gaugemax + gaugemin) / 2; // center point of the gauge
      switch (i / (arc / 4))
        {
        case -2: ucg.print(gaugemin, decimals); break; // 0%=gaugemin
        case -1: ucg.print((cent + gaugemin) / 2, decimals); break; // 25%
        case  0: ucg.print(cent, decimals); break; // central point // 50%
        case  1: ucg.print((cent + gaugemax) / 2, decimals); break; // 75%
        case  2: ucg.print(gaugemax, decimals); break; // 100%=gaugemax
        }
      } // check if I must print labels

    // Now draw the arc of the scale
    sx = cos((i + stp - 90) * c);
    sy = sin((i + stp - 90) * c);
    x0 = sx * radius + x;
    y0 = sy * radius + y;
    ucg.setColor(0, 255, 255, 255);
    if (i < (arc / 2)) ucg.drawLine(x0, y0, x1, y1); // don't draw the last part
    }
  }

void drawPointer(DrawContext &ctx, uint8_t x, uint8_t y, uint8_t arc, uint8_t radius, float value, float minval, float maxval)
  {
  uint8_t nl = 10; // pointer "length" (0=longest)
  // quick exit if value to paint is about the same for avoiding pointer flickering
  if ((value <= ctx.old_val + .05) && (value >= ctx.old_val - .05)) return;
  ctx.old_val = value;

  if (value < minval) value = minval;
  if (value > maxval) value = maxval;

  float sdeg = lmap(value, minval, maxval, (-(arc / 2) - 90), ((arc / 2) - 90)); // Map value to angle
  // Calculate tip of pointer coords
  float sx = cos(sdeg * c);
  float sy = sin(sdeg * c);

  // Calculate x delta of needle start (does not start at pivot point)
  float tx = tan((sdeg + 90) * c);

  // Erase old needle image if not first time pointer is drawn
  if (!ctx.first_start)
    {
    ucg.setColor(0, 0, 0, 0);
    ucg.drawLine(x + nl * ctx.ltx - 1, y - nl, ctx.osx - 1, ctx.osy);
    ucg.drawLine(x + nl * ctx.ltx, y - nl, ctx.osx, ctx.osy);
    ucg.drawLine(x + nl * ctx.ltx + 1, y - nl, ctx.osx + 1, ctx.osy);
    }
  ctx.first_start = false;

  // Store new pointer end coords for next erase
  ctx.ltx = tx;
  ctx.osx = sx * (radius - 2) + x;
  ctx.osy = sy * (radius - 2) + y;

  // Draw the pointer in the new position
  // draws 3 lines to thicken needle
  ucg.setColor(0, 255, 255, 255);
  ucg.drawLine(x + nl * ctx.ltx - 1, y - nl, ctx.osx - 1, ctx.osy);
  ucg.drawLine(x + nl * ctx.ltx, y - nl, ctx.osx, ctx.osy);
  ucg.drawLine(x + nl * ctx.ltx + 1, y - nl, ctx.osx + 1, ctx.osy);
  }

// Arduino "MAP" function but rewrited for using float numbers
float lmap(float x, float in_min, float in_max, float out_min, float out_max)
  {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
  }
