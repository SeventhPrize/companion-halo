/*
George Lyu
georgemylyu@gmail.com
gml8.iotlamps@gmail.com
02/13/2024

Companion Halo ESP32 Microcontroller
Code for the ESP32 microcontroller unit. Allows person to use the ESP32/lamp to interface with Google Sheets Apps Script. Synchronizes the color of each networked lamp.
NOTE: Connect to WiFiManager via broswer at 192.168.4.1 after connecting to soft access point.
With Google Sheet and associated Google Apps Script web app at https://script.google.com/macros/s/AKfycbzFHKNbergzUW4vbChCNa4Nt4BVOekBWpkDilusBkPwA8ruFMkJ7G2u4-WE241L0MpC/exec
*/

// Libraries
#include <Adafruit_NeoPixel.h>  // Neopixels LED manager
#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager WiFi Configuration Magic (IP 192.168.4.1)
#include <HTTPClient.h>         // HTTP requests
#include <ArduinoJson.h>        // JSON parsing
#include "touchHandler.h"       // Handles touch sensor readings
#include "secrets.h"            // Credentials

// IO pins
const byte IPIN_TOUCH = T4;  // Touch sensor pin
const byte OPIN_PIXEL = 0;   // LED data pin
const byte OPIN_OB_LED = 2;  // Onboard LED signals when WiFi is connected

// Neopixel constants
const byte N_PIXEL = 6;          // Number of LEDs on strip
const byte N_COLOR = 10;         // Number of colors
const byte DEFAULT_BRIGHT = 32;  // Default brightness (out of 255);
const uint16_t HUE_CAP = 65535;  // Maximum hue value in HSV color
const uint8_t BRIGHT_CAP = 255;  // Maximum brightness (value in HSV color)

// Animation constants
const unsigned int LOOP_DELAY = 50;                  // ms delay between each loop() call; analogous to framerate
const unsigned int IDLE_BREATHE_PERIOD = 6000;       // ms period of the breathing animation when idling
const float IDLE_BREATHE_LOBRIGHT = 0.20;            // minimum brightness (proportion of baseBright) when idling
const unsigned int ACTIVE_BREATHE_PERIOD = 2500;     // ms period of the breathing animation when changing brightness
const float ACTIVE_BREATHE_LOBRIGHT = 0.05;          // minimum brightness (proportion of baseBright) when changing brightness
const unsigned int CIRCUIT_PERIOD = 1000;            // ms period of the circuit animation when changing colors/brightness
const unsigned int SEND_RIPPLE_DURATION = 1000;      // ms duration of the ripple animation when sending a color change update
const unsigned int RECEIVE_RIPPLE_DURATION = 3000;   // ms duration of the ripple animation when receiving a new color update
const unsigned int CLICK_TRANSITION_DURATION = 200;  // ms duration of the transitionRotate animation when clicking changing colors

// Touch sensor constants
const unsigned int TOUCH_THRESHOLD = 30;           // threshold for touch sensor. if sensor responds less than threshold, then return a "touch"
const unsigned int HOLD_THRESHOLD = 1000;          // ms duration of continued touch sensor activation before a "click" is considered a "hold"
const unsigned int COLOR_CHANGE_WAIT = 3000;       // ms time to wait in color-change mode before going into brightness-change mode
const unsigned int BRIGHTNESS_CHANGE_WAIT = 7500;  // ms time to wait in brightness-change mode before going into sleep mode

// HTTP constants
const unsigned int WEB_PORTAL_TIMEOUT = 5 * 60;   // Seconds timeout for the WifiManager web portal
const char AP_NAME[] = SECRET_AP_NAME;            // Name of soft access point used by WiFiManager to connect ESP32 to WiFi
const char AP_PASS[] = SECRET_AP_PASS;            // Password of the soft access point used by WiFiManager to connect ESP32 to WiFi
const String MAC = WiFi.macAddress();             // Mac address of this ESP32

const String HTTP_URL = SECRET_HTTP_URL;  // URL of Google Apps Script

// Objects
Adafruit_NeoPixel pixel(N_PIXEL, OPIN_PIXEL, NEO_GRB + NEO_KHZ800);  // Manages the neopixel LED strip
WiFiManager wifiManager;                                             // Manages the WiFi connection
StaticJsonDocument<128> doc;                                         // Parses the JSON data given by HTTP requests to the Google Apps Script
HTTPClient http;                                                     // Manages the HTTP requests

// Lamp pixel variables
byte color = 0;                          // The current held colorstate/colorindex of the lamp. From 0 to N_COLOR-1
uint16_t hueArr[N_PIXEL] = { 0 };        // Array of each pixel's hue (in HSV color)
uint8_t baseBright = DEFAULT_BRIGHT;     // The maximum brightness of the lamp as set by the most recent brightness-change mode
uint8_t currentBright = DEFAULT_BRIGHT;  // The current brightness of the lamp

// Lamp operation variables
TouchHandler touch(IPIN_TOUCH, TOUCH_THRESHOLD);  // Handles touch sensor
byte mode = 1;                                    // represents the current mode of operation. 0 = sleep. 1 = idle. 2 = color-change. 3 = brightness-change.
unsigned long modeStart = 0;                      // millis() of the starttime of the current mode
bool isColorChange = false;                       // whether a color change update needs to be sent because of a color-change in color-change mode

// Color change variables
bool pushingSwitch = false;   // switch that flips whenever a color-change update is queued by user (see doUnhold())
String heldFlickerCode = "";  // LED-core-held flicker code

// Core 0 variables
TaskHandle_t TaskCore0;                        // Handle for the Core 0 task loopCore0
const unsigned int LOOP_DELAY_CORE0 = 1000;    // Loop delay for loopCore0
const unsigned int UPDATE_PERIOD = 5000;       // ms between updates from Google Apps Script about changes in color; ie, the time between each color synchronization
unsigned long lastUpdate = 0;                  // millis() of the last time lamp started checked for color-change updates
bool pushedSwitch = false;                     // switch that flips whenever a color-change update (that has been queued by user) is sent out by HTTP GET request
String receivedFlickerCode = heldFlickerCode;  // WiFi-core-held Flicker code of the last color-change update

// Setup, loop, and initialization

void setup() {
  /*
Initializes lamp.
*/

  // Begin Serial
  delay(3000);
  Serial.begin(9600);
  Serial.println("========== STARTING COMPANION HALO ==========");

  // Initialize output pins
  pinMode(OPIN_PIXEL, OUTPUT);   // Pixel signal pin
  pinMode(OPIN_OB_LED, OUTPUT);  // On-board LED signals when WiFi is connected

  // Begin pixels
  mode = 1;
  pixel.begin();
  pixel.clear();
  fillHuesByInd(color);

  // If touch sensor reads positive, manually open Soft AP Configuration Portal. Configuration Portal is for WiFi trouble-shooting and OTA flashing/firmware update.
  // This blocks the code until the Soft AP is closed. Can close via Configuration Portal at URL "192.168.4.1/exit"
  if (touch.isTouch()) {
    Serial.println("------ Opening Configuration Portal ------");
    pixel.fill(pixel.Color(DEFAULT_BRIGHT, DEFAULT_BRIGHT, DEFAULT_BRIGHT));
    pixel.show();
    Serial.print("Configuration Portal starting because the touch sensor measured positive at startup. Reading: ");
    Serial.println(touch.getReading());
    wifiManager.startConfigPortal(AP_NAME, AP_PASS);
    Serial.println("-------- Resuming lamp operation. --------");
  }

  // Set web portal timeout of the WiFi manager
  wifiManager.setConfigPortalTimeout(WEB_PORTAL_TIMEOUT);

  // Start loopCore0
  createCore0Task();

  // Idle to give some time for createCore0Task warm up
  delay(3000);

  // Set mode start
  modeStart = millis();  // Mark timestamp of start of operation
}

void loop() {
  /*
  Loops to execute lamp operation.  
  */

  // Delay for framerate
  delay(LOOP_DELAY);

  // If in idle mode (mode 1); and color-change pushes are up-to-date; and the held parameters and unequal to the received parameters (a color-change update must have been received),
  // then set the held parameters to the received parameters and do an animation to show the color-change update
  if ((mode == 1) && (pushingSwitch == pushedSwitch) && (heldFlickerCode != receivedFlickerCode)) {
    heldFlickerCode = receivedFlickerCode;
    color = heldFlickerCode.substring(0, heldFlickerCode.indexOf('.')).toInt();
    receiveNewUpdate();
    modeStart = millis();
  }

  // Get touch sensor reading and handle lamp animations
  handleTouch();
  updatePixels();
}

// Core 0: WiFi and HTTP

void createCore0Task() {
  /*
  Creates task pinned to core 0 to execute loopCore0.
  */
  xTaskCreatePinnedToCore(
    loopCore0,    // Task function
    "loopCore0",  // Name of task
    10000,        // Stack size of task. Typical stack size ~5000 for this task function
    NULL,         // Parameter of the task. Expcted Null for this task function
    0,            // Priority of the task
    &TaskCore0,   // Task handle to keep track of created task
    0);           // Pin task to core 0
  Serial.println("Started Core 0 WiFi Task.");
}

void loopCore0(void* args) {
  /*
  Expected NULL input.
  Loops infinitely. Every UPDATE_PERIOD, prints lamp status report to serial monitor, then performs HTTP GET request.
  If there are no color-change updates to be sent to Google Apps Script (pushingSwitch == pushedSwitch), then receives the color-change update parameters from the Google Apps Script.
  If there are color-change updates to be sent to Google Apps Script (pushingSwitch != pushedSwitch), then send this lamp's parameters to the Google Apps Script
  */

  while (true) {
    lastUpdate = millis();

    /*
    If in sleep mode (mode = 0), then turn off the WiFi and turn off onboard LED
    */
    if (mode == 0) {
      WiFi.disconnect();
      digitalWrite(OPIN_OB_LED, LOW);  // onboard LED low while disconnected from WiFi
    }

    /*
    Perform normal operations while mode >= 1
    */
    else {

      /*
      Prints the status of several key variables to serial.
      */
      Serial.println("==========================================");
      Serial.print("MAC                      ");
      Serial.println(MAC);
      Serial.print("WiFi network             ");
      Serial.println(WiFi.SSID());
      Serial.print("Mode                     ");
      Serial.println(mode);
      Serial.print("Color index              ");
      Serial.println(color);
      Serial.print("Current brightness       ");
      Serial.println(currentBright);
      Serial.print("Base brightness          ");
      Serial.println(baseBright);
      Serial.print("Current millis           ");
      Serial.println(millis());
      Serial.print("Mode start time          ");
      Serial.println(modeStart);
      Serial.print("Last push                ");
      Serial.println(touch.getLastPush());
      Serial.print("Last lift                ");
      Serial.println(touch.getLastLift());
      Serial.print("Last HTTP request        ");
      Serial.println(lastUpdate);
      Serial.print("WiFi core0 high water    ");
      Serial.println(uxTaskGetStackHighWaterMark(TaskCore0));
      Serial.print("Free heap                ");
      Serial.println(ESP.getFreeHeap());
      if (pushingSwitch != pushedSwitch) {
        Serial.println("Pending a color change push to webapp.");
      }
      Serial.println("==========================================");

      /*
      Connects the lamp to WiFi using WiFiManager.
      */
      while (WiFi.status() != WL_CONNECTED) {
        digitalWrite(OPIN_OB_LED, LOW);  // onboard LED low while disconnected from WiFi
        Serial.println("----- Attempting to connect to WiFi. -----");
        wifiManager.autoConnect(AP_NAME, AP_PASS);
        Serial.println("-------- Resuming lamp operation. --------");
      }
      digitalWrite(OPIN_OB_LED, HIGH);  // onboard LED high while connected to WiFi

      /*
      Derive the correct URL for HTTP request.
      If no color-change update to be sent, then URL contains this lamp's MAC.
      If there is color-change update to be sent, then URL contains this lamps' Flicker code.
      */
      bool colorChangePushing = false;
      String url = "";
      if (pushingSwitch == pushedSwitch) {
        url = HTTP_URL + "?" + "id=" + MAC;
      } else {
        colorChangePushing = true;
        receivedFlickerCode = heldFlickerCode;
        url = HTTP_URL + "?" + "fc=" + heldFlickerCode;
      }

      /*
      Makes HTTP GET request to Google Apps Script using computed url.
      */

      // HTTP GET
      Serial.println("Making request. . .");
      http.begin(url);
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // The request often sends me through multiple redirects, drastically slowing things down. Idk why.
      unsigned int responseCode = http.GET();
      String str = http.getString();
      http.end();
      Serial.println("HTTP GET received.");
      // Serial.println(str);

      // Check for valid GET
      if (responseCode != 200) {
        Serial.print("Erroneous HTTP response code: ");
        Serial.println(responseCode);
      } else if (str.length() == 0) {
        Serial.println("Error: Received string is empty.");
      }

      // Parse received JSON data
      else {
        char charArr[str.length()];
        strcpy(charArr, str.c_str());
        deserializeJson(doc, charArr);
        receivedFlickerCode = doc["fc"].as<String>();

        // If a color-change update was pushed, then set the switches equal to denote up-to-date color-change updates
        if (colorChangePushing) {
          pushedSwitch = pushingSwitch;
        }
      }
    }

    /*
    Delay until next update. Delays a minimum of 1000ms.
    */
    unsigned long msSinceLastUpdate = millis() - lastUpdate;
    if ((msSinceLastUpdate < UPDATE_PERIOD) && (UPDATE_PERIOD - msSinceLastUpdate > 1000)) {
      delay(UPDATE_PERIOD - msSinceLastUpdate);
    } else {
      delay(1000);
    }
  }
}

String makeFlickerCode() {
  /*
  RETURNS this lamp's current flicker code
  */
  unsigned int millisTag = millis() % 10000;
  return String(color) + "." + String(millisTag) + "." + MAC;
}

// Touch sensor

void handleTouch() {
  /*
  Directs lamp based on current touch status
  */
  switch (touch.isTouchDetailed(HOLD_THRESHOLD)) {
    case 1:
      doClick();
      break;
    case 2:
      doUnclick();
      break;
    case 3:
      doHold();
      break;
    case 4:
      doUnhold();
      break;
    default:
      break;
  }
}

// General helpers for coloring neopixels

void colorPixels() {
  /*
  Sets the neopixels to the hues in hueArr. Sets neopixel brightness to currentBright.
  */
  for (byte ind = 0; ind < N_PIXEL; ind++) {
    pixel.setPixelColor(ind, pixel.ColorHSV(hueArr[ind], 255, currentBright));
  }
}

void fillHuesByUint16(uint16_t hue) {
  /*
  Fills hueArr with the input hue
  INPUT
    hue; uint16_t to fill each hueArr index with    
  */
  for (byte ind = 0; ind < N_PIXEL; ind++) {
    hueArr[ind] = hue;
  }
}

void fillHuesByInd(byte colorInd) {
  /*
  Fills hueArr with the hue associated with the input colorindex/colorstate
  INPUT
    colorInd; byte ranging from 0 to N_COLOR-1 (analogous to colorstate/color)
  */
  fillHuesByUint16(getHue(colorInd));
}

uint16_t getHue(byte colorInd) {
  /*
  Computes the uint16_t hue associated with the given colorindex/colorstate/color
  INPUT
    colorInd; byte ranging from 0 to N_COLOR-1 (analagous to colorstate/color)
  */
  return HUE_CAP / N_COLOR * colorInd;
}

// Lamp interaction

void doClick() {
  /*
  Executes lamp operations on click
  */
  switch (mode) {
    // If mode 0 (sleep), wake up the lamp and put into mode 1 (idle)
    case 0:
      mode = 1;
      modeStart = millis();
      fillHuesByInd(color);  // turn pixels back on to current color
      baseBright = DEFAULT_BRIGHT;
      currentBright = DEFAULT_BRIGHT;
      break;
    // If mode 1 (idle), go to mode 2 (color-change)
    case 1:
      mode = 2;
      modeStart = millis();
      currentBright = baseBright;
      break;
    // If mode 2 (color-change), change the color to the next color
    case 2:
      transitionRotate(CLICK_TRANSITION_DURATION);
      color = (color + 1) % N_COLOR;
      isColorChange = true;
      break;
  }
}

void doUnclick() {
  /*
  Executes lamp operations on unclick
  */
  switch (mode) {
    // If in mode 3 (brightness-change mode), go to mode 1 (idle) and set the maximum brightness based on the brightness selected by brightness-change mode
    case 3:
      mode = 1;
      modeStart = millis();
      fillHuesByInd(color);
      baseBright = currentBright;
      break;
  }
}

void doHold() {
  switch (mode) {
    // If in mode 2 (color-change) and held for sufficient time, go to mode 3 (brightness-change)
    case 2:
      if ((millis() - touch.getLastPush() > HOLD_THRESHOLD) && (modeStart > touch.getLastLift())) {
        mode = 3;
        modeStart = millis();
        baseBright = BRIGHT_CAP;
      }
      break;
    // If in mode 3 (brightness-change) and held for sufficient time, go to mode 0 (sleep)
    case 3:
      if (millis() - modeStart > BRIGHTNESS_CHANGE_WAIT) {
        mode = 0;
        modeStart = millis();
        baseBright = 0;
        currentBright = 0;
        pixel.clear();
        pixel.show();
      }
      break;
  }
}

void doUnhold() {
  switch (mode) {
    // If in mode 2 (color-change) and sufficient time has passed, confirm the color change
    case 2:
      if (millis() - touch.getLastLift() > COLOR_CHANGE_WAIT) {

        // If the color was changed from the initial color, queue a color-change update to be pushed to Google Apps Script
        if (isColorChange) {
          isColorChange = false;
          pushingSwitch = !pushedSwitch;  // pushingSwitch != pushedSwitch denotes that a color-change update needs to be sent to Google Apps Script

          // Set held and received parameters to current parameters
          heldFlickerCode = makeFlickerCode();
          receivedFlickerCode = heldFlickerCode;

          // Perform color confirmation animation
          transitionRipple(getHue(color), HUE_CAP / 2, SEND_RIPPLE_DURATION);
        }

        // Reset colors (get rid the circuiting bright pixel)
        else {
          fillHuesByInd(color);
          colorPixels();
        }
        mode = 1;
        modeStart = millis();
      }
      break;
  }
}

void updatePixels() {
  /*
  Updates the pixel colors, brightness, and animation based on current mode.
  */
  switch (mode) {
    // In idle mode, gently breathe.
    case 1:
      pixelBreathe(IDLE_BREATHE_PERIOD, modeStart, IDLE_BREATHE_LOBRIGHT, true);
      break;
    // In color-change mode, keep constant brightness. Circuit a bright pixel to indicate mode.
    case 2:
      pixelCircuit(CIRCUIT_PERIOD, modeStart);
      break;
    // In brightness-change mode, quickly breathe to give user access to all brightness levels. Circuit a bright pixel to indicate mode.
    case 3:
      pixelCircuit(CIRCUIT_PERIOD, modeStart - HOLD_THRESHOLD);
      pixelBreathe(ACTIVE_BREATHE_PERIOD, modeStart, ACTIVE_BREATHE_LOBRIGHT, false);
      break;
  }
  // If not sleeping, update LEDs to show new color.
  if (mode != 0) {
    colorPixels();
    pixel.show();
  }
}

// Animations

void receiveNewUpdate() {
  /*
  Performs animation associated with receiving a new color-change update
  */
  // Create random pixel colors 4 times
  currentBright = baseBright;
  for (byte itr = 0; itr < 4; itr++) {
    for (byte ind = 0; ind < N_PIXEL; ind++) {
      hueArr[ind] = random(HUE_CAP);
    }
    colorPixels();
    pixel.show();
    delay(250);
  }

  // Ripple to the received color
  transitionRipple(getHue(color), HUE_CAP / 2, RECEIVE_RIPPLE_DURATION);
}

float computeCosine(unsigned int period, unsigned long initTime) {
  /*
  Computes float between 0 and 1 used for calculating progress through periodic animations.
  INPUT
    period; unsigned int representing ms period of the animation
    initTime; unsigned long representing millis() of the starttime of the animation
  RETURNS 0-1 float representing relative progress through the periodic animation
  */
  float angle = 2 * PI * (millis() - initTime) / period;
  return (cos(angle) + 1) / 2;
}

void pixelBreathe(unsigned int period, unsigned long initTime, float minBrightProp, bool startHigh) {
  /*
  Performs "breathing" animeation by periodically dimming brightness bewteen minBrightProp*baseBright and 1*baseBright
  INPUT
    period; ms period of animation
    initTime; millis() starttime of the animation
    minBrightProp; float 0-1 describing how bright the lowest brightness should be relative to baseBright
    startHigh; true if should start at brightest; false if should start at dimmest
  */
  float cosine = computeCosine(period, modeStart);
  if (!startHigh) {
    cosine = 1 - cosine;
  }
  float brightMult = cosine * (1 - minBrightProp) + minBrightProp;
  currentBright = brightMult * baseBright;
}

void pixelCircuit(unsigned int period, unsigned long initTime) {
  /*
  Performs "circuit" animeation by periodically circuiting a bright pixel through the LED strip
  INPUT
    period; ms period of animation
    initTime; millis() starttime of the animation
  */
  fillHuesByInd(color);                                             // fill all pixels to base color to erase the previous bright pixel
  float progress = float((millis() - initTime) % period) / period;  // find current circuit location on the LED strip
  byte brightPixel = floor(progress * N_PIXEL);
  hueArr[brightPixel] = random(HUE_CAP);  // set bright pixel to random color
}

void ripple(uint16_t targetHue, uint16_t maxDiff) {
  /*
  Performs one iteration of the "ripple" animation. Randomly perturbs hue of all pixels by a uniform random value determined by maxDiff centered around targetHue
  INPUT
    targetHue; uint16_t hue to center the pixels at
    maxDiff; uint16_t hue value; maximum hue perturbation 
  */
  fillHuesByUint16(targetHue);  // center pixel hues at targetHue
  // foreach pixel, perturb hue by a uniformly random value from -maxDiff to maxDiff
  for (byte ind = 0; ind < N_PIXEL; ind++) {
    if (random(2)) {
      hueArr[ind] = hueArr[ind] + random(maxDiff);
    } else {
      hueArr[ind] = hueArr[ind] - random(maxDiff);
    }
  }
}

void transitionRipple(uint16_t targetHue, uint16_t maxDiff, unsigned long duration) {
  /*
  Performs an entire "ripple" animation. Repeatedly perturbs hue of all pixels by a uniform random value determined by maxDiff centered around targetHue.
  INPUT
    targetHue; uint16_t hue to center the pixels at
    maxDiff; uint16_t hue value; maximum hue perturbation 
    duration; ms duration of entire animation
  */
  unsigned int iters = duration / LOOP_DELAY;  // number of animation frames

  // Repeatedly call ripple(), each time decreasing the input maxDiff perturbation so that the pixel hues converge to targetHue by the end of the animation
  uint16_t delta = maxDiff / iters;
  for (unsigned int itr = iters; itr > 0; itr--) {
    ripple(targetHue, delta * itr);
    colorPixels();
    pixel.show();
    delay(LOOP_DELAY);
  }

  // Set hues to targetHue
  fillHuesByUint16(targetHue);
  colorPixels();
  pixel.show();
  delay(LOOP_DELAY);
}

void transitionRotate(unsigned long duration) {
  /*
  Performs a "slide"/"wipe"/"dissolve" animation. Smoothly transforms pixel hues from the current colorstate to the next colorstate
  */
  unsigned int iters = duration / LOOP_DELAY;  // number of animation frames
  uint16_t delta = HUE_CAP / N_COLOR / iters;  // amount of hue to change each frame
  uint16_t nowHue = getHue(color);             // current hue given by color/colorstate
  for (unsigned int itr = 0; itr < iters; itr++) {
    fillHuesByUint16(nowHue + delta * itr);  // change hue to linearly progress toward the next colorstate
    colorPixels();
    pixel.show();
    delay(LOOP_DELAY);
  }
  // fill in pixels with the next colorstate's hue
  fillHuesByUint16(getHue((color + 1) % N_COLOR));
  colorPixels();
  pixel.show();
  delay(LOOP_DELAY);
}
