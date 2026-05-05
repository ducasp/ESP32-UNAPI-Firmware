/*
ESP32-UNAPI-Firmware.ino
    ESP32 UNAPI Implementation.
    Revision 1.1

Requires Arduino IDE and ESP32 libraries

Copyright (c) 2019 - 2026 Oduvaldo Pavan Junior ( ducasp@ gmail.com )
Copyright (c) 2026 - Leo Manes ( https://github.com/leomanes )
All rights reserved.

HTTP functionality
Copyright (c) 2025 Jeroen Taverne
All rights reserved.

If you integrate this on your hardware, please consider the 
possibility of sending one piece of it as a thank you to the author :)
Of course this is not mandatory, if you like the idea, contact the
author in the e-mail address above.

This file, that is part of ESP32 UNAPI Firmware program, is free
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

v1.0
    - Initial ESP32 support, no HTTPS, no firmware update

v1.1 (Merging with Leo Manes ESP32 Port)
    - HTTPS with TLS 1.2 by Leo Manes (WiFiClientSecure / mbedTLS)
    - Changed LittleFS to FFat (APP:I set to 3MB / OTA:above 1.5MB for now)
    - Added missing WiFiClientSecure include
    - Fixed Update.begin() parameters (U_SPIFFS instead of U_FS)
    - Fixed IPAddress comparisons
    - Added online led (blue using the onboard WS2812)
*/

#include "UNAPIESP.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

#include <FFat.h>
#include <Ticker.h>
#include <time.h>
#include <EEPROM.h>
#include "HTTPClient.h"

#define HTTP_PACKET_SIZE 2048

WiFiClientSecure *TClient1;
static char *g_caPem = NULL;
static size_t g_caPemLen = 0;
static uint8_t *g_caBundle = NULL;
static size_t g_caBundleLen = 0;
unsigned char bTLSInUse = 0;
unsigned char uchTLSHost[256];
bool bHasHostName = false;

const char chVer[4] = "1.1";
byte btConnections[4] = {CONN_CLOSED,CONN_CLOSED,CONN_CLOSED,CONN_CLOSED};
//UDP Objects
WiFiUDP Udp1, Udp2, Udp3, Udp4;
//TCP Connection Objects
WiFiClient * ClientList[4] = {NULL, NULL, NULL, NULL};
//Passive TCP Connection Objects
WiFiServer * ServerList[4] = {NULL, NULL, NULL, NULL};
byte btUDPConnections[4] = {CONN_CLOSED,CONN_CLOSED,CONN_CLOSED,CONN_CLOSED};
byte btIsTransient[4] = {0,0,0,0};
unsigned int uiLocalPorts[4] = {0,0,0,0};
unsigned int uiRemotePorts[4] = {0,0,0,0};
unsigned int uiListenPorts[4] = {0,0,0,0};
static byte btIPConnection1[4] = {0,0,0,0};
static byte btIPConnection2[4] = {0,0,0,0};
static byte btIPConnection3[4] = {0,0,0,0};
static byte btIPConnection4[4] = {0,0,0,0};
IPAddress externalIP,DNSQueryIP;
bool bIPAutomatic = true;
bool bDNSAutomatic = true;
bool bSerialUpdateInProgress = false;
// True when RS232 update is a CERT update (Y/z/E flow)
// rather than a firmware update (Z/z/E flow). The cert path streams bytes
// to a file on FFat instead of using Update.begin/write/end, because the
// ESP8266 reference's certs.bin is a LittleFS partition image which is
// neither format-compatible with our FFat partition nor with mbedTLS.
bool bSerialCertUpdateInProgress = false;
File g_certUploadFile;
unsigned char ucReceivedCommand = NO_COMMAND;
unsigned char ucLastCmd, ucLastResponse;
unsigned int uiLastDataSize;
unsigned char *ucLastData;
ESPConfig stDeviceConfiguration;
static byte btState = RX_PARSER_IDLE;
byte btReadyRetries;
byte btReceivedCommand;
bool bWiFiOn = false;
Ticker tickerRadioOff;
// Set from Ticker (esp_timer task) context; consumed in loop() to keep WiFi
// stack calls on the Arduino main task.
volatile bool bDisableRadioPending = false;
unsigned long longTimeOut;
unsigned long longReadyTimeOut;
struct tm timeinfo;
bool bSNTPOK = false;
bool bHoldConnection = false;
time_t now;
bool bSkipStateCheck = false;
// HTTP globals
static bool http_connected = false;
static HTTPClient http;
static WiFiClient client;
static WiFiClient *stream = NULL;
static unsigned int http_chunk_size = 0;
static unsigned int http_chunk_header_idx = 0;
static unsigned long longTimeOut2;
uint8_t buffer[1024];

// ======================== Helper Functions ========================
void WaitConnectionIfNeeded(bool bFastReturn);
void DisableRadio();
void RadioUpdateStatus();
void ScheduleTimeoutCheck();
bool CacheCertificates();

// ======================== ONBOARD WIFI-CONNECTED LED ========================
// Blue led if STA has an IP. "WiFi up"
// firmware OTA it should reconnect on its own after reboot
//
// Configure for your board:
//   WIFI_LED_PIN   pin number (LED_BUILTIN if WS2812 RGB on GPIO 48)
//   WIFI_LED_RGB   set to 1 if the onboard LED is a WS2812
//   WIFI_LED_ON    HIGH/LOW
#define USE_WIFI_LED
#ifndef WIFI_LED_PIN
  #ifdef ESP_S3_BOARD
    #define WIFI_LED_PIN  48      // ESP32-S3-Dev: WS2812 on GPIO 48
  #endif
  #ifdef ESP_C6_BOARD
    #define WIFI_LED_PIN  8      // ESP32-C6-Dev: WS2812 on GPIO 8
  #endif
#endif    
#ifndef WIFI_LED_RGB
  #define WIFI_LED_RGB  1       // 1 = WS2812; 0 = plain GPIO LED
#endif
#ifndef WIFI_LED_ON
  #ifdef ESP_S3_BOARD
    #define WIFI_LED_ON   HIGH
  #endif
  #ifdef ESP_C6_BOARD
    #define WIFI_LED_ON   HIGH
  #endif
#endif

static inline void wifiLedSet(bool on) {
#ifdef USE_WIFI_LED
#if WIFI_LED_RGB
  // blue at 50% = connected (R=0, G=0, B=128)
  rgbLedWrite(WIFI_LED_PIN, 0, 0, on ? 128 : 0);
#else
  digitalWrite(WIFI_LED_PIN, on ? WIFI_LED_ON : !WIFI_LED_ON);
#endif
#endif
}

static inline void wifiLedInit() {
#ifdef USE_WIFI_LED
#if !WIFI_LED_RGB
  pinMode(WIFI_LED_PIN, OUTPUT);
#endif
  wifiLedSet(false);
#endif
}

static byte http_open(const char *url) {
  http_close();

  WaitConnectionIfNeeded(false);

  if (WiFi.status() != WL_CONNECTED) {
    return http_close(UNAPI_ERR_NO_NETWORK);
  }

  if (!http.begin(client, url)) {
    return http_close(UNAPI_ERR_NO_CONN);
  }

  if (http.GET() != HTTP_CODE_OK) {
    return http_close(UNAPI_ERR_NO_DATA);
  }

  http_connected = true;
  stream = http.getStreamPtr();
  http_chunk_size = 0;
  http_chunk_header_idx = 0;

  return UNAPI_ERR_OK;
}

static byte http_receive(byte *btCommandData, unsigned int *uiCmdDataLen, bool read_text) {
  static char http_chunk_header[8];
  unsigned int idx = 0;

  while (1) {
    if (WiFi.status() != WL_CONNECTED) {
      *uiCmdDataLen = idx;
      return http_close(UNAPI_ERR_NO_NETWORK);
    }

    if (!http.connected() || !http_connected) {
      *uiCmdDataLen = idx;
      return http_close(UNAPI_ERR_NO_CONN);
    }

    unsigned int available = stream->available();
    while (available--) {
      uint8_t data = stream->read();
      if (http_chunk_size > 0) {
        http_chunk_size--;
        if (read_text && data == 10) {
          btCommandData[idx++] = 0;
          *uiCmdDataLen = idx;
          return UNAPI_ERR_OK;
        }
        btCommandData[idx++] = data;
        if (idx == HTTP_PACKET_SIZE) {
          *uiCmdDataLen = idx;
          return UNAPI_ERR_OK;
        }
      } else {
        if (http_chunk_header_idx > 0 && data == 10) {
          http_chunk_header[http_chunk_header_idx] = 0;
          sscanf(http_chunk_header, "%x", &http_chunk_size);
          http_chunk_header_idx = 0;
          if (http_chunk_size == 0) {
            *uiCmdDataLen = idx;
            if (idx > 0) {
              return UNAPI_ERR_OK;
            } else {
              return http_close(UNAPI_ERR_NO_DATA);
            }
          }
        } else {
          if (data > 32) {
            if (http_chunk_header_idx < sizeof(http_chunk_header)) {
              http_chunk_header[http_chunk_header_idx++] = data;
            }
          }
        }
      }
    }
  }
}

static void http_close(void) {
  http_connected = false;
  http.end();
}

static uint8_t http_close(uint8_t err) {
  http_close();
  return err;
}

// UNAPI send helpers - Bad IP translation fixed
// Extract the 4 raw IP bytes from an IPAddress. We can't just memcpy or cast
// &ip to uint8_t* like before. arduino-esp32's IPAddress Printable so DNS responses returning
// flash-code addresses like 0x3C0E6F20 instead of real IPs.)
static inline void ipToBytes(const IPAddress& ip, uint8_t* out) {
  out[0] = ip[0];
  out[1] = ip[1];
  out[2] = ip[2];
  out[3] = ip[3];
}

unsigned char getConnStatus() {
    wl_status_t st = WiFi.status();
    switch (st) {
        case WL_CONNECTED:      return 5; // STATION_GOT_IP
        case WL_CONNECT_FAILED: return 4; // STATION_CONNECT_FAIL
        case WL_NO_SSID_AVAIL:  return 3; // STATION_NO_AP_FOUND
        case WL_DISCONNECTED:   return 1; // STATION_CONNECTING
        case WL_IDLE_STATUS:    return 0; // STATION_IDLE
        default:                return 2; //Probably wrong password, not connected but SSID is available
  }
}

// Set time via NTP, as required for x.509 validation
void setClock(bool bFastReturn = false) {
  unsigned int uiRetryCount = 20;
  if (bFastReturn)
    uiRetryCount = 8;
  configTime(0,0, "pool.ntp.org");
 
  now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    now = time(nullptr);
    uiRetryCount--;
    if (uiRetryCount==0)
      break;
  }

  if (now > 8 * 3600 * 2)
    bSNTPOK = true;
  else
    bSNTPOK = false;
  
  gmtime_r(&now, &timeinfo);
}

void SendResponse (unsigned char ucCmd, unsigned char ucResponse, unsigned int uiDataSize, unsigned char *ucData) {
  ucReceivedCommand = REGULAR_COMMAND;
  ucLastCmd = ucCmd;
  ucLastResponse = ucResponse;
  uiLastDataSize = uiDataSize;
  ucLastData = ucData;
  Serial.write(ucCmd);
  Serial.write(ucResponse);
  Serial.write((uint8_t)((uiDataSize>>8)&0xff));
  Serial.write((uint8_t)(uiDataSize&0xff));
  if (uiDataSize)
    Serial.write(ucData,uiDataSize);
}

void SendQuickResponse (unsigned char ucCmd, unsigned char ucResponse) {
  ucReceivedCommand = QUICK_COMMAND;
  ucLastCmd = ucCmd;
  ucLastResponse = ucResponse;
  Serial.write(ucCmd);
  Serial.write(ucResponse);
}

void saveFileConfig() {
  EEPROM.put(0,stDeviceConfiguration);
  EEPROM.commit();
}

bool validateConfigFile() {
  bool bReturn = false;
  EEPROM.get(0,stDeviceConfiguration);
  if ((stDeviceConfiguration.ucConfigFileName[0]=='E')&&(stDeviceConfiguration.ucConfigFileName[1]=='S')\
  &&(stDeviceConfiguration.ucConfigFileName[2]=='P')&&(stDeviceConfiguration.ucConfigFileName[3]=='U')\
  &&(stDeviceConfiguration.ucConfigFileName[4]=='N')&&(stDeviceConfiguration.ucConfigFileName[5]=='A')\
  &&(stDeviceConfiguration.ucConfigFileName[6]=='P')&&(stDeviceConfiguration.ucConfigFileName[7]=='I'))
    bReturn = true;

  if ((!bReturn)||(stDeviceConfiguration.ucStructVersion<2))
  {
    if (!bReturn)
    {
      stDeviceConfiguration.ucConfigFileName[0]='E';
      stDeviceConfiguration.ucConfigFileName[1]='S';
      stDeviceConfiguration.ucConfigFileName[2]='P';
      stDeviceConfiguration.ucConfigFileName[3]='U';
      stDeviceConfiguration.ucConfigFileName[4]='N';
      stDeviceConfiguration.ucConfigFileName[5]='A';
      stDeviceConfiguration.ucConfigFileName[6]='P';
      stDeviceConfiguration.ucConfigFileName[7]='I';
      stDeviceConfiguration.ucStructVersion = 2;
      stDeviceConfiguration.ucNagle = 0;
      stDeviceConfiguration.ucAlwaysOn = 0;
      stDeviceConfiguration.uiRadioOffTimer = 120;
      stDeviceConfiguration.ucAutoClock = 0;
      stDeviceConfiguration.iGMT = -3;
    }
    else // just need to update structure
    {
      stDeviceConfiguration.ucStructVersion = 2;
      stDeviceConfiguration.ucAutoClock = 0;
      stDeviceConfiguration.iGMT = -3;
    }
    saveFileConfig();
  }

  return bReturn;
}

void ScheduleTimeoutCheck() {
    tickerRadioOff.once(stDeviceConfiguration.uiRadioOffTimer, []() { bDisableRadioPending = true; });
}

void  CancelTimeoutCheck() {
  tickerRadioOff.detach();
}

void WaitConnectionIfNeeded(bool bFastReturn = false) {
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long timeout = millis() + (bFastReturn ? 6500 : 10000);
    while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
      yield();
    }
  }
}

byte checkOpenConnections() {
  byte count = 0;
  for (int i = 0; i < 4; i++)
    if (btConnections[i] != CONN_CLOSED) count++;
  return count;
}

void setup() {
  WiFi.persistent(true);
  EEPROM.begin(32);
  Serial.begin(859372);
  Serial.setRxBufferSize(2148);
  Serial.setTimeout(1);
  Serial.print("TCP-IP UNAPI ESP32 v");
  Serial.println(chVer);
  Serial.println("(c) 2019-2026 Oduvaldo Pavan Junior - ducasp@gmail.com");
  validateConfigFile();
  longReadyTimeOut = 0;
  btReadyRetries = 3;
  btReceivedCommand = false;
#ifdef USE_WIFI_LED
  // Onboard LED for visible WiFi-up indicator. Always on; independent of debug.
  wifiLedInit();
  // WiFi event handler: drives the LED unconditionally; debug-prints only
  // when DEBUG_WIFI_CONNECT is defined. We register this even in production
  // builds so the LED accurately reflects link state across reconnects.
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:        wifiLedSet(true);  break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:  wifiLedSet(false); break;
      default: break;
    }
  });
#endif
  // Initialize file system (FFat). On first boot of a freshly-flashed image
  // the partition has no FAT structure yet — FFat.begin(false) will fail and
  // we format with FFat.begin(true). Subsequent boots mount cleanly.
  // IMPORTANT: do NOT print FFat status to ALL_SERIAL — that's the UART to
  // the FPGA and any stray text corrupts the UNAPI byte stream.
  if (!FFat.begin(false)) {
    FFat.begin(true);
  }

  WiFi.setSleep(false);
  WiFi.persistent(true);
  WiFi.begin();
  if (stDeviceConfiguration.ucAutoClock!=3)
  {
    WiFi.mode(WIFI_STA);
    // Pin minimum auth to WPA2-PSK so the station won't accept OPEN/WEP/WPA-PSK
    // candidates. This also nudges the driver away from preferring WPA3-SAE on
    // WPA2/WPA3 transition-mode APs, which is the most common cause of a
    // ~10-second 4-way handshake timeout when the password is otherwise correct.
    WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK);
    WiFi.setTxPower(WIFI_POWER_15dBm);
    bWiFiOn = true;
    if (!stDeviceConfiguration.ucAlwaysOn)
      ScheduleTimeoutCheck();
  }
  else
  {
    WiFi.mode(WIFI_OFF);
    bWiFiOn = false;
  }
  configTime(0,0, "pool.ntp.org");
  CacheCertificates();
  Serial.println("Ready");
}

void DisableRadio () {
  if (((!checkOpenConnections())&&(btState == RX_PARSER_IDLE)&&(!bSkipStateCheck)&&(!bHoldConnection))||\
      ((!checkOpenConnections())&&(bSkipStateCheck)&&(!bHoldConnection)))
  {    
    if (bWiFiOn)
    {
      bWiFiOn = false;
      RadioUpdateStatus();
      RadioUpdateStatus();
    }
  }
  else if ((!stDeviceConfiguration.ucAlwaysOn)&&(stDeviceConfiguration.ucAutoClock != 3))
    ScheduleTimeoutCheck();
}

void RadioUpdateStatus () {
  if (bWiFiOn){
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    if (!stDeviceConfiguration.ucAlwaysOn)
      ScheduleTimeoutCheck();
  }
  else
    WiFi.mode(WIFI_OFF);
}

bool CacheCertificates() {
  if (g_caPem != NULL)
    free(g_caPem);
  if (g_caBundle != NULL)
    free(g_caBundle);

  g_caPem = NULL;
  g_caBundle = NULL;
  g_caPemLen = 0;
  g_caBundleLen = 0;

  const char *pemPaths[] = {"/ca.pem", "/cacert.pem"};
  for (size_t i = 0; i < (sizeof(pemPaths) / sizeof(pemPaths[0])); i++) {
    File caFile = FFat.open(pemPaths[i], "r");
    if (caFile) {
      size_t size = caFile.size();
      if (size > 0) {
        char *buf = (char*)malloc(size + 1);
        if (buf != NULL) {
          size_t rd = caFile.readBytes(buf, size);
          buf[rd] = 0;
          caFile.close();
          g_caPem = buf;
          g_caPemLen = rd;
          return true;
        }
      }
      caFile.close();
    }
  }

  File bundle = FFat.open("/certs.bin", "r");
  if (bundle) {
    size_t size = bundle.size();
    if (size > 0) {
      uint8_t *buf = (uint8_t*)malloc(size);
      if (buf != NULL) {
        size_t rd = bundle.read(buf, size);
        bundle.close();
        if (rd == size) {
          g_caBundle = buf;
          g_caBundleLen = size;
          return true;
        }
        free(buf);
      } else {
        bundle.close();
      }
    } else {
      bundle.close();
    }
  }
  return false;
}

bool InitCertificates() {
  CacheCertificates();
  setClock(); // Required for X.509 validation

  return true;
}

// Load CA certificates from FFat (FATFS)
// Both cacert.pem and legacy certs.bin will work
bool loadCACertForClient(WiFiClientSecure *client) {
  if (g_caPem != NULL && g_caPemLen > 0) {
    client->setCACert(g_caPem);
    return true;
  }

  if (g_caBundle != NULL && g_caBundleLen > 0) {
    client->setCACertBundle(g_caBundle, g_caBundleLen);
    return true;
  }

  return false;
}

bool IsActivePortInUse (unsigned int uiPort) {
  bool bRet = true;
  byte btLoop;

  for (btLoop = 0; btLoop<4; btLoop++)
    if ( uiLocalPorts[btLoop] == uiPort )
    {
      bRet = false;
      break;
    }

  return bRet;  
}

WiFiServer *IsPassivePortInUse (unsigned int uiPort) {
  byte btLoop;
  WiFiServer * newServer;

  for (btLoop = 0; btLoop<4; btLoop++)
  {
    if ( uiListenPorts[btLoop] == uiPort )
      return ServerList[btLoop];
  }

  newServer = new WiFiServer(uiPort);
  newServer->setNoDelay(!stDeviceConfiguration.ucNagle);
  newServer->begin();
  return newServer;  
}

bool CheckActiveLocalPort (unsigned int *lpuiPort) {
  bool bRet = false;
  long lRand;

  if (*lpuiPort == 0xffff) //Random?
  {
    do
    {
      lRand = random (16384,32767);
      *lpuiPort = (unsigned int)lRand;
      bRet = IsActivePortInUse (*lpuiPort);
    }
    while (!bRet);
  }
  else
    bRet = IsActivePortInUse (*lpuiPort);

  return bRet;
}

void CheckPassiveLocalPort (unsigned int *lpuiPort) {
  long lRand;

  if (*lpuiPort == 0xffff) //Random?
  {
    lRand = random (16384,32767);
    *lpuiPort = (unsigned int)lRand;
  }
}

void CloseUdpConnection (byte btConnNumber) {
  switch (btConnNumber)
  {
    case 1:
      Udp1.stop();
    break;
    case 2:
      Udp2.stop();
    break;
    case 3:
      Udp3.stop();
    break;
    case 4:
      Udp4.stop();
    break;
  }
  btConnections[btConnNumber-1] = CONN_CLOSED;
  uiLocalPorts[btConnNumber-1] = 0;
  btIsTransient[btConnNumber-1] = 0;
}

void CloseTcpConnection (byte btConnNumber) {
  if (ClientList[btConnNumber-1]!=NULL)
  {
    ClientList[btConnNumber-1]->stop();
    if (!ServerList[btConnNumber-1])
      delete ClientList[btConnNumber-1];
    ClientList[btConnNumber-1] = NULL;
    switch (btConnNumber)
    {
      case 1:
        memset(btIPConnection1,0,4);
        uiRemotePorts[0]=0;
      break;
      case 2:
        memset(btIPConnection2,0,4);
        uiRemotePorts[1]=0;
      break;
      case 3:
        memset(btIPConnection3,0,4);
        uiRemotePorts[2]=0;
      break;
      case 4:
        memset(btIPConnection4,0,4);
        uiRemotePorts[3]=0;
      break;
    }
    uiRemotePorts[0]=0;

    if (btConnections[btConnNumber-1] == CONN_TCP_TLS_A)
      bTLSInUse = 0;

  }

  if (ServerList[btConnNumber-1])
  {
    switch (btConnNumber)
    {
      case 1:
        if (ServerList[0])
        {
          if ((ServerList[0]!=ServerList[1])&&(ServerList[0]!=ServerList[2])&&(ServerList[0]!=ServerList[3]))
          {
            //This is the last connection linked to this server
            ServerList[0]->stop();
            delete ServerList[0]; //object no longer needed
          }
          ServerList[0] = NULL;
        }
        memset(btIPConnection1,0,4);
        uiRemotePorts[0]=0;
        uiListenPorts[0]=0;
      break;
      case 2:
        if (ServerList[1])
        {
          if ((ServerList[1]!=ServerList[0])&&(ServerList[1]!=ServerList[2])&&(ServerList[1]!=ServerList[3]))
          {
            //This is the last connection linked to this server
            ServerList[1]->stop();
            delete ServerList[1];
          }
          ServerList[1] = NULL;
        }
        memset(btIPConnection2,0,4);
        uiRemotePorts[1]=0;
        uiListenPorts[1]=0;
      break;
      case 3:
        if (ServerList[2])
        {
          if ((ServerList[2]!=ServerList[0])&&(ServerList[2]!=ServerList[1])&&(ServerList[2]!=ServerList[3]))
          {
            //This is the last connection linked to this server
            ServerList[2]->stop();
            delete ServerList[2];
          }
          ServerList[2] = NULL;
        }
        memset(btIPConnection3,0,4);
        uiRemotePorts[2]=0;
        uiListenPorts[2]=0;
      break;
      case 4:
        if (ServerList[3])
        {
          if ((ServerList[3]!=ServerList[0])&&(ServerList[3]!=ServerList[2])&&(ServerList[3]!=ServerList[1]))
          {
            //This is the last connection linked to this server
            ServerList[3]->stop();
            delete ServerList[3];
          }
          ServerList[3] = NULL;
        }
        memset(btIPConnection4,0,4);
        uiRemotePorts[3]=0;
        uiListenPorts[3]=0;
      break;
    }    
  }
  btConnections[btConnNumber-1] = CONN_CLOSED;
  uiLocalPorts[btConnNumber-1] = 0;
  btIsTransient[btConnNumber-1] = 0;
}

void TcpSend(byte btCMDConnNumber,byte *btCommandData,unsigned int uiCmdDataLen) {
  unsigned int uiSent = 0;

  if ((ClientList[btCMDConnNumber-1]!=NULL)&&(ClientList[btCMDConnNumber-1]->connected()))
  {
    uiSent = uiSent + ClientList[btCMDConnNumber-1]->write(&btCommandData[uiSent],uiCmdDataLen-uiSent);
    if (uiSent == uiCmdDataLen)
      SendResponse (TCPIP_TCP_SEND, UNAPI_ERR_OK, 0, 0);
    else
      SendResponse (TCPIP_TCP_SEND, UNAPI_ERR_BUFFER, 0, 0);
  }
  else if (ClientList[btCMDConnNumber-1]!=NULL)
    SendResponse (TCPIP_TCP_SEND, UNAPI_ERR_CONN_STATE, 0, 0);
  else
    SendResponse (TCPIP_TCP_SEND, UNAPI_ERR_NO_CONN, 0, 0);
}

void TcpState(byte btCMDConnNumber)
{
  size_t availableBytes;
  byte btStatus;
  unsigned char ucResponse[16];
  memset (ucResponse,0,sizeof(ucResponse));

  if (btConnections[btCMDConnNumber-1] == CONN_TCP_TLS_A)
  {
    if (ClientList[btCMDConnNumber-1]->connected())
      ucResponse[0] = 1; //It is a flag indicating it is TLS connection
    else
    {
      //TODO: can check real error instead of returning TLS other?
      ucResponse[0] = 19;
    }
  }

  if (ClientList[btCMDConnNumber-1]!=NULL)
  {
    if (ClientList[btCMDConnNumber-1]->available()==0)
    {
      btStatus=ClientList[btCMDConnNumber-1]->connected() ? 4 : 0;
      if (btStatus==0)
        btStatus=7; //ESP8266 Wificlass go to 0 once remote end disconnects, this can lead UNAPI apps go nuts
      ucResponse[1]=btStatus; //Connection State
    }
    else
      ucResponse[1]=4; //Connection State forced to stablished when still there is data
    ucResponse[2]=(unsigned char)(ClientList[btCMDConnNumber-1]->available()&0xff); //Incoming bytes available LSB
    ucResponse[3]=(unsigned char)((ClientList[btCMDConnNumber-1]->available()>>8)&0xff); //Incoming bytes available MSB
    availableBytes = ClientList[btCMDConnNumber-1]->availableForWrite();
    if (availableBytes>2048)
      availableBytes = 2048; //We can receive only up to 2048 in a single command
    ucResponse[6]=(unsigned char)(availableBytes&0xff); //Free space in buffer LSB
    ucResponse[7]=(unsigned char)((availableBytes>>8)&0xff); //Free space in buffer MSB
    if (btCMDConnNumber==1)
      memcpy(&ucResponse[8],btIPConnection1,4);
    else if (btCMDConnNumber==2)
      memcpy(&ucResponse[8],btIPConnection2,4);
    else if (btCMDConnNumber==3)
      memcpy(&ucResponse[8],btIPConnection3,4);
    else
      memcpy(&ucResponse[8],btIPConnection4,4);
    ucResponse[12]=(unsigned char)(uiRemotePorts[btCMDConnNumber-1]&0xff);
    ucResponse[13]=(unsigned char)((uiRemotePorts[btCMDConnNumber-1]>>8)&0xff);
    ucResponse[14]=(unsigned char)(uiLocalPorts[btCMDConnNumber-1]&0xff);
    ucResponse[15]=(unsigned char)((uiLocalPorts[btCMDConnNumber-1]>>8)&0xff);
    SendResponse (TCPIP_TCP_STATE, UNAPI_ERR_OK, 16, ucResponse);
  }
  else if (btConnections[btCMDConnNumber-1] == CONN_TCP_PASSIVE)
  {
    // Here we can either say Listen (1) or Stablished (4)
    // If there is a client, it is stablished, otherwise, listening
    ucResponse[1] = ClientList[btCMDConnNumber-1] != NULL ? 4 : 1; //Connection State
    ucResponse[14]=(unsigned char)(uiLocalPorts[btCMDConnNumber-1]&0xff);
    ucResponse[15]=(unsigned char)((uiLocalPorts[btCMDConnNumber-1]>>8)&0xff);
    SendResponse (TCPIP_TCP_STATE, UNAPI_ERR_OK, 16, ucResponse);
    return;
  }
  else
  {
    SendResponse (TCPIP_TCP_STATE, UNAPI_ERR_NO_CONN, 0, 0);
    return;
  }
}

void TcpReceive (byte btCMDConnNumber, uint16_t ui16RcvSize, byte * btCommandData) {
  int iLenght = 0;
  btCommandData[0]=0;
  btCommandData[1]=0;

  if (ClientList[btCMDConnNumber-1]!=NULL)
  {
    iLenght = ClientList[btCMDConnNumber-1]->available();
    if (iLenght>0)
    {
      if (iLenght>ui16RcvSize)
        iLenght = ui16RcvSize; //get up to ui16RcvSize
      iLenght = ClientList[btCMDConnNumber-1]->read(&btCommandData[2],iLenght);
    }
    else
      iLenght = 0;

    if (iLenght)
      iLenght+=2;
    SendResponse (TCPIP_TCP_RCV, UNAPI_ERR_OK, (unsigned int) iLenght, btCommandData);
  }
  else
  {
    SendResponse (TCPIP_TCP_RCV, UNAPI_ERR_NO_CONN, 0, 0);
  }
}

void WarmBoot() {
  byte bti;
  for (bti=0;bti<4;++bti)
  {
    CloseTcpConnection(bti);
    CloseUdpConnection(bti);
  }
  if ((!bWiFiOn)&&(stDeviceConfiguration.ucAutoClock!=3))
  {
    bWiFiOn = true;
    RadioUpdateStatus();
  }
}

void received_data_parser () {
  static bool bInit = false;
  static unsigned long RXTimeOut;
  static byte btCommand;
  static byte btCommandData[MAX_CMD_DATA_LEN];
  static byte btIPRet[4];
  static unsigned int uiCmdDataLen, uiCmdDataRemaining,uiTmp;
  static byte btCmdInternalStep = 0;
  byte btTmp;
  unsigned int uiForHelper;
  unsigned int uiCMDLocalPort,uiCMDRemotePort;
  int iLenght;
  byte btCMDTransient;
  byte btCMDPassive;
  byte btCMDConnNumber;
  byte btCMDBeginRet;
  uint16_t ui16Port,ui16RcvSize;
  static int iAPCount = 0;
  unsigned int iAPItemI,iPWDCount;
  bool bOkParam = false;
  bool bPWD = false;
  unsigned char ucSSID[33];
  unsigned char ucPWD[65];
  unsigned char ucOTAServer[256];
  unsigned char ucOTAFile[256];
  uint16_t uiOTAPort;
  String stOTAServer,stOTAFile,stVersion;
  WiFiClient OTAclient;
  t_httpUpdate_return OTAret;
  uint32_t ulSerialUpdateSize = 0;
  byte bAutoIPConfig=0;
  uint32_t bin_flash_size;
  bool bScanReconnect = false;

  if (!bInit)
  {
    bInit = true;
    RXTimeOut = millis() + 250;
  }
  else
  {
    if (millis()>RXTimeOut)
    {
      //RX Timeout, reset state machine
      btState = RX_PARSER_IDLE;
    }
    RXTimeOut = millis() + 250;
  }

  switch (btState)
  {
    case RX_PARSER_IDLE:
      //Ok, so first let's check if it is a valid command
      if (Serial.readBytes(&btCommand,1) == 1)
      {
        btReceivedCommand = true;
        if ((btCommand!=CUSTOM_F_RELEASECONNECTION) && (btCommand!=CUSTOM_F_RESET) && (!bWiFiOn) && (btCommand!=CUSTOM_F_QUERY) && (stDeviceConfiguration.ucAutoClock!=3))
        {
          bWiFiOn = true;
          RadioUpdateStatus();
        }
        switch(btCommand)
        {
          case CUSTOM_F_INITCERTS:
            if(!InitCertificates())
            {
              SendQuickResponse(btCommand,UNAPI_ERR_NO_DATA);
            }
            else
            {
              SendQuickResponse(btCommand,UNAPI_ERR_OK);
            }
          break;
          case CUSTOM_F_RETRY_TX:
            if(ucReceivedCommand == QUICK_COMMAND)
            {
              SendQuickResponse (ucLastCmd, ucLastResponse);
            }
            else if(ucReceivedCommand == REGULAR_COMMAND)
            {
              SendResponse (ucLastCmd, ucLastResponse, uiLastDataSize, ucLastData);
            }
          break;
          case  CUSTOM_F_WARMBOOT:
            WarmBoot();
            Serial.println("Ready");
          break;
          case CUSTOM_F_END_RS232_UPDATE:
            if (!bSerialUpdateInProgress)
            {
              SendQuickResponse(btCommand,UNAPI_ERR_INV_PARAM);
            }
            else if (bSerialCertUpdateInProgress) 
            {
              // Finalize the cert upload: close the temp file, sniff the
              // first bytes to decide PEM vs mbedTLS bundle, rename to the
              // appropriate path, invalidate the cached-cert pointers so
              // next HTTPS connect reloads from FFat.
              g_certUploadFile.close();
              bSerialCertUpdateInProgress = false;
              bSerialUpdateInProgress = false;

              char hdr[12] = {0};
              size_t hdrRead = 0;
              File t = FFat.open("/cert_upload.tmp", "r");
              if (t)
              {
                hdrRead = t.readBytes(hdr, 11);
                t.close();
              }

              const char* targetPath = "/certs.bin";  // mbedTLS bundle (default)
              if (hdrRead >= 11 && memcmp(hdr, "-----BEGIN ", 11) == 0)
                targetPath = "/ca.pem";               // PEM text

              if (FFat.exists(targetPath)) FFat.remove(targetPath);
              if (FFat.rename("/cert_upload.tmp", targetPath))
              {
                // Drop in-memory cached certs so loadCACertForClient() reloads
                if (g_caPem)    { free(g_caPem);    g_caPem = NULL;    g_caPemLen = 0; }
                if (g_caBundle) { free(g_caBundle); g_caBundle = NULL; g_caBundleLen = 0; }
                SendQuickResponse(btCommand, UNAPI_ERR_OK);
                // No ESP.restart() — cert update is hot-applied.
              }
              else
                SendQuickResponse(btCommand, UNAPI_ERR_NO_DATA);
            }
            else
            {
              if (!Update.end())
              {
                SendQuickResponse(btCommand,UNAPI_ERR_NO_DATA);
              }
              else
              {
                SendQuickResponse(btCommand,UNAPI_ERR_OK);
                // On ESP32 - C6 if not waiting a little, the response sent is corrupted or not even sent
                Serial.flush();
                delay(100);
                bSerialUpdateInProgress = false;
                ESP.restart();
              }
            }
          break;
          //Reset?
          case CUSTOM_F_RESET:
            Serial.print("R0");
            if (!bSerialUpdateInProgress)
              ESP.restart();
            break;
          //GET Access Point Status
          case CUSTOM_F_GETAPSTS:
          {
            unsigned char uchConSts[34];
            memset(uchConSts,0,sizeof(uchConSts));
            uchConSts[0]= getConnStatus();
            strcpy((char*)&uchConSts[1],WiFi.SSID().c_str());
            SendResponse (CUSTOM_F_GETAPSTS, UNAPI_ERR_OK, (2+strlen((const char*)&uchConSts[1])), uchConSts);
          }
            break;
          //Query?
          case CUSTOM_F_QUERY:
            Serial.print("OK");
            break;
          case CUSTOM_F_GET_DATETIME:
            {
              unsigned char uchDateTime[7];
              time_t currTime;
              struct tm *currTimeInfo;

              if (!bSNTPOK)
              {
                WaitConnectionIfNeeded(true);
                if (WiFi.status() != WL_CONNECTED)
                  SendQuickResponse (CUSTOM_F_GET_DATETIME, UNAPI_ERR_NO_NETWORK);
              }

              if ((bSNTPOK)||(WiFi.status() == WL_CONNECTED))
              {
                if (!bSNTPOK)
                  setClock(true);
                if (bSNTPOK)
                {
                  time(&currTime);
                  currTime = currTime + (stDeviceConfiguration.iGMT * 3600);
                  currTimeInfo = localtime(&currTime);
                  uchDateTime[0] = currTimeInfo->tm_sec & 0xff;
                  uchDateTime[1] = currTimeInfo->tm_min & 0xff;
                  uchDateTime[2] = currTimeInfo->tm_hour & 0xff;
                  uchDateTime[3] = currTimeInfo->tm_mday & 0xff;
                  uchDateTime[4] = (currTimeInfo->tm_mon+1) & 0xff;
                  uchDateTime[5] = (currTimeInfo->tm_year + 1900) & 0xff;
                  uchDateTime[6] = ((currTimeInfo->tm_year + 1900)>>8) & 0xff;
                  SendResponse (CUSTOM_F_GET_DATETIME, UNAPI_ERR_OK, 7, uchDateTime);
                }
                else
                  SendQuickResponse (CUSTOM_F_GET_DATETIME, UNAPI_ERR_NO_NETWORK);
              }
            }
            break;
          case CUSTOM_F_HOLDCONNECTION:
            {
              bHoldConnection = true;
              if (!bWiFiOn)
              {
                bWiFiOn= true;
                RadioUpdateStatus();
              }
              SendQuickResponse(btCommand,UNAPI_ERR_OK);
            }
            break;
          case CUSTOM_F_RELEASECONNECTION:
            {
              bHoldConnection = false;
              if (stDeviceConfiguration.ucAutoClock==3)
              {
                bWiFiOn = false;
                RadioUpdateStatus();
              }
              SendQuickResponse(btCommand,UNAPI_ERR_OK);
            }
            break;
          case CUSTOM_F_QUERY_AUTOCLOCK:
            {
              unsigned char uchAutoClock[2];
              uchAutoClock[0] = stDeviceConfiguration.ucAutoClock;
              if (stDeviceConfiguration.iGMT>=0)
                uchAutoClock[1] = stDeviceConfiguration.iGMT&0xff;
              else
                uchAutoClock[1] = ((stDeviceConfiguration.iGMT*-1)&0xff)|0x80;
              SendResponse (CUSTOM_F_QUERY_AUTOCLOCK, UNAPI_ERR_OK, 2, uchAutoClock);
            }
            break;
          case CUSTOM_F_QUERY_SETTINGS:
            {
              unsigned char uchSettingsString[30];
              unsigned char uchTmp[10];

              sprintf((char*)uchTmp,"%u",stDeviceConfiguration.uiRadioOffTimer);
              
              if (stDeviceConfiguration.ucNagle)
                sprintf((char*)uchSettingsString,"ON:%s",(char*)uchTmp);
              else
                sprintf((char*)uchSettingsString,"OFF:%s",(char*)uchTmp);

              SendResponse (CUSTOM_F_QUERY_SETTINGS, UNAPI_ERR_OK, strlen((char*)uchSettingsString), uchSettingsString);
            }
            break;
          case CUSTOM_F_GET_VER:
            Serial.write(CUSTOM_F_GET_VER);
            Serial.write(chVer[0]-'0');
            Serial.write(chVer[2]-'0');
            break;
          case CUSTOM_F_SCAN:
            // ESP32 when with bad credentials will not scan unless you call disconnectds
            WiFi.disconnect();
            WiFi.scanNetworks(true,false);
            SendQuickResponse('S',0);
            break;
          case CUSTOM_F_SCAN_R:
            iAPCount = WiFi.scanComplete();
            Serial.write('s');
            if (iAPCount>=0)
            {
              bScanReconnect = true;
              Serial.write(0);
              Serial.write((unsigned char)iAPCount); //count of APs
              if (iAPCount>0)
              {
                for (int iScanRLoop = 0; iScanRLoop<iAPCount; iScanRLoop++)
                {
                  Serial.printf("%s",WiFi.SSID(iScanRLoop).c_str());
                  Serial.write(0); //Terminate SSID
                  Serial.write(WiFi.encryptionType(iScanRLoop) == WIFI_AUTH_OPEN ? 'O' : 'E');
                }
              }
            }
            else if (iAPCount == WIFI_SCAN_RUNNING)
            {
              Serial.write(UNAPI_ERR_NO_DATA); //no data yet
            }
            else {
              bScanReconnect = true;
              Serial.write(UNAPI_ERR_NO_NETWORK); //no AP found found
            }
            if (bScanReconnect) {
              WiFi.mode(WIFI_STA);
              WiFi.begin();
            }
            break;
          case CUSTOM_F_TURN_WIFI_OFF:
            bSkipStateCheck = true;
            DisableRadio();
            bSkipStateCheck = false;
            SendQuickResponse(btCommand,UNAPI_ERR_OK);
          break;
          case CUSTOM_F_TURN_RS232_OFF:
            // Note: it won't be possible to restore communications, only through power cycle or physical reset
            SendQuickResponse(btCommand,UNAPI_ERR_OK);
            Serial.end();
            break;
          case CUSTOM_F_CLEAR_AP:
            WiFi.disconnect();
            yield();
            WiFi.begin("XXXXXXXXXX","YYYYYYYYZZZZZZ");
            SendQuickResponse(btCommand,UNAPI_ERR_OK);
            break;
          case CUSTOM_F_NO_DELAY:
            stDeviceConfiguration.ucNagle = 0;
            saveFileConfig();
            SendQuickResponse(btCommand,UNAPI_ERR_OK);
          break;
          case CUSTOM_F_DELAY:
            stDeviceConfiguration.ucNagle = 1;
            saveFileConfig();
            SendQuickResponse(btCommand,UNAPI_ERR_OK);
          break;

          case TCPIP_HTTP_OPEN:
          case TCPIP_HTTP_RECEIVE:
          case TCPIP_HTTP_CLOSE:
          case TCPIP_GET_CAPAB:
          case TCPIP_GET_IPINFO:
          case TCPIP_NET_STATE:
          case TCPIP_DNS_Q:
          case TCPIP_DNS_Q_NEW:
          case TCPIP_UDP_OPEN:
          case TCPIP_UDP_CLOSE:
          case TCPIP_UDP_STATE:
          case TCPIP_UDP_SEND:
          case TCPIP_UDP_RCV:
          case TCPIP_TCP_OPEN:
          case TCPIP_TCP_CLOSE:
          case TCPIP_TCP_ABORT:
          case TCPIP_TCP_STATE:
          case TCPIP_TCP_SEND:
          case TCPIP_TCP_RCV:
          case CUSTOM_F_CONNECT_AP:
          case CUSTOM_F_UPDATE_FW:
          case CUSTOM_F_UPDATE_CERTS:
          case TCPIP_CONFIG_AUTOIP:
          case TCPIP_CONFIG_IP:
          case CUSTOM_F_START_RS232_UPDATE:
          case CUSTOM_F_START_RS232_CERT_UPDATE:
          case CUSTOM_F_BLOCK_RS232_UPDATE:
          case CUSTOM_F_WIFI_ON_TIMER_SET:
          case CUSTOM_F_SET_AUTOCLOCK:
            btState = RX_PARSER_WAIT_DATA_SIZE;
            btCmdInternalStep = 0;
            break;
          default:
            btState = RX_PARSER_IDLE;
            break;
        }
        
      }
    break;

    case RX_PARSER_WAIT_DATA_SIZE:
      if (Serial.readBytes(&btTmp,1) == 1)
      {
        if (btCmdInternalStep == 0)
        {
          uiCmdDataLen = btTmp*256;
          btCmdInternalStep++;
        }
        else
        {
          uiCmdDataLen += btTmp;
          uiCmdDataRemaining = uiCmdDataLen;
          btCmdInternalStep = 0;
          if (uiCmdDataLen)
            btState = RX_PARSER_GET_DATA;
          else
          {
            btState = RX_PARSER_PROCCESS_CMD;
            goto proccesscmd;
          }
        }
      }
    break;

    case RX_PARSER_GET_DATA:
      uiTmp = Serial.available();
      if (uiTmp > uiCmdDataRemaining)
        uiTmp = uiCmdDataRemaining;
      uiTmp = Serial.readBytes(&btCommandData[uiCmdDataLen - uiCmdDataRemaining],uiTmp);
      uiCmdDataRemaining -= uiTmp;
      if (uiCmdDataRemaining == 0)
        btState = RX_PARSER_PROCCESS_CMD;
      else
        break;
        
    case RX_PARSER_PROCCESS_CMD:
proccesscmd:
      switch(btCommand)
      {
        case CUSTOM_F_START_RS232_UPDATE:
        case CUSTOM_F_START_RS232_CERT_UPDATE:
          if ((uiCmdDataLen !=12)||(bSerialUpdateInProgress))
            SendQuickResponse(btCommand,UNAPI_ERR_INV_PARAM);
          else
          {
            ulSerialUpdateSize = btCommandData[0] + (btCommandData[1]*0x100) + (btCommandData[2]*0x10000) + (btCommandData[3]*0x1000000) + (btCommandData[4]*0x100000000) + (btCommandData[5]*0x10000000000)+ (btCommandData[6]*0x1000000000000)+ (btCommandData[7]*0x100000000000000);
            if (btCommand == CUSTOM_F_START_RS232_CERT_UPDATE)
            {
              // Stream the cert bundle into a FFat temp file. On END we'll
              // sniff the first bytes to decide whether it's PEM (rename to
              // /ca.pem) or an mbedTLS bundle (rename to /certs.bin).
              if (FFat.exists("/cert_upload.tmp")) FFat.remove("/cert_upload.tmp");
              g_certUploadFile = FFat.open("/cert_upload.tmp", "w");
              if (!g_certUploadFile)
                SendQuickResponse(btCommand, UNAPI_ERR_NO_DATA);
              else {
                bSerialUpdateInProgress = true;
                bSerialCertUpdateInProgress = true;
                SendQuickResponse(btCommand, UNAPI_ERR_OK);
              }
            }
            else
            {
              if (ulSerialUpdateSize > (ESP.getFreeSketchSpace() - 0x1000))
                SendQuickResponse(btCommand,UNAPI_ERR_INV_PARAM);
              else
              {
                if(!Update.begin(ulSerialUpdateSize, U_FLASH, -1, HIGH))
                  SendQuickResponse(btCommand,UNAPI_ERR_INV_PARAM);
                else
                {
                  SendQuickResponse(btCommand,UNAPI_ERR_OK);
                  bSerialUpdateInProgress = true;
                }
              }
            }
          }
        break;
        case CUSTOM_F_BLOCK_RS232_UPDATE:
          if ((uiCmdDataLen == 0)||(!bSerialUpdateInProgress))
            SendQuickResponse(btCommand,UNAPI_ERR_INV_PARAM);
          else if (bSerialCertUpdateInProgress) 
          {
            // Cert path: append to the FFat temp file
            size_t w = g_certUploadFile.write(btCommandData, uiCmdDataLen);
            if (w != uiCmdDataLen)
              SendQuickResponse(btCommand, UNAPI_ERR_NO_DATA);
            else
              SendQuickResponse(btCommand, UNAPI_ERR_OK);
          }
          else
          {
            if (Update.write(btCommandData,uiCmdDataLen)!=uiCmdDataLen)
              SendQuickResponse(btCommand,UNAPI_ERR_NO_DATA);
            else
              SendQuickResponse(btCommand,UNAPI_ERR_OK);
          }
        break;
        case CUSTOM_F_SET_AUTOCLOCK:
          if ( (uiCmdDataLen != 2) || (btCommandData[0]>3) ||\
               ((btCommandData[1]>12)&&((btCommandData[1]<129)||(btCommandData[1]>140))) )
            SendQuickResponse(btCommand,UNAPI_ERR_INV_PARAM);
          else
          {
            // Enabling a disabled adapter?
            if ((stDeviceConfiguration.ucAutoClock == 3)&&(btCommandData[0]!=3))
            {
              bWiFiOn = true;
              RadioUpdateStatus();
            }
            stDeviceConfiguration.ucAutoClock = btCommandData[0];
            if (btCommandData[1] > 12) //negative?
              stDeviceConfiguration.iGMT = (btCommandData[1]&0x0f)*-1;
            else
              stDeviceConfiguration.iGMT = btCommandData[1];
            saveFileConfig();
            if (stDeviceConfiguration.ucAutoClock == 3)
            {
              bSkipStateCheck = true;
              DisableRadio();
              bSkipStateCheck = false;
            }
            SendQuickResponse(btCommand,UNAPI_ERR_OK);
          }
        break;
        case CUSTOM_F_WIFI_ON_TIMER_SET:
          if (uiCmdDataLen != 2)
            SendQuickResponse(btCommand,UNAPI_ERR_INV_PARAM);
          else
          {
            stDeviceConfiguration.uiRadioOffTimer = (unsigned int)(btCommandData[0]*256) + btCommandData[1];
            if (stDeviceConfiguration.uiRadioOffTimer > 600) //10 minutes top
              stDeviceConfiguration.uiRadioOffTimer = 600;
            if ((stDeviceConfiguration.uiRadioOffTimer>0)&&(stDeviceConfiguration.uiRadioOffTimer<30))
              stDeviceConfiguration.uiRadioOffTimer = 30; //30 seconds at least
            if (stDeviceConfiguration.uiRadioOffTimer)
              stDeviceConfiguration.ucAlwaysOn = 0;
            else
            {
              stDeviceConfiguration.ucAlwaysOn = 1;
              CancelTimeoutCheck();
            }
            saveFileConfig();
            SendQuickResponse(btCommand,UNAPI_ERR_OK);
          }
        break;
        case TCPIP_CONFIG_AUTOIP:
          if ((uiCmdDataLen != 2) || (btCommandData[0]>1) || (btCommandData[1]>3))          
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else
          {
            if (btCommandData[0]) //set?
            {
              bIPAutomatic = (btCommandData[1] & 1) != 0;
              bDNSAutomatic = (btCommandData[1] & 2) != 0;
            }
            if (bIPAutomatic)
              bAutoIPConfig|=B00000001;
            else
              bAutoIPConfig&=B11111110;

            if (bDNSAutomatic)
              bAutoIPConfig|=B00000010;
            else
              bAutoIPConfig&=B11111101;
            SendResponse(btCommand,UNAPI_ERR_OK,1,&bAutoIPConfig);
          }
        break;
        case TCPIP_CONFIG_IP:
          externalIP[0] = btCommandData[1];
          externalIP[1] = btCommandData[2];
          externalIP[2] = btCommandData[3];
          externalIP[3] = btCommandData[4];
          if ((uiCmdDataLen != 5) || (btCommandData[0]==0) || (btCommandData[0]==2)|| (btCommandData[0]>6))
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else
          {
            if ((!bIPAutomatic) && ( (btCommandData[0]==1)||(btCommandData[0]==3)||(btCommandData[0]==4) ))
            {
              //Set IP/MASK/GATEWAY
              switch (btCommandData[0])
              {
                case 1:
                  WiFi.config(externalIP,WiFi.gatewayIP(),WiFi.subnetMask(),WiFi.dnsIP(0),WiFi.dnsIP(1));
                break;
                case 3:
                  WiFi.config(WiFi.localIP(),WiFi.gatewayIP(),externalIP,WiFi.dnsIP(0),WiFi.dnsIP(1));
                break;
                case 4:
                  WiFi.config(WiFi.localIP(),externalIP,WiFi.subnetMask(),WiFi.dnsIP(0),WiFi.dnsIP(1));
                break;
              }
              SendResponse(btCommand,UNAPI_ERR_OK,0,0);
            }
            else if ((!bDNSAutomatic) && ( (btCommandData[0]==5)||(btCommandData[0]==6) ))
            {
              //Set Primary DNS or secondary DNS
              switch (btCommandData[0])
              {
                case 5:
                  WiFi.config(WiFi.localIP(),WiFi.gatewayIP(),WiFi.subnetMask(),externalIP,WiFi.dnsIP(1));
                break;
                case 6:
                  WiFi.config(WiFi.localIP(),WiFi.gatewayIP(),WiFi.subnetMask(),WiFi.dnsIP(0),externalIP);
                break;
              }
              SendResponse(btCommand,UNAPI_ERR_OK,0,0);
            }
            else //sorry
              SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          }
        break;
        case CUSTOM_F_UPDATE_FW:
        case CUSTOM_F_UPDATE_CERTS:
          bOkParam = false;
          uiOTAPort = btCommandData[0]+(btCommandData[1]*256);
          if ((uiCmdDataLen <4)||(bSerialUpdateInProgress))
            SendQuickResponse(btCommand,UNAPI_ERR_INV_PARAM);
          else
          {
            for (iAPItemI=2; (iAPItemI<257)&&(iAPItemI<uiCmdDataLen); iAPItemI++)
            {
              ucOTAServer[iAPItemI-2]=btCommandData[iAPItemI];
              if (btCommandData[iAPItemI]==0)
              {
                bOkParam = true;
                iAPItemI++;
                break;
              }
            }
            if (bOkParam)
            {
              if (iAPItemI<uiCmdDataLen) //still more data?
              {
                //Yes
                iPWDCount=0;
                do
                {
                    ucOTAFile[iPWDCount]=btCommandData[iAPItemI];
                    iAPItemI++;
                    iPWDCount++;
                }
                while ((iAPItemI<uiCmdDataLen)&&(iPWDCount<255));
                ucOTAFile[iPWDCount]=0;
              }
              else
                bOkParam = false;

              if (bOkParam)
              {
                yield();
                stOTAServer = (const char*)ucOTAServer;
                stOTAFile = (const char*)ucOTAFile;
                if (btCommand == CUSTOM_F_UPDATE_FW) {
                  httpUpdate.rebootOnUpdate(false);                
                  OTAret = httpUpdate.update(OTAclient, stOTAServer, uiOTAPort, stOTAFile, chVer);
                  if (OTAret==HTTP_UPDATE_OK)
                    SendQuickResponse(btCommand,0);
                  else if (OTAret==HTTP_UPDATE_NO_UPDATES)
                    SendQuickResponse(btCommand,UNAPI_ERR_INV_OPER);
                  else
                    SendQuickResponse(btCommand,UNAPI_ERR_NO_DATA);
                } else {
                  // lets simply download the cert file
                  http.begin(stOTAServer+stOTAFile);
                  if (http.GET() == HTTP_CODE_OK) {
                    g_certUploadFile = FFat.open("/cert_upload.tmp", FILE_WRITE);
                    if (!g_certUploadFile) {
                        SendQuickResponse(btCommand, UNAPI_ERR_NO_DATA);
                    }
                    else {
                      // Stream data from web to file
                      stream = http.getStreamPtr();
                      while (http.connected() && (http.getSize() > 0 || http.getSize() == -1)) {
                          size_t size = stream->available();
                          if (size) {
                              int c = stream->readBytes(buffer, ((size > sizeof(buffer)) ? sizeof(buffer) : size));
                              g_certUploadFile.write(buffer, c);
                          }
                      }
                      g_certUploadFile.close();
                      // File received, now lets do the magic
                      char header[12] = {0};
                      size_t headerRead = 0;
                      File tt = FFat.open("/cert_upload.tmp", "r");
                      if (tt)
                      {
                        headerRead = tt.readBytes(header, 11);
                        tt.close();
                        const char* ttargetPath = "/certs.bin";  // mbedTLS bundle (default)
                        if (headerRead >= 11 && memcmp(header, "-----BEGIN ", 11) == 0)
                          ttargetPath = "/ca.pem";               // PEM text

                        if (FFat.exists(ttargetPath)) FFat.remove(ttargetPath);
                        if (FFat.rename("/cert_upload.tmp", ttargetPath))
                        {
                          // Drop in-memory cached certs so loadCACertForClient() reloads
                          if (g_caPem)    { free(g_caPem);    g_caPem = NULL;    g_caPemLen = 0; }
                          if (g_caBundle) { free(g_caBundle); g_caBundle = NULL; g_caBundleLen = 0; }
                          SendQuickResponse(btCommand, UNAPI_ERR_OK);
                          // No ESP.restart() — cert update is hot-applied.
                        }
                        else
                          SendQuickResponse(btCommand, UNAPI_ERR_NO_DATA);
                      }
                      else
                        SendQuickResponse(btCommand, UNAPI_ERR_NO_DATA);
                    }
                  }
                  else
                    SendQuickResponse(btCommand, UNAPI_ERR_NO_DATA);
                }
              }
            }
            if(!bOkParam)
              SendQuickResponse(btCommand,UNAPI_ERR_INV_PARAM);
          }
          break;
        case CUSTOM_F_CONNECT_AP:
        {
          bool bSentRsp = false;
          bOkParam = false;
          bPWD = false;

          if (uiCmdDataLen <3)
            SendQuickResponse(btCommand,UNAPI_ERR_INV_PARAM);
          else
          {
            for (iAPItemI=0; (iAPItemI<33)&&(iAPItemI<uiCmdDataLen); iAPItemI++)
            {
              ucSSID[iAPItemI]=btCommandData[iAPItemI];
              if (btCommandData[iAPItemI]==0)
              {
                bOkParam = true;
                iAPItemI++;
                break;
              }
            }

            if (bOkParam)
            {
              if (iAPItemI<uiCmdDataLen) //still more data?
              {
                //Yes
                bPWD = true;
                iPWDCount=0;
                do
                {
                    ucPWD[iPWDCount]=btCommandData[iAPItemI];
                    iAPItemI++;
                    iPWDCount++;
                }
                while ((iAPItemI<uiCmdDataLen)&&(iPWDCount<64));
                ucPWD[iPWDCount]=0;
              }
              WiFi.disconnect();
              delay(50);
              yield();
              if (bPWD)
                WiFi.begin((const char*)ucSSID,(const char*)ucPWD);
              else
                WiFi.begin((const char*)ucSSID);

              unsigned long ulTimeOut = millis() + 20000;
              do
              {
                wl_status_t APsts;
                APsts = WiFi.status();
                if (APsts == WL_CONNECTED)
                {
                  SendQuickResponse(btCommand,0);
                  bSentRsp = true;
                  break;
                }
                else if (APsts == WL_CONNECT_FAILED)
                {
                  SendQuickResponse(btCommand,UNAPI_ERR_NO_NETWORK);
                  bSentRsp = true;
                  break;
                }
                yield();
                delay(100);  // let the WiFi task run; yield() alone is too tight on ESP32
              }
              while (ulTimeOut>millis());
              if (!bSentRsp)
                SendQuickResponse(btCommand,UNAPI_ERR_NO_NETWORK);
            }
            else
              SendQuickResponse(btCommand,UNAPI_ERR_INV_PARAM);
          }
        }
        break;
        case TCPIP_GET_CAPAB:
          if ((uiCmdDataLen != 1) || (btCommandData[0]==0) || (btCommandData[0]>4))
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else
          {
            Serial.write(btCommand);
            Serial.write(UNAPI_ERR_OK);
            Serial.write(0);
            switch (btCommandData[0])
            {
              case TCPIP_GET_CAPAB_FLAGS:
                Serial.write(5);
                // Capability flags ENABLED:
                // 2 - Resolve host names by querying a DNS server
                // 3 - Open TCP connections in active mode
                // 5 - Open TCP connections in passive mode w/o specified remote socket
                //10 - Open UDP connections  
                //14 - Automatically obtain the IP addresses, by using DHCP or an equivalent protocol

                // Capability flags DISABLED:
                // 0 - Send ICMP echo messages (PINGs) and retrieve the answers
                // 1 - Resolve host names by querying local host file or database
                // 4 - Open TCP connections in passive mode with specified remote socket 
                // 6 - Send and receive TCP urgent data
                // 7 - Explicitly set the PUSH bit when sending TCP data
                // 8 - Send data to a TCP connection before the ESTABLISHED state is reached
                // 9 - Discard the output buffers of a TCP connection
                //11 - Open RAW IP connections
                //12 - Explicitly set the TTL and TOS for outgoing datagrams
                //13 - Explicitly set the automatic reply to PINGs on or off 
                //15 - Get the TTL and ToS for outgoing datagrams
                Serial.write(B00101100); //flags LSB
                Serial.write(B01000100); //flags MSB
                // Features flags ENABLED:
                // 1 - Physical link is wireless
                // 2 - Connection pool is shared by TCP, UDP and raw IP
                // 4 - The TCP/IP handling code is assisted by external hardware
                // 7 - IP datagram fragmentation is supported
                Serial.write(B10010110); //flags LSB
                // 10 TCPIP_DNS_Q is a blocking operation
                // 11 TCPIP_TCP_OPEN is a blocking operation
                // 12 The server certificate can be verified when opening a TCP connection with TLS in TCPIP_TCP_OPEN
                Serial.write(B00011100); //flags MSB
                // Link level - 4 - WiFi
                Serial.write(4);
              break;
              case TCPIP_GET_CAPAB_CONN:
                Serial.write(6);
                Serial.write(4); //Max 4 simultaneous TCP conns
                Serial.write(4); //Max 4 simultaneous UDP conns
                Serial.write(4 - checkOpenConnections()); //Free TCP conns
                Serial.write(4 - checkOpenConnections()); //Free UDP conns
                Serial.write(0); //Raw TCP not supported
                Serial.write(0); //Raw TCP not supported
              break;
              case TCPIP_GET_CAPAB_DGRAM:
                Serial.write(4);
                btTmp = 1500 & 0xff;
                Serial.write(btTmp); // LSB max incoming dg size
                btTmp = (1500 >> 8) & 0xff;
                Serial.write(btTmp); // MSB max incoming dg size
                btTmp = 2048 & 0xff;
                Serial.write(btTmp); // LSB max outgoing dg size
                btTmp = (2048 >> 8) & 0xff;
                Serial.write(btTmp); // MSB max outgoing dg size
              break;
              case TCPIP_GET_SECONDARY_CAPAB_FLAGS:
              Serial.write(5);
              //Secondary Capabilities enabled:
              //Bit 0: Automatically obtain the local IP address, subnet mask and default gateway, by using DHCP or an equivalent protocol                           
              //Bit 1: Automatically obtain the IP addresses of the DNS servers, by using DHCP or an equivalent protocol
              //Bit 2: Manually set the local IP address
              //Bit 4: Manually set the subnet mask IP address
              //Bit 5: Manually set the default gateway IP address
              //Bit 6: Manually set the primary DNS server IP address
              //Bit 7: Manually set the secondary DNS server IP address
              //Bit 8: Use TLS in TCP active connections
              //Secondary Capabilities disabled:
              //Bit 3: Manually set the peer IP address
              //Bit 9: Use TLS in TCP passive connections
              //Bits 10-15: Unused
              Serial.write(B11110111); //flags LSB
              Serial.write(B00000001); //flags MSB
              // Secondary features flags currently not defined, all 0
              Serial.write(0);
              Serial.write(0);
              // Unused, left to make driver easier / simpler
              Serial.write(0);
              break;
            }
          }
          break;

        case TCPIP_GET_IPINFO:
          if ((uiCmdDataLen != 1) || (btCommandData[0]==0) || (btCommandData[0]>6))
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else
          {
            WaitConnectionIfNeeded(false);
            switch (btCommandData[0])
            {
              case TCPIP_GET_IPINFO_LOCALIP:
                externalIP = WiFi.localIP();
              break;
              case TCPIP_GET_IPINFO_PEERIP:
                externalIP = IPAddress(0,0,0,0);
              break;
              case TCPIP_GET_IPINFO_SUBNETMASK:
                externalIP = WiFi.subnetMask();
              break;
              case TCPIP_GET_IPINFO_GATEWAY:
                btIPRet[0] = externalIP = WiFi.gatewayIP();
              break;
              case TCPIP_GET_IPINFO_PRIMDNS:
                externalIP = WiFi.dnsIP(0);
              break;
              case TCPIP_GET_IPINFO_SECDNS:
                externalIP = WiFi.dnsIP(1);
              break;
            }
            uint8_t ipBytes[4]; ipToBytes(externalIP, ipBytes);
            SendResponse(btCommand, UNAPI_ERR_OK, 4, ipBytes);
          }
          break;

        case TCPIP_NET_STATE:
          if (uiCmdDataLen != 0)
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else
          {
            unsigned char ucNetState;
            switch (WiFi.status())
            {
              case WL_CONNECTED:
                ucNetState=2; //Connection Open
              break;
              case WL_IDLE_STATUS:
                ucNetState=1; //possibly opening
              break;
              case WL_DISCONNECTED:
              case WL_NO_SSID_AVAIL:
              case WL_CONNECT_FAILED:
                ucNetState=0; //closed
              break;
              default:
                ucNetState=0; //Unknown as we can't translate, so just say closed
              break;
            }
            SendResponse(btCommand,UNAPI_ERR_OK,1,&ucNetState);
          }
          break;

        case TCPIP_DNS_Q_NEW:
          {
            unsigned char ucIsIp = true;
            unsigned char ucDirectIp[4];
            unsigned char *ucDQNstrstr;
            unsigned int uiDQNi;
            unsigned char ucDQNflags = btCommandData[0];
            unsigned char ucPoints = 0;
            if ((uiCmdDataLen < 2)|| (ucDQNflags&0xf8))
              SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
            else
            {
              btCommandData[uiCmdDataLen]=0; //zero terminate the DNS strings           
              // Check if it is an IP or something to resolve
              if (uiCmdDataLen<17) // 123.567.901.345 -> if it is more than 15 bytes long, remember CmdDataLen includes the first byte that is the flag
              {
                for (uiDQNi = 1; uiDQNi < uiCmdDataLen; ++uiDQNi)
                {
                  if ((btCommandData[uiDQNi]<'0')||(btCommandData[uiDQNi]>'9')) // not a digit
                  {
                    if (btCommandData[uiDQNi]=='.')
                      ++ucPoints;
                    else
                    {
                      ucIsIp = false;
                      break;
                    }
                    if (ucPoints>3)
                    {
                      ucIsIp = false;
                      break;
                    }
                  }
                }
                if (ucPoints == 3) //ok, if it is a valid IP address, it must have three points, otherwise it is not an IP address
                {
                  ucDQNstrstr = &btCommandData[1];
                  if (atoi((const char*)ucDQNstrstr)<0x100)
                  {
                    ucDirectIp[0] = (unsigned char)atoi((const char*)ucDQNstrstr);
                    ucDQNstrstr = (unsigned char*) strstr ((const char*)ucDQNstrstr,".");
                    if ((ucDQNstrstr) && (atoi((const char*)&ucDQNstrstr[1])<0x100))
                    {
                      ucDirectIp[1] = (unsigned char)atoi((const char*)&ucDQNstrstr[1]);
                      ++ucDQNstrstr;
                      ucDQNstrstr = (unsigned char*) strstr ((const char*)ucDQNstrstr,".");
                      if ((ucDQNstrstr) && (atoi((const char*)&ucDQNstrstr[1])<0x100))
                      {
                        ucDirectIp[2] = atoi((const char*)&ucDQNstrstr[1]);
                        ++ucDQNstrstr;
                        ucDQNstrstr = (unsigned char*) strstr ((const char*)ucDQNstrstr,".");
                        if ((ucDQNstrstr) && (atoi((const char*)&ucDQNstrstr[1])<0x100))
                        {
                          ucDirectIp[3] = atoi((const char*)&ucDQNstrstr[1]);
                          ucIsIp = true;
                        }
                        else
                          ucIsIp = false;
                      }
                      else
                        ucIsIp = false;
                    }
                    else
                      ucIsIp = false;
                  }
                  else
                    ucIsIp = false;
                }
                else
                  ucIsIp = false;
              }
              else
                ucIsIp = false;
  
              if ((ucDQNflags&2)&&(!ucIsIp))
                SendResponse(btCommand,UNAPI_ERR_INV_IP,0,0);
              else
              {
                WaitConnectionIfNeeded();
                if (ucIsIp)
                  SendResponse(btCommand,UNAPI_ERR_OK,4,&ucDirectIp[0]);
                else
                {
                  if(WiFi.hostByName((const char*)&btCommandData[1],DNSQueryIP))
                  {
                    if (DNSQueryIP == IPAddress(0,0,0,0) || DNSQueryIP == IPAddress(255,255,255,255))
                      SendResponse(btCommand,UNAPI_ERR_DNS,0,0);
                    else
                    {
                        uint8_t ipBytes[4]; ipToBytes(DNSQueryIP, ipBytes);
                        SendResponse(btCommand, UNAPI_ERR_OK, 4, ipBytes);
                    }
                  }
                  else
                    SendResponse(btCommand,UNAPI_ERR_DNS,0,0);
                }
              }
            }
          }
          break;

        case TCPIP_DNS_Q:
          if (uiCmdDataLen < 1)
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else
          {
            btCommandData[uiCmdDataLen]=0; //zero terminate the DNS strings
            WaitConnectionIfNeeded();
            if(WiFi.hostByName((const char*)btCommandData,DNSQueryIP))
            {
              if (DNSQueryIP == IPAddress(0,0,0,0) || DNSQueryIP == IPAddress(255,255,255,255))
                SendResponse(btCommand,UNAPI_ERR_DNS,0,0);
              else
              {
                uint8_t ipBytes[4]; ipToBytes(DNSQueryIP, ipBytes);
                SendResponse(btCommand, UNAPI_ERR_OK, 4, ipBytes);
              }
            }
            else
              SendResponse(btCommand,UNAPI_ERR_DNS,0,0);
          }
        break;

        case TCPIP_TCP_OPEN:
          externalIP[0] = btCommandData[0];
          externalIP[1] = btCommandData[1];
          externalIP[2] = btCommandData[2];
          externalIP[3] = btCommandData[3];
          uiCMDRemotePort = btCommandData[4] + btCommandData[5]*256;
          uiCMDLocalPort = btCommandData[6] + btCommandData[7]*256;
          btCMDPassive = btCommandData[10];
          btCMDBeginRet=0;

          if (btCMDPassive & 2)
            btCMDTransient = 0; //set, so resident
          else
            btCMDTransient = 1; //clear, so transient

          WaitConnectionIfNeeded(false);

          if ((uiCmdDataLen < 11) || (uiCMDLocalPort==0) || \
              (btCMDPassive & 0xf0) || ( (btCMDPassive & 4) && (uiCmdDataLen <12) ) || ( (btCMDPassive & 1) && (btCMDPassive & 4) ) || ( ((btCMDPassive & 1) == 0) && ((btCMDPassive & 4) ==0) && (btCMDPassive&8) ) || 
              ( ((btCMDPassive & 1) == 0) && ( (externalIP[0] | externalIP[1] | externalIP[2] | externalIP[3]) == 0) ) || //Active connection remote IP MUST be other than 0.0.0.0
              ( ((btCMDPassive & 1) == 1) && ( (externalIP[0] | externalIP[1] | externalIP[2] | externalIP[3]) != 0) ) )//Passive connection remote IP MUST be 0.0.0.0
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else if (WiFi.status() != WL_CONNECTED) {
            SendResponse(btCommand,UNAPI_ERR_NO_NETWORK,0,0);
          }
          else
          {
            if (btCMDPassive & 1) 
            {
              //set, so passive connection
              if (checkOpenConnections()<4) 
              {
                for (uiForHelper=0;uiForHelper<4;uiForHelper++)
                  if (btConnections[uiForHelper] == CONN_CLOSED)
                    break;
                btCMDConnNumber = uiForHelper+1;
                CheckPassiveLocalPort(&uiCMDLocalPort);
                ServerList[uiForHelper] = IsPassivePortInUse(uiCMDLocalPort);
                uiLocalPorts[uiForHelper] = uiCMDLocalPort;
                uiListenPorts[uiForHelper] = uiCMDLocalPort;
                btConnections[uiForHelper] = CONN_TCP_PASSIVE;
                btIsTransient[uiForHelper] = btCMDTransient;
                SendResponse(btCommand,UNAPI_ERR_OK,1,&btCMDConnNumber);
              }
              else
                SendResponse(btCommand,UNAPI_ERR_NO_FREE_CONN,0,0);
            }
            else
            {
              if (CheckActiveLocalPort(&uiCMDLocalPort))
              {
                if (btCMDPassive&4) //TLS?
                {
                  if ((bTLSInUse)||(checkOpenConnections()>=4))
                    //TODO: can we have more than 1 TLS connection on ESP32?
                    SendResponse(btCommand,UNAPI_ERR_NO_FREE_CONN,0,0);
                  else
                  {
                    if (uiCmdDataLen > 12)
                    {
                      bHasHostName = true;
                      memset (uchTLSHost, 0, sizeof(uchTLSHost));
                      if ((uiCmdDataLen-11)>256)
                        memcpy (uchTLSHost,&btCommandData[11],(uiCmdDataLen-11));
                      else
                        memcpy (uchTLSHost,&btCommandData[11],255);
                    }
                    else
                      bHasHostName = false;

                    for (uiForHelper=0;uiForHelper<4;uiForHelper++)
                      if (btConnections[uiForHelper] == CONN_CLOSED)
                        break;
                    btCMDConnNumber = uiForHelper+1;
                    bTLSInUse = btCMDConnNumber;
                    ClientList[uiForHelper] = new WiFiClientSecure();
                    TClient1 = (WiFiClientSecure*)ClientList[uiForHelper];
                    ClientList[uiForHelper]->setNoDelay(!stDeviceConfiguration.ucNagle);

                    if (btCMDPassive&8) //validate certificates?
                       loadCACertForClient(TClient1);
                    else
                      TClient1->setInsecure();

                    if (bHasHostName)
                      btCMDBeginRet = TClient1->connect((const char*)uchTLSHost, uiCMDRemotePort);
                    else
                      btCMDBeginRet = TClient1->connect(externalIP,uiCMDRemotePort);
  
                    if (btCMDBeginRet)
                    {
                      uiLocalPorts[uiForHelper] = TClient1->localPort();
                      uiRemotePorts[uiForHelper] = TClient1->remotePort();
                      switch (btCMDConnNumber) 
                      {
                        case 1: ipToBytes(externalIP, btIPConnection1); break;
                        case 2: ipToBytes(externalIP, btIPConnection2); break;
                        case 3: ipToBytes(externalIP, btIPConnection3); break;
                        case 4: ipToBytes(externalIP, btIPConnection4); break;
                      }
                      btConnections[uiForHelper] = CONN_TCP_TLS_A;
                      btIsTransient[uiForHelper] = btCMDTransient;
                      SendResponse(btCommand,UNAPI_ERR_OK,1,&btCMDConnNumber);
                    }
                    else
                    {
                      // TODO: instead of always returning ERR_TLS_OTHER, can we figure out error reason?
                      btCommandData[0] = 19;
                      delete ClientList[uiForHelper];
                      ClientList[uiForHelper] = NULL;
                      bTLSInUse = 0;
                      SendResponse(btCommand, UNAPI_ERR_NO_CONN, 1, btCommandData);
                    }
                  }
                }
                else //regular TCP
                {
                  //clear, so active connection
                  if (checkOpenConnections()<4) 
                  {
                    for (uiForHelper=0;uiForHelper<4;uiForHelper++)
                      if (btConnections[uiForHelper] == CONN_CLOSED)
                        break;
                    btCMDConnNumber = uiForHelper+1;
                    ClientList[uiForHelper] = new WiFiClient();
                    ClientList[uiForHelper]->setNoDelay(!stDeviceConfiguration.ucNagle);

                    btCMDBeginRet = ClientList[uiForHelper]->connect(externalIP,uiCMDRemotePort,uiCMDLocalPort);
                    if (btCMDBeginRet)
                    {
                      uiLocalPorts[uiForHelper] = ClientList[uiForHelper]->localPort();
                      uiRemotePorts[uiForHelper] = ClientList[uiForHelper]->remotePort();
                      switch (btCMDConnNumber) 
                      {
                        case 1: ipToBytes(externalIP, btIPConnection1); break;
                        case 2: ipToBytes(externalIP, btIPConnection2); break;
                        case 3: ipToBytes(externalIP, btIPConnection3); break;
                        case 4: ipToBytes(externalIP, btIPConnection4); break;
                      }
                      btConnections[uiForHelper] = CONN_TCP_ACTIVE;
                      btIsTransient[uiForHelper] = btCMDTransient;
                      SendResponse(btCommand,UNAPI_ERR_OK,1,&btCMDConnNumber);
                    }
                    else // (!btCMDBeginRet)
                    {
                      btCommandData[0]=0; //unknown reason
                      SendResponse(btCommand,UNAPI_ERR_NO_CONN,1,btCommandData);
                    }
                  }
                  else
                    SendResponse(btCommand,UNAPI_ERR_NO_FREE_CONN,0,0);
                }
              }
              else
                SendResponse(btCommand,UNAPI_ERR_CONN_EXISTS,0,0);
            }
          }
        break;

        case TCPIP_UDP_OPEN:
          uiCMDLocalPort = btCommandData[0] + btCommandData[1]*256;
          btCMDTransient = btCommandData[2];
          btCMDBeginRet=0;
          if ((uiCmdDataLen != 3) || (uiCMDLocalPort==0) || ((uiCMDLocalPort>=0xfff0)&&(uiCMDLocalPort!=0xffff)) || (btCMDTransient>1) )
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else if (WiFi.status() != WL_CONNECTED)
            SendResponse(btCommand,UNAPI_ERR_NO_NETWORK,0,0);
          else
          {
            if (CheckActiveLocalPort(&uiCMDLocalPort))
            {
              if (checkOpenConnections()<4)
              {
                for (uiForHelper=0;uiForHelper<4;uiForHelper++)
                  if (btConnections[uiForHelper] == CONN_CLOSED)
                    break;
                btCMDConnNumber = uiForHelper+1;
                switch (btCMDConnNumber)
                {
                  case 1:
                    btCMDBeginRet = Udp1.begin(uiCMDLocalPort);
                  break;
                  case 2:
                    btCMDBeginRet = Udp2.begin(uiCMDLocalPort);
                  break;
                  case 3:
                    btCMDBeginRet = Udp3.begin(uiCMDLocalPort);
                  break;
                  case 4:
                    btCMDBeginRet = Udp4.begin(uiCMDLocalPort);
                  break;
                }
                if (!btCMDBeginRet)
                  SendResponse(btCommand,UNAPI_ERR_CONN_EXISTS,0,0);
                else
                {
                  btConnections[uiForHelper] = CONN_UDP;
                  uiLocalPorts[uiForHelper] = uiCMDLocalPort;
                  btIsTransient[uiForHelper] = btCMDTransient;
                  SendResponse(btCommand,UNAPI_ERR_OK,1,&btCMDConnNumber);
                }
              }
              else
                SendResponse(btCommand,UNAPI_ERR_NO_FREE_CONN,0,0);
            }
            else
              SendResponse(btCommand,UNAPI_ERR_CONN_EXISTS,0,0);
          }
        break;

        case TCPIP_TCP_CLOSE:
        case TCPIP_TCP_ABORT:
          btCMDConnNumber = btCommandData[0];
          if (uiCmdDataLen != 1)
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else
          {
            if (btCMDConnNumber>4)
              SendResponse(btCommand,UNAPI_ERR_NO_CONN,0,0);
            else
            {
              if (btCMDConnNumber) //Not close all
              {
                if ( (btConnections[btCMDConnNumber-1] == CONN_TCP_ACTIVE) || (btConnections[btCMDConnNumber-1] == CONN_TCP_PASSIVE) || (btConnections[btCMDConnNumber-1] == CONN_TCP_TLS_A))
                {
                  CloseTcpConnection (btCMDConnNumber);
                  SendResponse(btCommand,UNAPI_ERR_OK,0,0);
                }
                else
                  SendResponse(btCommand,UNAPI_ERR_NO_CONN,0,0);
              }
              else //Close all open transient
              {
                for (uiForHelper=0;uiForHelper<4;uiForHelper++)
                {
                  if ( ((btConnections[uiForHelper] == CONN_TCP_ACTIVE) || (btConnections[uiForHelper] == CONN_TCP_PASSIVE) || (btConnections[uiForHelper] == CONN_TCP_TLS_A)) && (btIsTransient[uiForHelper]) )
                    CloseTcpConnection ((byte)(uiForHelper+1));
                }
                SendResponse(btCommand,UNAPI_ERR_OK,0,0);
              }
            }
          }
        break;

        case TCPIP_UDP_CLOSE:
          btCMDConnNumber = btCommandData[0];
          if (uiCmdDataLen != 1)
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else
          {
            if (btCMDConnNumber>4)
              SendResponse(btCommand,UNAPI_ERR_NO_CONN,0,0);
            else
            {
              if (btCMDConnNumber) //Not close all
              {
                if (btConnections[btCMDConnNumber-1] == CONN_UDP)
                {
                  CloseUdpConnection (btCMDConnNumber);
                  SendResponse(btCommand,UNAPI_ERR_OK,0,0);
                }
                else
                  SendResponse(btCommand,UNAPI_ERR_NO_CONN,0,0);
              }
              else //Close all open transient
              {
                for (uiForHelper=0;uiForHelper<4;uiForHelper++)
                {
                  if ( (btConnections[uiForHelper] == CONN_UDP) && (btIsTransient[uiForHelper]) )
                    CloseUdpConnection ((byte)(uiForHelper+1));
                }
                SendResponse(btCommand,UNAPI_ERR_OK,0,0);
              }
            }
          }
        break;

        case TCPIP_TCP_STATE:
          btCMDConnNumber = btCommandData[0];
          if (uiCmdDataLen != 1)
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else
          {
            if ((btCMDConnNumber>4)||(btCMDConnNumber==0))
              SendResponse(btCommand,UNAPI_ERR_NO_CONN,0,0);
            else
            {
              TcpState(btCMDConnNumber);
            }
          }
        break;

        case TCPIP_UDP_STATE:
          btCMDConnNumber = btCommandData[0];
          if (uiCmdDataLen != 1)
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else
          {
            if ((btCMDConnNumber>4)||(btCMDConnNumber==0))
              SendResponse(btCommand,UNAPI_ERR_NO_CONN,0,0);
            else
            {
              if (btConnections[btCMDConnNumber-1] == CONN_UDP)
              {
                switch (btCMDConnNumber)
                {
                  case 1:
                    iLenght = Udp1.parsePacket();
                  break;
                  case 2:
                    iLenght = Udp2.parsePacket();
                  break;
                  case 3:
                    iLenght = Udp3.parsePacket();
                  break;
                  case 4:
                    iLenght = Udp4.parsePacket();
                  break;
                }

                btCommandData[0] = (byte)(uiLocalPorts[btCMDConnNumber-1]&0xff);
                btCommandData[1] = (byte)((uiLocalPorts[btCMDConnNumber-1]>>8)&0xff);
                if (iLenght)
                  btCommandData[2]=1; //at least one packet
                else
                  btCommandData[2]=0; //no packet
                btCommandData[3] = (byte)(iLenght&0xff);
                btCommandData[4] = (byte)((iLenght>>8)&0xff);
                SendResponse(btCommand,UNAPI_ERR_OK,5,btCommandData);
              }
              else
                SendResponse(btCommand,UNAPI_ERR_NO_CONN,0,0);
            }
          }
        break;

        case TCPIP_TCP_SEND:
          if ((uiCmdDataLen < 3)||(btCommandData[1]&0xfe)) //only bit 1 is accepted
            SendResponse(btCommand,UNAPI_ERR_NO_CONN,0,0);
          else if (WiFi.status() != WL_CONNECTED)
            SendResponse(btCommand,UNAPI_ERR_NO_NETWORK,0,0);
          else
          {
            if ((btCommandData[0]>4)||(btCommandData[0]==0))
              SendResponse(btCommand,UNAPI_ERR_NO_CONN,0,0);
            else
            {
              TcpSend(btCommandData[0],&btCommandData[2],uiCmdDataLen-2);
            }
          }
        break;

        case TCPIP_UDP_SEND:
          btCMDConnNumber = btCommandData[0];
          externalIP[0] = btCommandData[1];
          externalIP[1] = btCommandData[2];
          externalIP[2] = btCommandData[3];
          externalIP[3] = btCommandData[4];
          ui16Port = btCommandData[5] + (btCommandData[6]*256);
          if (uiCmdDataLen < 8)
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else
          {
            if ((btCMDConnNumber>4)||(btCMDConnNumber==0))
              SendResponse(btCommand,UNAPI_ERR_NO_CONN,0,0);
            else
            {
              if (btConnections[btCMDConnNumber-1] == CONN_UDP)
              {
                if (WiFi.status() == WL_CONNECTED)
                {
                  switch (btCMDConnNumber)
                  {
                    case 1:
                      Udp1.beginPacket(externalIP,ui16Port);
                      Udp1.write(&btCommandData[7],uiCmdDataLen-7);
                      Udp1.endPacket();
                    break;
                    case 2:
                      Udp2.beginPacket(externalIP,ui16Port);
                      Udp2.write(&btCommandData[7],uiCmdDataLen-7);
                      Udp2.endPacket();
                    break;
                    case 3:
                      Udp3.beginPacket(externalIP,ui16Port);
                      Udp3.write(&btCommandData[7],uiCmdDataLen-7);
                      Udp3.endPacket();
                    break;
                    case 4:
                      Udp4.beginPacket(externalIP,ui16Port);
                      Udp4.write(&btCommandData[7],uiCmdDataLen-7);
                      Udp4.endPacket();
                    break;
                  }
                  yield();
                  SendResponse(btCommand,UNAPI_ERR_OK,0,0);
                }
                else
                  SendResponse(btCommand,UNAPI_ERR_NO_CONN,0,0);
              }
              else
                  SendResponse(btCommand,UNAPI_ERR_NO_NETWORK,0,0);
            }
          }
        break;

        case TCPIP_TCP_RCV:
          btCMDConnNumber = btCommandData[0];
          ui16RcvSize = btCommandData[1] + (btCommandData[2]*256);
          if (ui16RcvSize>2048)
            ui16RcvSize = 2048;
          if (uiCmdDataLen != 3)
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else
          {
            if ((btCMDConnNumber>4)||(btCMDConnNumber==0))
              SendResponse(btCommand,UNAPI_ERR_NO_CONN,0,0);
            else
            {
              TcpReceive(btCMDConnNumber,ui16RcvSize,btCommandData);
            }
          }
        break;

        case TCPIP_UDP_RCV:
          btCMDConnNumber = btCommandData[0];
          ui16RcvSize = btCommandData[1] + (btCommandData[2]*256);
          if (uiCmdDataLen != 3)
            SendResponse(btCommand,UNAPI_ERR_INV_PARAM,0,0);
          else
          {
            if ((btCMDConnNumber>4)||(btCMDConnNumber==0))
              SendResponse(btCommand,UNAPI_ERR_NO_CONN,0,0);
            else
            {
              if (btConnections[btCMDConnNumber-1] == CONN_UDP)
              {
                switch (btCMDConnNumber)
                {
                  case 1:
                    iLenght = Udp1.parsePacket();
                    if (iLenght)
                    {
                      externalIP = Udp1.remoteIP();
                      btCommandData[1] = externalIP[0];
                      btCommandData[2] = externalIP[1];
                      btCommandData[3] = externalIP[2];
                      btCommandData[4] = externalIP[3];
                      btCommandData[5] = (byte)(Udp1.remotePort()&0xff);
                      btCommandData[6] = (byte)((Udp1.remotePort()>>8)&0xff);
                      iLenght = Udp1.read(&btCommandData[7],iLenght);
                      if (iLenght>ui16RcvSize)
                        iLenght = ui16RcvSize;
                    }
                  break;
                  case 2:
                    iLenght = Udp2.parsePacket();
                    if (iLenght)
                    {
                      externalIP = Udp2.remoteIP();
                      btCommandData[1] = externalIP[0];
                      btCommandData[2] = externalIP[1];
                      btCommandData[3] = externalIP[2];
                      btCommandData[4] = externalIP[3];
                      btCommandData[5] = (byte)(Udp2.remotePort()&0xff);
                      btCommandData[6] = (byte)((Udp2.remotePort()>>8)&0xff);
                      iLenght = Udp2.read(&btCommandData[7],MAX_CMD_DATA_LEN);
                      if (iLenght>ui16RcvSize)
                        iLenght = ui16RcvSize;
                    }
                  break;
                  case 3:
                    iLenght = Udp3.parsePacket();
                    if (iLenght)
                    {
                      externalIP = Udp3.remoteIP();
                      btCommandData[1] = externalIP[0];
                      btCommandData[2] = externalIP[1];
                      btCommandData[3] = externalIP[2];
                      btCommandData[4] = externalIP[3];
                      btCommandData[5] = (byte)(Udp3.remotePort()&0xff);
                      btCommandData[6] = (byte)((Udp3.remotePort()>>8)&0xff);
                      iLenght = Udp3.read(&btCommandData[7],MAX_CMD_DATA_LEN);
                      if (iLenght>ui16RcvSize)
                        iLenght = ui16RcvSize;
                    }
                  break;
                  case 4:
                    iLenght = Udp4.parsePacket();
                    if (iLenght)
                    {
                      externalIP = Udp4.remoteIP();
                      btCommandData[1] = externalIP[0];
                      btCommandData[2] = externalIP[1];
                      btCommandData[3] = externalIP[2];
                      btCommandData[4] = externalIP[3];
                      btCommandData[5] = (byte)(Udp4.remotePort()&0xff);
                      btCommandData[6] = (byte)((Udp4.remotePort()>>8)&0xff);
                      iLenght = Udp4.read(&btCommandData[7],MAX_CMD_DATA_LEN);
                      if (iLenght>ui16RcvSize)
                        iLenght = ui16RcvSize;
                    }
                  break;
                }
                if (iLenght)
                  iLenght = iLenght + 6;
                if (iLenght)
                  SendResponse(btCommand,UNAPI_ERR_OK,iLenght,&btCommandData[1]);
                else
                  SendResponse(btCommand,UNAPI_ERR_NO_DATA,0,0);                
              }
              else
                SendResponse(btCommand,UNAPI_ERR_NO_CONN,0,0);
            }
          }
        break;

        case TCPIP_HTTP_OPEN:
          if (uiCmdDataLen > 0) {
            btCommandData[uiCmdDataLen] = 0;
            SendResponse(btCommand, http_open((const char *)btCommandData), 0, 0);
          } else {
            SendResponse(btCommand, UNAPI_ERR_INV_PARAM, 0, 0);
          }
          break;

        case TCPIP_HTTP_RECEIVE:
        {
          if (uiCmdDataLen == 1) {
            byte result = http_receive(btCommandData, &uiCmdDataLen, (btCommandData[0] > 0));
            SendResponse(btCommand, result, uiCmdDataLen, btCommandData);
          } else {
            SendResponse(btCommand, UNAPI_ERR_INV_PARAM, 0, 0);
          }
        }
          break;

        case TCPIP_HTTP_CLOSE:
          if (uiCmdDataLen == 0) {
            SendResponse(btCommand, http_close(UNAPI_ERR_OK), 0, 0);
          } else {
            SendResponse(btCommand, UNAPI_ERR_INV_PARAM, 0, 0);
          }
          break;

        default:
          SendResponse(btCommand,UNAPI_ERR_NOT_IMP,0,0);
        break;
      }
      btCmdInternalStep = 0;
      btState = RX_PARSER_IDLE;
      break;
  }
  if (!stDeviceConfiguration.ucAlwaysOn)
    ScheduleTimeoutCheck();
}

void loop() {
  unsigned int uiI;

  if (bDisableRadioPending) {
    bDisableRadioPending = false;
    DisableRadio();
  }

  if (Serial.available())
    received_data_parser();

  if ((!btReceivedCommand)&&(btReadyRetries))
  {
    if(longReadyTimeOut)
    {
      if (longReadyTimeOut<millis())
      {
        Serial.println("Ready");
        longReadyTimeOut = 0;
        --btReadyRetries;
      }
    }
    else
    {
      longReadyTimeOut = millis() + 5000;
    }
  }

  for (uiI=1;uiI<5;uiI++)
  {
    yield();
    if (btConnections[uiI-1] == CONN_TCP_PASSIVE && ServerList[uiI-1] != NULL)
    {
       WiFiClient newClient = ServerList[uiI-1]->available();
        if (newClient)
        {
            if (ClientList[uiI-1] == NULL) 
            {
                ClientList[uiI-1] = new WiFiClient(newClient);
                externalIP = newClient.remoteIP();
                uiRemotePorts[uiI-1] = newClient.remotePort();
                switch (uiI) 
                {
                    case 1: ipToBytes(externalIP, btIPConnection1); break;
                    case 2: ipToBytes(externalIP, btIPConnection2); break;
                    case 3: ipToBytes(externalIP, btIPConnection3); break;
                    case 4: ipToBytes(externalIP, btIPConnection4); break;
                }
            }
        }
    }
  }
}
