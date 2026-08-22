# Firmware GoPro Remote — nRF52840 Dongle (prototype BLE)

## Ce que fait ce firmware aujourd'hui
- Scanne en BLE et se connecte automatiquement à la première GoPro détectée
  (filtre sur le service `0xFEA6`).
- Effectue le **bonding** (pairing sécurisé), obligatoire pour l'API Open GoPro.
  Les clés sont sauvegardées en flash (`CONFIG_SETTINGS`) → pas besoin de
  re-pairer à chaque redémarrage.
- Découvre les caractéristiques **GP-0072** (Command) et **GP-0073**
  (Command Response), s'abonne aux notifications.
- 2 boutons câblés en GPIO (Camera ON / Camera OFF) → envoient la commande
  shutter correspondante (`03 01 01 01` / `03 01 01 00`).

## ⚠️ Important : ce code n'a pas pu être compilé/testé dans cet environnement
Je n'ai pas de toolchain nRF Connect SDK / Zephyr complet ici (SDK ~plusieurs
Go, incompatible avec le bac à sable où j'ai travaillé). Le code suit les API
Zephyr Bluetooth Host de façon rigoureuse (mêmes patterns que les exemples
officiels `central_hr` / `peripheral` de Nordic), mais **tu devras le compiler
et corriger d'éventuelles erreurs de syntaxe mineures** de ton côté — c'est un
point de départ solide, pas un binaire validé.

## Ce qui manque encore (volontairement, voir conversation précédente)
1. **ANT+ (3 boutons Garmin)** : nécessite la pile ANT propriétaire de Nordic
   (SoftDevice S212/S332 ou module ANT du nRF5 SDK), sous licence séparée à
   demander auprès de Nordic/ANT+ Alliance. Pas incluse ici — stub présent
   (`handle_garmin_button()`), à compléter une fois la pile ANT récupérée.
2. **Gestion fine de la conso (System OFF)** : le dongle est alimenté par USB
   pendant les tests, donc pas critique maintenant. À reprendre pour la
   version finale CR2032 (cf. l'architecture "réveil sur PORT event" vue
   précédemment).
3. **Thermistance / mesure batterie (ADC)** : pas câblées sur le dongle nu.
4. **3 boutons Garmin restants** (page droite/gauche/tour) : pas encore dans
   l'overlay ni dans `main.c`, à ajouter sur le même modèle que
   `btn_cam_on`/`btn_cam_off` une fois l'ANT+ en place.

## ⚠️ Pinout des boutons à vérifier avant de souder
Dans `boards/nrf52840dongle_nrf52840.overlay`, j'ai utilisé **P0.13 et
P0.15** à titre d'exemple. **Vérifie-les contre le "Pin assignment" du
guide utilisateur Nordic du nRF52840 Dongle (PCA10059)** avant de câbler quoi
que ce soit : certaines pastilles castellated du bord de la carte sont
partagées avec les lignes USB D+/D- et ne doivent pas être utilisées en GPIO.

## Comment builder

1. Installer le **nRF Connect SDK** (via l'extension VS Code "nRF Connect
   for VS Code", la méthode la plus simple) ou `west` en ligne de commande.
2. Copier ce dossier `gopro_remote_fw/` dans ton espace de travail nRF
   Connect SDK (à côté de tes autres applications `west`).
3. Build :
   ```
   west build -b nrf52840dongle_nrf52840 gopro_remote_fw
   ```
4. Le dongle nu boote en **mode bootloader DFU USB** (pas de SWD direct sauf
   si tu soudes les pastilles de test dédiées). Flash via `nrfutil` :
   ```
   nrfutil pkg generate --hw-version 52 --sd-req 0x00 \
     --application build/zephyr/zephyr.hex \
     --application-version 1 pkg.zip
   nrfutil dfu usb-serial -pkg pkg.zip -p /dev/ttyACM0
   ```
   (Appuie sur le petit bouton du dongle en le branchant pour forcer le mode
   DFU si besoin — la LED clignote en rouge quand il est en attente.)
5. Les logs sortent sur le port série USB CDC-ACM (`CONFIG_LOG` +
   `CONFIG_USB_CDC_ACM`) — ouvre-le avec `screen`, `minicom` ou le moniteur
   série de VS Code pour voir le déroulement (scan, connexion, bonding,
   découverte GATT, envoi des commandes).

## Test attendu
1. Mets ta GoPro Hero 11 Mini en mode pairing BLE (Réglages → Connexions →
   Appairer un appareil, ou équivalent selon le menu).
2. Flash et lance le firmware — il devrait scanner, se connecter, bonder,
   puis logger "GoPro prête".
3. Appuie sur le bouton Camera ON câblé → la GoPro doit démarrer
   l'enregistrement. Camera OFF → elle doit l'arrêter.
