#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <ArduinoLog.h>
#include <FS.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>

#define MOTOR_SLEEP_PIN D1
#define MOTOR_STEP_PIN D3
#define MOTOR_FAULT_PIN D2
#define SMOKER_PIN D8
#define SMOKER_FAN_PIN D7
#define STATUS_LED_PIN D6

#define DEGREES_PER_STEP 7.5    // motor resolution when fullstepping
#define STEPS_FOR_360 360 / 7.5
#define PUFF_DELAY 4000         // milliseconds between puffs


#define ADC_OFFSET 8.34         // measure and compare to a known voltage source 
#define ADC_NUM_SAMPLES 5
#define BATTERY_MINIMUM 6.0     // 2.5V per cell is the absolute lowest recommendation for Li-Ion cells (I use 2 x NCR18650B)

const byte DNS_PORT = 53;
const char *HOSTNAME = "tophat";

uint8_t steps = 1;
uint16_t pageloads = 0;
double current_voltage = 0;
boolean runSmoker = false;
unsigned long last_puff = millis();
DNSServer dnsServer;
AsyncWebServer server(80);    // Webserver on port 80;
IPAddress apIP(192, 168, 4, 1);

void OTA_setup() {
  ArduinoOTA.onStart([]() {
    Log.notice(F("ArduinoOTA start" CR));
  });
  ArduinoOTA.onEnd([]() {
    Log.notice(F("ArduinoOTA end" CR));
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Log.notice(F("ArduinoOTA progress: %u%" CR), (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Log.error(F("ArduinoOTA error[%u]: " CR), error);

    if (error == OTA_AUTH_ERROR) Log.error(F("ArduinoOTA Auth Failed" CR));
    else if (error == OTA_BEGIN_ERROR) Log.error(F("ArduinoOTA Begin Failed" CR));
    else if (error == OTA_CONNECT_ERROR) Log.error(F("ArduinoOTA Connect Failed" CR));
    else if (error == OTA_RECEIVE_ERROR) Log.error(F("ArduinoOTA Receive Failed" CR));
    else if (error == OTA_END_ERROR) Log.error(F("ArduinoOTA End Failed" CR));
  });

  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword((const char *)"sigge");
  ArduinoOTA.begin();
}

String getUptime() {
  long millisecs = millis();
  char s[32];
  snprintf(s, sizeof(s), "%02ld tim %02ld min %02ld sec", millisecs / 1000 / 60 / 60, (millisecs / 1000 / 60) % 60, (millisecs / 1000) % 60);
  return String(s);
}

String templateProcessor(const String& var) {
  // "%" in HTML must be escaped with an additional "%", like this "%%"!
  if (var == "BATTERY_VOLTAGE") {
    return String(current_voltage);
  } else if (var == "CONCURRENT_VISITORS") {
    return String(WiFi.softAPgetStationNum());
  } else if (var == "PAGE_LOADS") {
    ++pageloads;
    return String(pageloads);
  } else if(var == "UPTIME") {
    return getUptime();
  } else return var;
}

void wifi_setup() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(HOSTNAME);
  WiFi.hostname(HOSTNAME);
  // If DNS server is started with "*" for domain name, it will reply with provided IP to all DNS request
  dnsServer.start(DNS_PORT, "*", apIP);
  if (!MDNS.begin(HOSTNAME)) {
    Log.error(F("Error setting up MDNS responder!" CR));
  } else {
    Log.notice(F("mDNS responder started" CR));
    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
  }
  
  server.onNotFound([](AsyncWebServerRequest *request){
    if (request->method() == HTTP_OPTIONS) {
      request->send(200); // CORS-support
    } else {
      Log.notice(F("Web resource not found: %s" CR), request->url().c_str());

      if (request->url().endsWith(".html")) {
        request->redirect("/index.html");
      } else {
        request->send(404, "text/plain", "Resource not found.");
      }
    }
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    Log.verbose(F("serving GET /status" CR));
    auto response = new AsyncJsonResponse();
    response->addHeader("Cache-Control", "no-store, must-revalidate");
    
    auto root = response->getRoot();

    root["voltage"] = current_voltage;
    root["clients"] = WiFi.softAPgetStationNum();
    root["pageloads"] = pageloads;
    root["uptime"] = getUptime();
    root["runSmoker"] = runSmoker;

    response->setLength();
    request->send(response);
  });

  server.on("/state", HTTP_PUT, [](AsyncWebServerRequest *request) { }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    Log.verbose(F("serving PUT /state" CR));
 
    DynamicJsonDocument root(100);       
    auto error = deserializeJson(root, (const char*)data);

    if (!error) {
      if (root.containsKey("runSmoker")) {
        runSmoker = root["runSmoker"];
      }

      request->send(200);
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html", false, templateProcessor);
  });
  // serve all other resources, with cache-header
  server
    .serveStatic("/", SPIFFS, "/")
    .setCacheControl("max-age=21600");   // 6 hours in seconds.

  server.begin();
}

void setup() {
  pinMode(MOTOR_SLEEP_PIN, OUTPUT);
  digitalWrite(MOTOR_SLEEP_PIN, LOW);

  pinMode(MOTOR_STEP_PIN, OUTPUT);
  digitalWrite(MOTOR_STEP_PIN, LOW);

  pinMode(MOTOR_FAULT_PIN, INPUT);

  pinMode(SMOKER_PIN, OUTPUT);
  digitalWrite(SMOKER_PIN, LOW);

  pinMode(SMOKER_FAN_PIN, OUTPUT);
  digitalWrite(SMOKER_FAN_PIN, LOW);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);  // Show that we are on, and LED is working

  Serial.begin(115200);

  Log.begin(LOG_LEVEL_VERBOSE, &Serial);

  // mount SPIFFS filesystem, the files stored in the "data"-directory in the root of this project. Contains the web application files.
  if (!SPIFFS.begin()) {
    Log.error(F("Failed to mount SPIFFS filesystem." CR));
    return;
  } else {
    Log.notice(F("SPIFFS mounted." CR));
  }

  wifi_setup();
  OTA_setup();

  Log.notice(F("Setup done, wait a while before we start." CR));

  delay(2000);

  digitalWrite(STATUS_LED_PIN, HIGH);  // Turn off LED
}

void step_motor() {
  Log.notice(F("Step motor, count: %d" CR), steps);

  digitalWrite(MOTOR_SLEEP_PIN, HIGH);  // activate motor controller
  delay(2);                             // 1.7ms until controller is booted and ready for pulse
  digitalWrite(MOTOR_STEP_PIN, HIGH);   // trigger single step
  delayMicroseconds(2000);
  digitalWrite(MOTOR_STEP_PIN, LOW);
  delay(2);                             // let coils energize before we cut power
  digitalWrite(MOTOR_SLEEP_PIN, LOW);   // deactivate motor controller, save some battery
}

void motor_do_360() {
  Log.notice(F("Motor do 360 degrees." CR));

  digitalWrite(MOTOR_SLEEP_PIN, HIGH);  // activate motor controller
  delay(2);                             // 1.7ms until controller is booted and ready for pulse

  for (auto i = 0; i < STEPS_FOR_360 * 3; i++) {
    digitalWrite(MOTOR_STEP_PIN, HIGH);  // trigger single step
    delayMicroseconds(16000); // alt. 16000
    digitalWrite(MOTOR_STEP_PIN, LOW);
    delayMicroseconds(16000); // alt. 16000
  }

  delay(2);                             // let coils energize before we cut power
  digitalWrite(MOTOR_SLEEP_PIN, LOW);   // deactivate motor controller, save some battery
}

void puff_smoke() {
    Log.notice(F("Puff smoke" CR));
    digitalWrite(SMOKER_FAN_PIN, HIGH);  // start smoke fan for a short while to exhaust a smoke puff
    delay(150);
    digitalWrite(SMOKER_FAN_PIN, LOW);
}

void check_battery() {
  auto sum = 0;
  // take a number of analog samples and add them up
  for (auto count = 0; count < ADC_NUM_SAMPLES; count++) {
      sum += analogRead(A0);
      delay(5);
  }

  // https://arduinodiy.wordpress.com/2016/12/25/monitoring-lipo-battery-voltage-with-wemos-d1-minibattery-shield-and-thingspeak/
  current_voltage = (float)sum / (float)ADC_NUM_SAMPLES / 1023.0 * ADC_OFFSET;
  current_voltage = roundf(current_voltage * 100) / 100;  // round to two decimals

  if (current_voltage < BATTERY_MINIMUM) {
    Log.warning(F("Battery voltage too low, %DV. Shutting down!" CR), current_voltage);

    digitalWrite(SMOKER_PIN, LOW);
    digitalWrite(SMOKER_FAN_PIN, LOW);
    digitalWrite(MOTOR_SLEEP_PIN, LOW);

    // flash LED to show battery is too low
    for (auto i = 0; i < 10; i++) {
      digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
      delay(500);
    }

    ESP.deepSleep(0);
    exit(1);
  } else {
    Log.notice(F("Battery voltage %DV." CR), current_voltage);
  }
}

void loop() {

  auto currentMillis = millis();

  check_battery();

  dnsServer.processNextRequest();
  ArduinoOTA.handle();

  /* Enable if we want to use stepper-motor
  if (steps > STEPS_FOR_360) {
    motor_do_360();
    steps = 0;
  } else {
    step_motor();
    steps++;
  }*/

  if (runSmoker) {
    // Constant smoke
    digitalWrite(SMOKER_PIN, HIGH);
    digitalWrite(SMOKER_FAN_PIN, HIGH);
  } else {
    // Smoke puffing
    digitalWrite(SMOKER_PIN, currentMillis % 2000 > 500);  // activate smoker for parts of the time. Conserv battery and keep it from getting too hot.

    if (currentMillis - last_puff > PUFF_DELAY) {
      puff_smoke();
      last_puff = millis();
    }
  }

  delay(5);
}