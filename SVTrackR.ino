/*

 SVTrackR ( Arduino APRS Tracker )
 Copyright (C) 2014 Stanley Seow <stanleyseow@gmail.com>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 
 github URL :-
 https://github.com/stanleyseow/ArduinoTracker-MicroAPRS
 
 This sketch configure the MicroAPRS for the proper callsign and ssid and
 read/write data coming in from the MicroAPRS via debug port
 
 Pin 0/1 (rx,tx) connects to Arduino with MicroAPRS firmware
 Pin 8,9 ( rx,tx ) connects to GPS module
 Pin 2,3 connect to debug serial port
 
 Pin 4 - Buzzer during Radio Tx
 
 Date : 03 July 2014
 Written by Stanley Seow
 e-mail : stanleyseow@gmail.com
 Version : 0.2
 
 Pls flash the modem Arduino with http://unsigned.io/microaprs/ with USBtinyISP
 This firmware does NOT have an Arduino bootloader and run pure AVR codes. Once you
 have done this, follow the instructions on the above URL on how to set it up.
 We will call this Arduino/atmega328 as the "modem". 
 
 To use with Arduino Tracker (this sketch), connect the Modem Tx(pin1) to Arduino Rx(pin8) and 
 Modem Rx(pin0) to Arduino Rx(pin9).
 
 I had dropped bytes when using Softwaredebug for the MicroAPRS modem and therefore
 I'm using AltSoftdebug instead. The GPS module is still on Softwaredebug and 
 the hardware debug is still used for debug Monitor.
 
 ***** To use this sketch, pls change your CALLSIGN and SSID below under configModem().
 
 History :-
 03 July 2014 :-
 - Initial released
 
 06 July 2014 :-
 - added checks for speed and idle speed, modify the Txinternal
 - remove all debug Monitor output
 
 12 July 2014 :-
 - Added SmartBeaconing algorithm, Tx when turn is more than 25 deg
 - Reduce the Tx interval
 
 14 July 2014 :-
 - Fixed coordinates conversion from decimal to Deg Min formula
 - Formula to calculate distance from last Tx point so that it will Tx once the max direct distance 
  of 500m is reached
 
 18 July 2014 :-
 - Fixed lastTx checking routine and ensure lastTx is 5 or more secs
 - Check for analog0 button at least 10 secs per Tx
 - Rewrote the DecodeAPRS functions for display to LCD only
 
 1 Aug 2014 :-
 - Ported codes to Arduino Mini Pro 3.3V
 - Due to checksum errors and Mini Pro 3.3V 8Mhz on SoftwareSerial, I swapped GPS ports to AltSoftwareSerial
 
 3 Aug 2014 :-
 - Added #define codes to turn on/off LCD, DEBUG and TFT
 - Added TFT codes for 2.2" SPI TFT ( runs on 3.3V )
 - Added the F() macros to reduce memory usages on static texts
 
 6 Aug 2014
 - Added Course/Speed/Alt into comment field readable by aprs.fi
 
 10 Aug 2014
 - Added support for teensy 3.1 (mcu ARM Cortex M4) 
 
 19 Aug 2014
 - Added GPS simulator codes
 - Added Tx status every 10 Tx
 - Added counter for Headins, Time, Distance and Button
 
 19 Sep 2014
 - Modify button pressed to send STATUS & position 
 - If GPS not locked, only sent out STATUS 
 
 TODO :-
 - implement compression / decompression codes for smaller Tx packets
 - Telemetry packets
 - Split comments and location for smaller Tx packets
 
 Bugs :-
 
 - Not splitting callsign and info properly
 - Packets received from Modem is split into two serial read
 
*/

// Needed this to prevent compile error for #defines

#define VERSION "SVTrackR v0.7 " 


#if 1
__asm volatile ("nop");
#endif

#ifndef _CONFIGURATION_INCLUDED
#define _CONFIGURATION_INCLUDED
#include "config.h"
#endif
 
// Turn on/off 20x4 LCD
#undef LCD20x4
// Turn on/off debug
#define DEBUG
// Turn on/off 2.2" TFT
#undef TFT22
// Turn on/off GPS simulation
#undef GPSSIM

#ifdef TFT22
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9340.h>
#define _sclk 13
#define _miso 12
#define _mosi 11
#define _cs 7
#define _dc 6
#define _rst 5
Adafruit_ILI9340 tft = Adafruit_ILI9340(_cs, _dc, _rst);
#endif

#ifdef DEBUG 
  #if defined(__arm__) && defined(TEENSYDUINO)
  #else
  #include <SoftwareSerial.h>
  #endif
#endif

// Only needed for ATmega328
#if defined (__AVR_ATmega328P__) 
#include <AltSoftSerial.h>
#endif 

#include <TinyGPS++.h>

#ifdef LCD20x4
// My wiring for LCD on breadboard
#include <LiquidCrystal.h>
LiquidCrystal lcd(12, 11, 4, 5, 6, 7);
#endif


// Altdebug default on UNO is 8-Rx, 9-Tx
//AltSoftSerial ss;
#if defined (__AVR_ATmega328P__) 
  AltSoftSerial ss(8,9);
#else
// Map hw Serial2 to ss for gps port
  #define ss Serial2 
#endif

TinyGPSPlus gps;

#ifdef DEBUG
// Connect to GPS module on pin 9, 10 ( Rx, Tx )
  #if defined (__AVR_ATmega328P__) 
    SoftwareSerial debug(2,3);
  #elif defined(__arm__) && defined(TEENSYDUINO)
    #define debug Serial3
  #endif
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Put All global defines here
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef LCD20x4  
const byte buzzerPin = 10;
#else
const byte buzzerPin = 4;
#endif

const byte ledPin = 13;

// Detect for RF signal
// byte rfSignal = 0;
// byte missedPackets = 0;

unsigned int txCounter = 0;
unsigned long txTimer = 0;
unsigned long lastTx = 0;
unsigned long txInterval = 80000L;  // Initial 80 secs internal

int lastCourse = 0;
byte lastSpeed = 0;
byte buttonPressed = 0;

static unsigned int Hd,Ti,Di,Bn = 0;

int previousHeading, currentHeading = 0;
// Initial lat/lng pos, change to your base station coordnates
float lastTxLat = HOME_LAT;
float lastTxLng = HOME_LON;
float lastTxdistance, homeDistance = 0.0;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////


const unsigned int MAX_DEBUG_INPUT = 30;

void setup()
{
  
  
#if defined(__arm__) && defined(TEENSYDUINO)
// This is for reading the internal reference voltage
  analogReference(EXTERNAL);
  analogReadResolution(12);
  analogReadAveraging(32);
#endif
  
#ifdef LCD20x4  
  // LCD format is Col,Row for 20x4 LCD
  lcd.begin(20,4);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(VERSION);
  // Insert GPS Simulator codes if defined 
  #ifdef GPSSIM     
    lcd.setCursor(0,3);
    lcd.print("GPS Sim begin"); 
    delay(1000);
    lcd.clear();
  #endif
#endif  


#ifndef LCD20x4  
  // Buzzer uses pin 4, conflicting with 20x4 LCD pins
  pinMode(buzzerPin, OUTPUT);
#endif


#ifdef TFT22
  tft.begin();    
  tft.setRotation(1);
  tft.setCursor(0,0);
  tft.setTextSize(3); 
  tft.setTextColor(ILI9340_WHITE);  
  tft.print("SVTrackR"); 
  delay(1000);
  tft.fillScreen(ILI9340_BLACK);
#endif

#ifndef TFT
  // LED pin on 13, only enable for non-SPI TFT
  pinMode(ledPin,OUTPUT);
#endif


  
  Serial.begin(9600);
#ifdef DEBUG
  debug.begin(9600);
#endif
  ss.begin(9600);

#ifdef DEBUG
  debug.flush();
  debug.println();
  debug.println();
  debug.println(F("=========================================="));
  debug.print(F("DEBUG:- ")); 
  debug.println(F(VERSION)); 
  debug.println(F("=========================================="));
  debug.println();
#endif

  // Set a delay for the MicroAPRS to boot up before configuring it
  delay(1000);
  configModem();
  
  txTimer = millis();



 
} // end setup()


///////////////////////////////////////////////////////////////////////////////////////////////////////
void loop()
{
    // Speed in km/h
    const byte highSpeed = 80;       // High speed  
    const byte lowSpeed = 30;        // Low speed
    char c;
    boolean inputComplete = false;
    int headingDelta = 0;
    
#ifdef DEBUG    
    // Send commands from debug serial into hw Serial char by char 
#if defined (__AVR_ATmega328P__)    
    debug.listen();
#endif  
    if ( debug.available() > 0  ) {
          processIncomingDebug(debug.read());
    }
#endif    
    
    // Turn on listen() on GPS
#if defined (__AVR_ATmega328P__)        
    ss.listen();  
#endif    
    while ( ss.available() > 0 ) {
      gps.encode(ss.read());
    }
    
///////////////// Triggered by location updates /////////////////////// 
   if ( gps.location.isUpdated() ) { 

     homeDistance = TinyGPSPlus::distanceBetween(
          gps.location.lat(),
          gps.location.lng(),
          HOME_LAT, 
          HOME_LON);   
          
    lastTxdistance = TinyGPSPlus::distanceBetween(
          gps.location.lat(),
          gps.location.lng(),
          lastTxLat,
          lastTxLng);
          
      // Get headings and heading delta
      currentHeading = (int) gps.course.deg();
      if ( currentHeading >= 180 ) { 
      currentHeading = currentHeading-180; 
      }
      headingDelta = (int) ( previousHeading - currentHeading ) % 360;   
     
    } // endof gps.location.isUpdated()

///////////////// Triggered by time updates /////////////////////// 
// Update LCD every second

   if ( gps.time.isUpdated() ) {   
    
   // Turn on LED 13 when Satellites more than 3  
   // Disable when using TFT / SPI
#ifndef TFT 
   if ( gps.satellites.value() > 3 ) {
     digitalWrite(ledPin,HIGH);  
   } else {
     digitalWrite(ledPin,LOW);     
   }
#endif

#ifdef TFT22
      //tft.fillScreen(ILI9340_BLACK);
      tft.setCursor(0,0);

      tft.setTextSize(3);      
      tft.setTextColor(ILI9340_CYAN);  
      tft.print("9W2");    
      tft.setTextColor(ILI9340_YELLOW);   
      tft.print("SVT");  
      
      tft.setTextColor(ILI9340_RED);   
      tft.print("APRS");  
      
      tft.setTextColor(ILI9340_GREEN);   
      tft.println("TrackR");  

      tft.setCursor(0,22);
      tft.setTextSize(2);
      tft.setTextColor(ILI9340_WHITE);  
      tft.print("Date:");
      tft.setTextColor(ILI9340_YELLOW);
      tft.print(gps.date.value());
      tft.setTextColor(ILI9340_WHITE);
      tft.print(" Time:");
      tft.setTextColor(ILI9340_YELLOW);
      tft.println(gps.time.value());
      
      tft.setCursor(0,32);
      tft.setTextSize(3);
      tft.setTextColor(ILI9340_WHITE);
      tft.print("Lat:");
      tft.setTextColor(ILI9340_GREEN);  
      tft.println(gps.location.lat(),5);

      tft.setCursor(0,52);      
      tft.setTextColor(ILI9340_WHITE);     
      tft.print("Lng:");
      tft.setTextColor(ILI9340_GREEN);  
      tft.println(gps.location.lng(),5);

      tft.setCursor(0,100);
      tft.setTextSize(2);      
      tft.setTextColor(ILI9340_WHITE);  
      tft.print("Sats:");
      tft.setTextColor(ILI9340_CYAN);
      tft.print(gps.satellites.value());
      
      tft.setTextColor(ILI9340_WHITE); 
      tft.print(" HDOP:");
      tft.setTextColor(ILI9340_CYAN);
      tft.print(gps.hdop.value());
      
      tft.setTextColor(ILI9340_WHITE); 
      tft.print(" Alt:");
      tft.setTextColor(ILI9340_CYAN);
      tft.println((int)gps.altitude.meters());

      tft.setCursor(0,100);
      tft.setTextSize(2);      
      tft.setTextColor(ILI9340_WHITE); 
      tft.print("Speed:");
      tft.setTextColor(ILI9340_CYAN);
      tft.print(gps.speed.kmph());

      tft.setTextColor(ILI9340_WHITE); 
      tft.print(" Deg:");
      tft.setTextColor(ILI9340_CYAN);
      tft.println(gps.course.deg());
      
      tft.setTextSize(1);      
      tft.setTextColor(ILI9340_WHITE); 
      tft.print("Passed:");
      tft.setTextColor(ILI9340_CYAN);
      tft.print(gps.passedChecksum()); 

      tft.setTextColor(ILI9340_WHITE); 
      tft.print(" Failed:");
      tft.setTextColor(ILI9340_CYAN);
      tft.println(gps.failedChecksum());       
#endif

#ifdef LCD20x4
     lcd.clear();
     lcd.setCursor(1,0);
     lcd.print("   ");
     //lcd.setCursor(1,0);
     //lcd.print(txCounter);

     lcd.setCursor(4,0);
     lcd.print("       ");
     lcd.setCursor(4,0);
     lcd.print(lastTx);
     
     lcd.setCursor(10,0);
     lcd.print("    ");  
     lcd.setCursor(10,0);
     lcd.print((int)lastTxdistance);    

     lcd.setCursor(14,0);
     lcd.print("    ");  
     lcd.setCursor(14,0);
     lcd.print((float)homeDistance/1000,1); 
     
     lcd.setCursor(18,0);   
     lcd.print("  "); 
     lcd.setCursor(18,0);   
     lcd.print(gps.satellites.value());
    
     lcd.setCursor(0,1);
     lcd.print("H:");
     lcd.print(Hd);
     lcd.print(" T:");
     lcd.print(Ti);
     lcd.print(" D:");
     lcd.print(Di);
     lcd.print(" B:");
     lcd.print(Bn);
     
     lcd.setCursor(0,2);
     lcd.print("S:");
     lcd.print((int) gps.speed.kmph());
     lcd.print(" H:");
     lcd.print((int) gps.course.deg()); 
     lcd.print(" Ttl:");
     lcd.print(txCounter);
 
     lcd.setCursor(0,3);
     lcd.print(gps.location.lat(),5);
     lcd.setCursor(10,3);
     lcd.print(gps.location.lng(),5);
     delay(30); // To see the LCD display, add a little delays here
#endif
     
// Change the Tx internal based on the current speed
// This change will not affect the countdown timer
// Based on HamHUB Smart Beaconing(tm) algorithm

      if ( gps.speed.kmph() < 5 ) {
            txInterval = 300000;         // Change Tx internal to 5 mins
       } else if ( gps.speed.kmph() < lowSpeed ) {
            txInterval = 70000;          // Change Tx interval to 60
       } else if ( gps.speed.kmph() > highSpeed ) {
            txInterval = 30000;          // Change Tx interval to 30 secs
       } else {
        // Interval inbetween low and high speed 
            txInterval = (highSpeed / gps.speed.kmph()) * 30000;       
       } // endif
      
   }  // endof gps.time.isUpdated()
     

/*    
///////////////// Triggered by course updates /////////////////////// 
     
     
    if ( gps.course.isUpdated() ) {
        // Get headings and heading delta
        currentHeading = (int) gps.course.deg();
        if ( currentHeading >= 180 ) { currentHeading = currentHeading-180; }
      
        headingDelta = (int) ( previousHeading - currentHeading ) % 360;     

    } // endof gps.course.isUpdated()
*/
          
 ////////////////////////////////////////////////////////////////////////////////////
 // Check for when to Tx packet
 ////////////////////////////////////////////////////////////////////////////////////
 
  lastTx = 0;
  lastTx = millis() - txTimer;

  // Only check the below if locked satellites < 3
#ifdef GPSSIM
   if ( gps.satellites.value() == 0 ) {
#else
   if ( gps.satellites.value() > 3 ) {
#endif    
    if ( lastTx > 5000 ) {
        // Check for heading more than 25 degrees
        if ( headingDelta < -25 || headingDelta >  25 ) {
              Hd++;
#ifdef DEBUG                
            debug.println(F("*** Heading Change "));
#endif            
#ifdef LCD20x4            
            lcd.setCursor(0,0);
            lcd.print("H");  
#endif            
            TxtoRadio();
            previousHeading = currentHeading;
            // Reset the txTimer & lastTX for the below if statements
            txTimer = millis(); 
            lastTx = millis() - txTimer;
        } // endif headingDelta
    } // endif lastTx > 5000
    
    if ( lastTx > 10000 ) {
         // check of the last Tx distance is more than 600m
         if ( lastTxdistance > 600 ) {  
              Di++;
#ifdef DEBUG                     
            debug.println();
            debug.println(F("*** Distance > 600m ")); 
            debug.print(F("lastTxdistance:"));
            debug.println(lastTxdistance);
#endif          
#ifdef LCD20x4                        
            lcd.setCursor(0,0);
            lcd.print("D");
#endif            
            TxtoRadio();
            lastTxdistance = 0;   // Ensure this value is zero before the next Tx
            // Reset the txTimer & lastTX for the below if statements            
            txTimer = millis(); 
            lastTx = millis() - txTimer;
         } // endif lastTxdistance
    } // endif lastTx > 10000
    
    if ( lastTx >= txInterval ) {
        // Trigger Tx Tracker when Tx interval is reach 
        // Will not Tx if stationary bcos speed < 5 and lastTxDistance < 20
        if ( lastTxdistance > 20 ) {
              Ti++;
#ifdef DEBUG                    
                   debug.println();
                   debug.print(F("lastTx:"));
                   debug.print(lastTx);
                   debug.print(F(" txInterval:"));
                   debug.print(txInterval);     
                   debug.print(F(" lastTxdistance:"));
                   debug.println(lastTxdistance);               
                   debug.println(F("*** txInterval "));  

#endif                   
#ifdef LCD20x4            
                   lcd.setCursor(0,0);
                   lcd.print("T");    
#endif                   
                   TxtoRadio(); 
                   
                   // Reset the txTimer & lastTX for the below if statements   
                   txTimer = millis(); 
                   lastTx = millis() - txTimer;
        } // endif lastTxdistance > 20 
    } // endif of check for lastTx > txInterval

    } // Endif check for satellites
    // Check if the analog0 is plugged into 5V and more than 10 secs
    if ( analogRead(0) > 700 && (lastTx > 10000) ) {
          Bn++;
          buttonPressed = 1;
#ifdef DEBUG                
                debug.println();             
                debug.println(analogRead(0));
                debug.println(F("*** Button ")); 
 
#endif                
#ifdef LCD20x4                            
                lcd.setCursor(0,0);
                lcd.print("B");     
#endif                     
                TxtoRadio(); 
                // Reset the txTimer & lastTX for the below if statements  
                txTimer = millis(); 
                lastTx = millis() - txTimer;
     } // endif check analog0

     
} // end loop()

///////////////////////////////////////////////////////////////////////////////////////////////////////

void serialEvent() {
// Disable Serial Decode  
#ifdef LCD20x4
    decodeAPRS(); 
#endif    
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
//  Function to Tx to Radio
///////////////////////////////////////////////////////////////////////////////////////////////////////


void TxtoRadio() {
  
     char tmp[10];
     float latDegMin, lngDegMin = 0.0;
     String latOut, lngOut, cmtOut = "";
     unsigned int Mem = freeRam();
     float Volt = (float) readVcc()/1000;
  
     lastTxLat = gps.location.lat();
     lastTxLng = gps.location.lng();

     /*
     // Check for Rx APRS packets from Modem
     if ( !rfSignal ) {
          missedPackets++;
#ifdef DEBUG                 
       debug.print("missedPackets:");
       debug.println(missedPackets);
#endif       
     }
     // Reset the rfSignal flag
     rfSignal = 0;
     
     if ( missedPackets > 5 ) {
      
      // Store all the Tx packets into an array with timestamped
      //  
     
      missedPackets = 0;  // Reset the counter
     }
     else 
     */
     
     if ( lastTx >= 5000 ) { // This prevent ANY condition to Tx below 5 secs
#ifdef DEBUG                 
       debug.print("Time/Date: ");
       byte hour = gps.time.hour() +8; // GMT+8 is my timezone
       byte day = gps.date.day();
       
       if ( hour > 23 ) {
           hour = hour -24;
           day++;
       }  
       if ( hour < 10 ) {
       debug.print("0");       
       }       
       debug.print(hour);         
       debug.print(":");  
       if ( gps.time.minute() < 10 ) {
       debug.print("0");       
       }
       debug.print(gps.time.minute());
       debug.print(":");
       if ( gps.time.second() < 10 ) {
       debug.print("0");       
       }       
       debug.print(gps.time.second());
       debug.print(" ");

       if ( day < 10 ) {
       debug.print("0");
       }         
       debug.print(day);
       debug.print("/");
       if ( gps.date.month() < 10 ) {
       debug.print("0");       
       }
       debug.print(gps.date.month());
       debug.print("/");
       debug.print(gps.date.year());
       debug.println();

       debug.print("GPS: ");
       debug.print(lastTxLat,5);
       debug.print(" ");
       debug.print(lastTxLng,5);
       debug.println();

       debug.print("Sat:");           
       debug.print(gps.satellites.value());
       debug.print(" HDOP:");
       debug.print(gps.hdop.value());
       debug.print(" km/h:");           
       debug.print((int) gps.speed.kmph());
       debug.print(" Head:");
       debug.print((int) gps.course.deg());          
       debug.print(" Alt:");
       debug.print(gps.altitude.meters());
       debug.print("m");
       debug.println();

       debug.print(F("Distance(m): Home:"));
       debug.print(homeDistance,2);
       debug.print(" Last:");  
       debug.print(lastTxdistance,2);
       debug.println(); 
     
       debug.print(F("Tx since "));  
       debug.print((float)lastTx/1000); 
       debug.println(" sec");   
#endif             
       // Turn on the buzzer
       digitalWrite(buzzerPin,HIGH);  

       // Only send status/version every 10 packets to save packet size  
       if ( ( txCounter % 10 == 0 ) || buttonPressed ) {

          float base = TinyGPSPlus::distanceBetween(
          gps.location.lat(),
          gps.location.lng(),
          HOME_LAT, 
          HOME_LON)/1000;  
          
          float r1 = TinyGPSPlus::distanceBetween(
          gps.location.lat(),
          gps.location.lng(),
          RKK_LAT, 
          RKK_LON)/1000;            

          float r2 = TinyGPSPlus::distanceBetween(
          gps.location.lat(),
          gps.location.lng(),
          RDG_LAT, 
          RDG_LON)/1000; 
          
          float r3 = TinyGPSPlus::distanceBetween(
          gps.location.lat(),
          gps.location.lng(),
          RTB_LAT, 
          RTB_LON)/1000; 

         cmtOut.concat("!>");                
         cmtOut.concat(VERSION);       
         cmtOut.concat(Volt);
         cmtOut.concat("V S:");
         cmtOut.concat(gps.satellites.value());
         cmtOut.concat(" B:");         
         cmtOut.concat(base);
         cmtOut.concat("/");         
         cmtOut.concat(r1);
         cmtOut.concat("/");         
         cmtOut.concat(r2);
         cmtOut.concat("/");         
         cmtOut.concat(r3);         
#ifdef DEBUG          
       debug.print("TX STR: ");
       debug.print(cmtOut);  
       debug.println(); 
#endif               
        Serial.println(cmtOut);
        delay(1000);   
        cmtOut = ""; 
       } 
       
       
       latDegMin = convertDegMin(lastTxLat);
       lngDegMin = convertDegMin(lastTxLng);

       dtostrf(latDegMin, 2, 2, tmp );
       latOut.concat("lla0");      // set latitute command with the 0
       latOut.concat(tmp);
       latOut.concat("N");
     
       dtostrf(lngDegMin, 2, 2, tmp );
       lngOut.concat("llo");       // set longtitute command
       lngOut.concat(tmp);
       lngOut.concat("E");
     
       cmtOut.concat("@");
       cmtOut.concat(padding((int) gps.course.deg(),3));
       cmtOut.concat("/");
       cmtOut.concat(padding((int)gps.speed.mph(),3));
       cmtOut.concat("/A=");
       cmtOut.concat(padding((int)gps.altitude.feet(),6));
       cmtOut.concat(" Seq:");
       cmtOut.concat(txCounter);       

#ifdef DEBUG          
       debug.print("TX STR: ");
       debug.print(latOut);  
       debug.print(" ");       
       debug.print(lngOut);  
       debug.print(" ");
       debug.print(cmtOut);  
       debug.println(); 
#endif         
       
       // This condition is ONLY for button pressed ( do not sent out position if not locked )
       if ( gps.satellites.value() > 3 ) {

       Serial.println(latOut);
       delay(200);
       Serial.println(lngOut);
       delay(200);
       Serial.println(cmtOut);
       delay(200);

       }
       
       digitalWrite(buzzerPin,LOW);     
       // Reset the txTimer & Tx internal   
    
       txInterval = 80000;
       buttonPressed = 0;
       lastTx = 0;
#ifdef DEBUG               
       debug.print(F("FreeRAM:"));
       debug.print(Mem);
       debug.print(" Uptime:");
       debug.println((float) millis()/1000);
       debug.println(F("=========================================="));
#endif     

       txCounter++;
     } // endif lastTX
     
     
} // endof TxtoRadio()

///////////////////////////////////////////////////////////////////////////////////////////////////////

void processDebugData(const char * data) {
  // Send commands to modem
  Serial.println(data);
}  // end of processDebugData
  
///////////////////////////////////////////////////////////////////////////////////////////////////////
  

void processIncomingDebug(const byte inByte) {

  static char input_line [MAX_DEBUG_INPUT];
  static unsigned int input_pos = 0;

  switch (inByte)
    {
    case '\n':   // end of text
      input_line[input_pos] = 0;  // terminating null byte 
      // terminator reached! Process the data
      processDebugData(input_line);
      // reset buffer for next time
      input_pos = 0;  
      break;
    case '\r':   // discard carriage return
      break;
    default:
      // keep adding if not full ... allow for terminating null byte
      if (input_pos < (MAX_DEBUG_INPUT - 1))
        input_line [input_pos++] = inByte;
      break;
    }  // end of switch
   
} // endof processIncomingByte  
  
///////////////////////////////////////////////////////////////////////////////////////////////////////


void configModem() {
// Functions to configure the callsign, ssid, path and other settings
// c<callsign>
// sc<ssid>
// pd0 - turn off DST display
// pp0 - turn on PATH display


#ifdef LCD20x4                            
  lcd.setCursor(0,1);
  lcd.print("Configuring modem");
#endif                            


  Serial.print("c");  // Set SRC Callsign
  Serial.println(MYCALL);  // Set SRC Callsign
  delay(200);
  Serial.print("sc");      // Set SRC SSID
  Serial.println(CALL_SSID);      // Set SRC SSID
  delay(200);
  Serial.println("pd0");      // Disable printing DST 
  delay(200);
  Serial.println("pp0");      // Disable printing PATH
  delay(200);
  Serial.print("ls");      // Set symbol n / Bluedot
  Serial.println(SYMBOL_CHAR);      // Set symbol n / Bluedot
  delay(200);
  Serial.print("lt");      // Standard symbol 
  Serial.println(SYMBOL_TABLE);      // Standard symbol 
  delay(200);
  Serial.println("V1");      // Silent Mode ON 
  delay(200);
  //Serial.println("H");        // Print out the Settings
  //Serial.println("S");        // Save config
  
#ifdef LCD20x4                              
  lcd.setCursor(0,2);
  lcd.print("Done................");
  delay(500); 
#endif                             
  
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

void decodeAPRS() {
      // Dump whatever on the Serial to LCD line 1
      char c;
      String decoded="";
      int callIndex,callIndex2, dataIndex = 0;

      while ( Serial.available() > 0 ) {
         c = Serial.read();
//#ifdef DEBUG         
//         debug.print(c);
//#endif         
         decoded.concat(c); 
         // rfSignal = 1;
      }   

#ifdef DEBUG   
      debug.print("Decoded Packets:");
      debug.println(decoded);
#endif   
      callIndex = decoded.indexOf("[");
      callIndex2 = decoded.indexOf("]");
      String callsign = decoded.substring(callIndex+1,callIndex2);
      
      dataIndex = decoded.indexOf("DATA:");
      String data = decoded.substring(dataIndex+6,decoded.length());

      String line1 = data.substring(0,20);
      String line2 = data.substring(21,40);
      
#ifdef LCD20x4                                  
      lcd.setCursor(0,1);
      lcd.print("                    ");
      lcd.setCursor(0,1);
      lcd.print(callsign);
      lcd.setCursor(0,2);
      lcd.print("                    ");
      lcd.setCursor(0,2);      
      lcd.print(line1);
      lcd.setCursor(0,3);
      lcd.print("                    ");
      lcd.setCursor(0,3);
      lcd.print(line2);
#endif       

#ifdef DEBUG         
         debug.print(F("Callsign:"));
         debug.println(callsign);
         debug.print(F("Data:"));
         debug.println(data);
#endif  

#ifdef TFT22
      tft.drawLine(0,199,319,199, ILI9340_WHITE);
      tft.setCursor(0,200);
      tft.setTextSize(2);      
      tft.setTextColor(ILI9340_GREEN);  
      tft.print("Callsign:");
      tft.setTextColor(ILI9340_CYAN);
      tft.print(callsign);
      
      tft.setTextColor(ILI9340_WHITE);  
      tft.print(" Data:");
      tft.setTextColor(ILI9340_CYAN);
      tft.println(data);   
  
#endif

}

///////////////////////////////////////////////////////////////////////////////////////////////////////

float convertDegMin(float decDeg) {
  
  float DegMin;
  
  int intDeg = decDeg;
  decDeg -= intDeg;
  decDeg *= 60;
  DegMin = ( intDeg*100 ) + decDeg;
 
 return DegMin; 
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

long readVcc() {                 
  long result;  
#if defined(__arm__) && defined(TEENSYDUINO)
    extern "C" char* sbrk(int incr);
    result = 1195 * 4096 /analogRead(39);
#else
  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2);                     // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  result = 1126400L / result; // Back-calculate AVcc in mV  
#endif  

  return result;
}

String padding( int number, byte width ) {
  String result;
  
  // Prevent a log10(0) = infinity
  int temp = number;
  if (!temp) { temp++; }
    
  for ( int i=0;i<width-(log10(temp))-1;i++) {
       result.concat('0');
  }
  result.concat(number);
  return result;
}


int freeRam() {
#if defined(__arm__) && defined(TEENSYDUINO)
  char top;
        return &top - reinterpret_cast<char*>(sbrk(0));
#else  // non ARM, this is AVR
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
#endif  
}

