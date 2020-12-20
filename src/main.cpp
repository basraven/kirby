#include "Arduino.h"

#define USE_LITTLEFS

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WIFI_DETAILS.h>
#include <Scheduler.h>
#include <ArduinoJson.h>

StaticJsonDocument<200> doc;

#if defined USE_LITTLEFS
#include <LittleFS.h>
const char* fsName = "LittleFS";
FS* fileSystem = &LittleFS;
LittleFSConfig fileSystemConfig = LittleFSConfig();
#else
#error Please select a filesystem first by uncommenting one of the "#define USE_xxx" lines at the beginning of the sketch.
#endif
#include <WIFI_DETAILS.h>
#include <VAR_LOCATIONS.h>


#define DBG_OUTPUT_PORT Serial
const short int BUILTIN_LED1 = 1 ; // GPIO1 blue led and transaction GPIO

#ifndef STASSID
#define STASSID1 primaryWifiSSID
#define STAPSK1  primaryWifiPW
#define STASSID2 secundaryWifiSSID
#define STAPSK2  secundaryWifiPW
#endif

const char* ssid1 = STASSID1;
const char* ssid2 = STASSID2;
const char* wifipassword1 = STAPSK1;
const char* wifipassword2 = STAPSK2;
const char* host = "kirby";

ESP8266WebServer server(80);

static bool fsOK;
String unsupportedFiles = String();

File uploadFile;

static const char TEXT_PLAIN[] PROGMEM = "text/plain";
static const char FS_INIT_ERROR[] PROGMEM = "FS INIT ERROR";
static const char FILE_NOT_FOUND[] PROGMEM = "FileNotFound";
static const char WRONG_METHOD[] PROGMEM = "WrongMethod";

// WIFI
short const int wifiSleepMS = 2000;

// Temperature
#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS 0
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
float tempCelcius=0;
// float Fahrenheit=0;
short const int probeSleepMs = 3000;

// PWM
short const int PWMGPIO = 2;
short int currentPwm;
short int prevPwm;
short int pwmTaskDelayMs = 1;

// Auto pilot
bool autopilotState = false;
short const int autopilotDelay = 2000;
const short int autopilotSettingsSize = 20;
short int autopilotSettings[autopilotSettingsSize][2];// ;//= {{0,0}}; // 0 degrees celsius = 0 pwm strength


////////////////////////////////
// Utils to return HTTP codes, and determine content-type

void replyOK() {
  server.send(200, FPSTR(TEXT_PLAIN), "");
}

void replyOKWithMsg(String msg) {
  server.send(200, FPSTR(TEXT_PLAIN), msg);
}

void replyNotFound(String msg) {
  server.send(404, FPSTR(TEXT_PLAIN), msg);
}

void replyBadRequest(String msg) {
  DBG_OUTPUT_PORT.println(msg);
  server.send(400, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

void replyServerError(String msg) {
  DBG_OUTPUT_PORT.println(msg);
  server.send(500, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

////////////////////////////////
// Request handlers

/*
   Return the FS type, status and size info
*/
void handleStatus() {
  DBG_OUTPUT_PORT.println("New /status request");
  FSInfo fs_info;
  String json;
  json.reserve(128);

  json = "{\"type\":\"";
  json += fsName;
  json += "\", \"isOk\":";
  if (fsOK) {
    fileSystem->info(fs_info);
    json += F("\"true\", \"totalBytes\":\"");
    json += fs_info.totalBytes;
    json += F("\", \"usedBytes\":\"");
    json += fs_info.usedBytes;
    json += "\"";
  } else {
    json += "\"false\"";
  }
  json += F(",\"unsupportedFiles\":\"");
  json += unsupportedFiles;
  json += "\"}";

  server.send(200, "application/json", json);
}


/*
   Return the list of files in the directory specified by the "dir" query string parameter.
   Also demonstrates the use of chuncked responses.
*/
void handleFileList() {
  if (!fsOK) {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }

  if (!server.hasArg("dir")) {
    return replyBadRequest(F("DIR ARG MISSING"));
  }

  String path = server.arg("dir");
  if (path != "/" && !fileSystem->exists(path)) {
    return replyBadRequest("BAD PATH");
  }

  DBG_OUTPUT_PORT.println(String("handleFileList: ") + path);
  Dir dir = fileSystem->openDir(path);
  path.clear();

  // use HTTP/1.1 Chunked response to avoid building a huge temporary string
  if (!server.chunkedResponseModeStart(200, "text/json")) {
    server.send(505, F("text/html"), F("HTTP1.1 required"));
    return;
  }

  // use the same string for every line
  String output;
  output.reserve(64);
  while (dir.next()) {
    if (output.length()) {
      // send string from previous iteration
      // as an HTTP chunk
      server.sendContent(output);
      output = ',';
    } else {
      output = '[';
    }

    output += "{\"type\":\"";
    if (dir.isDirectory()) {
      output += "dir";
    } else {
      output += F("file\",\"size\":\"");
      output += dir.fileSize();
    }

    output += F("\",\"name\":\"");
    // Always return names without leading "/"
    if (dir.fileName()[0] == '/') {
      output += &(dir.fileName()[1]);
    } else {
      output += dir.fileName();
    }

    output += "\"}";
  }

  // send last string
  output += "]";
  server.sendContent(output);
  server.chunkedResponseFinalize();
}


/*
   Read the given file from the filesystem and stream it back to the client
*/
bool handleFileRead(String path) {
  DBG_OUTPUT_PORT.println(String("handleFileRead: ") + path);
  if (!fsOK) {
    replyServerError(FPSTR(FS_INIT_ERROR));
    return true;
  }

  if (path.endsWith("/")) {
    path += "index.htm";
  }

  String contentType;
  if (server.hasArg("download")) {
    contentType = F("application/octet-stream");
  } else {
    contentType = mime::getContentType(path);
  }

  if (!fileSystem->exists(path)) {
    // File not found, try gzip version
    path = path + ".gz";
  }
  if (fileSystem->exists(path)) {
    File file = fileSystem->open(path, "r");
    if (server.streamFile(file, contentType) != file.size()) {
      DBG_OUTPUT_PORT.println("Sent less data than expected!");
    }
    file.close();
    return true;
  }

  return false;
}



void handleMetrics(){
  DBG_OUTPUT_PORT.println("New /metrics request");
  String metrics;
  metrics += "kirby_temperature_current " + String(tempCelcius) + "\n";
  metrics += "kirby_pwm_prev " + String(prevPwm) + "\n";
  metrics += "kirby_pwm_current " + String(currentPwm) + "\n";
  metrics += "kirby_autopilot_state " + String(autopilotState) + "\n";
  for(byte i=0; i< autopilotSettingsSize ; i++){
    if(autopilotSettings[i][1]){
      metrics += "kirby_autopilot_setting{temperature=\"" + String(autopilotSettings[i][0]) + "\"} " + String(autopilotSettings[i][1]) + "\n";
    }
  }
  // metrics += "kirby_autopilot_setting{strength=\"100\"} " + String(autopilotState);
  server.send(200, "text/html", metrics);

}

void handlePWM(){
  DBG_OUTPUT_PORT.println("New /pwm request\n ");
  String path = server.arg("dir");
  DBG_OUTPUT_PORT.println(path);
  if (server.method() == HTTP_GET && path == "/pwm"){
    server.send(200, "application/json", String(currentPwm));
    return;
  }
  if (server.method() != HTTP_PUT && server.method() != HTTP_GET){
    return replyServerError(FPSTR(WRONG_METHOD));
  }

  if (path == "/" || path == "/pwm" || path == "/pwm/"){
    return replyBadRequest("BAD PATH");
  }
  // char delimiter[] = "/";
  char charUri[server.uri().length()];
  server.uri().toCharArray(charUri, server.uri().length()+1);

  // Returns first token 
  char* token = strtok(charUri, "/"); 
  byte i = 0;

  // FIXME: Remove while loop, for now: if it sits it fits
  while (token != NULL) { 
        if(i==1){
          currentPwm = String(token).toInt();
          // newPwmVal = token;
        }
        printf("%s\n", token); 
        token = strtok(NULL, "/");
        i++;
  } 

  // // Persist new value
  File file = fileSystem->open(locPwmCurrent, "w");
  if (file) {
    file.write(currentPwm);
    file.close();
    DBG_OUTPUT_PORT.println("New current PWM written: " + currentPwm);
    return replyOKWithMsg(String(currentPwm));
  } else {
    return replyServerError(F("PERSISTENCE FAILED"));
  }
  
}

void handleAutoPilot(){
  DBG_OUTPUT_PORT.println("New /autopilot request");
  if (server.method() == HTTP_GET){
    String json = "[\n";
    for(byte i=0; i<autopilotSettingsSize; i++){
      if(autopilotSettings[i][0] != 0){
        if(i>0){
          json += ",\n";
        }
        json += "\t{";

        json += "\t\t\"temperature\" : \"";
        json += autopilotSettings[i][0];
        json += "\", \n";
        
        json += "\t\t\"strength\" : \"";
        json += autopilotSettings[i][1];
        json += "\", \n";

        json += "\t}";
      }
    }
    json += "\n]";
    server.send(200, "application/json", json);
    return;
  }
  // TODO: use state toggle
  if (server.method() == HTTP_PUT){
    
  }
  if (server.method() == HTTP_POST){
    DBG_OUTPUT_PORT.println("Uploading new autopilot settings");
    
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, server.arg(0));

    // Test if parsing succeeds.
    if (error) {
      DBG_OUTPUT_PORT.print(F("deserializeJson() failed: "));
      DBG_OUTPUT_PORT.println(error.f_str());
      return replyServerError(error.f_str());
    }

    // Persist new value
    File file = fileSystem->open(locAutoPilotSettings, "r");
    fileSystem->remove(locAutoPilotSettings);
    file.close();

    file = fileSystem->open(locAutoPilotSettings, "w");
    if (file) {
      file.write("temperature,strength\n"); // csv headers
      for(byte i=0; i<doc.size(); i++){
        autopilotSettings[i][0] = doc[i]["temperature"];
        autopilotSettings[i][1] = doc[i]["strength"];
        
        // TODO: Make more efficient
        String csvString;
        csvString += autopilotSettings[i][0]; 
        csvString += ","; 
        csvString += autopilotSettings[i][1];  
        csvString += " "; 
        char charBuf[csvString.length() + 1];
        csvString.toCharArray(charBuf, csvString.length());
        // DBG_OUTPUT_PORT.println("charBuf");
        // DBG_OUTPUT_PORT.println(charBuf);
        file.write(charBuf);
        file.write("\n");
      }
      file.close();
      return replyOKWithMsg(String("New autopilot settings configured"));
    } else {
      return replyServerError(F("PERSISTENCE FAILED"));
    }
  }
  return replyServerError(FPSTR(WRONG_METHOD));
}


/*
   The "Not Found" handler catches all URI not explicitely declared in code
   First try to find and return the requested file from the filesystem,
   and if it fails, return a 404 page with debug information
*/
void handleNotFound() {
  
  String uri = ESP8266WebServer::urlDecode(server.uri()); // required to read paths with blanks
  
  // Handle wildcard paths
  
  if(uri.indexOf("/pwm") == 0){
    return handlePWM();
  }
  if(uri.indexOf("/metrics") == 0){
    return handlePWM();
  }  
  
  if(uri.indexOf("/autopilot") == 0){
    return handleAutoPilot();
  }  
  
  if (!fsOK) {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }


  if (handleFileRead(uri)) {
    return;
  }

  // Dump debug data
  String message;
  message.reserve(100);
  message = F("Error: File not found\n\nURI: ");
  message += uri;
  message += F("\nMethod: ");
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += F("\nArguments: ");
  message += server.args();
  message += '\n';
  for (uint8_t i = 0; i < server.args(); i++) {
    message += F(" NAME:");
    message += server.argName(i);
    message += F("\n VALUE:");
    message += server.arg(i);
    message += '\n';
  }
  message += "path=";
  message += server.arg("path");
  message += '\n';

  return replyNotFound(message);
}


////////////////////////////////
// Persistence tasks
void read_persistent_vars(const char * *varLocation, short int *varName){
  File file = LittleFS.open(*varLocation, "r");
  if (!file) {
    DBG_OUTPUT_PORT.println("Failed to open file for reading");
    return;
  }
  while (file.available()) {
    *varName = file.read();
  }
  file.close();
}
void read_persistent_autopilot_state(const char * *varLocation, bool *varName){
  File file = LittleFS.open(*varLocation, "r");
  if (!file) {
    DBG_OUTPUT_PORT.println("Failed to open file for reading");
    return;
  }
  while (file.available()) {
    *varName = bool(file.read());
  }
  file.close();
}
void read_persistent_autopilot_settings(const char * *varLocation, short int varName[autopilotSettingsSize][2]){
  File file = LittleFS.open(*varLocation, "r");
  if (!file) {
    DBG_OUTPUT_PORT.println("Failed to open file for reading");
    return;
  }
  
  // TODO: Could be nicer
  byte i = 0;
  char charBuf[15];
  String itemString;
  String readString;
  char * pch;
  while (file.available()) {
    readString = file.readStringUntil('\n');
    // DBG_OUTPUT_PORT.println("readString");
    // DBG_OUTPUT_PORT.println(readString);
    if(i != 0){
      readString.toCharArray(charBuf,15);
      
      pch = strtok(charBuf,",");
      itemString = String(pch);
      // DBG_OUTPUT_PORT.println("itemString1");
      // DBG_OUTPUT_PORT.println(itemString);
      autopilotSettings[i-1][0] = itemString.toInt();
      
      pch = strtok (NULL, ",");
      itemString = String(pch);
      // DBG_OUTPUT_PORT.println("itemString2");
      // DBG_OUTPUT_PORT.println(itemString);
      autopilotSettings[i-1][1] = itemString.toInt();
    }
    i++;
  }
  file.close();
}
  

////////////////////////////////
// PWM Signal Task
class PwmSignalTask : public Task {
protected:
    void setup() {
      pinMode(PWMGPIO, OUTPUT);
      DBG_OUTPUT_PORT.println("PWM Signal Task with delay of " + String(pwmTaskDelayMs) + " ms");
    }
    void loop() {
      if(prevPwm != currentPwm){
        DBG_OUTPUT_PORT.println("Updated PWM Signal, from " + String(prevPwm) + " to " + String(currentPwm));
        prevPwm = currentPwm;
      }
      // DBG_OUTPUT_PORT.println("TMP");
      if(currentPwm < 1){
        digitalWrite(PWMGPIO, LOW);
        // DBG_OUTPUT_PORT.println("LOW");
      } else if(currentPwm > 90){ 
        digitalWrite(PWMGPIO, HIGH);
        // DBG_OUTPUT_PORT.println("HIGH");
      } else {
        analogWrite(PWMGPIO, round(float(currentPwm) * float(2.55)));
        // analogWrite(PWMGPIO, 255);
        // DBG_OUTPUT_PORT.println(float(currentPwm) * float(2.55));
      }
      // delay(pwmTaskDelayMs);
    }

private:
    uint8_t state;
} pwmsignal_task;

////////////////////////////////
// Sensor Task
class SensorTask : public Task {
protected:
    void setup() {
      DBG_OUTPUT_PORT.println("Temperature probe started with delay of " + String(probeSleepMs) + " ms");
    }
    void loop() {
      sensors.requestTemperatures(); 
      float newTemp = sensors.getTempCByIndex(0);
      // DBG_OUTPUT_PORT.println("newTemp");
      // DBG_OUTPUT_PORT.println(sensors.getTempCByIndex(0));
      // DBG_OUTPUT_PORT.println(sensors.getTempCByIndex(1));
      // DBG_OUTPUT_PORT.println(sensors.getTempCByIndex(2));

      if(newTemp > 0 && newTemp < 100){
        tempCelcius = newTemp;
      }
      delay(probeSleepMs);
    }

private:
    uint8_t state;
} sensor_task;

////////////////////////////////
// Auto Pilot Task
class AutopilotTask : public Task {
protected:
    void setup() {
      // pinMode(BUILTIN_LED1, OUTPUT);

      if (fileSystem->exists(locAutoPilotSettings)) {
        read_persistent_autopilot_settings(&locAutoPilotSettings, autopilotSettings);
      }
      if (fileSystem->exists(locAutoPilotState)) {
        read_persistent_autopilot_state(&locAutoPilotState, &autopilotState);
      }
    }

    void loop() {
      for(byte i; i<autopilotSettingsSize; i++){
        if(autopilotSettings[i][1] > round(tempCelcius)){ // First temperature in the array higher than current temp
          currentPwm = autopilotSettings[i][1];
          break;
        }
      }
      delay(autopilotDelay);
    }

private:
    uint8_t state;
} autopilot_task;



////////////////////////////////
// WIFI Task

class WifiTask : public Task {
protected:
    void setup() {

      
      ////////////////////////////////
      // WEB SERVER INIT

      // Filesystem status
      server.on("/status", HTTP_GET, handleStatus);

      // List directory
      server.on("/list", HTTP_GET, handleFileList);
      
      // Get PWM strength
      server.on("/pwm", HTTP_GET, handlePWM);
      
      // Get Metrics strength
      server.on("/metrics", HTTP_GET, handleMetrics);
      
      // Get Metrics strength
      server.on("/autopilot", HTTP_GET, handleAutoPilot);

      // Default handler for all URIs not defined above
      // Use it to read files from filesystem
      server.onNotFound(handleNotFound);

      // Start server
      server.begin();
      DBG_OUTPUT_PORT.println("HTTP server started");
    }

    void loop() {
      server.handleClient();
      MDNS.update();
      delay(wifiSleepMS);
    }

private:
    uint8_t state;
} wifi_task;



void setup(void) {
  pinMode(PWMGPIO, OUTPUT);
  ////////////////////////////////
  // SERIAL INIT
  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.setDebugOutput(true);

  ////////////////////////////////
  // FILESYSTEM INIT

  fileSystemConfig.setAutoFormat(false);
  fileSystem->setConfig(fileSystemConfig);
  fsOK = fileSystem->begin();
  DBG_OUTPUT_PORT.println(fsOK ? F("Filesystem initialized.") : F("Filesystem init failed!"));

  Scheduler.start(&pwmsignal_task);
  ////////////////////////////////
  // WI-FI INIT
  WiFi.mode(WIFI_STA);
  WiFi.hostname(host);
  
  const char* *ssid = &ssid1;
  const char* *wifipassword = &wifipassword1;

  do{
    DBG_OUTPUT_PORT.printf("Connecting to %s\n", *ssid);
    WiFi.begin(*ssid, *wifipassword);
    for(uint8_t i=0; i<10; i++){
      delay(1000);
      DBG_OUTPUT_PORT.print(".");
      if(WiFi.status() == WL_CONNECTED){
        break;
      }
      // Wait for connection
    }
    if(ssid == &ssid1){
      ssid = &ssid2;
      wifipassword = &wifipassword2;
    }else{
      ssid = &ssid1;
      wifipassword = &wifipassword1;
    }
  } while (WiFi.status() != WL_CONNECTED);
  DBG_OUTPUT_PORT.println(F("Connected! IP address: "));
  DBG_OUTPUT_PORT.println(WiFi.localIP());  

  ////////////////////////////////
  // MDNS INIT
  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    DBG_OUTPUT_PORT.println(F("Open http://"));
    DBG_OUTPUT_PORT.println(host);
  }

  if (fileSystem->exists(locPwmCurrent)) {
    read_persistent_vars(&locPwmCurrent, &currentPwm);
  }

  Scheduler.start(&wifi_task);
  // Scheduler.start(&sensor_task);
  // Scheduler.start(&autopilot_task);

  Scheduler.begin();
 
}


void loop(void) {
  DBG_OUTPUT_PORT.println("This should never happen");
}
