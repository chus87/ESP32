**Enviar tiempo por Telegram** \--\> Envía cada hora el tiempo de la
ciudad indicada a tu bot de Telegram.

**Escanear red y enviar por Telegram** \--\> Hace un ping a toda la red
y devuelve las IP´s que hayan respondido. (La libreria necesaria, se descarga de este repo: https://github.com/dvarrel/ESPping).

**Telemetría por Telegram** \--\> Muestra datos de: IP, MAC, RSSI, Heal
libre y Uptime.

En todos hay que rellenar las siguientes constantes (Al principio de
cada sketch):

**SSID** \--\> El SSID de tu red.

**PASS** \--\> La clave de tu red wifi.

**TELEGRAM_BOT_TOKEN** \--\> Token del bot para Telegram.

**TELEGRAM_CHAT_ID **\--\> El ID del chat de bot.

Para el tiempo, hacen falta además estos 2:

**OPENWEATHER_KEY **\--\> La clave API de Openweather.

**CIUDAD** \--\> La ciudad de la que quieres saber el tiempo.
