/*
* This file is part of Cleanflight.
*
* Cleanflight is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Cleanflight is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdint.h>

#include <platform.h>
#include "drivers/io.h"
#include "drivers/pwm_mapping.h"
#include "drivers/timer.h"
#include "drivers/sensor.h"
#include "drivers/bus.h"


timerHardware_t timerHardware[] = {
    DEF_TIM(TIM1, CH4, PE14, TIM_USE_OUTPUT_AUTO, 0, 0),    // M1_OUT
    DEF_TIM(TIM1, CH3, PE13, TIM_USE_OUTPUT_AUTO, 0, 0),    // M2_OUT
    DEF_TIM(TIM1, CH2, PE11, TIM_USE_OUTPUT_AUTO, 0, 0),    // M3_OUT
    DEF_TIM(TIM1, CH1, PE9,  TIM_USE_OUTPUT_AUTO, 0, 0),    // M4_OUT

    DEF_TIM(TIM2, CH1, PA15, TIM_USE_SERVO, 0, 0),          // S1_OUT
    DEF_TIM(TIM2, CH4, PB11, TIM_USE_SERVO, 0, 0),          // S2_OUT
    DEF_TIM(TIM2, CH2, PB3, TIM_USE_SERVO, 0, 0),           // S3_OUT
    DEF_TIM(TIM2, CH3, PB10, TIM_USE_SERVO, 0, 0),          // S4_OUT
};

const int timerHardwareCount = sizeof(timerHardware) / sizeof(timerHardware[0]);

