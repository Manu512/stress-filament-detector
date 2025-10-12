# 🔬 Détecteur de Stress Filament

![ESP32](https://img.shields.io/badge/ESP32-Wemos_D1_Mini_32-blue)
![Sensors](https://img.shields.io/badge/Sensors-SS49E_Hall-green)
![Interface](https://img.shields.io/badge/Interface-Web_Modern-orange)
![Klipper](https://img.shields.io/badge/Klipper-Happy_Hare_MMU-red)
![License](https://img.shields.io/badge/License-MIT-yellow)

## 📖 Description

Système de détection de stress filament en temps réel pour imprimantes 3D utilisant deux capteurs Hall SS49E et un microcontrôleur ESP32. Le système détecte trois états distincts : **compression**, **neutre**, et **tension** grâce à une logique différentielle avancée.

**🔬 Basé sur :** Ce projet est une revisite et amélioration du [Voron ERCF Filament Stress Sensor](https://www.printables.com/model/803180-voron-ercf-filament-stress-sensor) de **jmillerfo**, adapté pour l'ESP32 avec interface web moderne et intégration Happy Hare optimisée.

**🎯 Intégration MMU :** Conçu pour s'intégrer avec [Happy Hare](https://github.com/moggieuk/Happy-Hare) dans Klipper/Kalico pour la gestion automatique du Multi-Material Unit (MMU). Le détecteur fournit les signaux de stress au firmware Klipper qui gère ensuite la logique de rétraction/avancement du filament.

**🖨️ Testé sur :** Voron 2.4 R2 avec carte BIGTREETECH MMB CAN V1.1

## 📸 Galerie

### Interface Web
![Interface Web](docs/interface_web.png)
*Interface web moderne avec monitoring temps réel et contrôles de calibration*

### Capteur ESP32
![Capteur ESP32](docs/ERFC_capteur_ESP32.PNG)
*Module ESP32 avec capteurs Hall SS49E intégrés*

### Imprimante Voron 2.4 R2
![Voron 2.4 R2](docs/voron_face.PNG)
*Configuration de test sur Voron 2.4 R2 avec stack BTT complet*

## ✨ Fonctionnalités

- 🎯 **Détection 3 zones** : Compression, Neutre, Tension
- 📊 **Monitoring temps réel** : Interface web moderne avec WebSocket
- 🔧 **Calibration automatique** : Point neutre configurable
- 📱 **Interface responsive** : Compatible mobile/tablette/desktop
- 🌐 **Accès réseau** : WiFi avec mDNS (.local)
- ⚡ **Sorties GPIO** : Contrôle direct des signaux

## 🛠️ Matériel requis

- **ESP32 Wemos D1 Mini 32**
- **2x Capteurs Hall SS49E**
- **Résistances de pull-up** (si nécessaire)
- **Alimentation 5V**

## 📐 Schéma de connexion

```
ESP32 D1 Mini 32
├── GPIO 32 ──── Capteur S1 (SS49E)
├── GPIO 33 ──── Capteur S2 (SS49E)
├── GPIO 26 ──── Sortie 1 (Output)
├── GPIO 25 ──── Sortie 2 (Output)
├── 3.3V   ──── VCC Capteurs
└── GND    ──── GND Capteurs
```

### 🔗 Connexion à la carte mère
**Pour BIGTREETECH MMB CAN V1.1 :**
```
ESP32 5V                ──► MMB CAN VCC
ESP32 Sortie 1 (GPIO 26) ──► MMB CAN Pin : PA3 (ou pin libre)
ESP32 Sortie 2 (GPIO 25) ──► MMB CAN Pin : PA4 (ou pin libre)  
ESP32 GND                ──► MMB CAN GND
```

## 🖨️ Intégration Klipper/Happy Hare

### Configuration Happy Hare MMU
Ce détecteur est conçu pour s'intégrer parfaitement avec [Happy Hare](https://github.com/moggieuk/Happy-Hare) dans Klipper/Kalico.

**Configuration dans `mmu_hardware.cfg` :**
```ini
[mmu_sensors]
sync_feedback_tension_pin: ^mmu:PB4      # Pin connectée à la Sortie 1 du détecteur
sync_feedback_compression_pin: ^mmu:PB3  # Pin connectée à la Sortie 2 du détecteur
```

**Configuration dans `mmu_parameters.cfg` :**
```ini
[mmu]
sync_feedback_enabled: 1
sync_feedback_buffer_range: 0.5
sync_feedback_buffer_maxrange: 7
sync_multiplier_high: 1.05
sync_multiplier_low: 0.95

autotune_rotation_distance: 1
```


**Macros Klipper personnalisées :**
```gcode
[gcode_macro STRESS_SENSOR_STATUS]
description: Affiche le statut du détecteur via l'interface web
gcode:
    # Vous pouvez interroger l'ESP32 via HTTP pour obtenir le statut
    {action_respond_info("Statut détecteur: http://voron-hall-controller.local")}
```

### Avantages de cette intégration
- ✅ **Détection précoce** : Stress détecté avant bourrage complet
- ✅ **Logique différentielle** : Plus fiable que les capteurs simples
- ✅ **Monitoring visuel** : Interface web pour diagnostic
- ✅ **Configuration flexible** : Calibration via interface web
- ✅ **Intégration native** : Signaux GPIO standard pour Klipper

## 🚀 Installation

### 1. Cloner le repository
```bash
git clone https://github.com/Manu512/stress-filament-detector.git
cd stress-filament-detector
```

### 2. Configuration PlatformIO
```bash
# Installer PlatformIO si nécessaire
pip install platformio

# Compiler et uploader le firmware
pio run --target upload

# Uploader l'interface web
pio run --target uploadfs
```

### 3. Configuration WiFi
Créer le fichier `src/config_private.h` avec vos credentials :
```cpp
// config_private.h - Configuration WiFi PRIVÉE (NE PAS COMMITTER!)
#ifndef CONFIG_PRIVATE_H
#define CONFIG_PRIVATE_H

// Configuration WiFi
const char* ssid = "VotreReseauWiFi";
const char* password = "VotreMotDePasse";

// Configuration OTA
const char* ota_password = "update123";

#endif
```

**⚠️ Important :** Ce fichier est exclu du git pour protéger vos credentials.

## 🎮 Utilisation

### Interface Web
1. Connecter l'ESP32 au réseau WiFi
2. Accéder à `http://voron-hall-controller.local`
3. Monitoring en temps réel des capteurs
4. Calibration du point neutre si nécessaire

### Logique de détection
- **🔴 COMPRESSION** : S1 > seuil ET S2 < seuil
- **🟡 NEUTRE** : S1 et S2 dans zone ±8 points
- **🔵 TENSION** : S1 < seuil ET S2 > seuil

## 📊 Paramètres

| Paramètre | Valeur par défaut | Description |
|-----------|-------------------|-------------|
| Fréquence d'échantillonnage | 20 Hz | Lecture des capteurs |
| Zone neutre | ±8 points | Seuil de tolérance |
| Point neutre S1 | 2000 | Valeur de référence |
| Point neutre S2 | 1993 | Valeur de référence |

## 🔧 Développement

### Structure du projet
```
├── src/
│   └── main.cpp           # Firmware ESP32
├── data/
│   ├── index.html         # Interface web
│   ├── style.css          # Styles modernes
│   └── script.js          # JavaScript WebSocket
├── platformio.ini         # Configuration PlatformIO
└── README.md             # Documentation
```

### Dépendances
- `AsyncTCP` - Communication WebSocket
- `ESPAsyncWebServer` - Serveur web
- `ArduinoJson` - Parsing JSON
- `ESPmDNS` - Résolution .local

## 🎨 Interface

L'interface web moderne propose :
- Dashboard temps réel avec indicateurs visuels
- Graphiques des valeurs des capteurs
- Système de calibration intuitif
- Design responsive avec animations fluides

## 📝 Configuration avancée

### Personnalisation des seuils
```cpp
// Dans main.cpp
int neutralZone = 8;           // Zone neutre ±8 points
int sampleCount = 10;          // Moyennage sur 10 échantillons
unsigned long debugInterval = 2000; // Debug toutes les 2s
```

## 🤝 Contribution

Les contributions sont les bienvenues ! 

1. Fork le projet
2. Créer une branche feature (`git checkout -b feature/AmazingFeature`)
3. Commit les changements (`git commit -m 'Add AmazingFeature'`)
4. Push vers la branche (`git push origin feature/AmazingFeature`)
5. Ouvrir une Pull Request

## 📄 License

Ce projet est sous licence MIT. Voir le fichier [LICENSE](LICENSE) pour plus de détails.

## �️ Setup de développement

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

## �👨‍💻 Auteur

**Manu512**
- GitHub: [@Manu512](https://github.com/Manu512)
- Projet: Détecteur de Stress Filament
- Date: Octobre 2025
- Setup: Voron 2.4 R2 + RPi4B + Waveshare 4.3" + Octopus Pro + U2C CAN + MMB + SB2209 + TMC5160(48V)

## 🔗 Liens utiles

- [Happy Hare MMU](https://github.com/moggieuk/Happy-Hare) - Multi-Material Unit pour Klipper
- [Voron ERCF Filament Stress Sensor](https://www.printables.com/model/803180-voron-ercf-filament-stress-sensor) - Projet original par jmillerfo
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