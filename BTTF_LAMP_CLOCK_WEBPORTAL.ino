#include "FS.h"
#include "Adafruit_NeoPixel.h"
#include "TM1637Display.h"
#include "WiFiManager.h"
#include "NTPClient.h"
#include "ArduinoJson.h"

#ifdef ESP32
  #include "SPIFFS.h"
#endif

#define ANALOG_SWITCH_PIN 34
#define LED_PIN 5
#define RED_CLK 16
#define RED1_DIO 17
#define RED2_DIO 18
#define RED3_DIO 19
#define AM 32
#define PM 33
#define NUMPIXELS 48

int LED_COLOR_MODE = 0;
const long UTC_OFFSET_SECONDS = 3600;
int UTC_OFFSET_HOURS = 2;
const int DISPLAY_BRIGHTNESS = 3;
const int LED_BRIGHTNESS = 250;

const int SWITCH_HOLD_TIME = 3000;
unsigned long switchPressStartTime = 0;

const char* AP_NAME = "BTTF_LAMP_CLOCK";
const char* AP_PASS = "password";

Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
TM1637Display red1(RED_CLK, RED1_DIO);
TM1637Display red2(RED_CLK, RED2_DIO);
TM1637Display red3(RED_CLK, RED3_DIO);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", UTC_OFFSET_SECONDS * UTC_OFFSET_HOURS);
WiFiManager manager;       

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(RED_CLK, OUTPUT);
  pinMode(RED1_DIO, OUTPUT);
  pinMode(RED2_DIO, OUTPUT);
  pinMode(RED3_DIO, OUTPUT);
  pinMode(AM, OUTPUT);
  pinMode(PM, OUTPUT);

  pinMode(ANALOG_SWITCH_PIN, INPUT);

  Serial.begin(9600);

  loadConfig();
  setupWifiManager();
  setupTimeClient();
  setupBrightness();
}

void loop() {
  updateTimeDisplays();
  updateAMPMLeds();  
  updateBacklight();
}

void loadConfig() {
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

        #if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {
        #else
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
        #endif

          Serial.println("\nparsed json");
          UTC_OFFSET_HOURS = json["utc_offset"];

        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
}

bool shouldSaveConfig = false;
void setShouldSaveConfig() {
  shouldSaveConfig = true;
}

void storeConfig() {
  if (SPIFFS.format()) {
    Serial.println("Successfully formatted FS");
  }

  if (SPIFFS.begin()) {
      Serial.println("mounted file system");
  } else {
    Serial.println("failed to mount fs");
  }

  Serial.println("saving config");
 #if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
  DynamicJsonDocument json(1024);
#else
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
#endif
  json["utc_offset"] = UTC_OFFSET_HOURS;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("failed to open config file for writing");
  }

#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
  serializeJson(json, Serial);
  serializeJson(json, configFile);
#else
  json.printTo(Serial);
  json.printTo(configFile);
#endif
  configFile.close();
}

void setupWifiManager() {
  WiFiManagerParameter custom_utc_offset("utf_offset", "UTC Offset", String(UTC_OFFSET_HOURS).c_str(), 2);

  manager.setDarkMode(true);
  manager.addParameter(&custom_utc_offset);
  manager.setSaveConfigCallback(setShouldSaveConfig);
  manager.setTimeout(180);

  bool wifiResult = manager.autoConnect(AP_NAME, AP_PASS);
  
  if (!wifiResult) {
    ESP.restart();
  }

  if (shouldSaveConfig) {
    UTC_OFFSET_HOURS = atoi(custom_utc_offset.getValue());
    storeConfig();
  }
  
  delay(3000);
}

void setupTimeClient() {
  timeClient.begin();
}

void setupBrightness() {
  red1.setBrightness(DISPLAY_BRIGHTNESS);
  red2.setBrightness(DISPLAY_BRIGHTNESS);
  red3.setBrightness(DISPLAY_BRIGHTNESS);
  pixels.setBrightness(LED_BRIGHTNESS);
}

void updateTimeDisplays() {
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime ((time_t *)&epochTime); 
  int currentYear = ptm->tm_year+1900;
  int monthDay = ptm->tm_mday;
  int currentMonth = ptm->tm_mon+1;
  
  red1.showNumberDecEx((currentMonth * 100) + monthDay, 0b01000000, true);
  red2.showNumberDecEx(currentYear, 0b00000000, true);
  red3.showNumberDecEx((timeClient.getHours() * 100) + timeClient.getMinutes(), 0b01000000, true);

  if((currentMonth * 30 + monthDay) >= 121 && (currentMonth * 30 + monthDay) < 331) {
    // DST SUMMER
    timeClient.setTimeOffset(UTC_OFFSET_SECONDS * UTC_OFFSET_HOURS);}
  else {
    // DST WINTER
    timeClient.setTimeOffset((UTC_OFFSET_SECONDS * UTC_OFFSET_HOURS) - 3600);
  }
}

void updateAMPMLeds() {
  if(timeClient.getHours() >= 13) {
    digitalWrite(AM, 0);
    digitalWrite(PM, 1);
  } else if(timeClient.getHours() == 12) {
    digitalWrite(AM, 0);
    digitalWrite(PM, 1);
  } else{
    digitalWrite(AM, 1);
    digitalWrite(PM, 0);
  }
}

void updateBacklight() {
  pixels.clear(); 

  if(LED_COLOR_MODE > 3){ LED_COLOR_MODE = 0; } // Reset counter

  handleSwitchPress();
  
  switch (LED_COLOR_MODE) {
    case 0: 
      for(int i = 0; i < 14; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 0, 0));
      }
      for(int i = 14; i < 29; i++) {
        pixels.setPixelColor(i, pixels.Color(160, 160, 0));
      }
      for(int i = 29; i < 44; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 0, 0));
      }
    break;

    case 1:
      for(int i = 0; i < 14; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 0, 255));
      }
      for(int i = 14; i < 29; i++) {
        pixels.setPixelColor(i, pixels.Color(200, 250, 255));
      }
      for(int i = 29; i < 44; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 0, 255));
      }
    break;

    case 2:
      for(int i = 0; i < 14; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 0, 10));
      }
      for(int i = 14; i < 29; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 10, 255));
      }
      for(int i = 29; i < 44; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 0, 10));
      }
    break;

    case 3:
      for(int i = 0; i < 44; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 0, 0));
      }
    break;
  }

  pixels.show();
}

void handleSwitchPress() {
  if(analogRead(ANALOG_SWITCH_PIN) > 100)
  { 
    if (switchPressStartTime == 0) {
      switchPressStartTime = millis();
    } else {
      unsigned long currentTime = millis();
      if (currentTime - switchPressStartTime >= SWITCH_HOLD_TIME) {
        factoryReset();
        switchPressStartTime = 0;
      }
    }

    delay(45);
  } else {
    if (switchPressStartTime != 0) {
      cycleColors();
    }
    
    switchPressStartTime = 0;
  }

  delay(100);
}

void cycleColors() {
  Serial.println("Cycling colors");
  LED_COLOR_MODE = LED_COLOR_MODE + 1;
}

void factoryReset() {
  Serial.println("Factory Reset");
  pixels.clear();
  red1.clear();
  red2.clear();
  red3.clear();
  delay(500);
  manager.resetSettings();
  ESP.restart();
}