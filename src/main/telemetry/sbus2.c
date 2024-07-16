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

#include <stdint.h>

#include "platform.h"

#include "build/debug.h"

#include "common/utils.h"
#include "common/time.h"
#include "common/axis.h"

#include "drivers/timer.h"

#include "telemetry/telemetry.h"
#include "telemetry/sbus2.h"

#include "rx/sbus.h"

#include "sensors/battery.h"
#include "sensors/sensors.h"
#include "sensors/temperature.h"
#include "sensors/diagnostics.h"

#include "io/gps.h"

#include "navigation/navigation.h"

#ifdef USE_ESC_SENSOR
#include "sensors/esc_sensor.h"
#include "flight/mixer.h"
#endif

#ifdef USE_TELEMETRY_SBUS2

const uint8_t sbus2SlotIds[SBUS2_SLOT_COUNT] = {
    0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3,
    0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
    0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB,
    0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB
};



sbus2_telemetry_frame_t sbusTelemetryData[SBUS2_SLOT_COUNT] = {};
uint8_t sbusTelemetryDataUsed[SBUS2_SLOT_COUNT] = {};
timeUs_t sbusTelemetryMinDelay[SBUS2_SLOT_COUNT] = {};

void handleSbus2Telemetry(timeUs_t currentTimeUs) 
{
    UNUSED(currentTimeUs);

    float voltage = getBatteryVoltage() * 0.01f;
    float cellVoltage = getBatteryAverageCellVoltage() * 0.01f;
    float current = getAmperage() * 0.01f;
    float capacity = getMAhDrawn();
    float altitude = getEstimatedActualPosition(Z) * 0.01f;
    float vario = getEstimatedActualVelocity(Z);
    float temperature = 0;
    uint32_t rpm = 0;

#ifdef USE_ESC_SENSOR
    escSensorData_t * escSensor = escSensorGetData();
    if (escSensor && escSensor->dataAge <= ESC_DATA_MAX_AGE) {
        rpm = escSensor->rpm;
        temperature = escSensor->temperature;
    } else {
        rpm = 0;
        temperature = 0;
    }
#endif

    //temperature = 42.16f;

    DEBUG_SET(DEBUG_SBUS2, 0, voltage);
    DEBUG_SET(DEBUG_SBUS2, 1, cellVoltage);
    DEBUG_SET(DEBUG_SBUS2, 2, current);
    DEBUG_SET(DEBUG_SBUS2, 3, capacity);
    DEBUG_SET(DEBUG_SBUS2, 4, altitude);
    DEBUG_SET(DEBUG_SBUS2, 5, vario);
    DEBUG_SET(DEBUG_SBUS2, 6, rpm);
    DEBUG_SET(DEBUG_SBUS2, 7, temperature);

    // 2 slots
    send_voltagef(1, voltage, cellVoltage);
    // 3 slots
    send_s1678_currentf(3, current, capacity, voltage);
    // 1 slot
    send_RPM(6, rpm);
    // 1 slot - esc temp
    static int change = 0;
    change++;
    int delta = change / 10;
    delta = delta % 20;
    send_SBS01T(7, temperature);

    // 8 slots, gps
    uint16_t speed = 0;
    float latitude = 0;
    float longitude = 0;

#ifdef USE_GPS
    if (gpsSol.fixType >= GPS_FIX_2D) {
        speed = (CMSEC_TO_KPH(gpsSol.groundSpeed) + 0.5f);
        latitude = gpsSol.llh.lat * 1e-7;
        longitude = gpsSol.llh.lon * 1e-7;
    }
#endif

    send_F1675f(8, speed, altitude, vario, latitude, longitude);
    // imu 1 slot
    int16_t temp16;
    bool valid = getIMUTemperature(&temp16);
    send_SBS01T(16, valid ? temp16 / 10 : 0);
    // baro
    valid = 0;
    valid = getBaroTemperature(&temp16);
    send_SBS01T(17, valid ? temp16 / 10 : 0);
    // temp sensors 18-25
#ifdef USE_TEMPERATURE_SENSOR
    for(int i = 0; i < 8; i++) {
        temp16 = 0;
        valid = getSensorTemperature(0, &temp16);
        send_SBS01T(18 + i, valid ? temp16 / 10 : 0);
    }
#else
    for(int i = 0; i < 8; i++) {
        send_SBS01T(18 + i, 0);
    }
#endif

    // 8 slots - gps
    // 
}

uint8_t sbus2GetTelemetrySlot(timeUs_t elapsed)
{
    if (elapsed < SBUS2_DEADTIME) {
        return 0xFF; // skip it
    }

    elapsed = elapsed - SBUS2_DEADTIME;

    uint8_t slot = (elapsed % SBUS2_SLOT_TIME) - 1;

    if (elapsed - (slot * SBUS2_SLOT_TIME) > SBUS2_SLOT_DELAY_MAX) {
        slot = 0xFF;
    }

    return slot;
}

FAST_CODE void taskSendSbus2Telemetry(timeUs_t currentTimeUs)
{
    if (!telemetrySharedPort || rxConfig()->receiverType != RX_TYPE_SERIAL ||
        rxConfig()->serialrx_provider != SERIALRX_SBUS2) {
        return;
    }

    timeUs_t elapsedTime = currentTimeUs - sbusGetLastFrameTime();

    if(elapsedTime > MS2US(8)) {
        return;
    }


    uint8_t telemetryPage = sbusGetCurrentTelemetryPage();

    uint8_t slot = sbus2GetTelemetrySlot(elapsedTime);

    if(slot < SBUS2_TELEMETRY_SLOTS) {
        int slotIndex = (telemetryPage * SBUS2_TELEMETRY_SLOTS) + slot;
        if (slotIndex < SBUS2_SLOT_COUNT) {
            if (sbusTelemetryDataUsed[slotIndex] != 0 && sbusTelemetryMinDelay[slotIndex] < currentTimeUs) {
                sbusTelemetryData[slotIndex].slotId = sbus2SlotIds[slotIndex];
                // send
                serialWriteBuf(telemetrySharedPort,
                               (const uint8_t *)&sbusTelemetryData[slotIndex],
                               sizeof(sbus2_telemetry_frame_t));
                sbusTelemetryMinDelay[slotIndex] = currentTimeUs + MS2US(1);
                //sbusTelemetryDataUsed[slotIndex] = 0;
            }
        }
    }
}

// 
void send_RPM(uint8_t port, uint32_t RPM)
{
   uint32_t value =  0;
   uint8_t bytes[2] = { };

   value =  RPM / 6;
   if(value > 0xffff){
    value = 0xffff;
   }
   bytes[1] = value >> 8;
   bytes[0] = value;
   SBUS2_transmit_telemetry_data( port , bytes);
}


void send_temp125(uint8_t port, int16_t temp)
{
   int16_t value=  0;
   uint8_t bytes[2] = { };

   value = temp | 0x4000;
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port , bytes);
}
void send_SBS01T(uint8_t port, int16_t temp){
  int16_t value=  0;
  uint8_t bytes[2] = {};

  value = temp | 0x8000;
  value = value + 100;
  bytes[0] = value;// >> 8;
  bytes[1] = value >> 8;
  SBUS2_transmit_telemetry_data(port , bytes);
}

void send_voltage(uint8_t port,uint16_t voltage1, uint16_t voltage2)
{
   uint16_t value = 0;
   uint8_t bytes[2] = { };
   
   // VOLTAGE1
   value = voltage1 | 0x8000; 
   if ( value > 0x9FFF ){
     value = 0x9FFF; // max voltage is 819.1
   }
   else if(value < 0x8000){
     value = 0x8000;
   }
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port , bytes);

   //VOLTAGE2
   value = voltage2;
   if ( value > 0x1FFF ){
     value = 0x1FFF; // max voltage is 819.1
   }  
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port+1 , bytes);
}

void send_s1678_current(uint8_t port, uint16_t current, uint16_t capacity, uint16_t voltage)
{
   uint16_t value = 0;
   uint32_t local = 0;
   uint8_t bytes[2] = { };
 
   
   // CURRENT
   local = ((uint32_t)current) * 1 ;
   value = (uint16_t)local;   
   if ( value > 0x3FFF )
   {
      // max current is 163.83
      value = 0x3FFF;
   }  
   bytes[0] = value >> 8;
   bytes[0] = bytes[0] | 0x40;
   bytes[0] = bytes[0] & 0x7F;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port , bytes);

   //VOLTAGE
   local = ((uint32_t)voltage) * 1;
   value = (uint16_t)local;   
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port+1 , bytes);

   // CAPACITY
   local = (uint32_t)capacity;
   value = (uint16_t)local;   
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port+2 , bytes);
}

void send_f1675_gps(uint8_t port, uint16_t speed, int16_t altitude, int16_t vario, int32_t latitude, int32_t longitude)
{
   uint16_t value1 = 0;
   int16_t  value2 = 0;
   uint32_t  value3 = 0;
   bool latitude_pos = false;
   bool longitude_pos = false;
   uint8_t bytes[2] = {};
 
   
   // SPEED -> Bit 14(bytes[1] bit7) -> GPS Valid or not
   value1 = speed | 0x4000;
   if (value1 > 0x43E7 ){
      value1 = 0x43E7;  // max Speed is 999 km/h
   }  
   else if( value1 < 0x4000){
     value1 = 0x4000;
   }
   bytes[0] = value1 >> 8;
   bytes[1] = value1;
   SBUS2_transmit_telemetry_data( port , bytes);

   //HIGHT
   value2 = altitude | 0x4000;
   /*if(value2 > 0x7FFF ){		// max = +16383
     value2 = 0x7FFF;  
   }  
   else if( value2 < 0xC000){	// min = -16384
     value2 = 0xC000;
   }*/
   bytes[0] = value2 >> 8;
   bytes[1] = value2;
   SBUS2_transmit_telemetry_data( port+1 , bytes);

   //TIME -> 12:34:56 Uhr = 12*60*60 + 34*60 + 56 = 45296 = 0xB0F0
   bytes[0] = 0xB0;
   bytes[1] = 0xF0;
   SBUS2_transmit_telemetry_data( port+2 , bytes);

   // VARIO
   value2 = vario * 10; 
   bytes[0] = value2 >> 8;
   bytes[1] = value2;
   SBUS2_transmit_telemetry_data( port+3 , bytes);

   // LATITUDE
   if(latitude >= 0){
    latitude_pos = true;
   }
   else{
    latitude_pos = false;
    latitude = latitude * -1;
   }
   bytes[0] = (uint8_t)(latitude/1000000);
   value3 = (uint32_t)(latitude%1000000);
   if(latitude_pos){
    bytes[1] = ((value3 >> 16) & 0x0f);    // North
   }
   else{
    bytes[1] = ((value3 >> 16) | 0x1f);    // South
   }
   SBUS2_transmit_telemetry_data( port+4 , bytes);

   bytes[0] = ((value3 >> 8) & 0xff);
   bytes[1] = value3 & 0xff;
   SBUS2_transmit_telemetry_data( port+5 , bytes);

   // LONGITUDE
   if(longitude >= 0){
    longitude_pos = true;
   }
   else{
    longitude_pos = false;
    longitude = longitude * -1;
   }
   bytes[0] = (uint8_t)(longitude/1000000);
   value3 = (uint32_t)(longitude%1000000);
   if(longitude_pos){
    bytes[1] = ((value3 >> 16) & 0x0f);    // Eath
   }
   else{
    bytes[1] = ((value3 >> 16) | 0x1f);    // West
   }
   SBUS2_transmit_telemetry_data( port+6 , bytes);

   bytes[0] = ((value3 >> 8) & 0xff);
   bytes[1] = value3 & 0xff;
   SBUS2_transmit_telemetry_data( port+7 , bytes);
}

void send_f1672_vario(uint8_t port, int16_t altitude, int16_t vario)
{
   int16_t value = 0;
   uint8_t bytes[2] = { };

   // VARIO
   value = vario;   
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port, bytes);
   
   //HIGHT
   value = altitude | 0x4000;
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 1, bytes);
}

void send_f1712_vario(uint8_t port, int16_t altitude, int16_t vario)
{
   int16_t  value = 0;
   uint8_t bytes[2] = { };

   // VARIO
   value = vario;   
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port , bytes);
   
   //HIGHT
   value = altitude | 0x4000;
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 1 , bytes);
   
}


void send_SBS01TE(uint8_t port, int16_t temp){
  send_temp125(port, temp);
}
void send_F1713(uint8_t port, int16_t temp){
  send_temp125(port, temp);
}

void send_SBS01RB(uint8_t port, uint32_t RPM){
  send_RPM(port, RPM);
}
void send_SBS01RM(uint8_t port, uint32_t RPM){
  send_RPM(port, RPM);
}
void send_SBS01RO(uint8_t port, uint32_t RPM){
  send_RPM(port, RPM);
}
void send_SBS01R(uint8_t port, uint32_t RPM){
  send_RPM(port, RPM);
}

void send_F1678(uint8_t port, uint16_t current, uint16_t capacity, uint16_t voltage){
  send_s1678_current(port, current, capacity, voltage);
}
void send_s1678_currentf(uint8_t port, float current, uint16_t capacity, float voltage){
  send_s1678_current(port, (uint16_t)(current * 100), capacity, (uint16_t)(voltage * 100));
}
void send_F1678f(uint8_t port, float current, uint16_t capacity, float voltage){
  send_s1678_current(port, (uint16_t)(current * 100), capacity, (uint16_t)(voltage * 100));
}
void send_SBS01V(uint8_t port,uint16_t voltage1, uint16_t voltage2){
  send_voltage(port, voltage1, voltage2);
}
void send_SBS01Vf(uint8_t port,float voltage1, float voltage2){
  send_voltage(port, (uint16_t)(voltage1 * 10), (uint16_t)(voltage2 * 10));
}
void send_voltagef(uint8_t port,float voltage1, float voltage2){
  send_voltage(port, (uint16_t)(voltage1 * 10), (uint16_t)(voltage2 * 10));
}
void send_SBS01C(uint8_t port, uint16_t current, uint16_t capacity, uint16_t voltage){
  send_s1678_current(port, current, capacity, voltage);
}
void send_SBS01Cf(uint8_t port, float current, uint16_t capacity, float voltage){
  send_s1678_current(port, (uint16_t)(current * 100), capacity, (uint16_t)(voltage * 100));
}

void send_f1712_variof(uint8_t port, int16_t altitude, float vario){
  send_f1712_vario(port, altitude, (int16_t)(vario * 10));
}
void send_f1672_variof(uint8_t port, int16_t altitude, float vario){
  send_f1672_vario(port, altitude, (int16_t)(vario * 100));
}
void send_F1712(uint8_t port, int16_t altitude, int16_t vario){
  send_f1712_vario(port, altitude, vario);
}
void send_F1712f(uint8_t port, int16_t altitude, float vario){
  send_f1712_vario(port, altitude, (int16_t)(vario * 10));
}
void send_F1672(uint8_t port, int16_t altitude, int16_t vario){
  send_f1672_vario(port, altitude, vario);
}
void send_F1672f(uint8_t port, int16_t altitude, float vario){
  send_f1672_vario(port, altitude, (int16_t)(vario * 100));
}

void send_F1675minf(uint8_t port, uint16_t speed, int16_t hight, int16_t vario, int8_t lat_deg, float lat_min, int8_t lon_deg, float lon_min){
  bool Lat_Negative = false;
  bool Lon_Negative = false;
  if(lat_deg < 0){
    Lat_Negative = true;
    lat_deg = lat_deg * -1;
  }
  if(lon_deg < 0){
    Lon_Negative = true;
    lon_deg = lon_deg * -1;
  }
  if(lat_min < 0){
    Lat_Negative = true;
    lat_min = lat_min * -1;
  }
  if(lon_min < 0){
    Lon_Negative = true;
    lon_min = lon_min * -1;
  }
  int32_t _latitude_deg = lat_deg;
  int32_t _longitude_deg = lon_deg;
  int32_t _latitude_min = lat_min * 10000;
  int32_t _longitude_min = lon_min * 10000;
  int32_t _latitude = _latitude_deg * 1000000;
  int32_t _longitude = _longitude_deg * 1000000;
  _latitude = _latitude + _latitude_min;
  _longitude = _longitude + _longitude_min;
  if(Lat_Negative){
    _latitude = _latitude * -1;
  }
  if(Lon_Negative){
    _longitude = _longitude * -1;
  }
  send_f1675_gps(port, speed, hight, vario, _latitude, _longitude);
}
void send_F1675min(uint8_t port, uint16_t speed, int16_t hight, int16_t vario, int8_t lat_deg, int32_t lat_min, int8_t lon_deg, int32_t lon_min){
  bool Lat_Negative = false;
  bool Lon_Negative = false;
  if(lat_deg < 0){
    Lat_Negative = true;
    lat_deg = lat_deg * -1;
  }
  if(lon_deg < 0){
    Lon_Negative = true;
    lon_deg = lon_deg * -1;
  }
  if(lat_min < 0){
    Lat_Negative = true;
    lat_min = lat_min * -1;
  }
  if(lon_min < 0){
    Lon_Negative = true;
    lon_min = lon_min * -1;
  }
  int32_t _latitude_deg = lat_deg;
  int32_t _longitude_deg = lon_deg;
  int32_t _latitude = _latitude_deg * 1000000;
  int32_t _longitude = _longitude_deg * 1000000;
  _latitude = _latitude + lat_min;
  _longitude = _longitude + lon_min;
  if(Lat_Negative){
    _latitude = _latitude * -1;
  }
  if(Lon_Negative){
    _longitude = _longitude * -1;
  }
  send_f1675_gps(port, speed, hight, vario, _latitude, _longitude);
}
void send_F1675(uint8_t port, uint16_t speed, int16_t hight, int16_t vario, int32_t latitude, int32_t longitude){
  int32_t _latitude = latitude;
  int32_t _longitude = longitude;
  int32_t _latitude_deg = _latitude/1000000;
  int32_t _longitude_deg = _longitude/1000000;
  int32_t _latitude_min = _latitude%1000000;
  int32_t _longitude_min = _longitude%1000000;
  _latitude = _latitude_deg * 1000000;
  _longitude = _longitude_deg * 1000000;
  _latitude = _latitude + ((_latitude_min * 60)/100);
  _longitude = _longitude + ((_longitude_min * 60)/100);
  send_f1675_gps(port, speed, hight, vario, _latitude, _longitude);
}
void send_F1675f(uint8_t port, uint16_t speed, int16_t hight, int16_t vario, float latitude, float longitude){
  int32_t _latitude = latitude * 1000000;
  int32_t _longitude = longitude * 1000000;
  int32_t _latitude_deg = _latitude/1000000;
  int32_t _longitude_deg = _longitude/1000000;
  int32_t _latitude_min = _latitude%1000000;
  int32_t _longitude_min = _longitude%1000000;
  _latitude = _latitude_deg * 1000000;
  _longitude = _longitude_deg * 1000000;
  _latitude = _latitude + ((_latitude_min * 60)/100);
  _longitude = _longitude + ((_longitude_min * 60)/100);
  send_f1675_gps(port, speed, hight, vario, _latitude, _longitude);
}
void send_SBS10G(
  uint8_t port,
  uint16_t hours,          // 0 to 24
  uint16_t minutes,        // 0 to 60
  uint16_t seconds,        // 0 to 60
  float latitude,          // decimal degrees (i.e. 52.520833; negative value for southern hemisphere)
  float longitude,         // decimal degrees (i.e. 13.409430; negative value for western hemisphere)
  float altitudeMeters,    // meters (valid range: -1050 to 4600)
  uint16_t speed,          // km/h (valid range 0 to 511)
  float gpsVario)          // m/s (valid range: -150 to 260)
{
   uint32_t utc = (hours*3600) + (minutes*60) + seconds;
   uint32_t lat, lon;
   // scale latitude/longitude (add 0.5 for correct rounding)
   if (latitude > 0) {
     lat = (600000.0*latitude) + 0.5;
   }
   else {
     lat = (-600000.0*latitude) + 0.5;
     // toggle south bit
     lat |= 0x4000000;
   }
   if (longitude > 0) {
     lon = (600000.0*longitude) + 0.5;
   }
   else {
     lon = (-600000.0*longitude) + 0.5;
     // toggle west bit
     lon |= 0x8000000;
   }
   // convert altitude (add 0.5 for correct rounding)
   uint16_t alt = (altitudeMeters>=-820 && altitudeMeters<=4830) ?(1.25*(altitudeMeters+820)) + 0.5  : 0;
   // error check speed
   if (speed < 512) {
    // set speed enable bit
    speed |= 0x200;
   }
   else {
    speed = 0;
   }
   // initialize buffer
   uint8_t bytes[2] = { };
   // slot 0 (utc)
   bytes[0] = (utc&0x00ff);
   bytes[1] = (utc&0xff00)>>8;
   SBUS2_transmit_telemetry_data(port , bytes);
   // slot 1 (latitude & utc)
   bytes[0] = ((lat&0x007f)<<1) | ((utc&0x10000)>>16);
   bytes[1] =  (lat&0x7f80)>>7;
   SBUS2_transmit_telemetry_data(port+1 , bytes);
   // slot 2 (latitude & longitude)
   bytes[0] =  (lat&0x07f8000)>>15;
   bytes[1] = ((lat&0x7800000)>>23) | (lon&0x0f)<<4;
   SBUS2_transmit_telemetry_data(port+2 , bytes);
   // slot 3 (longitude)
   bytes[0] = (lon&0x00ff0)>>4;
   bytes[1] = (lon&0xff000)>>12;
   SBUS2_transmit_telemetry_data(port+3 , bytes);
   // slot 4 (longitude & speed)
   bytes[0] = ((lon&0xff00000)>>20);
   bytes[1] = (speed&0xff);
   SBUS2_transmit_telemetry_data(port+4 , bytes);
   // slot 5 (pressure & speed)
   bytes[0] = ((speed&0x300)>>8);
   bytes[1] = 0x00;
   SBUS2_transmit_telemetry_data(port+5 , bytes);
   // slot 6 (altitude & pressure)
   bytes[0] = ((alt&0x003)<<6);
   bytes[1] =  (alt&0x3fc)>>2;
   SBUS2_transmit_telemetry_data(port+6 , bytes);
   // slot (7 (vario & altitude)
   uint16_t vario;
   // error check vario
   if (gpsVario >= -150 && gpsVario <= 260) {
    // scale vario (add 0.5 for correct rounding)
    vario = (10.0*(gpsVario + 150)) + 0.5;
    // set vario enable
    vario |= 0x1000;
   }
   else {
    vario = 0;
   }
   bytes[0] = ((vario&0x001f)<<3) | ((alt&0x1c00)>>10);
   bytes[1] =  (vario&0x1fe0)>>5;
   SBUS2_transmit_telemetry_data(port+7 , bytes);
}
void send_scorpion_kontronik(
  uint8_t port, 
  uint16_t voltage, 
  uint16_t capacity, 
  uint32_t rpm,
  uint16_t current,
  uint16_t temp,
  uint16_t becTemp,
  uint16_t becCurrent,
  uint16_t pwm) 
{
   uint32_t value = 0;
   uint8_t bytes[2] = { };

   // voltage 41.1 = 4110
   value = voltage | 0x8000; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port , bytes);

   // 1330 mah => 1.33 Ah
   value = capacity; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 1 , bytes);

   // 2250 rpm => 2250
   value = rpm / 6; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 2 , bytes);

   // 13310 => 133.1 A
   value = current; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 3 , bytes);

   // 41 => 41 Celsius
   value = temp; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 4 , bytes);

   // 21 => Bec Celsius
   value = becTemp; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 5 , bytes);

   // 650 => 6,5 Bec Current
   value = becCurrent; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 6 , bytes);

   // PWM output
   value = pwm; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 7 , bytes);    
}

void send_scorpion(
  uint8_t port, 
  uint16_t voltage, 
  uint16_t capacity, 
  uint32_t rpm,
  uint16_t current,
  uint16_t temp,
  uint16_t becTemp,
  uint16_t becCurrent,
  uint16_t pwm) 
{
   send_scorpion_kontronik(
    port,
    voltage,
    capacity,
    rpm,
    current,
    temp,
    becTemp,
    becCurrent,
    pwm);
}

void send_kontronik(
  uint8_t port, 
  uint16_t voltage, 
  uint16_t capacity, 
  uint32_t rpm,
  uint16_t current,
  uint16_t temp,
  uint16_t becTemp,
  uint16_t becCurrent,
  uint16_t pwm) 
{
   send_scorpion_kontronik(
    port,
    voltage,
    capacity,
    rpm,
    current,
    temp,
    becTemp,
    becCurrent,
    pwm);
}

void send_jetcat(
  uint8_t port, 
  uint32_t rpm,
  uint16_t egt,
  uint16_t pump_volt,
  uint32_t setrpm,
  uint16_t thrust,
  uint16_t fuel,
  uint16_t fuelflow,
  uint16_t altitude,
  uint16_t quality,
  uint16_t volt,
  uint16_t current,
  uint16_t speed,
  uint16_t status,
  uint32_t secondrpm) 
{
   uint32_t value = 0;
   uint8_t bytes[2] = {};

   // Actual RPM with 0x4000 Offset -> why?
   value = rpm / 100; 
   value = value | 0x4000;
   if(value > 0xffff){
    value = 0xffff;
   }
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port , bytes);
   
   // EGT Abgastemperatur in °C
   value = egt;
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 1 , bytes);
   
   // Pump Voltage 12.34V = 1234
   value = pump_volt;
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 2 , bytes);
   
   // Setpoint RPM without Offset
   value = setrpm / 100; 
   if(value > 0xffff){
    value = 0xffff;
   }
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 3 , bytes);
   
   // Thrust 123.4N = 1234
   value = thrust; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 4 , bytes);
   
   // Fuel (remain) in ml
   value = fuel; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 5 , bytes);
   
   // Fuel Flow in ml/min
   value = fuelflow; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 6 , bytes);
   
   // Altitude -> without offset?
   value = altitude; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 7 , bytes);
   
   // Fuel Quality in %
   value = quality; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 8 , bytes);
   
   // Voltage 12.34V = 1234
   value = volt;
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 9 , bytes);
   
   // Current 123.4A = 1234
   value = current; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 10 , bytes);
   
   // Speed in km/h
   value = speed; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 11 , bytes);
   
   // Status and Error Code
   value = status; 
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 12 , bytes);
   
   // Second RPM without Offset
   value = secondrpm / 100; 
   if(value > 0xffff){
    value = 0xffff;
   }
   bytes[0] = value >> 8;
   bytes[1] = value;
   SBUS2_transmit_telemetry_data( port + 13 , bytes);
 
}


void SBUS2_transmit_telemetry_data(uint8_t slotId , const uint8_t *bytes)
{
    if(slotId > 0 && slotId < SBUS2_SLOT_COUNT) {
        sbusTelemetryData[slotId].payload.data[0] = bytes[0];
        sbusTelemetryData[slotId].payload.data[1] = bytes[1];
        sbusTelemetryData[slotId].slotId = sbus2SlotIds[slotId];
        //sbusTelemetryData[i].payload.data[0] = 0x81;
        //sbusTelemetryData[i].payload.data[1] = 0x80;
        sbusTelemetryDataUsed[slotId] = 1;
    }

}

void sbus2startDeadTime(timeUs_t currentTime)
{
    UNUSED(currentTime);
}

void initSbus2Telemetry(void)
{
    /*
    const timerHardware_t *timerRx = timerGetByUsageFlag(TIM_USE_ANY);
    TCH_t *tch = timerGetTCH(timerRx);

    uint32_t baseClock = timerClock(tch->timHw->tim);
    uint32_t clock = baseClock;
    uint32_t timerPeriod;
    uint32_t baud = 1000000;
    

    do {
        timerPeriod = clock / baud;
        if (timerPeriod > 0xFFFF) {
            if (clock > 1) {
                clock = clock / 2;   // this is wrong - mhz stays the same ... This will double baudrate until ok (but minimum baudrate is < 1200)
            } else {
                // TODO unable to continue, unable to determine clock and timerPeriods for the given baud
            }
        }
    } while (timerPeriod > 0xFFFF);

    timerConfigure(tch, timerPeriod, baseClock);
    */
}

#endif
