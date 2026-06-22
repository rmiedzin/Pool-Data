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

- [ ] **TelnetStream** — Serial Monitor déporté par WiFi (bibliothèque TelnetStream)
  > Évite d'aller brancher USB pour voir les logs. Discuté, différé.

---

## Hardware

- [ ] **Résistance DS18B20 : 4.7kΩ → 1kΩ** — câble 4m = marginal avec 4.7kΩ (AN148 Maxim)
  > À faire après la période de baseline. Éliminera les 1–2% d'erreurs mesurées.

---

## Notes

- Toujours bumper `FW_VERSION` et mettre à jour `CHANGELOG.md` à chaque release
- Ne jamais committer `secrets.h` (repo GitHub public)
- Partition scheme : `min_spiffs` (1.9 MB app) — défini dans `sketch.yaml`
