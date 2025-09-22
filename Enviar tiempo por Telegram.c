// Sketch para ESP32-C3: OpenWeather -> Telegram
// Requisitos: ArduinoJson (instalar en Library Manager)

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ========== CONFIG (RELLENA) ==========
const char* SSID = "SSID";
// Si tu contraseña contiene una barra invertida '\' -> usa doble '\\'
const char* PASS = "PASS";
const char* TELEGRAM_BOT_TOKEN = "TOKEN"; // pon aquí el token nuevo
const long   TELEGRAM_CHAT_ID  = CHAT_ID;                 // pon aquí tu chat_id numérico
const char* OPENWEATHER_KEY = "KEY";
const char* CIUDAD = "Madrid,ES";
const unsigned long INTERVAL_MS = 60UL * 60UL * 1000UL; // 1 hora
bool use_insecure = true; // en desarrollo true; en producción usa setCACert()
// =======================================

// ----------------- utility: urlencode (declarada antes de usar) -----------------
String urlencode(const String &str) {
  String encoded = "";
  char buf[4];
  for (size_t i = 0; i < str.length(); i++) {
    char c = str[i];
    if ( ('a' <= c && c <= 'z') ||
         ('A' <= c && c <= 'Z') ||
         ('0' <= c && c <= '9') ) {
      encoded += c;
    } else if (c == ' ') {
      encoded += '+';
    } else {
      sprintf(buf, "%%%02X", (uint8_t)c);
      encoded += buf;
    }
  }
  return encoded;
}

// ----------------- send Telegram (usa urlencode) -----------------
bool sendTelegramMessage(const String &text) {
  WiFiClientSecure client;
  if (use_insecure) client.setInsecure();
  HTTPClient https;
  String url = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN + "/sendMessage";
  if (!https.begin(client, url)) {
    Serial.println("Telegram: HTTPS begin failed");
    return false;
  }
  https.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "chat_id=" + String(TELEGRAM_CHAT_ID) + "&text=" + urlencode(text);
  int code = https.POST(body);
  String resp = https.getString();
  Serial.printf("Telegram code=%d resp=%s\n", code, resp.c_str());
  https.end();
  return (code == 200 || code == 201);
}

// ----------------- HTTP GET helper -----------------
String httpGet(const char* host, const char* pathQuery) {
  WiFiClientSecure client;
  if (use_insecure) client.setInsecure();
  HTTPClient https;
  String url = String("https://") + host + pathQuery;
  if (!https.begin(client, url)) {
    Serial.println("HTTPS begin failed");
    return "";
  }
  int httpCode = https.GET();
  String payload = "";
  if (httpCode > 0) {
    payload = https.getString();
  } else {
    Serial.printf("GET failed, error: %s\n", https.errorToString(httpCode).c_str());
  }
  https.end();
  return payload;
}

unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Inicio ESP32-C3 Weather->Telegram (corregido)");
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);
  Serial.print("Conectando WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    Serial.print('.');
    delay(300);
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi conectado.");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("ERROR: no conectado a WiFi.");
  }
}

void loop() {
  unsigned long now = millis();
  if (now - lastSend < INTERVAL_MS) {
    delay(200);
    return;
  }

  String path = String("/data/2.5/weather?q=") + CIUDAD + "&units=metric&lang=es&appid=" + OPENWEATHER_KEY;
  String body = httpGet("api.openweathermap.org", path.c_str());
  if (body.length() == 0) {
    Serial.println("ERROR: respuesta vacía de OpenWeather.");
    lastSend = now;
    return;
  }

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("JSON parse error: "); Serial.println(err.c_str());
    lastSend = now;
    return;
  }

  const char* name = doc["name"] | "??";
  float temp = doc["main"]["temp"] | 0.0;
  float feels = doc["main"]["feels_like"] | 0.0;
  int humidity = doc["main"]["humidity"] | 0;
  const char* desc = doc["weather"][0]["description"] | "sin datos";

  String mensaje = String("Tiempo en ") + name + ": " + desc + ". ";
  mensaje += String("T=") + String(temp,1) + "°C (sensación " + String(feels,1) + "°C). ";
  mensaje += "Humedad " + String(humidity) + "%.";

  Serial.println("-> " + mensaje);
  if (sendTelegramMessage(mensaje)) {
    Serial.println("Enviado OK");
  } else {
    Serial.println("Fallo envío Telegram");
  }

  lastSend = now;
}

