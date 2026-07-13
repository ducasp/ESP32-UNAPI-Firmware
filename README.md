# ESP32 UNAPI Firmware

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/R6R2BRGX6)

A sample TCP IP and SSH UNAPI driver that work along with this firmware
is available at:

(ROM Version)
https://github.com/ducasp/MSX-Development/tree/master/UNAPI_BIOS_CUSTOM_ESP_V2

This application is an example of how to set configurations, scan and join networks or update firmware or certificates on IO interface:
https://github.com/ducasp/MSX-Development/tree/master/CFGESP

Memory mapped IO interface version:
https://github.com/ducasp/MSX-Development/tree/master/UPDTESP

# How to build it

You will need Arduino IDE and install ESP32 Arduino IDE and LibSSH-ESP32

I STRONGLY RECOMMEND that you stick to LibSSH-ESP32 5.8.0 and update it with the changed files in the LibSSH-ESP32-Modifications folder,
those changes greatly reduce memory heap fragmentation and allow stable SSH connections for really long periods.

Choose the proper ESP32 module/board you are targeting.

To generate certificates file, first get the most up to date certificate list in PEM format:

https://curl.se/docs/caextract.html

Then use the script available at:

https://github.com/espressif/esp-idf/blob/master/components/mbedtls/esp_crt_bundle/gen_crt_bundle.py

To get the certificates in a single bundle file. That file you can rename to certs.bin and then upload
it to ESP using a tool like CFGESP / UPDTESP locally or through a web server.

# Project Design Constraints

This firmware was designed taking into consideration that the other end connected to it either has
a large 2KB reception buffer OR is fast enough to pick-up data at the desired baud rate without the
help of such large buffer.

Copyright (c) 2019 - 2026 Oduvaldo Pavan Junior ( ducasp@ gmail.com )
All rights reserved.

If you integrate this on your hardware, please consider the possibility of sending one piece of it
as a thank you to the author :) Of course this is not mandatory, if you like the idea, contact the
author in the e-mail address above.

This software uses ESP32 for Arduino IDE library, from ESPRESSIF.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as 
published by the Free Software Foundation, either version 2.1 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>
