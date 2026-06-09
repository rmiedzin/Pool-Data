# Changelog — Pool Data

Toutes les modifications notables sont documentées ici.

---

## [v1.1] — 2026-06-09 — UI debug dynamique + typographie + alignements

### Ajouté
- **Page INFOS SYSTEME dynamique (Option C)** : `refreshDebugVolatile()` rafraîchit sélectivement les lignes 1 (date/heure), 6 (RSSI/WiFi/NTP), 9 (Uptime/Heap), 10 (ThingSpeak), 11 (Last/Next TS) toutes les **1 seconde** sans redessiner toute la page
- **Anti-flicker** : overwrite direct sans `fillRect` — Font2 (bitmap) redessine fond + texte en un seul passage, élimine le flash noir
- **RSSI dynamique** : ligne RSSI/WiFi/NTP ajoutée au refresh volatile (RSSI peut varier sans navigation)
- **Colonnes fixes** dans la page debug : `x=160` pour les lignes à 2 valeurs, `x=140/232` pour RSSI/WiFi/NTP, `x=120/200/262` pour ThingSpeak — même principe que la ligne RSSI appliqué à toutes les lignes multi-valeurs
- **Typographie française** : espace avant et après `:` dans tous les labels (`BME280 : OK`, `Air : 25.3C`, etc.) sauf formats d'heure

### Modifié
- **4 vues** (était 6) : suppression des vues Air seul et Eau seul (redondantes avec la vue Main)
- **Vue Main** : températures en grand (`FreeSansBold18pt7b × textSize(2)` = effectif 36pt, ~66px) — icônes décalées à droite
- **Header uniforme** `Y_HDR=42` sur toutes les vues — `FreeSansBold9pt7b` pour tous les titres
- **Vues renommées** : "POOL LOCAL" → "POOL DATA", "SYSTEME DEBUG" → "INFOS SYSTEME"
- **Vue HISTORIQUE** : cadre navy + titre "HISTORIQUE" ajouté, légende déplacée à l'intérieur du graphe
- **Ordre des vues** : Main → Hum/Press → Historique → Infos Système
- **Hum/Press** : `FreeSansBold24pt7b`, humidité en `TFT_YELLOW`
- **Symbole °** : `drawCircle` (1px) → donut `fillCircle(r=6) + fillCircle(r=4, noir)` = anneau 2px, proportionnel à la fonte
- **Icônes alignées** : cercles thermomètre et goutte maintenant centrés sur le même axe x=33
- **Barres WiFi** : `y_baseline` 23 → 30, alignées verticalement avec le texte "POOL DATA" (y=12..30)
- **Layout debug réorganisé** (12 lignes) : Date/Heure en 1re ligne, Reset en dernière ligne, noms complets `BME280`, `DS18B20`
- **Double refresh éliminé** : suppression du `drawViewDebug()` post-ThingSpeak, remplacé par `g_debugLastRefresh=0` (force volatile refresh ≤1s)
- **"s" de Next TS corrigé** : `%-5lus` → buffer intermédiaire + `%-7s` pour éviter le décalage du suffixe
- **`FW_VERSION`** : `v1.0` → `v1.1`

### Corrigé
- **Watchdog "already initialized"** : `esp_task_wdt_deinit()` ajouté avant `esp_task_wdt_init()` (ESP32 Core v3.x auto-initialise le TWDT avant `setup()`)
- **BME280 en bleu** : `setTextColor(TFT_WHITE, TFT_BLACK)` ajouté avant le premier `print()` du bloc debug (fond `TFT_NAVY` du header débordait)
- **Doublons Serial** : `splashLog()` appelle déjà `Serial.println()` — suppression des 5 blocs `Serial.println(F("XXX OK"))` redondants

---

## [v1.0] — 2026-06-09 — Touch + 6 vues + rétroéclairage PWM

### Ajouté
- **Rétroéclairage PWM** : GPIO 19 → LED du TFT via LEDC canal 1 (5 kHz, 8 bits). `screenOn()` / `screenOff()` contrôlent la luminosité. Allumé pleine puissance au boot (splash visible)
- **Détection touch** : GPIO 13 ← T_IRQ du TFT (actif LOW, INPUT_PULLUP). Front descendant détecté en `loop()`, anti-rebond 250 ms
- **Auto-extinction** : écran OFF après 5 min sans touch (`SCREEN_TIMEOUT = 300000`). Un touch réveille l'écran sans changer la vue
- **6 vues cycliques** (touch pour avancer) :
  - Vue 0 — **Main** : layout existant 2 colonnes + graphe 77 px
  - Vue 1 — **Air** : température air `FreeSansBold24pt` centrée plein écran
  - Vue 2 — **Eau** : température eau `FreeSansBold24pt` centrée plein écran
  - Vue 3 — **Graphe** : graphe plein écran `drawGraph(16, 222)` → **206 px** de tracé (vs 77 px en vue normale)
  - Vue 4 — **Hum/Press** : humidité (24pt vert) + pression (18pt magenta) sur fond noir
  - Vue 5 — **Debug** : IP, AP/BSSID, RSSI, BME/DS18/NTP/WiFi, uptime, heap libre, lectures, stats ThingSpeak (OK/FAIL/entry), heure NTP, compte à rebours prochain envoi TS
- **`drawTempFull()`** : variante de `drawTempVal()` pour affichage centré sur la pleine largeur (320 px)
- **`drawCurrentView()`** : dispatcher appelé au touch et après chaque lecture capteurs (si écran allumé)
- **Statistiques** : `g_readCount`, `g_tsSentOK`, `g_tsFailCount`, `g_lastTsEntry` — incrémentés dans `loop()` et `sendThingSpeak()`

### Modifié
- **`drawGraph()`** : accepte paramètres `gY0`, `gY1` (défaut = vue normale 151..228). Vue graphe plein écran = `drawGraph(16, 222)`. Fond, axes, grille, courbes et repères horaires adaptés automatiquement
- **`sendThingSpeak()`** : met à jour `g_tsSentOK` / `g_tsFailCount` / `g_lastTsEntry` à chaque appel
- **`setup()`** : init LEDC PWM + `pinMode(TOUCH_IRQ)` ajoutés avant `tft.init()`. `drawLayout()` + `updateHeader()` + `drawGraph()` remplacés par `drawCurrentView()`
- **`loop()`** : touch detection + screen timeout + redraw conditionnel (uniquement si `g_screenOn`). Vue Debug rafraîchie après chaque envoi TS
- `drawLayout()` / `updateValues()` fusionnées dans `drawViewMain()` (plus appelées indépendamment)
- Version `v0.9` → `v1.0`

### Câblage ajouté (depuis v0.9)
- `GPIO 19` → broche `LED` du module TFT ILI9341 (rétroéclairage PWM)
- `GPIO 13` ← broche `T_IRQ` du module TFT ILI9341 (touch interrupt)

---

## [v0.9] — 2026-06-09 — Températures en grand pleine largeur

### Modifié
- **Layout vertical** : Air et Eau empilées sur deux lignes pleine largeur (320 px) au lieu de deux colonnes
- Chaque section 60 px : label centré (9pt) + valeur centrée (18pt) — plus lisible à distance
- Séparateur léger (0x2965) entre les deux sections températures
- `Y_AIR_BASELINE=93`, `Y_EAU_BASELINE=154`, `Y_TEMP_END=163`
- `drawTempVal()` : paramètre `colCx` → `yBaseline`, centrage sur `X_MID` (pleine largeur)
- `GRAPH_Y0` 132→175, `GRAPH_Y1` 226→228, `GRAPH_H` 94→53 px (graphe plus compact)
- Suppression des defines `COL_L`, `COL_R`, `Y_TEMP_LBL`, `Y_TEMP_BASELINE` (obsolètes)
- Version `v0.8` → `v0.9`

---

## [v0.8] — 2026-06-09 — Layout simplifié + graphe agrandi

### Supprimé
- **Humidité et Pression** retirées de l'affichage TFT (toujours envoyées à ThingSpeak via fields 3 et 4)
- Fonction `drawSensVal()` supprimée
- Defines `Y_SENS_LBL`, `Y_SENS_BASELINE`, `Y_SENS_END` supprimés

### Modifié
- **Zone températures** agrandie : labels y=52 (était 47), baseline y=108 (était 90)
- **Séparateur** temps/graphe : `Y_TEMP_END` = 120 (était Y_SENS_END=165)
- **Graphe** : `GRAPH_Y0` 175→132, `GRAPH_Y1` 222→226, `GRAPH_H` 47→**94 px** (×2)
- Version `v0.7` → `v0.8`

---

## [v0.7] — 2026-06-09 — Stabilité : watchdog + WiFi monitoring + BME validation

### Ajouté
- **Watchdog 30 s** (`esp_task_wdt`, API struct core v3.x) : reboot automatique si `loop()` se bloque (HTTP freeze, I²C bloqué...). `esp_task_wdt_reset()` appelé en tête de chaque itération
- **Monitoring WiFi en temps réel** : `WiFi.status()` vérifié à chaque itération de `loop()` ; `g_wifiOK` mis à jour dynamiquement + header TFT rafraîchi si changement (perte / reconnexion)

### Modifié
- **NTP** : `configTime()` + `setenv()` + `tzset()` → `configTzTime()` (un seul appel, plus robuste) ; timeout 10 s → 20 s ; `delay(500)` ajouté avant la sync pour laisser la pile réseau se stabiliser
- **`g_bmeOK`** : réévalué à chaque lecture (plage sanité −40/+85°C) ; si échec → `g_tempAir`, `g_hum`, `g_press` remis à `NAN` (affichage `--.-` / `-- %` / `---- hPa`)
- **Partition scheme** : `sketch.yaml` mis à jour vers `min_spiffs` (1.9 MB app + OTA, vs 1.2 MB default) — à appliquer via *Tools → Partition Scheme* dans l'IDE
- Version `v0.6` → `v0.7`

---

## [v0.6] — 2026-06-09 — Repères horaires sur le graphe

### Ajouté
- **Repères horaires** sur l'axe X du graphe : tick gris + label "14h", "13h"... centré dessous, toutes les 24 px (= 1 heure = 12 mesures × 2 px)
- Repères ancrés sur l'heure NTP courante ; seules les heures couvrant les données réelles sont affichées (`x >= xDataStart`)

### Modifié
- **Légende "Air" / "Eau"** déplacée en haut de la zone graphe (y=167, sous le séparateur), libérant le bas pour les labels d'heures
- `GRAPH_Y0` : 170 → 175 (sous la légende haut)
- `GRAPH_Y1` : 230 → 222 (au-dessus des ticks/labels horaires)
- `GRAPH_H` : 60 → 47 px
- Version `v0.5` → `v0.6`

---

## [v0.5] — 2026-06-09 — Graphe historique + ThingSpeak

### Ajouté
- **Buffer circulaire** : `g_airHistory[138]` + `g_eauHistory[138]` (138 × 5 min = 11h30)
- **`drawGraph()`** : tracé Air (jaune) + Eau (cyan), auto-scale Y arrondi à 5°C, ancré à droite (nouveau à droite, défile vers la gauche), grille, axes, légende
- **`addHistoryPoint()`** : écriture circulaire dans le buffer
- **`sendThingSpeak()`** : HTTP GET vers api.thingspeak.com, fields 1-5 (Air, Eau, Hum, Press, RSSI), timeout 8 s, log Serial
- **ThingSpeak** : envoi toutes les 5 min (`TS_INTERVAL`), timer séparé de la lecture capteurs

### Modifié
- `secrets.h` : `SECRET_API_KEY` + `SECRET_CHANNEL_ID` activés
- `drawLayout()` : zone graphe vide (remplie par `drawGraph()`)
- `loop()` : 3 timers indépendants — lecture (30 s), ThingSpeak (5 min), NTP (1 h)
- Version `v0.4` → `v0.5`

---

## [v0.4] — 2026-06-09 — FreeSansBold + layout + zone graphe

### Ajouté
- **FreeSansBold fonts** : 18pt pour températures, 12pt pour hum/press, 9pt pour labels (via TFT_eSPI GFX fonts)
- **Valeur + unité sur la même ligne** : `drawTempVal()` centre `26.1°C` dans la colonne (° dessiné via `drawCircle`, C en 12pt)
- **`drawSensVal()`** : idem pour `55.6 %` et `1012.2 hPa` (12pt, centré)
- **Zone graphes réservée** (y=165 à y=240, 75 px) : fond sombre + grille légère + label placeholder
- **Centrage automatique** : `textWidth()` calcule la largeur réelle de chaque valeur → toujours centré dans sa colonne

### Modifié
- `drawLayout()` : labels en FreeSans9pt (TC_DATUM) — plus propres que textSize 1
- Header row 2 : supprimé "v0.3 |" — garde l'IP seule (version dans le footer des versions précédentes, ici supprimé)
- `Y_TEMP_END = 112`, `Y_SENS_END = 165` — zone capteurs compactée pour libérer la zone graphe
- Version `v0.3` → `v0.4`

---

## [v0.3] — 2026-06-08 — WiFi + NTP + heure + bargraphe signal

### Ajouté
- **WiFi** : connexion au démarrage (`WiFi.persistent(false)` + `setAutoReconnect(true)`), timeout 30 s
- **NTP** : sync `pool.ntp.org / time.google.com / time.cloudflare.com`, timezone `CET-1CEST,M3.5.0,M10.5.0/3`
- **Heure `HH:MM`** : affichée dans le header (rangée 1 droite, textSize 2), mise à jour toutes les 30 s
- **Bargraphe WiFi** : 4 barres dans le header (rangée 1 extrême droite), vert = actif, gris = éteint, tout-gris = hors ligne
- **IP address** : affichée dans le header rangée 2, dessinée une fois après la connexion WiFi
- **Splash boot redessiné** : messages `splashLog()` défilants (> BME OK, > DS18 OK, > WiFi, > NTP) avec couleurs vert/orange/rouge
- **Petites barres WiFi dans le splash** : dessinées sur la même ligne que "OK IP..." à la fin de la connexion
- **Resync NTP automatique** toutes les heures (`NTP_SYNC_INTERVAL = 3600000`)
- **`secrets.h`** : credentials WiFi (exclu du dépôt via `.gitignore`)
- **`.gitignore`** : exclut `secrets.h`, `*.elf`, `*.bin`, `build/`

### Modifié
- Header agrandi de 28 px → **48 px** (2 rangées : titre/heure/barres + version/IP/status)
- `Y_HDR = 48`, `Y_TOP_END = 165`, `Y_BOT_END = 225` (layout légèrement recentré)
- `drawWifiBars(x, y_baseline, rssi, small)` : fonction unique pour header et splash
- Version `v0.2` → `v0.3`

---

## [v0.2] — 2026-06-08 — Affichage TFT propre

### Ajouté
- **Layout TFT fixe** : `drawLayout()` dessine le cadre une seule fois au boot (header, séparateurs, labels couleur)
- **Mise à jour partielle** : `updateValues()` ne redessine que les valeurs — pas de scintillement
- **Symbole °C** : cercle dessiné via `drawCircle()` + lettre C (fonctionne sans font spéciale)
- **Status capteurs** dans le header : `BME` et `DS18` en vert/rouge selon état
- **Splash d'initialisation** : messages de boot sur TFT avec couleurs (vert=OK, rouge=erreur)
- **Variables globales** `g_tempAir`, `g_hum`, `g_press`, `g_tempEau` — partagées entre loop et display
- **Footer** : version + date de compilation + board

### Modifié
- Version `v0.1-test` → `v0.2`
- Scanner I2C supprimé (servait au debug, non nécessaire en prod)
- Séquence couleurs test supprimée
- Intervalle lecture : 30 s (stabilisation mesure BME280 mode forcé)

---

## [v0.1-test] — 2026-06-08 — Validation capteurs

### Ajouté
- Validation complète : BME280 4-pin + DS18B20 sur D1 Mini ESP32
- Scanner I2C pour diagnostic (détection adresse 0x76 / 0x77)
- Bibliothèque basculée `Adafruit_BMP280` → `Adafruit_BME280` (module 4 pins = vrai BME280)
- **BME280 mode forcé** (`setSampling MODE_FORCED`) pour réduire l'auto-échauffement
- **Offset calibration** `BME_TEMP_OFFSET = -0.4f` (écart mesuré vs DS18B20 dans l'air)
- TFT ILI9341 initialisé + test couleurs Rouge/Vert/Bleu
- Affichage basique des 4 valeurs sur TFT (textSize 3, fond noir)

### Diagnostics résolus
- `BME280 non détecté` → SDO non connecté à GND (module 5 pins) puis remplacé par module 4 pins
- `BMP280 vs BME280` → chip ID différent, bibliothèque incompatible → switch `Adafruit_BMP280`
- `Écran noir` → rails breadboard avec zones mortes + fil DC (IO26) manquant
- Offset BME280 : réduit de ~1°C (mode normal) à 0.4°C (mode forcé)
