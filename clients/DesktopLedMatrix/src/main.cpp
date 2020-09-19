#define FASTLED_ALLOW_INTERRUPTS 0

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <WebSerial.h>
#include <Ticker.h>
#include <GAsyncMqttClient.cpp>
#include <Arduino_JSON.h>

#include <FastLED.h>
#include <Wire.h>
#include <FastLED_NeoMatrix.h>

extern "C" {
#include <osapi.h>
#include <os_type.h>
}

#include "config.h"
#include "passwd.h"


//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// Variables
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

#define PIN D6
#define LED_MATRIX_WIDTH 64
#define LED_MATRIX_HEIGHT 8
#define NUMMATRIX (LED_MATRIX_WIDTH*LED_MATRIX_HEIGHT)
CRGB matrixleds[NUMMATRIX];
int A[LED_MATRIX_WIDTH][LED_MATRIX_HEIGHT];
int B[LED_MATRIX_WIDTH][LED_MATRIX_HEIGHT];

FastLED_NeoMatrix *matrix = new FastLED_NeoMatrix(
  matrixleds,
  LED_MATRIX_WIDTH,
  LED_MATRIX_HEIGHT, 
  NEO_MATRIX_TOP  + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG);

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

Ticker mqttReconnectTimer;
GAsyncMqttClient mqttClient;
void connectToMqtt();
// JSONVar payloadJson;
double latest_payload[LED_MATRIX_WIDTH];

AsyncWebServer server(80);

unsigned long timestamp = 0;
unsigned long interval = 80; //interval of 80 milliseconds
// Ticker screenUpdateTimer;
void gol(int A[][8], int n, int m);
void audio_vis();
void updateScreen();

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// Setup Wifi
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println("Connected to Wi-Fi.");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}

void setup_wifi() {
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
  connectToWifi();
}


//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// Setup MQTT
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

void connectToMqtt() {
  WebSerial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent) {
  WebSerial.println("Connected to MQTT.");
  WebSerial.print("Session present: ");
  WebSerial.println(sessionPresent);

  mqttClient.publish("connected/device", 2, true, "{\"device_name\": \"device/desktop_led_matrix\"}");
  mqttClient.subscribe("data/service/audio_vis", 0);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  WebSerial.println("Disconnected from MQTT.");

  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  WebSerial.println("Subscribe acknowledged.");
  WebSerial.print("  packetId: ");
  WebSerial.println(packetId);
  WebSerial.print("  qos: ");
  WebSerial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId) {
  WebSerial.println("Unsubscribe acknowledged.");
  WebSerial.print("  packetId: ");
  WebSerial.println(packetId);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  if (len == total) {
    char new_payload[len+1];
    new_payload[len] = '\0';
    strncpy(new_payload, payload, len);

    if (strcmp(topic, "data/service/audio_vis") == 0) {
      JSONVar payloadJson = JSON.parse(new_payload);
      JSONVar fast_bar_values = payloadJson["fast_bar_values"];
      for (int i=0; i<LED_MATRIX_WIDTH; i++) {
        latest_payload[i] = (double) fast_bar_values[i];
      }
      delete payloadJson;
    }
  } else {
    WebSerial.println("Packet loss.");
  }
}

void onMqttPublish(uint16_t packetId) {
  WebSerial.println("Publish acknowledged.");
  WebSerial.print("  packetId: ");
  WebSerial.println(packetId);
}

void setup_mqtt() {
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onGMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCredentials(MQTT_USERNAME, MQTT_PASSWORD);
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// Setup Web Server
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

void handleOn(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "turing it on!");
  digitalWrite(LED_BUILTIN, LOW);
}

void handleOff(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "turing it off!");
  digitalWrite(LED_BUILTIN, HIGH);
}

void handleNotFound(AsyncWebServerRequest *request){
  digitalWrite(LED_BUILTIN, LOW);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += request->url();
  message += "\nMethod: ";
  message += (request->method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += request->args();
  message += "\n";
  for (uint8_t i=0; i<request->args(); i++){
    message += " " + request->argName(i) + ": " + request->arg(i) + "\n";
  }
  request->send(404, "text/plain", message);
  digitalWrite(LED_BUILTIN, HIGH);
}

void recvMsg(uint8_t *data, size_t len){
  WebSerial.println("Received Data...");
  String d = "";
  for(uint i=0; i < len; i++){
    d += char(data[i]);
  }

  if (d == "-restart") {
    WebSerial.println("***********Restarting Device***********");
    ESP.restart();
  }

  WebSerial.println(d);
}

void setup_server(void) {

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(LED_BUILTIN, LOW);
    request->send(200, "text/plain", "hello from esp8266!");
    delay(1000);
    digitalWrite(LED_BUILTIN, HIGH);
  });

  server.on("/hello", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(LED_BUILTIN, LOW);
    request->send(200, "text/plain", "hello from esp8266!");
    delay(1000);
    digitalWrite(LED_BUILTIN, HIGH);
  });

  server.on("/on", HTTP_GET, handleOn);
  server.on("/off", HTTP_GET, handleOff);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// Setup WebSerial
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

void setup_webserial(void) {
  WebSerial.begin(&server);
  WebSerial.msgCallback(recvMsg);
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// Setup OTA
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

void setup_ota(void) {
  AsyncElegantOTA.begin(&server);
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// Setup LEDs
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

void setup_led(void) {
  FastLED.addLeds<NEOPIXEL,PIN>(matrixleds, NUMMATRIX);
  matrix->setTextWrap(false);
  matrix->setBrightness(5);
  matrix->begin();
  // matrix->setTextColor(colors[0]);
  // screenUpdateTimer.attach_scheduled(interval, updateScreen);
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// Setup Everything
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

void setup(void) {
  Serial.begin(9800);
  Serial.println("Booting");
  pinMode(LED_BUILTIN, OUTPUT);

  setup_ota();
  setup_webserial();
  setup_server();
  setup_mqtt();
  setup_led();
  setup_wifi();

  digitalWrite(LED_BUILTIN, LOW);
  delay(1000);
  digitalWrite(LED_BUILTIN, HIGH);

  for(int i = 0; i < LED_MATRIX_HEIGHT; i += 1) {
    for(int j = 0; j < LED_MATRIX_WIDTH; j += 1) {
      A[i][j] = 0;
    }
  }
  A[3][3] = 1;
  A[4][4] = 1;
  A[5][3] = 1;
  A[5][4] = 1;
  A[4][5] = 1;
}

inline int getmat(int A[][8], int x, int y, int n, int m) {
  if (x == -1) x = n - 1;
  if (y == -1) y = m - 1;
  if (x == n) x = 0;
  if (y == m) y = 0;
  return A[x][y];
}

void gol(int A[][8], int n, int m) {
  int huemax = 0;
  for(int i = 0; i < n; i += 1) {
    for(int j = 0; j < m; j += 1) {
      B[i][j] = -A[i][j];
      for(int ii = -1; ii <= 1; ii += 1) {
        for(int jj = -1; jj <= 1; jj += 1) {
          B[i][j] += getmat(A, i + ii, j + jj, n, m);
        }
      }
      huemax += B[i][j];
    }
  }
  for(int i = 0; i < n; i += 1) {
    for(int j = 0; j < m; j += 1) {
      if (A[i][j]) {
        if (B[i][j] < 2) {
          A[i][j] = 0;
          // WebSerial.println("Drawing at : " + (String)i + "," + (String)j);
          matrix->drawPixel(i, j, matrix->Color24to16(CRGB::Black));
          // WebSerial.println("Finished drawing at : " + (String)i + "," + (String)j);
        }
        else if (B[i][j] > 3) {
          A[i][j] = 0;
          // WebSerial.println("Drawing at : " + (String)i + "," + (String)j);
          matrix->drawPixel(i, j, matrix->Color24to16(CRGB::Black));
          // WebSerial.println("Finished drawing at : " + (String)i + "," + (String)j);
        } else {
          // WebSerial.println("Drawing at : " + (String)i + "," + (String)j);
          matrix->drawPixel(i, j, matrix->Color24to16(CRGB::Wheat));
          // WebSerial.println("Finished drawing at : " + (String)i + "," + (String)j);
        }
      } else {
        if (B[i][j] == 3) {
          A[i][j] = 1;
          // WebSerial.println("Drawing at : " + (String)i + "," + (String)j);
          matrix->drawPixel(i, j, matrix->Color24to16(CRGB::Wheat));
          // WebSerial.println("Finished drawing at : " + (String)i + "," + (String)j);
        } else {
          // WebSerial.println("Drawing at : " + (String)i + "," + (String)j);
          matrix->drawPixel(i, j, matrix->Color24to16(CRGB::Black));
          // WebSerial.println("Finished drawing at : " + (String)i + "," + (String)j);
        }
      }
    }
  }
}

void audio_vis() {
  for (int i=0; i<LED_MATRIX_WIDTH; i++) {
    double bar_height = latest_payload[i] * LED_MATRIX_HEIGHT;
    for (int j=0; j<LED_MATRIX_HEIGHT; j++) {
      if (LED_MATRIX_HEIGHT - j <= bar_height + 1) {
        matrix->drawPixel(i, j, matrix->Color24to16(CRGB::Wheat));
      } else {
        matrix->drawPixel(i, j, matrix->Color24to16(CRGB::Black));
      }
    }
  }
}

const char *MODE = "audio_vis";

void updateScreen() {
  if (strcmp(MODE, "gol") == 0) {
    gol(A, LED_MATRIX_WIDTH, LED_MATRIX_HEIGHT);
  } else if (strcmp(MODE, "audio_vis") == 0) {
    audio_vis();
  }
  
  matrix->show();
}

void loop(void) {
  AsyncElegantOTA.loop();
  unsigned long current_time=millis();
  if(current_time - timestamp > interval) {
    timestamp = current_time;
    updateScreen();
  }
}