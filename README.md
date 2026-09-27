# 🔬 Détecteur de Stress Filament

![ESP32](https://img.shields.io/badge/ESP32-Wemos_D1_Mini_32-blue)
![Sensors](https://img.shields.io/badge/Sensors-SS49E_Hall-green)
![Interface](https://img.shields.io/badge/Interface-Web_Modern-orange)
![Klipper](https://img.shields.io/badge/Klipper-Happy_Hare_MMU-red)
![License](https://img.shields.io/badge/License-MIT-yellow)

## 📖 Description

Capteur de position du buffer filament, entre le MMU et l'extrudeur, pour imprimantes 3D. Deux capteurs Hall SS49E sont lus en **différentiel** par un ESP32. Le module en tire une mesure **proportionnelle** : de la tension (le filament tire) à la compression (le filament pousse), en passant par le neutre.

Le module fournit cette mesure à Happy Hare de deux façons, **en même temps** :

| Sortie | Broche | Mode Happy Hare | Usage |
|---|---|---|---|
| Analogique (DAC) | GPIO 25 | **type P** (proportionnel) | mode recommandé, par défaut |
| Tout ou rien | GPIO 26 | type D (tension/compression) | secours |

Pour passer d'un mode à l'autre, il suffit de changer la config Klipper : pas besoin de reflasher l'ESP32. En type P, Happy Hare règle en continu la vitesse du moteur du MMU (autotune par filtre de Kalman étendu), au lieu de la faire osciller entre deux niveaux.

**🔬 Basé sur :** ce projet reprend et améliore le [Voron ERCF Filament Stress Sensor](https://www.printables.com/model/803180-voron-ercf-filament-stress-sensor) de **jmillerfo**. Il est adapté à l'ESP32, avec une interface web et une intégration Happy Hare.

**🖨️ Testé sur :** Voron 2.4 R2, BIGTREETECH MMB CAN, Happy Hare v3, Kalico.

## 📸 Galerie

### Interface Web
![Interface Web](docs/interface_web.png)
*Interface web avec monitoring temps réel et contrôles de calibration*

### Capteur ESP32
![Capteur ESP32](docs/ERFC_capteur_ESP32.PNG)
*Module ESP32 avec capteurs Hall SS49E intégrés*

### Imprimante Voron 2.4 R2
![Voron 2.4 R2](docs/voron_face.PNG)
*Configuration de test sur Voron 2.4 R2 avec stack BTT complet*

## ✨ Fonctionnalités

- 📏 **Mesure proportionnelle** : sortie analogique pour Happy Hare type P
- 🎯 **Sortie tout ou rien conservée** : compression, neutre ou tension, avec hystérésis
- 🧮 **Mesure différentielle** : la différence entre les deux capteurs annule la dérive thermique et les variations d'alimentation
- 🛡️ **La mesure ne dépend jamais du réseau** : le Wi-Fi ne bloque jamais la boucle de mesure
- 📶 **Wi-Fi sans recompilation** : identifiants stockés en mémoire NVS, point d'accès de repli pour les saisir
- 📊 **Interface web temps réel** : WebSocket, calibration, réglages, RSSI Wi-Fi
- 🔄 **Mise à jour OTA**
- 🧪 **Logique testée sur PC** : tests unitaires natifs, sans matériel

## 🛠️ Matériel requis

- **ESP32 Wemos D1 Mini 32**
- **2x capteurs Hall SS49E**
- **Alimentation 5V**, fournie par la carte MMU

## 📐 Schéma de connexion

```
ESP32 D1 Mini 32
├── GPIO 32 ──── Capteur S1 (SS49E)          entrée ADC1
├── GPIO 33 ──── Capteur S2 (SS49E)          entrée ADC1
├── GPIO 25 ──── Sortie ANALOGIQUE (DAC1)    -> Happy Hare type P
├── GPIO 26 ──── Sortie tout ou rien (DAC2)  -> Happy Hare type D, en secours
├── 3.3V   ──── VCC capteurs
└── GND    ──── GND capteurs
```

GPIO 25 et 26 sont les deux seules broches DAC de l'ESP32. En mode analogique, GPIO 25 fournit une tension continue et ne sert plus de sortie logique.

### 🔗 Connexion à la carte MMU

**Exemple sur BIGTREETECH MMB CAN :**
```
ESP32 5V                    ──► MMB 5V
ESP32 GND                   ──► MMB GND
ESP32 GPIO 25 (analogique)  ──► MMB STP8 (PB12), entrée lue par l'ADC
ESP32 GPIO 26 (secours)     ──► une entrée libre, uniquement pour le type D
```

> ⚠️ **Plage DAC limitée à 160-255.** Le DAC de l'ESP32 sait fournir du courant, mais presque pas en absorber. Sur l'entrée STP8 de la MMB, il ne parvient pas à descendre sous environ 1,9 V : en dessous de la valeur 144, la courbe se tasse puis s'inverse. Le firmware n'utilise donc que la plage 160-255, avec le neutre à 208. Les bornes à déclarer dans Klipper dépendent de la carte et de son entrée : **mesurez-les sur votre machine.**

## 🖨️ Intégration Klipper / Happy Hare

### Mode proportionnel (type P), recommandé

**`mmu_hardware.cfg`**, section `[mmu_sensors]` :
```ini
sync_feedback_tension_pin:
sync_feedback_compression_pin:
sync_feedback_analog_pin: mmu:PB12
# Tensions lues par la MMB, normalisées entre 0 et 1, relevées sur la machine de test :
sync_feedback_analog_max_tension: 0.623
sync_feedback_analog_neutral_point: 0.795
sync_feedback_analog_max_compression: 0.969
```

**`mmu_parameters.cfg`**, section `[mmu]` :
```ini
sync_feedback_enabled: 1
sync_feedback_buffer_range: 12      # course utile du buffer, en mm, à mesurer
sync_feedback_buffer_maxrange: 14   # course maximale, en mm
```

Pour relever les bornes, placez le bras du buffer en tension maximale, au neutre, puis en compression maximale. Pour chaque position, notez la valeur lue par Klipper sur `mmu:PB12`. Ne reprenez pas les valeurs ci-dessus telles quelles.

### Mode tout ou rien (type D), en secours

Désactivez la sortie analogique depuis l'interface web (commande `set_analog_output`), puis :
```ini
sync_feedback_analog_pin:
sync_feedback_tension_pin: ^mmu:<entrée reliée à GPIO 25>
sync_feedback_compression_pin: ^mmu:<entrée reliée à GPIO 26>
```

## 🚀 Installation

### 1. Cloner le dépôt
```bash
git clone https://github.com/Manu512/stress-filament-detector.git
cd stress-filament-detector
```

### 2. Compiler et téléverser
```bash
pip install platformio

pio run -e wemos_d1_mini32 -t upload      # firmware, par USB
pio run -e wemos_d1_mini32 -t uploadfs    # interface web (LittleFS)

pio run -e wemos_d1_mini32_ota -t upload  # ensuite, par le réseau (OTA)
```
Avant d'utiliser l'OTA, adaptez `upload_port` dans `platformio.ini` au nom ou à l'IP de votre module.

### 3. Configurer le Wi-Fi
Il n'y a plus d'identifiants à compiler, ni de fichier `config_private.h`.

1. Au premier démarrage, sans identifiants enregistrés, le module ouvre un **point d'accès** `stress-filament-XXXXXX`.
2. Connectez-vous-y, ouvrez l'interface web à l'adresse IP du point d'accès, puis saisissez votre SSID et votre mot de passe.
3. Les identifiants sont enregistrés en NVS, et le module rejoint votre réseau sous le nom d'hôte `stress-filament`.

Après 3 échecs de connexion consécutifs, le module repasse en point d'accès. Pendant tout ce temps, **la mesure et les sorties continuent de fonctionner.**

## 🎮 Utilisation

### Interface web
Accessible à l'adresse IP du module, ou par son nom d'hôte si votre réseau le résout : par exemple `http://stress-filament.lan`. Elle affiche en temps réel les deux capteurs, l'écart entre eux, la valeur DAC, l'état, le niveau de tension et le RSSI Wi-Fi.

### Calibration
Les valeurs sont **en millivolts**, lues avec `analogReadMilliVolts()`. Tant qu'aucune calibration valide n'est enregistrée, la mesure n'a pas de référence.

| Commande WebSocket | Effet |
|---|---|
| `capture_neutral` | prend la position actuelle comme neutre |
| `capture_span` | prend la position actuelle comme pleine échelle |
| `set_neutral` | impose le neutre (`n1`, `n2`, et `span` en option) |
| `save_simple_calibration` | règle la bande morte, l'hystérésis, l'échelle et le filtrage |
| `reset_calibration` | revient aux valeurs par défaut |
| `set_analog_output` | active ou désactive la sortie analogique |
| `set_wifi` / `forget_wifi` | enregistre ou efface les identifiants Wi-Fi |

### Logique de mesure
- `delta = (S1 - neutre1) - (S2 - neutre2)`, filtré par une moyenne exponentielle.
- `delta > 0` : **compression** ; `delta < 0` : **tension**.
- La sortie analogique est proportionnelle à `delta` sur la plage DAC 160-255.
- La sortie tout ou rien applique une bande morte et une hystérésis.

## 📊 Paramètres par défaut

| Paramètre | Valeur | Description |
|---|---|---|
| Échantillonnage | 20 Hz | lecture des capteurs |
| Neutre S1 / S2 | 1751 / 1639 mV | ordre de grandeur, à calibrer |
| Pleine échelle (`span`) | 150 | écart correspondant à la pleine échelle |
| Bande morte | ±16 | demi-largeur de la zone neutre |
| Hystérésis | 8 | marge pour quitter un état |
| Filtrage (`alpha`) | 32/256 | moyenne exponentielle |
| Plage DAC | 160-255, neutre 208 | voir l'avertissement ci-dessus |

## 🔧 Développement

### Structure du projet
```
├── src/
│   ├── main.cpp            # lecture des capteurs, sorties, serveur web
│   ├── net.h / net.cpp     # réseau non bloquant (machine à états, NVS, point d'accès)
├── lib/stress_core/        # logique de mesure pure, sans Arduino
├── test/test_stress_core/  # tests unitaires (Unity)
├── data/                   # interface web (index.html, style.css, script.js)
├── platformio.ini          # environnements USB, OTA et tests natifs
└── CHANGEMENTS.md          # détail de la refonte (réseau fiable, mode proportionnel)
```

### Tests
Tout ce qui prend une décision se trouve dans `lib/stress_core` et se teste sur PC, sans matériel :
```bash
pio test -e native
```

### Dépendances
- `ESP32Async/AsyncTCP`
- `ESP32Async/ESPAsyncWebServer`
- `bblanchon/ArduinoJson`

## 🤝 Contribution

Les contributions sont les bienvenues !

1. Forkez le projet
2. Créez une branche (`git checkout -b feature/AmazingFeature`)
3. Commitez vos changements (`git commit -m 'Add AmazingFeature'`)
4. Poussez la branche (`git push origin feature/AmazingFeature`)
5. Ouvrez une Pull Request

## 📄 Licence

Ce projet est sous licence MIT. Voir le fichier [LICENSE](LICENSE).

## 🛠️ Setup de développement

**Configuration de test :**
- **Imprimante :** Voron 2.4 R2
- **Contrôleur :** Raspberry Pi 4B (4GB RAM)
- **Stockage :** HDD USB 1TB
- **Écran :** Waveshare 4.3" avec mod Peek-a-boo display (fbeauKmi)
- **Carte mère principale :** BTT Octopus Pro V1.1
- **Interface CAN :** BTT U2C CAN Bus Adapter
- **Carte MMU :** BIGTREETECH MMB CAN V1.1
- **Toolhead CAN :** BTT SB2209 (CAN Bus)
- **Hotend :** BambuLab X1C Hotend
- **Steppers A/B :** TMC5160 (Alimentation 48V)
- **Steppers autres :** TMC2209 (Alimentation 24V)
- **Firmware :** Klipper avec Happy Hare MMU
- **MMU :** Multi-Material Unit avec gestion Happy Hare
- **Capteurs :** 2x SS49E Hall sensors positionnés sur le chemin filament

**Retour d'expérience :**
- Détection ultra-précise des micro-contraintes
- Intégration transparente avec Happy Hare
- Monitoring temps réel très utile pour le tuning
- Calibration simple et efficace
- Compatible avec architecture CAN Bus complète (U2C + MMB CAN + SB2209)
- Fonctionne parfaitement avec BambuLab Hotend haute débit
- Testé avec TMC5160 48V sur axes A/B pour performances maximales
- Stabilité excellente même à haute vitesse d'impression
- Interface web accessible depuis écran Waveshare 4.3" (mod Peek-a-boo)
- Stockage 1TB parfait pour logs longue durée et timelapses

## 👨‍💻 Auteur

**Manu512**
- GitHub : [@Manu512](https://github.com/Manu512)
- Projet : Détecteur de Stress Filament
- Date : octobre 2025, refonte septembre 2026 (mode proportionnel, réseau non bloquant)

## 🔗 Liens utiles

- [Happy Hare MMU](https://github.com/moggieuk/Happy-Hare) - Multi-Material Unit pour Klipper
- [Voron ERCF Filament Stress Sensor](https://www.printables.com/model/803180-voron-ercf-filament-stress-sensor) - Projet original par jmillerfo
- [Kalico](https://github.com/KalicoCrew/kalico) - Fork de Klipper
- [Documentation Klipper](https://www.klipper3d.org/) - Firmware 3D printer
- [Raspberry Pi 4B](https://www.raspberrypi.org/products/raspberry-pi-4-model-b/) - Contrôleur principal
- [Waveshare 4.3" Display](https://www.waveshare.com/4.3inch-dsi-lcd.htm) - Écran tactile DSI
- [Peek-a-boo Display Mod](https://www.printables.com/model/747183-peek-a-boo-display) - Mod d'affichage par fbeauKmi
- [BTT Octopus Pro V1.1](https://github.com/bigtreetech/BIGTREETECH-OCTOPUS-Pro) - Carte mère principale
- [BTT U2C CAN](https://github.com/bigtreetech/U2C) - Interface CAN Bus USB
- [BTT MMB CAN](https://github.com/bigtreetech/MMB) - Carte MMU CAN Bus
- [BTT SB2209](https://github.com/bigtreetech/EBB) - Toolhead CAN Board
- [TMC5160 Datasheet](https://www.trinamic.com/products/integrated-circuits/details/tmc5160/) - Drivers 48V haute performance
- [BambuLab Hotend](https://bambulab.com/) - Système hotend haute performance
- [Documentation ESP32](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [PlatformIO](https://platformio.org/)
- [Capteurs Hall SS49E](https://www.allegromicro.com/en/products/sense/linear-and-angular-position/linear-hall-sensors)
- [Voron Design](https://vorondesign.com/) - Imprimantes 3D open source

---

⭐ **N'hésitez pas à mettre une étoile si ce projet vous aide !**
