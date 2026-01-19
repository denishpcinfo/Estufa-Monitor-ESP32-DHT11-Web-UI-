#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

// ===================== DHT =====================
#define DHTPIN 13
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ===================== WiFi =====================
const char* SSID     = "";
const char* PASSWORD = "";

// ===================== Telegram =====================
const char* BOT_TOKEN = "";
const char* CHAT_ID   = "";
WiFiClientSecure tgClient;

// ===================== Pins =====================
const int sensorNivelAlto   = 34;
const int sensorUmidadeSolo = 35; (ADC1) - analógico

const int releBomba      = 14;
const int releVentilador = 27;
const int torneiraFechar = 26;
const int torneiraAbrir  = 25;
const int releExaustor   = 33;
const int luz1           = 32;

// Active-low flags
const bool ACT_LOW_bomba      = true;
const bool ACT_LOW_ventilador = true;
const bool ACT_LOW_valvFechar = true;
const bool ACT_LOW_valvAbrir  = true;
const bool ACT_LOW_exaustor   = true;
const bool ACT_LOW_luz        = false;

// ===================== Limites =====================
const float temperaturaMaxima = 25.0;
const float umidadeLimite     = 30.0;

// Luz 18/6
const unsigned long tempoLuzOn  = 18UL * 60UL * 60UL * 1000UL;
const unsigned long tempoLuzOff =  6UL * 60UL * 60UL * 1000UL;

// ===================== SOLO (calibração + leitura) =====================
int SOLO_SECO    = 1427; // seu RAW no seco
int SOLO_MOLHADO = 1050; // seu RAW no molhado
const int SOLO_SAMPLES = 20;

// ===================== Irrigação automática por solo =====================
const bool IRRIG_SOLO_AUTO = true;
const int  SOLO_LIMIAR_LIGA_PCT    = 35;
const int  SOLO_LIMIAR_DESARMA_PCT = 50;
const unsigned long MIN_INTERVALO_IRRIG_SOLO_MS = 6UL * 60UL * 60UL * 1000UL; // 6h

// Bomba ligada no máximo 6s
const unsigned long IRRIG_MAX_MS = 6000UL; // 6s

// ===================== Nível (debounce) =====================
bool nivelBaixoState = false;
unsigned long lastNivelCheckMs = 0;
const unsigned long NIVEL_DEBOUNCE_MS = 1500; // 1.5s

// ===================== Estados =====================
bool irrigando = false;
unsigned long inicioIrrigacaoMs = 0;

unsigned long tempoUltimaTrocaLuz = 0;
bool luzLigada = true;

bool exaustorLigado = true;

bool soloAbaixoDoLimite = false;
unsigned long ultimaIrrigacaoPorSoloMs = 0;

// ===================== Web =====================
WebServer uiServer(81);

// ===================== Leituras cacheadas =====================
float  lastTemp = NAN;
float  lastHum  = NAN;
int    lastNivel = 0;
int    lastSoloRaw = 0;
int    lastSoloPct = 0;

unsigned long lastSensorsAt = 0;

// ===================== Helpers relé =====================
inline void setOn(int pin, bool activeLow)  { digitalWrite(pin, activeLow ? LOW : HIGH); }
inline void setOff(int pin, bool activeLow) { digitalWrite(pin, activeLow ? HIGH : LOW); }

inline bool isOn(int pin, bool activeLow) {
  int v = digitalRead(pin);
  return activeLow ? (v == LOW) : (v == HIGH);
}

// ===================== Solo (não-bloqueante) =====================
int readSoilRawAvg() {
  static int samples[SOLO_SAMPLES];
  static int idx = 0;
  static bool filled = false;

  int v = analogRead(sensorUmidadeSolo);

  // RAW inválido → reseta média
  if (v <= 0 || v >= 4095) {
    idx = 0;
    filled = false;
    return -1;
  }

  samples[idx++] = v;
  if (idx >= SOLO_SAMPLES) {
    idx = 0;
    filled = true;
  }

  if (!filled) return -1;

  long sum = 0;
  for (int i = 0; i < SOLO_SAMPLES; i++) sum += samples[i];
  return sum / SOLO_SAMPLES;
}


int rawToSoilPercent(int raw) {
  int seco = SOLO_SECO;
  int molhado = SOLO_MOLHADO;
  if (seco < molhado) {
    int t = seco; seco = molhado; molhado = t;
  }

  raw = constrain(raw, molhado, seco);
  int pct = map(raw, seco, molhado, 0, 100);
  return constrain(pct, 0, 100);
}


// ===================== Telegram =====================
bool sendTelegramMessage(const String& text) {
  tgClient.setInsecure();
  if (!tgClient.connect("api.telegram.org", 443)) return false;

  String msg = text;
  msg.replace(" ", "%20");

  String url = "/bot" + String(BOT_TOKEN)
             + "/sendMessage?chat_id=" + String(CHAT_ID)
             + "&text=" + msg;

  tgClient.print(
    "GET " + url + " HTTP/1.1\r\n"
    "Host: api.telegram.org\r\n"
    "Connection: close\r\n\r\n"
  );

  tgClient.stop();
  return true;
}

// ===================== WiFi events =====================
void WiFiEventHandler(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println(F("[WiFi] STA_START"));
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println(F("[WiFi] Conectado ao AP (sem IP ainda)"));
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print(F("[WiFi] IP obtido: "));
      Serial.println(WiFi.localIP());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println(F("[WiFi] Desconectado do AP, tentando reconectar..."));
      break;
    default:
      break;
  }
}

// ===================== Persistência (SÓ LUZ - CONGELADO) =====================
Preferences prefs;

struct LightSnap {
  uint32_t version = 2;
  bool luzLigada = true;
  uint32_t restanteMs = 0;
};

LightSnap lightSnap;

void applyLightState(bool ligar) {
  if (ligar) setOn(luz1, ACT_LOW_luz);
  else       setOff(luz1, ACT_LOW_luz);
  luzLigada = ligar;
}

void saveLightSnap() {
  prefs.putBytes("lsnap", &lightSnap, sizeof(lightSnap));
}

void saveRemainingLight() {
  unsigned long now = millis();
  unsigned long elapsed = now - tempoUltimaTrocaLuz;
  unsigned long period  = luzLigada ? tempoLuzOn : tempoLuzOff;
  unsigned long nextMs  = (elapsed < period) ? (period - elapsed) : 0;

  lightSnap.luzLigada  = luzLigada;
  lightSnap.restanteMs = (uint32_t)nextMs;
  saveLightSnap();
}

void restoreLightFrozenFromSnap() {
  applyLightState(lightSnap.luzLigada);

  unsigned long period = luzLigada ? tempoLuzOn : tempoLuzOff;
  unsigned long restante = lightSnap.restanteMs;

  if (restante == 0 || restante > period) restante = period;

  tempoUltimaTrocaLuz = millis() - (period - restante);
}

void trocaLuzPersist(bool ligar) {
  applyLightState(ligar);
  tempoUltimaTrocaLuz = millis();

  lightSnap.luzLigada  = luzLigada;
  lightSnap.restanteMs = (uint32_t)(luzLigada ? tempoLuzOn : tempoLuzOff);
  saveLightSnap();
}

// ===================== Leitura de sensores =====================
void updateSensors() {
  unsigned long now = millis();
  if (now - lastSensorsAt < 1000) return; // 1s
  lastSensorsAt = now;

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  lastTemp = isnan(t) ? lastTemp : t;
  lastHum  = isnan(h) ? lastHum  : h;

  lastNivel = digitalRead(sensorNivelAlto);

  bool nivelBaixo = (lastNivel == HIGH);
  if (nivelBaixo) return;

  int raw = readSoilRawAvg();
  if (raw >= 0) {
    lastSoloRaw = raw;
    lastSoloPct = rawToSoilPercent(lastSoloRaw);
  }
}

// ===================== UI =====================
const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Grow Monitor</title>
<style>
body{font-family:system-ui,Arial;margin:18px;background:#0b1324;color:#e8eef9}
.card{background:#121b33;border:1px solid #25304d;border-radius:14px;padding:16px;margin:12px 0}
h1{font-size:20px;margin:0 0 12px}
.grid{display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(220px,1fr))}
.kv{display:flex;justify-content:space-between;border-bottom:1px dashed #2a3a63;padding:6px 0}
.badge{padding:2px 8px;border-radius:999px;background:#1f2a4d}
.ok{background:#234b2a} .warn{background:#4d2a23}
</style></head>
<body>
<h1>🌱 Grow Monitor</h1>

<div class="grid">
 <div class="card"><h1>Sensores</h1>
  <div class="kv"><span>Temperatura</span><span id="t">--</span></div>
  <div class="kv"><span>Umidade do ar</span><span id="h">--</span></div>
  <div class="kv"><span>Nível d'água</span><span id="n">--</span></div>
  <div class="kv"><span>Umidade do solo</span><span id="s">--</span></div>
  <div class="kv"><span>Solo RAW</span><span id="sr">--</span></div>
 </div>

 <div class="card"><h1>Relés</h1>
  <div class="kv"><span>Luz</span><span id="rluz"  class="badge">--</span></div>
  <div class="kv"><span>Bomba d'água</span><span id="rbom"  class="badge">--</span></div>
  <div class="kv"><span>Ventilador</span><span id="rvent" class="badge">--</span></div>
  <div class="kv"><span>Torneira</span><span id="rvf" class="badge">--</span></div>
  <div class="kv"><span>Exaustor</span><span id="rexa" class="badge">--</span></div>
  <div class="kv"><span>Irrigando agora?</span><span id="ir" class="badge">--</span></div>
 </div>

 <div class="card"><h1>Ciclo Luz</h1>
  <div class="kv"><span>Próx. troca da luz</span><span id="nextL">--</span></div>
 </div>
</div>

<script>
async function load(){
  const r = await fetch('/status.json'); const j = await r.json();
  const on = v=> v?'ON':'OFF';
  const pad = ms=>{
    const s=Math.max(0,Math.floor(ms/1000));
    const h=Math.floor(s/3600), m=Math.floor((s%3600)/60);
    return `${h}h ${m}m`;
  };

  document.getElementById('t').textContent  = `${j.temp?.toFixed?.(1) ?? '--'} °C`;
  document.getElementById('h').textContent  = `${j.umid?.toFixed?.(0) ?? '--'} %`;
  document.getElementById('n').textContent  = j.nivel ? 'BAIXO' : 'CHEIO';
  document.getElementById('s').textContent  = `${j.soloPct ?? '--'} %`;
  document.getElementById('sr').textContent = `${j.soloRaw ?? '--'}`;

  const set=(id,val)=>{ const el=document.getElementById(id); el.textContent=on(val); el.className='badge '+(val?'ok':'warn'); };
  set('rluz',  j.relays.luz);
  set('rbom',  j.relays.bomba);
  set('rvent', j.relays.vent);
  set('rvf',   j.relays.valvula);
  set('rexa',  j.relays.exaustor);
  set('ir',    j.irrigando);

  document.getElementById('nextL').textContent = pad(j.next.lightMs);
}
load(); setInterval(load, 1000);
</script>
</body></html>
)HTML";

void handleRoot(){ uiServer.send_P(200, "text/html", PAGE); }

void handleStatus() {
  unsigned long now = millis();
  unsigned long elapsedLight = now - tempoUltimaTrocaLuz;
  unsigned long period = luzLigada ? tempoLuzOn : tempoLuzOff;
  unsigned long nextLight = (elapsedLight < period) ? (period - elapsedLight) : 0;

  String json = "{";
  json += "\"temp\":" + (isnan(lastTemp) ? String("null") : String(lastTemp,1)) + ",";
  json += "\"umid\":" + (isnan(lastHum)  ? String("null") : String(lastHum,0)) + ",";
  json += "\"nivel\":" + String(lastNivel) + ",";
  json += "\"soloRaw\":" + String(lastSoloRaw) + ",";
  json += "\"soloPct\":" + String(lastSoloPct) + ",";
  json += "\"irrigando\":" + String(irrigando ? "true":"false") + ",";
  json += "\"relays\":{";
  json += "\"luz\":"      + String(isOn(luz1,           ACT_LOW_luz)        ? "true":"false") + ",";
  json += "\"bomba\":"    + String(isOn(releBomba,      ACT_LOW_bomba)      ? "true":"false") + ",";
  json += "\"vent\":"     + String(isOn(releVentilador, ACT_LOW_ventilador) ? "true":"false") + ",";
  json += "\"valvula\":"  + String(isOn(torneiraFechar, ACT_LOW_valvFechar) ? "false":"true") + ",";
  json += "\"exaustor\":" + String(isOn(releExaustor,   ACT_LOW_exaustor)   ? "true":"false");
  json += "},";
  json += "\"next\":{";
  json += "\"lightMs\":" + String(nextLight);
  json += "}}";

  uiServer.send(200, "application/json", json);
}

// ===================== Controles =====================
void controleLuz() {
  unsigned long now = millis();
  if (luzLigada && now - tempoUltimaTrocaLuz >= tempoLuzOn) trocaLuzPersist(false);
  else if (!luzLigada && now - tempoUltimaTrocaLuz >= tempoLuzOff) trocaLuzPersist(true);
}

void controleNivelAgua() {
  unsigned long now = millis();
  if (now - lastNivelCheckMs < NIVEL_DEBOUNCE_MS) return;
  lastNivelCheckMs = now;

  bool nivelBaixo = (digitalRead(sensorNivelAlto) == HIGH);

  if (nivelBaixo) {
    setOn(torneiraAbrir,   ACT_LOW_valvAbrir);
    setOff(torneiraFechar, ACT_LOW_valvFechar);
  } else {
    setOn(torneiraFechar,  ACT_LOW_valvFechar);
    setOff(torneiraAbrir,  ACT_LOW_valvAbrir);
  }

  nivelBaixoState = nivelBaixo;
}

void controleIrrigacaoPorSolo() {
  if (!IRRIG_SOLO_AUTO) return;
  unsigned long now = millis();

  if (nivelBaixoState) {
    if (irrigando) {
      setOff(releBomba, ACT_LOW_bomba);
      irrigando = false;
    }
    return;
  }

  if (irrigando) {
    if (now - inicioIrrigacaoMs >= IRRIG_MAX_MS) {
      setOff(releBomba, ACT_LOW_bomba);
      irrigando = false;
    }
    return;
  }

  if (lastSoloRaw <= 0) return;

  if (ultimaIrrigacaoPorSoloMs != 0 && (now - ultimaIrrigacaoPorSoloMs) < MIN_INTERVALO_IRRIG_SOLO_MS) return;

  if (!soloAbaixoDoLimite && lastSoloPct <= SOLO_LIMIAR_LIGA_PCT) soloAbaixoDoLimite = true;
  if (soloAbaixoDoLimite && lastSoloPct >= SOLO_LIMIAR_DESARMA_PCT) soloAbaixoDoLimite = false;

  if (soloAbaixoDoLimite) {
    setOn(releBomba, ACT_LOW_bomba);
    irrigando = true;
    inicioIrrigacaoMs = now;
    ultimaIrrigacaoPorSoloMs = now;
  }
}

// ===================== Setup / Loop =====================
void setup() {
  Serial.begin(115200);

  // ADC (solo)
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  WiFi.onEvent(WiFiEventHandler);

  pinMode(sensorNivelAlto, INPUT);
  pinMode(releBomba, OUTPUT);
  pinMode(releVentilador, OUTPUT);
  pinMode(torneiraFechar, OUTPUT);
  pinMode(torneiraAbrir, OUTPUT);
  pinMode(releExaustor, OUTPUT);
  pinMode(luz1, OUTPUT);

  setOff(releBomba, ACT_LOW_bomba);
  setOff(releVentilador, ACT_LOW_ventilador);
  setOff(torneiraFechar, ACT_LOW_valvFechar);
  setOff(torneiraAbrir,  ACT_LOW_valvAbrir);
  setOff(releExaustor,   ACT_LOW_exaustor);

  dht.begin();

  // Persistência da luz (congelado)
  prefs.begin("grow", false);
  lightSnap.luzLigada  = true;
  lightSnap.restanteMs = (uint32_t)tempoLuzOn;

  if (prefs.getBytesLength("lsnap") == sizeof(LightSnap)) {
    prefs.getBytes("lsnap", &lightSnap, sizeof(LightSnap));
  }
  restoreLightFrozenFromSnap();

  // Lê nível e já aplica válvula na hora
  lastNivel = digitalRead(sensorNivelAlto);
  nivelBaixoState = (lastNivel == HIGH);

  if (nivelBaixoState) {
    setOn(torneiraAbrir,   ACT_LOW_valvAbrir);
    setOff(torneiraFechar, ACT_LOW_valvFechar);
  } else {
    setOn(torneiraFechar,  ACT_LOW_valvFechar);
    setOff(torneiraAbrir,  ACT_LOW_valvAbrir);
  }

  // Wi-Fi
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  WiFi.setSleep(false);

  Serial.println("\nConectando WiFi");
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Conectado. IP: ");
    Serial.println(WiFi.localIP());
    sendTelegramMessage("ESP32 conectado. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("Sem Wi-Fi!");
  }

  updateSensors();

  uiServer.on("/", handleRoot);
  uiServer.on("/status.json", handleStatus);
  uiServer.begin();
}

void loop() {
  uiServer.handleClient();

  updateSensors();

  static unsigned long last = 0;
  if (millis() - last >= 200) {
    last = millis();
    controleLuz();
    controleNivelAgua();
    controleIrrigacaoPorSolo();
  }

  static unsigned long lastSave = 0;
  if (millis() - lastSave >= 30000UL) { // 30s
    lastSave = millis();
    saveRemainingLight();
  }
}
