# ROADMAP — Pool Data

Idées d'évolution, par ordre de priorité approximatif.
Cocher quand implémenté.

---

## En cours / fait

- [x] ArduinoOTA — flash firmware par WiFi (v1.2)
- [x] Graphe 48h — buffer circulaire 576 pts, step dynamique (v1.3)
- [x] Vue STATISTIQUES — T° min/max eau+air + horodatage + NVS (v1.3)
- [x] Appui long vue stats → reset NVS (v1.3)
- [x] Flèche tendance sur vue Main (v1.4)
- [x] Écran de veille (clock + T° eau, luminosité réduite) (v1.4)
- [x] Double tap → retour vue 0 (v1.4)

---

## Affichage / UX

- [ ] **Extinction réelle de la dalle en veille (priorité haute — préservation matériel)**
  > **Constat 08/2026 :** blobs sombres au centre de la dalle, fixes, en expansion lente.
  > Dégradation de la couche cristaux liquides / adhésif du polariseur — favorisée par
  > l'ambiance chaude et humide du pool house, aggravée par une matrice pilotée 24 h/24.
  >
  > **État actuel du code :** `screenOff()` (ligne ~160) n'est **jamais appelé** — code mort.
  > Le commentaire `SCREEN_TIMEOUT` ligne 62 (« 5 min sans touch → écran OFF ») ne décrit pas
  > le comportement réel : après 5 min, `enterScreensaver()` se contente de `ledcWrite(LED_PIN, 18)`
  > (~7 % luminosité) et redessine T° eau + heure toutes les 30 s. Aucune commande de veille
  > n'est envoyée à l'ILI9341 → la matrice LC reste alimentée et pilotée en permanence.
  >
  > **À faire — conserver le timer 5 min et le réveil au toucher :**
  > - Après `SCREEN_TIMEOUT` (5 min, inchangé) → extinction complète au lieu de l'économiseur :
  >   `ledcWrite(LED_PIN, 0)` **puis** `tft.writecommand(0x28)` (DISPOFF) et `tft.writecommand(0x10)` (SLPIN).
  >   C'est la mise en sommeil de la dalle qui protège, pas seulement l'extinction des LED.
  > - Réveil sur front descendant `TOUCH_IRQ` (GPIO 13) — déjà géré dans la loop, brancher sur
  >   la nouvelle fonction : `tft.writecommand(0x11)` (SLPOUT), `delay(120)` **obligatoire**
  >   (temps de stabilisation du régulateur interne ILI9341), `tft.writecommand(0x29)` (DISPON),
  >   `ledcWrite(LED_PIN, 255)`, puis `drawCurrentView()`.
  > - Le contrôleur tactile (XPT2046) est un composant distinct du driver écran : il reste
  >   opérationnel dalle endormie, le réveil au toucher fonctionne sans modification matérielle.
  > - Décider du sort de l'économiseur : soit supprimé (extinction directe à 5 min), soit conservé
  >   en palier intermédiaire avec extinction totale à 30 min. Préférence : suppression pure —
  >   c'est lui qui maintient la dalle sous tension.
  > - Réécrire `screenOff()` / `screenOn()` en conséquence et supprimer le code mort.
  > - Corriger le commentaire ligne 62 pour qu'il décrive le comportement réel.
  >
  > **Attendu :** ralentit la progression des blobs, ne les résorbe pas (dégâts irréversibles).
  > Bénéfice secondaire : consommation et échauffement réduits dans le boîtier.
  >
  > **Piste matériel long terme si la dalle continue de se dégrader :** remplacement par un écran
  > sans rétroéclairage — RLCD réflectif (ST7305) ou e-paper. Pas de dissipation dans le boîtier,
  > bien plus tolérant à l'ambiance humide. Contrepartie : perte de la couleur et du tactile réactif
  > (à arbitrer avec les 6 vues actuelles).

- [ ] **Alerte seuil T° eau** — si T° < X°C ou > Y°C, clignoter rétroéclairage + bandeau rouge
  > Seuils configurables (par défaut : < 18°C = trop froide, > 32°C = trop chaude)

---

## Alertes / notifications

- [ ] **Notification push smartphone** — HTTP POST vers Pushover ou ntfy.sh si T° hors seuil ou DS18B20 erreurs répétées
  > Fonctionne sur le même WiFi, pas besoin de serveur externe si ntfy.sh self-hosted

- [ ] **Alerte gel hiver** — si T° eau < 4°C → alerte critique (protection canalisation)

---

## Données / capteurs

- [ ] **Météo locale** — appel API openweathermap.org (gratuit) : T° extérieure + prévisions pluie
  > Vue dédiée ou intégrée à la vue Main. Utile pour décider si baignade OK.

- [ ] **Indice UV** — via même API météo (UV index)

- [ ] **Plusieurs DS18B20** — mesure à différentes profondeurs ou entrée/sortie filtration
  > Nécessite résolution des adresses OneWire

---

## Qualité de l'eau

- [ ] **Capteur pH** (analogique + sonde pH) — affichage + alerte si hors plage 7.0–7.4
- [ ] **Capteur chlore / ORP** (électrode redox) — indication traitement nécessaire
  > Matériel supplémentaire, investissement ~30–80€

---

## Réseau / fiabilité

- [ ] **Blocage total sans reprise automatique — incident « écran blanc » (priorité haute)**

  ### Constat — 08/2026

  Après ~2 mois de fonctionnement continu sans incident, système retrouvé **totalement figé,
  écran entièrement blanc**. Trou de **5–6 h dans les envois ThingSpeak** confirmé sur le canal A :
  ce n'est donc pas une simple panne d'affichage, tout le firmware était arrêté.
  Reprise par **simple appui sur RESET**, sans rien débrancher.

  Éléments écartés au fil du diagnostic :
  - *Panne d'affichage seule* — écartée, les envois TS se sont arrêtés en même temps.
  - *Corrosion / mauvais contact* — écartée, inspection visuelle des connecteurs : rien (local
    de piscine au sel, hypothèse plausible a priori mais non vérifiée sur le terrain).
  - *Lien avec la coupure EDF du 11/08/2026 (03:22→04:22)* — écarté, elle est survenue
    plusieurs jours **avant** l'incident, système ayant tourné normalement entre-temps.

  ### Scénario retenu (cohérent, non prouvé)

  1. Blocage ou plantage dans la `loop()` — appel I²C sans retour, pile WiFi, ou autre.
  2. Le TWDT 30 s fait son travail : `trigger_panic = true` → panique → **reboot automatique**.
     *Le watchdog a probablement fonctionné : c'est le redémarrage qui a échoué.*
  3. Un reset logiciel ne coupe pas l'alimentation des périphériques. Si le bus I²C était resté
     verrouillé (esclave maintenant SDA au niveau bas en pleine transaction — défaut classique),
     il l'est toujours au redémarrage.
  4. `bme.begin(0x76)` échoue, `bme.begin(0x77)` échoue.
  5. **`while (1) delay(1000)`** ligne ~1221 → blocage définitif. Le watchdog n'est pas encore
     armé à ce stade du `setup()` : aucune reprise possible.
  6. L'appui sur RESET met EN au niveau bas assez longtemps pour que les GPIO passent en haute
     impédance et libèrent le bus → BME280 détecté → tout repart.

  L'écran blanc s'explique en parallèle : `ledcWrite(LED_PIN, 255)` ligne ~1190 allume le
  rétroéclairage **avant** `tft.init()` ligne 1198. Si l'init SPI de la dalle a échoué elle aussi,
  on obtient une dalle allumée à fond mais non initialisée — soit un écran blanc.

  > **Le `while(1)` n'est pas la cause de l'incident, c'est son amplificateur.** Il transforme un
  > aléa dont le système aurait dû se relever seul en 5–6 h de panne nécessitant une intervention
  > physique. Le déclencheur reste inconnu — et le restera tant que le point 1 ci-dessous n'est pas fait.

  ### Preuve perdue

  `g_resetReason` est lu au boot et affiché dans la vue DEBUG, mais **jamais persisté**.
  Au moment de l'appui sur RESET, la raison du reboot précédent (`TaskWDT`, `Panic`, `Brownout`…)
  a été effacée. Elle était pourtant affichée à l'écran, sous la vue DEBUG, pendant les 5–6 h
  où le système était figé. *À ne pas reproduire.*

  ### À faire, dans cet ordre

  1. **Persister la raison du reset en NVS** — `g_resetReason` + compteur de reboots + horodatage
     NTP du dernier boot, historique des N derniers événements, affichés dans la vue DEBUG.
     *Seule action qui produit de l'information. Sans elle, la prochaine occurrence sera aussi
     aveugle que celle-ci.*
  2. **Supprimer le `while (1) delay(1000)` sur échec BME280** (ligne ~1221) → dégradation
     gracieuse. Le DS18B20 (T° eau, donnée principale) et l'envoi ThingSpeak n'ont aucun besoin
     du BME280. Le reste du code sait déjà gérer le cas : la vue DEBUG affiche `g_bmeOK ? "OK" : "ERR"`.
     La dégradation gracieuse était prévue, cette ligne la contredit. Prévoir une nouvelle tentative
     de `bme.begin()` périodique dans la loop pour récupérer le capteur à chaud.
  3. **Armer le watchdog dès la première ligne du `setup()`** au lieu de la fin (~ligne 1374),
     avec un timeout large (60–90 s pour couvrir WiFi 30 s + NTP 20 s) et un `esp_task_wdt_reset()`
     dans chaque boucle d'attente. Tout le `setup()` est actuellement à nu.
  4. **Routine de déverrouillage I²C avant `bme.begin()`** — 9 impulsions sur SCL en GPIO nu pour
     libérer un esclave bloqué, puis STOP, puis `Wire.begin()`. ~15 lignes, parade standard,
     traite directement le scénario retenu.
  5. **Allumer le rétroéclairage après `tft.init()`** — un échec d'init donnerait un écran noir
     plutôt que blanc : symptôme plus lisible et moins agressif pour la dalle.

  > Les points 2 et 3 garantissent qu'un tel incident se solde par un redémarrage automatique.
  > Le point 1 fait qu'on saura pourquoi. Le point 4 attaque la cause la plus probable.

  ### En réserve, si le problème persiste après les 5 points

  - **Watchdog applicatif sur le flux de données** — si N cycles consécutifs (ex. 3 = 15 min) sans
    envoi ThingSpeak réussi **et** WiFi présent → `ESP.restart()`. Couvre les blocages silencieux
    de la pile réseau.
  - **Chien de garde écran** — relire périodiquement l'ID du driver via `readcommand8(0xD3, …)`,
    comparer à `0x9341`, ré-initialiser à chaud si divergence. Utile seulement si un jour l'écran
    meurt **sans** que les envois TS s'arrêtent — ce n'est pas le cas ici.
  - **Détection de brownout** — `ESP_RST_BROWNOUT` déjà géré dans `resetReasonText()`, à remonter
    dans le journal NVS. Si le motif se confirme : condensateur de découplage, ou alim plus costaude.
  - **Watchdog matériel externe** (TPL5010 ou équivalent) — seule protection contre un gel total
    du SoC. Dernier recours.
  - **Reboot préventif hebdomadaire** — masque la cause au lieu de la corriger. À éviter.

- [ ] **Cache local coupure internet — bulk update ThingSpeak**
  > Si l'envoi ThingSpeak échoue (box down, coupure internet), continuer à mesurer
  > toutes les 5 min et stocker en RAM (buffer circulaire — 24h = 288 entrées, négligeable).
  > Au retour du WiFi : ré-envoi groupé via `POST /channels/467925/bulk_update.json`
  > (jusqu'à 960 mesures, chacune avec son timestamp — `delta_t` relatif utilisable
  > si pas de NTP pendant la coupure). Les données s'insèrent à leur vraie place
  > dans l'historique → PoolWatch (courbes, archive, records) les voit sans modif.
  > Limite : coupure EDF = ESP32 éteint, rien à cacher (prévoir batterie/UPS sinon accepter le trou).
  > Constaté : coupure élec. 11/08/2026 03:22→04:22 = trou 60 min dans TS.

- [ ] **IP fixe** — réservation DHCP (MAC → IP) dans la Livebox Orange
  > MAC ESP32 visible dans INFOS SYSTEME (ligne AP/MAC). OTA plus stable avec IP fixe.

- [ ] **Fix OTA Arduino IDE — mdns-discovery 1.1.0 cassé sur macOS Tahoe**
  > Cause : ESP32 3.3.11 a installé mdns-discovery 1.1.0 (arm64 natif) qui ne découvre rien sur Tahoe. La 1.0.12 (x86_64/Rosetta) fonctionnait.
  > Fix : `cp ~/Library/Arduino15/packages/builtin/tools/mdns-discovery/1.0.12/mdns-discovery ~/Library/Arduino15/packages/builtin/tools/mdns-discovery/1.1.0/mdns-discovery` puis relancer l'IDE.
  > Fix 2 (après que le port apparaît) : corriger `{upload.port.properties.port}` dans `platform.txt` ligne 384 → remplacer par `3232`.

- [ ] **TelnetStream** — Serial Monitor déporté par WiFi (bibliothèque TelnetStream)
  > Évite d'aller brancher USB pour voir les logs. Discuté, différé.

---

## Hardware

- [ ] **Résistance DS18B20 : 4.7kΩ → 1kΩ** — câble 4m = marginal avec 4.7kΩ (AN148 Maxim)
  > À faire après la période de baseline. Éliminera les 1–2% d'erreurs mesurées.

---

---

## PoolWatch — app iOS & watchOS

### Design / UX (prochaine session)

- [ ] **Alerte T°Eau cible** — notification push quand T°Eau atteint un seuil configurable (ex. "26° atteints !"). Nécessite APNs ou service tiers (Pushover / ntfy.sh).
- [ ] **Alerte station hors ligne** — notification si aucune donnée Canal B pendant X min
- [ ] **Alerte batterie < 20%** — push si `batteryPct` < 20 lors du fetch
- [ ] **Évaporation en L/j** — multiplier mm/j par la surface (ex. 8×4 = 32 m²) → affiché à côté de mm/j
- [ ] **Point de rosée** — calculé depuis T°Ext + HumExt (formule Magnus), affiché dans la card Station météo
- [ ] **Vue journalière 7j/30j** — courbe avec bandes min–max par jour pour suivre la saison
- [ ] **Heure la plus chaude** — d'après l'historique, identifier le créneau horaire où T°Eau est max
- [ ] **Live Activity + Dynamic Island** — T°Eau en direct dans la Dynamic Island avec barre vers seuil cible (iOS 16.2+)
- [ ] **Widget interactif** — bouton Rafraîchir dans le widget sans ouvrir l'app (iOS 17+ `Button` in widget)
- [ ] **Widget medium — sparkline** — mini graphe 1h T°Eau directement dans le widget medium
- [ ] **Digital Crown Watch** — zoomer/dézoomer la plage du graphe en faisant tourner la couronne
- [ ] **Siri Shortcut** — "Hey Siri, la piscine ?" → annonce T°Eau + T°Ext
- [ ] **Export CSV** — bouton pour exporter la période affichée vers le presse-papiers ou Fichiers

---

## Notes

- Toujours bumper `FW_VERSION` et mettre à jour `CHANGELOG.md` à chaque release
- Ne jamais committer `secrets.h` (repo GitHub public)
- Partition scheme : `min_spiffs` (1.9 MB app) — défini dans `sketch.yaml`
