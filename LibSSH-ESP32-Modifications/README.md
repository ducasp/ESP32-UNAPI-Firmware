# ESP32 UNAPI Firmware LibSSH-ESP32-Modifications

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/R6R2BRGX6)

Those changes improve the fragmentation caused in the memory heap due to reallocations and resizing
of buffers.


# How to build it

Install LibSSH-ESP32 5.8.0 on your Arduino IDE environment. Copy the files to the folder Arduino\libraries\LibSSH-ESP32\src
in your Documents folder. If you have already built LibSSH once, you will need to force a clean rebuild, just hit
CTRL + SHIFT + R and pick your poison to drink while it rebuilds (might take a while).