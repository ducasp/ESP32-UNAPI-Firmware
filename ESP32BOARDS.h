/*
ESP32BOARDS.h
    ESP32 UNAPI Implementation.
    Revision 1.00

Requires Arduino IDE and ESP32 libraries

Copyright (c) 2019 - 2026 Oduvaldo Pavan Junior ( ducasp@ gmail.com )
Copyright (c) 2026 - Leo Manes ( https://github.com/leomanes )
All rights reserved.

If you integrate this on your hardware, please consider the 
possibility of sending one piece of it as a thank you to the author :)
Of course this is not mandatory, if you like the idea, contact the
author in the e-mail address above.

This file, that is part of ESP8266 UNAPI Firmware program, is free
software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free
Software Foundation, either version 2.1 of the License, or (at your
option) any later version.

This file is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this file.  If not, see <https://www.gnu.org/licenses/>

Edit this file to choose your target processor and FLASH size
If for some reason you are using a target not here, add the new
target following the examples of the existing targets and request
an update on the main repository please.

I do not recommend using 4M flash as the firmware for most platforms
currently has over 1M (and considering space for OTA and file system)
leaving little room for new features.

Also edit this file to select the LED behavior you want and the GPIO
PIN and type of led use.
*/

#ifndef _ESP32BOARDS_H
#define _ESP32BOARDS_H

//Baud Rates
#define B9600 9600
#define B19200 19200
#define B57600 57600
#define B115200 115200
#define B230400 230400
#define B460800 460800
#define B921600 921600
#define B859372 859372

//Choose the default baud rate
//Notice that if replacing a legacy ESP-01 on MSX FPGA or MSX PICO, the default is 859372 for it
//Notice that in my tests 859372 did not work nicely with ESP32-WROOM but worked fine with ESP32C6 and S3
//921600 should work nice on all of them
#define ESP32BAUDRATE B859372

//Uncomment ONLY the ESP Model you are targetting
#define ESP32_C6
//#define ESP32_S3
//#define ESP32_WROOM

//Uncomment ONLY the Flash Size you are targetting
//#define FLASH_4M
//#define FLASH_8M
#define FLASH_16M

#ifdef ESP32_S3
  #ifdef FLASH_4M
    #define FIRMWARETYPE "ESP32S3-4M"
  #endif
  #ifdef FLASH_8M
    #define FIRMWARETYPE "ESP32S3-8M"
  #endif
  #ifdef FLASH_16M
    #define FIRMWARETYPE "ESP32S3-16M"
  #endif
#endif

#ifdef ESP32_C6
  #ifdef FLASH_4M
    #define FIRMWARETYPE "ESP32C6-4M"
  #endif
  #ifdef FLASH_8M
    #define FIRMWARETYPE "ESP32C6-8M"
  #endif
  #ifdef FLASH_16M
    #define FIRMWARETYPE "ESP32C6-16M"
  #endif
#endif

#ifdef ESP32_WROOM
  #ifdef FLASH_4M
    #define FIRMWARETYPE "ESP32WROOM-4M"
  #endif
  #ifdef FLASH_8M
    #define FIRMWARETYPE "ESP32WROOM-8M"
  #endif
  #ifdef FLASH_16M
    #define FIRMWARETYPE "ESP32WROOM-16M"
  #endif
#endif

// ======================== ONBOARD WIFI-CONNECTED LED ========================
// Blue led if STA has an IP. "WiFi up"
// firmware OTA it should reconnect on its own after reboot
//
// Configure for your board:
//   WIFI_LED_PIN   pin number (LED_BUILTIN if WS2812 RGB on GPIO 48)
//   WIFI_LED_RGB   set to 1 if the onboard LED is a WS2812
//   WIFI_LED_ON    HIGH/LOW
// If you do not want to have LED support, comment the line below
#define USE_WIFI_LED
#ifndef WIFI_LED_PIN
  #ifdef ESP32_S3
    #define WIFI_LED_PIN  48      // ESP32-S3-Dev: WS2812 on GPIO 48
  #endif
  #ifdef ESP32_C6
    #define WIFI_LED_PIN  8       // ESP32-C6-Dev: WS2812 on GPIO 8
  #endif
  #ifdef ESP32_WROOM
    #define WIFI_LED_PIN  2       // Most ESP32-WROOM have it on PIN 2
  #endif
#endif    
#ifndef WIFI_LED_RGB
  #ifdef ESP32_S3
    #define WIFI_LED_RGB  1       // 1 = WS2812; 0 = plain GPIO LED
  #endif
  #ifdef ESP32_C6
    #define WIFI_LED_RGB  1       // 1 = WS2812; 0 = plain GPIO LED
  #endif
  #ifdef ESP32_WROOM
    #define WIFI_LED_RGB  0       // 1 = WS2812; 0 = plain GPIO LED
  #endif
#endif
#ifndef WIFI_LED_ON
  #ifdef ESP32_S3
    #define WIFI_LED_ON   HIGH
  #endif
  #ifdef ESP32_C6
    #define WIFI_LED_ON   HIGH
  #endif
  #ifdef ESP32_WROOM
    #define WIFI_LED_ON   HIGH
  #endif
#endif


#endif
