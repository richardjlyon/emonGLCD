//------------------------------------------------------------------------------------------------------------------------------------------------
// emonGLCD Home Energy Monitor 1.2

// Assumes power1 is domestic energy, power3 is heating energy
// Shows total energy use (domestic and heating)
// Illuminates the red LED when heating is on
//
// emonGLCD documentation http://openEnergyMonitor.org/emon/emonglcd

// RTC to reset Kwh counters at midnight is implemented is software. 
// Correct time is updated via NanodeRF which gets time from internet
// Temperature recorded on the emonglcd is also sent to the NanodeRF for online graphing

// GLCD library by Jean-Claude Wippler: JeeLabs.org
// 2010-05-28 <jcw@equi4.com> http://opensource.org/licenses/mit-license.php
//
// Authors: Glyn Hudson and Trystan Lea
// Part of the: openenergymonitor.org project
// Licenced under GNU GPL V3
// http://openenergymonitor.org/emon/license

// THIS SKETCH REQUIRES:

// Libraries in the standard arduino libraries folder:
//
//	- OneWire library	http://www.pjrc.com/teensy/td_libs_OneWire.html
//	- DallasTemperature	http://download.milesburton.com/Arduino/MaximTemperature
//                           or https://github.com/milesburton/Arduino-Temperature-Control-Library
//	- JeeLib		https://github.com/jcw/jeelib
//	- RTClib		https://github.com/jcw/rtclib
//	- GLCD_ST7565		https://github.com/jcw/glcdlib
//
// Other files in project directory (should appear in the arduino tabs above)
//	- icons.ino
//	- templates.ino
//
//-------------------------------------------------------------------------------------------------------------------------------------------------

#include <JeeLib.h>
#include <GLCD_ST7565.h>
#include <avr/pgmspace.h>
GLCD_ST7565 glcd;

#include <OneWire.h>		    // http://www.pjrc.com/teensy/td_libs_OneWire.html
#include <DallasTemperature.h>      // http://download.milesburton.com/Arduino/MaximTemperature/ (3.7.2 Beta needed for Arduino 1.0)

#include <RTClib.h>                 // Real time clock (RTC) - used for software RTC to reset kWh counters at midnight
#include <Wire.h>                   // Part of Arduino libraries - needed for RTClib
RTC_Millis RTC;

//--------------------------------------------------------------------------------------------
// RFM12B Settings
//--------------------------------------------------------------------------------------------
#define EMONTXNODE 10               // Defined in emontx firmware
#define BASENODE 15                 // Defined by raspberry pi formware
#define MYNODE 20                   // Should be unique on network, node ID 30 reserved for base station
#define freq RF12_868MHZ            // frequency - match to same frequency as RFM12B module (change to 868Mhz or 915Mhz if appropriate)
#define group 210 

#define ONE_WIRE_BUS 5              // temperature sensor connection - hard wired 

unsigned long fast_update, slow_update;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
double temp,maxtemp,mintemp;

//---------------------------------------------------
// Data structures for transfering data between units
//---------------------------------------------------
typedef struct { int power1, power2, power3, Vrms; } PayloadTX;         // neat way of packaging data for RF comms
PayloadTX emontx;

typedef struct { int temperature; } PayloadGLCD;
PayloadGLCD emonglcd;

int hour = 12, minute = 0;
int cval_all;                       // all electricity (heating and domestic)
int cval_heat;                      // heating electricity
double allkwh = 0;
double heatkwh = 0;

const int greenLED=6;               // Green tri-color LED
const int redLED=9;                 // Red tri-color LED
const int LDRpin=4;    		    // analog pin of onboard lightsensor 


//-------------------------------------------------------------------------------------------- 
// Flow control
//-------------------------------------------------------------------------------------------- 
unsigned long last_emontx;                   // Used to count time from last emontx update
unsigned long last_emonbase;                   // Used to count time from last emontx update

//--------------------------------------------------------------------------------------------
// Setup
//--------------------------------------------------------------------------------------------
void setup()
{
  delay(500); 				   //wait for power to settle before firing up the RF
  rf12_initialize(MYNODE, freq,group);
  delay(100);				   //wait for RF to settle befor turning on display
  glcd.begin(0x19);
  glcd.backLight(200);
  
  sensors.begin();                         // start up the DS18B20 temp sensor onboard  
  sensors.requestTemperatures();
  temp = (sensors.getTempCByIndex(0));     // get inital temperture reading
  mintemp = temp; maxtemp = temp;          // reset min and max

  pinMode(greenLED, OUTPUT); 
  pinMode(redLED, OUTPUT); 
}

//--------------------------------------------------------------------------------------------
// Loop
//--------------------------------------------------------------------------------------------
void loop()
{
  
  //--------------------------------------------------------------------------------------------
  // Get data packet and adjust clock
  //--------------------------------------------------------------------------------------------
  if (rf12_recvDone())
  {
    if (rf12_crc == 0 && (rf12_hdr & RF12_HDR_CTL) == 0)  // and no rf errors
    {
      int node_id = (rf12_hdr & 0x1F);
      if (node_id == EMONTXNODE)               // e.g. 10 is the emonTx NodeID
      {
        emontx = *(PayloadTX*) rf12_data; 
        last_emontx = millis();
      }  
      
      if (node_id == BASENODE)		        // e.g. 15 is the emonBase node ID
      {
        RTC.adjust(DateTime(2012, 1, 1, rf12_data[1], rf12_data[2], rf12_data[3]));
        last_emonbase = millis();
      } 
    }
  }

  //--------------------------------------------------------------------------------------------
  // Display update every 200ms
  //--------------------------------------------------------------------------------------------
  if ((millis()-fast_update)>200)
  {
    fast_update = millis();
    
    // get clock info
    DateTime now = RTC.now();
    int last_hour = hour;
    hour = now.hour();
    minute = now.minute();

    // get power data
    cval_all = cval_all + (emontx.power1 - cval_all)*0.50;         // smooth transitions
    cval_heat = cval_heat + (emontx.power2 - cval_heat)*0.50;      // smooth transitions
    allkwh += (emontx.power1 * 0.2) / 3600000;                     // power1 carries total power    

    // reset the power data at midnight
    if (last_hour == 23 && hour == 00) { allkwh = 0; } // reset Kwh/d counter at midnight
    
    // draw the display
    draw_power_page( "POWER", cval_all, "USE", allkwh);
    draw_temperature_time_footer(temp, mintemp, maxtemp, hour, minute);
    glcd.refresh();

    // set the backlight brightness
    int LDR = analogRead(LDRpin);                     // Read the LDR Value so we can work out the light level in the room.
    int LDRbacklight = map(LDR, 0, 1023, 50, 250);    // Map the data from the LDR from 0-1023 (Max seen 1000) to var GLCDbrightness min/max
    LDRbacklight = constrain(LDRbacklight, 0, 255);   // Constrain the value to make sure its a PWM value 0-255
    if ((hour > 22) ||  (hour < 5)) glcd.backLight(0); else glcd.backLight(LDRbacklight);  
    
    // set the heating LED
    if (cval_heat > 1000)                             // Light the red LED if the heating is drawing more than 1kW
    {
      analogWrite(redLED, 255);
    } else {
      analogWrite(redLED, 0);
    }
  } 
  
  if ((millis()-slow_update)>10000)
  {
    slow_update = millis();
    
    // get the temperature
    sensors.requestTemperatures();
    temp = (sensors.getTempCByIndex(0));
    emonglcd.temperature = (int) (temp * 100);                          // set emonglcd payload
    
    // send the temperature
    rf12_sendNow(0, &emonglcd, sizeof emonglcd);                     //send temperature data via RFM12B using new rf12_sendNow wrapper -glynhudson
    rf12_sendWait(2);    

    // capture max and min temperatures
    if (temp > maxtemp) maxtemp = temp;
    if (temp < mintemp) mintemp = temp;
  }
}
