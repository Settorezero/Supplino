/*
 * Draws analog gauges using the UCGlibrary by Oliver
 * 
 * derived from https://github.com/Cyb3rn0id/Analog_Gauge_Arduino
 * derived from a "Bodmer" instructable : https://www.instructables.com/Arduino-sketch-for-a-retro-analogue-meter-graphic-/
 * 
 * Copyright (c) 2022 Giovanni Bernardo, Paolo Loberto.
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
