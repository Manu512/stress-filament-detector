/* sha256.js - SHA-256 et HMAC-SHA256 en JavaScript pur.
 *
 * Le navigateur ne donne acces a crypto.subtle que sur une page servie en
 * HTTPS, ce que le module ne fait pas : d'ou cette implementation, limitee a
 * ce dont le deverrouillage a besoin.
 */

const SHA256_K = new Uint32Array([
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
]);

const rotr = (x, n) => (x >>> n) | (x << (32 - n));

// Empreinte SHA-256 d'un Uint8Array, rendue en Uint8Array de 32 octets.
function sha256(octets) {
  const h = new Uint32Array([
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  ]);

  // Bourrage : un bit a 1, des zeros, puis la longueur en bits sur 64 bits.
  const taille = Math.ceil((octets.length + 9) / 64) * 64;
  const blocs = new Uint8Array(taille);
  blocs.set(octets);
  blocs[octets.length] = 0x80;
  const vue = new DataView(blocs.buffer);
  vue.setUint32(taille - 8, Math.floor(octets.length / 0x20000000));
  vue.setUint32(taille - 4, (octets.length << 3) >>> 0);

  const w = new Uint32Array(64);   // un Uint32Array tronque de lui-meme a 32 bits
  for (let debut = 0; debut < taille; debut += 64) {
    for (let t = 0; t < 16; t++) w[t] = vue.getUint32(debut + 4 * t);
    for (let t = 16; t < 64; t++) {
      const s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >>> 3);
      const s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >>> 10);
      w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }

    let [a, b, c, d, e, f, g, i] = h;
    for (let t = 0; t < 64; t++) {
      const t1 = i + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + SHA256_K[t] + w[t];
      const t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      i = g;
      g = f;
      f = e;
      e = (d + t1) >>> 0;
      d = c;
      c = b;
      b = a;
      a = (t1 + t2) >>> 0;
    }
    [a, b, c, d, e, f, g, i].forEach((valeur, k) => { h[k] += valeur; });
  }

  const empreinte = new Uint8Array(32);
  const vueEmpreinte = new DataView(empreinte.buffer);
  h.forEach((valeur, k) => vueEmpreinte.setUint32(4 * k, valeur));
  return empreinte;
}

// HMAC-SHA256 (RFC 2104) sur des Uint8Array.
function hmacSha256(cle, message) {
  if (cle.length > 64) cle = sha256(cle);
  const interne = new Uint8Array(64 + message.length);
  const externe = new Uint8Array(64 + 32);
  for (let k = 0; k < 64; k++) {
    const octet = cle[k] || 0;
    interne[k] = octet ^ 0x36;
    externe[k] = octet ^ 0x5c;
  }
  interne.set(message, 64);
  externe.set(sha256(interne), 64);
  return sha256(externe);
}

// Reponse attendue par le module : HMAC-SHA256(cle, defi), en hexadecimal.
function hmacSha256Hex(cle, message) {
  const octets = (texte) => new TextEncoder().encode(texte);
  return Array.from(hmacSha256(octets(cle), octets(message)), (o) => o.toString(16).padStart(2, '0')).join('');
}

// Pour verifier ce fichier hors navigateur (node) ; sans effet dans la page.
if (typeof module !== 'undefined') module.exports = { sha256, hmacSha256, hmacSha256Hex };
