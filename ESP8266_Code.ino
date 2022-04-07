/* Official code for ESP8266 boards  version 3.1 */

#pragma GCC optimize ("-Ofast")
#include <ArduinoJson.h>
#include <bearssl/bearssl.h>
#include <TypeConversion.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Ticker.h>
#include <ESP8266WebServer.h>

namespace {
const char* SSID = "My cool wifi name";
const char* PASSWORD = "My secret wifi pass";
const char* USERNAME = "my_cool_username";
const char* RIG_IDENTIFIER = "Auto";
const char* MINER_KEY = "None";
const bool USE_HIGHER_DIFF = true;
const bool WEB_DASHBOARD = true;
const bool WEB_HASH_UPDATER = false;
const bool LED_BLINKING = true;

const char * DEVICE = "ESP8266";
const char * POOLPICKER_URL[] = {"https://server.duinocoin.com/getPool"};
const char * MINER_BANNER = "Official ESP8266 Miner";
const char * MINER_VER = "3.1";
unsigned int share_count = 0;
unsigned int port = 0;
unsigned int difficulty = 0;
float hashrate = 0;
String AutoRigName = "";
String host = "";
String node_id = "";

const char WEBSITE[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
<title>@@DEVICE@@ dashboard</title>
</head>
<body>
<br>
<center>
<strong>
<p>Esp32 @@ID@@ Mining Statistics</p></strong>
<p>IP @@IP_ADDR@@</p>
<br>
<p>@@HASHRATE@@ Kh/s</p>
<p>Diff @@DIFF@@</p>
<p>@@SHARES@@ Shares</p>
<p>@@NODE@@</p>
<p>Free Memory @@MEMORY@@</p>
<p>Rig ID @@ID@@</p>
<p>Ver.@@VERSION@@</p>
</center>
</body>
</html>
)=====";

ESP8266WebServer server(80);

void hashupdater(){ //update hashrate every 3 sec in browser without reloading page
  server.send(200, "text/plane", String(hashrate / 1000));
  Serial.println("Update hashrate on page");
};

void UpdateHostPort(String input) {
  // Thanks @ricaun for the code
  DynamicJsonDocument doc(256);
  deserializeJson(doc, input);
  const char* name = doc["name"];
  
  host = String((const char*)doc["ip"]);
  port = int(doc["port"]);
  node_id = String(name);

  Serial.println("Poolpicker selected the best mining node: " + node_id);
}

String httpGetString(String URL) {
  String payload = "";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  if (http.begin(client, URL)) {
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) payload = http.getString();
    else Serial.printf("Error fetching node from poolpicker: %s\n", http.errorToString(httpCode).c_str());

    http.end();
  }
  return payload;
}

void UpdatePool() {
  String input = "";
  int waitTime = 1;
  int poolIndex = 0;

//  int poolSize = sizeof(POOLPICKER_URL) / sizeof(char*);
//    while (input == "") {
//    Serial.println("Fetching mining node from the poolpicker in " + String(waitTime) + "s");
//    input = httpGetString(POOLPICKER_URL[poolIndex]);
//    poolIndex += 1;

    // Check if pool index needs to roll over
//    if( poolIndex >= poolSize ){
//      poolIndex %= poolSize;
//      delay(waitTime * 1000);

      // Increase wait time till a maximum of 32 seconds (addresses: Limit connection requests on failure in ESP boards #1041)
//      waitTime *= 2;
//      if( waitTime > 32 )
//        waitTime = 32;
//    }
//  }

/* *****ini awal kode manual pool***** */
input = "{\"ip\":\"51.158.182.90\",\"name\":\"star-pool-1\",\"port\":9779,\"server\":\"duino-rest-1\",\"succes\":true}";
/* *****ini akhir kode manual pool***** */

  // Setup pool with new input
  UpdateHostPort(input);
}

WiFiClient client;
String client_buffer = "";
String chipID = "";
String START_DIFF = "";

// Loop WDT... please don't feed me...
// See lwdtcb() and lwdtFeed() below
Ticker lwdTimer;
#define LWD_TIMEOUT   60000

unsigned long lwdCurrentMillis = 0;
unsigned long lwdTimeOutMillis = LWD_TIMEOUT;

#define END_TOKEN  '\n'
#define SEP_TOKEN  ','

#define LED_BUILTIN 2

#define BLINK_SHARE_FOUND    1
#define BLINK_SETUP_COMPLETE 2
#define BLINK_CLIENT_CONNECT 3
#define BLINK_RESET_DEVICE   5

void SetupWifi() {
  Serial.println("Connecting to: " + String(SSID));
  WiFi.mode(WIFI_STA); // Setup ESP in client mode
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.begin(SSID, PASSWORD);

  int wait_passes = 0;
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++wait_passes >= 10) {
      WiFi.begin(SSID, PASSWORD);
      wait_passes = 0;
    }
  }

  Serial.println("\n\nnSuccessfully connected to WiFi");
  Serial.println("Local IP address: " + WiFi.localIP().toString());
  Serial.println("Rig name: " + String(RIG_IDENTIFIER));
  Serial.println();

  UpdatePool();
}

void SetupOTA() {
  // Prepare OTA handler
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
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

  ArduinoOTA.setHostname(RIG_IDENTIFIER); // Give port a name not just address
  ArduinoOTA.begin();
}

void blink(uint8_t count, uint8_t pin = LED_BUILTIN) {
  if (LED_BLINKING){
    uint8_t state = HIGH;

    for (int x = 0; x < (count << 1); ++x) {
      digitalWrite(pin, state ^= HIGH);
      delay(50);
    }
  }
}

void RestartESP(String msg) {
  Serial.println(msg);
  Serial.println("Restarting ESP...");
  blink(BLINK_RESET_DEVICE);
  ESP.reset();
}

// Our new WDT to help prevent freezes
// code concept taken from https://sigmdel.ca/michel/program/esp8266/arduino/watchdogs2_en.html
void ICACHE_RAM_ATTR lwdtcb(void) {
  if ((millis() - lwdCurrentMillis > LWD_TIMEOUT) || (lwdTimeOutMillis - lwdCurrentMillis != LWD_TIMEOUT))
    RestartESP("Loop WDT Failed!");
}

void lwdtFeed(void) {
  lwdCurrentMillis = millis();
  lwdTimeOutMillis = lwdCurrentMillis + LWD_TIMEOUT;
}

void VerifyWifi() {
  while (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0))
    WiFi.reconnect();
}

void handleSystemEvents(void) {
  VerifyWifi();
  ArduinoOTA.handle();
  yield();
}

// https://stackoverflow.com/questions/9072320/split-string-into-string-array
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int max_index = data.length() - 1;

  for (int i = 0; i <= max_index && found <= index; i++) {
    if (data.charAt(i) == separator || i == max_index) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == max_index) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void waitForClientData(void) {
  client_buffer = "";

  while (client.connected()) {
    if (client.available()) {
      client_buffer = client.readStringUntil(END_TOKEN);
      if (client_buffer.length() == 1 && client_buffer[0] == END_TOKEN)
        client_buffer = "???\n"; // NOTE: Should never happen

      break;
    }
    handleSystemEvents();
  }
}

void ConnectToServer() {
  if (client.connected())
    return;

  Serial.println("\n\nConnecting to the Duino-Coin server...");
  while (!client.connect(host, port));

  waitForClientData();
  Serial.println("Connected to the server. Server version: " + client_buffer );
  blink(BLINK_CLIENT_CONNECT); // Sucessfull connection with the server
}

bool max_micros_elapsed(unsigned long current, unsigned long max_elapsed) {
  static unsigned long _start = 0;

  if ((current - _start) > max_elapsed) {
    _start = current;
    return true;
  }
  return false;
}

void dashboard() {
  Serial.println("Handling HTTP client");

  String s = WEBSITE;
  s.replace("@@IP_ADDR@@", WiFi.localIP().toString());
  
  s.replace("@@HASHRATE@@", String(hashrate / 1000));
  s.replace("@@DIFF@@", String(difficulty / 100));
  s.replace("@@SHARES@@", String(share_count));
  s.replace("@@NODE@@", String(node_id));

  s.replace("@@DEVICE@@", String(DEVICE));
  s.replace("@@ID@@", String(RIG_IDENTIFIER));
  s.replace("@@MEMORY@@", String(ESP.getFreeHeap()));
  s.replace("@@VERSION@@", String(MINER_VER));

  server.send(200, "text/html", s);
}

} // namespace

void setup() {
  Serial.begin(500000);
  Serial.println("\nDuino-Coin " + String(MINER_VER));
  pinMode(LED_BUILTIN, OUTPUT);

  // Autogenerate ID if required
  chipID = String(ESP.getChipId(), HEX);
  if( strcmp(RIG_IDENTIFIER, "Auto") == 0 ){
    AutoRigName = "ESP8266-" + chipID;
    AutoRigName.toUpperCase();
    RIG_IDENTIFIER = AutoRigName.c_str();
  }

  SetupWifi();
  SetupOTA();

  lwdtFeed();
  lwdTimer.attach_ms(LWD_TIMEOUT, lwdtcb);
  if (USE_HIGHER_DIFF) START_DIFF = "ESP8266H";
  else START_DIFF = "ESP8266";

  if(WEB_DASHBOARD) {
    if (!MDNS.begin(RIG_IDENTIFIER)) {
      Serial.println("mDNS unavailable");
    }
    MDNS.addService("http", "tcp", 80);
    Serial.print("Configured mDNS for dashboard on http://" 
                  + String(RIG_IDENTIFIER)
                  + ".local (or http://"
                  + WiFi.localIP().toString()
                  + ")");
    server.on("/", dashboard);
    if (WEB_HASH_UPDATER) server.on("/hashrateread", hashupdater);
    server.begin();
  }

  blink(BLINK_SETUP_COMPLETE);
}

void loop() {
  br_sha1_context sha1_ctx, sha1_ctx_base;
  uint8_t hashArray[20];
  String duco_numeric_result_str;
  
  // 1 minute watchdog
  lwdtFeed();

  // OTA handlers
  VerifyWifi();
  ArduinoOTA.handle();
  if(WEB_DASHBOARD) server.handleClient();

  ConnectToServer();
  Serial.println("Asking for a new job for user: " + String(USERNAME));
  client.print("JOB," + String(USERNAME) + "," + String(START_DIFF) + "," + String(MINER_KEY));

  waitForClientData();
  String last_block_hash = getValue(client_buffer, SEP_TOKEN, 0);
  String expected_hash = getValue(client_buffer, SEP_TOKEN, 1);
  difficulty = getValue(client_buffer, SEP_TOKEN, 2).toInt() * 100 + 1;

  int job_len = last_block_hash.length() + expected_hash.length() + String(difficulty).length();
  Serial.println("Received a correct job with size of " + String(job_len) + " bytes");
  expected_hash.toUpperCase();
  br_sha1_init(&sha1_ctx_base);
  br_sha1_update(&sha1_ctx_base, last_block_hash.c_str(), last_block_hash.length());

  float start_time = micros();
  max_micros_elapsed(start_time, 0);

  String result = "";
  digitalWrite(LED_BUILTIN, HIGH);
  for (unsigned int duco_numeric_result = 0; duco_numeric_result < difficulty; duco_numeric_result++) {
    // Difficulty loop
    sha1_ctx = sha1_ctx_base;
    duco_numeric_result_str = String(duco_numeric_result);
    br_sha1_update(&sha1_ctx, duco_numeric_result_str.c_str(), duco_numeric_result_str.length());
    br_sha1_out(&sha1_ctx, hashArray);
    result = experimental::TypeConversion::uint8ArrayToHexString(hashArray, 20);
    if (result == expected_hash) {
      // If result is found
      unsigned long elapsed_time = micros() - start_time;
      float elapsed_time_s = elapsed_time * .000001f;
      hashrate = duco_numeric_result / elapsed_time_s;
      share_count++;
      blink(BLINK_SHARE_FOUND);

      client.print(String(duco_numeric_result)
                   + ","
                   + String(hashrate)
                   + ","
                   + String(MINER_BANNER)
                   + " "
                   + String(MINER_VER)
                   + ","
                   + String(RIG_IDENTIFIER)
                   + ",DUCOID"
                   + String(chipID));

      waitForClientData();
      Serial.println(client_buffer
                     + " share #"
                     + String(share_count)
                     + " (" + String(duco_numeric_result) + ")"
                     + " hashrate: "
                     + String(hashrate / 1000, 2)
                     + " kH/s ("
                     + String(elapsed_time_s)
                     + "s)");
      break;
    }
    if (max_micros_elapsed(micros(), 500000))
    {
      handleSystemEvents();
    }
    else
    {
      delay(0);
    }
  }
}
