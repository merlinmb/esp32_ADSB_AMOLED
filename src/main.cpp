// Call up the SPIFFS FLASH filing system
#include <FS.h>
#include <SPIFFS.h>

#include <Arduino.h>
#include <LilyGo_AMOLED.h> //To use LilyGo AMOLED series screens, please include <LilyGo_AMOLED.h>
#include "TFT_eSPI.h"

#include <Wire.h>

#include "time.h"
#include "connectionDetails.h"

#include <TimeLib.h>

// #define SECURE 1
#include "merlinNetwork.h"
#include "merlinUpdateWebServer.h"

#include "merlinFlightStats.h"

#include "fonts/Orbitron_Medium_20.h"
#include "fonts/NotoSansBold15.h"
#include "fonts/Monospaced_plain_12.h"
#include "fonts/Monospaced_plain_14.h"
#include "fonts/Monospaced_plain_16.h"
#include "fonts/Monospaced_plain_18.h"
#include "fonts/Orbitron_Bold_16.h"
#include "fonts/Orbitron_Medium_14.h"

#include "OneButton.h"

#define WMAPNAME "ADSB_Monitor"

#define LOAD_GLCD  // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2 // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4 // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
#define LOAD_FONT6 // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
#define LOAD_FONT7 // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:-.
#define LOAD_FONT8 // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
// #define LOAD_FONT8N // Font 8. Alternative to Font 8 above, slightly narrower, so 3 digits fit a 160 pixel TFT

#define DEBUGFONT Orbitron_Medium_20

#define CLOCKFONT Orbitron_Light_32 // Orbitron_Medium_20

#define ADSBLINEFONT Monospaced_plain_16
#define ADSBLINETIMEFONT Monospaced_plain_18
#define ADSBLINEHEADINGFONT Orbitron_Medium_20 // Square digital
#define ADSBLINEDETAILFONT Orbitron_Medium_20  // Square digital

#define NODEPARTURESHEADINGFONT Orbitron_Medium_20 // Square digital

#define FLIGHTDETAILS_MINIHEADINGFONT Monospaced_plain_16
#define FLIGHTDETAILS_LABELFONT Orbitron_Light_32
#define FLIGHTDETAILS_NUMBERFONT Orbitron_Light_32
#define FLIGHTDETAILS_FONTMEDIUM Orbitron_Medium_20
#define FLIGHTDETAILS_FONTSMALL Monospaced_plain_16
#define FLIGHTDETAILS_DESCRIPTIONFONT Orbitron_Medium_20

#define SYSINFOHEADINGFONT Orbitron_Medium_20 // Square digital
#define SYSINFOFONTMEDIUM Orbitron_Bold_16
#define SYSINFOLABELFONT Monospaced_plain_12

#define BUTTON1 21
#define BUTTON2 0

OneButton _button1 = OneButton(BUTTON1, true, true);
OneButton _button2 = OneButton(BUTTON2, true, true);

#define BRIGHTNESS_PIN 14
int _brightnesses[5] = {0, 51, 115, 192, 255};
int _selectedBrightness = 4;

#define NUMBERSCREENS 1
#define TIMEBOXMARGIN 5

#define location "51.39502, -1.3387" // 97 Enborne Road

#define MCMDVERSION 1.4

#define MAXBRIGHTNESS 255
#define MINBRIGHTNESS 20

#define BACKGROUNDCOLOR TFT_BLACK
#define CENTER_COLOR TFT_GREENYELLOW

#define CALLINGATBACKGROUNDCOLOR TFT_BLACK
#define CALLINGATMARGIN 60
#define CALLINGATSPRITEMOVEBY 12

bool _brightnessHigh;
byte _brightness = 100;

int px = 10;
int py = 10;

String _mqttPostFix = "";
float _batteryVoltage = 0;

#define HTTPSPORT 443
#define icon_width 25
#define icon_height 25

boolean _forceUpdate = false;
boolean _forceRender = false;

/* frames */
byte _currentFrame = 2;
int _currentSubFrame = 0;
#define MAXRENDER_EMERGENCIES 4

bool _forceDrawClock = false;
bool _skipDrawClock = false;

bool _forceDrawCallingAt = false;
bool _forceDrawFlightDetails = false;
bool _forceDrawSysInfo = false;
bool _forceDrawADSB = false;
bool _forceDrawEmpty = false;

String _locationCode = "";

TFT_eSPI _display = TFT_eSPI();
TFT_eSprite _mainSprite = TFT_eSprite(&_display);
TFT_eSprite _overviewStatSprite = TFT_eSprite(&_display);
TFT_eSprite _topStatSprite = TFT_eSprite(&_display);
TFT_eSprite _mapSprite = TFT_eSprite(&_display);
TFT_eSprite _emergencySprite[MAXRENDER_EMERGENCIES] = {TFT_eSprite(&_display), TFT_eSprite(&_display), TFT_eSprite(&_display), TFT_eSprite(&_display)};

LilyGo_Class _amoled;
#define DISPLAY_WIDTH 536  //_amoled.width()
#define DISPLAY_HEIGHT 240 //_amoled.height()

#define CENTERX DISPLAY_WIDTH / 2
#define CENTERY DISPLAY_HEIGHT / 2

#define UPDATE_WIFICHECK_INTERVAL_MILLISECS 60000 // Update every 1 min
#define UPDATE_CALLINGATSCROLL 120                // Update every 200ms
#define UPDATE_CALLINGATSCROLLPAUSE 2000          // Pause for x time if the end of the scrolling screen is reached
#define UPDATE_UI_FRAME_INTERVAL_MILLISECS 10000  // transition screen every ... milliseconds
#define UPDATE_ADSBS_INTERVAL_MILLISECS 30000     // Update every 30 seconds
#define UPDATE_EMPTY_INTERVAL_MILLISECS 10000     // Update every 10seconds
#define UPDATE_TIME_INTERVAL_MILLISECS 900        // Update every <1sec

unsigned long _runCurrent;
unsigned long _runFrame;
unsigned long _runTime;
unsigned long _runEmptyFrame;
unsigned long _runDataUpdate = 0;
unsigned long _runWiFiConnectionCheck = 0;
unsigned long _runBrightness = 0;

bool _initComplete = false;
bool _displayInit = false;

const byte DEBUGBUFFERLENGTH = 8;
byte _debugBufferPosition = 0;
String _debugBuffer[DEBUGBUFFERLENGTH];

String _ip = "";

#define NTPTIMEOUTVAL 4500
const char *ntpServer = "pool.ntp.org";

const long timezoneOffset = 0; // 0-23
const long gmtOffset_sec = timezoneOffset * 60 * 60;
const int daylightOffset_sec = 0;
unsigned long _epochTime;

long period = 1000;
long currentTime = 0;

String _configStation = "";
int _configFlipSreen = 999;

String _lastMQTTMessage = "";

#ifndef _swap_int16_t
#define _swap_int16_t(a, b) \
  {                         \
    int16_t t = a;          \
    a = b;                  \
    b = t;                  \
  }
#endif


/***************************************************
  Screen Control Functions
****************************************************/

void setBrightness(byte brightnessValue)
{
  DEBUG_PRINTLN("setBrightness: " + String(brightnessValue));

  _amoled.setBrightness(brightnessValue);
  

  for (int i = 0; i < 5; i++)
  {
    if (brightnessValue == _brightnesses[i])
    {
      _selectedBrightness = i;
      break;
    }
  }
}

void toggleBrightness(bool isBright)
{
  _brightness = (isBright) ? MAXBRIGHTNESS : MINBRIGHTNESS;

  //_display.setContrast(isBright ? 255 : 80);
  //_currentNeoPixelColour = pixelBrightness(_currentNeoPixelColour, _brightness);
  setBrightness(_brightness);

  _brightnessHigh = isBright;
}

void clearSprite()
{
  _mainSprite.fillSprite(BACKGROUNDCOLOR);
}

void clearOverviewSprite()
{
  _overviewStatSprite.fillSprite(BACKGROUNDCOLOR);
}

void clearTopStatSprite()
{
  _topStatSprite.fillSprite(BACKGROUNDCOLOR);
}

void clearMapSprite()
{
  _mapSprite.fillSprite(BACKGROUNDCOLOR);
}

void clearEmergencySprite(byte line)
{
  _emergencySprite[line].fillSprite(BACKGROUNDCOLOR);
}

void clear_Display()
{
  clearSprite();
  _amoled.pushColors(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, (uint16_t *)_mainSprite.getPointer());
}

/***************************************************
  Clock Rendering
****************************************************/

#define TFT_GREY 0x5AEB



bool initial = 1;

/***************************************************
  SPIFFS functions
****************************************************/
bool parseConfigValue(String key, String value)
{
  DEBUG_PRINTLN("Parsing Config Value, " + key + ": " + value);

  key.toLowerCase();
  value.trim();

  if (key == "station")
  {
    value.toUpperCase();
    if (_configStation != value)
    {
      _configStation = value;
      _locationCode = _configStation;
      _forceUpdate = true;
    }
  }

  if (key == "flipscreen")
  {
    int __intValue = (value == "true" ? 3 : 1);
    if (_configFlipSreen != __intValue)
    {
      _configFlipSreen = __intValue;
      _display.setRotation(_configFlipSreen);
    }
  }

  if (key == "brightness")
  {
    int __newVal = value.toInt();
    if (_brightness != __newVal)
    {
      _brightness = value.toInt();
      setBrightness(_brightness);
    }
  }

  DEBUG_PRINTLN("parseConfigValue() - completed...");

  return true;
}

void setupSPIFFS()
{
  if (SPIFFS.begin())
  {
    DEBUG_PRINTLN("SPIFFS: Mounted file system");
  }
  else
  {
    DEBUG_PRINTLN("SPIFFS: FAILED to mount file system!");
  }
}
void loadCustomParamsSPIFFS()
{
  // read configuration from FS json
  DEBUG_PRINTLN("loadCustomParamsSPIFFS() - Open config file...");

  File __configFile = SPIFFS.open("/config.ini", FILE_READ);
  if (__configFile)
  {
    DEBUG_PRINTLN("Reading config file [" + String(__configFile.size()) + " bytes]");
    while (__configFile.available())
    {
      String __inString = __configFile.readStringUntil('\n');
      DEBUG_PRINTLN("Read line: " + __inString);
      int __equalsLoc = __inString.indexOf('=');

      String __key = __inString.substring(0, __equalsLoc);
      String __value = __inString.substring(__equalsLoc + 1, __inString.length());

      parseConfigValue(__key, __value);
    }

    DEBUG_PRINTLN("loadCustomParamsSPIFFS() - close config file...");
    __configFile.close();
    DEBUG_PRINTLN("... Done");
  }
}

void writeStrtoFile(File file, String key, String value)
{
  DEBUG_PRINTLN("    " + key + ": " + value);
  file.println(key + "=" + value);
}

void saveConfigValuesSPIFFS()
{
  DEBUG_PRINTLN("saveConfigValuesSPIFFS()");
  if (SPIFFS.remove("/config.ini"))
  {
    DEBUG_PRINTLN("Deleted old file");
  }

  DEBUG_PRINTLN("Open File in Write Mode");
  // open the file in write mode
  File __configFile = SPIFFS.open("/config.ini", FILE_WRITE);
  DEBUG_PRINTLN("Saving config to FS");

  writeStrtoFile(__configFile, "station", String(_configStation));
  writeStrtoFile(__configFile, "flipscreen", String(_configFlipSreen == 3));
  writeStrtoFile(__configFile, "brightness", String(_brightness));
  __configFile.close();
  DEBUG_PRINTLN("... Done");
  delay(250); // give SPIFFS chance to settle
}

void drawProgress(byte percentage, String label)
{
  clearSprite();
  //_sprite.setTextAlignment(TEXT_ALIGN_CENTER);
  //_sprite.setFont(ArialMT_Plain_10);
  //_sprite.drawString(64, 10, label);
  //_sprite.drawProgressBar(2, 28, 124, 10, percentage);
  //_sprite.display();
}

void DisplayOut(String outStr)
{

  _debugBufferPosition++;
  if (_debugBufferPosition >= DEBUGBUFFERLENGTH)
  {
    for (byte i = 0; i < DEBUGBUFFERLENGTH - 1; i++) // StackArray - shift to left
    {
      _debugBuffer[i] = _debugBuffer[i + 1];
    }
    _debugBufferPosition = DEBUGBUFFERLENGTH - 1;
  }

  DEBUG_PRINTLN(outStr);

  if (_initComplete)
    return;

  // render
  clearSprite();

  _mainSprite.setFreeFont(&DEBUGFONT);
  _mainSprite.setTextColor(TFT_WHITE, TFT_BLACK, true);
  //_sprite.setTextSize(12);
  _debugBuffer[_debugBufferPosition] = outStr;
  for (byte i = 0; i < _debugBufferPosition; i++) // StackArray - shift to left
  {
    _mainSprite.drawString(_debugBuffer[i], 5, 26 * i);
  }
  _mainSprite.unloadFont();

  //_sprite.pushSprite(0, 0);
  _amoled.pushColors(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, (uint16_t *)_mainSprite.getPointer());
  // delay(100);
}

// Draw a triangle
void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint32_t colour)
{
  _mainSprite.drawLine(x0, y0, x1, y1, colour);
  _mainSprite.drawLine(x1, y1, x2, y2, colour);
  _mainSprite.drawLine(x2, y2, x0, y0, colour);
}

// Draw a triangle
void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
  drawTriangle(x0, y0, x1, y1, x2, y2, TFT_WHITE);
}

// Fill a triangle
void fillTriangle(TFT_eSprite &__sprite, uint16_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{

  int16_t a, b, y, last;
  // Sort coordinates by Y order (y2 >= y1 >= y0)
  if (y0 > y1)
  {
    _swap_int16_t(y0, y1);
    _swap_int16_t(x0, x1);
  }
  if (y1 > y2)
  {
    _swap_int16_t(y2, y1);
    _swap_int16_t(x2, x1);
  }
  if (y0 > y1)
  {
    _swap_int16_t(y0, y1);
    _swap_int16_t(x0, x1);
  }

  if (y0 == y2)
  { // Handle awkward all-on-same-line case as its own thing
    a = b = x0;
    if (x1 < a)
      a = x1;
    else if (x1 > b)
      b = x1;
    if (x2 < a)
      a = x2;
    else if (x2 > b)
      b = x2;
      __sprite.drawFastHLine(a, y0, b - a + 1, color);
    return;
  }

  int16_t
      dx01 = x1 - x0,
      dy01 = y1 - y0,
      dx02 = x2 - x0,
      dy02 = y2 - y0,
      dx12 = x2 - x1,
      dy12 = y2 - y1;
  int32_t
      sa = 0,
      sb = 0;

  // For upper part of triangle, find scanline crossings for segments
  // 0-1 and 0-2.  If y1=y2 (flat-bottomed triangle), the scanline y1
  // is included here (and second loop will be skipped, avoiding a /0
  // error there), otherwise scanline y1 is skipped here and handled
  // in the second loop...which also avoids a /0 error here if y0=y1
  // (flat-topped triangle).
  if (y1 == y2)
    last = y1; // Include y1 scanline
  else
    last = y1 - 1; // Skip it
  for (y = y0; y <= last; y++)
  {
    a = x0 + sa / dy01;
    b = x0 + sb / dy02;
    sa += dx01;
    sb += dx02;
    /* longhand:
      a = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
      b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
    */
    if (a > b)
      _swap_int16_t(a, b);
      __sprite.drawFastHLine(a, y, b - a + 1, color);
  }

  // For lower part of triangle, find scanline crossings for segments
  // 0-2 and 1-2.  This loop is skipped if y1=y2.
  sa = dx12 * (y - y1);
  sb = dx02 * (y - y0);
  for (; y <= y2; y++)
  {
    a = x1 + sa / dy12;
    b = x0 + sb / dy02;
    sa += dx12;
    sb += dx02;
    /* longhand:
      a = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
      b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
    */
    if (a > b)
      _swap_int16_t(a, b);
    // writeFastHLine(a, y, b - a + 1);
    // display->drawHorizontalLine(a, y, b - a + 1);
    __sprite.drawFastHLine(a, y, b - a + 1, color);
  }
}

void fillandDrawRect(int x1, int y1, int x2, int y2, int radius, uint8_t bgColor, uint8_t lineColor)
{
  _mainSprite.fillRect(x1, y1, x2, y2, bgColor);
  _mainSprite.fillRoundRect(x1 - TIMEBOXMARGIN - 3, y1 - TIMEBOXMARGIN - 3, x2 - x1 + TIMEBOXMARGIN * 2 + 6, y2 - y1 + TIMEBOXMARGIN * 2 + 6, radius, lineColor);
}
/***************************************************
  ADSB Data Parsing
****************************************************/
String fixTimeFormatting(String _shortTime)
{
  // DEBUG_PRINTLN("ETA: " + __eta);
  _shortTime.trim();
  int __strLen = _shortTime.length();

  if (__strLen > 0)
    return _shortTime.substring(0, 2) + ":" + _shortTime.substring(2);
  else
    return "tba";
}

String returnPostfix(int number)
{

  String postfix;

  if (number >= 11 && number <= 13)
  {
    postfix = "th";
  }
  else
  {
    int lastDigit = number % 10;
    switch (lastDigit)
    {
    case 1:
      postfix = "st";
      break;
    case 2:
      postfix = "nd";
      break;
    case 3:
      postfix = "rd";
      break;
    default:
      postfix = "th";
      break;
    }
  }

  return postfix;
}

boolean isDigit(char c)
{
  return ((c >= '0') && (c <= '9'));
}

// check a string to see if it is numeric
bool isNumeric(String str)
{
  for (byte i = 0; i < str.length(); i++)
  {
    if (isDigit(str.charAt(i)))
      return true;
  }
  return false;
}

String getLiveADSBStatusFromInput(String input)
{
  if (isNumeric(input))
    return (fixTimeFormatting(input));
  return input;
}

u_int16_t getLiveADSBStatusColorFromInput(String _flightStatus)
{

  _flightStatus.toLowerCase();

  if (_flightStatus == "cruising")
  {
    return TFT_ORANGE;
  }

  else if (_flightStatus == "descending")
  {
    return TFT_RED;
  }

  else if (_flightStatus == "ascending")
  {
    return TFT_GREEN;
  }

  return TFT_WHITE;
}

u_int16_t getLiveADSBStatusColorFromSquawk(int _flightSquawk)
{
  if (isSquawkEmergency(_flightSquawk))
  {
    return TFT_RED;
  }
  return TFT_WHITE;
}

/***************************************************
  Display & Rendering
****************************************************/
void drawClockBase(int centerX, int centerY, u_int16_t clockColor = TFT_YELLOW)
{
  // x,y,w,h,color
  _mainSprite.fillRect(centerX - 80, centerY - 36, 200, 36, TFT_BLACK);

  _mainSprite.setTextColor(clockColor);
  _mainSprite.setFreeFont(&CLOCKFONT);
  _mainSprite.setTextDatum(BL_DATUM);

  _mainSprite.drawString(String(timeHour) + ":" + String(timeMin) + ":" + String(timeSec), centerX - 60, centerY - 1);

  _mainSprite.unloadFont();
}

#define COL1 20
#define COL2 COL1 + 160
#define COL3 COL2 + 185
#define COL4 COL3 + 65
#define COL5 DISPLAY_WIDTH - 20

#define ROW1 2
#define ROW2 ROW1 + 20
#define ROW3 ROW2 + 35
#define ROW4 ROW3 + 140
#define ROW5 ROW4 + 70

#define ROWHEIGHT 32
#define HEADINGMARGIN 12
#define HEADINGHEIGHT 20

void RenderHeadingtoSprite(TFT_eSprite &__sprite, String __heading, uint16_t __bgColor, uint16_t __fgColor)
{
  __sprite.fillRoundRect(0, 0, DISPLAY_WIDTH, HEADINGHEIGHT + 2, 4, __bgColor);
  __sprite.setFreeFont(&SYSINFOHEADINGFONT);
  __sprite.setTextDatum(TC_DATUM);
  __sprite.setTextColor(__fgColor);
  __sprite.drawString(__heading, CENTERX, 0);
  __sprite.unloadFont();
}

void outputRow(TFT_eSprite &__sprite, int __row, String _col1, String _col2 = "", String _col3 = "", String _col4 = "", String _col5 = "", uint16_t __overrideColor1 = TFT_CYAN, uint16_t __overrideColor2 = TFT_WHITE)
{
  __sprite.setFreeFont(&ADSBLINEFONT);
  __sprite.setTextColor(TFT_DARKGREY);
  __sprite.setTextDatum(TL_DATUM);

  __sprite.drawString(_col1.c_str(), COL1, __row * ROWHEIGHT);
  __sprite.drawString(_col4.c_str(), COL4, __row * ROWHEIGHT);
  __sprite.drawString(_col5.c_str(), COL5, __row * ROWHEIGHT);
  __sprite.unloadFont();

  __sprite.setFreeFont(&ADSBLINEDETAILFONT);
  __sprite.setTextColor(__overrideColor1);
  __sprite.drawString(_col2.c_str(), COL2, __row * ROWHEIGHT - 5);
  __sprite.setTextColor(__overrideColor2);
  __sprite.drawString(_col3.c_str(), COL3, __row * ROWHEIGHT - 5);
  __sprite.unloadFont();
}

void RenderGeneralStatsSprite()
{
  DEBUG_PRINTLN("RenderGeneralStatsSprite");
  clearTopStatSprite();

  RenderHeadingtoSprite(_topStatSprite, "Aircraft Statistics", TFT_WHITE, TFT_BLACK);

  outputRow(_topStatSprite, 1, "Fastest:", _flightStats.aircraft[_flightStats.fastestAircraft].identifier, String((int)(_flightStats.aircraft[_flightStats.fastestAircraft].speed)) + "kt");
  outputRow(_topStatSprite, 2, "Slowest:", _flightStats.aircraft[_flightStats.slowestAircraft].identifier, String((int)(_flightStats.aircraft[_flightStats.slowestAircraft].speed)) + "kt");
  outputRow(_topStatSprite, 3, "Highest:", _flightStats.aircraft[_flightStats.highestAircraft].identifier, String((int)(_flightStats.aircraft[_flightStats.highestAircraft].altitude)) + "ft");
  outputRow(_topStatSprite, 4, "Lowest:", _flightStats.aircraft[_flightStats.lowestAircraft].identifier, String((int)(_flightStats.aircraft[_flightStats.lowestAircraft].altitude)) + "ft");
  outputRow(_topStatSprite, 5, "Closest:", _flightStats.aircraft[_flightStats.closestAircraft].identifier, String((int)(_flightStats.aircraft[_flightStats.closestAircraft].distance)) + "nmi");
  outputRow(_topStatSprite, 6, "Farthest:", _flightStats.aircraft[_flightStats.farthestAircraft].identifier, String((int)(_flightStats.aircraft[_flightStats.farthestAircraft].distance)) + "nmi");
  outputRow(_topStatSprite, 7, "Emergencies:", (_flightStats.emergencyCount > 0) ? String(_flightStats.emergencyCount) : "None", "", "", "", (_flightStats.emergencyCount > 0) ? TFT_RED : TFT_GREEN);
}

#define P_ROWHEIGHTSMALL 14
#define P_ROWHEIGHTBIG 34
#define P_ROWFONTOFFSET 4

#define P_ROW1 2
#define P_ROW2 P_ROW1 + P_ROWHEIGHTSMALL
#define P_ROW3 P_ROW2 + P_ROWHEIGHTBIG
#define P_ROW4 P_ROW3 + P_ROWHEIGHTSMALL + P_ROWHEIGHTSMALL + P_ROWHEIGHTSMALL
#define P_ROW5 P_ROW4 + P_ROWHEIGHTSMALL
#define P_ROW6 P_ROW5 + P_ROWHEIGHTBIG
#define P_ROW7 P_ROW6 + P_ROWHEIGHTSMALL
#define P_ROW8 P_ROW7 + P_ROWHEIGHTSMALL
#define P_ROW9 P_ROW8 + P_ROWHEIGHTSMALL

#define P_COLWIDTHSMALL 8
#define P_COLWIDTHBIG 65

#define P_COL1 20
#define P_COL2 P_COL1 + P_COLWIDTHBIG
#define P_COL3 P_COL2 + P_COLWIDTHBIG
#define P_COL4 P_COL3 + P_COLWIDTHBIG
#define P_COL5 P_COL4 + P_COLWIDTHBIG + P_COLWIDTHSMALL
#define P_COL6 P_COL5 + P_COLWIDTHBIG

void RenderAircraftToSprite(TFT_eSprite &sprite, AircraftDetailsStruct aircraft)
{
  DEBUG_PRINTLN("RenderAircraftToSprite: ADSB");

  sprite.fillSprite(BACKGROUNDCOLOR);
  DEBUG_PRINTLN("RenderAircraftToSprite: render headings");
  sprite.setTextDatum(TL_DATUM);
  sprite.setFreeFont(&FLIGHTDETAILS_MINIHEADINGFONT);

  sprite.setTextColor(TFT_DARKGREY);
  sprite.drawString("closest flight", P_COL1, P_ROW1);
  sprite.setTextColor(TFT_YELLOW);

  sprite.setTextColor(TFT_DARKGREY);
  sprite.drawString("tracking", P_COL6, P_ROW1);
  sprite.drawString("status", P_COL5, P_ROW4);
  sprite.drawString("distance away", P_COL1, P_ROW4);
  sprite.drawString("squawk", P_COL1, P_ROW6);
  sprite.drawString("altitude", P_COL5, P_ROW6);

  sprite.setTextColor(TFT_WHITE);
  sprite.setTextDatum(TL_DATUM);

  // number of tracked aircraft
  if (_flightStats.totalAircraft > 0)
  {
    int __pBoxSize = 80;

    sprite.fillRoundRect(DISPLAY_WIDTH - __pBoxSize, P_ROW1, __pBoxSize - 2, P_ROW3 - 6, 4, TFT_MAGENTA);
    sprite.setTextDatum(TC_DATUM);

    sprite.setFreeFont(&FLIGHTDETAILS_NUMBERFONT);
    sprite.drawString(String(_flightStats.totalAircraft), (DISPLAY_WIDTH - __pBoxSize / 2) - 5, P_ROW1);
    sprite.unloadFont();
  }

  // callsign
  if (aircraft.identifier.length() > 0)
  {
    sprite.setTextDatum(TL_DATUM);
    sprite.setFreeFont(&FLIGHTDETAILS_LABELFONT);
    sprite.drawString(aircraft.identifier, P_COL2, P_ROW2);
    sprite.unloadFont();
  }

  // aircraft make
  if (aircraft.description.length() > 0)
  {
    sprite.setFreeFont(&FLIGHTDETAILS_DESCRIPTIONFONT);
    sprite.setTextColor(TFT_WHITE);
    sprite.setTextDatum(TC_DATUM);
    sprite.drawString(aircraft.description, DISPLAY_WIDTH / 2, P_ROW3);
    sprite.unloadFont();
  }

  // distance
  if (aircraft.distance > 0)
  {
    sprite.setFreeFont(&FLIGHTDETAILS_LABELFONT);
    sprite.setTextDatum(TL_DATUM);
    sprite.setTextColor(TFT_WHITE);
    sprite.drawString(String((int)aircraft.distance) + "nmi", P_COL2, P_ROW5);
    sprite.unloadFont();
  }

  // status asc, dec, cruise
  if (aircraft.status.length() > 0)
  {
    sprite.setTextDatum(TL_DATUM);
    sprite.setFreeFont(&FLIGHTDETAILS_LABELFONT);
    sprite.setTextColor(getLiveADSBStatusColorFromInput(aircraft.status));
    sprite.drawString(aircraft.status, P_COL5, P_ROW5);
    sprite.unloadFont();
  }

  // squawk
  if (aircraft.squawk > 0)
  {
    sprite.setTextDatum(TL_DATUM);
    sprite.setFreeFont(&FLIGHTDETAILS_LABELFONT);
    sprite.setTextColor(getLiveADSBStatusColorFromSquawk(aircraft.squawk));
    sprite.drawString(String(aircraft.squawk), P_COL2, P_ROW7);
    sprite.unloadFont();
  }

  // altitude
  if (aircraft.altitude > 0)
  {
    sprite.setTextDatum(TL_DATUM);
    sprite.setFreeFont(&FLIGHTDETAILS_LABELFONT);
    sprite.setTextColor(TFT_WHITE);
    sprite.drawString(String((int)aircraft.altitude) + "ft", P_COL5, P_ROW7);
    sprite.unloadFont();
  }
}

void RenderEmergencySprite(int __emergencyAircraftIndex)
{

  DEBUG_PRINTLN("RenderEmergencySprite: ADSB " + String(__emergencyAircraftIndex));
  RenderAircraftToSprite(_emergencySprite[__emergencyAircraftIndex], _flightStats.aircraft[_flightStats.emergencyAircraft[__emergencyAircraftIndex]]);
  _emergencySprite[__emergencyAircraftIndex].drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, TFT_RED);
  //_emergencySprite[__emergencyAircraftIndex].drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, TFT_RED);
}

void renderEmpty()
{
  clearSprite();

  _mainSprite.fillRoundRect(0, 0, DISPLAY_WIDTH, ROWHEIGHT * 2, 5, TFT_WHITE);
  _mainSprite.setTextColor(TFT_BLACK);
  _mainSprite.setFreeFont(&SYSINFOHEADINGFONT);

  /*
      TL_DATUM = 0 = Top left
      TC_DATUM = 1 = Top centre
      TR_DATUM = 2 = Top right
      ML_DATUM = 3 = Middle left
      MC_DATUM = 4 = Middle centre
      MR_DATUM = 5 = Middle right
      BL_DATUM = 6 = Bottom left
      BC_DATUM = 7 = Bottom centre
      BR_DATUM = 8 = Bottom right
  */
  _mainSprite.setTextDatum(1);
  _mainSprite.drawString("No aircraft are", CENTERX, 2);
  _mainSprite.drawString("currently being tracked", CENTERX, ROWHEIGHT);
  _mainSprite.unloadFont();
}

#define SI_ROWFONTOFFSET 6
#define SI_COLFONTOFFSET 15

void renderSystemInfo()
{
  DEBUG_PRINTLN("renderSystemInfo()");
  clearSprite();

  _batteryVoltage = (analogRead(4) * 2 * 3.3 * 1000) / 4096;
  String __batteryVoltage = String(_batteryVoltage / 1000, 1) + "V " + String(_batteryVoltage / 1000 / 5.0 * 100, 0) + "%";

  RenderHeadingtoSprite(_mainSprite, "Aircraft Tracker (v" + String(MCMDVERSION) + ")", TFT_CYAN, TFT_BLACK);

  outputRow(_mainSprite, 1, "WiFi", getWiFIAPName(), "", "", "");
  outputRow(_mainSprite, 2, "IP", IpAddress2String(WiFi.localIP()), "", "", "");
  outputRow(_mainSprite, 3, "Strength", String(dBmtoPercentage(WiFi.RSSI())) + "%", "", "", "");
  outputRow(_mainSprite, 4, "Power", __batteryVoltage, "", "", "");
  outputRow(_mainSprite, 5, "Memory", String(ESP.getFreeHeap()), "", "", "");
  outputRow(_mainSprite, 6, "By", "markbeets@gmail.com", "", "", "");
}

uint16_t getAircraftColorByAltitude(TFT_eSprite &sprite, int altitude)
{
  // Get the color based on the altitude
  uint8_t r, g, b;

  if (altitude <= 2000)
  {
    r = 255; g = 165; b = 0; // Orange for low altitude
  }
  else if (altitude <= 6000)
  {
    float factor = (altitude - 2000) / 4000.0;
    r = 255;
    g = 165 + factor * (255 - 165);
    b = 0;
  }
  else if (altitude <= 10000)
  {
    float factor = (altitude - 6000) / 4000.0;
    r = 255 - factor * 255;
    g = 255;
    b = factor * 255;
  }
  else if (altitude <= 20000)
  {
    float factor = (altitude - 10000) / 10000.0;
    r = 0;
    g = 255 - factor * 255;
    b = 255;
  }
  else if (altitude <= 30000)
  {
    float factor = (altitude - 20000) / 10000.0;
    r = factor * 75;
    g = 0;
    b = 255 - factor * (255 - 130);
  }
  else
  {
    float factor = (altitude - 30000) / 10000.0;
    r = 75 + factor * (238 - 75);
    g = factor * 130;
    b = 130 + factor * (238 - 130);
  }

  return sprite.color565(r, g, b);
}

// Function to render the map
void renderMap(TFT_eSprite &_sprite)
{
  DEBUG_PRINTLN("Rendering map...");

  // Clear the sprite
  DEBUG_PRINTLN("Clearing sprite...");
  _sprite.fillSprite(TFT_BLACK);


  if(_flightStats.totalAircraft==0)
  {
    renderEmpty();
    return;
  }

  _sprite.setTextDatum(TL_DATUM);
  _sprite.setFreeFont(&FLIGHTDETAILS_MINIHEADINGFONT);

  // Draw the center cross
  DEBUG_PRINTLN("Drawing center cross...");

  int centerX = DISPLAY_WIDTH / 2;
  int centerY = DISPLAY_HEIGHT / 2;

  // Determine the maximum latitude and longitude differences in miles
  float maxLatDiffMiles = 0;
  float maxLonDiffMiles = 0;
  for (int i = 0; i < _flightStats.totalAircraft; i++)
  {
    float latDiff = abs(_flightStats.aircraft[i].latitude - myLat);
    float lonDiff = abs(_flightStats.aircraft[i].longitude - myLon);

    // Convert latitude and longitude differences to miles
    float latDiffMiles = latDiff * 69.0;                       // 1 degree latitude ≈ 69 miles
    float lonDiffMiles = lonDiff * 69.0 * cos(radians(myLat)); // Adjust longitude by cos(latitude)

    if (latDiffMiles > maxLatDiffMiles)
      maxLatDiffMiles = latDiffMiles;
    if (lonDiffMiles > maxLonDiffMiles)
      maxLonDiffMiles = lonDiffMiles;
  }

  // Calculate scaling factors
  float xScale = (DISPLAY_WIDTH / 2.0) / maxLonDiffMiles;
  float yScale = (DISPLAY_HEIGHT / 2.0) / maxLatDiffMiles;
  float scale = min(xScale, yScale); // Use the smaller scale to maintain proportions

  // Draw center lines and circles with scaling based on miles
  float target_2point5_miles = 2.5 * scale;
  float radius_10_miles = 10 * scale; // Radius for 10 miles in pixels
  float radius_50_miles = 50 * scale; // Radius for 50 miles in pixels

  _sprite.setTextColor(TFT_GREY);
  _sprite.setTextDatum(BC_DATUM);
  // Draw the circles representing 10 miles and 50 miles
  _sprite.drawCircle(centerX, centerY, static_cast<int>(radius_10_miles), TFT_DARKGREY); // Circle for 10 miles
  _sprite.drawCircle(centerX, centerY, static_cast<int>(radius_50_miles), TFT_DARKGREY); // Circle for 50 miles


  _sprite.drawString("10", centerX , centerY + static_cast<int>(radius_10_miles));
  _sprite.drawString("50", centerX , centerY + static_cast<int>(radius_50_miles));


  _sprite.drawLine(centerX - static_cast<int>(target_2point5_miles), centerY, centerX + static_cast<int>(target_2point5_miles), centerY, CENTER_COLOR); // Horizontal line
  _sprite.drawLine(centerX, centerY - static_cast<int>(target_2point5_miles), centerX, centerY + static_cast<int>(target_2point5_miles), CENTER_COLOR); // Vertical line

  // Render each aircraft
  DEBUG_PRINTLN("Rendering aircraft...");
  for (int i = 0; i < _flightStats.totalAircraft; i++)
  {
    AircraftDetailsStruct __aircraft = _flightStats.aircraft[i];
    DEBUG_PRINTLN("Processing aircraft: " + __aircraft.identifier);

    // Calculate relative position in miles
    float __latDiffMiles = (__aircraft.latitude - myLat) * 69.0;
    float __lonDiffMiles = (__aircraft.longitude - myLon) * 69.0 * cos(radians(myLat));

    DEBUG_PRINTLN("Latitude difference (miles): " + String(__latDiffMiles));
    DEBUG_PRINTLN("Longitude difference (miles): " + String(__lonDiffMiles));

    // Scale the differences to fit within the display
    int __x = centerX + static_cast<int>(__lonDiffMiles * scale);
    int __y = centerY - static_cast<int>(__latDiffMiles * scale); // Subtract latitude difference for northern hemisphere

    DEBUG_PRINTLN("Calculated position: x=" + String(__x) + ", y=" + String(__y));

    if (__x >= DISPLAY_WIDTH || __x < 0 || __y >= DISPLAY_HEIGHT || __y < 0)
    {
      DEBUG_PRINTLN("Aircraft is outside display bounds, skipping...");
      continue; // Skip if the aircraft is outside the display bounds
    }

    DEBUG_PRINTLN("Getting color for aircraft");
    uint16_t __aircraftColor = getAircraftColorByAltitude(_sprite, __aircraft.altitude); // Get color based on altitude
    
    DEBUG_PRINTLN("Generated color for aircraft: " + String(__aircraftColor, HEX));

    _sprite.fillCircle(__x, __y, 8, __aircraftColor);
    DEBUG_PRINTLN("Aircraft rendered at position: x=" + String(__x) + ", y=" + String(__y));
    
    int __lineLength = 25;
    
    // Draw heading line
    float headingRadians = radians(__aircraft.heading);
    int lineX = __x + static_cast<int>(__lineLength * sin(headingRadians));
    int lineY = __y - static_cast<int>(__lineLength * cos(headingRadians));
    _sprite.drawLine(__x, __y, lineX, lineY, __aircraftColor);

    if (__aircraft.identifierUnknown)
    {
      DEBUG_PRINTLN("Aircraft identifier is unknown, skipping text rendering...");
      _sprite.setTextDatum(TL_DATUM);
      _sprite.setTextColor(TFT_WHITE);
      _sprite.drawString(__aircraft.identifier, __x + 5, __y + 5);
    }
  }
  _sprite.unloadFont();

  DEBUG_PRINTLN("Map rendering complete.");
}

/***************************************************
  MQTT
****************************************************/
void mqttTransmitCustomSubscribe() {}
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  DEBUG_PRINT("Message arrived [");
  DEBUG_PRINT(topic);
  DEBUG_PRINT("] ");
  char message_buff[100];
  int i = 0;
  for (i = 0; i < length; i++)
  {
    message_buff[i] = payload[i];
  }
  message_buff[i] = '\0';
  String __payloadString = String(message_buff);

  DEBUG_PRINTLN(__payloadString);

  String __incomingTopic = String(topic);

  _lastMQTTMessage = __incomingTopic + " " + __payloadString;

  if (__incomingTopic == "cmnd/" + String(MQTT_CLIENTNAME) + "/reset")
  {
    DEBUG_PRINTLN("Resetting ESP");
    ESP.restart();
  }
  if (__incomingTopic == "cmnd/" + String(MQTT_CLIENTNAME) + "/info")
  {
    mqttTransmitInitStat();
  }
  if (__incomingTopic == "cmnd/mcmddevices/brightness")
  {
    _brightness = __payloadString.toInt();
    DEBUG_PRINTLN("Setting Brightness to: " + String(_brightness));
    setBrightness(_brightness);
  }

  if (__incomingTopic == "cmnd/mcmddevices/brightnesspercentage")
  {
    _brightness = __payloadString.toInt();
    _brightness = map(_brightness, 0, 100, 0, 255);
    DEBUG_PRINTLN("Setting Brightness to: " + String(_brightness));
    setBrightness(_brightness);
  }

  

}
void mqttCustomSubscribe() {}
void mqttTransmitCustomStat() {}
/***************************************************
  Setup
****************************************************/

void initDisplay()
{
  DEBUG_PRINTLN("Initialising Display");

  
  int __amoledRetryCount = 0;
  while (!_amoled.begin())
  {
    DEBUG_PRINTLN("There is a problem with the device!~");
    delay(1000);
    __amoledRetryCount++;
    if (__amoledRetryCount >= 3)
    {
      DEBUG_PRINTLN("Restarting device after 3 failed attempts...");
      ESP.restart();
    }
  }

  if (_configFlipSreen == 999)
    _configFlipSreen = 1;

  DEBUG_PRINTLN("Screen Rotation = " + String(_configFlipSreen));
  //_display.setRotation(_configFlipSreen);
  //_amoled.setRotation(_configFlipSreen);

  _mainSprite.setSwapBytes(true);
  _amoled.setBrightness(_brightnesses[_selectedBrightness]);

  _mainSprite.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  _mainSprite.setTextColor(TFT_WHITE, TFT_BLACK);

  _overviewStatSprite.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  _overviewStatSprite.setTextColor(TFT_WHITE, TFT_BLACK);

  _topStatSprite.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  _topStatSprite.setTextColor(TFT_WHITE, TFT_BLACK);

  _mapSprite.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  _mapSprite.setTextColor(TFT_WHITE, TFT_BLACK);

  for (byte i = 0; i < MAXRENDER_EMERGENCIES; i++)
  {
    _emergencySprite[i].createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  }

  clear_Display();

  _displayInit = true;
}

void setupWebServer()
{
  DEBUG_PRINTLN("Handling Web Request...");

  _httpServer.on("/", []()
                 {
					   String __infoStr = "<html><head>"+style;
             __infoStr += "<script>  ";
             __infoStr += "function checkFlipped() {      document.getElementById('flipscreen').value=document.getElementById('flipscreenHidden').checked;  }";
             __infoStr += "function submitForm() { checkFlipped();    document.getElementById('myForm').submit(); }";
             __infoStr +="</script>";
             __infoStr += "</head>";
					   __infoStr += "<div align=left><H1><i>" + String(MQTT_CLIENTNAME) + "</i></H1>";
             __infoStr += loginIndex+loginIndex2;

					   __infoStr += "<hr class='new5'>";
             __infoStr += "<form action='/set' id='myForm'>";
             
             
             __infoStr += "<input for='station' data-lpignore='true' name='station' type='text' value='"+_locationCode+"' width=40%><br>";
					   __infoStr += "Vertically Flip Screen:&nbsp;&nbsp;<input id='flipscreenHidden' onclick='checkFlipped()' data-lpignore='true' name='flipscreenHidden' type='checkbox' value='true' width=20% ";
             __infoStr +=  String(_configFlipSreen==3?"checked":"")+"><input type='hidden' name='flipscreen' id='flipscreen' value='false' /><br>";

            __infoStr += "Screen brightness:&nbsp;&nbsp;";
            __infoStr += "<select id='brightness' name='brightness'>";
            for (int i = 0; i < 5; i++)
            {
                __infoStr += "<option value='"+String(_brightnesses[i])+"'"+ (_selectedBrightness==i?"selected='selected'":"") +">"+String(map(_brightnesses[i], 0, 255, 0, 100))+"%</option>";
            }
            
            __infoStr += "</select><br>";


             __infoStr += "<input type='submit' class='btn' value='Save setting(s)'>";
             __infoStr += "</form>";


					   __infoStr += "<hr  class='new5'>";
             for (byte i = 0; i < DEBUGBUFFERLENGTH; i++)
             {
                __infoStr += _debugBuffer[i] + "\n<br>";
             }           
					   __infoStr += "<hr class='new5'>Connected to: " + String(SSID) + " (" + _rssiQualityPercentage + "%)<br>";
					   __infoStr += "Last Message Received:  <i>" + _lastMQTTMessage;
					   __infoStr += "</i><br>Last Message Published: <i>" + _lastPublishedMQTTMessage;

					   __infoStr += "</i><br><hr  class='new5'>IP Address: " + IpAddress2String(WiFi.localIP());
					   __infoStr += "<br>MAC Address: " + WiFi.macAddress();
					   __infoStr += "<br>" + String(MQTT_CLIENTNAME) + " - Firmware version: <b>" + String(MCMDVERSION,1);					   
					   __infoStr += "</b></div>";

					   String __retStr = __infoStr+"</html>";

					   _httpServer.sendHeader("Connection", "close");
					   _httpServer.send(200, "text/html", __retStr); });

  _httpServer.on("/serverIndex", HTTP_GET, []()
                 {
					   _httpServer.sendHeader("Connection", "close");
					   _httpServer.send(200, "text/html", serverIndex); });

  _httpServer.on("/reset", []()
                 {
					   String _webClientReturnString = "Resetting device";
					   _httpServer.send(200, "text/plain", _webClientReturnString);
					   ESP.restart();
					   delay(1000); });
  _httpServer.on("/resetSettings", []()
                 {
                   String _webClientReturnString = "Resetting Settings";
                   _httpServer.send(200, "text/plain", _webClientReturnString);

                   if (SPIFFS.exists("/config.ini"))
                   {
                     DEBUG_PRINTLN("Removing Configuration files from SPIFFS");
                     SPIFFS.remove("/config.ini");
                   } });

  _httpServer.on("/defaults", []()
                 {
                    String _webClientReturnString = "Resetting device to defaults";
                    _httpServer.send(200, "text/plain", _webClientReturnString); });

  /*handling uploading firmware file */
  _httpServer.on(
      "/update", HTTP_POST, []()
      {
			_httpServer.sendHeader("Connection", "close");
			_httpServer.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
			ESP.restart(); },
      []()
      {
        HTTPUpload &upload = _httpServer.upload();
        if (upload.status == UPLOAD_FILE_START)
        {
          DisplayOut("Updating Firmware");
          DEBUG_PRINT("Update: ");
          DEBUG_PRINTLN(upload.filename.c_str());
          if (!Update.begin(UPDATE_SIZE_UNKNOWN))
          { // start with max available size
            Update.printError(Serial);
          }
        }
        else if (upload.status == UPLOAD_FILE_WRITE)
        {
          //_updatingFirmware = true;
          /* flashing firmware to ESP*/
          if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
          {
            Update.printError(Serial);
          }
        }
        else if (upload.status == UPLOAD_FILE_END)
        {
          if (Update.end(true))
          { // true to set the size to the current progress
            DEBUG_PRINTLN("Update Success:" + String(upload.totalSize) + "\nRebooting...\n");
            //_updatingFirmware = false;
          }
          else
          {
            Update.printError(Serial);
          }
        }
      });

  _httpServer.on("/set", HTTP_GET, []()
                 {
			String __retMessage = "";
			String __val = "";
			bool __update = false;

			for (uint8_t i = 0; i < _httpServer.args(); i++) {
				__val = _httpServer.arg(i);
				String __key = _httpServer.argName(i);
				__update = parseConfigValue(__key, __val);
				__retMessage += " " + _httpServer.argName(i) + ": " + _httpServer.arg(i) + (__update ? " set." : " not set.") + "\n";
			}
			_httpServer.send(200, "text/plain", __retMessage);

			if (__update) {
				saveConfigValuesSPIFFS();
			} });

  _httpServer.onNotFound(handleSendToRoot);

  _httpServer.begin();

  DEBUG_PRINTLN("Web Request Completed...");
}

void updateLocalTime()
{
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo))
  {
    return;
  }

  adjustBST(&timeinfo);

  strftime(timeHour, 3, "%H", &timeinfo);
  strftime(timeMin, 3, "%M", &timeinfo);
  strftime(timeSec, 3, "%S", &timeinfo);
  /*ú
   strftime(timeWeekDay,10, "%A", &timeinfo);
   strftime(timeday, 3, "%d", &timeinfo);
   strftime(timemonth, 10, "%B", &timeinfo);
   strftime(timeyear, 5, "%Y", &timeinfo);
  */
}

void rebootESP()
{
  DEBUG_PRINTLN("Rebooting ESP");
  delay(250);
  ESP.restart();
}

void toggleSysInfoFrame()
{
  DEBUG_PRINTLN("toggleSysInfoFrame");
  _currentFrame = (_currentFrame == 0 ? 2 : 0);
  _forceRender = true;
}

void rotateBrightness()
{
  DEBUG_PRINTLN("rotateBrightness");
  _selectedBrightness--;
  if (_selectedBrightness < 0)
    _selectedBrightness = 4;

  setBrightness(_brightnesses[_selectedBrightness]);
}

void advanceFrame()
{
  DEBUG_PRINTLN("clickButton2");
  _forceRender = true;
}

void updateFlightStats()
{
  DEBUG_PRINTLN("updateFlightStats");
  if (WiFi.status() == WL_CONNECTED)
  {
    DEBUG_PRINTLN("WiFi Connected");
    DEBUG_PRINTLN("updateFlightStats.fetchFlightData");
    DisplayOut("Fetching flight data");
    String jsonResponse = fetchFlightData(host, path, port);

    DisplayOut("Parsing flight data");
    if (jsonResponse.length() > 0)
    {
      DynamicJsonDocument _flightDetailsJSONDoc(20000);

      //_flightDetailsJSONDoc.clear();       // Clear the document before use
      //_flightDetailsJSONDoc.shrinkToFit(); // Release unused memory
      DeserializationError error = deserializeJson(_flightDetailsJSONDoc, jsonResponse);

      if (!error)
      {
        processFlightData(_flightDetailsJSONDoc);
        printFlightStats();
      }
      else
      {
        DEBUG_PRINTLN("JSON parsing failed! " + String(error.c_str()));
      }
    }
    else
    {
      DEBUG_PRINTLN("Failed to fetch flight data!");
    }
  }
}

void updateADSBDataRenderSprites()
{

  clearSprite();
  //_sprite.pushSprite(0, 0);
  _amoled.pushColors(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, (uint16_t *)_mainSprite.getPointer());

  DisplayOut("Updating ADSB departures");

  updateLocalTime();
  updateFlightStats();

  DisplayOut("Found " + String(_flightStats.totalAircraft) + " aircraft");

  _runDataUpdate = millis();
  int __maxrenderEmergencies = min(_flightStats.emergencyCount, MAXRENDER_EMERGENCIES);
  for (byte i = 0; i < __maxrenderEmergencies; i++)
  {
    DisplayOut("Rendering emergency #" + String(i));
    RenderEmergencySprite(i);
  }

  DisplayOut("Rendering overview screen");
  RenderAircraftToSprite(_overviewStatSprite, _flightStats.aircraft[_flightStats.closestAircraft]);

  DisplayOut("Rendering general statistics");
  RenderGeneralStatsSprite();

  DisplayOut("Rendering Map Sprite");
  renderMap(_mapSprite);
}

void setupWifi()
{

  DisplayOut("Initialising WiFi: 1st AP");
  DisplayOut(_networkConnection ? WIFI_ACCESSPOINT : WIFI_ACCESSPOINT1);

  if (!isWiFiConnected(_mqttClientId))
  {
    flipAPDetails();
    DisplayOut("Initialising WiFi: 2nd AP");
    DisplayOut(_networkConnection ? WIFI_ACCESSPOINT : WIFI_ACCESSPOINT1);

    if (!isWiFiConnected(_mqttClientId))
    {
      DisplayOut("WiFi connection failed");
      DisplayOut("Restarting device");
      rebootESP();
    }
  }
}

void setup()
{
  Serial.begin(115200);

  DEBUG_PRINTLN("Starting...");
  _mqttPostFix = String(random(0xffff), HEX);
  _mqttClientId = MQTT_CLIENTNAME;
  _deviceClientName = MQTT_CLIENTNAME;

  
  initDisplay();
  delay(250); // give the screen time to init

  DisplayOut("Starting ADSBMonitor");
  DisplayOut("----------------------------------");
  
  setupWifi();

  DisplayOut("Web Server config");
  setupWebServer();

  // Start the server
  // DEBUG_PRINT(F("********** Free Heap: "));   DEBUG_PRINTLN(ESP.getFreeHeap());
  DisplayOut("Web Server starting");
  _httpServer.begin();

  // DEBUG_PRINT(F("********** Free Heap: "));   DEBUG_PRINTLN(ESP.getFreeHeap());
  DisplayOut("OTA Firmware Setup");
  setupOTA();

  DisplayOut("Configuring MQTT");
  setupMQTT();
  mqttReconnect(_mqttClientId);
  mqttCustomSubscribe();
  mqttSendInitStat();

  DisplayOut("Setup Time Server");
  checkBST();

  DisplayOut("Attempting MQTT: ");
  DisplayOut(String(MQTT_SERVERADDRESS) + ":1883");
  _mqttClient.setServer(MQTT_SERVERADDRESS, 1883);
  _mqttClient.setCallback(mqttCallback);

  
  DisplayOut("Free Heap Memory: " + String(ESP.getFreeHeap()));

  DisplayOut("DNS Setup");
  if (MDNS.begin(_deviceClientName))
  {
    DisplayOut("Connect to:");
    DisplayOut(" http://" + String(_deviceClientName) + ".local");
    MDNS.addService("http", "tcp", 80);
  }
  else
  {
    DisplayOut("DNS Setup failed");
  }
  _ip = WiFi.localIP().toString();
  DisplayOut("IP:" + _ip);

  DisplayOut("Setting up button 1");
  _button1.attachClick(rotateBrightness);
  _button1.attachDoubleClick(toggleSysInfoFrame);

  DisplayOut("Setting up button 2");
  
  _button2.attachClick(advanceFrame);
  _button2.attachDuringLongPress(rebootESP);
  _button2.attachDoubleClick(updateADSBDataRenderSprites);

  DisplayOut("Opening Filesystem");
  setupSPIFFS();
  loadCustomParamsSPIFFS();

  DisplayOut("Updating local time");
  setupTimeClient();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  updateLocalTime();
  


  DisplayOut("Initialisation complete");
  _forceUpdate = true;
  _forceRender = true;
  _initComplete = true;
}

void loop()
{

  if (_initComplete && !_updatingFirmware)
  {
    _button1.tick();
    _button2.tick();

    _runCurrent = millis(); // sets the counter

    if (_runCurrent - _runDataUpdate >= UPDATE_ADSBS_INTERVAL_MILLISECS || _forceUpdate)
    {
      updateADSBDataRenderSprites();
      _forceUpdate = true;
    }

    if (_runCurrent - _runWiFiConnectionCheck >= UPDATE_WIFICHECK_INTERVAL_MILLISECS)
    {
      isWiFiConnected(); // make sure we're still connected
      _runWiFiConnectionCheck = millis();
    }

    if ((_runCurrent - _runFrame >= UPDATE_UI_FRAME_INTERVAL_MILLISECS) || _forceUpdate || _forceRender)
    {

      if (_forceUpdate)
        DEBUG_PRINTLN("_forceUpdate is True");
      if (_forceRender)
        DEBUG_PRINTLN("_forceRender is True");

      DEBUG_PRINTLN("Current Frame: " + String(_currentFrame));

      if (_flightStats.totalAircraft == 0)
      {
        renderEmpty();
        //_sprite.pushSprite(0, 0);
        _amoled.pushColors(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, (uint16_t *)_mainSprite.getPointer());
      }
      else
      {

        switch (_currentFrame)
        {
        case 0:
          DEBUG_PRINTLN("Pushing System Info sprite to main sprite");
          renderSystemInfo();
          _skipDrawClock = true;
          break;
        case 1:
          DEBUG_PRINTLN("Pushing Overview sprite to main sprite");
          _overviewStatSprite.pushToSprite(&_mainSprite, 0, 0);
          _skipDrawClock = false;
          break;
        case 2:
          DEBUG_PRINTLN("Pushing TopStat sprite to main sprite");
          _topStatSprite.pushToSprite(&_mainSprite, 0, 0);
          _skipDrawClock = true;
          break;
        case 3:
          DEBUG_PRINTLN("Pushing map to main sprite");
          _mapSprite.pushToSprite(&_mainSprite, 0, 0);
          _skipDrawClock = true;
          break;
        default:
          if (_currentFrame > 3 && _currentFrame < 7 && _currentFrame < _flightStats.emergencyCount + 3)
          {
            DEBUG_PRINTLN("Pushing Emergency sprite [" + String(_currentSubFrame - 4) + "] to main sprite");
            _emergencySprite[_currentFrame - 4].pushToSprite(&_mainSprite, 0, 0);
            _skipDrawClock = false;
          }
          break;
        }
      }

      //_sprite.pushSprite(0, 0);
      _amoled.pushColors(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, (uint16_t *)_mainSprite.getPointer());

      _currentFrame++;
      if (_currentFrame > _flightStats.emergencyCount + 3)
      {
        _currentFrame = 1;
      }

      _forceUpdate = false;
      _forceRender = false;
      _runFrame = millis();
    }

    if ((_runCurrent - _runTime >= UPDATE_TIME_INTERVAL_MILLISECS) || _forceDrawClock)
    {
      updateLocalTime();
      if (!_skipDrawClock)
      {

        drawClockBase(CENTERX, DISPLAY_HEIGHT, (_currentFrame == 0 ? TFT_CYAN : TFT_YELLOW));
      }
      _forceDrawClock = false;
      _runTime = millis();
    }

    _amoled.pushColors(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, (uint16_t *)_mainSprite.getPointer());

  

  }

  _mqttClient.loop();
  ArduinoOTA.handle();        /* this function will handle incomming chunk of SW, flash and respond sender */
  _httpServer.handleClient(); //// Check if a client has connected
}
