#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#include <DNSServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <FS.h>

#include <ResetDetect.h>
#include "EEPROMAnything.h"

#include <FastLED.h>
#include "NewServo.h"

const String SSID = "ESP8266";

#define MSFREQ    25


#define LFT_PIN   D5
#define RGT_PIN   D6

#define LFT 0
#define RGT 1

#define JOYMSG    0
#define ADJMSG    2
#define MAXTIMEOUT 10

#define HTTP_PORT 80
#define DNS_PORT  53
#define WS_PORT   2205

// Set up the servos
#define NUMSERVOS 2
newServo servos[NUMSERVOS] = {newServo(LFT_PIN, 0),
                              newServo(RGT_PIN, 0)};

DNSServer dnsServer;
ESP8266WebServer webServer(HTTP_PORT);
WebSocketsServer webSocket = WebSocketsServer(WS_PORT);

byte timeout = MAXTIMEOUT;
bool oldConnected = true;
bool connected = false;


struct config_t{
  char ssid[17];
  char password[17] = "password";
  limits_t limits[NUMSERVOS];
}  config;


void setup() {
  Serial.begin(115200); Serial.printf("\n%s\n", SSID.c_str());
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  sprintf(config.ssid, "%s %06X", SSID.c_str(), ESP.getChipId());
  
  loadEEPROM();

  Serial.printf("SSID: '%s'\n", config.ssid);  
  Serial.printf("Password: '%s'\n", config.password);

  // Set up the servos with the values retreived from the EEPROM
  for(byte id = 0; id < NUMSERVOS; id++) {
    servos[id].limits = config.limits[id];
    Serial.printf("Servos:%d, min:%d, zero:%d, max:%d\n", id, servos[id].limits.min, servos[id].limits.zero, servos[id].limits.max);
  }
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(config.ssid, config.password, 1, false, 1);

  SPIFFS.begin();  

  // Create a simple homepage
  webServer.serveStatic("/", SPIFFS, "/index.html");
  webServer.serveStatic("/servo", SPIFFS, "/servo.html");

  // Redirect all unknown traffic back to the homepage
  webServer.onNotFound([](){
    webServer.sendHeader("Location", "http://" + SSID + ".local/");
    webServer.send(303);
  });

  MDNS.begin(SSID.c_str());
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  
  webServer.begin();

  webSocket.onEvent(webSocketEvent);
  webSocket.begin();
}

void loadEEPROM() {
  bool updateEEPROM = false;

  // Read the servo limits from the EEPROM
  EEPROM.begin(sizeof(config.limits));
  EEPROM_readAnything(0, config.limits);

  // Check each servo to see if the values read from EEPROM are valid
  for(byte i=0; i< NUMSERVOS; i++) {
    // If the EEPROM is invalid create a new set of limits from the default
    if(!config.limits[i].checkLimits()) {  
      config.limits[i] = limits_t();   
      updateEEPROM = true;
    }
  }

  // If any of the limits were invalid then reflash the EEPROM with the new values
  if(updateEEPROM) {
    Serial.printf("update EEPROM\n");
    EEPROM_writeAnything(0, config.limits);    
    EEPROM.commit();
  }
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();  
  webSocket.loop();

  EVERY_N_MILLISECONDS(MSFREQ) { 
    for(byte i=0; i< NUMSERVOS; i++) 
      servos[i].update();
  }

  // Keep a counter to check for communication timeouts
  EVERY_N_MILLISECONDS(500) { 
    connected = (timeout < MAXTIMEOUT);
    digitalWrite(LED_BUILTIN, !connected);

    // Update items when the connected state changes
    if(oldConnected != connected) {
      for(byte i=0; i< NUMSERVOS; i++) 
        servos[i].connect(connected);  
    }
    
    if(connected) timeout += 1;    
    oldConnected = connected;
  }
}


void webSocketEvent(unsigned char clientNo, WStype_t type, unsigned char *data, unsigned int len)
{
  uint8_t msgtype, id;
  int8_t x, y;
  uint16_t ms;
  String message;
  int16_t leftWheel, rightWheel;
  
  StaticJsonBuffer<200> jsonBuffer;
  
  switch(type)
  {
  case WStype_CONNECTED:
    break;

  case WStype_DISCONNECTED:
    break;
    
  case WStype_BIN: 
  
//    message = "Packet: ";   
//    for(int i = 0; i < len; i += 1) 
//      message += String(data[i], HEX) + ",";
//    Serial.println(message);
  
  
    msgtype = data[0];
    
    switch (msgtype) {
      case JOYMSG:
        x = data[1];
        y = data[2];

        leftWheel =  (y*7.0) + (x*7.0);
        rightWheel = (y*7.0) - (x*7.0);
       
        //Serial.printf("x:%d, y:%d, l:%d, r:%d\n", x, y, leftWheel, rightWheel);

        servos[LFT].targetValue = - leftWheel;
        servos[RGT].targetValue = + rightWheel;
        break;

      case ADJMSG:
        id = data[1];
        ms = (uint16_t)(data[2] << 8) + (uint16_t)data[3];
        //Serial.printf("id: %d, ms: %d\n", id, ms);
        servos[id].writeMicroseconds(ms);
        break;
    }

    timeout = 0;
    break;

  case WStype_TEXT:
    Serial.printf("<-%s\n", data);
    JsonObject& root = jsonBuffer.parseObject(data);
    
    if (root.success()) {
      const char* msgtype = root["type"];
 
      if(strcmp(msgtype, "getinfo") == 0) 
        sendServoData();
        
      if(strcmp(msgtype, "setinfo") == 0) {
        uint8_t id = root["id"];
        config.limits[id].min = servos[id].limits.min = root["min"];
        config.limits[id].zero = servos[id].limits.zero = root["zero"];
        config.limits[id].max = servos[id].limits.max = root["max"];

        EEPROM_writeAnything(0,  config.limits);    
        EEPROM.commit();
  
        Serial.printf("Updated Servo %d Settings\n", id);
      }
    }
    break;    
  }
}

void sendServoData() {
  StaticJsonBuffer<300> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  JsonArray& servosObj = root.createNestedArray("servos");

  String jsonString;
  
  root["numservos"] = NUMSERVOS;

  for(byte id = 0; id < NUMSERVOS; id++) {
    JsonObject& servoObj = servosObj.createNestedObject();
    servoObj["id"] = id;
    servoObj["min"] = config.limits[id].min;
    servoObj["zero"] = config.limits[id].zero;
    servoObj["max"] = config.limits[id].max;
  }

  root.printTo(jsonString);
  webSocket.sendTXT(0, jsonString.c_str(), jsonString.length());
  Serial.printf("->%s %d\n", jsonString.c_str(), jsonString.length());
}


