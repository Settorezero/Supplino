/*
Draw 2 parametric analog gauges using the UCGlibrary by Oliver
derived from https://github.com/Cyb3rn0id/Analog_Gauge_Arduino
derived from a "Bodmer" instructable : https://www.instructables.com/Arduino-sketch-for-a-retro-analogue-meter-graphic-/
*/

#include "gauge.hpp"

Gauge::Gauge(Ucglib *ucg) : ucg(ucg), old_val(-999), ltx(0), osx(0), osy(0), first_start(true)
{
}

void Gauge::drawGauge(uint8_t x, uint8_t y, uint8_t arc, uint8_t radius, uint8_t stp, uint8_t tickl, float gaugemin, float gaugemax, uint8_t decimals, float gz, float yz)
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
        int ga = 0;
        int ya = 0;
        if (gz > gaugemin)
            ga = (int)lmap(gz, gaugemin, gaugemax, amin, amax); // green zone will be between gaugemin and GZ
        if (yz > gaugemin)
            ya = (int)lmap(yz, gaugemin, gaugemax, amin, amax); // yellow zone will be between GZ and YZ
        // red zone will be between YZ and gaugemax

        // green zone
        if ((gz >= gaugemin) && (gz < yz))
        {
            this->ucg->setColor(0, 0, 255, 0);
            if (i >= amin && i < ga)
            {
                this->ucg->drawTriangle(x0, y0, x1, y1, x2, y2);
                this->ucg->drawTriangle(x1, y1, x2, y2, x3, y3);
            }
        }
        // yellow zone
        if ((yz > gz) && (yz <= gaugemax))
        {
            this->ucg->setColor(0, 255, 255, 0);
            if (i >= ga && i < ya)
            {
                this->ucg->drawTriangle(x0, y0, x1, y1, x2, y2);
                this->ucg->drawTriangle(x1, y1, x2, y2, x3, y3);
            }
        }

        // red zone
        if ((yz > gaugemin) && (yz >= gz) && (yz <= gaugemax))
        {
            this->ucg->setColor(0, 255, 0, 0);
            if (i >= ya && i < amax - 1)
            {
                this->ucg->drawTriangle(x0, y0, x1, y1, x2, y2);
                this->ucg->drawTriangle(x1, y1, x2, y2, x3, y3);
            }
        }
        // now we can draw ticks, above colored zones

        // Check if this is a "Short" scale tick
        uint8_t tl = tickl;
        if (i % (arc / 4) != 0)
            tl = (tickl / 2) + 1;

        // Recalculate coords in case tick length is changed
        x0 = sx * (radius + tl) + x;
        y0 = sy * (radius + tl) + y;
        x1 = sx * radius + x;
        y1 = sy * radius + y;

        // Draw tick
        this->ucg->setColor(0, 255, 255, 255);
        this->ucg->drawLine(x0, y0, x1, y1);

        // Check if labels should be drawn
        if (i % (arc / 4) == 0) // gauge will have 5 main ticks at 0, 25, 50, 75 and 100% of the scale
        {
            // Calculate label positions
            x0 = sx * (radius + tl + 10) + x - 5; // 10, 5 are my offset for the font I've used for labels
            y0 = sy * (radius + tl + 10) + y + 5;

            // print label positions
            this->ucg->setFontMode(UCG_FONT_MODE_TRANSPARENT);
            this->ucg->setFont(ucg_font_orgv01_hf);
            this->ucg->setColor(0, 255, 255, 255);
            this->ucg->setPrintPos(x0, y0);
            float cent = (gaugemax + gaugemin) / 2; // center point of the gauge
            switch (i / (arc / 4))
            {
            case -2:
                this->ucg->print(gaugemin, decimals);
                break; // 0%=gaugemin
            case -1:
                this->ucg->print((cent + gaugemin) / 2, decimals);
                break; // 25%
            case 0:
                this->ucg->print(cent, decimals);
                break; // central point // 50%
            case 1:
                this->ucg->print((cent + gaugemax) / 2, decimals);
                break; // 75%
            case 2:
                this->ucg->print(gaugemax, decimals);
                break; // 100%=gaugemax
            }
        } // check if I must print labels

        // Now draw the arc of the scale
        sx = cos((i + stp - 90) * c);
        sy = sin((i + stp - 90) * c);
        x0 = sx * radius + x;
        y0 = sy * radius + y;
        this->ucg->setColor(0, 255, 255, 255);
        if (i < (arc / 2))
            this->ucg->drawLine(x0, y0, x1, y1); // don't draw the last part
    }
}

void Gauge::drawPointer(uint8_t x, uint8_t y, uint8_t arc, uint8_t radius, float value, float minval, float maxval)
{
  uint8_t nl = 10; // pointer "length" (0=longest)
  // quick exit if value to paint is about the same for avoiding pointer flickering
  if ((value <= this->old_val + .05) && (value >= this->old_val - .05)) return;
  this->old_val = value;

  if (value < minval) value = minval;
  if (value > maxval) value = maxval;

  float sdeg = lmap(value, minval, maxval, (-(arc / 2) - 90), ((arc / 2) - 90)); // Map value to angle
  // Calculate tip of pointer coords
  float sx = cos(sdeg * c);
  float sy = sin(sdeg * c);

  // Calculate x delta of needle start (does not start at pivot point)
  float tx = tan((sdeg + 90) * c);

  // Erase old needle image if not first time pointer is drawn
  if (!this->first_start)
  {
    this->ucg->setColor(0, 0, 0, 0);
    this->ucg->drawLine(x + nl * this->ltx - 1, y - nl, this->osx - 1, this->osy);
    this->ucg->drawLine(x + nl * this->ltx, y - nl, this->osx, this->osy);
    this->ucg->drawLine(x + nl * this->ltx + 1, y - nl, this->osx + 1, this->osy);
  }
  this->first_start = false;

  // Store new pointer end coords for next erase
  this->ltx = tx;
  this->osx = sx * (radius - 2) + x;
  this->osy = sy * (radius - 2) + y;

  // Draw the pointer in the new position
  // draws 3 lines to thicken needle
  this->ucg->setColor(0, 255, 255, 255);
  this->ucg->drawLine(x + nl * this->ltx - 1, y - nl, this->osx - 1, this->osy);
  this->ucg->drawLine(x + nl * this->ltx, y - nl, this->osx, this->osy);
  this->ucg->drawLine(x + nl * this->ltx + 1, y - nl, this->osx + 1, this->osy);
}

// Arduino "MAP" function but rewrited for using float numbers
float Gauge::lmap(float x, float in_min, float in_max, float out_min, float out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}