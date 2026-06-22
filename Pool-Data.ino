// ═══════════════════════════════════════════════════════════
//  Pool Data — ESP32 D1 Mini
//  Voir CHANGELOG.md pour l'historique complet
// ═══════════════════════════════════════════════════════════
#define FW_VERSION "v1.4"

#ifndef ARDUINO_ARCH_ESP32
  #error "Board incorrect — sélectionner : Tools > Board > ESP32 Dev Module"
#endif

#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <Adafruit_BME280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TFT_eSPI.h>  // FreeSans9pt7b / FreeSansBold12/18/24pt7b inclus via gfxfont.h
#include <esp_task_wdt.h>
#include <esp_system.h>   // esp_reset_reason()
#include <ArduinoOTA.h>
#include <Preferences.h>
#include "secrets.h"

// ── WiFi / NTP ──────────────────────────────────────────────
const char* WIFI_SSID = SECRET_SSID;
const char* WIFI_PASS = SECRET_PASSWORD;
#define TZ_STR  "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP1    "pool.ntp.org"
#define NTP2    "time.google.com"
#define NTP3    "time.cloudflare.com"
const unsigned long WIFI_TIMEOUT      = 30000UL;
const unsigned long NTP_TIMEOUT       = 20000UL;
const unsigned long NTP_SYNC_INTERVAL = 3600000UL;

// ── ThingSpeak ───────────────────────────────────────────────
const unsigned long TS_INTERVAL = 300000UL;          // 5 min
unsigned long lastTS          = 0;
unsigned long lastWifiRetry   = 0;

// ── Calibration ──────────────────────────────────────────────
#define BME_TEMP_OFFSET  -0.4f

// ── I²C ─────────────────────────────────────────────────────
#define SDA_PIN  21
#define SCL_PIN  22
Adafruit_BME280 bme;

// ── DS18B20 ─────────────────────────────────────────────────
#define ONE_WIRE_BUS 17
OneWire           oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);

// ── TFT ─────────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();

// ── Rétroéclairage + Touch ────────────────────────────────
#define LED_PIN         19        // GPIO → LED backlight (PWM)
#define TOUCH_IRQ       13        // GPIO ← T_IRQ du TFT (actif LOW)
#define SCREEN_TIMEOUT  300000UL  // 5 min sans touch → écran OFF
#define VIEW_COUNT        5

// ── Timers ───────────────────────────────────────────────────
const unsigned long READ_INTERVAL = 300000UL;  // 5 min
unsigned long lastRead    = 0;
unsigned long lastNTPSync = 0;

// ── Données globales capteurs ────────────────────────────────
float g_tempAir = NAN;
float g_hum     = NAN;
float g_press   = NAN;
float g_tempEau = DEVICE_DISCONNECTED_C;
bool  g_bmeOK   = false;
bool  g_dsOK    = false;
bool  g_wifiOK  = false;
bool  g_ntpOK   = false;
char  g_ipBuf[16] = "---";

// ── Historique graphe ────────────────────────────────────────
//   Buffer circulaire — step dynamique, ancré à droite
#define GRAPH_POINTS 576  // 48h × 12 pts/h
float g_airHistory[GRAPH_POINTS];
float g_eauHistory[GRAPH_POINTS];
int   g_histCount = 0;
int   g_histHead  = 0;

// ── Vues & écran ─────────────────────────────────────────────
uint8_t       g_view        = 0;
bool          g_screenOn    = true;
unsigned long lastTouchTime      = 0;
unsigned long g_debugLastRefresh = 0;   // refresh sélectif page debug (toutes les 1 s)

// ── Statistiques ─────────────────────────────────────────────
uint32_t          g_readCount   = 0;
uint32_t          g_tsSentOK    = 0;
uint32_t          g_tsFailCount = 0;
uint32_t          g_lastTsEntry = 0;
uint32_t          g_dsReadOK    = 0;   // lectures DS18B20 valides
uint32_t          g_dsReadErr   = 0;   // lectures DS18B20 invalides

// ── Statistiques T° persistantes (NVS) ──────────────────────
float g_eauMin = 999.f,  g_eauMax = -999.f;
float g_airMin = 999.f,  g_airMax = -999.f;
char  g_eauMinTs[14] = "--/-- --:--";
char  g_eauMaxTs[14] = "--/-- --:--";
char  g_airMinTs[14] = "--/-- --:--";
char  g_airMaxTs[14] = "--/-- --:--";
esp_reset_reason_t g_resetReason = ESP_RST_UNKNOWN;  // raison du dernier reboot
bool          g_screensaverOn  = false;              // économiseur actif
unsigned long g_saverRefresh   = 0;                  // dernier redraw économiseur

// ── Layout pixels (paysage 320×240) ─────────────────────────
//
//  Vue 0 — Main :
//  y=0    ┌──────────────────────────────────────────────────────┐
//         │ POOL DATA               14:32    [WiFi bars]         │
//  y=42   ├──────────────────────────────────────────────────────┤
//         │  🌡 (thermo)    26.3°C  (Bold24pt)                   │  zone Air  (99px)
//  y=141  ├──────────────────────────────────────────────────────┤
//         │  💧 (goutte)    24.1°C  (Bold24pt)                   │  zone Eau  (99px)
//  y=240  └──────────────────────────────────────────────────────┘
//
//  Zone Air baseline : y=102  (texte y=67..114, marge 26px)
//  Zone Eau baseline : y=201  (texte y=166..213, marge 26px)
//  Icône thermo      : ix=18, iy=74  (centre y=91)
//  Icône goutte      : ix=18, iy=173 (centre y=190)
//
#define Y_HDR      42
#define Y_ZONE_MID 141   // séparateur Air / Eau

// ── Zone graphe ──────────────────────────────────────────────
#define GRAPH_X0   24    // bord gauche (après labels axe Y)
#define GRAPH_X1  298    // bord droit
#define GRAPH_Y0  151    // haut du tracé (vue normale)
#define GRAPH_Y1  228    // bas du tracé  (vue normale)
#define GRAPH_W   (GRAPH_X1 - GRAPH_X0)   // 274 px
#define GRAPH_H   (GRAPH_Y1 - GRAPH_Y0)   // 77 px (vue normale)

// ── Splash ──────────────────────────────────────────────────
static int splashY = 72;


// ─────────────────────────────────────────────────────────────
// Rétroéclairage
// ─────────────────────────────────────────────────────────────
void screenOn() {
  ledcWrite(LED_PIN, 255);
  g_screenOn    = true;
  lastTouchTime = millis();
}
void screenOff() {
  ledcWrite(LED_PIN, 0);
  g_screenOn = false;
}


// ─────────────────────────────────────────────────────────────
// Bargraphe WiFi
// ─────────────────────────────────────────────────────────────
void drawWifiBars(int x, int y_baseline, int rssi, bool small = false) {
  int bars;
  if      (rssi == -999) bars = 0;
  else if (rssi >= -55)  bars = 4;
  else if (rssi >= -70)  bars = 3;
  else if (rssi >= -80)  bars = 2;
  else                   bars = 1;

  uint8_t bh[4], bw, gap;
  if (small) { bh[0]=3; bh[1]=5; bh[2]=7; bh[3]=9; bw=3; gap=1; }
  else       { bh[0]=5; bh[1]=9; bh[2]=13;bh[3]=17;bw=4; gap=2; }

  for (int i = 0; i < 4; i++) {
    int bx = x + i * (bw + gap);
    int by = y_baseline - bh[i];
    uint16_t c;
    if      (bars == 0) c = TFT_RED;
    else if (i < bars)  c = TFT_GREEN;
    else                c = TFT_DARKGREY;
    tft.fillRect(bx, by, bw, bh[i], c);
  }
}


// ─────────────────────────────────────────────────────────────
// Icône thermomètre — ix,iy = coin haut-gauche (17×34 px)
// ─────────────────────────────────────────────────────────────
void drawIconThermo(int ix, int iy, uint16_t col) {
  int cx = ix + 11;
  // Tube extérieur
  tft.fillRoundRect(ix + 2, iy, 18, 46, 7, col);
  // Cavité intérieure (noire)
  tft.fillRect(ix + 5, iy + 3, 12, 38, TFT_BLACK);
  // Mercure (bas du tube)
  tft.fillRect(ix + 5, iy + 23, 12, 18, col);
  // Bulbe
  tft.fillCircle(cx, iy + 46, 17, col);
  // Graduations à droite
  tft.drawFastHLine(ix + 20, iy +  8, 8, col);
  tft.drawFastHLine(ix + 20, iy + 18, 8, col);
  tft.drawFastHLine(ix + 20, iy + 28, 8, col);
  tft.drawFastHLine(ix + 20, iy + 38, 8, col);
}


// ─────────────────────────────────────────────────────────────
// Icône goutte d'eau — ix,iy = coin haut-gauche (20×34 px)
// ─────────────────────────────────────────────────────────────
void drawIconDrop(int ix, int iy, uint16_t col) {
  int cx = ix + 19;
  // Partie ronde (bas)
  tft.fillCircle(cx, iy + 46, 18, col);
  // Partie effilée (haut) — triangle apex→base
  tft.fillTriangle(cx - 18, iy + 46, cx + 18, iy + 46, cx, iy, col);
  // Reflet (highlight)
  tft.fillCircle(cx - 6, iy + 30, 4, TFT_WHITE);
}


// ─────────────────────────────────────────────────────────────
// Température dans une zone (vue principale)
// baseline : coordonnée Y de la ligne de base du texte
// ─────────────────────────────────────────────────────────────
void drawTempZone(float val, bool isWater, uint16_t color, int baseline) {
  char numBuf[8];
  if (isnan(val) || (isWater && val == DEVICE_DISCONNECTED_C))
    strcpy(numBuf, "--.-");
  else
    snprintf(numBuf, sizeof(numBuf), "%.1f", val);

  // Fonte : FreeSansBold18pt7b × textSize(2) ≈ 36pt effectif
  // glyph_ab=25 → ascent 50px | glyph_bb=8 → descent 16px | total 66px
  const int degR = 6, degGap = 7, cGap = 5;  // degR=6 → anneau 2px (r ext=6, int=4)
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.setTextSize(2);
  int numW = tft.textWidth(numBuf);        // largeur à 2×
  tft.setTextSize(1);
  int cW   = tft.textWidth("C");           // largeur "C" à 1×
  int totalW = numW + degGap + (degR + 1) * 2 + cGap + cW;

  // Centré dans la zone à droite de l'icône (cx = 185)
  int x0 = 185 - totalW / 2;
  if (x0 < 67) x0 = 67;                   // marge après icône (droite ≈ 60)

  // Tracé nombre en 2×
  tft.setTextSize(2);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(x0, baseline);
  tft.print(numBuf);

  // Symbole ° : donut 2px (fillCircle plein puis trou noir)
  int dx = tft.getCursorX() + degGap;
  int dy = baseline - 44;                  // calé en haut des capitales (2×)
  int cx = dx + degR, cy = dy + degR;
  tft.fillCircle(cx, cy, degR,   color);      // plein
  tft.fillCircle(cx, cy, degR-2, TFT_BLACK);  // trou → anneau 2px

  // "C" en 1×
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(dx + (degR + 1) * 2 + cGap, baseline);
  tft.print("C");
  tft.setTextFont(1);
}



// ─────────────────────────────────────────────────────────────
// Splash log
// ─────────────────────────────────────────────────────────────
void splashLog(const char* msg, uint16_t color = TFT_WHITE) {
  tft.fillRect(0, splashY, 320, 12, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(12, splashY + 2);
  tft.print(msg);
  Serial.println(msg);
  splashY += 14;
}


// ─────────────────────────────────────────────────────────────
// Horodatage Serial (HH:MM:SS — si NTP OK)
// ─────────────────────────────────────────────────────────────
void serialTimestamp() {
  if (!g_ntpOK) return;
  struct tm ti;
  if (!getLocalTime(&ti)) return;
  char buf[10];
  strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
  Serial.print(buf); Serial.print(F("  "));
}


// ─────────────────────────────────────────────────────────────
// Ajoute un point dans le buffer circulaire
// ─────────────────────────────────────────────────────────────
void addHistoryPoint(float air, float eau) {
  g_airHistory[g_histHead] = air;
  g_eauHistory[g_histHead] = eau;
  g_histHead = (g_histHead + 1) % GRAPH_POINTS;
  if (g_histCount < GRAPH_POINTS) g_histCount++;
}


// ─────────────────────────────────────────────────────────────
// Graphe historique
//   gY0, gY1 : zone de tracé  (appelé depuis drawViewGraph)
//   Vue graphe : drawGraph(Y_HDR+6, 224) → gH = 176 px
// ─────────────────────────────────────────────────────────────
void drawGraph(int gY0 = GRAPH_Y0, int gY1 = GRAPH_Y1) {
  int gH = gY1 - gY0;

  // Légende intégrée — coin haut-droit du graphe
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, 0x0841); tft.setCursor(GRAPH_X1 - 42, gY0 + 3); tft.print("Air");
  tft.setTextColor(TFT_CYAN,   0x0841); tft.setCursor(GRAPH_X1 - 18, gY0 + 3); tft.print("Eau");

  if (g_histCount < 2) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x2965, 0x0841);
    tft.drawString("[ mesures en cours... ]", 170, (gY0 + gY1) / 2);
    tft.setTextDatum(TL_DATUM);
    return;
  }

  // ── Calcul min/max ──
  float vMin = 999.f, vMax = -999.f;
  for (int i = 0; i < g_histCount; i++) {
    int idx = (g_histHead - g_histCount + i + GRAPH_POINTS) % GRAPH_POINTS;
    float a = g_airHistory[idx];
    float e = g_eauHistory[idx];
    if (!isnan(a))   { vMin = min(vMin, a); vMax = max(vMax, a); }
    if (e > -100.f)  { vMin = min(vMin, e); vMax = max(vMax, e); }
  }
  vMin = floorf(vMin / 5.f) * 5.f;
  vMax = ceilf (vMax / 5.f) * 5.f;
  if (vMax - vMin < 5.f) vMax = vMin + 5.f;
  float vRange = vMax - vMin;

  // ── Axe + grille ──
  const uint16_t gc = 0x18C3;
  tft.drawFastVLine(GRAPH_X0, gY0, gH,    gc);
  tft.drawFastHLine(GRAPH_X0, gY1, GRAPH_W, gc);
  int nGrid = (int)(vRange / 5.f);
  for (int g = 1; g < nGrid && g < 8; g++) {
    int gy = gY1 - (int)((float)g / (float)nGrid * gH);
    tft.drawFastHLine(GRAPH_X0, gy, GRAPH_W, gc);
  }

  // Labels axe Y
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(0x5AEB, 0x0841);
  char lb[6];
  snprintf(lb, sizeof(lb), "%d", (int)vMax);
  tft.setCursor(2, gY0);      tft.print(lb);
  snprintf(lb, sizeof(lb), "%d", (int)vMin);
  tft.setCursor(2, gY1 - 8);  tft.print(lb);

  // ── Tracé des courbes (step dynamique, ancré à droite) ──
  float stepF = (g_histCount > 1) ? (float)GRAPH_W / (g_histCount - 1) : (float)GRAPH_W;
  for (int i = 1; i < g_histCount; i++) {
    int idx0 = (g_histHead - g_histCount + i - 1 + GRAPH_POINTS) % GRAPH_POINTS;
    int idx1 = (g_histHead - g_histCount + i     + GRAPH_POINTS) % GRAPH_POINTS;

    int x0 = GRAPH_X0 + (int)((i - 1) * stepF);
    int x1 = GRAPH_X0 + (int)(i       * stepF);
    if (x1 == x0) x1 = x0 + 1;
    if (x0 < GRAPH_X0) x0 = GRAPH_X0;
    if (x1 > GRAPH_X1) x1 = GRAPH_X1;

    float a0 = g_airHistory[idx0], a1 = g_airHistory[idx1];
    if (!isnan(a0) && !isnan(a1)) {
      int y0 = constrain(gY1 - (int)((a0-vMin)/vRange*gH), gY0, gY1);
      int y1 = constrain(gY1 - (int)((a1-vMin)/vRange*gH), gY0, gY1);
      tft.drawLine(x0, y0, x1, y1, TFT_GREEN);
    }
    float e0 = g_eauHistory[idx0], e1 = g_eauHistory[idx1];
    if (e0 > -100.f && e1 > -100.f) {
      int y0 = constrain(gY1 - (int)((e0-vMin)/vRange*gH), gY0, gY1);
      int y1 = constrain(gY1 - (int)((e1-vMin)/vRange*gH), gY0, gY1);
      tft.drawLine(x0, y0, x1, y1, TFT_CYAN);
    }
  }

  // ── Repères horaires ──
  if (g_ntpOK) {
    struct tm ti;
    if (getLocalTime(&ti)) {
      int ptSinceHour = ti.tm_min / 5;
      int xLastHour   = GRAPH_X1 - (int)(ptSinceHour * stepF);
      int xDataStart  = GRAPH_X1 - (int)((g_histCount - 1) * stepF);
      int tickH       = (stepF * 12.f >= 16.f) ? 1 : 6;  // toutes les 1h ou 6h

      tft.setTextFont(1); tft.setTextSize(1);
      for (int h = 0; h < 49; h += tickH) {
        int x = xLastHour - (int)(h * 12 * stepF);
        if (x > GRAPH_X1)     continue;
        if (x < GRAPH_X0 + 8) break;
        if (x < xDataStart)   break;
        tft.drawFastVLine(x, gY1 + 1, 3, TFT_DARKGREY);
        int hour = ((ti.tm_hour - h) % 24 + 24) % 24;
        char lbl[5];
        snprintf(lbl, sizeof(lbl), "%dh", hour);
        int lw = (int)strlen(lbl) * 6;
        int lx = x - lw / 2;
        if (lx < GRAPH_X0) lx = GRAPH_X0;
        tft.setTextColor(0x5AEB, 0x0841);
        tft.setCursor(lx, gY1 + 5);
        tft.print(lbl);
      }
    }
  }
}


// ─────────────────────────────────────────────────────────────
// Envoi ThingSpeak
// ─────────────────────────────────────────────────────────────
void sendThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!g_bmeOK || isnan(g_tempAir)) return;

  char url[192];
  if (g_dsOK) {
    snprintf(url, sizeof(url),
      "http://api.thingspeak.com/update?api_key=" SECRET_API_KEY
      "&field1=%.2f&field2=%.2f&field3=%.2f&field4=%.2f&field5=%d",
      g_tempAir, g_tempEau, g_hum, g_press, (int)WiFi.RSSI());
  } else {
    snprintf(url, sizeof(url),
      "http://api.thingspeak.com/update?api_key=" SECRET_API_KEY
      "&field1=%.2f&field3=%.2f&field4=%.2f&field5=%d",
      g_tempAir, g_hum, g_press, (int)WiFi.RSSI());
  }

  HTTPClient http;
  http.setTimeout(8000);
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == 200) {
    g_lastTsEntry = (uint32_t)http.getString().toInt();
    g_tsSentOK++;
    serialTimestamp();
    Serial.print(F("TS OK — entree #")); Serial.println(g_lastTsEntry);
  } else {
    g_tsFailCount++;
    serialTimestamp();
    Serial.print(F("TS ERR — HTTP ")); Serial.println(httpCode);
  }
  http.end();
}


// ─────────────────────────────────────────────────────────────
// Header vue principale (temps, WiFi, statuts capteurs)
// ─────────────────────────────────────────────────────────────
void updateHeader() {
  struct tm ti;
  bool timeOK = g_ntpOK && getLocalTime(&ti);
  char timeBuf[6];
  if (timeOK) strftime(timeBuf, sizeof(timeBuf), "%H:%M", &ti);
  else        strcpy(timeBuf, "--:--");

  tft.fillRect(206, 10, 70, 22, TFT_NAVY);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setCursor(209, 25);
  tft.print(timeBuf);
  tft.setTextFont(1);

  tft.fillRect(279, 4, 38, 34, TFT_NAVY);   // zone élargie pour couvrir l'ancienne et la nouvelle position
  bool wconn = (WiFi.status() == WL_CONNECTED);
  drawWifiBars(281, 30, wconn ? (int)WiFi.RSSI() : -999);  // y_baseline=30 → bars alignées avec le titre (y=12..30)

}


// ─────────────────────────────────────────────────────────────
// Flèche tendance T° eau (zone droite de la zone Eau)
// ─────────────────────────────────────────────────────────────
void drawTrendArrow(float trend) {
  const int ax = 284, ay = 162, aw = 22, ah = 30;
  tft.fillRect(ax, ay, aw, ah, TFT_BLACK);
  if (isnan(trend)) return;
  uint16_t col = (trend > 0.3f) ? TFT_RED : (trend < -0.3f) ? TFT_CYAN : TFT_GREEN;
  int cx = ax + aw / 2;                       // 295
  if (trend > 0.3f) {                         // ↑ hausse
    tft.fillTriangle(cx, ay, cx - 9, ay + 13, cx + 9, ay + 13, col);
    tft.fillRect(cx - 4, ay + 13, 8, 17, col);
  } else if (trend < -0.3f) {                 // ↓ baisse
    tft.fillRect(cx - 4, ay, 8, 17, col);
    tft.fillTriangle(cx, ay + ah, cx - 9, ay + 17, cx + 9, ay + 17, col);
  } else {                                    // → stable
    int my = ay + ah / 2;
    tft.fillRect(ax, my - 3, 14, 6, col);
    tft.fillTriangle(ax + 13, my - 7, ax + 13, my + 7, ax + aw, my, col);
  }
}


// ─────────────────────────────────────────────────────────────
// Économiseur d'écran — T° eau + heure, luminosité réduite
// ─────────────────────────────────────────────────────────────
void drawScreensaver() {
  tft.fillScreen(TFT_BLACK);
  // T° eau centrée
  {
    char buf[10];
    if (g_dsOK && g_tempEau > -100.f) snprintf(buf, sizeof(buf), "%.1fC", g_tempEau);
    else strcpy(buf, "--.-C");
    tft.setFreeFont(&FreeSansBold18pt7b); tft.setTextSize(2);
    int w = tft.textWidth(buf);
    tft.setTextColor(g_dsOK ? TFT_CYAN : TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(160 - w / 2, 115);
    tft.print(buf);
    tft.setTextSize(1);
  }
  // Heure
  {
    char buf[6] = "--:--";
    if (g_ntpOK) { struct tm ti; getLocalTime(&ti); strftime(buf, sizeof(buf), "%H:%M", &ti); }
    tft.setFreeFont(&FreeSansBold9pt7b);
    int w = tft.textWidth(buf);
    tft.setTextColor(0x4208, TFT_BLACK);
    tft.setCursor(160 - w / 2, 178);
    tft.print(buf);
    tft.setTextFont(1);
  }
}

void enterScreensaver() {
  g_screenOn      = false;
  g_screensaverOn = true;
  ledcWrite(LED_PIN, 18);   // ~7% luminosité
  drawScreensaver();
  g_saverRefresh = millis();
  Serial.println(F("Economiseur actif"));
}

void wakeFromScreensaver() {
  g_screensaverOn = false;
  g_screenOn      = true;
  ledcWrite(LED_PIN, 255);
  lastTouchTime   = millis();
  drawCurrentView();
}


// ─────────────────────────────────────────────────────────────
// ── VUE 0 — Principal : icônes + 2 températures ─────────────
// ─────────────────────────────────────────────────────────────
void drawViewMain() {
  tft.fillScreen(TFT_BLACK);
  // Header
  tft.fillRect(0, 0, 320, Y_HDR, TFT_NAVY);
  tft.setFreeFont(&FreeSansBold9pt7b); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setCursor(8, 25); tft.print("POOL DATA");
  tft.drawFastHLine(0, Y_HDR,      320, TFT_DARKGREY);   // bas header
  tft.drawFastHLine(0, Y_ZONE_MID, 320, TFT_DARKGREY);   // séparateur zones
  // Zone Air (y=42..140, centre y=91) — icône 28×63px, centrage iy=58
  drawIconThermo(22, 58, TFT_GREEN);
  drawTempZone(g_tempAir, false, TFT_GREEN, 108);
  // Zone Eau (y=141..240, centre y=190) — icône 38×64px, centrage iy=158
  drawIconDrop(14, 158, TFT_CYAN);
  drawTempZone(g_tempEau, true, TFT_CYAN, 207);
  drawTrendArrow(computeEauTrend());
  // Header dynamique (heure + WiFi)
  updateHeader();
}



// ─────────────────────────────────────────────────────────────
// ── VUE 1 — Graphe avec header ───────────────────────────────
// ─────────────────────────────────────────────────────────────
void drawViewGraph() {
  tft.fillScreen(0x0841);                          // fond graphe
  tft.fillRect(0, 0, 320, Y_HDR, TFT_NAVY);       // header par-dessus
  tft.setFreeFont(&FreeSansBold9pt7b); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(TC_DATUM); tft.drawString("HISTORIQUE", 160, 12); tft.setTextDatum(TL_DATUM);
  tft.drawFastHLine(0, Y_HDR, 320, TFT_DARKGREY);
  // Zone graphe : Y_HDR+6 .. 224  →  gH = 176 px
  drawGraph(Y_HDR + 6, 224);
}


// ─────────────────────────────────────────────────────────────
// ── VUE 1 — Humidité / Pression ─────────────────────────────
// ─────────────────────────────────────────────────────────────
void drawViewHumPress() {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 320, Y_HDR, TFT_NAVY);
  tft.setFreeFont(&FreeSansBold9pt7b); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(TC_DATUM); tft.drawString("HUMIDITE / PRESSION", 160, 12); tft.setTextDatum(TL_DATUM);
  tft.drawFastHLine(0, Y_HDR, 320, TFT_DARKGREY);

  // ── Humidité (zone y=42..140, centre y=91, baseline 103) ──
  {
    char buf[12];
    if (!isnan(g_hum)) snprintf(buf, sizeof(buf), "%.1f %%", g_hum);
    else               strcpy(buf, "--.- %");
    tft.setFreeFont(&FreeSansBold24pt7b);
    int w = tft.textWidth(buf);
    tft.setTextColor(!isnan(g_hum) ? TFT_YELLOW : TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(160 - w / 2, 103);
    tft.print(buf);
  }

  tft.drawFastHLine(0, 140, 320, TFT_DARKGREY);

  // ── Pression (zone y=140..240, centre y=190, baseline 202) ──
  {
    char buf[16];
    if (!isnan(g_press)) snprintf(buf, sizeof(buf), "%.1f hPa", g_press);
    else                 strcpy(buf, "---- hPa");
    tft.setFreeFont(&FreeSansBold24pt7b);
    int w = tft.textWidth(buf);
    tft.setTextColor(!isnan(g_press) ? TFT_MAGENTA : TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(160 - w / 2, 202);
    tft.print(buf);
  }

  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(BR_DATUM);
  tft.drawString("touch >", 316, 236);
  tft.setTextDatum(TL_DATUM);
}


// ─────────────────────────────────────────────────────────────
// Raison du dernier reboot (texte court)
static const char* resetStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "PowerOn";
    case ESP_RST_EXT:       return "ExtReset";
    case ESP_RST_SW:        return "Software";
    case ESP_RST_PANIC:     return "Panic/Crash";
    case ESP_RST_INT_WDT:   return "IntWDT";
    case ESP_RST_TASK_WDT:  return "TaskWDT";  // notre watchdog 30s
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DeepSleep";
    case ESP_RST_BROWNOUT:  return "Brownout";
    default:                return "Unknown";
  }
}


// ─────────────────────────────────────────────────────────────
// ── VUE 3 — Debug système ────────────────────────────────────
// ─────────────────────────────────────────────────────────────
void drawViewDebug() {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 320, Y_HDR, TFT_NAVY);
  tft.setFreeFont(&FreeSansBold9pt7b); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(TC_DATUM); tft.drawString("INFOS SYSTEME  " FW_VERSION, 160, 12); tft.setTextDatum(TL_DATUM);
  tft.drawFastHLine(0, Y_HDR, 320, TFT_DARKGREY);

  // Contenu : Font2 (16px) — 12 lignes × dy=16 = 192px, y=44..236
  tft.setTextFont(2); tft.setTextSize(1);
  int y = Y_HDR + 2;
  const int dy = 16;
  char buf[48];

  // ── 1. Date + Heure (NTP) ──
  tft.setTextColor(TFT_WHITE, TFT_BLACK);   // reset couleur après le header navy
  char dtBuf[20] = "--/--/----  --:--";
  if (g_ntpOK) { struct tm ti; getLocalTime(&ti); strftime(dtBuf, sizeof(dtBuf), "%d/%m/%Y  %H:%M", &ti); }
  tft.setCursor(4, y); tft.print(dtBuf); y += dy;

  // ── 2. Capteurs ── col1=x4  col2=x160
  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4,   y); tft.print("BME280 : ");
  tft.setTextColor(g_bmeOK ? TFT_GREEN : TFT_RED, TFT_BLACK); tft.print(g_bmeOK ? "OK" : "ERR");
  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(160, y); tft.print("DS18B20 : ");
  tft.setTextColor(g_dsOK  ? TFT_GREEN : TFT_RED, TFT_BLACK); tft.print(g_dsOK  ? "OK" : "ERR");
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  char dsBuf[12]; snprintf(dsBuf, sizeof(dsBuf), " %lu/%lu", g_dsReadOK, g_dsReadErr);
  tft.print(dsBuf);
  tft.setTextColor(TFT_WHITE, TFT_BLACK); y += dy;

  // ── 3. IP ──
  snprintf(buf, sizeof(buf), "IP : %s", g_ipBuf);
  tft.setCursor(4, y); tft.print(buf); y += dy;

  // ── 4. AP ──
  tft.setCursor(4, y);
  if (g_wifiOK) {
    snprintf(buf, sizeof(buf), "AP : %s", WiFi.BSSIDstr().c_str());
    tft.setTextColor(TFT_WHITE,  TFT_BLACK); tft.print(buf);
  } else {
    tft.setTextColor(TFT_ORANGE, TFT_BLACK); tft.print("AP : non connecte");
  }
  tft.setTextColor(TFT_WHITE, TFT_BLACK); y += dy;

  // ── 5. MAC ──
  snprintf(buf, sizeof(buf), "MAC : %s", WiFi.macAddress().c_str());
  tft.setCursor(4, y); tft.print(buf); y += dy;

  // ── 6. RSSI | WiFi | NTP (3 colonnes) ──
  {
    int rssi = g_wifiOK ? (int)WiFi.RSSI() : 0;
    uint16_t rc = !g_wifiOK   ? TFT_DARKGREY :
                  rssi >= -55 ? TFT_GREEN  :
                  rssi >= -70 ? TFT_YELLOW :
                  rssi >= -80 ? TFT_ORANGE : TFT_RED;
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4,   y); tft.print("RSSI : ");
    snprintf(buf, sizeof(buf), "%ddBm", rssi);
    tft.setTextColor(rc, TFT_BLACK); tft.print(buf);
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(140, y); tft.print("WiFi : ");
    tft.setTextColor(g_wifiOK ? TFT_GREEN : TFT_RED, TFT_BLACK); tft.print(g_wifiOK ? "OK" : "HS");
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(232, y); tft.print("NTP : ");
    tft.setTextColor(g_ntpOK  ? TFT_GREEN : TFT_RED, TFT_BLACK); tft.print(g_ntpOK  ? "OK" : "ERR");
    tft.setTextColor(TFT_WHITE, TFT_BLACK); y += dy;
  }

  // ── 7. Températures ── col1=x4  col2=x160
  char airBuf[8], eauBuf[8];
  if (!isnan(g_tempAir)) snprintf(airBuf, sizeof(airBuf), "%.1f", g_tempAir); else strcpy(airBuf, "--.-");
  if (g_dsOK)            snprintf(eauBuf, sizeof(eauBuf), "%.1f", g_tempEau); else strcpy(eauBuf, "--.-");
  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4,   y); tft.print("Air : ");
  tft.setTextColor(TFT_GREEN,   TFT_BLACK); tft.print(airBuf); tft.print("C");
  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(160, y); tft.print("Eau : ");
  tft.setTextColor(TFT_CYAN,    TFT_BLACK); tft.print(eauBuf); tft.print("C");
  tft.setTextColor(TFT_WHITE, TFT_BLACK); y += dy;

  // ── 8. Humidité / Pression ── col1=x4  col2=x160
  char humBuf[8], prsBuf[8];
  if (!isnan(g_hum))   snprintf(humBuf, sizeof(humBuf), "%.1f", g_hum);   else strcpy(humBuf, "----");
  if (!isnan(g_press)) snprintf(prsBuf, sizeof(prsBuf), "%.1f", g_press); else strcpy(prsBuf, "----");
  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4,   y); tft.print("Hum : ");
  tft.setTextColor(TFT_YELLOW,  TFT_BLACK); tft.print(humBuf); tft.print("%");
  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(160, y); tft.print("P : ");
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK); tft.print(prsBuf); tft.print("hPa");
  tft.setTextColor(TFT_WHITE, TFT_BLACK); y += dy;

  // ── 9. Uptime / Heap ── col1=x4  col2=x160
  {
    unsigned long upS = millis() / 1000;
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4,   y);
    snprintf(buf, sizeof(buf), "Uptime : %luh%02lum", upS/3600, (upS%3600)/60); tft.print(buf);
    tft.setCursor(160, y);
    snprintf(buf, sizeof(buf), "Heap : %lukB   ", (unsigned long)ESP.getFreeHeap()/1024); tft.print(buf);
    y += dy;
  }

  // ── 10. ThingSpeak ── col1=x4  col2=x120  col3=x200
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(4,   y); tft.print("ThingSpeak");
  tft.setCursor(120, y); tft.print("OK : ");
  snprintf(buf, sizeof(buf), "%-5lu", (unsigned long)g_tsSentOK);    tft.print(buf);
  tft.setCursor(200, y); tft.print("Failed : ");
  snprintf(buf, sizeof(buf), "%-5lu", (unsigned long)g_tsFailCount); tft.print(buf);
  y += dy;

  // ── 11. Last TS + Next TS ── col1=x4  col2=x160
  {
    unsigned long elS = (millis() - lastRead) / 1000;
    char lectBuf[10];
    if (elS < 60) snprintf(lectBuf, sizeof(lectBuf), "%lus",      elS);
    else          snprintf(lectBuf, sizeof(lectBuf), "%lum%02lus", elS/60, elS%60);
    unsigned long nowMs = millis();
    unsigned long nextTs = (lastTS + TS_INTERVAL > nowMs) ? (lastTS + TS_INTERVAL - nowMs) / 1000 : 0;
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4,   y);
    snprintf(buf, sizeof(buf), "Last TS : %-6s", lectBuf);   tft.print(buf);
    tft.setCursor(160, y);
    char nextBuf[10]; snprintf(nextBuf, sizeof(nextBuf), "%lus", nextTs);
    snprintf(buf, sizeof(buf), "Next TS : %-5s", nextBuf);  tft.print(buf);
    y += dy;
  }

  // ── 12. Reset ──
  snprintf(buf, sizeof(buf), "Reset : %s", resetStr(g_resetReason));
  tft.setCursor(4, y); tft.print(buf);

  tft.setTextFont(1);
  g_debugLastRefresh = millis();   // évite un refresh sélectif immédiat après redraw complet
}


// ─────────────────────────────────────────────────────────────
// Refresh sélectif page debug — lignes volatiles seulement
// ─────────────────────────────────────────────────────────────
void refreshDebugVolatile() {
  tft.setTextFont(2); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  const int dy  = 16;
  // positions y des lignes volatiles (pas de fillRect → overwrite direct, zéro flicker)
  const int y1  = Y_HDR + 2 + 0 * dy;   // Date + Heure → 44
  const int y6  = Y_HDR + 2 + 5 * dy;   // RSSI/WiFi/NTP→ 124
  const int y9  = Y_HDR + 2 + 8 * dy;   // Uptime/Heap  → 172
  const int y10 = y9  + dy;              // ThingSpeak   → 188
  const int y11 = y10 + dy;              // Last/Next TS → 204
  char buf[48];

  // ── Ligne 1 : Date + Heure ── (longueur fixe → pas de padding nécessaire)
  char dtBuf[20] = "--/--/----  --:--";
  if (g_ntpOK) { struct tm ti; getLocalTime(&ti); strftime(dtBuf, sizeof(dtBuf), "%d/%m/%Y  %H:%M", &ti); }
  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4, y1); tft.print(dtBuf);

  // ── Ligne 6 : RSSI | WiFi | NTP ── (colonnes fixes → pas de chevauchement)
  {
    int rssi = g_wifiOK ? (int)WiFi.RSSI() : 0;
    uint16_t rc = !g_wifiOK   ? TFT_DARKGREY :
                  rssi >= -55 ? TFT_GREEN  :
                  rssi >= -70 ? TFT_YELLOW :
                  rssi >= -80 ? TFT_ORANGE : TFT_RED;
    char rssiVal[10];
    if (g_wifiOK) snprintf(rssiVal, sizeof(rssiVal), "%ddBm", rssi);
    else          strcpy(rssiVal, "---");
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4,   y6); tft.print("RSSI : ");
    snprintf(buf, sizeof(buf), "%-8s", rssiVal);   // padding 8 chars → couvre -100dBm
    tft.setTextColor(rc,        TFT_BLACK); tft.print(buf);
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(140, y6); tft.print("WiFi : ");
    tft.setTextColor(g_wifiOK ? TFT_GREEN : TFT_RED, TFT_BLACK); tft.print(g_wifiOK ? "OK " : "HS ");
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(232, y6); tft.print("NTP : ");
    tft.setTextColor(g_ntpOK  ? TFT_GREEN : TFT_RED, TFT_BLACK); tft.print(g_ntpOK  ? "OK " : "ERR");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }

  // ── Ligne 9 : Uptime / Heap ── col1=x4  col2=x160
  {
    unsigned long upS = millis() / 1000;
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4,   y9);
    snprintf(buf, sizeof(buf), "Uptime : %luh%02lum", upS/3600, (upS%3600)/60); tft.print(buf);
    tft.setCursor(160, y9);
    snprintf(buf, sizeof(buf), "Heap : %lukB   ", (unsigned long)ESP.getFreeHeap()/1024); tft.print(buf);
  }

  // ── Ligne 10 : ThingSpeak ── col1=x4  col2=x120  col3=x200
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(4,   y10); tft.print("ThingSpeak");
  tft.setCursor(120, y10); tft.print("OK : ");
  snprintf(buf, sizeof(buf), "%-5lu", (unsigned long)g_tsSentOK);    tft.print(buf);
  tft.setCursor(200, y10); tft.print("Failed : ");
  snprintf(buf, sizeof(buf), "%-5lu  ", (unsigned long)g_tsFailCount); tft.print(buf);

  // ── Ligne 11 : Last TS + Next TS ── col1=x4  col2=x160
  {
    unsigned long elS = (millis() - lastRead) / 1000;
    char lectBuf[10];
    if (elS < 60) snprintf(lectBuf, sizeof(lectBuf), "%lus",      elS);
    else          snprintf(lectBuf, sizeof(lectBuf), "%lum%02lus", elS/60, elS%60);
    unsigned long nowMs = millis();
    unsigned long nextTs = (lastTS + TS_INTERVAL > nowMs) ? (lastTS + TS_INTERVAL - nowMs) / 1000 : 0;
    tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4,   y11);
    snprintf(buf, sizeof(buf), "Last TS : %-6s", lectBuf);   tft.print(buf);
    tft.setCursor(160, y11);
    char nextBuf[10]; snprintf(nextBuf, sizeof(nextBuf), "%lus", nextTs);
    snprintf(buf, sizeof(buf), "Next TS : %-5s", nextBuf);  tft.print(buf);
  }

  tft.setTextFont(1);
  g_debugLastRefresh = millis();
}


// ─────────────────────────────────────────────────────────────
// Dispatcher — dessine la vue courante
// ─────────────────────────────────────────────────────────────
// Statistiques T° — NVS
// ─────────────────────────────────────────────────────────────
void loadStats() {
  Preferences p; p.begin("pool-s", true);
  g_eauMin = p.getFloat("eau_min", 999.f);
  g_eauMax = p.getFloat("eau_max", -999.f);
  g_airMin = p.getFloat("air_min", 999.f);
  g_airMax = p.getFloat("air_max", -999.f);
  if (p.isKey("eau_min_ts")) p.getString("eau_min_ts", g_eauMinTs, sizeof(g_eauMinTs));
  if (p.isKey("eau_max_ts")) p.getString("eau_max_ts", g_eauMaxTs, sizeof(g_eauMaxTs));
  if (p.isKey("air_min_ts")) p.getString("air_min_ts", g_airMinTs, sizeof(g_airMinTs));
  if (p.isKey("air_max_ts")) p.getString("air_max_ts", g_airMaxTs, sizeof(g_airMaxTs));
  p.end();
}

void saveStats() {
  Preferences p; p.begin("pool-s", false);
  p.putFloat("eau_min", g_eauMin); p.putFloat("eau_max", g_eauMax);
  p.putFloat("air_min", g_airMin); p.putFloat("air_max", g_airMax);
  p.putString("eau_min_ts", g_eauMinTs); p.putString("eau_max_ts", g_eauMaxTs);
  p.putString("air_min_ts", g_airMinTs); p.putString("air_max_ts", g_airMaxTs);
  p.end();
}

void resetStats() {
  g_eauMin = 999.f; g_eauMax = -999.f;
  g_airMin = 999.f; g_airMax = -999.f;
  strcpy(g_eauMinTs, "--/-- --:--"); strcpy(g_eauMaxTs, "--/-- --:--");
  strcpy(g_airMinTs, "--/-- --:--"); strcpy(g_airMaxTs, "--/-- --:--");
  Preferences p; p.begin("pool-s", false); p.clear(); p.end();
}

void updateStats(float eau, float air) {
  bool changed = false;
  char ts[14];
  if (g_ntpOK) { struct tm ti; getLocalTime(&ti); strftime(ts, sizeof(ts), "%d/%m %H:%M", &ti); }
  else strcpy(ts, "--/-- --:--");
  if (!isnan(eau) && eau > -100.f) {
    if (eau < g_eauMin) { g_eauMin = eau; strlcpy(g_eauMinTs, ts, sizeof(g_eauMinTs)); changed = true; }
    if (eau > g_eauMax) { g_eauMax = eau; strlcpy(g_eauMaxTs, ts, sizeof(g_eauMaxTs)); changed = true; }
  }
  if (!isnan(air)) {
    if (air < g_airMin) { g_airMin = air; strlcpy(g_airMinTs, ts, sizeof(g_airMinTs)); changed = true; }
    if (air > g_airMax) { g_airMax = air; strlcpy(g_airMaxTs, ts, sizeof(g_airMaxTs)); changed = true; }
  }
  if (changed) saveStats();
}

float computeEauTrend() {
  if (g_histCount < 13) return NAN;
  int idxNow = (g_histHead - 1  + GRAPH_POINTS) % GRAPH_POINTS;
  int idx1h  = (g_histHead - 13 + GRAPH_POINTS) % GRAPH_POINTS;
  float vNow = g_eauHistory[idxNow], v1h = g_eauHistory[idx1h];
  if (vNow <= -100.f || v1h <= -100.f) return NAN;
  return vNow - v1h;
}


// ─────────────────────────────────────────────────────────────
// ── VUE 4 — Statistiques T° ──────────────────────────────────
// ─────────────────────────────────────────────────────────────
void drawViewStats() {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 320, Y_HDR, TFT_NAVY);
  tft.setFreeFont(&FreeSansBold9pt7b); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(TC_DATUM); tft.drawString("STATISTIQUES", 160, 12); tft.setTextDatum(TL_DATUM);
  tft.drawFastHLine(0, Y_HDR, 320, TFT_DARKGREY);

  tft.setTextFont(2); tft.setTextSize(1);
  const int dy = 16;
  int y = Y_HDR + 2;
  char buf[32];

  // ── Eau ──
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(4, y); tft.print("TEMPERATURE EAU"); y += dy;

  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4, y); tft.print("  Min : ");
  if (g_eauMin < 900.f) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%.1fC", g_eauMin); tft.print(buf);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setCursor(160, y); tft.print(g_eauMinTs);
  } else { tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.print("--.--"); }
  y += dy;

  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4, y); tft.print("  Max : ");
  if (g_eauMax > -900.f) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%.1fC", g_eauMax); tft.print(buf);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setCursor(160, y); tft.print(g_eauMaxTs);
  } else { tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.print("--.--"); }
  y += dy;

  // ── Air ──
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(4, y); tft.print("TEMPERATURE AIR"); y += dy;

  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4, y); tft.print("  Min : ");
  if (g_airMin < 900.f) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%.1fC", g_airMin); tft.print(buf);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setCursor(160, y); tft.print(g_airMinTs);
  } else { tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.print("--.--"); }
  y += dy;

  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4, y); tft.print("  Max : ");
  if (g_airMax > -900.f) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%.1fC", g_airMax); tft.print(buf);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setCursor(160, y); tft.print(g_airMaxTs);
  } else { tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.print("--.--"); }
  y += dy;

  y += dy;  // ligne vide

  // ── Ecart + Tendance ──
  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4, y); tft.print("Ecart Eau-Air : ");
  if (g_dsOK && !isnan(g_tempAir)) {
    float delta = g_tempEau - g_tempAir;
    tft.setTextColor(delta >= 0 ? TFT_MAGENTA : TFT_CYAN, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%+.1fC", delta); tft.print(buf);
  } else { tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.print("--.-"); }
  y += dy;

  float trend = computeEauTrend();
  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(4, y); tft.print("Tendance 1h   : ");
  if (!isnan(trend)) {
    uint16_t tc = trend > 0.2f ? TFT_RED : trend < -0.2f ? TFT_CYAN : TFT_GREEN;
    const char* dir = trend > 0.2f ? "hausse" : trend < -0.2f ? "baisse" : "stable";
    tft.setTextColor(tc, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%s (%+.1fC/h)", dir, trend); tft.print(buf);
  } else { tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.print("-- (< 1h)"); }
  y += dy * 2;  // espace avant bouton

  // ── Bouton reset ──
  tft.fillRoundRect(4, y, 312, 18, 9, 0x2104);
  tft.drawRoundRect(4, y, 312, 18, 9, TFT_DARKGREY);
  tft.setTextColor(TFT_DARKGREY, 0x2104);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Maintenir appuye pour reinitialiser", 160, y + 9);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
}


// ─────────────────────────────────────────────────────────────
void drawCurrentView() {
  switch (g_view) {
    case 0: drawViewMain();     break;
    case 1: drawViewHumPress(); break;
    case 2: drawViewGraph();    break;
    case 3: drawViewDebug();    break;
    case 4: drawViewStats();    break;
    default: g_view = 0; drawViewMain(); break;
  }
}


// ─────────────────────────────────────────────────────────────
void setup() {
  g_resetReason = esp_reset_reason();   // capturé avant tout init
  Serial.begin(115200);
  delay(1500);
  Serial.println(F("════════════════════════════════"));
  Serial.println(F("  Pool Data  D1 Mini ESP32"));
  Serial.print  (F("  Firmware : ")); Serial.println(F(FW_VERSION));
  Serial.print  (F("  Compile  : ")); Serial.println(F(__DATE__));
  Serial.println(F("════════════════════════════════"));

  // ── Rétroéclairage PWM (API core v3.x) ──
  ledcAttach(LED_PIN, 5000, 8);       // pin, fréquence, résolution bits
  ledcWrite(LED_PIN, 255);            // plein dès le boot (splash visible)
  g_screenOn    = true;
  lastTouchTime = millis();

  // ── Touch IRQ ──
  pinMode(TOUCH_IRQ, INPUT_PULLUP);   // actif LOW, pull-up interne

  // ── Splash ──
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(12, 8);
  tft.print("Pool Data");
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(12, 50);
  tft.print(FW_VERSION "   " __DATE__);
  tft.drawFastHLine(0, 63, 320, TFT_DARKGREY);
  splashY = 72;

  // ── BME280 ──
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(100);
  splashLog("> BME280...");
  g_bmeOK = bme.begin(0x76);
  if (!g_bmeOK) g_bmeOK = bme.begin(0x77);
  if (!g_bmeOK) {
    splashLog("  NON DETECTE — verif SDA/SCL", TFT_RED);
    while (1) delay(1000);
  }
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::FILTER_OFF);
  {
    bme.takeForcedMeasurement();
    float t0 = bme.readTemperature() + BME_TEMP_OFFSET;
    char msg[48];
    snprintf(msg, sizeof(msg), "  OK  %.1f C  (offset %.1f C)", t0, (float)BME_TEMP_OFFSET);
    splashLog(msg, TFT_GREEN);
  }

  // ── DS18B20 ──
  splashLog("> DS18B20...");
  ds18b20.begin();
  ds18b20.setWaitForConversion(false);
  ds18b20.requestTemperatures();
  { unsigned long t = millis();
    while (!ds18b20.isConversionComplete() && millis() - t < 1000) delay(5); }
  float tTest = ds18b20.getTempCByIndex(0);
  g_dsOK = (tTest != DEVICE_DISCONNECTED_C);
  if (g_dsOK) {
    char msg[32]; snprintf(msg, sizeof(msg), "  OK  %.1f C", tTest);
    splashLog(msg, TFT_GREEN);
  } else {
    splashLog("  non detecte — verif GPIO17 + 4.7k", TFT_ORANGE);
  }

  // ── WiFi ──
  splashLog("> WiFi connexion...");
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  {
    unsigned long t0 = millis();
    int ndot = 0;
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT) {
      tft.fillRect(0, splashY, 260, 12, TFT_BLACK);
      tft.setTextFont(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(1);
      tft.setCursor(24, splashY + 2);
      for (int i = 0; i <= ndot % 40; i++) tft.print(".");
      ndot++;
      delay(500);
    }
    tft.fillRect(0, splashY, 260, 12, TFT_BLACK);
  }
  g_wifiOK = (WiFi.status() == WL_CONNECTED);
  if (g_wifiOK) {
    IPAddress ip = WiFi.localIP();
    snprintf(g_ipBuf, sizeof(g_ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    int rssi = WiFi.RSSI();
    { char msg[56]; snprintf(msg, sizeof(msg), "  OK  %s  %d dBm", g_ipBuf, rssi);
      splashLog(msg, TFT_GREEN); }
    { char msg[32]; snprintf(msg, sizeof(msg), "  AP: %s", WiFi.BSSIDstr().c_str());
      splashLog(msg, TFT_DARKGREY); }
    drawWifiBars(286, splashY - 5, rssi, true);
  } else {
    splashLog("  WiFi ECHEC (timeout 30 s)", TFT_ORANGE);
    strcpy(g_ipBuf, "no WiFi");
  }

  // ── NTP ──
  if (g_wifiOK) {
    delay(500);
    splashLog("> NTP sync...");
    configTzTime(TZ_STR, NTP1, NTP2, NTP3);
    { struct tm ti;
      unsigned long t0 = millis();
      while (!getLocalTime(&ti) && millis() - t0 < NTP_TIMEOUT) delay(200);
      g_ntpOK = getLocalTime(&ti); }
    if (g_ntpOK) {
      struct tm ti; getLocalTime(&ti);
      char msg[36]; strftime(msg, sizeof(msg), "  OK  %H:%M:%S  (CET)", &ti);
      splashLog(msg, TFT_GREEN);
    } else {
      splashLog("  NTP ECHEC (port UDP 123 bloque ?)", TFT_ORANGE);
    }
    lastNTPSync = millis();
  }

  // ── ArduinoOTA ──
  if (g_wifiOK) {
    splashLog("> OTA init...");
    ArduinoOTA.setHostname("pool-data");
    ArduinoOTA.setPassword(SECRET_OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
      esp_task_wdt_reset();
      Serial.println(F("OTA : debut"));
      ledcWrite(LED_PIN, 255);              // écran allumé pendant l'OTA
      g_screenOn = true;
      tft.fillScreen(TFT_BLACK);
      tft.setFreeFont(&FreeSansBold9pt7b);
      tft.setTextSize(1);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("OTA UPDATE", 160, 80);
      tft.setTextDatum(TL_DATUM);
      tft.fillRect(4, 130, 312, 20, TFT_DARKGREY);  // fond barre de progression
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      esp_task_wdt_reset();
      int w = (int)(312UL * progress / total);
      tft.fillRect(4,     130, w,       20, TFT_CYAN);
      tft.fillRect(4 + w, 130, 312 - w, 20, TFT_DARKGREY);
      tft.setTextFont(2); tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      char pctBuf[8]; snprintf(pctBuf, sizeof(pctBuf), "%3d%%", (int)(progress * 100UL / total));
      tft.setCursor(140, 160); tft.print(pctBuf);
    });

    ArduinoOTA.onEnd([]() {
      Serial.println(F("\nOTA : termine — reboot"));
      tft.setFreeFont(&FreeSansBold9pt7b);
      tft.setTextSize(1);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("OK — Reboot...", 160, 190);
      tft.setTextDatum(TL_DATUM);
    });

    ArduinoOTA.onError([](ota_error_t error) {
      char errBuf[24]; snprintf(errBuf, sizeof(errBuf), "OTA erreur [%u]", error);
      Serial.println(errBuf);
      tft.setFreeFont(&FreeSansBold9pt7b);
      tft.setTextSize(1);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("OTA ERREUR !", 160, 190);
      tft.setTextDatum(TL_DATUM);
    });

    ArduinoOTA.begin();
    splashLog("  OTA OK  (pool-data)", TFT_GREEN);
  }

  delay(2000);

  // ── Chargement stats NVS ──
  loadStats();

  // ── Vue principale ──
  g_view = 0;
  drawCurrentView();

  // Première lecture immédiate, premier envoi TS différé de 5 min
  lastRead = millis() - READ_INTERVAL;
  lastTS   = millis();

  // ── Watchdog 30 s ──
  // Core v3.x initialise le TWDT avant setup() → deinit d'abord
  esp_task_wdt_deinit();
  const esp_task_wdt_config_t wdtCfg = {
    .timeout_ms    = 30000,
    .idle_core_mask = 0,
    .trigger_panic  = true
  };
  esp_task_wdt_init(&wdtCfg);
  esp_task_wdt_add(NULL);
  Serial.println(F("Watchdog 30 s actif — pret"));
}


// ─────────────────────────────────────────────────────────────
void loop() {
  esp_task_wdt_reset();
  ArduinoOTA.handle();
  unsigned long now = millis();

  // ── Monitoring WiFi ──────────────────────────────────────
  bool wifiNow = (WiFi.status() == WL_CONNECTED);
  if (wifiNow != g_wifiOK) {
    g_wifiOK = wifiNow;
    Serial.println(g_wifiOK ? F("WiFi reconnecte") : F("WiFi perdu"));
    if (g_view == 0 && g_screenOn) updateHeader();
  }

  // ── Touch detection ─────────────────────────────────────────
  //   court (< 1500 ms)  : vue suivante
  //   double tap (< 400 ms entre deux taps) : retour vue 0
  //   long (>= 1500 ms, vue 4) : reset stats
  //   n'importe quel tap sur économiseur : réveil
  static bool          lastIRQ        = HIGH;
  static unsigned long touchStartMs   = 0;
  static bool          longPressDone  = false;
  static unsigned long lastTapRelease = 0;    // horodatage dernier tap court (double tap)
  bool irqNow = digitalRead(TOUCH_IRQ);

  if (lastIRQ == HIGH && irqNow == LOW) {          // front descendant = début toucher
    if (now - touchStartMs >= 250) {               // anti-rebond
      touchStartMs = now; longPressDone = false;
      if (g_screensaverOn) {
        wakeFromScreensaver();
        longPressDone = true;                      // évite changement de vue au lâcher
      } else if (!g_screenOn) {
        screenOn(); drawCurrentView();
        longPressDone = true;
      } else if (lastTapRelease > 0 && now - lastTapRelease < 500) {
        // Double tap : 2e appui dans les 500 ms du lâcher précédent → vue 0
        g_view = 0; screenOn(); drawCurrentView();
        lastTapRelease = 0; longPressDone = true;
      }
    }
  }

  if (lastIRQ == LOW && irqNow == HIGH) {          // front montant = fin toucher
    unsigned long held = now - touchStartMs;
    if (g_screenOn && !g_screensaverOn && !longPressDone && held >= 30 && held < 1500) {
      g_view = (g_view + 1) % VIEW_COUNT;          // tap simple → vue suivante
      screenOn(); drawCurrentView();
      lastTapRelease = now;
    }
    touchStartMs = 0; longPressDone = false;
  }

  if (irqNow == LOW && touchStartMs > 0 && !longPressDone && g_screenOn && !g_screensaverOn) {
    if (now - touchStartMs >= 1500 && g_view == 4) {
      longPressDone = true;
      screenOn();
      resetStats();
      tft.fillRect(4, Y_HDR + 50, 312, 80, TFT_BLACK);
      tft.setFreeFont(&FreeSansBold9pt7b); tft.setTextSize(1);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("Stats reinitialisees !", 160, Y_HDR + 90);
      tft.setTextDatum(TL_DATUM); tft.setTextFont(1);
      esp_task_wdt_reset(); delay(1500);
      drawViewStats();
    }
  }

  lastIRQ = irqNow;

  // ── Économiseur d'écran après SCREEN_TIMEOUT ────────────
  if (g_screenOn && !g_screensaverOn && (now - lastTouchTime >= SCREEN_TIMEOUT)) {
    enterScreensaver();
  }
  // Refresh économiseur toutes les 30 s (mise à jour heure)
  if (g_screensaverOn && (now - g_saverRefresh >= 30000)) {
    drawScreensaver();
    g_saverRefresh = now;
  }

  // ── Refresh sélectif page debug toutes les 1 s ──
  if (g_screenOn && g_view == 3 && (now - g_debugLastRefresh >= 1000)) {
    refreshDebugVolatile();
  }

  // ── Lecture capteurs toutes les 5 min ────────────────────
  if (now - lastRead >= READ_INTERVAL) {
    lastRead = now;

    bme.takeForcedMeasurement();
    float rawAir = bme.readTemperature();
    g_bmeOK = !isnan(rawAir) && (rawAir > -40.f) && (rawAir < 85.f);
    if (g_bmeOK) {
      g_tempAir = rawAir + BME_TEMP_OFFSET;
      g_hum     = bme.readHumidity();
      g_press   = bme.readPressure() / 100.0f;
    } else {
      g_tempAir = NAN;
      g_hum     = NAN;
      g_press   = NAN;
    }

    ds18b20.requestTemperatures();
    { unsigned long cs = millis();
      while (!ds18b20.isConversionComplete() && millis() - cs < 1000) delay(5); }
    g_tempEau = ds18b20.getTempCByIndex(0);
    g_dsOK    = (g_tempEau != DEVICE_DISCONNECTED_C) && !isnan(g_tempEau);
    if (g_dsOK) g_dsReadOK++; else g_dsReadErr++;

    g_readCount++;
    addHistoryPoint(g_tempAir, g_tempEau);
    updateStats(g_tempEau, g_tempAir);

    // Redessine la vue courante ou rafraîchit l'économiseur
    if (g_screenOn)      drawCurrentView();
    else if (g_screensaverOn) { drawScreensaver(); g_saverRefresh = now; }

    serialTimestamp();
    Serial.print(F("Air:")); Serial.print(g_tempAir, 1); Serial.print(F("C  "));
    Serial.print(F("Hum:")); Serial.print(g_hum, 1);     Serial.print(F("%  "));
    Serial.print(F("Prs:")); Serial.print(g_press, 1);   Serial.print(F("hPa  "));
    Serial.print(F("Eau:"));
    if (g_dsOK) { Serial.print(g_tempEau, 1); Serial.print(F("C  ")); }
    else         { Serial.print(F("ERR  ")); }
    if (g_wifiOK) {
      Serial.print(F("RSSI:")); Serial.print(WiFi.RSSI()); Serial.print(F("dBm"));
      Serial.print(F("  AP:")); Serial.println(WiFi.BSSIDstr());
    } else { Serial.println(); }
  }

  // ── ThingSpeak toutes les 5 min ───────────────────────────
  if (g_wifiOK && (now - lastTS >= TS_INTERVAL)) {
    lastTS = now;
    sendThingSpeak();
    // Les compteurs TS seront visibles dans refreshDebugVolatile() ≤1s — pas de redraw complet ici
    g_debugLastRefresh = 0;   // force un refresh volatile immédiat au prochain tick
  }

  // ── Resync NTP toutes les 1 h ─────────────────────────────
  if (g_wifiOK && (now - lastNTPSync >= NTP_SYNC_INTERVAL)) {
    lastNTPSync = now;
    configTzTime(TZ_STR, NTP1, NTP2, NTP3);
    unsigned long t0 = millis();
    struct tm ti;
    while (!getLocalTime(&ti) && millis() - t0 < 5000) delay(200);
    g_ntpOK = getLocalTime(&ti);
    Serial.println(g_ntpOK ? F("NTP resync OK") : F("NTP resync ECHEC"));
    if (g_view == 0 && g_screenOn) updateHeader();
  }

  // ── Reconnexion WiFi active toutes les 30 s si besoin ────
  if (!g_wifiOK && (now - lastWifiRetry >= 30000)) {
    lastWifiRetry = now;
    Serial.println(F("WiFi HS — tentative reconnexion..."));
    WiFi.reconnect();
  }
}
