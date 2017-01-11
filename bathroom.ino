#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>

IPAddress ip(192,168,178,200);
IPAddress dns(192,168,178,5);
IPAddress gateway(192,168,178,5);
const char* ssid = "404 Network not found";
const char* password = "******";

WiFiClient wifiClient;
ESP8266WebServer server(80);
PubSubClient client(wifiClient);

// MQTT: ID, server IP, port, username and password
const PROGMEM char* MQTT_CLIENT_ID = "beaf-o";
const PROGMEM char* MQTT_SERVER_IP = "iot.eclipse.org";
const PROGMEM uint16_t MQTT_SERVER_PORT = 1883;
const PROGMEM char* MQTT_USER = "beaf-o";
const PROGMEM char* MQTT_PASSWORD = "beaf-o";

// MQTT: topics
// state
const PROGMEM char* MQTT_SPOTS_STATE_TOPIC = "home-assistant/bathroom/spots/light/status";
const PROGMEM char* MQTT_SPOTS_COMMAND_TOPIC = "home-assistant/bathroom/spots/light/switch";

const PROGMEM char* MQTT_LIGHT_STATE_TOPIC = "home-assistant/bathroom/stripes/light/status";
const PROGMEM char* MQTT_LIGHT_COMMAND_TOPIC = "home-assistant/bathroom/stripes/light/switch";

// brightness
const PROGMEM char* MQTT_LIGHT_BRIGHTNESS_STATE_TOPIC = "home-assistant/bathroom/stripes/brightness/status";
const PROGMEM char* MQTT_LIGHT_BRIGHTNESS_COMMAND_TOPIC = "home-assistant/bathroom/stripes/brightness/set";

// colors (rgb)
const PROGMEM char* MQTT_LIGHT_RGB_STATE_TOPIC = "home-assistant/bathroom/stripes/rgb/status";
const PROGMEM char* MQTT_LIGHT_RGB_COMMAND_TOPIC = "home-assistant/bathroom/stripes/rgb/set";

// payloads by default (on/off)
const PROGMEM char* LIGHT_ON = "on";
const PROGMEM char* LIGHT_OFF = "off";

// variables used to store the state, the brightness and the color of the light
boolean m_rgb_state = false;
uint8_t m_rgb_brightness = 100;
uint8_t m_rgb_red = 255;
uint8_t m_rgb_green = 255;
uint8_t m_rgb_blue = 255;

// pins used for the rgb led (PWM)
const PROGMEM uint8_t RGB_LIGHT_RED_PIN = 1;
const PROGMEM uint8_t RGB_LIGHT_GREEN_PIN = 2;
const PROGMEM uint8_t RGB_LIGHT_BLUE_PIN = 5;

const PROGMEM uint8_t SPOTS_PIN_1 = 7;
const PROGMEM uint8_t SPOTS_PIN_2 = 8;

const int outputPins[] = {RGB_LIGHT_RED_PIN, RGB_LIGHT_GREEN_PIN, RGB_LIGHT_BLUE_PIN, SPOTS_PIN_1, SPOTS_PIN_2};

// buffer used to send/receive data with MQTT
const uint8_t MSG_BUFFER_SIZE = 20;
char m_msg_buffer[MSG_BUFFER_SIZE]; 

// function called to adapt the brightness and the color of the led
void setColor(uint8_t p_red, uint8_t p_green, uint8_t p_blue) {
  // convert the brightness in % (0-100%) into a digital value (0-255)
  uint8_t brightness = map(m_rgb_brightness, 0, 100, 0, 1023);

  analogWrite(RGB_LIGHT_RED_PIN, map(p_red, 0, 255, 0, brightness));
  analogWrite(RGB_LIGHT_GREEN_PIN, map(p_green, 0, 255, 0, brightness));
  analogWrite(RGB_LIGHT_BLUE_PIN, map(p_blue, 0, 255, 0, brightness));
}

// function called to publish the state of the led (on/off)
void publishRGBState() {
  if (m_rgb_state) {
    client.publish(MQTT_LIGHT_STATE_TOPIC, LIGHT_ON, true);
  } else {
    client.publish(MQTT_LIGHT_STATE_TOPIC, LIGHT_OFF, true);
  }
}

// function called to publish the brightness of the led (0-100)
void publishRGBBrightness() {
  snprintf(m_msg_buffer, MSG_BUFFER_SIZE, "%d", m_rgb_brightness);
  client.publish(MQTT_LIGHT_BRIGHTNESS_STATE_TOPIC, m_msg_buffer, true);
}

// function called to publish the colors of the led (xx(x),xx(x),xx(x))
void publishRGBColor() {
  snprintf(m_msg_buffer, MSG_BUFFER_SIZE, "%d,%d,%d", m_rgb_red, m_rgb_green, m_rgb_blue);
  client.publish(MQTT_LIGHT_RGB_STATE_TOPIC, m_msg_buffer, true);
}

// function called when a MQTT message arrived
void callback(char* p_topic, byte* p_payload, unsigned int p_length) {
  // concat the payload into a string
  String payload;
  for (uint8_t i = 0; i < p_length; i++) {
    payload.concat((char)p_payload[i]);
  }
  
  // handle message topic
  if (String(MQTT_LIGHT_COMMAND_TOPIC).equals(p_topic)) {
    // test if the payload is equal to "ON" or "OFF"
    if (payload.equals(String(LIGHT_ON))) {
      if (m_rgb_state != true) {
        m_rgb_state = true;
        setColor(m_rgb_red, m_rgb_green, m_rgb_blue);
        publishRGBState();
      }
    } else if (payload.equals(String(LIGHT_OFF))) {
      if (m_rgb_state != false) {
        m_rgb_state = false;
        setColor(0, 0, 0);
        publishRGBState();
      }
    }
  } else if (String(MQTT_LIGHT_BRIGHTNESS_COMMAND_TOPIC).equals(p_topic)) {
    uint8_t brightness = payload.toInt();
    if (brightness < 0 || brightness > 100) {
      // do nothing...
      return;
    } else {
      m_rgb_brightness = brightness;
      setColor(m_rgb_red, m_rgb_green, m_rgb_blue);
      publishRGBBrightness();
    }
  } else if (String(MQTT_LIGHT_RGB_COMMAND_TOPIC).equals(p_topic)) {
    // get the position of the first and second commas
    uint8_t firstIndex = payload.indexOf(',');
    uint8_t lastIndex = payload.lastIndexOf(',');
    
    uint8_t rgb_red = payload.substring(0, firstIndex).toInt();
    if (rgb_red < 0 || rgb_red > 255) {
      return;
    } else {
      m_rgb_red = rgb_red;
    }
    
    uint8_t rgb_green = payload.substring(firstIndex + 1, lastIndex).toInt();
    if (rgb_green < 0 || rgb_green > 255) {
      return;
    } else {
      m_rgb_green = rgb_green;
    }
    
    uint8_t rgb_blue = payload.substring(lastIndex + 1).toInt();
    if (rgb_blue < 0 || rgb_blue > 255) {
      return;
    } else {
      m_rgb_blue = rgb_blue;
    }
    
    setColor(m_rgb_red, m_rgb_green, m_rgb_blue);
    publishRGBColor();
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("INFO: Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("INFO: connected");
      
      // Once connected, publish an announcement...
      // publish the initial values
      publishRGBState();
      publishRGBBrightness();
      publishRGBColor();

      // ... and resubscribe
      client.subscribe(MQTT_LIGHT_COMMAND_TOPIC);
      client.subscribe(MQTT_LIGHT_BRIGHTNESS_COMMAND_TOPIC);
      client.subscribe(MQTT_LIGHT_RGB_COMMAND_TOPIC);
      client.subscribe(MQTT_SPOTS_COMMAND_TOPIC);
    } else {
      Serial.print("ERROR: failed, rc=");
      Serial.print(client.state());
      Serial.println("DEBUG: try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup(void){
  Serial.begin(115200);

  setupButtons();
  setupStripes();
  setupWifi();
  setupHttpServer();
  setupMqtt();
}

void setupButtons() {
  for (int p = 0; p < sizeof(outputPins); p++) {
    int pin = outputPins[p];
    pinMode(pin, OUTPUT);
    digitalWrite(pin, 0);
  }  
}

void setupStripes() {
  analogWriteRange(255);
  setColor(0, 0, 0);  
}

void setupWifi() {
  WiFi.config(ip, dns, gateway);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());  
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

void loop(void){
  server.handleClient(); 

  if (!client.connected()) {
    reconnect();
  }
  
  client.loop();
}

void handleRoot() {
  Serial.println("Bathroom is occupied.\n");
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
