# Télécommande vélo GoPro + Garmin Edge — Specs & BOM

## 1. Spécifications fonctionnelles

### Objectif
Boîtier compact fixé sur le guidon, permettant de piloter à distance :
- la caméra **GoPro Hero 11 Black Mini** (via BLE / Open GoPro API)
- le compteur **Garmin Edge Explore** (via ANT+ / profil Generic Controls)

### Commandes (5 boutons)

| # | Bouton | Action | Protocole |
|---|--------|--------|-----------|
| 1 | Caméra ON | Démarre l'enregistrement GoPro | BLE (Open GoPro) |
| 2 | Caméra OFF | Arrête l'enregistrement GoPro | BLE (Open GoPro) |
| 3 | Page droite | Fait défiler l'écran Edge vers la droite | ANT+ (Generic Controls) |
| 4 | Page gauche | Fait défiler l'écran Edge vers la gauche | ANT+ (Generic Controls) |
| 5 | Tour (lap) | Marque un tour sur l'Edge | ANT+ (Generic Controls) |

Chaque bouton déclenche une action indépendante, immédiate (pas de mode/menu à naviguer).

### Fonction annexe (optionnelle, à confirmer)
- **Thermomètre** : mesure de température ambiante via thermistance, exploitable plus tard (log interne ou affichage si un écran est ajouté ultérieurement).

### Hors périmètre pour cette version
- **Écran** : mis de côté pour l'instant (pourra être ajouté dans une v2, voir échanges précédents sur LCD à segments HT1621).
- Pas de configuration/paramétrage à distance (pas de BLE config, pas d'app compagnon prévue).

### Contraintes d'usage
- Fixation guidon, utilisation en conditions extérieures (vibrations, météo — étanchéité du boîtier à prévoir).
- Autonomie visée : plusieurs mois sur une seule pile CR2032.
- Temps de réponse acceptable : de l'ordre de la seconde entre appui et exécution (connexion BLE/ANT+ à la volée, pas de radio "always-on").

---

## 2. Spécifications techniques

### Architecture générale
- MCU **réveillé uniquement sur appui bouton** (System OFF entre deux actions), pas de connexion radio permanente.
- Au réveil : le firmware identifie le bouton pressé (registre LATCH du contrôleur GPIO), active la pile radio correspondante (BLE ou ANT+), exécute la commande, puis retourne en veille profonde.
- **Un seul protocole actif à la fois** (multi-protocole séquentiel, pas simultané) → simplifie le firmware et limite la consommation.

### MCU / radio
- **Nordic nRF52832** (module Ebyte E73-2G4M08S1E) — Cortex-M4F, 512KB flash / 64KB RAM.
- Choisi car il supporte nativement **BLE et ANT+** sur le même radio (stacks différentes selon le protocole utilisé), et car il est le seul testé avec succès en pratique pour le pairing BLE avec une GoPro (contrairement à l'ESP32).
- Stack logicielle : **nRF Connect SDK (Zephyr)**, développement en C bas niveau.

### Boutons & réveil
- 5 boutons tactiles, GPIO individuels (pas de matrice), pull-up interne, logique active-low.
- Réveil via **événement PORT** (SENSE + registre LATCH) : un seul type d'interruption gère les 5 boutons sans consommer de canal GPIOTE dédié, conso en veille négligeable (< 1µA côté GPIO).
- Anti-rebond logiciel (relecture ~20-30ms après réveil).

### Communication GoPro (BLE)
- Basé sur l'**Open GoPro API** (documentation officielle GoPro, licence MIT).
- Pairing BLE classique (bonding) requis au préalable ; clés stockées en flash (NVS Zephyr) pour reconnexion directe ensuite.
- Commandes shutter écrites sur la caractéristique **GP-0072**, réponse lue sur **GP-0073**.
- Réabonnement aux notifications à chaque connexion (la GoPro ne conserve pas l'état de souscription).

### Communication Garmin Edge (ANT+)
- Profil ANT+ **Generic Controls** (identique à celui utilisé par l'accessoire officiel Garmin *Edge Remote*).
- Commandes envoyées en **Page 73 (Generic Command)** avec code touche correspondant (page droite / page gauche / lap).
- Edge Explore confirmé compatible ANT+ et avec l'accessoire Edge Remote (même profil).

### Alimentation
- **Pile CR2032**, alimentation directe du nRF52 (pas de régulateur — le chip fonctionne nativement entre 1.7V et 3.6V).
- Condensateur tampon (tantale/polymère, boîtier SMD compact) pour absorber les pics de courant BLE/ANT+ sans faire chuter la tension vue par le chip en fin de vie de pile.
- Condensateur céramique de filtrage HF en complément.
- **Mesure de batterie** : lecture logicielle via le canal interne VDD du SAADC du nRF52 — aucun composant supplémentaire nécessaire.

### Thermistance (optionnel)
- NTC standard (ex. 10kΩ @ 25°C) montée en pont diviseur avec une résistance fixe, lue sur une entrée ADC du nRF52.
- Activée ponctuellement seulement (pas de mesure continue) pour préserver l'autonomie.

### Carte électronique
- Basée sur le module **Ebyte E73-2G4M08S1E** (18.0 x 13.0mm, boîtier castellated 43 broches).
- Footprint avec **pads étendus** (languettes débordant vers l'extérieur du module) pour permettre une soudure manuelle au fer, sans four à refusion.
- Programmation/debug via **SWD** (broches SWDIO/SWCLK/GND/VDD en header ou pads dédiés).

### Outillage de développement
- Sonde de programmation : **J-Link EDU Mini** (usage non commercial), alternative possible avec un **ST-Link V2** piloté via OpenOCD (pas de ST-Link V3, restreint aux puces ST).
- Carte de prototypage recommandée avant PCB final : **nRF52-DK** (debugger intégré, exemples Zephyr prêts à l'emploi).

---

## 3. Nomenclature (BOM)

| Réf. | Composant | Quantité | Rôle | Remarques |
|------|-----------|----------|------|-----------|
| U1 | Module **Ebyte E73-2G4M08S1E** (nRF52832) | 1 | MCU + radio BLE/ANT+ | Boîtier castellated, footprint à pads étendus |
| BT1 | Pile bouton **CR2032** + support | 1 | Alimentation | Alimentation directe, sans régulateur |
| SW1–SW5 | Boutons tactiles (tact switch) | 5 | Caméra ON, Caméra OFF, Page droite, Page gauche, Tour | GPIO pull-up, active-low |
| C1 | Condensateur **tantale/polymère 47–100µF**, boîtier 0805/1206 | 1 | Tampon anti-chute de tension (pics radio) | Basse ESR requise |
| C2 | Condensateur céramique **100nF**, boîtier 0402 | 1 | Filtrage HF | — |
| NTC1 | Thermistance NTC **10kΩ** | 1 (optionnel) | Mesure de température | Pont diviseur avec résistance fixe |
| R1 | Résistance fixe **10kΩ** | 1 (optionnel) | Pont diviseur thermistance | Valeur à ajuster selon plage de mesure visée |
| J1 | Header/pads **SWD** (SWDIO, SWCLK, GND, VDD) | 1 | Programmation/debug | Non peuplé en série, utile prototypage |
| PCB | Carte custom (footprint E73 pads étendus) | 1 | Support | KiCad, gabarit déjà ébauché dans la conversation |
| — | Boîtier (impression 3D ou autre) | 1 | Protection/fixation guidon | À définir (étanchéité à prévoir) |

### Outillage (non monté sur la carte finale)
| Outil | Rôle |
|-------|------|
| J-Link EDU Mini (ou ST-Link V2 + OpenOCD) | Flash / debug SWD |
| nRF52-DK | Prototypage avant PCB final |

---

## 4. Points ouverts / à trancher

- **Feedback utilisateur** (LED/buzzer) : évoqué mais pas figé — à décider si on ajoute un retour visuel/sonore de confirmation d'action.
- **Boîtier** : matériau, méthode de fixation guidon (collier, support GoPro standard ?), étanchéité.
- **Gestion des 2 boutons caméra** : à confirmer si ON/OFF séparés sont plus pratiques qu'un bouton toggle unique (redondance volontaire pour éviter les erreurs en roulant).
- **Écran** : différé, mais l'architecture (SAADC batterie, thermistance) reste compatible avec un ajout futur (segment LCD HT1621 déjà étudié).
