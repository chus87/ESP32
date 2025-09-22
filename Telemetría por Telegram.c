// sketch_sep21a_complete.ino
// Telemetría periódica + control vía Telegram (solo tu chat_id)


const char* SSID = "SSID";
// Si tu contraseña contiene una barra invertida '\' -> usa doble '\\'
const char* PASS = "PASS";
const char* TELEGRAM_BOT_TOKEN = "TOKEN"; // pon aquí el token nuevo
const long   TELEGRAM_CHAT_ID  = CHAT_ID; 
// --------------------------------------------------

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>      // >= v7
#include <Preferences.h>

// Ajustes por defecto
#define DEFAULT_TELEM_INTERVAL_MS  (10UL * 60UL * 1000UL) // 10 min por defecto

Preferences prefs;

unsigned long telemIntervalMs = DEFAULT_TELEM_INTERVAL_MS;
unsigned long lastTelemSent = 0;
long lastUpdateId = 0;
bool telemEnabled = true;

unsigned long lastPollMs = 0;
const unsigned long POLL_PERIOD_MS = 2000UL; // cada 2s mira updates (no abuses)

// --- helpers ---
String urlEncode(const String &str) {
  String encoded = "";
  char c;
  const char *s = str.c_str();
  for (size_t i = 0; i < str.length(); ++i) {
    c = s[i];
    if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += '+';
    } else {
      char buf[5];
      sprintf(buf, "%%%.2X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

void connectWiFi() {
  Serial.printf("Conectando a %s ...\n", SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 20000) { // 20s timeout
      Serial.println("\nNo se pudo conectar en 20s. Reintentando...");
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(SSID, PASS);
      start = millis();
    }
  }
  Serial.println();
  Serial.print("WiFi OK. IP: ");
  Serial.println(WiFi.localIP());
}

// sendMessage usando HTTPS (WiFiClientSecure). setInsecure() para evitar problemas de certificados
bool sendTelegramMessage(const String &text) {
  WiFiClientSecure client;
  client.setInsecure(); // evita validación de certificado (útil para microcontrolador)
  HTTPClient http;

  String url = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN + "/sendMessage";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "chat_id=" + String(TELEGRAM_CHAT_ID) + "&text=" + urlEncode(text);

  int code = http.POST(body);
  if (code <= 0) {
    Serial.printf("sendTelegramMessage: fallo POST (%d)\n", code);
    http.end();
    return false;
  }
  // opcional: puedes parsear la respuesta y comprobar ok:true
  String resp = http.getString();
  Serial.printf("sendTelegramMessage: HTTP %d, resp len %u\n", code, (unsigned)resp.length());
  http.end();
  return (code == 200 || code == 201);
}

void sendStatusTelegram(long toChatId = TELEGRAM_CHAT_ID) {
  // Construye mensaje de estado
  String msg;
  msg += "📡 Telemetría - Estado\n";
  msg += "IP: " + WiFi.localIP().toString() + "\n";
  msg += "MAC: " + WiFi.macAddress() + "\n";
  msg += "RSSI: " + String(WiFi.RSSI()) + " dBm\n";
  msg += "Heap libre: " + String(ESP.getFreeHeap()) + " bytes\n";
  msg += "Uptime: " + String(millis() / 1000) + " s\n";
  msg += "Intervalo telem: " + String(telemIntervalMs / 1000) + " s\n";
  msg += "Telem activa: " + String(telemEnabled ? "SI" : "NO") + "\n";
  sendTelegramMessage(msg);
}

void persistAll() {
  // Guarda interval, lastTelemSent, lastUpdateId, enabled
  prefs.putULong("interval", telemIntervalMs);
  prefs.putULong("last_telem", lastTelemSent);
  prefs.putLong("last_update", lastUpdateId);
  prefs.putUInt("enabled", telemEnabled ? 1 : 0);
}

void handleCommand(const String &cmd, long from_id, const String &fromName) {
  // Sólo ejecuta si viene del usuario permitido
  if (from_id != TELEGRAM_CHAT_ID) {
    Serial.printf("Comando recibido desde %ld. Ignorado (no autorizado).\n", from_id);
    // opcional: avisar que no estás autorizado -> NO lo hacemos por privacidad
    return;
  }

  Serial.printf("handleCommand: '%s' from %ld (%s)\n", cmd.c_str(), from_id, fromName.c_str());

  if (cmd.startsWith("/status")) {
    sendStatusTelegram();
    return;
  }

  if (cmd.startsWith("/help")) {
    String h = "Comandos validos:\n";
    h += "/status - estado ahora\n";
    h += "/setinterval <segundos> - intervalo telemetria\n";
    h += "/starttelemetry - arrancar envio periodico\n";
    h += "/stoptelemetry - parar envio periodico\n";
    h += "/help - mostrar esto\n";
    sendTelegramMessage(h);
    return;
  }

  if (cmd.startsWith("/setinterval")) {
    // formato: /setinterval 60
    int sp = cmd.indexOf(' ');
    if (sp < 0) {
      sendTelegramMessage("Uso: /setinterval <segundos>");
      return;
    }
    String num = cmd.substring(sp + 1);
    unsigned long secs = num.toInt();
    if (secs < 1) {
      sendTelegramMessage("Intervalo invalido. Debe ser >= 1 s.");
      return;
    }
    telemIntervalMs = secs * 1000UL;
    prefs.putULong("interval", telemIntervalMs);
    sendTelegramMessage("Intervalo actualizado a " + String(secs) + " s.");
    return;
  }

  if (cmd.startsWith("/starttelemetry")) {
    telemEnabled = true;
    prefs.putUInt("enabled", 1);
    sendTelegramMessage("Telemetria ACTIVADA.");
    return;
  }

  if (cmd.startsWith("/stoptelemetry")) {
    telemEnabled = false;
    prefs.putUInt("enabled", 0);
    sendTelegramMessage("Telemetria PARADA.");
    return;
  }

  // si no reconoció
  sendTelegramMessage("Comando desconocido. /help para ayuda.");
}

// Llama a getUpdates y procesa sólo mensajes autorizados
void pollTelegramUpdates() {
  // usa offset para no procesar updates antiguos
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN + "/getUpdates?timeout=0";
  if (lastUpdateId != 0) {
    url += "&offset=" + String(lastUpdateId + 1);
  }
  http.begin(client, url);
  int code = http.GET();
  if (code <= 0) {
    // fallo de red
    // Serial.printf("getUpdates fallo HTTP %d\n", code);
    http.end();
    return;
  }
  String payload = http.getString();
  http.end();

  // parsear JSON
  StaticJsonDocument<6144> doc; // ajusta si necesitas más
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("deserializeJson fallo: ");
    Serial.println(err.c_str());
    return;
  }
  if (!doc.containsKey("ok") || !doc["ok"].as<bool>()) {
    // respuesta no ok
    // Serial.println("Telegram no ok");
    return;
  }
  JsonArray results = doc["result"].as<JsonArray>();
  for (JsonObject upd : results) {
    long update_id = upd["update_id"].as<long>();
    // guarda offset para no reprocesar
    if (update_id > lastUpdateId) {
      lastUpdateId = update_id;
      prefs.putLong("last_update", lastUpdateId);
    }

    if (!upd.containsKey("message")) continue;
    JsonObject message = upd["message"].as<JsonObject>();

    // identifica remitente
    if (!message.containsKey("from")) continue;
    JsonObject from = message["from"].as<JsonObject>();
    long from_id = from["id"].as<long>();
    String fromName = "";
    if (from.containsKey("username")) fromName = String(from["username"].as<const char*>());
    else if (from.containsKey("first_name")) fromName = String(from["first_name"].as<const char*>());

    // texto
    if (!message.containsKey("text")) continue;
    String text = String(message["text"].as<const char*>());

    // sólo procesa si viene del tu chat id
    if (from_id != TELEGRAM_CHAT_ID) {
      Serial.printf("Update de %ld ignorado (no autorizado)\n", from_id);
      continue;
    }

    // procesar comando
    handleCommand(text, from_id, fromName);
  }
}

void maybeSendTelemetry() {
  if (!telemEnabled) return;
  unsigned long now = millis();
  // cuidado overflow: usamos diferencias con unsigned long
  if ((now - lastTelemSent) < telemIntervalMs) return;
  // construir telemetria
  String msg;
  msg += "📊 Telemetria periódica\n";
  msg += "IP: " + WiFi.localIP().toString() + "\n";
  msg += "MAC: " + WiFi.macAddress() + "\n";
  msg += "RSSI: " + String(WiFi.RSSI()) + " dBm\n";
  msg += "Heap libre: " + String(ESP.getFreeHeap()) + " bytes\n";
  msg += "Uptime: " + String(now / 1000) + " s\n";
  sendTelegramMessage(msg);

  lastTelemSent = now;
  prefs.putULong("last_telem", lastTelemSent);
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("=== Telemetria Telegram (ESP32) ===");

  // Preferences namespace "telemetry"
  prefs.begin("telemetry", false);
  telemIntervalMs = prefs.getULong("interval", DEFAULT_TELEM_INTERVAL_MS);
  lastTelemSent = prefs.getULong("last_telem", 0);
  lastUpdateId = prefs.getLong("last_update", 0);
  telemEnabled = (prefs.getUInt("enabled", 1) == 1);

  connectWiFi();

  // mandar un mensaje de inicio (opcional)
  sendTelegramMessage(String("Device arrancado. IP: ") + WiFi.localIP().toString());

  // enviar un status inicial
  sendStatusTelegram();
}

void loop() {
  unsigned long now = millis();

  // Poll Telegram periódicamente
  if (now - lastPollMs >= POLL_PERIOD_MS) {
    lastPollMs = now;
    pollTelegramUpdates();
  }

  // Telemetría periódica
  maybeSendTelemetry();

  // trabajo ligero
  delay(200);
}

