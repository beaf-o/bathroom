#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>

/**
static const uint8_t D0   = 16;
static const uint8_t D1   = 5;
static const uint8_t D2   = 4;
static const uint8_t D3   = 0;
static const uint8_t D4   = 2;
static const uint8_t D5   = 14;
static const uint8_t D6   = 12;
static const uint8_t D7   = 13;
static const uint8_t D8   = 15;
static const uint8_t D9   = 3;
static const uint8_t D10  = 1;
*/

const PROGMEM char* CLIENT_ID = "bathroom-main"; // bathroom-mirror

IPAddress ip(192,168,178,200);

WiFiClient wifiClient;
ESP8266WebServer server(80);
PubSubClient client(wifiClient);

// CREDENTIALS SECTION - START
const PROGMEM int OTA_PORT = 8266;
const PROGMEM char* DEFAULT_PW = "****";
const PROGMEM char* MQTT_SERVER_IP = "****";
const PROGMEM char* MQTT_FALLBACK_SERVER_IP = "****";
const PROGMEM uint16_t MQTT_SERVER_PORT = 1883;
const PROGMEM char* MQTT_USER = "****";
const PROGMEM char* MQTT_PASSWORD = "****";
// CREDENTIALS SECTION - END

const PROGMEM char* ESP_STATE_TOPIC = "home-assistant/esp/bathroom/status";
const PROGMEM char* ESP_IP_TOPIC = "home-assistant/esp/bathroom/ip";

const PROGMEM char* SPOTS_STATE_TOPIC = "home-assistant/bathroom/spots/status";
const PROGMEM char* SPOTS_COMMAND_TOPIC = "home-assistant/bathroom/spots/switch";

const PROGMEM char* STRIPES_STATE_TOPIC = "home-assistant/bathroom/stripes/status";
const PROGMEM char* STRIPES_COMMAND_TOPIC = "home-assistant/bathroom/stripes/set";

const PROGMEM char* PANIC_TOPIC = "home-assistant/panic";

// buffer used to send/receive data with MQTT
const uint8_t MSG_BUFFER_SIZE = 20;
char m_msg_buffer[MSG_BUFFER_SIZE]; 

const int BUFFER_SIZE = JSON_OBJECT_SIZE(10);

// payloads by default (on/off)
const PROGMEM char* LIGHT_ON = "ON";
const PROGMEM char* LIGHT_OFF = "OFF";

const PROGMEM uint8_t DEFAULT_BRIGHTNESS = 50;

// variables used to store the state, the brightness and the color of the light
boolean spotsState = true;
boolean stripesState = true;

boolean panicMode = false;
boolean isInitial = true;

// Maintained state for reporting to HA
byte red = 0;
byte green = 0;
byte blue = 255;
byte brightness = DEFAULT_BRIGHTNESS;

// Real values to write to the LEDs (ex. including brightness and state)
byte realRed = 0;
byte realGreen = 0;
byte realBlue = 0;

// Globals for fade/transitions
bool startFade = false;
unsigned long lastLoop = 0;
int transitionTime = 0;
bool inFade = false;
int loopCount = 0;
int stepR, stepG, stepB;
int redVal, grnVal, bluVal;

// Globals for flash
bool flash = false;
bool startFlash = false;
int flashLength = 0;
unsigned long flashStartTime = 0;
byte flashRed = red;
byte flashGreen = green;
byte flashBlue = blue;
byte flashBrightness = brightness;

// pins used for the rgb led (PWM)
const PROGMEM uint8_t RGB_LIGHT_RED_PIN = D5;
const PROGMEM uint8_t RGB_LIGHT_GREEN_PIN = D6;
const PROGMEM uint8_t RGB_LIGHT_BLUE_PIN = D8;

const PROGMEM uint8_t SPOTS_PIN_1 = D2;
const PROGMEM uint8_t SPOTS_PIN_2 = D3;

const int outputPins[] = {RGB_LIGHT_RED_PIN, RGB_LIGHT_GREEN_PIN, RGB_LIGHT_BLUE_PIN, SPOTS_PIN_1, SPOTS_PIN_2};

void setColor(int inR, int inG, int inB) {
  analogWrite(RGB_LIGHT_RED_PIN, inR);
  analogWrite(RGB_LIGHT_GREEN_PIN, inG);
  analogWrite(RGB_LIGHT_BLUE_PIN, inB);

  Serial.println("Setting LEDs:");
  Serial.print("R: ");
  Serial.print(inR);
  Serial.print(", G: ");
  Serial.print(inG);
  Serial.print(", B: ");
  Serial.println(inB);
}

void toggleSpots() {
  return;
  if (spotsState == true) {
    digitalWrite(SPOTS_PIN_1, HIGH);
    digitalWrite(SPOTS_PIN_2, HIGH);
  } else {
    digitalWrite(SPOTS_PIN_1, LOW);
    digitalWrite(SPOTS_PIN_2, LOW);
  }
}

void publishSpotsState() {
  Serial.print("Publish spots state: ");
  
  if (spotsState) {
    client.publish(SPOTS_STATE_TOPIC, LIGHT_ON, true);
    Serial.println("ON");
  } else {
    client.publish(SPOTS_STATE_TOPIC, LIGHT_OFF, true);
    Serial.println("OFF");
  }
}

/*
  SAMPLE PAYLOAD FOR json light:
    {
      "brightness": 120,
      "color": {
        "r": 255,
        "g": 100,
        "b": 100
      },
      "flash": 2,
      "transition": 5,
      "state": "ON"
    }
  */

// function called when a MQTT message arrived
void callback(char* p_topic, byte* p_payload, unsigned int p_length) {
  // concat the payload into a string
  String payload;
  for (uint8_t i = 0; i < p_length; i++) {
    payload.concat((char)p_payload[i]);
  }

  Serial.print("Handle topic: '");
  Serial.print(p_topic);
  Serial.print("' with payload: '");
  Serial.print(payload);
  Serial.println("'");
  
  if (String(PANIC_TOPIC).equals(p_topic)) {
    handlePanicTopic(payload);
  } else if (String(SPOTS_COMMAND_TOPIC).equals(p_topic)) {
    // test if the payload is equal to "ON" or "OFF"
    if (payload.equals(String(LIGHT_ON)) && spotsState == false) {
      spotsState = true;
      toggleSpots();
    } else if (payload.equals(String(LIGHT_OFF)) && spotsState == true) {
      spotsState = false;
      toggleSpots();
    }
    publishSpotsState();
  } else if (String(STRIPES_COMMAND_TOPIC).equals(p_topic)) {
    char message[p_length + 1];
    for (int i = 0; i < p_length; i++) {
      message[i] = (char)payload[i];
    }
    message[p_length] = '\0';
  
    if (!processJson(message)) {
      return;
    }
  
    if (stripesState) {
      // Update lights
      realRed = map(red, 0, 255, 0, brightness);
      realGreen = map(green, 0, 255, 0, brightness);
      realBlue = map(blue, 0, 255, 0, brightness);
    }
    else {
      realRed = 0;
      realGreen = 0;
      realBlue = 0;
    }
  
    startFade = true;
    inFade = false; // Kill the current fade
  
    publishStripesState();
  }
}

bool processJson(char* message) {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.parseObject(message);

  if (!root.success()) {
    Serial.println("parseObject() failed");
    return false;
  }

  if (root.containsKey("state")) {
    if (strcmp(root["state"], LIGHT_ON) == 0) {
      stripesState = true;
    }
    else if (strcmp(root["state"], LIGHT_OFF) == 0) {
      stripesState = false;
    }
  }

  // If "flash" is included, treat RGB and brightness differently
  if (root.containsKey("flash")) {
    flashLength = (int)root["flash"] * 1000;

    if (root.containsKey("brightness")) {
      flashBrightness = root["brightness"];
    }
    else {
      flashBrightness = brightness;
    }

    if (root.containsKey("color")) {
      flashRed = root["color"]["r"];
      flashGreen = root["color"]["g"];
      flashBlue = root["color"]["b"];
    }
    else {
      flashRed = red;
      flashGreen = green;
      flashBlue = blue;
    }

    flashRed = map(flashRed, 0, 255, 0, flashBrightness);
    flashGreen = map(flashGreen, 0, 255, 0, flashBrightness);
    flashBlue = map(flashBlue, 0, 255, 0, flashBrightness);

    flash = true;
    startFlash = true;
  }
  else { // Not flashing
    flash = false;

    if (root.containsKey("color")) {
      red = root["color"]["r"];
      green = root["color"]["g"];
      blue = root["color"]["b"];
    }

    if (root.containsKey("brightness")) {
      brightness = root["brightness"];
    }

    if (root.containsKey("transition")) {
      transitionTime = root["transition"];
    }
    else {
      transitionTime = 0;
    }
  }

  return true;
}

void handlePanicTopic(String payload) {
  if (payload.equals("ON")) {
    Serial.println("----------------------------------------");
    Serial.println("-----          PANIC MODE          -----");
    Serial.println("----------------------------------------");

    panicMode = true;
  } else if (panicMode != false && payload.equals("OFF")) {
    panicMode = false;
    Serial.println("Set panic mode = false");
  }
}

void publishStripesState() {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();

  root["state"] = (stripesState) ? LIGHT_ON : LIGHT_OFF;
  JsonObject& color = root.createNestedObject("color");
  color["r"] = red;
  color["g"] = green;
  color["b"] = blue;

  root["brightness"] = brightness;

  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  client.publish(STRIPES_STATE_TOPIC, buffer, true);
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("INFO: Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("INFO: connected");
      client.subscribe(PANIC_TOPIC);
      client.subscribe(STRIPES_COMMAND_TOPIC);
      client.subscribe(SPOTS_COMMAND_TOPIC);
    } else {
      Serial.print("ERROR: failed, rc=");
      Serial.println(client.state());
      Serial.println("DEBUG: try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup(void){
  Serial.begin(115200);
  Serial.println("");

  setupButtons();
  setupSpots();
  setupStripes();
  setupWifi();
  setupHttpServer();
  setupMqtt();
  setupOTA();
}

void setupButtons() {
  for (int p = 0; p < 5; p++) {
    int pin = outputPins[p];
    pinMode(pin, OUTPUT);
  }  
}

void setupSpots() {
  Serial.println("Switch on spots by default");
  digitalWrite(SPOTS_PIN_1, HIGH);
  digitalWrite(SPOTS_PIN_2, HIGH);
}

void setupStripes() {
  analogWriteRange(255);
  setColor(0, 0, 255);  
}

void setupWifi() {
  delay(10);
  WiFiManager wifiManager;
  wifiManager.setTimeout(180);

  if (!wifiManager.autoConnect(CLIENT_ID, DEFAULT_PW)) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }
}

void setupHttpServer() {
  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");  
}

void setupMqtt() {
  // init the MQTT connection
  client.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);
  client.setCallback(callback);
}

void setupOTA() {
  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setHostname(CLIENT_ID);
  ArduinoOTA.setPassword(DEFAULT_PW);

  ArduinoOTA.onStart([]() {
    Serial.println("Starting OTA");
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  
  ArduinoOTA.begin();
}

String IpAddress2String(const IPAddress& ipAddress){
  return String(ipAddress[0]) + String(".") +\
    String(ipAddress[1]) + String(".") +\
    String(ipAddress[2]) + String(".") +\
    String(ipAddress[3]); 
}

void loop(void) {
  server.handleClient(); 

  if (!client.connected()) {
    reconnect();
  }

  if (isInitial == true) {
    IPAddress ipAddress = WiFi.localIP();
    String ipString = IpAddress2String(ipAddress);

    client.publish(ESP_STATE_TOPIC, "online", true);
    client.publish(ESP_IP_TOPIC, ipString.c_str(), true);
    
    client.publish(SPOTS_STATE_TOPIC, LIGHT_ON, true);
    //client.publish(SPOTS_COMMAND_TOPIC, LIGHT_ON, true);
  }
  
  client.loop();

  if (panicMode == true) {
    blinkRed();
  }

  handleStripes();
  isInitial = false;
}

void blinkRed() {
  for (int br = 0; br < 100; br++) {

    setColor(255, 0, 0);
    delay(500);
  
    setColor(0, 0, 0);
    delay(500);
  }
}

void handleStripes() {
  if (flash) {
    if (startFlash) {
      startFlash = false;
      flashStartTime = millis();
    }

    if ((millis() - flashStartTime) <= flashLength) {
      if ((millis() - flashStartTime) % 1000 <= 500) {
        setColor(flashRed, flashGreen, flashBlue);
      } else {
        setColor(0, 0, 0);
        // If you'd prefer the flashing to happen "on top of"
        // the current color, uncomment the next line.
        // setColor(realRed, realGreen, realBlue);
      }
    } else {
      flash = false;
      setColor(realRed, realGreen, realBlue);
    }
  }

  if (startFade) {
    // If we don't want to fade, skip it.
    if (transitionTime == 0) {
      setColor(realRed, realGreen, realBlue);

      redVal = realRed;
      grnVal = realGreen;
      bluVal = realBlue;

      startFade = false;
    } else {
      loopCount = 0;
      stepR = calculateStep(redVal, realRed);
      stepG = calculateStep(grnVal, realGreen);
      stepB = calculateStep(bluVal, realBlue);

      inFade = true;
    }
  }

  if (inFade) {
    startFade = false;
    unsigned long now = millis();
    if (now - lastLoop > transitionTime) {
      if (loopCount <= 1020) {
        lastLoop = now;
        
        redVal = calculateVal(stepR, redVal, loopCount);
        grnVal = calculateVal(stepG, grnVal, loopCount);
        bluVal = calculateVal(stepB, bluVal, loopCount);
        
        setColor(redVal, grnVal, bluVal); // Write current values to LED pins

        Serial.print("Loop count: ");
        Serial.println(loopCount);
        loopCount++;
      } else {
        inFade = false;
      }
    }
  }
}

void handleRoot() {
  //Serial.println("Bathroom is occupied.\n");
  server.send(200, "text/plain", "Bathroom is occupied.");  
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i < server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  
  server.send(404, "text/plain", message);
}

// From https://www.arduino.cc/en/Tutorial/ColorCrossfader
/* BELOW THIS LINE IS THE MATH -- YOU SHOULDN'T NEED TO CHANGE THIS FOR THE BASICS
* 
* The program works like this:
* Imagine a crossfade that moves the red LED from 0-10, 
*   the green from 0-5, and the blue from 10 to 7, in
*   ten steps.
*   We'd want to count the 10 steps and increase or 
*   decrease color values in evenly stepped increments.
*   Imagine a + indicates raising a value by 1, and a -
*   equals lowering it. Our 10 step fade would look like:
* 
*   1 2 3 4 5 6 7 8 9 10
* R + + + + + + + + + +
* G   +   +   +   +   +
* B     -     -     -
* 
* The red rises from 0 to 10 in ten steps, the green from 
* 0-5 in 5 steps, and the blue falls from 10 to 7 in three steps.
* 
* In the real program, the color percentages are converted to 
* 0-255 values, and there are 1020 steps (255*4).
* 
* To figure out how big a step there should be between one up- or
* down-tick of one of the LED values, we call calculateStep(), 
* which calculates the absolute gap between the start and end values, 
* and then divides that gap by 1020 to determine the size of the step  
* between adjustments in the value.
*/
int calculateStep(int prevValue, int endValue) {
    int step = endValue - prevValue; // What's the overall gap?
    if (step) {                      // If its non-zero, 
        step = 1020/step;            //   divide by 1020
    }
    
    return step;
}

/* The next function is calculateVal. When the loop value, i,
*  reaches the step size appropriate for one of the
*  colors, it increases or decreases the value of that color by 1. 
*  (R, G, and B are each calculated separately.)
*/
int calculateVal(int step, int val, int i) {
    if ((step) && i % step == 0) { // If step is non-zero and its time to change a value,
        if (step > 0) {              //   increment the value if step is positive...
            val += 1;
        } else if (step < 0) {         //   ...or decrement it if step is negative
            val -= 1;
        }
    }
    
    // Defensive driving: make sure val stays in the range 0-255
    if (val > 255) {
        val = 255;
    } else if (val < 0) {
        val = 0;
    }
    
    return val;
}
