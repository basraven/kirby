#include "Arduino.h"

#define USE_LITTLEFS

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WIFI_DETAILS.h>
#include <Scheduler.h>

#if defined USE_LITTLEFS
#include <LittleFS.h>
const char* fsName = "LittleFS";
FS* fileSystem = &LittleFS;
LittleFSConfig fileSystemConfig = LittleFSConfig();
#else
#error Please select a filesystem first by uncommenting one of the "#define USE_xxx" lines at the beginning of the sketch.
#endif
#include <WIFI_DETAILS.h>


#define DBG_OUTPUT_PORT Serial

#ifndef STASSID
#define STASSID primaryWifiSSID
#define STAPSK  primaryWifiPW
#endif

const char* ssid = STASSID;
const char* password = STAPSK;
const char* host = "kirby";

ESP8266WebServer server(80);

static bool fsOK;
String unsupportedFiles = String();

File uploadFile;

static const char TEXT_PLAIN[] PROGMEM = "text/plain";
static const char FS_INIT_ERROR[] PROGMEM = "FS INIT ERROR";
static const char FILE_NOT_FOUND[] PROGMEM = "FileNotFound";

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
  DBG_OUTPUT_PORT.println("handleStatus");
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

  String json;
  json.reserve(128);

  json = "{\"metrics\":\"";
  server.send(200, "application/json", json);

}

void handlePWM(){
  // if (String("/pwm").indexOf(server.uri()) > 0) {
  //   return;
  // }
  // String targetPercentage = server.arg(1);
  // DBG_OUTPUT_PORT.print("New target PWM of" + targetPercentage );
  String json;
  json.reserve(128);

  json = "{\"pwm\":\"";
  server.send(200, "application/json", json);

}

void handleAutoPilot(){
  // if (String("/pwm").indexOf(server.uri()) > 0) {
  //   return;
  // }
  // String targetPercentage = server.arg(1);
  // DBG_OUTPUT_PORT.print("New target PWM of" + targetPercentage );
  String json;
  json.reserve(128);

  json = "{\"autopilot\":\"";
  server.send(200, "application/json", json);

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
  
  if(uri.indexOf("/auto") == 0){
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
  DBG_OUTPUT_PORT.print(message);

  return replyNotFound(message);
}



////////////////////////////////
// Sensor Task
class SensorTask : public Task {
protected:
    void setup() {

    }

    void loop() {
      DBG_OUTPUT_PORT.print("SENSORRR");
      delay(3000);
    }

private:
    uint8_t state;
} sensor_task;

////////////////////////////////
// Auto Pilot Task
class AutopilotTask : public Task {
protected:
    void setup() {

    }

    void loop() {
      DBG_OUTPUT_PORT.print("Autopilot");
      delay(5000);
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
    }

private:
    uint8_t state;
} wifi_task;



void setup(void) {
  ////////////////////////////////
  // SERIAL INIT
  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.setDebugOutput(true);
  DBG_OUTPUT_PORT.print('\n');

  ////////////////////////////////
  // FILESYSTEM INIT

  fileSystemConfig.setAutoFormat(false);
  fileSystem->setConfig(fileSystemConfig);
  fsOK = fileSystem->begin();
  DBG_OUTPUT_PORT.println(fsOK ? F("Filesystem initialized.") : F("Filesystem init failed!"));

  ////////////////////////////////
  // WI-FI INIT
  DBG_OUTPUT_PORT.printf("Connecting to %s\n", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(host);
  WiFi.begin(ssid, password);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DBG_OUTPUT_PORT.print(".");
  }
  DBG_OUTPUT_PORT.println("");
  DBG_OUTPUT_PORT.print(F("Connected! IP address: "));
  DBG_OUTPUT_PORT.println(WiFi.localIP());

  ////////////////////////////////
  // MDNS INIT
  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    DBG_OUTPUT_PORT.print(F("Open http://"));
    DBG_OUTPUT_PORT.print(host);
    DBG_OUTPUT_PORT.println(F(".local/edit to open the FileSystem Browser"));
  }

  Scheduler.start(&wifi_task);
  Scheduler.start(&sensor_task);
  Scheduler.start(&autopilot_task);

  Scheduler.begin();
 
}


void loop(void) {
  DBG_OUTPUT_PORT.print("This should never happen");
}
