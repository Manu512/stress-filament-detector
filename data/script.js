/* script.js - Interface propre pour détecteur de stress filament */

// Variables globales
let ws = null;
// Valeurs par defaut du firmware, en millivolts. Elles sont remplacees des la
// premiere trame recue du module.
let neutralRaw1 = 1751;
let neutralRaw2 = 1639;
let deadbandPoints = 16;

// Initialisation
document.addEventListener('DOMContentLoaded', function() {
  initWebSocket();
  
  // Bouton de calibration
  const capButton = document.getElementById('capNeutral');
  if (capButton) {
    capButton.addEventListener('click', captureNeutral);
  }
});

// WebSocket
function initWebSocket() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const wsUrl = `${protocol}//${window.location.host}/ws`;
  
  ws = new WebSocket(wsUrl);
  
  ws.onopen = function() {
    updateConnectionStatus(true);
    console.log('WebSocket connecté');
  };
  
  ws.onmessage = function(event) {
    try {
      const data = JSON.parse(event.data);
      updateUI(data);
    } catch (e) {
      console.error('Erreur parsing JSON:', e.message);
    }
  };
  
  ws.onclose = function() {
    updateConnectionStatus(false);
    console.log('WebSocket fermé - reconnexion dans 3s');
    setTimeout(initWebSocket, 3000);
  };
  
  ws.onerror = function(error) {
    console.error('Erreur WebSocket:', error);
    updateConnectionStatus(false);
  };
}

// Mise à jour du statut de connexion
function updateConnectionStatus(connected) {
  const wsStatus = document.getElementById('wsStatus');
  const status = document.getElementById('status');
  const wsStatusBadge = document.getElementById('wsStatusBadge');
  const statusBadge = document.getElementById('statusBadge');
  
  if (connected) {
    if (wsStatus) wsStatus.textContent = 'Ouvert';
    if (status) status.textContent = 'Connecté';
    if (wsStatusBadge) {
      wsStatusBadge.textContent = 'Connecté';
      wsStatusBadge.className = 'status-badge connected';
    }
    if (statusBadge) {
      statusBadge.textContent = 'En ligne';
      statusBadge.className = 'status-badge connected';
    }
  } else {
    if (wsStatus) wsStatus.textContent = 'Fermé';
    if (status) status.textContent = 'Déconnecté';
    if (wsStatusBadge) {
      wsStatusBadge.textContent = 'Déconnecté';
      wsStatusBadge.className = 'status-badge disconnected';
    }
    if (statusBadge) {
      statusBadge.textContent = 'Hors ligne';
      statusBadge.className = 'status-badge disconnected';
    }
  }
}

// Mise à jour interface
function updateUI(data) {
  if (data.raw1 !== undefined) {
    const raw1El = document.getElementById('raw1');
    if (raw1El) {
      raw1El.textContent = data.raw1;
      updateZoneStatus();
    }
  }
  
  if (data.raw2 !== undefined) {
    const raw2El = document.getElementById('raw2');
    if (raw2El) {
      raw2El.textContent = data.raw2;
      updateZoneStatus();
    }
  }
  
  if (data.output1 !== undefined) {
    const output1El = document.getElementById('output1');
    if (output1El) {
      output1El.textContent = data.output1 ? 'HIGH' : 'LOW';
      output1El.className = data.output1 ? 'output-value high' : 'output-value low';
    }
  }
  
  if (data.output2 !== undefined) {
    const output2El = document.getElementById('output2');
    if (output2El) {
      // En mode analogique GPIO 25 ne porte pas un niveau logique : afficher
      // LOW serait faux.
      if (data.analog_out) {
        output2El.textContent = 'PWM';
        output2El.className = 'output-value low';
      } else {
        output2El.textContent = data.output2 ? 'HIGH' : 'LOW';
        output2El.className = data.output2 ? 'output-value high' : 'output-value low';
      }
    }
  }
  
  // Champs ajoutes par la version proportionnelle. Appel en tete pour que
  // l'affichage reste coherent meme si un champ historique manque.
  majMesure(data);

  if (data.position !== undefined) {
    updateCurrentState(data.position);
  }
  
  if (data.cal_neutral_raw1 !== undefined) {
    neutralRaw1 = data.cal_neutral_raw1;
    const capN1 = document.getElementById('capN1');
    if (capN1) {
      capN1.textContent = neutralRaw1;
      updateZoneStatus();
    }
  }
  
  if (data.cal_neutral_raw2 !== undefined) {
    neutralRaw2 = data.cal_neutral_raw2;
    const capN2 = document.getElementById('capN2');
    if (capN2) {
      capN2.textContent = neutralRaw2;
      updateZoneStatus();
    }
  }
  
  if (data.neutral_zone !== undefined) {
    deadbandPoints = data.neutral_zone;
    const zoneTampon = document.getElementById('zoneTampon');
    if (zoneTampon) zoneTampon.textContent = `±${deadbandPoints}`;
    updateZoneStatus();
  }
}

// Mettre à jour l'état actuel avec les 3 zones
function updateCurrentState(position) {
  const stateDiv = document.getElementById('currentState');
  if (!stateDiv) return;
  
  // Enlever les classes précédentes
  stateDiv.className = 'current-state';
  
  switch(position) {
    case 1: // POS_SENSOR1 = COMPRESSION
      stateDiv.textContent = '🔴 COMPRESSION';
      stateDiv.classList.add('compression');
      break;
    case 2: // POS_SENSOR2 = TENSION  
      stateDiv.textContent = '🔵 TENSION';
      stateDiv.classList.add('tension');
      break;
    default: // POS_NEUTRAL = NEUTRE
      stateDiv.textContent = '🟡 NEUTRE';
      stateDiv.classList.add('neutral');
      break;
  }
}

// Mettre à jour le status des zones pour chaque capteur
function updateZoneStatus() {
  const raw1El = document.getElementById('raw1');
  const raw2El = document.getElementById('raw2');
  
  if (!raw1El || !raw2El) return;
  
  const raw1 = parseInt(raw1El.textContent) || 0;
  const raw2 = parseInt(raw2El.textContent) || 0;
  
  // Calculer les zones S1
  const s1_min = neutralRaw1 - deadbandPoints;
  const s1_max = neutralRaw1 + deadbandPoints;
  let s1_status, s1_class;
  
  if (raw1 < s1_min) {
    s1_status = 'BAS';
    s1_class = 'zone-status zone-bas';
  } else if (raw1 > s1_max) {
    s1_status = 'HAUT';
    s1_class = 'zone-status zone-haut';
  } else {
    s1_status = 'NEUTRE';
    s1_class = 'zone-status zone-neutre';
  }
  
  // Calculer les zones S2
  const s2_min = neutralRaw2 - deadbandPoints;
  const s2_max = neutralRaw2 + deadbandPoints;
  let s2_status, s2_class;
  
  if (raw2 < s2_min) {
    s2_status = 'BAS';
    s2_class = 'zone-status zone-bas';
  } else if (raw2 > s2_max) {
    s2_status = 'HAUT';
    s2_class = 'zone-status zone-haut';
  } else {
    s2_status = 'NEUTRE';
    s2_class = 'zone-status zone-neutre';
  }
  
  // Mettre à jour l'affichage S1
  const zone1 = document.getElementById('zone1');
  if (zone1) {
    zone1.textContent = s1_status;
    zone1.className = s1_class;
  }
  
  // Mettre à jour l'affichage S2
  const zone2 = document.getElementById('zone2');
  if (zone2) {
    zone2.textContent = s2_status;
    zone2.className = s2_class;
  }
  
  // Afficher les zones neutres
  const neutral1 = document.getElementById('neutral1');
  const neutral2 = document.getElementById('neutral2');
  
  if (neutral1) {
    neutral1.textContent = `[${s1_min}-${s1_max}]`;
  }
  
  if (neutral2) {
    neutral2.textContent = `[${s2_min}-${s2_max}]`;
  }
}

// Capturer point neutre
function captureNeutral() {
  if (ws && ws.readyState === WebSocket.OPEN) {
    const cmd = {
      cmd: 'capture_neutral'
    };
    ws.send(JSON.stringify(cmd));
    console.log('Capture du point neutre demandée');
    
    // Feedback visuel
    const btn = document.getElementById('capNeutral');
    if (btn) {
      const originalText = btn.textContent;
      btn.textContent = '✅ Capturé !';
      btn.style.background = 'linear-gradient(135deg, #28a745, #20c997)';
      
      setTimeout(() => {
        btn.textContent = originalText;
        btn.style.background = '';
      }, 2000);
    }
  } else {
    console.log('WebSocket non connecté');
  }
}

// ---------------------------------------------------------------------------
// Sortie proportionnelle, reglages avances et reseau
// ---------------------------------------------------------------------------

const PHASES_WIFI = ["inactif", "connexion...", "connecte", "nouvelle tentative", "point d'acces"];

function envoyer(objet) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(objet));
    return true;
  }
  console.log('WebSocket non connecte');
  return false;
}

function confirmerBouton(id, texte) {
  const b = document.getElementById(id);
  if (!b) return;
  const initial = b.textContent;
  b.textContent = texte;
  b.style.background = 'linear-gradient(135deg, #28a745, #20c997)';
  setTimeout(() => { b.textContent = initial; b.style.background = ''; }, 2000);
}

// Les champs de saisie ne sont reecrits par le module que lorsqu'ils n'ont pas
// le focus : sinon la valeur serait ecrasee pendant la frappe, a chaque
// rafraichissement.
function majChamp(id, valeur) {
  const e = document.getElementById(id);
  if (e && document.activeElement !== e && valeur !== undefined) e.value = valeur;
}

function majMesure(data) {
  if (data.delta !== undefined) {
    const e = document.getElementById('delta');
    if (e) e.textContent = data.delta;
  }
  if (data.span !== undefined) {
    const e = document.getElementById('deltaSpan');
    if (e) e.textContent = `pleine echelle ±${data.span}`;
  }
  if (data.pwm !== undefined && data.pwm_full) {
    const e = document.getElementById('pwm');
    if (e) e.textContent = `${(100 * data.pwm / data.pwm_full).toFixed(1)} %`;
  }
  if (data.tension_permille !== undefined) {
    const e = document.getElementById('tensionLevel');
    if (e) e.textContent = `tension ${data.tension_permille} ‰`;
  }
  if (data.analog_out !== undefined) {
    const e = document.getElementById('modeSortie');
    if (e) {
      e.textContent = data.analog_out ? 'type P (analogique)' : 'type D (tout ou rien)';
      e.className = data.analog_out ? 'output-value high' : 'output-value low';
    }
  }
  majChamp('inZone',  data.neutral_zone);
  majChamp('inHyst',  data.hysteresis);
  majChamp('inSpan',  data.span);
  majChamp('inAlpha', data.alpha);

  if (data.wifi_phase !== undefined) {
    const e = document.getElementById('wifiPhase');
    if (e) {
      e.textContent = PHASES_WIFI[data.wifi_phase] || '?';
      e.className = (data.wifi_phase === 2) ? 'output-value high' : 'output-value low';
    }
  }
  if (data.wifi_ip !== undefined || data.wifi_ssid !== undefined) {
    const e = document.getElementById('wifiInfo');
    if (e) e.textContent = `${data.wifi_ssid || '—'} / ${data.wifi_ip || '—'}`;
  }
}

function brancherControles() {
  const brancher = (id, action) => {
    const b = document.getElementById(id);
    if (b) b.addEventListener('click', action);
  };

  brancher('btnModeD', () => {
    if (envoyer({ cmd: 'set_analog_output', enabled: false })) confirmerBouton('btnModeD', '✅ Type D');
  });
  brancher('btnModeP', () => {
    if (envoyer({ cmd: 'set_analog_output', enabled: true })) confirmerBouton('btnModeP', '✅ Type P');
  });
  brancher('btnCapSpan', () => {
    if (envoyer({ cmd: 'capture_span' })) confirmerBouton('btnCapSpan', '✅ Capturé');
  });

  brancher('btnSaveAdv', () => {
    const n = (id) => parseInt(document.getElementById(id).value, 10);
    const cmd = { cmd: 'save_simple_calibration' };
    const z = n('inZone'), h = n('inHyst'), sp = n('inSpan'), a = n('inAlpha');
    if (!isNaN(z))  cmd.deadband_points = z;
    if (!isNaN(h))  cmd.hysteresis = h;
    if (!isNaN(sp)) cmd.span = sp;
    if (!isNaN(a))  cmd.alpha = a;
    if (envoyer(cmd)) confirmerBouton('btnSaveAdv', '✅ Enregistré');
  });

  brancher('btnSaveWifi', () => {
    const ssid = document.getElementById('inSsid').value;
    const pass = document.getElementById('inPass').value;
    if (!ssid) { alert('Renseignez le SSID'); return; }
    if (envoyer({ cmd: 'set_wifi', ssid: ssid, password: pass })) {
      document.getElementById('inPass').value = '';
      confirmerBouton('btnSaveWifi', '✅ Connexion...');
    }
  });
  brancher('btnForgetWifi', () => {
    if (envoyer({ cmd: 'forget_wifi' })) confirmerBouton('btnForgetWifi', '✅ Oublié');
  });
}

document.addEventListener('DOMContentLoaded', brancherControles);
