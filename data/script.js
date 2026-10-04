/* script.js - interface du detecteur de stress filament */

const PHASES_WIFI = ["Inactif", "Connexion...", "Connecté", "Nouvelle tentative", "Point d'accès"];
const ONGLETS = ["mesure", "calibration", "reglages"];
const DUREE_COURBE_MS = 60000;
const TENSION_ALIM = 3.3;   // volts : la sortie filtree vaut rapport cyclique x alimentation

const ERREURS_VERROU = {
  bad_key: 'Clé refusée.',
  retry: 'Trop rapide : réessayez dans une seconde.',
  locked: 'Écriture refusée : réglages verrouillés.',
};

let ws = null;
let protege = false;      // vrai des que le module s'est annonce verrouille sur cette connexion
let verrouille = false;   // sans annonce du module, rien n'est verrouille
let defi = null;          // dernier defi recu
let cle = null;           // cle saisie, gardee en memoire vive pour rouvrir la session apres une reconnexion
let historique = [];   // [instant en ms, position en fraction de la pleine echelle]
let zoneNeutre = 0;    // demi-largeur de la zone neutre, en fraction de la pleine echelle

const el = (id) => document.getElementById(id);
const borner = (v, mini, maxi) => Math.min(maxi, Math.max(mini, v));
const nombre = (v, decimales) => v.toFixed(decimales).replace('.', ',');
const signe = (v) => (v > 0 ? `+${v}` : v < 0 ? `−${-v}` : '0');   // vrai signe moins, pas un trait d'union

// ---------------------------------------------------------------------------
// Onglets
// ---------------------------------------------------------------------------

function afficherOnglet(nom) {
  if (!ONGLETS.includes(nom)) nom = ONGLETS[0];
  ONGLETS.forEach((o) => { el(`onglet-${o}`).hidden = (o !== nom); });
  document.querySelectorAll('.onglet').forEach((b) => {
    b.classList.toggle('actif', b.dataset.onglet === nom);
    b.setAttribute('aria-selected', b.dataset.onglet === nom);
  });
  if (nom === 'mesure') tracerCourbe();
  placerVerrou();
}

function brancherOnglets() {
  document.querySelectorAll('.onglet').forEach((b) => {
    b.addEventListener('click', () => { location.hash = b.dataset.onglet; });
  });
  window.addEventListener('hashchange', () => afficherOnglet(location.hash.slice(1)));
  afficherOnglet(location.hash.slice(1));
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------

function initWebSocket() {
  // Servie par le module, la page s'adresse a son propre hote. Ouverte hors du
  // module (mise au point), l'hote se passe par ?hote=nom-ou-adresse.
  const hote = new URLSearchParams(location.search).get('hote') || location.host;
  const protocole = location.protocol === 'https:' ? 'wss:' : 'ws:';

  ws = new WebSocket(`${protocole}//${hote}/ws`);
  ws.onopen = () => {
    majLiaison(true);
    // Le module annonce l'etat de son verrou a chaque connexion. Un firmware
    // sans verrou n'annonce rien : la page reste alors deverrouillee.
    protege = false;
    majVerrou({ locked: false });
  };
  ws.onmessage = (evenement) => {
    try {
      const data = JSON.parse(evenement.data);
      if (data.type === 'auth') majVerrou(data);
      else if (data.type !== 'log') majInterface(data);
    } catch (e) {
      console.error('Trame illisible :', e.message);
    }
  };
  ws.onclose = () => {
    majLiaison(false);
    setTimeout(initWebSocket, 3000);
  };
  ws.onerror = () => majLiaison(false);
}

function majLiaison(ouverte) {
  el('liaison').classList.toggle('ouverte', ouverte);
  el('liaisonTexte').textContent = ouverte ? 'Connecté' : 'Déconnecté';
}

function envoyer(objet) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(objet));
    return true;
  }
  console.log('WebSocket non connecte');
  return false;
}

// ---------------------------------------------------------------------------
// Verrou des ecritures
// ---------------------------------------------------------------------------

// La carte du verrou n'a rien a faire sur l'onglet Mesure, qui ne fait que lire.
function placerVerrou() {
  el('verrou').hidden = !protege || !el('onglet-mesure').hidden;
}

function majVerrou(data) {
  verrouille = !!data.locked;
  protege = protege || verrouille;
  defi = data.nonce || null;
  if (data.error === 'bad_key') cle = null;

  // Reconnexion : la cle gardee en memoire rouvre la session sans nouvelle saisie.
  if (verrouille && cle && defi && !data.error) {
    repondreAuDefi();
    return;
  }

  const message = data.error ? (ERREURS_VERROU[data.error] || data.error) : '';
  el('formVerrou').hidden = !verrouille;
  el('verrouOuvert').hidden = verrouille;
  el('verrouErreur').hidden = !message;
  el('verrouErreur').textContent = message;
  el('verrouEtat').hidden = !protege;
  el('verrouEtat').textContent = verrouille ? 'Verrouillé' : 'Déverrouillé';
  el('verrouEtat').className = `liaison ${verrouille ? 'ferme' : 'libre'}`;
  document.querySelectorAll('.ecriture').forEach((b) => { b.disabled = verrouille; });
  placerVerrou();
}

// La cle ne quitte pas la page : seule part l'empreinte du defi.
function repondreAuDefi() {
  envoyer({ cmd: 'unlock', response: hmacSha256Hex(cle, defi) });
}

function brancherVerrou() {
  el('formVerrou').addEventListener('submit', (evenement) => {
    evenement.preventDefault();
    if (!el('inCle').value || !defi) return;
    cle = el('inCle').value;
    el('inCle').value = '';
    repondreAuDefi();
  });
  brancher('btnVerrouiller', () => {
    cle = null;
    envoyer({ cmd: 'lock' });
  });
}

// ---------------------------------------------------------------------------
// Affichage
// ---------------------------------------------------------------------------

function majInterface(data) {
  majMesure(data);
  majSorties(data);
  majCalibration(data);
  majReseau(data);
}

function majMesure(data) {
  if (data.delta === undefined || data.span === undefined) return;

  // Position en fraction de la pleine echelle : negative en traction, positive
  // en compression.
  const valide = data.cal_valid !== false && data.span > 0;
  const fraction = valide ? borner(data.delta / data.span, -1, 1) : 0;
  zoneNeutre = valide ? borner((data.neutral_zone || 0) / data.span, 0, 1) : 0;

  const etat = data.position === 1 ? 'compression' : data.position === 2 ? 'traction' : 'neutre';
  el('etat').textContent = { compression: 'Compression', traction: 'Traction', neutre: 'Neutre' }[etat];
  el('etat').className = `etat ${etat}`;

  const pourcent = Math.round(Math.abs(fraction) * 100);
  el('position').textContent = !valide ? 'Calibration absente'
    : pourcent === 0 ? 'Centré'
    : `${fraction < 0 ? 'Traction' : 'Compression'} ${pourcent} %`;

  el('jaugeCurseur').style.left = `${50 + 50 * fraction}%`;
  el('jaugeCurseur').className = `jauge-curseur ${etat}`;
  el('jaugeZone').style.left = `${50 - 50 * zoneNeutre}%`;
  el('jaugeZone').style.width = `${100 * zoneNeutre}%`;

  el('detailMesure').textContent =
    `Écart ${signe(data.delta)} mV · pleine échelle ±${data.span} mV · zone neutre ±${data.neutral_zone} mV`;

  const maintenant = Date.now();
  historique.push([maintenant, fraction]);
  while (historique.length && historique[0][0] < maintenant - DUREE_COURBE_MS) historique.shift();
  if (!el('onglet-mesure').hidden) tracerCourbe();
}

function majSorties(data) {
  if (data.analog_out !== undefined) {
    el('modeSortie').textContent = data.analog_out ? 'Type P (analogique)' : 'Type D (tout ou rien)';
    el('btnModeP').classList.toggle('actif', data.analog_out);
    el('btnModeD').classList.toggle('actif', !data.analog_out);
    // GPIO 26 ne sert qu'en type D : en type P la ligne n'a rien a montrer.
    el('ligne26').hidden = data.analog_out;
  }
  const niveau = (haut) => (haut ? 'Haut' : 'Bas');

  // En type P, GPIO 25 porte un PWM filtre et non un niveau logique.
  if (data.analog_out && data.pwm !== undefined && data.pwm_full) {
    const rapport = data.pwm / data.pwm_full;
    el('sortie25').textContent = `PWM ${nombre(100 * rapport, 1)} % · ≈ ${nombre(TENSION_ALIM * rapport, 2)} V`;
  } else if (data.output2 !== undefined) {
    el('sortie25').textContent = niveau(data.output2);
  }
  if (data.output1 !== undefined) el('sortie26').textContent = niveau(data.output1);

  const capteur = (idValeur, idEcart, brut, neutre) => {
    if (brut === undefined) return;
    el(idValeur).textContent = `${brut} mV`;
    if (neutre !== undefined) {
      el(idEcart).textContent = `(${signe(brut - neutre)})`;
    }
  };
  capteur('raw1', 'ecart1', data.raw1, data.cal_neutral_raw1);
  capteur('raw2', 'ecart2', data.raw2, data.cal_neutral_raw2);
}

// Les champs de saisie ne sont reecrits par le module que lorsqu'ils n'ont pas
// le focus : sinon la valeur serait ecrasee pendant la frappe, a chaque
// rafraichissement.
function majChamp(id, valeur) {
  const e = el(id);
  if (e && document.activeElement !== e && valeur !== undefined) e.value = valeur;
}

function majCalibration(data) {
  if (data.cal_valid !== undefined) {
    el('calValide').textContent = data.cal_valid ? 'Valide' : 'Absente';
    el('calValide').className = `pastille ${data.cal_valid ? 'ok' : 'alerte'}`;
  }
  if (data.cal_neutral_raw1 !== undefined) el('capN1').textContent = `${data.cal_neutral_raw1} mV`;
  if (data.cal_neutral_raw2 !== undefined) el('capN2').textContent = `${data.cal_neutral_raw2} mV`;
  if (data.span !== undefined) el('capSpan').textContent = `±${data.span} mV`;

  majChamp('inZone',  data.neutral_zone);
  majChamp('inHyst',  data.hysteresis);
  majChamp('inSpan',  data.span);
  majChamp('inAlpha', data.alpha);
}

function majReseau(data) {
  if (data.wifi_phase !== undefined) {
    el('wifiPhase').textContent = PHASES_WIFI[data.wifi_phase] || '?';
    el('wifiPhase').className = `pastille ${data.wifi_phase === 2 ? 'ok' : ''}`;
  }
  if (data.wifi_ssid !== undefined) el('wifiSsid').textContent = data.wifi_ssid || '—';
  if (data.wifi_ip !== undefined) el('wifiIp').textContent = data.wifi_ip || '—';

  if (data.wifi_rssi !== undefined) {
    // Le module renvoie 0 tant qu'il n'est pas associe a un reseau.
    const rssi = data.wifi_rssi;
    const barres = rssi === 0 ? 0 : rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -75 ? 2 : 1;
    const qualite = ['', 'faible', 'moyen', 'bon', 'excellent'][barres];
    el('wifiBarres').querySelectorAll('i').forEach((b, i) => b.classList.toggle('actif', i < barres));
    el('wifiSignal').textContent = rssi === 0 ? '—' : `${signe(rssi)} dBm · ${qualite}`;
  }
}

// ---------------------------------------------------------------------------
// Courbe de la derniere minute
// ---------------------------------------------------------------------------

function tracerCourbe() {
  const toile = el('courbe');
  const largeur = toile.clientWidth, hauteur = toile.clientHeight;
  if (!largeur || !hauteur) return;

  const densite = window.devicePixelRatio || 1;
  if (toile.width !== largeur * densite || toile.height !== hauteur * densite) {
    toile.width = largeur * densite;
    toile.height = hauteur * densite;
  }
  const c = toile.getContext('2d');
  c.setTransform(densite, 0, 0, densite, 0, 0);
  c.clearRect(0, 0, largeur, hauteur);

  const couleur = (nom) => getComputedStyle(document.documentElement).getPropertyValue(nom).trim();
  const y = (fraction) => hauteur / 2 - fraction * (hauteur / 2 - 6);   // compression vers le haut
  const maintenant = Date.now();
  const x = (instant) => largeur * (1 - (maintenant - instant) / DUREE_COURBE_MS);

  // Zone neutre, puis axe du milieu
  c.globalAlpha = 0.18;
  c.fillStyle = couleur('--neutre');
  c.fillRect(0, y(zoneNeutre), largeur, y(-zoneNeutre) - y(zoneNeutre));
  c.globalAlpha = 1;
  c.strokeStyle = couleur('--bord');
  c.lineWidth = 1;
  c.beginPath();
  c.moveTo(0, y(0));
  c.lineTo(largeur, y(0));
  c.stroke();

  c.fillStyle = couleur('--discret');
  c.font = '11px system-ui, sans-serif';
  c.textBaseline = 'top';
  c.fillText('Compression', 0, 0);
  c.textBaseline = 'bottom';
  c.fillText('Traction', 0, hauteur);

  if (historique.length < 2) return;
  c.strokeStyle = couleur('--accent');
  c.lineWidth = 2;
  c.lineJoin = 'round';
  c.beginPath();
  historique.forEach(([instant, fraction], i) => {
    if (i === 0) c.moveTo(x(instant), y(fraction));
    else c.lineTo(x(instant), y(fraction));
  });
  c.stroke();
}

// ---------------------------------------------------------------------------
// Commandes
// ---------------------------------------------------------------------------

function signalerEnvoi(bouton, texte) {
  const initial = bouton.textContent;
  bouton.textContent = texte;
  bouton.classList.add('fait');
  setTimeout(() => { bouton.textContent = initial; bouton.classList.remove('fait'); }, 2000);
}

function brancher(id, action) {
  const b = el(id);
  if (b) b.addEventListener('click', () => action(b));
}

// Les commandes qui modifient la calibration ou la sortie demandent un second
// clic : un appui involontaire pendant une impression fausserait le retour
// vers Happy Hare.
function brancherAvecConfirmation(id, action) {
  const b = el(id);
  if (!b) return;
  const initial = b.textContent;
  let delai = null;
  const desarmer = () => {
    clearTimeout(delai);
    delai = null;
    b.classList.remove('arme');
    b.textContent = initial;
  };
  b.addEventListener('click', () => {
    if (b.classList.contains('actif') || b.classList.contains('fait')) return;
    if (delai) {
      desarmer();
      action(b);
      return;
    }
    b.classList.add('arme');
    b.textContent = 'Confirmer ?';
    delai = setTimeout(desarmer, 3000);
  });
}

function brancherControles() {
  brancherAvecConfirmation('btnCapNeutre', (b) => {
    if (envoyer({ cmd: 'capture_neutral' })) signalerEnvoi(b, 'Neutre capturé');
  });
  brancherAvecConfirmation('btnCapSpan', (b) => {
    if (envoyer({ cmd: 'capture_span' })) signalerEnvoi(b, 'Pleine échelle capturée');
  });
  brancherAvecConfirmation('btnModeP', () => envoyer({ cmd: 'set_analog_output', enabled: true }));
  brancherAvecConfirmation('btnModeD', () => envoyer({ cmd: 'set_analog_output', enabled: false }));
  brancherAvecConfirmation('btnReboot', (b) => {
    if (envoyer({ cmd: 'reboot' })) signalerEnvoi(b, 'Redémarrage...');
  });
  brancherAvecConfirmation('btnForgetWifi', (b) => {
    if (envoyer({ cmd: 'forget_wifi' })) signalerEnvoi(b, 'Réseau oublié');
  });

  brancher('btnSaveAdv', (b) => {
    const n = (id) => parseInt(el(id).value, 10);
    const cmd = { cmd: 'save_simple_calibration' };
    const z = n('inZone'), h = n('inHyst'), sp = n('inSpan'), a = n('inAlpha');
    if (!isNaN(z))  cmd.deadband_points = z;
    if (!isNaN(h))  cmd.hysteresis = h;
    if (!isNaN(sp)) cmd.span = sp;
    if (!isNaN(a))  cmd.alpha = a;
    if (envoyer(cmd)) signalerEnvoi(b, 'Enregistré');
  });

  brancher('btnSaveWifi', (b) => {
    const ssid = el('inSsid').value;
    const pass = el('inPass').value;
    if (!ssid) { alert('Renseignez le SSID'); return; }
    if (envoyer({ cmd: 'set_wifi', ssid: ssid, password: pass })) {
      el('inPass').value = '';
      signalerEnvoi(b, 'Connexion...');
    }
  });
}

document.addEventListener('DOMContentLoaded', () => {
  brancherOnglets();
  brancherVerrou();
  brancherControles();
  initWebSocket();
  window.addEventListener('resize', tracerCourbe);
});
