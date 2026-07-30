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
