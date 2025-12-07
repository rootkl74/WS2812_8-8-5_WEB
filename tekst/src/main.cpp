#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

// ============= НАСТРОЙКИ =============
const int MATRIX_WIDTH    = 8;
const int MATRIX_HEIGHT   = 8;
const int NUM_TILES       = 5;
const int LED_PIN         = D4;
const uint8_t BRIGHTNESS  = 60;
const int WEB_PORT        = 80;

const int SCREEN_WIDTH    = NUM_TILES * MATRIX_WIDTH;
const int NUM_PIXELS      = MATRIX_WIDTH * MATRIX_HEIGHT * NUM_TILES;

const int EEPROM_SIZE     = 1024;
const int MAX_TEXT_LEN    = 64;
const int NUM_LINES       = 10;

const unsigned long DEFAULT_SCROLL_DELAY = 150;
unsigned long SCROLL_DELAY = DEFAULT_SCROLL_DELAY;

const char* DEFAULT_SSID = "Root_HOME";
const char* DEFAULT_PASS = "rootrootroot19821609";
const char* AP_SSID = "ClockAP";
const char* AP_PASS = "12345678";
const unsigned long WIFI_TIMEOUT = 10000;

// Адреса EEPROM
const int WIFI_FLAG_ADDR = 0;
const int WIFI_SSID_ADDR = 1;
const int WIFI_PASS_ADDR = 33;
const int SETTINGS_ADDR  = 97;

int COLOR_ADDR = SETTINGS_ADDR + 4 + NUM_LINES * (1 + MAX_TEXT_LEN);
int SPEED_ADDR   = COLOR_ADDR + NUM_LINES * 3;

const int MAX_SSID_LEN = 32;
const int MAX_PASS_LEN = 64;

char savedSSID[MAX_SSID_LEN];
char savedPass[MAX_PASS_LEN];
// =====================================

Adafruit_NeoPixel strip(NUM_PIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
ESP8266WebServer server(WEB_PORT);

char lines[NUM_LINES][MAX_TEXT_LEN + 1];
uint8_t reds[NUM_LINES], greens[NUM_LINES], blues[NUM_LINES];
uint8_t brightness = BRIGHTNESS;

int currentLineIndex = 0;
int textOffset = 0;
unsigned long lastScroll = 0;
unsigned long lastChange = 0;

enum DisplayState {
  SCROLLING,
  FADE_OUT,
  CHANGE_LINE,
  FADE_IN
};

DisplayState displayState = SCROLLING;
uint8_t currentBrightness = BRIGHTNESS;

IPAddress localIP;
bool shouldServeWiFi = false;

// Прототипы
int XY(int x, int y);
void drawChar(char c, int x, uint32_t color);
void drawCyrillicCharByIndex(int idx, int x, uint32_t color);
int getCyrillicIndex(const char* str, size_t i);
void showTextFrame();
void runStartupTest();
void handleRoot();
void handleSettings();
void saveTextSettings();
void loadTextSettings();
void handleWiFiSelect();
void handleSaveWiFi();
void saveWiFiSettings();
bool loadWiFiSettings();
int getTextWidth(const char* text);
void fadeOut();
void fadeIn();

// ============ Шрифт 5x8 — цифры и символы ============
const byte font5x8[][5] PROGMEM = {
  {0x00, 0x00, 0x00, 0x00, 0x00}, // пробел
  {0x00, 0x00, 0x58, 0x00, 0x00}, // !
  {0x00, 0x00, 0x00, 0x00, 0x00}, // "
  {0x00, 0x28, 0x7E, 0x28, 0x00}, // #
  {0x00, 0x24, 0x5E, 0x52, 0x00}, // $
  {0x00, 0x60, 0x50, 0x23, 0x00}, // %
  {0x00, 0x36, 0x49, 0x26, 0x00}, // &
  {0x00, 0x40, 0x40, 0x00, 0x00}, // '
  {0x00, 0x00, 0x3C, 0x40, 0x3C}, // (
  {0x00, 0x00, 0x3C, 0x04, 0x3C}, // )
  {0x00, 0x14, 0x3E, 0x14, 0x00}, // *
  {0x00, 0x08, 0x3E, 0x08, 0x00}, // +
  {0x00, 0x00, 0x00, 0x40, 0x40}, // ,
  {0x00, 0x00, 0x08, 0x08, 0x00}, // -
  {0x00, 0x00, 0x00, 0x00, 0x60}, // .
  {0x00, 0x20, 0x10, 0x08, 0x04}, // /
  {0x7e, 0x81, 0x81, 0x81, 0x7e}, // 0
  {0x00, 0x84, 0x82, 0xff, 0x80}, // 1
  {0xe2, 0x91, 0x91, 0x91, 0x8e}, // 2
  {0x42, 0x91, 0x91, 0x91, 0x6e}, // 3
  {0x0f, 0x10, 0x10, 0x10, 0xff}, // 4
  {0x4e, 0x91, 0x91, 0x91, 0x61}, // 5
  {0x7e, 0x91, 0x91, 0x91, 0x62}, // 6
  {0x06, 0x01, 0x01, 0x01, 0xff}, // 7
  {0x76, 0x89, 0x89, 0x89, 0x76}, // 8
  {0x46, 0x89, 0x89, 0x89, 0x7e}, // 9
  {0x00, 0x00, 0x36, 0x00, 0x00}  // :
};

// ============ Шрифт 5x8 — кириллица ============
const byte cyrillic_5x8[][5] PROGMEM = {
  {0xfe, 0x21, 0x21, 0x21, 0xfe}, // А
  {0xff, 0x89, 0x89, 0x89, 0x71}, // Б
  {0xff, 0x89, 0x89, 0x89, 0x76}, // В
  {0xff, 0x01, 0x01, 0x01, 0x03}, // Г
  {0xc0, 0x3e, 0x21, 0x3f, 0xc0}, // Д
  {0xff, 0x91, 0x91, 0x91, 0x81}, // Е
  {0xe3, 0x14, 0xff, 0x14, 0xe3}, // Ж
  {0x42, 0x89, 0x89, 0x89, 0x76}, // З
  {0xff, 0x30, 0x18, 0x06, 0xff}, // И
  {0xfe, 0x60, 0x31, 0x18, 0xfe}, // Й
  {0xff, 0x08, 0x08, 0x14, 0xe3}, // К
  {0xfc, 0x02, 0x01, 0x01, 0xff}, // Л
  {0xff, 0x0e, 0x70, 0x0e, 0xff}, // М
  {0xff, 0x10, 0x10, 0x10, 0xff}, // Н
  {0x7e, 0x81, 0x81, 0x81, 0x7e}, // О
  {0xff, 0x01, 0x01, 0x01, 0xff}, // П
  {0xff, 0x11, 0x11, 0x11, 0x0e}, // Р
  {0x7e, 0x81, 0x81, 0x81, 0xc3}, // С
  {0x03, 0x01, 0xff, 0x01, 0x03}, // Т
  {0x47, 0x88, 0x88, 0x88, 0xff}, // У
  {0x1e, 0x21, 0xff, 0x21, 0x1e}, // Ф
  {0xc7, 0x28, 0x10, 0x28, 0xc7}, // Х
  {0x7f, 0x40, 0x40, 0x7f, 0xc0}, // Ц
  {0x0f, 0x10, 0x10, 0x10, 0xff}, // Ч
  {0xff, 0x80, 0xff, 0x80, 0xff}, // Ш
  {0x7f, 0x40, 0x7f, 0x40, 0xff}, // Щ
  {0x42, 0x91, 0x91, 0x91, 0xff}, // Э
  {0xff, 0x10, 0x7e, 0x81, 0x7e}, // Ю
  {0xfe, 0x93, 0x92, 0x93, 0x82}, // Я
  {0xe6, 0x19, 0x09, 0x09, 0xff}, // Ё
  {0x01, 0xff, 0x88, 0x88, 0x70}, // Ъ
  {0xff, 0x88, 0x88, 0x88, 0x70}, // Ь
  {0xff, 0x88, 0x70, 0x00, 0xff}, // ы
  {0x00, 0x42, 0x7F, 0x42, 0x7F}, // Ж (альт)
  {0x00, 0x3C, 0x04, 0x3C, 0x40}, // Э (альт)
  {0x00, 0x7F, 0x04, 0x08, 0x7F}, // Х (симм)
};

// ============ Определение кириллицы ============
int getCyrillicIndex(const char* str, size_t i) {
  unsigned char c = str[i];
  unsigned char c2 = str[i + 1];

  if (c == 0xD0) {
    if (c2 == 0x81) return 28; // Ё
    if (c2 >= 0x90 && c2 <= 0x9F) return c2 - 0x90;
    if (c2 >= 0xA0 && c2 <= 0xBF) {
      if (c2 == 0xAC) return 29; // Ъ
      if (c2 == 0x9C) return 30; // Ь
      return c2 - 0xA0 + 16;
    }
  }
  if (c == 0xD1) {
    if (c2 == 0x91) return 28; // ё
    if (c2 >= 0x80 && c2 <= 0x8F) return c2 - 0x80 + 32;
    if (c2 == 0x8B) return 31; // ы
  }
  return -1;
}

// ============ XY() — линейное подключение ============
int XY(int x, int y) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= 8) return -1;
  int tile = x / MATRIX_WIDTH;
  int local_x = x % MATRIX_WIDTH;
  int pos = y * MATRIX_WIDTH + local_x;
  return tile * 64 + pos;
}

// ============ Отрисовка символа 5x8 — БЕЗ переворота ============
void drawChar(char c, int x, uint32_t color) {
  if (c < 32 || c > 126) return;
  int idx = (c - 32);
  for (int col = 0; col < 5; col++) {
    byte bits = pgm_read_byte(&font5x8[idx][col]);
    for (int row = 0; row < 8; row++) {
      // Используем (1 << row) — бит 0 = нижняя строка
      if (bits & (1 << row)) {
        int px = x + col;
        int py = row;
        if (px >= 0 && px < SCREEN_WIDTH && py < 8) {
          strip.setPixelColor(XY(px, py), color);
        }
      }
    }
  }
}

// ============ Отрисовка кириллицы — без переворота ============
void drawCyrillicCharByIndex(int idx, int x, uint32_t color) {
  if (idx < 0 || idx >= 32) return;
  for (int col = 0; col < 5; col++) {
    byte bits = pgm_read_byte(&cyrillic_5x8[idx][col]);
    for (int row = 0; row < 8; row++) {
      if (bits & (1 << row)) {
        int px = x + col;
        int py = row;
        if (px >= 0 && px < SCREEN_WIDTH && py < 8) {
          strip.setPixelColor(XY(px, py), color);
        }
      }
    }
  }
}

// ============ Реальная ширина текста ============
int getTextWidth(const char* text) {
  int width = 0;
  size_t len = strlen(text);
  for (size_t i = 0; i < len; i++) {
    int idx = getCyrillicIndex(text, i);
    if (idx != -1) {
      width += 6;
      i++;
    } else {
      width += 6;
    }
  }
  return width;
}

// ============ Плавное затемнение ============
void fadeOut() {
  if (currentBrightness > 0) {
    currentBrightness--;
    strip.setBrightness(currentBrightness);
    strip.show();
  }
}

// ============ Плавное включение ============
void fadeIn() {
  if (currentBrightness < brightness) {
    currentBrightness++;
    strip.setBrightness(currentBrightness);
    strip.show();
  }
}

// ============ Показ текста ============
void showTextFrame() {
  strip.clear();
  const char* text = lines[currentLineIndex];
  uint32_t color = strip.Color(reds[currentLineIndex], greens[currentLineIndex], blues[currentLineIndex]);
  int x = -textOffset;

  size_t len = strlen(text);
  for (size_t i = 0; i < len; i++) {
    int idx = getCyrillicIndex(text, i);
    if (idx != -1) {
      drawCyrillicCharByIndex(idx, x, color);
      x += 6;
      i++;
    } else {
      drawChar(text[i], x, color);
      x += 6;
    }
  }
  strip.show();
}

// ============ Тест при старте ============
void runStartupTest() {
  Serial.println("🔹 Тест: точка по центру");
  strip.clear();
  for (int x = 0; x < SCREEN_WIDTH; x++) {
    strip.setPixelColor(XY(x, 3), strip.Color(255, 255, 255));
    strip.show();
    delay(80);
    strip.clear();
  }
  Serial.println("🔹 Тест завершён.");
}

// ============ Работа с EEPROM ============
void saveTextSettings() {
  int addr = SETTINGS_ADDR;
  EEPROM.write(addr++, reds[0]);
  EEPROM.write(addr++, greens[0]);
  EEPROM.write(addr++, blues[0]);
  EEPROM.write(addr++, brightness);

  for (int i = 0; i < NUM_LINES; i++) {
    size_t len = strlen(lines[i]);
    EEPROM.write(addr++, len);
    for (size_t j = 0; j < len; j++) {
      EEPROM.write(addr++, lines[i][j]);
    }
  }

  addr = COLOR_ADDR;
  for (int i = 0; i < NUM_LINES; i++) {
    EEPROM.write(addr++, reds[i]);
    EEPROM.write(addr++, greens[i]);
    EEPROM.write(addr++, blues[i]);
  }

  EEPROM.write(SPEED_ADDR, (uint8_t)SCROLL_DELAY);
  EEPROM.commit();
}

void loadTextSettings() {
  int addr = SETTINGS_ADDR;
  reds[0] = EEPROM.read(addr++);
  greens[0] = EEPROM.read(addr++);
  blues[0] = EEPROM.read(addr++);
  brightness = EEPROM.read(addr++);
  if (brightness == 0) brightness = BRIGHTNESS;
  strip.setBrightness(brightness);
  currentBrightness = brightness;

  for (int i = 0; i < NUM_LINES; i++) {
    int len = EEPROM.read(addr++);
    for (int j = 0; j < len; j++) {
      lines[i][j] = EEPROM.read(addr++);
    }
    lines[i][len] = '\0';
  }

  addr = COLOR_ADDR;
  for (int i = 0; i < NUM_LINES; i++) {
    reds[i] = EEPROM.read(addr++);
    greens[i] = EEPROM.read(addr++);
    blues[i] = EEPROM.read(addr++);
  }

  SCROLL_DELAY = EEPROM.read(SPEED_ADDR);
  if (SCROLL_DELAY == 0 || SCROLL_DELAY > 1000) SCROLL_DELAY = DEFAULT_SCROLL_DELAY;

  bool hasText = false;
  for (int i = 0; i < NUM_LINES; i++) {
    if (strlen(lines[i]) > 0) hasText = true;
  }

  if (!hasText) {
    strcpy(lines[0], "0123456789");
    strcpy(lines[1], "АБВГДЕЖЗИЙ");
    strcpy(lines[2], "КЛМН ОПРСТУ");
    strcpy(lines[3], "ФХЦЧШЩЪЫЬЭ");
    strcpy(lines[4], "Ю Я Ё");
    saveTextSettings();
  }

  if (reds[0] == 0 && greens[0] == 0 && blues[0] == 0) {
    for (int i = 0; i < NUM_LINES; i++) {
      reds[i] = 255 * (i % 3 == 0);
      greens[i] = 255 * (i % 3 == 1);
      blues[i] = 255 * (i % 3 == 2);
    }
  }
}

bool loadWiFiSettings() {
  if (EEPROM.read(WIFI_FLAG_ADDR) != 1) return false;
  int len = EEPROM.read(WIFI_SSID_ADDR);
  if (len == 0 || len >= MAX_SSID_LEN) return false;
  for (int i = 0; i < len; i++) {
    savedSSID[i] = EEPROM.read(WIFI_SSID_ADDR + 1 + i);
  }
  savedSSID[len] = '\0';

  len = EEPROM.read(WIFI_PASS_ADDR);
  if (len > MAX_PASS_LEN) len = MAX_PASS_LEN;
  for (int i = 0; i < len; i++) {
    savedPass[i] = EEPROM.read(WIFI_PASS_ADDR + 1 + i);
  }
  savedPass[len] = '\0';
  return true;
}

void saveWiFiSettings() {
  EEPROM.write(WIFI_FLAG_ADDR, 1);
  int len = strlen(savedSSID);
  EEPROM.write(WIFI_SSID_ADDR, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(WIFI_SSID_ADDR + 1 + i, savedSSID[i]);
  }

  len = strlen(savedPass);
  EEPROM.write(WIFI_PASS_ADDR, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(WIFI_PASS_ADDR + 1 + i, savedPass[i]);
  }
  EEPROM.commit();
}

// ============ Веб-интерфейс ============
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="UTF-8">
  <title>5x8 Шрифт</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body { font-family: Arial; text-align: center; margin: 20px; background: #f9f9f9; }
    .section { margin: 15px; text-align: left; display: inline-block; background: white; padding: 15px; border-radius: 8px; }
    input, button { padding: 8px; margin: 3px; width: 120px; }
    .color-input { width: 60px; }
    .text-input { width: 280px; }
    button { background: #1B5E20; color: white; border: none; cursor: pointer; }
  </style>
</head>
<body>
  <h1>🪧 5×8 Шрифт</h1>
  <p><strong>IP:</strong> )rawliteral" + localIP.toString() + R"rawliteral(</p>

  <div class="section">
    <h2>💡 Яркость</h2>
    <input type="range" id="bright" min="0" max="255" value=")rawliteral" + String(brightness) + R"rawliteral(" oninput="document.getElementById('bval').textContent=this.value">
    <span id="bval">)rawliteral" + String(brightness) + R"rawliteral(</span>
    <button onclick="setBrightness()">Установить</button>
  </div>

  <div class="section">
    <h2>⏱️ Скорость</h2>
    <input type="range" id="speed" min="50" max="500" value=")rawliteral" + String(SCROLL_DELAY) + R"rawliteral(" oninput="document.getElementById('sval').textContent=this.value">
    <span id="sval">)rawliteral" + String(SCROLL_DELAY) + R"rawliteral(</span>
    <button onclick="setSpeed()">Сохранить</button>
  </div>

  <div>)rawliteral";

  for (int i = 0; i < NUM_LINES; i++) {
    html += "<div class='section'><h2>📜 Строка " + String(i+1) + "</h2>";
    html += "<input type='text' class='text-input' id='line" + String(i+1) + "' value='";
    html += String(lines[i]).c_str();
    html += "'><br>";
    html += "R: <input type='number' class='color-input' id='r" + String(i+1) + "' value='" + String(reds[i]) + "'>";
    html += "G: <input type='number' class='color-input' id='g" + String(i+1) + "' value='" + String(greens[i]) + "'>";
    html += "B: <input type='number' class='color-input' id='b" + String(i+1) + "' value='" + String(blues[i]) + "'>";
    html += "<button onclick=\"saveLine(" + String(i+1) + ")\">Сохранить</button></div>";
  }

  html += R"rawliteral(
  </div>
  <script>
    function setBrightness() { fetch('/settings?brightness='+d('bright')); }
    function setSpeed() { fetch('/settings?speed='+d('speed')); }
    function saveLine(n) {
      fetch('/settings?line'+n+'='+encodeURIComponent(d('line'+n)) +
            '&r'+n+'='+d('r'+n)+'&g'+n+'='+d('g'+n)+'&b'+n+'='+d('b'+n));
    }
    function d(id) { return document.getElementById(id).value; }
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleSettings() {
  if (server.hasArg("brightness")) {
    brightness = server.arg("brightness").toInt();
    brightness = constrain(brightness, 0, 255);
    strip.setBrightness(brightness);
    currentBrightness = brightness;
  }

  if (server.hasArg("speed")) {
    SCROLL_DELAY = server.arg("speed").toInt();
    SCROLL_DELAY = constrain(SCROLL_DELAY, 50, 500);
  }

  for (int i = 1; i <= NUM_LINES; i++) {
    String argName = "line" + String(i);
    if (server.hasArg(argName)) {
      String newText = server.arg(argName);
      if (newText.length() > MAX_TEXT_LEN) {
        newText = newText.substring(0, MAX_TEXT_LEN);
      }
      newText.toCharArray(lines[i-1], MAX_TEXT_LEN + 1);
    }
    if (server.hasArg("r" + String(i))) reds[i-1] = server.arg("r" + String(i)).toInt();
    if (server.hasArg("g" + String(i))) greens[i-1] = server.arg("g" + String(i)).toInt();
    if (server.hasArg("b" + String(i))) blues[i-1] = server.arg("b" + String(i)).toInt();
  }

  saveTextSettings();
  textOffset = -SCREEN_WIDTH;
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleWiFiSelect() {
  if (WiFi.scanComplete() == -2) WiFi.scanNetworks(true);
  int n = WiFi.scanComplete();

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<body>
  <h1>📶 Выберите сеть</h1>
  <form action="/save-wifi" method="post">
    <select name="ssid">)rawliteral";

  if (n == -2) html += "<option>Сканирование...</option>";
  else if (n == 0) html += "<option>Нет сетей</option>";
  else {
    for (int i = 0; i < n; i++) {
      html += "<option>" + WiFi.SSID(i) + "</option>";
    }
  }

  html += R"rawliteral(
    </select><br>
    <input type="password" name="pass" placeholder="Пароль"><br>
    <button type="submit">Подключиться</button>
  </form>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleSaveWiFi() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    server.arg("ssid").toCharArray(savedSSID, MAX_SSID_LEN);
    server.arg("pass").toCharArray(savedPass, MAX_PASS_LEN);
    saveWiFiSettings();
    server.send(200, "text/html", "<h1>✅ Подключаемся...</h1>");
    delay(1000);
    ESP.restart();
  } else {
    server.send(200, "text/html", "<h2>❌ Ошибка</h2>");
  }
}

// ============ setup и loop ============
void setup() {
  strip.begin();
  strip.show();
  EEPROM.begin(EEPROM_SIZE);

  Serial.begin(115200);
  Serial.println("\n🚀 5x8 Шрифт — старт");

  if (loadWiFiSettings()) {
    Serial.print("Сеть: ");
    Serial.println(savedSSID);
  } else {
    strcpy(savedSSID, DEFAULT_SSID);
    strcpy(savedPass, DEFAULT_PASS);
  }

  WiFi.begin(savedSSID, savedPass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    localIP = WiFi.localIP();
    Serial.println("\n✅ IP: " + localIP.toString());
    loadTextSettings();
    runStartupTest();
    server.on("/", handleRoot);
    server.on("/settings", handleSettings);
    server.begin();
    lastScroll = millis();
    textOffset = -SCREEN_WIDTH;
    currentBrightness = brightness;
    return;
  }

  Serial.println("\n❌ AP Mode");
  WiFi.softAP(AP_SSID, AP_PASS);
  localIP = WiFi.softAPIP();
  server.on("/", handleWiFiSelect);
  server.on("/save-wifi", HTTP_POST, handleSaveWiFi);
  server.begin();
  shouldServeWiFi = true;
}

void loop() {
  if (shouldServeWiFi) {
    server.handleClient();
    delay(10);
    return;
  }

  server.handleClient();

  unsigned long now = millis();

  switch (displayState) {
    case SCROLLING:
      if (now - lastScroll >= SCROLL_DELAY) {
        showTextFrame();
        textOffset++;

        const char* text = lines[currentLineIndex];
        int visibleEnd = getTextWidth(text) + 2;

        if (textOffset > visibleEnd) {
          displayState = FADE_OUT;
          lastChange = now;
        }
        lastScroll = now;
      }
      break;

    case FADE_OUT:
      fadeOut();
      if (currentBrightness == 0 || now - lastChange > 2) {
        displayState = CHANGE_LINE;
        lastChange = now;
      }
      delay(2);
      break;

    case CHANGE_LINE:
      currentLineIndex = (currentLineIndex + 1) % NUM_LINES;
      textOffset = -SCREEN_WIDTH;
      displayState = FADE_IN;
      lastChange = now;
      break;

    case FADE_IN:
      fadeIn();
      if (currentBrightness >= brightness || now - lastChange > 2) {
        displayState = SCROLLING;
        lastScroll = now;
      }
      delay(2);
      break;
  }

  delay(1);
}
