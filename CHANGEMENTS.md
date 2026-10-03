# Refonte : fiabilité réseau et sortie proportionnelle

> **Journal technique.** Les sections 1 à 8 racontent la refonte de septembre 2026,
> menée sur la branche `feat/proportionnel-et-wifi-resilient`, fusionnée depuis. Elles
> sont conservées telles qu'elles ont été écrites ; ce qui s'est révélé faux y est
> corrigé sur place, avec la date. La section 9 fait le point au 2026-10-03.
>
> **Depuis le 2026-10-03, sur `main`, la sortie analogique n'est plus le DAC.**
> Tout ce que les sections 2 à 7 disent du DAC au présent (256 puis 96
> niveaux, `kDacMin`, `deltaToDac`, champ `dac`, bornes 0.623 / 0.795 / 0.969,
> « aucun changement de câblage ») décrit la variante `sortie-dac-direct`. L'état
> réel est en section 10.

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

30 cas au 2026-10-03, contre 21 annoncés ici à l'origine : rejet du mode commun,
convergence du filtre, saturation et monotonie de la sortie PWM, hystérésis, refus d'une
calibration incohérente.

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

> **Correction du 2026-10-03.** Ce n'est plus vrai : l'interface a été étendue
> depuis (mesure différentielle, sortie proportionnelle, réglages, réseau) et lit
> ces champs. La liste ci-dessus oubliait le champ `wifi_rssi`, que le module envoie
> mais que l'interface n'affiche pas, la commande `set_neutral`, et la clé
> `log_category`.
> Sur `main`, le champ `dac` est remplacé par `pwm` et `pwm_full`.

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

> **2026-10-03.** Essayé et adopté, avec 10 µF : voir la section 10.

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

> **État au 2026-10-03.** Une impression à deux outils (T2 et T5, 304 min) est
> allée à son terme le 2026-09-26. La qualité de la régulation pendant les
> changements d'outil n'a pas été consignée dans ce journal.

Optionnel, jamais mesuré : le comportement **moteurs en marche**. Tous les relevés
ci-dessus ont été faits à l'arrêt. La ligne PB12 porte maintenant un niveau
analogique lent au milieu d'une carte qui pilote quatre moteurs pas à pas ; un
coup d'oscilloscope pendant un mouvement lèverait le dernier doute.

> **Correction du 2026-10-03.** Ce passage citait deux scripts « fournis à la racine
> du projet », `surveiller.py` et `commander.py`. Ils n'ont jamais été ajoutés au
> dépôt.

**Point à surveiller** : trois redémarrages sur *brownout* au tout premier
démarrage après flash, plus aucun ensuite. Probablement l'appel de courant de la
radio sur l'alimentation USB du poste. En service le module est alimenté par le
5 V de la MMB. Si le phénomène réapparaît, le levier est `WiFi.setTxPower()`.

## 9. Point au 2026-10-03

### Deux variantes, deux branches

- `sortie-dac-direct` : le firmware décrit par les sections 1 à 8. Sortie DAC
  160..255, reliée directement à l'entrée STP8.
- `main`, depuis la fusion de `sortie-pwm-filtre-rc` le 2026-10-03 : la sortie
  analogique passe en PWM, filtré par 1 kΩ et 10 µF. C'est le firmware en service
  sur la machine.

Les bornes Klipper ne sont pas les mêmes d'une variante à l'autre. Le filtre est
maintenant soudé sur la machine : y remettre le firmware DAC impose de remesurer
ses bornes, celles de la section 7 ayant été relevées sans résistance en série.

### Calibration par les deux butées

La section 6 concluait que le neutre « ne peut se capturer que filament chargé ».
En pratique il se calcule : c'est le milieu des deux butées mécaniques. Relevé du
2026-10-03, sans filament :

```
butee compression (repos)   S1 1753 mV   S2 1607 mV   S1-S2 = +146
butee tension (tenue)       S1 1662 mV   S2 1770 mV   S1-S2 = -108
milieu                      S1 1707,5    S2 1688,5    course +-127
```

Envoyé au module par `set_neutral` : `n1 = 1708`, `n2 = 1689`, `span = 125`, soit
127 moins 2 unités de bruit, pour que les butées saturent franchement la sortie.

La calibration précédente (1702 / 1683, `span` 134) avait le même écart S1−S2 au
neutre, 19 : elle était centrée. Seule sa pleine échelle était trop large de 5 %,
et les butées ne donnaient que ±0,95.

Par rapport au relevé du 2026-09-24 (S1 de 1625 à 1754 mV, S2 de 1567 à 1770 mV,
amplitude du delta 276), la course mesurée ici est plus courte : 254. La cause
n'est pas établie. Le balayage à la main de septembre a pu dépasser les butées
relevées ici, ou l'aimant a pu bouger entre-temps.

### Incident du 2026-10-03

Impression mono-filament (T2) avec le firmware DAC. Faits relevés dans `mmu.log`
et sur la machine :

- 09:55, 09:56 et 10:00 : FlowGuard déclenche trois fois, « Compression stuck »
  après 164 à 213 mm de mouvement et 40 à 41 mm de correction
  (`flowguard_max_relief: 40`).
- La distance de rotation corrigée de la porte, stable vers 24,9 pendant la
  première couche, monte à 30,9 en moins de trois minutes avant le premier arrêt.
- `ADJUST_TENSION`, servo baissé : 13,8 mm de recul commandés au pignon, 8,7 mm
  comptés par l'encodeur, et la lecture du capteur ne bouge pas (0,87 à 0,89).
- Le filament est retrouvé cassé dans le bowden.
- Après réparation, l'impression reprend et va à son terme (225 min). Sur 45 s, la
  lecture oscille alors de −0,67 à +0,52, médiane −0,51.

**La cause n'est pas établie.** Une casse dans le bowden alors que le capteur lit
une compression fait penser à une lecture fausse, mais rien ne l'a démontré. Le
même fichier était allé à son terme le 2026-09-25. L'aimant du buffer a été bloqué
mécaniquement dans la foulée, par précaution, et la calibration refaite comme
ci-dessus.

L'oscillation de ±0,6 ne vient pas de la calibration, qui était centrée. Son
origine n'a pas été cherchée.

### Défauts corrigés le 2026-10-03

Relevés en confrontant la documentation au code, puis corrigés.

- **Une calibration refusée restait active.** `set_neutral` et
  `save_simple_calibration` écrivaient les valeurs reçues dans la calibration en
  service avant de les valider. Refusées, elles n'étaient pas enregistrées en NVS,
  mais restaient appliquées jusqu'au redémarrage. Les deux commandes valident
  désormais une copie (`assignIfValid`), et `alpha` est borné de 1 à 256. Trois
  tests natifs couvrent ce garde-fou.
- **L'interface affichait des valeurs fausses.** La légende « Zone Tampon ±8 »
  était figée alors que la bande morte vaut 16 par défaut : elle suit maintenant
  la valeur du module. Les valeurs affichées avant la première trame (2000 / 1993,
  ±400) dataient d'avant le passage en millivolts : elles sont remplacées par un
  tiret. La ligne « Sortie 2 (GPIO 25) » indiquait LOW en mode analogique : elle
  indique maintenant la nature de la sortie.
- **La consigne de calibration de l'interface contredisait ce journal.** Elle
  demandait de placer le filament « en position neutre (pas de contrainte) », alors
  que la position de repos est la butée de compression. Elle renvoie maintenant à
  la calibration par les deux butées.

## 10. Sortie PWM filtrée, 2026-10-03

La section 7 écartait le PWM : « aucun problème mesuré ne le justifie ». Il a été
essayé le 2026-10-03 pour gagner en plage et en résolution. Ce n'est pas une
réponse à l'incident du jour (section 9), dont la cause reste inconnue.

### Matériel

```
GPIO 25 ---[ 1 kOhm ]---+--- cable vers STP8 (PB12)
                        |
                     [ 10 uF ]   electrolytique traversant, + cote resistance
                        |
                       GND
```

Filtre soudé côté module. 10 µF et non les 4,7 µF envisagés en section 7 : c'est
la valeur qui était disponible. Constante de temps calculée, résistance seule :
10 ms.

### Firmware

`deltaToDac` devient `deltaToDuty`. LEDC canal 0, 20 kHz, 11 bits, rapport
cyclique borné de 5 % à 95 % (102..1945, neutre 1024) : 1844 niveaux contre 96.
Le statut WebSocket porte `pwm` et `pwm_full` à la place de `dac`. 30 tests natifs.

### Courbe de transfert mesurée

Bras immobile. Le rapport cyclique est imposé depuis le module en déplaçant son
neutre (`set_neutral`), et lu côté Klipper dans `filament_proportional.value_raw`,
médiane sur 3 s :

```
 pwm      rapport   value_raw   tension
  102      5,0 %     0,1396     0,461 V
  336     16,4 %     0,2416     0,797 V
  570     27,8 %     0,3464     1,143 V
  803     39,2 %     0,4518     1,491 V
 1024     50,0 %     0,5515     1,820 V
 1240,5   60,6 %     0,6467     2,134 V
 1473,5   72,0 %     0,7508     2,478 V
 1704     83,2 %     0,8549     2,821 V
 1945     95,0 %     0,9593     3,166 V
```

Droite ajustée : `value_raw = 4,462e-4 × pwm + 0,0931`, écart maximal 0,0017.
Bornes écrites dans `mmu_hardware.cfg` : `0.139 / 0.550 / 0.961`. L'étendue couvre
82 % de l'échelle ADC, contre 35 % avec le DAC.

L'extrapolation à 0 % donne 0,31 V. C'est cohérent avec le schéma de la section 2 :
1 kΩ de filtre et 100 Ω d'entrée en série, face au tirage de 10 kΩ, donnent
3,3 × 1,1 / 11,1 = 0,33 V.

À rapport cyclique fixe, la lecture se disperse d'environ 0,01 crête à crête. Une
unité de delta pèse 0,003 : c'est le bruit du capteur qui se voit, maintenant que
la sortie est assez fine pour le montrer. L'ondulation du PWM n'a pas été mesurée
à l'oscilloscope.

### Le DAC qui ne lâche pas la broche

Premier flash OTA du firmware PWM par-dessus le firmware DAC : Klipper lit 0,958
quel que soit le rapport cyclique, de 5 % à 95 %. La sortie ne suit pas.

Un passage par le mode tout ou rien, donc un `pinMode` sur GPIO 25, rend la main :
broche à l'état bas, Klipper lit 0,5445. De retour en PWM, la courbe est linéaire
mais écrasée, de 0,569 à 0,981, avec un plancher à 1,80 V, proche de celui du DAC
relevé en section 7.

Explication retenue, non démontrée au-delà de ces relevés : l'état du DAC survit
au redémarrage logiciel d'une mise à jour OTA. `ledcAttachPin` ne rend pas la
broche au numérique, d'où la sortie figée. `pinMode` la rend, mais le DAC reste
alimenté et se bat contre la sortie logique, d'où le plancher.

Correctif : `dacDisable()` puis `pinMode()` avant `ledcAttachPin()`. Vérifié en
reflashant le firmware DAC puis le firmware PWM corrigé, sans coupure
d'alimentation : la courbe du tableau ci-dessus sort directement.

Une courbe relevée avant ce correctif ne vaut rien. Celle de 0,569 à 0,981 aurait
donné des bornes fausses, et un tirage apparent de 0,84 kΩ au lieu de 10 kΩ.

### Ce qui n'a pas été vérifié

- La courbe après une vraie coupure d'alimentation.
- Le comportement en impression. Une première impression démarre au moment où ces
  lignes sont écrites ; son résultat n'est pas consigné ici.
- Le comportement moteurs en marche, comme en section 8.
- L'effet de bord utile de la section 2, fil débranché égale compression maximale.
  Avec ces bornes, une entrée remontée à 3,3 V dépasse `max_compression` (0,961) ;
  ce que Happy Hare en fait n'a pas été observé.

### Reset matériel par PB3 : essayé, inopérant

Le fil qui reliait PB3 (header I2C de la MMB) à GPIO 25 a été déplacé vers la
broche RST de l'ESP32, pour pouvoir redémarrer le module depuis Klipper.

- PB3 déclaré en entrée : il lit l'état haut pendant que la ligne du capteur
  balaie 0,139 à 0,961. Le fil n'est donc plus sur la ligne du capteur.
- PB3 déclaré en sortie, impulsion basse de 200 ms : l'ESP32 ne redémarre pas, son
  WebSocket émet sans interruption.

Le fil ne relie donc pas PB3 à RST. Le premier essai ne prouvait rien de plus : une
entrée en l'air, ou tirée par ailleurs, lit aussi l'état haut. PB3 est laissé non
déclaré dans Klipper en attendant un contrôle au multimètre.
