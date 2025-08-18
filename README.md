# Estufa-Monitor-ESP32-DHT11-Web-UI-
Sistema de automação simples para cultivo usando ESP32

<img width="1088" height="889" alt="esquema eletrico" src="https://github.com/user-attachments/assets/e713ff0c-2f34-4d9e-8528-11f5400b55d1" />

Itens:
ESP-32S, 
Módulo rele 8 canais 5V, 
Sensor de temperatura DHT11 3.3v-5v, 
Ventilador 5V, 
Válvula solenóide elétrica de três fios, dois controles 220v, 
Bomba De Água Elétrica, Alta Pressão, Diafragma 12V, 
Power Cooling Case Fan 12V, 
Sensor capacitivo de nível de água sem contato, 3.3v-5v, 
2x Lâmpada Full Espectro 300 leds AC85V-265V.


leitura de temperatura/umidade (DHT11), controle de luz, bomba, ventilador, exaustor e válvulas, UI web (porta 81) para status em tempo real, persistência opcional do estado (NVS + NTP) para retomar “de onde parou” após queda de energia.

⚙️ Funcionalidades

Ciclos da luz: 18h ON / 6h OFF.

Irrigação: 1 ciclo a cada 24h, com duração de 10 s.

Controle térmico/umidade: liga/desliga ventilador/exaustor com cooldown de 5 min.

UI HTTP (porta 81):

/ → dashboard HTML.

/status.json → JSON com sensores, estados e próximos eventos.

Persistência:

Salva estado atual e timestamps em NVS (flash).

Usa NTP para reconstruir o “tempo perdido” enquanto estava sem energia.

Eventos críticos (troca de luz, início/fim irrigação): no máximo 1 gravação / minuto.

Demais mudanças: no máximo 1 gravação / 15 minutos.

🧩 Hardware / Ligações (padrão do código) Função Pino ESP32 DHT11 (DATA) 13 Sensor nível alto 34 (entrada) Bomba (relé) 14 Ventilador (relé) 27 Válvula fechar 26 Válvula abrir 25 Exaustor (relé) 33 Luz (relé) 32

🛠️ Dependências

Plataforma: Arduino Core for ESP32.

Bibliotecas Arduino:

WiFi.h

WebServer.h

DHT.h (para DHT11)

Preferences.h (NVS)

time.h (NTP)

🔧 Configuração rápida

No topo do código:

const char* SSID = "xxx"; const char* PASSWORD = "xxx";

Ajuste para o seu Wi-Fi.

Fusos:

const long GMT_OFFSET = -3 * 3600; const int DST_OFFSET = 0;

🧪 Como compilar e rodar

Abra o sketch no Arduino IDE / PlatformIO.

Se quiser retomar o estado após queda de energia, deixe ATIVADO:

#define USE_PERSIST

Para rodar sem persistência, comente essa linha.

Compile e faça upload para o ESP32.

Abra o Serial Monitor (115200) para ver o IP.

No navegador, acesse: http://:81/

🌐 Endpoints HTTP

GET / → UI (HTML).

GET /status.json → status em JSON:

{ "temp": 24.3, "umid": 55, "nivel": 0, "irrigando": false, "relays": { "luz": true, "bomba": false, "vent": false, "valvula": true, "exaustor": false }, "next": { "lightMs": 123456, "irrigMs": 7890000 } }

💾 Persistência (NVS) e NTP — como funciona

Ativo somente se #define USE_PERSIST estiver definido.

O que salva (estrutura Snapshot):

luzLigada, exaustorLigado, irrigando.

lastLightSwitchSec (epoch em segundos).

lastIrrigSec (epoch).

irrigRemainMs (tempo restante se caiu no meio da irrigação).

Quando grava:

Críticos (troca de luz, início/fim irrigação): máx. 1/min.

Outros (exaustor, ventilador, resto): máx. 1/15 min.

Se nada muda, não grava.

No boot:

Se houver Wi-Fi, sincroniza NTP rapidamente.

Reconstrói fase da luz no ciclo (ON/OFF) pelo epoch salvo.

Irrigação:

Se caiu no meio e tem NTP: completa só o que falta.

Sem NTP confiável: não retoma automaticamente (fail-safe).

Restaura estado do exaustor/ventilador.

Por que epoch (NTP)? Porque millis() zera no reboot; com hora real dá pra saber quanto tempo passou enquanto estava sem energia.

🔐 Limites e boas práticas

Gravações na flash são limitadas (ciclos por setor). A política de 1 min (críticos) / 15 min (não críticos) prolonga muito a vida útil.

Se seu NTP demorar (Wi-Fi instável), a recuperação de irrigação no meio do ciclo não será retomada automaticamente — intencional para segurança.

🧹 Como “zerar” os dados salvos (NVS)

Se quiser limpar o snapshot e começar do zero, adicione temporariamente no setup():

#if defined(USE_PERSIST) prefs.begin("grow", false); prefs.clear(); // APAGA tudo do namespace "grow" prefs.end(); #endif

Compile/suba uma vez, depois remova esse trecho para não apagar sempre.

🔧 Ajustes rápidos (constantes) // Períodos const unsigned long intervaloEntreIrrigacoes = 24UL * 60 * 60 * 1000; // 24h const unsigned long tempoLuzOn = 18UL * 60 * 60 * 1000; // 18h const unsigned long tempoLuzOff = 6UL * 60 * 60 * 1000; // 6h const unsigned long duracaoIrrigacaoMs = 10UL * 1000; // 10s

// Persistência (se USE_PERSIST) const unsigned long PERSIST_INTERVAL_MS = 15UL * 60UL * 1000UL; // não críticos const unsigned long CRITICAL_INTERVAL_MS = 1UL * 60UL * 1000UL; // críticos

🧭 Solução de problemas

UI não abre: verifique o IP no Serial e use http://IP:81/. Confirme que o roteador permite clientes se acessarem.

Sem leitura DHT: confirme o pino 13 (DATA) e o GND/5V/3V3 corretos; DHT11 pode precisar de resistor de pull-up (4.7k–10k) no DATA.

Relé “invertido”: ajuste ACT_LOW_* (true = ativo-baixo; false = ativo-alto).

Não retoma irrigação após queda: precisa de NTP. Confira Wi-Fi e servidores NTP (internet).

Flash desgastando: revise se não aumentou a frequência de gravações; mantenha 1 min (críticos) / 15 min (não críticos) ou aumente ainda mais.
