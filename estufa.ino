#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>

#define USE_PERSIST

#if defined(USE_PERSIST)
  #include <Preferences.h>
  #include <time.h>
  const long GMT_OFFSET = -3 * 3600;
  const int  DST_OFFSET = 0;
#endif

#define DHTPIN 13
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

const char* SSID     = "";
const char* PASSWORD = "";

const int sensorNivelAlto = 34;
const int releBomba      = 14;
const int releVentilador = 27;
const int torneiraFechar = 26;
const int torneiraAbrir  = 25;
const int releExaustor   = 33;
const int luz1           = 32;

const bool ACT_LOW_bomba      = true;
const bool ACT_LOW_ventilador = true;
const bool ACT_LOW_valvFechar = true;
const bool ACT_LOW_valvAbrir  = true;
const bool ACT_LOW_exaustor   = true;
const bool ACT_LOW_luz        = false; 

const float temperaturaMaxima = 25.0;
const float umidadeLimite = 30.0;
const unsigned long intervaloEntreIrrigacoes = 48UL * 60 * 60 * 1000;
const unsigned long tempoLuzOn  = 18UL * 60 * 60 * 1000;
const unsigned long tempoLuzOff = 6UL  * 60 * 60 * 1000;

const unsigned long duracaoIrrigacaoMs = 10UL * 1000;
bool irrigando = false;
unsigned long inicioIrrigacao = 0;
unsigned long tempoUltimaIrrigacao = 0;
unsigned long tempoUltimaTrocaLuz  = 0;
bool luzLigada = true;
bool exaustorLigado = true;

WebServer uiServer(81);

float  lastTemp = NAN;
float  lastHum  = NAN;
int    lastNivel = 0;
unsigned long lastSensorsAt = 0;

inline void setOn(int pin, bool activeLow){ digitalWrite(pin, activeLow ? LOW : HIGH); }
inline void setOff(int pin, bool activeLow){ digitalWrite(pin, activeLow ? HIGH : LOW); }
inline bool isOn(int pin, bool activeLow){
  int v = digitalRead(pin);
  return activeLow ? (v == LOW) : (v == HIGH);
}

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

#if defined(USE_PERSIST)
Preferences prefs;

struct Snapshot {
  uint32_t version = 1;
  bool luzLigada;
  bool exaustorLigado;
  bool irrigando;
  uint32_t lastLightSwitchSec;
  uint32_t lastIrrigSec;
  uint32_t irrigRemainMs;
};
Snapshot snap;

bool dirtySnap = false;
bool dirtyCrit = false; 
unsigned long lastPersistMs    = 0;
unsigned long lastCriticalSave = 0; 

const unsigned long PERSIST_INTERVAL_MS  = 15UL * 60UL * 1000UL; 
const unsigned long CRITICAL_INTERVAL_MS = 1UL  * 60UL * 1000UL; 

void saveSnap(){
  prefs.putBytes("snap", &snap, sizeof(snap));
  dirtySnap = false;
  lastPersistMs = millis();
}

void markDirty(){ 
  dirtySnap = true;
}

void markCriticalDirty(){ 
  dirtySnap = true;
  dirtyCrit = true;
}

void periodicPersist(){
  if (!dirtySnap && !dirtyCrit) return;
  unsigned long now = millis();

  if (dirtyCrit && (now - lastCriticalSave >= CRITICAL_INTERVAL_MS)) {
    saveSnap();
    lastCriticalSave = now;
    dirtyCrit = false;
    return;
  }

  if (dirtySnap && (now - lastPersistMs >= PERSIST_INTERVAL_MS)) {
    saveSnap();
  }
}

void waitForTime(unsigned long ms=5000){
  configTime(GMT_OFFSET, DST_OFFSET, "pool.ntp.org", "time.nist.gov");
  unsigned long start=millis(); struct tm t;
  while (millis()-start < ms) { if (getLocalTime(&t)) return; delay(100); }
}

uint32_t nowSec(){
  time_t n = time(nullptr);
  return (n > 1700000000) ? (uint32_t)n : 0;
}

void trocaLuz(bool ligar){
  if (ligar){ setOn(luz1, ACT_LOW_luz); luzLigada = true; }
  else      { setOff(luz1, ACT_LOW_luz); luzLigada = false; }
  tempoUltimaTrocaLuz = millis();
  snap.luzLigada = luzLigada;
  uint32_t n = nowSec(); if (n) snap.lastLightSwitchSec = n;
  markCriticalDirty(); 
}

void startIrrigacao(){
  setOn(releBomba, ACT_LOW_bomba);
  irrigando = true;
  inicioIrrigacao = millis();
  tempoUltimaIrrigacao = millis();
  snap.irrigando = true;
  uint32_t n = nowSec(); if (n) snap.lastIrrigSec = n;
  snap.irrigRemainMs = duracaoIrrigacaoMs;
  markCriticalDirty(); 
}

void stopIrrigacao(){
  setOff(releBomba, ACT_LOW_bomba);
  irrigando = false;
  snap.irrigando = false;
  snap.irrigRemainMs = 0;
  markCriticalDirty();
}

void tickIrrigacaoRemain(){
  if (!irrigando) return;
  unsigned long elapsed = millis() - inicioIrrigacao;
  uint32_t remain = (elapsed >= duracaoIrrigacaoMs) ? 0 : (duracaoIrrigacaoMs - elapsed);
  static unsigned long last = 0;
  if (millis() - last > 1000) {
    if (snap.irrigRemainMs != remain) { snap.irrigRemainMs = remain; markDirty(); }
    last = millis();
  }
}

void reconstruirEstadosPosFalha(){
  uint32_t n = nowSec();

  if (snap.lastLightSwitchSec && n){
    uint64_t deltaMs = (uint64_t)(n - snap.lastLightSwitchSec) * 1000ULL;
    uint64_t on  = tempoLuzOn, off = tempoLuzOff, ciclo = on + off;
    uint64_t fase0 = snap.luzLigada ? 0 : on;
    uint64_t fase  = (fase0 + (deltaMs % ciclo)) % ciclo;
    bool ligadaAgora = (fase < on);
    uint64_t desdeTroca = ligadaAgora ? fase : (fase - on);
    trocaLuz(ligadaAgora);
    tempoUltimaTrocaLuz = millis() - (unsigned long)desdeTroca;
  } else {
    trocaLuz(snap.luzLigada);
  }

  if (snap.irrigando){
    if (n && snap.lastIrrigSec){
      uint64_t deltaMs = (uint64_t)(n - snap.lastIrrigSec) * 1000ULL;
      if (deltaMs >= duracaoIrrigacaoMs){ stopIrrigacao(); }
      else {
        uint32_t restante = (uint32_t)(duracaoIrrigacaoMs - deltaMs);
        startIrrigacao();
        inicioIrrigacao = millis() - (duracaoIrrigacaoMs - restante);
        if (snap.irrigRemainMs != restante) { snap.irrigRemainMs = restante; markDirty(); }
      }
    } else {
      stopIrrigacao();
    }
  } else {
    if (n && snap.lastIrrigSec){
      uint64_t deltaMs64 = (uint64_t)(n - snap.lastIrrigSec) * 1000ULL;
      unsigned long deltaMs32 = (deltaMs64 > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (unsigned long)deltaMs64;
      tempoUltimaIrrigacao = millis() - deltaMs32;
    }
  }

  if (snap.exaustorLigado){
    setOn(releExaustor,   ACT_LOW_exaustor);
    setOn(releVentilador, ACT_LOW_ventilador);
    exaustorLigado = true;
  } else {
    setOff(releExaustor,   ACT_LOW_exaustor);
    setOff(releVentilador, ACT_LOW_ventilador);
    exaustorLigado = false;
  }
}
#endif 

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
.small{opacity:.8;font-size:12px}
</style></head>
<body>
<h1>🌱 Grow Monitor</h1>

<div class="grid">
 <div class="card"><h1>Sensores</h1>
  <div class="kv"><span>Temperatura</span><span id="t">--</span></div>
  <div class="kv"><span>Umidade do ar</span><span id="h">--</span></div>
  <div class="kv"><span>Nível d'água</span><span id="n">--</span></div>
 </div>

 <div class="card"><h1>Relés</h1>
  <div class="kv"><span>Luz</span><span id="rluz"  class="badge">--</span></div>
  <div class="kv"><span>Bomba d'água</span><span id="rbom"  class="badge">--</span></div>
  <div class="kv"><span>Ventilador</span><span id="rvent" class="badge">--</span></div>
  <div class="kv"><span>Torneira</span><span id="rvf" class="badge">--</span></div>
  <div class="kv"><span>Exaustor</span><span id="rexa" class="badge">--</span></div>
  <div class="kv"><span>Irrigando agora?</span><span id="ir" class="badge">--</span></div>
 </div>

 <div class="card"><h1>Ciclos</h1>
  <div class="kv"><span>Próx. troca da luz</span><span id="nextL">--</span></div>
  <div class="kv"><span>Próx. irrigação (mín.)</span><span id="nextI">--</span></div>
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
  document.getElementById('t').textContent = `${j.temp?.toFixed?.(1) ?? '--'} °C`;
  document.getElementById('h').textContent = `${j.umid?.toFixed?.(0) ?? '--'} %`;
  document.getElementById('n').textContent = j.nivel ? 'BAIXO' : 'CHEIO';

  const set=(id,val)=>{ const el=document.getElementById(id); el.textContent=on(val); el.className='badge '+(val?'ok':'warn'); };
  set('rluz', j.relays.luz);
  set('rbom', j.relays.bomba);
  set('rvent', j.relays.vent);
  set('rvf',  j.relays.valvula);
  set('rexa', j.relays.exaustor);
  set('ir',   j.irrigando);

  document.getElementById('nextL').textContent = pad(j.next.lightMs);
  document.getElementById('nextI').textContent = pad(j.next.irrigMs);
}
load(); setInterval(load, 1000);
</script>
</body></html>
)HTML";

void handleRoot(){ uiServer.send_P(200, "text/html", PAGE); }

void handleStatus(){
  if (millis() - lastSensorsAt > 800){
    lastTemp  = dht.readTemperature();
    lastHum   = dht.readHumidity();
    lastNivel = digitalRead(sensorNivelAlto);
    lastSensorsAt = millis();
  }

  unsigned long now = millis();
  unsigned long elapsedLight = now - tempoUltimaTrocaLuz;
  unsigned long period = luzLigada ? tempoLuzOn : tempoLuzOff;
  unsigned long nextLight = (elapsedLight < period) ? (period - elapsedLight) : 0;

  unsigned long nextIrrig = (tempoUltimaIrrigacao + intervaloEntreIrrigacoes > now)
                          ? (tempoUltimaIrrigacao + intervaloEntreIrrigacoes - now)
                          : 0;

  String json = "{";
  json += "\"temp\":" + (isnan(lastTemp) ? String("null") : String(lastTemp,1)) + ",";
  json += "\"umid\":" + (isnan(lastHum)  ? String("null") : String(lastHum,0)) + ",";
  json += "\"nivel\":" + String(lastNivel) + ",";
  json += "\"irrigando\":" + String(irrigando ? "true":"false") + ",";
  json += "\"relays\":{";
  json += "\"luz\":"      + String(isOn(luz1,           ACT_LOW_luz)        ? "true":"false") + ",";
  json += "\"bomba\":"    + String(isOn(releBomba,      ACT_LOW_bomba)      ? "true":"false") + ",";
  json += "\"vent\":"     + String(isOn(releVentilador, ACT_LOW_ventilador) ? "true":"false") + ",";
  json += "\"valvula\":"  + String(isOn(torneiraFechar, ACT_LOW_valvFechar) ? "false":"true") + ",";
  json += "\"exaustor\":" + String(isOn(releExaustor,   ACT_LOW_exaustor)   ? "true":"false");
  json += "},";
  json += "\"next\":{";
  json += "\"lightMs\":" + String(nextLight) + ",";
  json += "\"irrigMs\":" + String(nextIrrig);
  json += "}}";

  uiServer.send(200, "application/json", json);
}

void controleLuz() {
  unsigned long now = millis();
#if defined(USE_PERSIST)
  if (luzLigada && now - tempoUltimaTrocaLuz >= tempoLuzOn) {
    trocaLuz(false);
  } else if (!luzLigada && now - tempoUltimaTrocaLuz >= tempoLuzOff) {
    trocaLuz(true);
  }
#else
  if (luzLigada && now - tempoUltimaTrocaLuz >= tempoLuzOn) {
    setOff(luz1, ACT_LOW_luz);
    luzLigada = false;
    tempoUltimaTrocaLuz = now;
  } else if (!luzLigada && now - tempoUltimaTrocaLuz >= tempoLuzOff) {
    setOn(luz1, ACT_LOW_luz);
    luzLigada = true;
    tempoUltimaTrocaLuz = now;
  }
#endif
}

void controleIrrigacao() {
  unsigned long now = millis();

  if (!irrigando) {
    if ((now - tempoUltimaIrrigacao) > intervaloEntreIrrigacoes) {
#if defined(USE_PERSIST)
      startIrrigacao();
#else
      setOn(releBomba, ACT_LOW_bomba);
      irrigando = true;
      inicioIrrigacao = now;
      tempoUltimaIrrigacao = now;
#endif
    }
  } else {
    if (now - inicioIrrigacao >= duracaoIrrigacaoMs) {
#if defined(USE_PERSIST)
      stopIrrigacao();
#else
      setOff(releBomba, ACT_LOW_bomba);
      irrigando = false;
#endif
    }
  }
}

void controleTemperatura() {
  static unsigned long lastRun = 0;
  unsigned long now = millis();
  const unsigned long COOLDOWN = 300000UL;

  if (lastRun == 0) lastRun = now - COOLDOWN;
  if (now - lastRun < COOLDOWN) return;
  lastRun = now;

  float t = dht.readTemperature();
  if (isnan(t)) return;

  bool prev = exaustorLigado;
  if (t < temperaturaMaxima) {
    setOff(releVentilador, ACT_LOW_ventilador);
    setOff(releExaustor,   ACT_LOW_exaustor);
    exaustorLigado = false;
  } else {
    setOn(releVentilador, ACT_LOW_ventilador);
    setOn(releExaustor,   ACT_LOW_exaustor);
    exaustorLigado = true;
  }
#if defined(USE_PERSIST)
  if (exaustorLigado != prev){ snap.exaustorLigado = exaustorLigado; markDirty(); }
#endif
}

void controleUmidadeExaustor() {
  static unsigned long lastRun = 0;
  unsigned long now = millis();
  const unsigned long COOLDOWN = 300000UL;

  if (lastRun == 0) lastRun = now - COOLDOWN;
  if (now - lastRun < COOLDOWN) return;
  lastRun = now;

  float h = dht.readHumidity();

  bool prev = exaustorLigado;
  if (!isnan(h) && h > umidadeLimite) {
    setOn(releExaustor,   ACT_LOW_exaustor);
    setOn(releVentilador, ACT_LOW_ventilador);
    exaustorLigado = true;
  } else if (!exaustorLigado) {
    setOff(releExaustor,   ACT_LOW_exaustor);
    setOff(releVentilador, ACT_LOW_ventilador);
    exaustorLigado = false;
  }
#if defined(USE_PERSIST)
  if (exaustorLigado != prev){ snap.exaustorLigado = exaustorLigado; markDirty(); }
#endif
}

void controleNivelAgua() {
  int nivelAlto = digitalRead(sensorNivelAlto);
  if (nivelAlto == HIGH) {
    setOn(torneiraAbrir,  ACT_LOW_valvAbrir);
    setOff(torneiraFechar, ACT_LOW_valvFechar);
  } else {
    setOn(torneiraFechar, ACT_LOW_valvFechar);
    setOff(torneiraAbrir,  ACT_LOW_valvAbrir);
  }
}

void setup() {
  Serial.begin(115200);

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
  setOff(torneiraAbrir, ACT_LOW_valvAbrir);
  setOff(releExaustor, ACT_LOW_exaustor);
  setOn(luz1, ACT_LOW_luz);

  dht.begin();

  tempoUltimaIrrigacao = millis();
  tempoUltimaTrocaLuz  = millis();

  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  WiFi.setSleep(false);

  Serial.println("\nConectando WiFi");
  for (int i=0; i<20 && WiFi.status()!=WL_CONNECTED; ++i){ delay(500); Serial.print("."); }
  Serial.println();
  if (WiFi.status()==WL_CONNECTED) {
    Serial.print("Conectado. IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("Sem Wi-Fi!");
  }

#if defined(USE_PERSIST)
  prefs.begin("grow", false);

  snap.luzLigada          = luzLigada;
  snap.exaustorLigado     = exaustorLigado;
  snap.irrigando          = irrigando;
  snap.lastLightSwitchSec = 0;
  snap.lastIrrigSec       = 0;
  snap.irrigRemainMs      = 0;

  if (prefs.getBytesLength("snap") == sizeof(Snapshot)) {
    prefs.getBytes("snap", &snap, sizeof(Snapshot));
  }

  if (WiFi.status()==WL_CONNECTED) waitForTime(); 
  reconstruirEstadosPosFalha();
#endif

  uiServer.on("/", handleRoot);
  uiServer.on("/status.json", handleStatus);
  uiServer.begin();
}

void loop() {
  uiServer.handleClient();

  static unsigned long last = 0;
  if (millis() - last >= 200) {
    last = millis();
    controleLuz();
    controleIrrigacao();
    controleNivelAgua();
    controleTemperatura();
    controleUmidadeExaustor();
#if defined(USE_PERSIST)
    tickIrrigacaoRemain();
#endif
  }
#if defined(USE_PERSIST)
  periodicPersist();
#endif
}
