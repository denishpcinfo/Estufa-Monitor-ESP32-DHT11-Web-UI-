# 🌱 Grow Controller – ESP32

Sistema embarcado para automação de estufa / grow indoor utilizando **ESP32**, com controle de **luz**, **irrigação por umidade do solo**, **nível de água**, **ventilação**, **exaustão** e **monitoramento via interface web**.

<img width="1276" height="776" alt="Captura de tela de 2026-01-18 21-07-18" src="https://github.com/user-attachments/assets/6776d20b-9fe5-4a4a-adfd-f250eefa3acb" />

---

## 🧩 Hardware

- **ESP32 (ESP-32S)**
- **Módulo relé 8 canais – 5 V**
- **Sensor de temperatura/umidade DHT11 (3.3 V–5 V)**
- **Sensor capacitivo de umidade do solo (3.3 V)**
- **Sensor capacitivo de nível de água sem contato (3.3 V–5 V)**
- **Válvula solenoide elétrica – 220 V**
- **Bomba de água diafragma – 12 V**
- **Ventilador 5 V**
- **Cooler tipo case fan – 12 V**
- **2× Lâmpadas Full Spectrum – 300 LEDs (AC 85–265 V)**

---

## ⚙️ Funcionalidades

<img width="1906" height="897" alt="Captura de tela 2026-01-18 212243" src="https://github.com/user-attachments/assets/b8e035c6-bfc6-44b1-9498-c3c266e66e02" />


### 💡 Controle de Luz
- Ciclo **18h ON / 6h OFF**
- Persistência do estado usando **NVS (flash)**
- Em queda de energia:
  - O tempo do ciclo **fica congelado**
  - Ao religar, a luz continua **exatamente de onde parou**
  - O tempo desligado **não é descontado**

---

### 💧 Irrigação automática (por umidade do solo)
- Baseada **exclusivamente na umidade do solo**
- Inicia irrigação quando a umidade ≤ limite configurado
- **Bomba liga por no máximo 6 segundos**
- Irrigação só ocorre se:
  - Sensor de solo estiver válido
  - Nível de água **não estiver baixo**
- Intervalo mínimo entre irrigações configurável (ex: 6 h)

---

### 🚰 Controle de Nível de Água
- Sensor capacitivo **sem contato**
- Quando o nível está **baixo**:
  - Abre válvula para encher
  - **Suspende qualquer ação de irrigação**
  - Leituras do solo são ignoradas (evita travamento do ADC)
- Quando o nível volta a **cheio**:
  - Fecha válvula
  - Sistema retoma funcionamento normal

---

### 🌬️ Temperatura e Umidade do Ar
- Ventilador e exaustor acionados por:
  - Temperatura máxima configurada
  - Umidade do ar elevada
- Cooldown de **5 minutos** entre decisões (anti-oscilação)

---

## 🌐 Interface Web (HTTP – porta 81)

### Endpoints

- **GET /**  
  Painel HTML com:
  - Temperatura
  - Umidade do ar
  - Umidade do solo (%)
  - Solo RAW
  - Nível de água
  - Estados dos relés
  - Próxima troca da luz

- **GET /status.json**  
  Retorna o estado completo do sistema em JSON

### Exemplo de resposta

```json
{
  "temp": 24.3,
  "umid": 55,
  "nivel": 0,
  "soloRaw": 1320,
  "soloPct": 48,
  "irrigando": false,
  "relays": {
    "luz": true,
    "bomba": false,
    "vent": false,
    "valvula": true,
    "exaustor": false
  },
  "next": {
    "lightMs": 123456
  }
}
💾 Persistência (NVS – Flash)
O que é persistido
Estado da luz (ON / OFF)

Tempo restante até a próxima troca de luz

Como funciona
O estado é salvo a cada 30 segundos

Em queda de energia:

O tempo não avança

Ao religar, o ciclo continua exatamente do ponto salvo

Não utiliza NTP

Modelo simples, previsível e seguro

🛠️ Dependências
Plataforma
Arduino Core para ESP32

Bibliotecas
WiFi.h

WebServer.h

DHT.h

Preferences.h

WiFiClientSecure.h

🔧 Configuração rápida
No início do código:

const char* SSID     = "SEU_WIFI";
const char* PASSWORD = "SUA_SENHA";
Ajustes principais
// Luz
const unsigned long tempoLuzOn  = 18UL * 60UL * 60UL * 1000UL;
const unsigned long tempoLuzOff =  6UL * 60UL * 60UL * 1000UL;

// Irrigação
const unsigned long IRRIG_MAX_MS = 6000UL; // 6 segundos
const unsigned long MIN_INTERVALO_IRRIG_SOLO_MS = 6UL * 60UL * 60UL * 1000UL;

// Solo (calibração)
int SOLO_SECO    = 1427;
int SOLO_MOLHADO = 1050;
🧪 Como compilar e rodar
Abra o sketch no Arduino IDE ou PlatformIO

Selecione a placa ESP32 correta

Compile e faça upload

Abra o Serial Monitor (115200)

Anote o IP exibido

Acesse no navegador:

http://IP_DO_ESP32:81/
🧹 Como limpar os dados salvos (NVS)
Para resetar o estado persistido:

prefs.begin("grow", false);
prefs.clear();
prefs.end();
⚠️ Use uma vez, compile/suba e depois remova para não apagar sempre.


📌 Observações finais

Este projeto prioriza robustez, segurança elétrica e comportamento previsível.
Nenhuma ação crítica é executada com sensores inválidos ou estados inconsistentes.

