# CLAUDE.md — Pool Data

Ce fichier fournit le contexte à Claude Code pour ce projet.

## Vue d'ensemble

Sketch ESP32 D1 Mini : acquisition capteurs BME280 + DS18B20 + affichage TFT ILI9341 2.4" tactile + WiFi + NTP + envoi ThingSpeak toutes les 5 min. Fichier unique : `Pool-Data.ino`.

## Version courante

`v1.4` — définie par `#define FW_VERSION "v1.4"` en tête du `.ino`. Voir `CHANGELOG.md`.

## Composants

| Composant                  | Rôle                                | Interface                                   |
|----------------------------|-------------------------------------|---------------------------------------------|
| D1 Mini ESP32              | Microcontrôleur                     | —                                           |
| BME280 (4 pins)            | Température air, humidité, pression | I²C (SDA=GPIO21, SCL=GPIO22, addr 0x76)    |
| DS18B20                    | Température eau                     | OneWire (GPIO17)                            |
| TFT ILI9341 2.4" 320×240  | Affichage + touch                   | SPI (MOSI=23, SCK=18, CS=5, DC=26, RST=16) |
| Touch XPT2046 (intégré)    | Détection toucher                   | T_IRQ → GPIO13 (actif LOW)                 |

> Le module BME280 est un **4 pins** (VCC, GND, SCL, SDA) — adresse I²C 0x76 fixée en interne.

## Câblage — D1 Mini ESP32

```
     GAUCHE inner (pos/haut→bas)     DROITE inner (pos/haut→bas)
     ─────────────────────────────────────────────────────────────
 1   RST                                  TXD(1)
 2   SVP(36)                              RXD(3)
 3   IO26 ──→ TFT DC                      IO22 ──→ BME280 SCL
 4   IO18 ──→ TFT SCK                     IO21 ──→ BME280 SDA
 5   IO19 ──→ TFT LED (PWM backlight)     IO17 ──→ DS18B20 DATA
 6   IO23 ──→ TFT SDI/MOSI               IO16 ──→ TFT RST
 7   IO5  ──→ TFT CS                      GND  ──→ rail −
 8   3V3  ──→ rail +                      VCC  (idem 3V3)
 9   TCK(13) ←── TFT T_IRQ               TDO  (libre)
10   SD3  (libre)                         SDD  (libre)
                      [ USB Micro ]
```

### TFT ILI9341 — connecteurs (vue arrière, de gauche à droite)

```
VCC  GND  CS  RESET  DC  SDI  SCK  LED  SDO  T_CLK  T_CS  T_DIN  T_DO  T_IRQ
 │    │    │    │     │    │    │    │    │      NC    NC    NC     NC    │
3V3  GND  IO5 IO16  IO26 IO23 IO18 IO19  NC     (non connectés)        IO13
```

### BME280 4 pins

| BME280 | D1 Mini     |
|--------|-------------|
| VCC    | rail + (3V3)|
| GND    | rail −      |
| SCL    | IO22        |
| SDA    | IO21        |

### DS18B20

| DS18B20       | Connexion                              |
|---------------|----------------------------------------|
| VCC (rouge)   | rail + (3V3)                           |
| GND (noir)    | rail −                                 |
| DATA (jaune)  | IO17                                   |
| DATA ↔ VCC    | résistance **4.7 kΩ** ⚠️ obligatoire  |

## Configuration TFT_eSPI

Fichier : `~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h`

```cpp
#define ILI9341_DRIVER
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS    5
#define TFT_DC   26
#define TFT_RST  16
#define SPI_FREQUENCY 10000000
```

## Bibliothèques requises (Arduino Library Manager)

| Bibliothèque              | Usage                              |
|---------------------------|------------------------------------|
| `Adafruit BME280 Library` | BME280 (+ Adafruit Unified Sensor) |
| `DallasTemperature`       | DS18B20                            |
| `OneWire`                 | Bus OneWire DS18B20                |
| `TFT_eSPI` (Bodmer)       | Affichage TFT ILI9341 + FreeFonts  |
| `Wire`, `WiFi`, `time.h`  | I²C, WiFi, NTP — core ESP32        |
| `ArduinoOTA`              | Mise à jour firmware par WiFi — core ESP32 |
| `Preferences`             | Stockage NVS (stats min/max) — core ESP32  |
| `esp_task_wdt`            | Watchdog — core ESP32              |
| `HTTPClient`              | Envoi ThingSpeak — core ESP32      |

Board : **ESP32 Dev Module** via `sketch.yaml`.

## Constantes clés

| Constante           | Valeur        | Rôle                                |
|---------------------|---------------|-------------------------------------|
| `READ_INTERVAL`     | 300 000 ms    | Lecture capteurs (5 min)            |
| `TS_INTERVAL`       | 300 000 ms    | Envoi ThingSpeak (5 min)            |
| `NTP_SYNC_INTERVAL` | 3 600 000 ms  | Resync NTP (1 h)                    |
| `SCREEN_TIMEOUT`    | 300 000 ms    | Auto-extinction écran (5 min)       |
| `BME_TEMP_OFFSET`   | −0.4°C        | Correction auto-échauffement BME280 |
| `ONE_WIRE_BUS`      | 17            | GPIO DS18B20                        |
| `LED_PIN`           | 19            | GPIO rétroéclairage TFT (PWM)       |
| `TOUCH_IRQ`         | 13            | GPIO T_IRQ tactile (actif LOW)      |
| `Y_HDR`             | 42            | Hauteur uniforme du header (toutes vues) |
| `VIEW_COUNT`        | 5             | Nombre de vues                      |
| `GRAPH_POINTS`      | 576           | Buffer historique (48h × 12 pts/h)  |
| Watchdog            | 30 000 ms     | Reboot si `loop()` bloqué           |

## ThingSpeak — champs envoyés

| Field    | Donnée                    | Unité |
|----------|---------------------------|-------|
| `field1` | Température air (BME280)  | °C    |
| `field2` | Température eau (DS18B20) | °C    |
| `field3` | Humidité (BME280)         | %     |
| `field4` | Pression (BME280)         | hPa   |
| `field5` | RSSI WiFi                 | dBm   |

## Architecture — 5 vues (touch court = suivante, appui long sur vue 4 = reset stats)

```
Vue 0 — POOL DATA (Main)
  Header navy 42px : "POOL DATA" | HH:MM | barres WiFi
  Zone Air  (y=42..140) : icône thermomètre + température (FreeSansBold18pt×2)
  Zone Eau  (y=141..240): icône goutte    + température (FreeSansBold18pt×2)
  updateHeader() : rafraîchit heure + barres WiFi toutes les 5 min

Vue 1 — HUM/PRESS
  Header navy 42px : centré (TC_DATUM)
  Humidité (FreeSansBold24pt, TFT_YELLOW, centré)
  Pression (FreeSansBold24pt, TFT_MAGENTA, centré)

Vue 2 — HISTORIQUE (Graphe)
  Header navy 42px : "HISTORIQUE" centré
  drawGraph(Y_HDR+6, 224) → 176 px de hauteur utile
  Buffer circulaire 576 pts × 5 min = 48h — Air (vert) + Eau (cyan)
  Step dynamique : stepF = GRAPH_W / (histCount-1)
  Repères horaires adaptatifs : toutes les 1h (stepF×12 ≥ 16px) ou 6h
  Légende intégrée en haut-droite du graphe

Vue 3 — INFOS SYSTEME (Debug)
  Header navy 42px : "INFOS SYSTEME  v1.3" centré
  12 lignes Font2, dy=16, y=44..236
  Lignes 1,6,9,10,11 rafraîchies toutes les 1 s (refreshDebugVolatile)

Vue 4 — STATISTIQUES
  Header navy 42px : "STATISTIQUES  v1.3" centré
  T° eau min/max + horodatage (dd/mm HH:MM)
  T° air min/max + horodatage
  Écart Eau-Air (magenta/cyan selon signe)
  Tendance 1h (hausse/stable/baisse, couleur)
  Bouton arrondi bas : "Maintenir appuye pour reinitialiser"
  Appui long ≥1500 ms → resetStats() + confirmation rouge 1,5 s
  Persistance NVS (namespace "pool-s", Preferences)
```

## Page INFOS SYSTEME — layout 12 lignes

```
 1  09/06/2026  14:51                           ← volatile (1s)
 2  BME280 : OK          DS18B20 : OK
 3  IP : 192.168.1.64
 4  AP : FA:A7:31:56:37:52
 5  MAC : CC:7B:5C:26:C8:00
 6  RSSI : -67dBm   WiFi : OK   NTP : OK        ← volatile (1s)
 7  Air : 25.3C          Eau : 25.3C
 8  Hum : 65.2%          P : 1013.5hPa
 9  Uptime : 0h02m       Heap : 182kB            ← volatile (1s)
10  ThingSpeak    OK : 5    Failed : 0            ← volatile (1s)
11  Last TS : 47s         Next TS : 253s         ← volatile (1s)
12  Reset : PowerOn
```

Colonnes fixes : **x=160** (lignes 2 col), **x=140/232** (RSSI/WiFi/NTP), **x=120/200/262** (ThingSpeak).

Typographie française : espace avant et après `:` sauf dans les formats d'heure (`%H:%M`).

## Architecture code

```
setup()
  ├── Serial.begin + bannière version
  ├── LEDC PWM backlight (GPIO19, 5kHz, 8bit) → pleine luminosité
  ├── pinMode(TOUCH_IRQ, INPUT_PULLUP)
  ├── tft.init() + splash
  ├── Wire.begin(21,22) + BME280 init (mode forcé)
  ├── DS18B20 init + setWaitForConversion(false)
  ├── WiFi.begin() + boucle timeout 30 s
  ├── configTzTime() + boucle getLocalTime() 10 s
  ├── ArduinoOTA.setHostname("pool-data") + setPassword() + onStart/onProgress/onEnd/onError + begin()
  ├── loadStats() → NVS Preferences
  ├── drawCurrentView() → Vue 0 Main
  ├── esp_task_wdt_deinit() + esp_task_wdt_init(30s) + esp_task_wdt_add()
  └── lastTouchTime = millis()

loop()  [~10 ms par itération]
  ├── esp_task_wdt_reset()
  ├── ArduinoOTA.handle()
  ├── Monitoring WiFi (wifiOK → updateHeader si vue 0)
  ├── Touch : 3 états (front desc/mont/maintenu) → vue suivante / réveil / reset stats
  ├── Auto-extinction : now - lastTouchTime >= SCREEN_TIMEOUT
  ├── refreshDebugVolatile() si vue 3 + écran ON + elapsed >= 1000 ms
  ├── toutes les 5 min :
  │    ├── BME280 takeForcedMeasurement()
  │    ├── DS18B20 requestTemperatures() + boucle isConversionComplete()
  │    ├── addHistoryPoint() → buffer circulaire
  │    ├── drawCurrentView() si écran ON
  │    └── Serial log
  ├── toutes les 5 min (offset possible) :
  │    ├── sendThingSpeak() → HTTP GET api.thingspeak.com (timeout 8s)
  │    └── g_debugLastRefresh = 0 → force refresh volatile immédiat
  └── toutes les 1 h : configTzTime() resync NTP
```

## Points d'implémentation importants

- **`refreshDebugVolatile()`** : overwrite direct sans `fillRect` (pas de flash noir) — Font2 bitmap redessine fond + texte pixel par pixel. Padding trailing spaces pour couvrir les variations de longueur
- **Watchdog ESP32 Core v3.x** : `esp_task_wdt_deinit()` obligatoire avant `esp_task_wdt_init()` (le TWDT est auto-initialisé avant `setup()`)
- **`setFreeFont()` ne reset pas textSize** : toujours appeler `setTextSize(1)` explicitement après
- **Avec FreeFont + `setCursor(x,y)`** : `y` = baseline (pas top). Avec `TC_DATUM + drawString` : `y` = top du texte
- **Symbole °** : donut `fillCircle(r=6, color)` + `fillCircle(r=4, TFT_BLACK)` = anneau 2px, proportionnel à la fonte 66px effective
- **Double refresh debug évité** : pas d'appel `drawViewDebug()` dans le bloc ThingSpeak — `g_debugLastRefresh=0` force le volatile refresh au prochain tick
- **Barres WiFi** : `drawWifiBars(281, 30, rssi)` — `y_baseline=30` aligne les barres avec le bas du texte "POOL DATA" (FreeSansBold9pt7b : top=y12, bottom=y30)

## secrets.h (ne jamais committer)

```cpp
#pragma once
#define SECRET_SSID         "nom_wifi"
#define SECRET_PASSWORD     "mot_de_passe"
#define SECRET_OTA_PASSWORD "mot_de_passe_ota"
#define SECRET_API_KEY      "CLE_THINGSPEAK"
#define SECRET_CHANNEL_ID   467925
```

Fichier exclu du dépôt via `.gitignore` — **ne jamais committer**. Le dépôt GitHub est **public**.

## ArduinoOTA — mise à jour firmware par WiFi

### Configuration dans le code

```cpp
ArduinoOTA.setHostname("pool-data");
ArduinoOTA.setPassword(SECRET_OTA_PASSWORD);  // dans secrets.h
ArduinoOTA.handle();  // appelé à chaque itération de loop()
esp_task_wdt_reset(); // dans onProgress → watchdog maintenu pendant le flash
```

### Procédure upload OTA

1. S'assurer que le Mac et l'ESP32 sont sur le **même sous-réseau** (même SSID ou sous-réseau bridgé)
2. Dans Arduino IDE : **Outils → Port → Ports réseau → pool-data at 192.168.x.x**
3. Si le port n'apparaît pas : vérifier *Réglages Système → Confidentialité et sécurité → Réseau local → Arduino IDE → ON*
4. Renseigner le mot de passe OTA dans la boîte de dialogue (valeur de `SECRET_OTA_PASSWORD`)
5. **Upload** Ctrl+U — barre de progression cyan sur le TFT, puis reboot automatique

> Le champ password ne peut pas être vide dans Arduino IDE 2.x (bouton Upload grisé).

### Ecran OTA pendant le flash

Le sketch affiche une vue dédiée pendant l'OTA :
- Barre de progression cyan + pourcentage
- Message "OTA OK — Reboot..." ou "OTA Erreur : <code>" en fin de flash
- Rétroéclairage forcé ON (même si l'écran était éteint)

### Précautions

- **Toujours flasher en USB au moins une fois** après un changement de code OTA (ex. nouveau mot de passe, changement hostname) — l'OTA ne peut pas se mettre à jour elle-même à chaud
- Si l'OTA échoue en cours de flash, l'ESP32 reste sur l'ancien firmware (rollback automatique)
- Le watchdog 30 s est maintenu via `esp_task_wdt_reset()` dans `onProgress` — pas de reboot intempestif pendant un flash lent

## Flash & debug

### USB (première fois ou si OTA inaccessible)

1. Ouvrir `Pool-Data.ino` dans Arduino IDE 2.x
2. Board auto-sélectionné via `sketch.yaml` (sinon : *ESP32 Dev Module*, partition *min_spiffs*)
3. Sélectionner le port COM/série
4. **Verify** Ctrl+R — **Upload** Ctrl+U
5. Serial Monitor à **115200 baud**

### OTA (mises à jour courantes)

Voir section ArduinoOTA ci-dessus.
