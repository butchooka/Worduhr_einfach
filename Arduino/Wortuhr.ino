/*
  ============================================================================
  Worduhr einfach  -  Wortuhr mit 16x16 WS2812-Matrix, ESP32, ohne Home Assistant
  ============================================================================
  Projekt:   https://github.com/butchooka/Worduhr_einfach
  MakerWorld: https://makerworld.com/de/@butchooka

  BENÖTIGTE BOARDS / BIBLIOTHEKEN
  ----------------------------------------------------------------------------
  1) ESP32-Boardpaket von Espressif, Version 3.x oder neuer
     https://github.com/espressif/arduino-esp32
     Boardverwalter-URL:
     https://espressif.github.io/arduino-esp32/package_esp32_index.json
     (enthält bereits: WiFi, WebServer, DNSServer, ESPmDNS, Preferences)

  2) FastLED, Version 3.9 oder neuer  (Bibliotheksverwalter: "FastLED")
     https://github.com/FastLED/FastLED

  Mehr wird nicht gebraucht.

  BOARD-EINSTELLUNG
  ----------------------------------------------------------------------------
  Wemos Mini ESP32:  "WEMOS LOLIN32" oder "ESP32 Dev Module"
  ESP32-C6:          "ESP32C6 Dev Module" (LED_PIN unten anpassen)

  ERSTER START
  ----------------------------------------------------------------------------
  Ist kein WLAN gespeichert (oder nicht erreichbar), öffnet die Uhr nach 20 s den
  offenen Hotspot "Worduhr". Verbinden, die Konfigurationsseite öffnet sich
  automatisch (sonst http://192.168.4.1), unten unter "WLAN" das Heim-WLAN eintragen.
  Danach erreichbar unter http://worduhr.local oder der IP aus dem Router.

  BUCHSTABENRASTER (8 Buchstabenzeilen x 16 Spalten, je Zeile 2 LED-Zeilen)
  ----------------------------------------------------------------------------
    Zeile 0   E S X I S T X X X X X X X X X X
    Zeile 1   Z W A N Z I G H A L B X Z E H N
    Zeile 2   F Ü N F V I E R T E L X N A C H
    Zeile 3   V O R X H A L B X Z E H N E L F
    Zeile 4   Z W E I D R E I N E U N V I E R
    Zeile 5   A C H T E I N S E C H S F Ü N F
    Zeile 6   S I E B E N Z W Ö L F X X U H R
    Zeile 7   X + X 1 2 3 4 X M I N U T E N X
  (Zeile 5: EINS und SECHS teilen sich das S. Zeile 7 hat nur 15 Zeichen,
   ein X am Ende wurde ergänzt.)
  Weicht dein Raster ab, nur die Wort-Tabelle (Word W_xxx = {Zeile, Spalte, Länge})
  weiter unten anpassen.
  ============================================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <FastLED.h>
#include <time.h>
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "webpage.h"

// ----------------------------------------------------------------------------
// Hardware / Grundeinstellungen
// ----------------------------------------------------------------------------
#define LED_PIN      32          // Datenpin der Matrix (Wemos Mini ESP32: 32; beim ESP32-C6 z. B. 4)
#define COLOR_ORDER  GRB         // bei falschen Farben z. B. RGB probieren
#define MATRIX_W     16
#define MATRIX_H     16
#define NUM_LEDS     (MATRIX_W * MATRIX_H)
#define AP_NAME      "Worduhr"   // Hotspot-Name (offen, ohne Passwort)
#define HOSTNAME     "worduhr"
#define CFG_MAGIC    3

// Typen stehen bewusst ganz oben (wegen der automatischen Prototypen der Arduino IDE)
struct Word { uint8_t row, col, len; };
enum Effect { EFF_NONE, EFF_RAINBOW, EFF_AURORA, EFF_MATRIX, EFF_BLINK_RED, EFF_BLINK_GREEN, EFF_BLINK_BLUE, EFF_BLINK_WHITE };

CRGB leds[NUM_LEDS];
WebServer server(80);
DNSServer dns;
Preferences prefs;
const IPAddress apIP(192, 168, 4, 1);

// ----------------------------------------------------------------------------
// Konfiguration (wird im Flash gespeichert)
// ----------------------------------------------------------------------------
struct Config {
  uint8_t  magic;
  uint8_t  brightDay;      // 0..100 %
  uint8_t  brightNight;    // 0..100 %
  uint16_t maxMilliamps;   // Strombegrenzung
  uint8_t  colorMode;      // 0..7 Standardfarben, 8 = Zufall (jede Stunde), 9 = eigene
  uint32_t customColor;    // 0xRRGGBB
  float    lat, lon;       // Standort
  int16_t  nightOffset;    // Minuten zum Sonnenuntergang (Nachtbeginn)
  int16_t  morningOffset;  // Minuten zum Sonnenaufgang (Morgen Tag)
  uint8_t  dayNightMode;   // 0 = automatisch, 1 = immer Tag, 2 = immer Nacht
  uint8_t  ntpManual;      // 0 = per DHCP, 1 = eigener Server
  char     ntpServer[48];
  char     tz[56];         // POSIX-Zeitzone
  uint16_t effectSeconds;
  uint8_t  minuteDots;     // Einzelminuten (+1..+4 MINUTEN) anzeigen
  uint8_t  mirrorX, mirrorY, serpentine;
} cfg;

String savedSsid, savedPass;

void setDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CFG_MAGIC;
  cfg.brightDay = 80;
  cfg.brightNight = 15;
  cfg.maxMilliamps = 500;
  cfg.colorMode = 0;
  cfg.customColor = 0xFFFFFF;
  cfg.lat = 47.999f;
  cfg.lon = 7.842f;
  cfg.nightOffset = 0;
  cfg.morningOffset = 0;
  cfg.dayNightMode = 0;
  cfg.ntpManual = 0;
  strcpy(cfg.ntpServer, "pool.ntp.org");
  strcpy(cfg.tz, "CET-1CEST,M3.5.0,M10.5.0/3");
  cfg.effectSeconds = 20;
  cfg.minuteDots = 1;
  cfg.mirrorX = 0;
  cfg.mirrorY = 0;
  cfg.serpentine = 1;
}

void loadConfig() {
  setDefaults();
  if (prefs.getBytesLength("cfg") == sizeof(Config)) {
    Config tmp;
    prefs.getBytes("cfg", &tmp, sizeof(Config));
    if (tmp.magic == CFG_MAGIC) cfg = tmp;
  }
  cfg.ntpServer[sizeof(cfg.ntpServer) - 1] = 0;
  cfg.tz[sizeof(cfg.tz) - 1] = 0;
}

void saveConfig() { prefs.putBytes("cfg", &cfg, sizeof(Config)); }

// ----------------------------------------------------------------------------
// Matrix-Zuordnung: logisch (x = Spalte, y = LED-Zeile, 0 = oben) -> LED-Index
// ----------------------------------------------------------------------------
int XY(int x, int y) {
  if (cfg.mirrorX) x = MATRIX_W - 1 - x;
  if (cfg.mirrorY) y = MATRIX_H - 1 - y;
  int i = y * MATRIX_W;
  if (cfg.serpentine && (y & 1)) i += MATRIX_W - 1 - x;
  else i += x;
  return i;
}

// ----------------------------------------------------------------------------
// Farben
// ----------------------------------------------------------------------------
CRGB randomColor = CRGB::White;

CRGB currentColor() {
  static const CRGB presets[8] = {
    CRGB(255, 170, 80),   // 0 Warmweiß
    CRGB(255, 255, 255),  // 1 Weiß
    CRGB(255, 0, 0),      // 2 Rot
    CRGB(255, 100, 0),    // 3 Orange
    CRGB(0, 255, 0),      // 4 Grün
    CRGB(0, 200, 200),    // 5 Cyan
    CRGB(0, 0, 255),      // 6 Blau
    CRGB(255, 0, 160)     // 7 Magenta
  };
  if (cfg.colorMode < 8) return presets[cfg.colorMode];
  if (cfg.colorMode == 8) return randomColor;
  return CRGB((cfg.customColor >> 16) & 255, (cfg.customColor >> 8) & 255, cfg.customColor & 255);
}

// ----------------------------------------------------------------------------
// Wort-Tabelle  {Buchstabenzeile, Startspalte, Länge}
// ----------------------------------------------------------------------------

const Word W_ES        = {0, 0, 2};
const Word W_IST       = {0, 3, 3};
const Word W_ZWANZIG   = {1, 0, 7};
const Word W_HALB_OBEN = {1, 7, 4};    // HALB in Zeile 1
const Word W_ZEHN_M    = {1, 12, 4};   // ZEHN (Minuten)
const Word W_FUENF_M   = {2, 0, 4};    // FÜNF (Minuten)
const Word W_VIERTEL   = {2, 4, 7};
const Word W_NACH      = {2, 12, 4};
const Word W_VOR       = {3, 0, 3};
const Word W_HALB_UNT  = {3, 4, 4};    // HALB in Zeile 3
const Word W_ZEHN_H    = {3, 9, 4};    // ZEHN (Stunde)
const Word W_ELF       = {3, 13, 3};
const Word W_ZWEI      = {4, 0, 4};
const Word W_DREI      = {4, 4, 4};
const Word W_NEUN      = {4, 8, 4};
const Word W_VIER      = {4, 12, 4};
const Word W_ACHT      = {5, 0, 4};
const Word W_EINS      = {5, 4, 4};
const Word W_SECHS     = {5, 7, 5};    // teilt sich das S mit EINS
const Word W_FUENF_H   = {5, 12, 4};   // FÜNF (Stunde)
const Word W_SIEBEN    = {6, 0, 6};
const Word W_ZWOELF    = {6, 6, 5};
const Word W_UHR       = {6, 13, 3};

// Welches HALB leuchten soll: W_HALB_UNT (Zeile 3, steht nach VOR/NACH) oder W_HALB_OBEN (Zeile 1)
const Word& W_HALB = W_HALB_UNT;
// Minutenzeile (Zeile 7): "+" in Spalte 1, Ziffern 1..4 in Spalte 3..6, MINUTEN ab Spalte 8
const uint8_t MIN_ROW = 7, PLUS_COL = 1, DIGIT_COL0 = 3;
const Word W_MINUTEN = {7, 8, 7};

// Stunde 0..11 -> Wort (0 = zwölf)
const Word* HOUR_WORDS[12] = {
  &W_ZWOELF, &W_EINS, &W_ZWEI, &W_DREI, &W_VIER, &W_FUENF_H,
  &W_SECHS, &W_SIEBEN, &W_ACHT, &W_NEUN, &W_ZEHN_H, &W_ELF
};

// Ein Buchstabe = 2 LED-Zeilen
void setLetter(uint8_t row, uint8_t col, CRGB c) {
  leds[XY(col, row * 2)] = c;
  leds[XY(col, row * 2 + 1)] = c;
}

void drawWord(const Word& w, CRGB c, uint8_t len = 0) {
  uint8_t n = len ? len : w.len;
  for (uint8_t i = 0; i < n; i++) setLetter(w.row, w.col + i, c);
}

// ----------------------------------------------------------------------------
// Zeit / Tag-Nacht
// ----------------------------------------------------------------------------
bool timeValid() { return time(nullptr) > 1700000000; }

int sunriseMin = 0, sunsetMin = 0, sunPolar = 0;  // sunPolar: 0 normal, 1 Polartag, 2 Polarnacht
int sunYday = -1;
bool sunDirty = true;
bool nightNow = false;

int normMin(int m) { m %= 1440; if (m < 0) m += 1440; return m; }

String fmtMin(int m) {
  m = normMin(m);
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", m / 60, m % 60);
  return String(b);
}

int utcOffsetMinutes(time_t now) {
  struct tm l, g;
  localtime_r(&now, &l);
  gmtime_r(&now, &g);
  int d = (l.tm_hour * 60 + l.tm_min) - (g.tm_hour * 60 + g.tm_min);
  if (d > 720) d -= 1440;
  if (d < -720) d += 1440;
  return d;
}

// Sonnenauf-/untergang (NOAA-Näherung), Ergebnis in Minuten Ortszeit
void calcSun(const struct tm& lt, int offMin) {
  const double N = lt.tm_yday + 1;
  const double g = 2.0 * PI / 365.0 * (N - 1.0);
  const double eq = 229.18 * (0.000075 + 0.001868 * cos(g) - 0.032077 * sin(g)
                    - 0.014615 * cos(2 * g) - 0.040849 * sin(2 * g));
  const double decl = 0.006918 - 0.399912 * cos(g) + 0.070257 * sin(g)
                    - 0.006758 * cos(2 * g) + 0.000907 * sin(2 * g)
                    - 0.002697 * cos(3 * g) + 0.00148 * sin(3 * g);
  const double latR = cfg.lat * DEG_TO_RAD;
  const double cosH = (cos(90.833 * DEG_TO_RAD) - sin(latR) * sin(decl)) / (cos(latR) * cos(decl));
  sunPolar = 0;
  if (cosH > 1.0) sunPolar = 2;
  else if (cosH < -1.0) sunPolar = 1;
  else {
    const double ha = acos(cosH) * RAD_TO_DEG;
    const double riseUtc = 720.0 - 4.0 * (cfg.lon + ha) - eq;
    const double setUtc  = 720.0 - 4.0 * (cfg.lon - ha) - eq;
    sunriseMin = normMin((int)lround(riseUtc) + offMin);
    sunsetMin  = normMin((int)lround(setUtc) + offMin);
  }
  sunYday = lt.tm_yday;
  sunDirty = false;
}

bool computeNight(const struct tm& lt, time_t now) {
  if (cfg.dayNightMode == 1) return false;
  if (cfg.dayNightMode == 2) return true;
  if (sunDirty || sunYday != lt.tm_yday) calcSun(lt, utcOffsetMinutes(now));
  if (sunPolar == 1) return false;
  if (sunPolar == 2) return true;
  const int nowMin = lt.tm_hour * 60 + lt.tm_min;
  const int s = normMin(sunsetMin + cfg.nightOffset);
  const int r = normMin(sunriseMin + cfg.morningOffset);
  if (s >= r) return nowMin >= s || nowMin < r;
  return nowMin >= s && nowMin < r;
}

// ----------------------------------------------------------------------------
// NTP (DHCP oder fester Server)
// ----------------------------------------------------------------------------
static char ntpActive[48];
bool sntpRunning = false;

void startNtp() {
  setenv("TZ", cfg.tz, 1);
  tzset();
  if (sntpRunning) { esp_netif_sntp_deinit(); sntpRunning = false; }

  if (cfg.ntpManual && cfg.ntpServer[0]) {
    strlcpy(ntpActive, cfg.ntpServer, sizeof(ntpActive));
    esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntpActive);
    if (esp_netif_sntp_init(&sc) == ESP_OK) sntpRunning = true;
  } else {
    // Server aus DHCP (Option 42), falls der Router welche liefert.
    // Sonst greifen die beiden Fallback-Server.
    esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,
                             ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.cloudflare.com"));
    sc.server_from_dhcp = true;
    sc.renew_servers_after_new_IP = true;
    sc.index_of_first_server = 1;
    sc.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
    if (esp_netif_sntp_init(&sc) == ESP_OK) sntpRunning = true;
  }
}

// ----------------------------------------------------------------------------
// Uhr darstellen
// ----------------------------------------------------------------------------
void renderClock(const struct tm& t) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  const CRGB c = currentColor();
  drawWord(W_ES, c);
  drawWord(W_IST, c);

  const int m = t.tm_min;
  const int sector = m / 5;       // 0..11
  const int rem = m % 5;          // 0..4 Einzelminuten
  int h = t.tm_hour % 12;
  if (sector >= 5) h = (h + 1) % 12;

  switch (sector) {
    case 0:  drawWord(W_UHR, c); break;
    case 1:  drawWord(W_FUENF_M, c); drawWord(W_NACH, c); break;
    case 2:  drawWord(W_ZEHN_M, c);  drawWord(W_NACH, c); break;
    case 3:  drawWord(W_VIERTEL, c); drawWord(W_NACH, c); break;
    case 4:  drawWord(W_ZWANZIG, c); drawWord(W_NACH, c); break;
    case 5:  drawWord(W_FUENF_M, c); drawWord(W_VOR, c); drawWord(W_HALB, c); break;
    case 6:  drawWord(W_HALB, c); break;
    case 7:  drawWord(W_FUENF_M, c); drawWord(W_NACH, c); drawWord(W_HALB, c); break;
    case 8:  drawWord(W_ZWANZIG, c); drawWord(W_VOR, c); break;
    case 9:  drawWord(W_VIERTEL, c); drawWord(W_VOR, c); break;
    case 10: drawWord(W_ZEHN_M, c);  drawWord(W_VOR, c); break;
    case 11: drawWord(W_FUENF_M, c); drawWord(W_VOR, c); break;
  }

  // "ein Uhr" statt "eins Uhr"
  if (sector == 0 && h == 1) drawWord(W_EINS, c, 3);
  else drawWord(*HOUR_WORDS[h], c);

  // +1..+4 MINUTEN
  if (cfg.minuteDots && rem > 0) {
    setLetter(MIN_ROW, PLUS_COL, c);
    setLetter(MIN_ROW, DIGIT_COL0 + rem - 1, c);
    drawWord(W_MINUTEN, c);
  }
}

// Solange keine Uhrzeit vorliegt: "ES IST" pulsiert (orange = Hotspot, blau = verbinde/NTP)
bool apActive = false;

void renderNoTime() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  CRGB c = apActive ? CRGB(255, 120, 0) : CRGB(0, 70, 255);
  c.nscale8(beatsin8(30, 40, 255));
  drawWord(W_ES, c);
  drawWord(W_IST, c);
}

// ----------------------------------------------------------------------------
// Effekte
// ----------------------------------------------------------------------------
Effect effect = EFF_NONE;
uint32_t effectStart = 0, effectEnd = 0;
float mHead[MATRIX_W], mSpeed[MATRIX_W];
const CRGBPalette16 auroraPal = CRGBPalette16(CRGB(0, 10, 20), CRGB(0, 180, 90), CRGB(0, 110, 200), CRGB(110, 0, 190));

void initMatrixFx() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int i = 0; i < MATRIX_W; i++) {
    mHead[i] = -(float)random8(0, 20);
    mSpeed[i] = 5.0f + random8(0, 80) / 10.0f;
  }
}

void startEffect(Effect e) {
  effect = e;
  if (e == EFF_NONE) return;
  effectStart = millis();
  effectEnd = effectStart + (uint32_t)cfg.effectSeconds * 1000UL;
  if (e == EFF_MATRIX) initMatrixFx();
}

void renderEffect(uint32_t now, float dt) {
  const uint32_t t = now - effectStart;
  switch (effect) {
    case EFF_RAINBOW:
      for (int y = 0; y < MATRIX_H; y++)
        for (int x = 0; x < MATRIX_W; x++)
          leds[XY(x, y)] = CHSV((uint8_t)(t / 12) + x * 10 + y * 10, 255, 255);
      break;

    case EFF_AURORA:
      for (int y = 0; y < MATRIX_H; y++)
        for (int x = 0; x < MATRIX_W; x++) {
          uint8_t n = inoise8(x * 45, y * 30, (uint16_t)(t / 14));
          uint8_t idx = qsub8(n, 50);
          idx = qadd8(idx, idx);
          uint8_t v = qadd8(70, inoise8(x * 30 + 5000, y * 60, (uint16_t)(t / 9)));
          leds[XY(x, y)] = ColorFromPalette(auroraPal, idx, v, LINEARBLEND);
        }
      break;

    case EFF_MATRIX:
      fadeToBlackBy(leds, NUM_LEDS, 25);
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i].r = scale8(leds[i].r, 215);
        leds[i].b = scale8(leds[i].b, 215);
      }
      for (int x = 0; x < MATRIX_W; x++) {
        mHead[x] += mSpeed[x] * dt;
        if (mHead[x] >= MATRIX_H + 6) {
          mHead[x] = -(float)random8(0, 12);
          mSpeed[x] = 5.0f + random8(0, 80) / 10.0f;
        }
        int y = (int)floorf(mHead[x]);
        if (y >= 0 && y < MATRIX_H) leds[XY(x, y)] = CRGB(140, 255, 140);
      }
      break;

    case EFF_BLINK_RED:
    case EFF_BLINK_GREEN:
    case EFF_BLINK_BLUE:
    case EFF_BLINK_WHITE: {
      CRGB c = (effect == EFF_BLINK_RED) ? CRGB(255, 0, 0)
             : (effect == EFF_BLINK_GREEN) ? CRGB(0, 255, 0)
             : (effect == EFF_BLINK_BLUE) ? CRGB(0, 0, 255) : CRGB(255, 255, 255);
      c.nscale8(beatsin8(15, 0, 255, effectStart));   // ca. 4 s pro Zyklus
      fill_solid(leds, NUM_LEDS, c);
      break;
    }
    default: break;
  }
}

// ----------------------------------------------------------------------------
// WLAN / Hotspot
// ----------------------------------------------------------------------------
uint32_t lastConnectedMs = 0, apStarted = 0, lastReconnectMs = 0;
bool mdnsStarted = false;

void startAP() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_NAME);                 // kein Passwort -> offener Hotspot
  dns.start(53, "*", apIP);             // Captive Portal
  apActive = true;
  apStarted = millis();
}

void stopAP() {
  if (!apActive) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
}

void netTick() {
  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    lastConnectedMs = now;
    if (!mdnsStarted) {
      if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
    }
    // Hotspot abschalten, sobald WLAN steht und niemand mehr darauf verbunden ist
    if (apActive && WiFi.softAPgetStationNum() == 0 && now - apStarted > 10000) stopAP();
  } else {
    // WLAN weg (z. B. Router-Ausfall) -> nach 2 Minuten wieder Hotspot anbieten
    if (!apActive && now - lastConnectedMs > 120000) startAP();
    if (savedSsid.length() && now - lastReconnectMs > 60000) {
      lastReconnectMs = now;
      WiFi.reconnect();
    }
  }
}

// ----------------------------------------------------------------------------
// Webinterface
// ----------------------------------------------------------------------------
// Die HTML-Seite liegt in webpage.h (INDEX_HTML)

String jq(const String& in) {
  String o = "\"";
  for (unsigned i = 0; i < in.length(); i++) {
    char ch = in[i];
    if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
    else if ((uint8_t)ch < 0x20) o += ' ';
    else o += ch;
  }
  o += '"';
  return o;
}

String jsonState() {
  time_t n = time(nullptr);
  struct tm lt;
  localtime_r(&n, &lt);
  char tb[12] = "--:--:--";
  const bool tv = timeValid();
  if (tv) strftime(tb, sizeof(tb), "%H:%M:%S", &lt);

  String s;
  s.reserve(900);
  s += "{\"c\":{";
  s += "\"bd\":" + String(cfg.brightDay);
  s += ",\"bn\":" + String(cfg.brightNight);
  s += ",\"ma\":" + String(cfg.maxMilliamps);
  s += ",\"cm\":" + String(cfg.colorMode);
  s += ",\"cc\":" + String(cfg.customColor);
  s += ",\"lat\":" + String(cfg.lat, 4);
  s += ",\"lon\":" + String(cfg.lon, 4);
  s += ",\"no\":" + String(cfg.nightOffset);
  s += ",\"mo\":" + String(cfg.morningOffset);
  s += ",\"dn\":" + String(cfg.dayNightMode);
  s += ",\"nm\":" + String(cfg.ntpManual);
  s += ",\"ns\":" + jq(String(cfg.ntpServer));
  s += ",\"tz\":" + jq(String(cfg.tz));
  s += ",\"es\":" + String(cfg.effectSeconds);
  s += ",\"md\":" + String(cfg.minuteDots);
  s += ",\"mx\":" + String(cfg.mirrorX);
  s += ",\"my\":" + String(cfg.mirrorY);
  s += ",\"sp\":" + String(cfg.serpentine);
  s += "},\"s\":{";
  s += "\"tv\":" + String(tv ? "true" : "false");
  s += ",\"t\":" + jq(String(tb));
  s += ",\"night\":" + String(nightNow ? "true" : "false");
  String sr = "-", ss = "-";
  if (sunPolar == 0) { sr = fmtMin(sunriseMin + cfg.morningOffset); ss = fmtMin(sunsetMin + cfg.nightOffset); }
  else if (sunPolar == 1) { sr = "Polartag"; ss = "Polartag"; }
  else { sr = "Polarnacht"; ss = "Polarnacht"; }
  s += ",\"sr\":" + jq(sr);
  s += ",\"ss\":" + jq(ss);
  s += ",\"wc\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  s += ",\"ssid\":" + jq(WiFi.SSID());
  s += ",\"ip\":" + jq(WiFi.localIP().toString());
  s += ",\"ap\":" + String(apActive ? "true" : "false");
  s += ",\"eff\":" + String(effect != EFF_NONE ? "true" : "false");
  s += "}}";
  return s;
}

long argLong(const char* name, long def, long lo, long hi) {
  if (!server.hasArg(name)) return def;
  long v = server.arg(name).toInt();
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return v;
}

float argFloat(const char* name, float def, float lo, float hi) {
  if (!server.hasArg(name)) return def;
  String a = server.arg(name);
  a.replace(',', '.');
  float v = a.toFloat();
  return fmaxf(lo, fminf(hi, v));
}

void applyConfig(bool ntpChanged) {
  FastLED.setMaxPowerInVoltsAndMilliamps(5, cfg.maxMilliamps);
  sunDirty = true;
  if (ntpChanged) startNtp();
}

void handleSave() {
  cfg.brightDay     = argLong("bd", cfg.brightDay, 0, 100);
  cfg.brightNight   = argLong("bn", cfg.brightNight, 0, 100);
  cfg.maxMilliamps  = argLong("ma", cfg.maxMilliamps, 50, 10000);
  cfg.colorMode     = argLong("cm", cfg.colorMode, 0, 9);
  cfg.customColor   = (uint32_t)argLong("cc", cfg.customColor, 0, 0xFFFFFF);
  cfg.lat           = argFloat("lat", cfg.lat, -90.0f, 90.0f);
  cfg.lon           = argFloat("lon", cfg.lon, -180.0f, 180.0f);
  cfg.nightOffset   = argLong("no", cfg.nightOffset, -720, 720);
  cfg.morningOffset = argLong("mo", cfg.morningOffset, -720, 720);
  cfg.dayNightMode  = argLong("dn", cfg.dayNightMode, 0, 2);
  cfg.effectSeconds = argLong("es", cfg.effectSeconds, 1, 600);
  cfg.minuteDots    = argLong("md", cfg.minuteDots, 0, 1);
  cfg.mirrorX       = argLong("mx", cfg.mirrorX, 0, 1);
  cfg.mirrorY       = argLong("my", cfg.mirrorY, 0, 1);
  cfg.serpentine    = argLong("sp", cfg.serpentine, 0, 1);

  bool ntpChanged = false;
  uint8_t nm = argLong("nm", cfg.ntpManual, 0, 1);
  if (nm != cfg.ntpManual) ntpChanged = true;
  cfg.ntpManual = nm;

  String ns = server.arg("ns");
  ns.trim();
  if (ns.length() && ns.length() < sizeof(cfg.ntpServer) && ns != String(cfg.ntpServer)) {
    strcpy(cfg.ntpServer, ns.c_str());
    ntpChanged = true;
  }
  String tz = server.arg("tz");
  tz.trim();
  if (tz.length() && tz.length() < sizeof(cfg.tz) && tz != String(cfg.tz)) {
    strcpy(cfg.tz, tz.c_str());
    ntpChanged = true;
  }

  saveConfig();
  applyConfig(ntpChanged);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleEffect() {
  String n = server.arg("n");
  Effect e = EFF_NONE;
  if (n == "rainbow") e = EFF_RAINBOW;
  else if (n == "aurora") e = EFF_AURORA;
  else if (n == "matrix") e = EFF_MATRIX;
  else if (n == "red") e = EFF_BLINK_RED;
  else if (n == "green") e = EFF_BLINK_GREEN;
  else if (n == "blue") e = EFF_BLINK_BLUE;
  else if (n == "white") e = EFF_BLINK_WHITE;
  startEffect(e);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleScan() {
  int n = WiFi.scanNetworks(false, false);
  String o = "[";
  bool first = true;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    if (!first) o += ',';
    first = false;
    o += jq(s);
  }
  o += ']';
  WiFi.scanDelete();
  server.send(200, "application/json", o);
}

void handleWifi() {
  String s = server.arg("s"), p = server.arg("p");
  if (!s.length()) { server.send(400, "application/json", "{\"ok\":false}"); return; }
  prefs.putString("ssid", s);
  prefs.putString("pass", p);
  savedSsid = s;
  savedPass = p;
  WiFi.disconnect(false);
  WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  lastConnectedMs = millis();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleWifiReset() {
  prefs.remove("ssid");
  prefs.remove("pass");
  savedSsid = "";
  savedPass = "";
  WiFi.disconnect(false);
  startAP();
  server.send(200, "application/json", "{\"ok\":true}");
}

void setupServer() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/state", HTTP_GET, []() { server.send(200, "application/json", jsonState()); });
  server.on("/api/save", HTTP_POST, handleSave);
  server.on("/api/effect", HTTP_POST, handleEffect);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, handleWifi);
  server.on("/api/wifireset", HTTP_POST, handleWifiReset);
  // Captive Portal: alles Unbekannte (Handy-Verbindungstests) auf die Startseite umleiten
  server.onNotFound([]() {
    if (apActive) {
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "404");
    }
  });
  server.begin();
}

// ----------------------------------------------------------------------------
// setup / loop
// ----------------------------------------------------------------------------
void setBrightnessPercent(uint8_t pct) {
  FastLED.setBrightness((uint16_t)pct * 255 / 100);
}

void setup() {
  Serial.begin(115200);
  random16_add_entropy(esp_random());
  randomColor = CRGB(CHSV(random8(), 255, 255));

  prefs.begin("worduhr", false);
  loadConfig();
  savedSsid = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");

  FastLED.addLeds<WS2812B, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, cfg.maxMilliamps);
  setBrightnessPercent(cfg.brightDay);
  FastLED.clear(true);

  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  startNtp();   // vor dem Verbinden, damit DHCP-NTP-Server übernommen werden können

  if (savedSsid.length()) {
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());
    const uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
      renderNoTime();
      FastLED.show();
      delay(30);
    }
  }
  if (WiFi.status() != WL_CONNECTED) startAP();
  lastConnectedMs = millis();

  setupServer();
}

void loop() {
  static uint32_t lastFrame = 0, lastSec = 0;
  static int lastHour = -1;

  server.handleClient();
  if (apActive) dns.processNextRequest();
  netTick();

  const uint32_t now = millis();

  // Sekundentakt: Tag/Nacht bestimmen, Zufallsfarbe zur vollen Stunde wechseln
  if (now - lastSec >= 1000) {
    lastSec = now;
    if (timeValid()) {
      time_t t = time(nullptr);
      struct tm lt;
      localtime_r(&t, &lt);
      nightNow = computeNight(lt, t);
      if (lt.tm_hour != lastHour) {
        lastHour = lt.tm_hour;
        randomColor = CRGB(CHSV(random8(), 255, 255));
      }
    } else {
      nightNow = false;
    }
  }

  const uint32_t interval = (effect != EFF_NONE) ? 25 : 200;
  if (now - lastFrame < interval) { delay(1); return; }
  const float dt = (now - lastFrame) / 1000.0f;
  lastFrame = now;

  if (effect != EFF_NONE && now >= effectEnd) effect = EFF_NONE;

  setBrightnessPercent(nightNow ? cfg.brightNight : cfg.brightDay);

  if (effect != EFF_NONE) {
    renderEffect(now, dt);
  } else if (!timeValid()) {
    renderNoTime();
  } else {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    renderClock(lt);
  }
  FastLED.show();
}
