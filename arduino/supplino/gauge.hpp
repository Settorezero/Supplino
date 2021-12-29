/*
Draw 2 parametric analog gauges using the UCGlibrary by Oliver
derived from https://github.com/Cyb3rn0id/Analog_Gauge_Arduino
derived from a "Bodmer" instructable : https://www.instructables.com/Arduino-sketch-for-a-retro-analogue-meter-graphic-/
*/

#ifndef GAUGE_H
#define GAUGE_H
#include <Arduino.h>
#include "Ucglib.h"

#define c 0.0174532925F // pi/180 (for grad to rad conversion)

class Gauge {
    public:
        Gauge(Ucglib *ucg);
        void drawGauge(uint8_t x, uint8_t y, uint8_t arc, uint8_t radius, uint8_t stp, uint8_t tickl, float gaugemin, float gaugemax, uint8_t decimals, float gz, float yz);
        void drawPointer(uint8_t x, uint8_t y, uint8_t arc, uint8_t radius, float value, float minval, float maxval);

    private:
        Ucglib *ucg;
        float old_val;
        float ltx; // Saved x coord of bottom of pointer
        uint16_t osx;
        uint16_t osy;
        bool first_start;
        float lmap(float x, float in_min, float in_max, float out_min, float out_max);
};
#endif