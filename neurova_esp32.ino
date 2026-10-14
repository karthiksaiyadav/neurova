
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <math.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* TS_WRITE_KEY = "YOUR_THINGSPEAK_WRITE_KEY";
const char* TS_URL = "http://api.thingspeak.com/update";

#define DHT_PIN 4
#define DHT_TYPE DHT22
#define PIEZO_PIN 34

DHT dht(DHT_PIN, DHT_TYPE);

const float TEMP_BASELINE = 25.0;
const float TEMP_WARNING = 35.0;
const float RH_BASELINE = 55.0;
const float RH_WARNING = 65.0;
const float RH_HIGH = 75.0;
const float RISK_ALERT_LIMIT = 30.0;

const int PIEZO_EVENT_THRESHOLD = 250;
const int PIEZO_FULL_SCALE = 1000;

const float W_VIBRATION = 0.40;
const float W_HUMIDITY = 0.30;
const float W_TEMPERATURE = 0.30;

const unsigned long DHT_INTERVAL = 2500;
const unsigned long PIEZO_INTERVAL = 100;
const unsigned long PIEZO_WINDOW_US = 50000;
const unsigned long CLOUD_INTERVAL = 20000;
const unsigned long WIFI_RETRY_INTERVAL = 10000;
const unsigned long EVENT_COOLDOWN = 2000;
const unsigned long DISPLAY_INTERVAL = 2500;

float temperature = NAN;
float humidity = NAN;

int piezoMinimum = 0;
int piezoMaximum = 0;
int vibrationAmplitude = 0;

float vibrationRisk = 0.0;
float humidityRisk = 0.0;
float temperatureRisk = 0.0;
float totalRisk = 0.0;

bool environmentValid = false;
bool humidityWarning = false;
bool humidityHigh = false;
bool temperatureWarning = false;
bool riskAlert = false;
bool vibrationEventPending = false;

unsigned long lastDHTRead = 0;
unsigned long lastPiezoSample = 0;
unsigned long lastCloudUpdate = 0;
unsigned long lastWiFiRetry = 0;
unsigned long lastVibrationEvent = 0;
unsigned long lastDisplay = 0;

float clamp100(float value) {
  if (value < 0.0) return 0.0;
  if (value > 100.0) return 100.0;
  return value;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWi-Fi unavailable; will retry.");
  }
}

void readEnvironment() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t) ||
      h < 0.0 || h > 100.0 ||
      t < -40.0 || t > 80.0) {
    Serial.println("ERROR: Invalid DHT22 reading.");
    environmentValid = false;
    return;
  }

  humidity = h;
  temperature = t;
  environmentValid = true;

  humidityWarning = humidity > RH_WARNING;
  humidityHigh = humidity > RH_HIGH;
  temperatureWarning = temperature > TEMP_WARNING;
}

void readPiezo() {
  int minimumValue = 4095;
  int maximumValue = 0;

  unsigned long start = micros();

  while ((unsigned long)(micros() - start) <
         PIEZO_WINDOW_US) {
    int value = analogRead(PIEZO_PIN);

    if (value < minimumValue) minimumValue = value;
    if (value > maximumValue) maximumValue = value;
  }

  piezoMinimum = minimumValue;
  piezoMaximum = maximumValue;
  vibrationAmplitude = maximumValue - minimumValue;

  vibrationRisk = clamp100(
    (float)vibrationAmplitude * 100.0 /
    PIEZO_FULL_SCALE
  );

  unsigned long now = millis();

  if (vibrationAmplitude >= PIEZO_EVENT_THRESHOLD &&
      (lastVibrationEvent == 0 ||
       now - lastVibrationEvent >= EVENT_COOLDOWN)) {
    lastVibrationEvent = now;
    vibrationEventPending = true;
    Serial.println("VIBRATION EVENT: Threshold crossed.");
  }
}

void calculateRisk() {
  if (!environmentValid) {
    totalRisk = NAN;
    riskAlert = false;
    return;
  }

  temperatureRisk = clamp100(
    (temperature - TEMP_BASELINE) /
    (TEMP_WARNING - TEMP_BASELINE) * 100.0
  );

  humidityRisk = clamp100(
    (humidity - RH_BASELINE) /
    (RH_HIGH - RH_BASELINE) * 100.0
  );

  totalRisk =
      W_VIBRATION * vibrationRisk
    + W_HUMIDITY * humidityRisk
    + W_TEMPERATURE * temperatureRisk;

  totalRisk = clamp100(totalRisk);
  riskAlert = totalRisk > RISK_ALERT_LIMIT;
}

void printResults() {
  Serial.println("\n========== NEUROVA ==========");
  Serial.println("Material: RAW PADDY RICE");

  if (!environmentValid) {
    Serial.println("ENVIRONMENT STATUS: INVALID");
    Serial.println("Check DHT22 wiring and readings.");
  } else {
    Serial.print("Temperature: ");
    Serial.print(temperature, 1);
    Serial.println(" deg C");

    Serial.print("Relative humidity: ");
    Serial.print(humidity, 1);
    Serial.println(" % RH");

    Serial.print("Temperature risk: ");
    Serial.print(temperatureRisk, 1);
    Serial.println(" / 100");

    Serial.print("Humidity risk: ");
    Serial.print(humidityRisk, 1);
    Serial.println(" / 100");

    Serial.print("Combined storage risk: ");
    Serial.print(totalRisk, 1);
    Serial.println(" / 100");

    Serial.print("Risk alert: ");
    Serial.println(riskAlert ? "ACTIVE" : "NOT ACTIVE");

    if (temperatureWarning)
      Serial.println("WARNING: Temperature > 35 C");

    if (humidityWarning)
      Serial.println("WARNING: Humidity > 65% RH");

    if (humidityHigh)
      Serial.println("HIGH HUMIDITY: > 75% RH");
  }

  Serial.println("--- PIEZO DIAGNOSTICS ---");

  Serial.print("ADC minimum: ");
  Serial.println(piezoMinimum);

  Serial.print("ADC maximum: ");
  Serial.println(piezoMaximum);

  Serial.print("Vibration amplitude: ");
  Serial.print(vibrationAmplitude);
  Serial.println(" ADC counts");

  Serial.print("Vibration risk: ");
  Serial.print(vibrationRisk, 1);
  Serial.println(" / 100");

  if (piezoMinimum == 0 && piezoMaximum == 0) {
    Serial.println("CHECK: GPIO34 reads zero.");
  } else if (piezoMinimum == 4095 &&
             piezoMaximum == 4095) {
    Serial.println("CHECK: ADC saturated high.");
  } else if (vibrationAmplitude == 0) {
    Serial.println("NOTE: No ADC variation detected.");
  }

  if (vibrationEventPending)
    Serial.println("Vibration event awaiting cloud upload.");

  Serial.println("Vibration does not confirm insect presence.");
  Serial.println("=============================");
}

void sendToThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Upload skipped: Wi-Fi disconnected.");
    return;
  }

  if (!environmentValid || isnan(totalRisk)) {
    Serial.println("Upload skipped: invalid sensor data.");
    return;
  }

  HTTPClient http;

  String url = String(TS_URL);
  url += "?api_key=" + String(TS_WRITE_KEY);
  url += "&field1=" + String(temperature, 2);
  url += "&field2=" + String(humidity, 2);
  url += "&field3=" + String(vibrationEventPending ? 1 : 0);
  url += "&field4=" + String(humidityWarning ? 1 : 0);
  url += "&field5=" + String(temperatureWarning ? 1 : 0);
  url += "&field6=" + String(totalRisk, 2);
  url += "&field7=" + String(riskAlert ? 1 : 0);
  url += "&field8=" + String(vibrationAmplitude);

  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  if (!http.begin(url)) {
    Serial.println("ERROR: Could not initialize HTTP.");
    return;
  }

  int responseCode = http.GET();

  if (responseCode == HTTP_CODE_OK) {
    String response = http.getString();
    response.trim();

    long entryID = response.toInt();

    if (entryID > 0) {
      Serial.print("ThingSpeak upload successful. Entry: ");
      Serial.println(entryID);
      vibrationEventPending = false;
    } else {
      Serial.println("ThingSpeak rejected the update.");
      Serial.print("Response: ");
      Serial.println(response);
    }
  } else {
    Serial.print("ThingSpeak HTTP error: ");
    Serial.println(http.errorToString(responseCode));
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n================================");
  Serial.println("NEUROVA - RAW PADDY MONITOR");
  Serial.println("ESP32 + DHT22 + PIEZO");
  Serial.println("================================");

  analogReadResolution(12);
  analogSetPinAttenuation(PIEZO_PIN, ADC_11db);
  pinMode(PIEZO_PIN, INPUT);

  dht.begin();
  connectWiFi();

  lastDHTRead = millis() - DHT_INTERVAL;
  lastPiezoSample = millis() - PIEZO_INTERVAL;
  lastCloudUpdate = millis() - CLOUD_INTERVAL;
  lastDisplay = millis();
}

void loop() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED &&
      now - lastWiFiRetry >= WIFI_RETRY_INTERVAL) {
    lastWiFiRetry = now;
    Serial.println("Retrying Wi-Fi...");
    WiFi.reconnect();
  }

  if (now - lastDHTRead >= DHT_INTERVAL) {
    lastDHTRead = now;
    readEnvironment();
  }

  now = millis();

  if (now - lastPiezoSample >= PIEZO_INTERVAL) {
    lastPiezoSample = now;
    readPiezo();
  }

  calculateRisk();

  now = millis();

  if (now - lastDisplay >= DISPLAY_INTERVAL) {
    lastDisplay = now;
    printResults();
  }

  now = millis();

  if (now - lastCloudUpdate >= CLOUD_INTERVAL) {
    lastCloudUpdate = now;
    sendToThingSpeak();
  }

  delay(10);
}
