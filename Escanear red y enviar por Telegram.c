#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPping.h>     // ESPping library

const char* SSID = "SSID";
// Si tu contraseña contiene una barra invertida '\' -> usa doble '\\'
const char* PASS = "PASS";
const char* TELEGRAM_BOT_TOKEN = "TOKEN"; // pon aquí el token nuevo
const long   TELEGRAM_CHAT_ID  = CHAT_ID; 
// ------------------------------------------------

WiFiClientSecure tlsClient;
unsigned long lastTelegramCheck = 0;
const unsigned long TELEGRAM_POLL_INTERVAL = 3000; // ms
long lastUpdateId = 0; // offset para getUpdates

// CONTROL DE ESCANEOS
volatile bool scanning = false;        // bloqueo para evitar escaneos concurrentes
bool scannedOnConnect = false;         // para evitar re-escaneo al reconectar varias veces
unsigned long lastScanMillis = 0;
const unsigned long MIN_SCAN_INTERVAL_MS = 60UL * 1000UL; // cooldown mínimo entre escaneos (60s)

// helpers para IP <-> uint32
uint32_t ipToUint32(const IPAddress &ip) {
  return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
}
IPAddress uint32ToIP(uint32_t v) {
  return IPAddress((uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v);
}

// URL-encode (básico)
String urlEncode(const String &str) {
  String encoded = "";
  char c;
  char buf[5];
  for (size_t i = 0; i < str.length(); i++) {
    c = str[i];
    if ( (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

// Enviar mensaje simple a Telegram
bool telegramSendMessage(const String &text) {
  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN +
               "/sendMessage?chat_id=" + String(TELEGRAM_CHAT_ID) + "&text=" + urlEncode(text);

  tlsClient.setInsecure(); // no validar cert (más simple). Considera usar CA si quieres validar.
  http.begin(tlsClient, url);
  int code = http.GET();
  String payload = http.getString();
  http.end();
  return (code >= 200 && code < 300);
}

// Lanza escaneo de la subred obteniendo máscara desde WiFi
// Nota: función bloqueante (como antes). Está protegida por 'scanning' y por cooldown.
void scanSubnetAndNotify() {
  // Si ya está en escaneo, no hacemos nada.
  if (scanning) return;

  // cooldown
  unsigned long now = millis();
  if ((now - lastScanMillis) < MIN_SCAN_INTERVAL_MS) {
    unsigned long waitMs = MIN_SCAN_INTERVAL_MS - (now - lastScanMillis);
    telegramSendMessage("Espera " + String((waitMs + 999) / 1000) + " s antes de volver a escanear.");
    return;
  }

  scanning = true;
  lastScanMillis = now;

  IPAddress localIP = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();

  uint32_t ip32 = ipToUint32(localIP);
  uint32_t mask32 = ipToUint32(mask);
  uint32_t net32 = ip32 & mask32;

  // calcular bits host
  uint8_t hostBits = 0;
  for (int i = 0; i < 32; ++i) {
    if (((mask32 >> i) & 1) == 0) hostBits++;
  }
  uint32_t hosts = 0;
  if (hostBits == 0) {
    hosts = 1;
  } else if (hostBits > 16) {
    // limitar la cantidad máxima a /16 para seguridad/tiempo
    hosts = (1UL << 16) - 2;
  } else {
    hosts = (1UL << hostBits) - 2; // excluye network y broadcast
  }
  if (hosts == 0) hosts = 1;

  // Mensaje inicial de aviso
  telegramSendMessage("Iniciando escaneo. IP=" + localIP.toString() + " máscara=" + mask.toString() +
                      " -> " + String(hosts) + " hosts (máx).");

  String aliveList = "";
  uint32_t maxToScan = hosts;

  for (uint32_t i = 1; i <= maxToScan; ++i) {
    uint32_t target32 = net32 + i;
    IPAddress target = uint32ToIP(target32);
    if (target == localIP) continue; // saltar propia IP
    bool ok = Ping.ping(target, 1); // 1 intento
    if (ok) {
      if (aliveList.length() > 0) aliveList += ", ";
      aliveList += target.toString();
      // si la lista se hace enorme podemos enviar parcialmente cada X entradas (opcional)
      if (aliveList.length() > 800) {
        telegramSendMessage("Hosts vivos (parcial): " + aliveList);
        aliveList = "";
      }
    }
  }

  if (aliveList.length() == 0) {
    telegramSendMessage("Escaneo completado: ningún host respondió al ping.");
  } else {
    telegramSendMessage("Escaneo completado. Hosts vivos: " + aliveList);
  }

  scanning = false;
}

// Comprobar Telegram /getUpdates para recibir comandos simples
void checkTelegramForCommands() {
  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN +
               "/getUpdates?offset=" + String(lastUpdateId + 1) + "&limit=5&timeout=0";

  tlsClient.setInsecure();
  http.begin(tlsClient, url);
  int code = http.GET();
  if (code <= 0) { http.end(); return; }
  String payload = http.getString();
  http.end();

  if (payload.indexOf("\"result\":[]") != -1) return;

  // Buscamos cada "update_id" y procesamos; la implementación es simple pero robusta para varios resultados.
  int idx = 0;
  while (true) {
    int pu = payload.indexOf("\"update_id\":", idx);
    if (pu == -1) break;
    pu += strlen("\"update_id\":");
    long updateId = 0;
    while (pu < (int)payload.length() && isDigit(payload[pu])) {
      updateId = updateId * 10 + (payload[pu] - '0');
      pu++;
    }
    if (updateId <= lastUpdateId) {
      idx = pu;
      continue;
    }
    lastUpdateId = updateId;

    // buscar chat.id cercano a este update
    int pc = payload.indexOf("\"chat\":{\"id\":", pu);
    if (pc == -1) {
      idx = pu;
      continue;
    }
    pc += strlen("\"chat\":{\"id\":");
    long chatId = 0;
    bool neg = false;
    // leer posible signo -
    if (payload[pc] == '-') { neg = true; pc++; }
    while (pc < (int)payload.length() && isDigit(payload[pc])) {
      chatId = chatId * 10 + (payload[pc] - '0');
      pc++;
    }
    if (neg) chatId = -chatId;

    // extraer texto del mensaje (si existe)
    int pt = payload.indexOf("\"text\":\"", pc);
    if (pt == -1) {
      idx = pu;
      continue;
    }
    pt += strlen("\"text\":\"");
    String text = "";
    while (pt < (int)payload.length()) {
      char c = payload[pt++];
      if (c == '"') break;
      if (c == '\\' && pt < (int)payload.length()) {
        char e = payload[pt++];
        if (e == 'n') text += '\n';
        else if (e == 'r') text += '\r';
        else if (e == 't') text += '\t';
        else if (e == '"') text += '"';
        else if (e == '\\') text += '\\';
        else { text += e; }
      } else {
        text += c;
      }
    }
    String cmd = text;
    cmd.toLowerCase();
    cmd.trim();

    // Solo responder si viene del chat esperado
    if (chatId == TELEGRAM_CHAT_ID) {
      if (cmd == "escanear" || cmd == "/escanear") {
        // Si ya hay un escaneo en curso o estamos en cooldown, respondemos en lugar de iniciar otro.
        unsigned long now = millis();
        if (scanning) {
          telegramSendMessage("Ya estoy escaneando — espera a que termine.");
        } else if ((now - lastScanMillis) < MIN_SCAN_INTERVAL_MS) {
          unsigned long waitSec = (MIN_SCAN_INTERVAL_MS - (now - lastScanMillis) + 999) / 1000;
          telegramSendMessage("Demasiado pronto. Espera " + String(waitSec) + " s antes del próximo escaneo.");
        } else {
          telegramSendMessage("Comando recibido: lanzando escaneo...");
          scanSubnetAndNotify();
        }
      } else {
        telegramSendMessage("Comando desconocido. Envía 'escanear' o '/escanear' para lanzar el escaneo.");
      }
    }

    idx = pu;
  }
}

void setup() {
  // NO Serial por petición del usuario

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);

  // esperar a conexión (bloqueante mínimo)
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }

  // una vez conectado, enviar aviso (solo una vez) y lanzar escaneo inicial
  telegramSendMessage("Dispositivo conectado. IP: " + WiFi.localIP().toString());

  if (!scannedOnConnect) {
    scannedOnConnect = true;
    scanSubnetAndNotify();
  }
}

void loop() {
  unsigned long now = millis();
  if (now - lastTelegramCheck >= TELEGRAM_POLL_INTERVAL) {
    lastTelegramCheck = now;
    checkTelegramForCommands();
  }
  // loop ligero: no hacemos más (evitamos lanzar escaneos periódicos automáticos)
}

