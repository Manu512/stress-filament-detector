# Refonte : fiabilité réseau et sortie proportionnelle

Branche `feat/proportionnel-et-wifi-resilient`. Deux chantiers indépendants, dans
un ordre choisi : la fiabilité d'abord, parce qu'elle peut planter une impression
aujourd'hui, le proportionnel ensuite, qui est un gain.

## 1. Fiabilité — le réseau ne bloque plus la mesure

### Le défaut

```cpp
// src/main.cpp, version d'origine
WiFi.begin(ssid, password);
while (WiFi.status() != WL_CONNECTED) {   // aucune sortie
  delay(500);
  Serial.print(".");
}
```

Cette boucle est dans `setup()`, donc **avant** `loop()`. Tant que le WiFi ne
répond pas, rien ne démarre : ni la lecture des capteurs, ni les sorties vers la
MMB, ni l'OTA qui aurait permis de se rattraper.

Les sorties restent alors à l'état laissé par `setupHardware()`, `LOW`/`LOW`,
c'est-à-dire **neutre en permanence**. Happy Hare croit le buffer au repos alors
que le capteur ne mesure plus rien. C'est une panne silencieuse, et elle se
déclenche sur un simple redémarrage de box.

### Ce qui remplace

`src/net.h` et `src/net.cpp` : une machine à états appelée depuis `loop()`, qui
ne bloque jamais.

```
Idle -> Connecting -> Connected
             |            |
             |  echec     |  perte de lien
             v            v
          Retrying <------+
             |
             |  3 echecs consecutifs
             v
        AccessPoint   (SSID stress-filament-xxxxxx)
```

- Délai maximal de connexion : 15 s. Reprise toutes les 20 s.
- Après trois échecs, point d'accès de configuration.
- **Identifiants en NVS**, plus compilés en dur. `src/config.h` et
  `config_private.h` disparaissent. Deux commandes WebSocket les gèrent :
  `set_wifi` et `forget_wifi`.
- L'espace NVS `net` est distinct de `hall` : effacer le WiFi ne touche pas à la
  calibration.

Dans `setup()`, l'ordre est désormais explicite : capteurs et sorties d'abord,
réseau ensuite.

## 2. Sortie proportionnelle — Happy Hare type P

### Pourquoi

Happy Hare distingue quatre styles de capteur de sync-feedback. Le montage
d'origine est de **type D**, deux interrupteurs. Le **type P**, analogique,
change deux choses :

| | Type D | Type P |
|---|---|---|
| AutoTune | mode « deux niveaux » : la vitesse du gear **oscille en permanence** autour de l'estimation, même après convergence | EKF, filtre de Kalman : pas d'oscillation |
| FlowGuard v4 | détection bouchon/emmêlement | idem, **plus** la prévention active |

La prévention d'emmêlement est conditionnée dans le code de Happy Hare v4 :

```python
if self.p.tangle_prevention_enabled and self.mmu.sensor_manager.has_sensor(SENSOR_PROPORTIONAL):
```

Le type P est déjà supporté par la **v3**, via `sync_feedback_analog_pin` : cette
évolution ne demande donc pas de migrer en v4.

### Ce qui change dans le firmware

L'information continue existait déjà : elle était calculée puis jetée.

```cpp
int cval1 = s1 / 10;   // 0..4095
int cval2 = s2 / 10;
// ...puis reduite a 3 etats
```

Elle est maintenant exposée sur le DAC. GPIO 25 et 26 sont les deux seules
broches DAC de l'ESP32, et la version d'origine les utilisait déjà en numérique :
**aucun changement de câblage côté module**.

- `ANALOG_OUT_PIN` = GPIO 25, tension continue, 256 niveaux
- GPIO 26 reste numérique et signale la compression, utilisable comme endstop de
  homing extrudeur
- Bascule type D / type P par la commande `set_analog_output`, sans reflasher

### Côté Klipper

Une broche **ADC** est nécessaire côté MMB. Croisement entre les broches ADC du
STM32G0B1 (`stm32f0_adc.c`, branche `CONFIG_MACH_STM32G0`) et celles que BTT sort
sur connecteur :

```
MOT    PA0   servo selecteur     pris
Sensor PA1   encodeur            pris
RGB    PA2   neopixel            pris
STP1   PA3   gear DIAG           cavalier pose volontairement, on garde
STP2   PA4   selecteur DIAG      idem
STP8   PB12  pre-gate 5          <- RETENU
STP9   PB11  pre-gate 6          disponible aussi
STP10  PB10  pre-gate 7          disponible aussi, mais plus loin
STP11  PB2   endstop selecteur   pris
```

Toutes les broches ADC exposées sont occupées : il faut permuter. Ça ne coûte rien
puisqu'un pre-gate est une entrée **logique**, qui accepte n'importe quelle broche.

**`PB12` / STP8 est retenu** : c'est le plus proche du header I2C parmi les trois
libérables, et ça passe au millimètre côté longueur de câble.

| Fil | De | Vers |
|---|---|---|
| sortie analogique ESP32, GPIO 25 | PB4 (header I2C) | **STP8**, broche signal (`PB12`) |
| interrupteur pre-gate 5 | STP8 | **PB4** (header I2C) |

```ini
# mmu.cfg, [board_pins mmu]
MMU_PRE_GATE_5=PB4              # etait PB12

# mmu_hardware.cfg, [mmu_sensors]
sync_feedback_tension_pin:
sync_feedback_compression_pin:
sync_feedback_analog_pin: mmu:PB12
sync_feedback_analog_max_compression: <releve>
sync_feedback_analog_max_tension:     <releve>
sync_feedback_analog_neutral_point:   0.50
```

### Pourquoi pas PA3 ou PA4, plus proches encore

Ce sont les DIAG du gear et du sélecteur, et les cavaliers sont posés
**volontairement**. En libérer un imposerait aussi de retirer la ligne
`extra_endstop_pins: tmc2209_...:virtual_endstop` correspondante — vérifié dans
`klippy/extras/tmc.py` de Kalico, ligne 738 :

```python
if self.diag_pin is None:
    raise ppins.error("tmc virtual endstop requires diag pin config")
```

Klipper refuserait de démarrer sinon.

### Ce que porte l'entrée, d'après le schéma BTT

Chaque entrée d'endstop, `STP1` à `STP11`, porte le même montage :

```
3.3V ---[ 10K ]---+--- signal ---[ 100R ]--- broche du connecteur
                  |
               [ 100nF ]
                  |
                 GND
```

Conséquences : le RC de 100 Ω et 100 nF donne 10 µs, sans effet à notre bande
passante et même utile en anti-repliement.

> **Ce paragraphe affirmait une chose fausse, que la mesure a démentie — voir la
> section 7.** Il disait que le tirage tirait « 330 µA au plus, que la sortie
> bufferisée du DAC absorbe avec une erreur de quelques dizaines de millivolts au
> point bas ». L'erreur réelle au point bas est de **1,9 volt** : le DAC de
> l'ESP32 ne sait pratiquement pas absorber de courant, et la moitié basse de sa
> plage est inexploitable.
>
> C'était une déduction tirée d'un schéma et écrite au présent de l'indicatif. Le
> relevé du point bas n'était pas un détail d'ajustement, c'était le point à
> sonder en premier.

Effet de bord utile, celui-ci confirmé par la mesure : si l'ESP32 n'est pas
alimenté ou si le fil se débranche,
le tirage remonte l'entrée à 3,3 V, donc **compression maximale**. Une panne du
capteur se voit, au lieu de passer pour un neutre.

**Repérage de la broche signal sur le connecteur** : les trois broches sont 5 V,
GND et signal. Le signal est la seule à **3,3 V** à vide, précisément à cause de ce
tirage.

## 3. Qualité de la mesure

| Point | Avant | Après |
|---|---|---|
| Échantillonnage | `10 x delay(2)` = **20 ms bloquants**, plus `delay(20)` en fin de boucle | accumulateur non bloquant, 8 échantillons à 200 Hz |
| Lecture ADC | `analogRead()` brut, non linéaire | `analogReadMilliVolts()`, courbe de calibration d'usine |
| Filtrage | moyenne de 10 seulement | moyenne + filtre exponentiel réglable |
| Sorties | mises à jour **au changement d'état** | écriture continue du DAC |

## 4. Architecture et tests

Tout ce qui décide quelque chose vit dans `lib/stress_core`, **sans Arduino** :
calcul du delta, filtre, conversion DAC, machine à états, validation de la
calibration. `src/main.cpp` ne fait plus que lire, écrire et servir le web.

```
pio test -e native
```

21 cas : rejet du mode commun, convergence du filtre, saturation et monotonie du
DAC, hystérésis, refus d'une calibration incohérente.

**Deux défauts réels ont été trouvés par ces tests pendant l'écriture :**

1. **Zone morte du filtre.** `(ecart * alpha) / 256` tronque vers zéro : sous
   `256/alpha` unités d'écart, l'incrément tombe à 0 et le filtre se fige avant
   la consigne. Biais permanent sur le DAC. Corrigé par un arrondi au plus
   proche, plus un pas minimal garanti.
2. **Saturation basse fausse.** `128 + (-400 * 255) / 800` tronque vers zéro,
   donc −127 au lieu de −128 : la sortie valait 1 au lieu de 0. La borne haute
   tombait juste par hasard. Corrigé par un arrondi s'écartant de zéro.

Sur ce poste Windows il n'y a pas de `gcc` natif ; les tests sont joués via WSL :

```bash
wsl -d rag-sherpa -- bash -c 'cd /mnt/d/Projet/Voron/ESP32/amont && \
  g++ -std=gnu++17 -Wall -Wextra -I lib/stress_core -I .pio/libdeps/native/Unity/src \
  lib/stress_core/*.cpp test/test_stress_core/*.cpp .pio/libdeps/native/Unity/src/unity.c \
  -o /tmp/t && /tmp/t'
```

## 5. Contrat WebSocket

Tous les champs et commandes d'origine sont **conservés** : `raw1`, `raw2`,
`output1`, `output2`, `neutral_zone`, `cal_valid`, `cal_neutral_raw1/2`,
`position`, et les commandes `capture_neutral`, `save_simple_calibration`,
`reset_calibration`, `send_updates`, `interval_ms`.

Ajouts : `delta`, `span`, `hysteresis`, `alpha`, `dac`, `analog_out`,
`tension_permille`, `wifi_phase`, `wifi_ip`, `wifi_ssid`, et les commandes
`capture_span`, `set_analog_output`, `set_wifi`, `forget_wifi`.

L'interface web existante fonctionne donc sans modification ; elle ignore
simplement les nouveaux champs.

## 6. Mesures relevées sur la machine le 2026-09-24

Firmware flashé et vérifié : HTTP 200, reconnexion WiFi automatique, ADC
caractérisé sur eFuse Vref 1100, aucun redémarrage en régime établi.

**Le module se reconnecte sans qu'aucun identifiant n'ait été manipulé.** L'IDF
conserve la configuration station dans sa propre NVS (`nvs.net80211`), et
`WiFi.begin()` sans argument la réutilise. C'est ce que fait `net::lancerTentative()`
quand notre espace NVS est vide.

Relevé **sans filament**, ressort en butée, buffer balayé à la main d'un bout à
l'autre :

```
delta   min -123   max +153   amplitude 276
DAC     min   89   max  177   sur 0-255
S1      1625 a 1754 mV        S2  1567 a 1770 mV
position de repos a vide : delta +106 a +136, tres stable (amplitude 10)
```

### Piège d'unité, corrigé

Le firmware lit désormais en **millivolts** (`analogReadMilliVolts`), plus en
comptes ADC bruts. Les valeurs de neutre par défaut reprises de l'ancienne
version (2000 / 1993, en comptes) produisaient un delta d'une centaine d'unités
au repos et un état COMPRESSION permanent. Les défauts sont passés en mV.

### Le buffer est à ressort

Confirmé : au repos sans filament, le ressort pousse le bras en butée côté
compression. Trois conséquences :

1. **Le neutre ne peut se capturer que filament chargé.** Sans lui, on ne
   capture que la butée du ressort.
2. L'état « collé en butée compression » devient un **indicateur de présence
   filament**. Happy Hare v4 a un réglage dédié, `Buffer resting spring state`,
   à positionner sur `compression`.
3. La course sera **asymétrique** autour du neutre. Côté Klipper c'est prévu :
   `analog_max_compression`, `analog_max_tension` et `analog_neutral_point` sont
   trois réglages distincts. Le mappage DAC du firmware, lui, reste symétrique
   (`±span`) — à revoir si l'asymétrie mesurée s'avère forte.

## 7. Reste à faire

## 7. Le plancher du DAC — mesuré le 2026-09-24

La bascule en type P semblait fonctionner : klippy démarrait sans un
avertissement. Elle était pourtant inexploitable, parce que les bornes de
remplissage `0` et `1` acceptent n'importe quoi.

### Ce qui a été relevé

En imposant le DAC pas à pas, bras immobile, et en lisant l'ADC de la MMB :

```
dac   0..144   lecture coincee entre 2,07 et 1,89 V, et DECROISSANTE
dac 144        coude
dac 144..255   lineaire, collee au nominal a 0,1 V pres
```

Sous le coude, la courbe de transfert **s'inverse** : deux positions du bras y
donnent la même tension. Aucun capteur proportionnel ne peut fonctionner ainsi.

### Le câblage n'est pas en cause

La **même broche** pilotée en numérique atteint `0,017 V` et `3,300 V`. Le nœud
peut donc être tiré aux deux rails sans difficulté. C'est le **DAC** qui ne sait
pratiquement pas absorber de courant : son buffer de sortie est conçu pour des
charges à haute impédance, et il ne contre pas le tirage de l'entrée STP8.

### Ce qui a été corrigé

`lib/stress_core/stress_core.h` : `kDacMin` passe de 0 à **160**, `kDacMax` reste
à 255, le neutre tombe à **208**. `deltaToDac` compte désormais à partir de
`kDacMin` plutôt qu'en écart signé autour du neutre : le numérateur reste positif,
un seul arrondi suffit, et les deux extrémités tombent exactement sur les bornes —
ce que la forme signée ne garantissait pas sur une plage de 95 pas, impaire.

`isValid()` borne aussi `span` à 4095, qui entre dans un produit.

Départ à 160 et non 145 : 16 pas de marge sous le coude, dont la stabilité en
température n'a pas été caractérisée.

### Ce que ça coûte, et ce que ça ne coûte pas

96 niveaux au lieu de 256, soit **1 % de la course par pas**. Le bruit mesuré au
repos est de 0,8 mV contre 12 mV de quantification : c'est donc la quantification
qui limite, d'un facteur 15. Largement suffisant pour un estimateur de biais lent.

Si la pleine échelle devenait nécessaire, la solution est du **PWM sur GPIO 25**
filtré par 1 kΩ + 4,7 µF — un GPIO est push-pull, le tirage devient sans objet.
Écarté pour l'instant : aucun problème mesuré ne le justifie.

### Bornes relevées, écrites dans `mmu_hardware.cfg`

```
butee tension       dac 160   raw 0.623   2,057 V
neutre              dac 208   raw 0.795   2,624 V
butee compression   dac 255   raw 0.969   3,197 V
```

Réponse vérifiée strictement croissante sur cinq points, et confirmée en direct :
le bras promené à la main parcourt bien −0,997 à +0,978.

Piège rencontré : **`sync_feedback_bias_raw` n'est pas une lecture ADC.**
`mmu_sync_feedback_manager.py` renvoie `value`, déjà mappée dans `[-1, 1]`, et non
`value_raw`. Raisonner en volts dessus mène à des conclusions absurdes.

### Course du buffer

`sync_feedback_buffer_range` valait 2, hérité du type D où il désignait l'écart
entre les deux interrupteurs. Mesuré filament chargé, pincement maintenu :
**11,5 mm**, sur trois relevés convergents. Porté à 12, `maxrange` à 14.

Ce paramètre ne règle pas que le gain de l'EKF : `maxrange` fixe toute l'échelle
de correction, `nudge` compris. À 6, la neutralisation abandonnait à 5,88 mm avec
« exceeded buffer ».

Le PTFE ayant du jeu avec le filament pour ne pas coincer, il y a **~1,5 mm de
course morte au changement de sens**. Des pas de mesure inférieurs à ce jeu ne
produisent rigoureusement rien.

## 8. Reste à faire

Une **impression avec changement d'outil** : le seul test qui exerce la boucle
complète, et le seul qui dira si le gain corrigé rend la régulation plus franche.

Optionnel, jamais mesuré : le comportement **moteurs en marche**. Tous les relevés
ci-dessus ont été faits à l'arrêt. La ligne PB12 porte maintenant un niveau
analogique lent au milieu d'une carte qui pilote quatre moteurs pas à pas ; un
coup d'oscilloscope pendant un mouvement lèverait le dernier doute.

**Outils fournis à la racine du projet :**

```
surveiller.py [duree] [ip]     releve en continu, min/max, span suggere
commander.py capture_neutral   envoie une commande et montre l'effet
commander.py capture_span
commander.py set_analog_output 0|1
commander.py regler span=153 alpha=32
```

**Point à surveiller** : trois redémarrages sur *brownout* au tout premier
démarrage après flash, plus aucun ensuite. Probablement l'appel de courant de la
radio sur l'alimentation USB du poste. En service le module est alimenté par le
5 V de la MMB. Si le phénomène réapparaît, le levier est `WiFi.setTxPower()`.
