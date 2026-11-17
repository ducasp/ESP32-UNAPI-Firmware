# ESP32 UNAPI Firmware Arduino ESP32 Core Modifications

The ESP32 firmware needs one small addition to the Arduino ESP32 core `Network`
library: a public accessor for the underlying lwIP socket descriptor of the
`NetworkUDP` class.

This is required by the non destructive UDP `UDP_STATE` / `UDP_STATEX` routines:
with the socket descriptor the firmware can peek the oldest pending datagram
using `recvfrom(..., MSG_PEEK, ...)`, which does not consume, reorder or alter
any datagram. The upstream `NetworkUDP::parsePacket()` is destructive (it
removes the datagram from the socket into an internal buffer) and returns 0 if
a datagram is already buffered, so calling it from a state query loses data.

## What changes

Only one line is added to `libraries/Network/src/NetworkUdp.h`, in the public
section of the `NetworkUDP` class:

```cpp
int fd() const { return udp_server; }
```

The file in this folder is the full patched header, kept here so the change is
easy to review and reproduce.

## How to apply

1. Locate the Arduino ESP32 core installation. In the Arduino IDE the path is
   usually:
   - Windows: `%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\<version>\libraries\Network\src\NetworkUdp.h`
   - Linux: `~/.arduino15/packages/esp32/hardware/esp32/<version>/libraries/Network/src/NetworkUdp.h`
   - macOS: `~/Library/Arduino15/packages/esp32/hardware/esp32/<version>/libraries/Network/src/NetworkUdp.h`
2. Replace that `NetworkUdp.h` with the one from this folder (or just add the
   `fd()` line manually if your core version differs from the one here).
3. Do a clean rebuild (Arduino IDE: Sketch > Clean Build Folder, or close and
   reopen the IDE) so the patched header is picked up by every translation
   unit.

## Tested core versions

The header in this folder corresponds to the `Network` library from the
arduino-esp32 3.x series (the firmware itself already requires 3.x APIs).
If you use a different 3.x version and prefer not to replace the whole file,
apply the one line change above to your own copy of `NetworkUdp.h` - that is
all the firmware needs.

## Note

If the patch is not applied, the build fails at the `UdpX.fd()` calls in
`ESP32-UNAPI-Firmware.ino`, which is intentional: the firmware must not fall
back to the destructive `parsePacket()` behaviour for state queries.
