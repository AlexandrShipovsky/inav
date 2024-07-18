/*
 * This file is part of INAV.
 *
 * INAV is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * INAV is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with INAV.  If not, see <http://www.gnu.org/licenses/>.
 */


#pragma once

#include <stdint.h>

#include "platform.h"

#include "common/time.h"
#include "common/utils.h"

#define SBUS2_TELEMETRY_PAYLOAD_SIZE 3

#define SBUS2_TELEMETRY_ITEM_SIZE   3
#define SBUS2_TELEMETRY_SLOTS       8
#define SBUS2_TELEMETRY_PAGES       4

#define SBUS2_DEADTIME              MS2US(2)
#define SBUS2_SLOT_TIME             650u
#define SBUS2_SLOT_DELAY_MAX        (MIN(350u, (SBUS2_SLOT_TIME / 2u)))

#define SBUS2_SLOT_COUNT            (SBUS2_TELEMETRY_PAGES * SBUS2_TELEMETRY_SLOTS)

#if defined(USE_TELEMETRY) && defined(USE_TELEMETRY_SBUS2)

// Information on SBUS2 sensors from: https://github.com/BrushlessPower/SBUS2-Telemetry/tree/master
typedef struct sbus2_telemetry_frame_s {
    uint8_t slotId;
    union
    {
        uint8_t data[2];
        uint16_t u16;
    } payload;
} __attribute__((packed)) sbus2_telemetry_frame_t;

STATIC_ASSERT(sizeof(sbus2_telemetry_frame_t) == 3, sbus2_telemetry_size);

extern const uint8_t sbus2SlotIds[SBUS2_SLOT_COUNT];
extern sbus2_telemetry_frame_t sbusTelemetryData[SBUS2_SLOT_COUNT];
extern uint8_t sbusTelemetryDataUsed[SBUS2_SLOT_COUNT];
extern timeUs_t sbusTelemetryDataLastSent[SBUS2_SLOT_COUNT];

// refresh telemetry buffers 
void handleSbus2Telemetry(timeUs_t currentTimeUs);

// time critical, send sbus2 data
void taskSendSbus2Telemetry(timeUs_t currentTimeUs);

uint8_t sbus2GetTelemetrySlot(timeUs_t elapsed);

void initSbus2Telemetry(void);
void sbus2startDeadTime(timeUs_t currentTime);


#endif
