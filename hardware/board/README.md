# Télécommande GoPro / Garmin Edge — Projet KiCad

## État de ce projet
- **Schéma électrique** (`gopro_garmin_remote.kicad_sch`) : complet et **validé**.
  Généré programmatiquement puis vérifié avec `kicad-cli` (chargement OK, export
  netlist OK). Tous les nets attendus sont correctement formés : `VBAT`, `GND`,
  `SWDIO`, `SWCLK`, `GPIO_CAM_ON`, `GPIO_CAM_OFF`, `GPIO_PAGE_R`, `GPIO_PAGE_L`,
  `GPIO_LAP`, `NTC_SENSE`. Seuls `ANT` et `RESET` du module restent volontairement
  non connectés à ce stade.
- **Footprint E73** (`RemoteLocal.pretty/E73-2G4M08S1E_hand_solder.kicad_mod`) :
  le gabarit à pads étendus (soudure manuelle) vu précédemment, référencé par U1.
- **PCB** : pas encore généré (voir "Prochaines étapes").

## Composants du schéma

| Réf. | Description | Nets connectés |
|------|-------------|-----------------|
| U1 | Module E73-2G4M08S1E (symbole custom) | VBAT, GND, SWDIO, SWCLK, GPIO_CAM_ON/OFF, GPIO_PAGE_R/L, GPIO_LAP, NTC_SENSE |
| BT1 | Pile CR2032 | VBAT, GND |
| C1 | 100µF tantale (découplage) | VBAT, GND |
| C2 | 100nF céramique (filtrage HF) | VBAT, GND |
| SW1–SW5 | Boutons (CAM_ON, CAM_OFF, PAGE_R, PAGE_L, LAP) | GND + GPIO dédié |
| NTC1 | Thermistance 10k | VBAT, NTC_SENSE |
| R1 | Résistance 10k (pont diviseur thermistance) | NTC_SENSE, GND |
| J1 | Header SWD 4 pins | VBAT, SWDIO, SWCLK, GND |

## ⚠️ Points à vérifier avant fabrication

1. **Pinout réel du E73** : le symbole `U1` utilise des **numéros de broches
   placeholder** (1–7, 10–15) qui ne correspondent PAS forcément aux vraies
   broches physiques du module (43 pads castellated). Avant de router un PCB,
   recale ces numéros sur le vrai tableau de pinout du datasheet Ebyte, ou sur
   la librairie [kicad-lib de spbnick](https://github.com/spbnick/kicad-lib)
   déjà vérifiée par fabrication réelle.
2. **Empreinte E73** : le footprint fourni est un **gabarit pédagogique**
   (technique de pad étendu démontrée sur 8 broches). Il faut le compléter
   pour les 43 broches réelles avant de router le PCB.
3. **Disposition du schéma** : les labels sont un peu serrés visuellement
   (généré par script, pas par un humain dans l'éditeur). L'électrique est
   correcte, mais tu voudras sans doute réarranger les labels dans KiCad pour
   plus de lisibilité (Outils > rien à recalculer, juste du confort visuel).

## Prochaines étapes suggérées

1. Ouvrir `gopro_garmin_remote.kicad_pro` dans KiCad 7+, vérifier le schéma à
   l'écran, réarranger si besoin.
2. Compléter le pinout réel du E73 (symbole + footprint).
3. Passer en mode PCB (`Outils > Mettre à jour PCB depuis schéma`), placer les
   composants, router les pistes.
4. Ajouter le contour du boîtier / mounting holes selon fixation guidon retenue.

## Outils utilisés pour générer ce projet
- `kiutils` (Python) pour écrire un `.kicad_sch` syntaxiquement valide.
- `kicad-cli` 7.0.11 pour valider le chargement et exporter le netlist/PDF.
- Symboles réutilisés tels quels depuis les bibliothèques standard KiCad :
  `Device:R`, `Device:C`, `Device:Battery_Cell`, `Device:Thermistor_NTC`,
  `Switch:SW_Push`, `Connector_Generic:Conn_01x04`.
