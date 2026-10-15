




#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <math.h>

const char* WIFI_SSID = "VNRVJIET_E";
const char* WIFI_PASSWORD = "vnrvjiet@123";

const char* SUPABASE_URL =
  "https://dhasmvkonmhmqaerslpo.supabase.co/functions/v1/sensor-ingest";

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
float totalRisk = 0.0;
float temperatureRisk = 0.0;
float humidityRisk = 0.0;
float vibrationRisk = 0.0;

int piezoMinimum = 0;
int piezoMaximum = 0;
int vibrationAmplitude = 0;

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
    Serial.println("\nWi-Fi connection failed.");
  }
}

void readEnvironment() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t) ||
      h < 0.0 || h > 100.0 ||
      t < -40.0 || t > 80.0) {
    Serial.println("ERROR: DHT22 reading failed.");
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
    Serial.println("VIBRATION EVENT DETECTED");
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

  totalRisk = clamp100(
      0.40 * vibrationRisk
    + 0.30 * humidityRisk
    + 0.30 * temperatureRisk
  );

  riskAlert = totalRisk > RISK_ALERT_LIMIT;
}

void printResults() {
  Serial.println("\n========== NEUROVA ==========");

  Serial.print("Wi-Fi: ");
  Serial.println(
    WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED"
  );

  Serial.print("Environment valid: ");
  Serial.println(environmentValid ? "YES" : "NO");

  if (environmentValid) {
    Serial.print("Temperature: ");
    Serial.print(temperature, 1);
    Serial.println(" C");

    Serial.print("Humidity: ");
    Serial.print(humidity, 1);
    Serial.println(" % RH");

    Serial.print("Temperature risk: ");
    Serial.print(temperatureRisk, 1);
    Serial.println(" / 100");

    Serial.print("Humidity risk: ");
    Serial.print(humidityRisk, 1);
    Serial.println(" / 100");

    Serial.print("Combined risk: ");
    Serial.print(totalRisk, 1);
    Serial.println(" / 100");

    Serial.println(
      riskAlert ? "RISK ALERT: ACTIVE" : "Risk alert: NORMAL"
    );
  }

  Serial.print("Piezo minimum: ");
  Serial.println(piezoMinimum);

  Serial.print("Piezo maximum: ");
  Serial.println(piezoMaximum);

  Serial.print("Vibration amplitude: ");
  Serial.print(vibrationAmplitude);
  Serial.println(" ADC counts");

  Serial.print("Vibration risk: ");
  Serial.print(vibrationRisk, 1);
  Serial.println(" / 100");

  if (humidityWarning)
    Serial.println("WARNING: Humidity above 65% RH");

  if (humidityHigh)
    Serial.println("HIGH HUMIDITY: Above 75% RH");

  if (temperatureWarning)
    Serial.println("WARNING: Temperature above 35 C");

  if (vibrationEventPending)
    Serial.println("Vibration event pending upload");

  Serial.println("=============================");
}

void sendToSupabase() {
  Serial.println("\n--- SUPABASE UPLOAD ---");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("UPLOAD SKIPPED: Wi-Fi disconnected.");
    Serial.print("Wi-Fi status code: ");
    Serial.println(WiFi.status());
    return;
  }

  if (!environmentValid || isnan(totalRisk)) {
    Serial.println("UPLOAD SKIPPED: Invalid DHT22 data.");
    Serial.print("Environment valid: ");
    Serial.println(environmentValid ? "YES" : "NO");
    Serial.print("Temperature: ");
    Serial.println(temperature);
    Serial.print("Humidity: ");
    Serial.println(humidity);
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);

  if (!http.begin(client, SUPABASE_URL)) {
    Serial.println("ERROR: HTTPS initialization failed.");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  String payload = "{";
  payload += "\"device_id\":\"NEUROVA-ESP32\",";
  payload += "\"temperature\":" + String(temperature, 2) + ",";
  payload += "\"humidity\":" + String(humidity, 2) + ",";
  payload += "\"vibration_amplitude\":" +
             String(vibrationAmplitude) + ",";
  payload += "\"vibration_event\":";
  payload += vibrationEventPending ? "true" : "false";
  payload += ",\"temperature_warning\":";
  payload += temperatureWarning ? "true" : "false";
  payload += ",\"humidity_warning\":";
  payload += humidityWarning ? "true" : "false";
  payload += ",\"total_risk\":" + String(totalRisk, 2) + ",";
  payload += "\"risk_alert\":";
  payload += riskAlert ? "true" : "false";
  payload += "}";

  Serial.println("Sending POST request...");
  Serial.print("Payload: ");
  Serial.println(payload);

  int responseCode = http.POST(payload);

  String response = http.getString();

  Serial.print("HTTP status: ");
  Serial.println(responseCode);

  Serial.print("Server response: ");
  Serial.println(response);

  if (responseCode >= 200 && responseCode < 300) {
    Serial.println("HTTP request accepted.");

    if (vibrationEventPending) {
      vibrationEventPending = false;
    }
  } else if (responseCode == 401 || responseCode == 403) {
    Serial.println("Check Edge Function authentication.");
  } else if (responseCode == 404) {
    Serial.println("Check Supabase endpoint and deployment.");
  } else if (responseCode >= 500) {
    Serial.println("Supabase server or database error.");
  } else if (responseCode < 0) {
    Serial.println("Network, TLS, DNS, or timeout error.");
  } else {
    Serial.println("Request was not accepted.");
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\nNEUROVA RAW PADDY MONITOR");

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
    sendToSupabase();
  }

  delay(10);
}
