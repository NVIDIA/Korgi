/*
* Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include "control_surface_map.h"

#include <map>
#include <string>

#define BUTTON(__channel) ControlSurface(ControlSurface::Type::Button, __channel)
#define SLIDER(__channel) ControlSurface(ControlSurface::Type::Slider, __channel)
#define KNOB(__channel) ControlSurface(ControlSurface::Type::RotaryKnob, __channel)

typedef std::map<std::string, ControlSurface> ControlSurfaceMap;

static ControlSurfaceMap controlMap_Korg_nanoKONTROL2 = {
    { "rewind",         BUTTON(43) },
    { "fwd",            BUTTON(44) },
    { "stop",           BUTTON(42) },
    { "play",           BUTTON(41) },
    { "rec",            BUTTON(45) },
    { "cycle",          BUTTON(46) },
    { "marker_set",     BUTTON(60) },
    { "marker_prev",    BUTTON(61) },
    { "marker_next",    BUTTON(62) },
    { "track_prev",     BUTTON(58) },
    { "track_next",     BUTTON(59) },
    { "S0",             BUTTON(32) },
    { "S1",             BUTTON(33) },
    { "S2",             BUTTON(34) },
    { "S3",             BUTTON(35) },
    { "S4",             BUTTON(36) },
    { "S5",             BUTTON(37) },
    { "S6",             BUTTON(38) },
    { "S7",             BUTTON(39) },
    { "M0",             BUTTON(48) },
    { "M1",             BUTTON(49) },
    { "M2",             BUTTON(50) },
    { "M3",             BUTTON(51) },
    { "M4",             BUTTON(52) },
    { "M5",             BUTTON(53) },
    { "M6",             BUTTON(54) },
    { "M7",             BUTTON(55) },
    { "R0",             BUTTON(64) },
    { "R1",             BUTTON(65) },
    { "R2",             BUTTON(66) },
    { "R3",             BUTTON(67) },
    { "R4",             BUTTON(68) },
    { "R5",             BUTTON(69) },
    { "R6",             BUTTON(70) },
    { "R7",             BUTTON(71) },
    { "sl0",            SLIDER(0) },
    { "sl1",            SLIDER(1) },
    { "sl2",            SLIDER(2) },
    { "sl3",            SLIDER(3) },
    { "sl4",            SLIDER(4) },
    { "sl5",            SLIDER(5) },
    { "sl6",            SLIDER(6) },
    { "sl7",            SLIDER(7) },
    { "kn0",            KNOB(16) },
    { "kn1",            KNOB(17) },
    { "kn2",            KNOB(18) },
    { "kn3",            KNOB(19) },
    { "kn4",            KNOB(20) },
    { "kn5",            KNOB(21) },
    { "kn6",            KNOB(22) },
    { "kn7",            KNOB(23) },
};

static std::map<std::string, ControlSurfaceMap*> controlSurfaces = {
    { "nanoKONTROL2", &controlMap_Korg_nanoKONTROL2 }
};

static ControlSurfaceMap *controlSurfaceMap = nullptr;

bool setControlSurfaceType(const char *name)
{
    if (controlSurfaces.find(name) == controlSurfaces.end())
    {
        return false;
    }

    controlSurfaceMap = controlSurfaces[name];
    return true;
}

bool mapControl(ControlSurface& out, const char *name)
{
    if (controlSurfaceMap->find(name) == controlSurfaceMap->end())
    {
        return false;
    }

    out = (*controlSurfaceMap)[name];
    return true;
}
