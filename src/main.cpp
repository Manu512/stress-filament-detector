#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <ArduinoOTA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ESPmDNS.h>

// Configuration WiFi (credentials dans config_private.h)
#include "config_private.h"

// Pins du hardware
#define SENSOR_1_PIN 32
#define SENSOR_2_PIN 33
#define OUTPUT_1_PIN 26
#define OUTPUT_2_PIN 25

// Variables POC Arduino (logique simplifiée avec priorités)
int sensor1_threshold = 2015; // seuil S1 (GPIO32) - optimisé selon logs
int sensor2_threshold = 2005; // seuil S2 (GPIO33) - optimisé selon logs  
int neutral_zone = 8;         // hystérésis plus large pour stabilité
int cal_neutral_raw1 = 2000;  // point neutre capteur 1 observé
int cal_neutral_raw2 = 1993;  // point neutre capteur 2 observé

// États de position basés sur le POC Arduino
enum Position { POS_NEUTRAL, POS_SENSOR1, POS_SENSOR2 };
Position currentPosition = POS_NEUTRAL;

// Variables de contrôle
unsigned long last_output_update = 0;
unsigned long output_update_interval = 50; // 20Hz comme le POC (50ms)
bool cal_valid = false;

// WiFi et serveur web
// Credentials définis dans config_private.h
const char* hostname = "voron-hall-controller";
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences prefs;

// Types de logs pour la console
enum LogCategory { CAT_ALL = 0, CAT_VOLTAGE = 1, CAT_RAW = 2, CAT_OUTPUT = 3, CAT_DBG = 4 };
LogCategory consoleFilterCategory = CAT_ALL;

// Variables d'envoi des updates
bool sendUpdates = true;
unsigned long lastUpdateTime = 0;
unsigned long updateIntervalMs = 200;

void logToClients(const String &msg, LogCategory cat = CAT_DBG) {
  // Category filtering
  if (consoleFilterCategory != CAT_ALL && cat != consoleFilterCategory) return;
  
  JsonDocument d;
  d["type"] = "log";
  d["category"] = (int)cat;
  d["msg"] = msg;
  String output;
  serializeJson(d, output);
  ws.textAll(output);
}

void controlOutputs() {
  // Lecture des capteurs avec échantillonnage simple comme dans le POC
  unsigned long s1 = 0;
  unsigned long s2 = 0;
  for (byte t = 0; t < 10; t++) {
    s1 += analogRead(SENSOR_1_PIN);
    s2 += analogRead(SENSOR_2_PIN);
    delay(2);
  }
  int cval1 = s1 / 10;  // Moyenne sur 10 échantillons
  int cval2 = s2 / 10;
  
  // Logique différentielle avec zone neutre : S1 vs S2 pour détecter compression/tension
  Position newPosition = currentPosition;
  
  // Calculer les zones neutres
  int s1_neutre_min = cal_neutral_raw1 - neutral_zone;
  int s1_neutre_max = cal_neutral_raw1 + neutral_zone;
  int s2_neutre_min = cal_neutral_raw2 - neutral_zone;
  int s2_neutre_max = cal_neutral_raw2 + neutral_zone;
  
  // COMPRESSION : S1 augmente (sort de la zone neutre par le haut) ET S2 diminue (sort par le bas)
  if (cval1 > s1_neutre_max && cval2 < s2_neutre_min) {
    newPosition = POS_SENSOR1; // Position 1 = COMPRESSION
  }
  // TENSION : S2 augmente (sort de la zone neutre par le haut) ET S1 diminue (sort par le bas)
  else if (cval2 > s2_neutre_max && cval1 < s1_neutre_min) {
    newPosition = POS_SENSOR2; // Position 2 = TENSION
  }
  else {
    // Si les deux capteurs sont dans leur zone neutre ou en position ambiguë
    if ((cval1 >= s1_neutre_min && cval1 <= s1_neutre_max) && 
        (cval2 >= s2_neutre_min && cval2 <= s2_neutre_max)) {
      newPosition = POS_NEUTRAL; // Les deux dans la zone neutre
    }
    // Sinon maintenir la position actuelle (hystérésis)
  }
  
  // Limitation de la fréquence de mise à jour des sorties (comme dans le POC)
  unsigned long now = millis();
  if (now - last_output_update >= output_update_interval) {
    last_output_update = now;
    
    // Mettre à jour les sorties si la position change
    if (newPosition != currentPosition) {
      currentPosition = newPosition;
      
      switch (currentPosition) {
        case POS_SENSOR1:
          digitalWrite(OUTPUT_1_PIN, HIGH);  // OUT1 = HIGH (COMPRESSION)
          digitalWrite(OUTPUT_2_PIN, LOW);   // OUT2 = LOW
          logToClients("Position: COMPRESSION (OUT1=HIGH)", CAT_OUTPUT);
          break;
          
        case POS_SENSOR2:
          digitalWrite(OUTPUT_1_PIN, LOW);   // OUT1 = LOW
          digitalWrite(OUTPUT_2_PIN, HIGH);  // OUT2 = HIGH (TENSION)
          logToClients("Position: TENSION (OUT2=HIGH)", CAT_OUTPUT);
          break;
          
        case POS_NEUTRAL:
          digitalWrite(OUTPUT_1_PIN, LOW);   // OUT1 = LOW
          digitalWrite(OUTPUT_2_PIN, LOW);   // OUT2 = LOW
          logToClients("Position: NEUTRAL (OUT1=LOW, OUT2=LOW)", CAT_OUTPUT);
          break;
      }
    }
  }
}

void notifyClients() {
  if (!sendUpdates) return;
  
  unsigned long now = millis();
  if (now - lastUpdateTime < updateIntervalMs) return;
  lastUpdateTime = now;
  
  JsonDocument doc;
  doc["type"] = "data";
  
  // Valeurs des capteurs
  doc["raw1"] = analogRead(SENSOR_1_PIN);
  doc["raw2"] = analogRead(SENSOR_2_PIN);
  
  // États des sorties
  doc["output1"] = digitalRead(OUTPUT_1_PIN);
  doc["output2"] = digitalRead(OUTPUT_2_PIN);
  
  // Seuils actuels
  doc["sensor1_threshold"] = sensor1_threshold;
  doc["sensor2_threshold"] = sensor2_threshold;
  doc["neutral_zone"] = neutral_zone;
  
  // Status de calibration
  doc["cal_valid"] = cal_valid;
  doc["cal_neutral_raw1"] = cal_neutral_raw1;
  doc["cal_neutral_raw2"] = cal_neutral_raw2;
  
  // Position actuelle
  doc["position"] = (int)currentPosition;
  
  String output;
  serializeJson(doc, output);
  ws.textAll(output);
}

void handleWebSocket(AsyncWebSocket *server, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WS: client %u connecté\n", client->id());
    logToClients(String("WS: client ") + String(client->id()) + " connecté", CAT_OUTPUT);
    notifyClients();
    return;
  }

  if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WS: client %u déconnecté\n", client->id());
    logToClients(String("WS: client ") + String(client->id()) + " déconnecté", CAT_OUTPUT);
    return;
  }

  if (type == WS_EVT_DATA) {
    String msg;
    for (size_t i = 0; i < len; ++i) msg += (char)data[i];

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, msg);
    if (err) {
      Serial.print("JSON parse error: ");
      Serial.println(err.c_str());
      
      JsonDocument resp;
      resp["type"] = "error";
      resp["msg"] = String("JSON parse error: ") + err.c_str();
      String respStr;
      serializeJson(resp, respStr);
      client->text(respStr);
      return;
    }

    bool changed = false;

    // Handler pour capturer le point neutre
    if (doc.containsKey("cmd")) {
      const char *cmd = doc["cmd"];
      
      if (strcmp(cmd, "capture_neutral") == 0) {
        int raw1 = analogRead(SENSOR_1_PIN);
        int raw2 = analogRead(SENSOR_2_PIN);
        cal_neutral_raw1 = raw1;
        cal_neutral_raw2 = raw2;
        prefs.putInt("cal_raw1", cal_neutral_raw1);
        prefs.putInt("cal_raw2", cal_neutral_raw2);
        logToClients(String("Neutre capturé: Raw1=") + String(cal_neutral_raw1) + ", Raw2=" + String(cal_neutral_raw2), CAT_RAW);
        changed = true;
      }
      
      else if (strcmp(cmd, "save_simple_calibration") == 0) {
        if (doc.containsKey("deadband_points")) {
          int deadband = doc["deadband_points"].as<int>();
          if (deadband < 5) deadband = 5;
          if (deadband > 50) deadband = 50;
          
          // Adapter les seuils comme dans le POC avec les valeurs mesurées
          sensor1_threshold = cal_neutral_raw1 + deadband; // TENSION (S1)
          sensor2_threshold = cal_neutral_raw2 + deadband; // COMPRESSION (S2)
          neutral_zone = deadband / 4; // Hystérésis proportionnelle
          
          prefs.putInt("s1_thresh", sensor1_threshold);
          prefs.putInt("s2_thresh", sensor2_threshold);
          prefs.putInt("n_zone", neutral_zone);
        }
        
        cal_valid = true;
        prefs.putBool("cal_valid", cal_valid);
        
        logToClients(String("Calibration POC sauvée: S1=") + String(sensor1_threshold) + 
                     ", S2=" + String(sensor2_threshold) + 
                     ", hysteresis=" + String(neutral_zone), CAT_DBG);
        changed = true;
      }
      
      else if (strcmp(cmd, "reset_calibration") == 0) {
        cal_valid = false;
        sensor1_threshold = 2010;
        sensor2_threshold = 2030;
        neutral_zone = 5;
        prefs.putBool("cal_valid", cal_valid);
        prefs.putInt("s1_thresh", sensor1_threshold);
        prefs.putInt("s2_thresh", sensor2_threshold);
        prefs.putInt("n_zone", neutral_zone);
        logToClients("Calibration reset", CAT_DBG);
        changed = true;
      }
    }

    // Configuration du debug et des updates
    if (doc.containsKey("send_updates")) {
      sendUpdates = doc["send_updates"].as<bool>();
      changed = true;
    }

    if (doc.containsKey("interval_ms")) {
      unsigned long v = doc["interval_ms"].as<unsigned long>();
      if (v >= 50 && v <= 60000) updateIntervalMs = v;
      changed = true;
    }

    if (changed) notifyClients();
  }
}

void setupHardware() {
  pinMode(SENSOR_1_PIN, INPUT);
  pinMode(SENSOR_2_PIN, INPUT);
  pinMode(OUTPUT_1_PIN, OUTPUT);
  pinMode(OUTPUT_2_PIN, OUTPUT);
  
  digitalWrite(OUTPUT_1_PIN, LOW);
  digitalWrite(OUTPUT_2_PIN, LOW);
  
  // Chargement des préférences (NVS reset retiré pour la version finale)
  prefs.begin("hall", false);
  sensor1_threshold = prefs.getInt("s1_thresh", sensor1_threshold);
  sensor2_threshold = prefs.getInt("s2_thresh", sensor2_threshold);
  neutral_zone = prefs.getInt("n_zone", neutral_zone);
  cal_neutral_raw1 = prefs.getInt("cal_raw1", cal_neutral_raw1);
  cal_neutral_raw2 = prefs.getInt("cal_raw2", cal_neutral_raw2);
  cal_valid = prefs.getBool("cal_valid", cal_valid);
  
  Serial.println("Configuration POC chargée (RESET NVS):");
  Serial.printf("  Seuil S1: %d\n", sensor1_threshold);
  Serial.printf("  Seuil S2: %d\n", sensor2_threshold);
  Serial.printf("  Zone neutre: %d\n", neutral_zone);
  Serial.printf("  Calibration valide: %s\n", cal_valid ? "OUI" : "NON");
}

void setupWeb() {
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  ws.onEvent(handleWebSocket);
  server.addHandler(&ws);

  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/plain", "ok");
  });

  server.begin();
  Serial.println("Serveur web démarré");
}

void setupWiFi() {
  WiFi.setHostname(hostname);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.print("Connexion au réseau WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  
  IPAddress IP = WiFi.localIP();
  Serial.print("Connecté au WiFi avec l'IP: ");
  Serial.println(IP);
  Serial.print("Hostname: ");
  Serial.println(hostname);
  
  // Démarrer mDNS
  if (MDNS.begin(hostname)) {
    Serial.println("mDNS démarré avec succès");
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("Erreur lors du démarrage mDNS");
  }
  
  Serial.println("Connexion à l'interface web: http://" + IP.toString() + " ou http://" + String(hostname) + ".local");
}

void setupOTA() {
  ArduinoOTA.setHostname("stress-filament");
  ArduinoOTA.setPassword(ota_password); // Défini dans config_private.h
  
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  
  ArduinoOTA.begin();
  Serial.println("OTA ready");
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Détecteur Stress Filament - Version POC ===");
  
  if (!LittleFS.begin()) {
    Serial.println("Erreur LittleFS.begin()");
  }
  
  setupHardware();
  setupWiFi();
  setupWeb();
  setupOTA();
  
  Serial.println("=== Système prêt ===");
  Serial.println("Déplacez l'aimant sur le slider pour tester");
}

void loop() {
  ArduinoOTA.handle();
  controlOutputs();
  notifyClients();
  
  // Debug périodique avec zones neutres
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 2000) { // Debug toutes les 2 secondes
    lastDebug = millis();
    int cval1 = analogRead(SENSOR_1_PIN);
    int cval2 = analogRead(SENSOR_2_PIN);
    
    // Calculer les zones pour affichage
    int s1_min = cal_neutral_raw1 - neutral_zone;
    int s1_max = cal_neutral_raw1 + neutral_zone;
    int s2_min = cal_neutral_raw2 - neutral_zone;  
    int s2_max = cal_neutral_raw2 + neutral_zone;
    
    Serial.printf("Debug: S1=%d[%d-%d] S2=%d[%d-%d] Pos=%d OUT1=%d OUT2=%d\n", 
                  cval1, s1_min, s1_max, cval2, s2_min, s2_max,
                  (int)currentPosition, digitalRead(OUTPUT_1_PIN), digitalRead(OUTPUT_2_PIN));
                  
    // Status des zones
    String s1_status = (cval1 < s1_min) ? "BAS" : (cval1 > s1_max) ? "HAUT" : "NEUTRE";
    String s2_status = (cval2 < s2_min) ? "BAS" : (cval2 > s2_max) ? "HAUT" : "NEUTRE";
    Serial.printf("       Zones: S1=%s S2=%s\n", s1_status.c_str(), s2_status.c_str());
  }
  
  delay(20); // ~50Hz loop rate
}