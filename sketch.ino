/*
 * ============================================================================
 *  SombraViva — Estação IoT de Ilha de Calor (ESP32 + Wokwi)
 *  Global Solution FIAP 2026 — Physical Computing: IoT & IoB
 *
 *  Integrantes:
 *    Douglas dos Santos Melo ............ RM556439
 *    Henrique Sanches ................... RM557959
 *    João Pedro Kraide Máximo ........... RM563166
 *    Matheus Marcelino Dantas da Silva .. RM556332
 *    Nicolas Caciolato Reis ............. RM556506
 * ============================================================================
 *  O que este firmware faz:
 *   1. Lê DOIS sensores DHT22: um na "Zona Arborizada" e outro na
 *      "Zona Sem Vegetação" (Calçadão de Osasco, após a retirada das árvores).
 *   2. Lê um LDR (fotoresistor) para estimar a incidência solar (lux).
 *   3. Calcula o Índice de Calor (sensação térmica) de cada zona e a
 *      diferença térmica entre elas (intensidade da ilha de calor).
 *   4. Classifica o risco: NORMAL / ATENCAO / ALERTA / PERIGO.
 *   5. Sinaliza localmente: semáforo de LEDs, buzzer e display OLED.
 *   6. Envia as medições em JSON para a API REST do SombraViva (FastAPI),
 *      autenticando o dispositivo com token Bearer (integração com Cyber):
 *      a cada 30 s E imediatamente quando o nível de risco muda
 *      (ex.: ATENCAO -> PERIGO), sem esperar o próximo ciclo.
 *   7. Consulta a temperatura oficial de Osasco na API Open-Meteo para
 *      comparar sensor x dado meteorológico (integração com o website).
 *
 *  Botão:  clique curto  -> troca a tela do display
 *          clique longo (>1 s) -> força o envio imediato para a API
 * ============================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <time.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
//  CONFIGURAÇÃO — ALTERE AQUI
// ---------------------------------------------------------------------------
// Rede Wi-Fi do simulador Wokwi (aberta, sem senha, com acesso à internet).
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// URL PÚBLICA do endpoint da API SombraViva (FastAPI).
// O simulador do Wokwi NÃO enxerga "localhost" do seu PC: publique a API
// (Render, Railway, VM com IP público) ou exponha com ngrok:
//     ngrok http 8000  ->  https://SEU-ID.ngrok-free.app/api/v1/medicoes
// Enquanto a API não estiver no ar, o valor padrão (httpbin.org) apenas
// "ecoa" o JSON recebido — serve para testar o envio no vídeo.
const char* API_URL = "https://httpbin.org/post";  // TROCAR pela URL do tunel: https://XXXX.trycloudflare.com/api/v1/medicoes
// const char* API_URL = "https://SEU-ID.ngrok-free.app/api/v1/medicoes";

// Token do dispositivo (Bearer). Deve ser o mesmo cadastrado na API
// (variável SOMBRAVIVA_DEVICE_TOKENS no backend). Use um valor FICTÍCIO.
// Em produção o token ficaria na memória NVS/flash criptografada, não no código.
const char* DEVICE_ID    = "esp32-osasco-calcadao-01";
const char* DEVICE_TOKEN = "sv-dev-TROQUE-ESTE-TOKEN";

// Coordenadas de Osasco-SP para a Open-Meteo
const char* OPEN_METEO_URL =
  "https://api.open-meteo.com/v1/forecast?latitude=-23.5329&longitude=-46.7917"
  "&current=temperature_2m,relative_humidity_2m&timezone=America%2FSao_Paulo";

// Intervalos (ms)
const unsigned long INTERVALO_LEITURA    = 5000;    // lê sensores a cada 5 s
const unsigned long INTERVALO_ENVIO      = 30000;   // envio periódico à API a cada 30 s
// Além do periódico, uma mudança de nível de risco dispara envio imediato (ver loop()).
const unsigned long INTERVALO_OPEN_METEO = 600000;  // Open-Meteo a cada 10 min

// ---------------------------------------------------------------------------
//  PINAGEM (ESP32 DevKitC V4)
//  - Evitamos os pinos de "strapping" (0, 2, 5, 12, 15) para não interferir
//    no boot.
//  - GPIO 34 é somente entrada e pertence ao ADC1 (o ADC2 não funciona
//    junto com o Wi-Fi), por isso é ideal para o LDR.
//  - Saídas (LEDs/buzzer) em GPIOs com capacidade de saída (13, 25, 26, 27).
// ---------------------------------------------------------------------------
const int PIN_DHT_ARBORIZADA = 17;  // DHT22 #1 — Zona Arborizada
const int PIN_DHT_CALCADAO   = 16;  // DHT22 #2 — Zona Sem Vegetação (Calçadão)
const int PIN_LDR            = 34;  // AO do módulo LDR (entrada analógica)
const int PIN_LED_VERDE      = 25;
const int PIN_LED_AMARELO    = 26;
const int PIN_LED_VERMELHO   = 27;
const int PIN_BUZZER         = 13;
const int PIN_BOTAO          = 33;  // com INPUT_PULLUP (botão liga ao GND)
const int PIN_I2C_SDA        = 21;
const int PIN_I2C_SCL        = 22;

// ---------------------------------------------------------------------------
//  LIMIARES DE RISCO — base científica
// ---------------------------------------------------------------------------
// Índice de Calor (Heat Index) — faixas do NOAA / National Weather Service (EUA):
//   < 27 °C ............ sem risco relevante              -> NORMAL
//   27 a 32 °C ......... "Caution": fadiga com exposição   -> ATENCAO
//   32 a 41 °C ......... "Extreme Caution": cãibras e
//                        exaustão por calor possíveis      -> ALERTA
//   >= 41 °C ........... "Danger"/"Extreme Danger":
//                        insolação provável                -> PERIGO
// Fonte: https://www.weather.gov/ama/heatindex
const float LIMIAR_ATENCAO = 27.0;
const float LIMIAR_ALERTA  = 32.0;
const float LIMIAR_PERIGO  = 41.0;

// Ilha de calor: estudos da EPA (EUA) apontam que áreas urbanas sem
// vegetação ficam tipicamente de 1 a 3 °C (dia) mais quentes que áreas
// arborizadas, chegando a mais em picos. Usamos 3 °C como ilha de calor
// relevante: se a diferença Calçadão - Arborizada >= 3 °C, o risco
// sobe pelo menos para ATENCAO e a tela destaca "ILHA DE CALOR".
// Fonte: https://www.epa.gov/heatislands/learn-about-heat-islands
const float LIMIAR_ILHA_CALOR = 3.0;

// Fuso de Brasília (UTC-3, sem horário de verão)
const long GMT_OFFSET_SEG = -3 * 3600;

// LDR do Wokwi (mesmos valores dos atributos "gamma" e "rl10" do componente)
const float LDR_GAMMA = 0.7;
const float LDR_RL10  = 50.0;   // kΩ a 10 lux

// ---------------------------------------------------------------------------
//  OBJETOS E ESTADO
// ---------------------------------------------------------------------------
Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHTesp dhtArborizada;
DHTesp dhtCalcadao;

enum NivelRisco { NORMAL = 0, ATENCAO = 1, ALERTA = 2, PERIGO = 3 };
const char* NOME_RISCO[]  = { "normal", "atencao", "alerta", "perigo" };
const char* TEXTO_RISCO[] = { "NORMAL", "ATENCAO", "ALERTA", "PERIGO" };

struct LeituraZona {
  const char* zona;      // identificador enviado à API
  const char* rotulo;    // texto curto para o display
  float temperatura;     // °C
  float umidade;         // %
  float indiceCalor;     // °C
  NivelRisco risco;
  bool valida;
};

LeituraZona arborizada = { "arborizada",     "Arborizada", NAN, NAN, NAN, NORMAL, false };
LeituraZona calcadao   = { "sem_vegetacao",  "Calcadao",   NAN, NAN, NAN, NORMAL, false };

float luminosidadeLux = 0;
float deltaIlhaCalor  = 0;      // °C (Calçadão - Arborizada)
NivelRisco riscoGeral = NORMAL;
NivelRisco riscoEnviado = NORMAL;   // último nível de risco enviado à API
bool houveEnvio = false;            // só compara depois do 1º envio

float tempOficial = NAN;        // Open-Meteo
float umidOficial = NAN;

unsigned long ultimaLeitura   = 0;
unsigned long ultimoEnvio     = 0;
unsigned long ultimoOpenMeteo = 0;
bool openMeteoPendente = true;  // consulta já no início

int telaAtual = 0;
const int TOTAL_TELAS = 4;
String statusEnvio = "aguardando";

// Botão (debounce + clique longo)
bool botaoPressionado = false;
unsigned long inicioPressao = 0;
bool longoTratado = false;

// ---------------------------------------------------------------------------
//  CÁLCULOS
// ---------------------------------------------------------------------------

// Índice de calor pelo algoritmo do NWS/NOAA (regressão de Rothfusz com
// ajustes), convertido para °C. Referência:
// https://www.wpc.ncep.noaa.gov/html/heatindex_equation.shtml
float calcularIndiceCalor(float tempC, float umidade) {
  float T = tempC * 9.0 / 5.0 + 32.0;           // °F
  float RH = umidade;
  // Fórmula simples (válida para HI < 80 °F)
  float hi = 0.5 * (T + 61.0 + ((T - 68.0) * 1.2) + (RH * 0.094));
  if ((hi + T) / 2.0 >= 80.0) {
    hi = -42.379 + 2.04901523 * T + 10.14333127 * RH
         - 0.22475541 * T * RH - 0.00683783 * T * T
         - 0.05481717 * RH * RH + 0.00122874 * T * T * RH
         + 0.00085282 * T * RH * RH - 0.00000199 * T * T * RH * RH;
    if (RH < 13.0 && T >= 80.0 && T <= 112.0) {
      hi -= ((13.0 - RH) / 4.0) * sqrt((17.0 - fabs(T - 95.0)) / 17.0);
    } else if (RH > 85.0 && T >= 80.0 && T <= 87.0) {
      hi += ((RH - 85.0) / 10.0) * ((87.0 - T) / 5.0);
    }
  }
  return (hi - 32.0) * 5.0 / 9.0;               // volta para °C
}

NivelRisco classificarRisco(float indiceCalor) {
  if (indiceCalor >= LIMIAR_PERIGO)  return PERIGO;
  if (indiceCalor >= LIMIAR_ALERTA)  return ALERTA;
  if (indiceCalor >= LIMIAR_ATENCAO) return ATENCAO;
  return NORMAL;
}

// Converte a leitura do ADC (12 bits, 3,3 V) em lux.
// O módulo do Wokwi tem o LDR em série com um resistor de 10 kΩ.
float lerLuminosidadeLux() {
  int adc = analogRead(PIN_LDR);
  float tensao = adc / 4095.0 * 3.3;
  if (tensao >= 3.29) return 0;                 // escuro total
  if (tensao <= 0.001) return 100000;           // sol direto (saturado)
  float resistencia = 10000.0 * tensao / (3.3 - tensao);
  float lux = pow(LDR_RL10 * 1e3 * pow(10, LDR_GAMMA) / resistencia, 1.0 / LDR_GAMMA);
  if (!isfinite(lux)) lux = 100000;
  return lux;
}

void lerZona(DHTesp& sensor, LeituraZona& z) {
  TempAndHumidity v = sensor.getTempAndHumidity();
  if (sensor.getStatus() != DHTesp::ERROR_NONE || isnan(v.temperature) || isnan(v.humidity)) {
    z.valida = false;
    Serial.printf("[DHT] Falha na leitura da zona %s: %s\n", z.zona, sensor.getStatusString());
    return;
  }
  z.temperatura = v.temperature;
  z.umidade     = v.humidity;
  z.indiceCalor = calcularIndiceCalor(v.temperature, v.humidity);
  z.risco       = classificarRisco(z.indiceCalor);
  z.valida      = true;
}

void lerSensores() {
  lerZona(dhtArborizada, arborizada);
  lerZona(dhtCalcadao, calcadao);
  luminosidadeLux = lerLuminosidadeLux();

  if (arborizada.valida && calcadao.valida) {
    deltaIlhaCalor = calcadao.temperatura - arborizada.temperatura;
  }

  // Risco geral = o pior entre as zonas (a população está exposta ao pior caso)
  NivelRisco r = NORMAL;
  if (arborizada.valida && arborizada.risco > r) r = arborizada.risco;
  if (calcadao.valida   && calcadao.risco   > r) r = calcadao.risco;
  // Ilha de calor relevante eleva o risco no mínimo para ATENCAO
  if (deltaIlhaCalor >= LIMIAR_ILHA_CALOR && r < ATENCAO) r = ATENCAO;
  riscoGeral = r;

  Serial.printf("[LEITURA] Arborizada: %.1fC %.0f%% IC=%.1fC | Calcadao: %.1fC %.0f%% IC=%.1fC | "
                "Delta=%.1fC | Luz=%.0f lux | Risco=%s\n",
                arborizada.temperatura, arborizada.umidade, arborizada.indiceCalor,
                calcadao.temperatura, calcadao.umidade, calcadao.indiceCalor,
                deltaIlhaCalor, luminosidadeLux, TEXTO_RISCO[riscoGeral]);
}

// ---------------------------------------------------------------------------
//  ATUADORES (LEDs e buzzer)
// ---------------------------------------------------------------------------
//  NORMAL  -> verde
//  ATENCAO -> amarelo
//  ALERTA  -> vermelho fixo
//  PERIGO  -> vermelho piscando + buzzer intermitente
void atualizarAtuadores() {
  bool pisca = (millis() / 500) % 2;   // alterna a cada 500 ms
  digitalWrite(PIN_LED_VERDE,    riscoGeral == NORMAL);
  digitalWrite(PIN_LED_AMARELO,  riscoGeral == ATENCAO);
  digitalWrite(PIN_LED_VERMELHO, riscoGeral == ALERTA || (riscoGeral == PERIGO && pisca));

  static bool buzzerLigado = false;
  bool deveTocar = (riscoGeral == PERIGO && pisca);
  if (deveTocar && !buzzerLigado) { tone(PIN_BUZZER, 2000); buzzerLigado = true; }
  if (!deveTocar && buzzerLigado) { noTone(PIN_BUZZER);     buzzerLigado = false; }
}

// ---------------------------------------------------------------------------
//  DISPLAY OLED (texto sem acentos: a fonte padrão não tem UTF-8)
// ---------------------------------------------------------------------------
void desenharZona(const LeituraZona& z) {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("ZONA %s", z.rotulo);
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  if (!z.valida) { display.setCursor(0, 20); display.print("Sensor sem leitura"); return; }
  display.setTextSize(2);
  display.setCursor(0, 14);
  display.printf("%.1fC", z.temperatura);
  display.setTextSize(1);
  display.setCursor(0, 34);
  display.printf("Umidade:  %.0f%%", z.umidade);
  display.setCursor(0, 44);
  display.printf("Sensacao: %.1fC", z.indiceCalor);
  display.setCursor(0, 54);
  display.printf("Risco: %s", TEXTO_RISCO[z.risco]);
}

void atualizarDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  switch (telaAtual) {
    case 0: desenharZona(arborizada); break;
    case 1: desenharZona(calcadao);   break;
    case 2:
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("ILHA DE CALOR");
      display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
      display.setTextSize(2);
      display.setCursor(0, 14);
      display.printf("%+.1fC", deltaIlhaCalor);
      display.setTextSize(1);
      display.setCursor(0, 34);
      display.print(deltaIlhaCalor >= LIMIAR_ILHA_CALOR ? "Calcadao MAIS QUENTE!" : "Diferenca baixa");
      display.setCursor(0, 44);
      display.printf("Luz: %.0f lux", luminosidadeLux);
      display.setCursor(0, 54);
      display.printf("RISCO: %s", TEXTO_RISCO[riscoGeral]);
      break;
    case 3:
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("OSASCO x SENSOR");
      display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
      display.setCursor(0, 14);
      if (isnan(tempOficial)) display.print("Open-Meteo: --");
      else display.printf("Open-Meteo: %.1fC %.0f%%", tempOficial, umidOficial);
      display.setCursor(0, 26);
      display.printf("Calcadao:   %.1fC", calcadao.temperatura);
      display.setCursor(0, 38);
      if (!isnan(tempOficial)) display.printf("Diferenca:  %+.1fC", calcadao.temperatura - tempOficial);
      display.setCursor(0, 54);
      display.printf("API: %s", statusEnvio.c_str());
      break;
  }
  display.display();
}

// ---------------------------------------------------------------------------
//  REDE: Wi-Fi, NTP, Open-Meteo e API SombraViva
// ---------------------------------------------------------------------------
void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("[WIFI] Conectando a %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);  // canal 6 acelera a conexão no Wokwi
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI] Conectado! IP: %s\n", WiFi.localIP().toString().c_str());
    configTime(GMT_OFFSET_SEG, 0, "pool.ntp.org", "time.google.com");
  } else {
    Serial.println("\n[WIFI] Sem conexao — seguindo em modo offline (somente alertas locais).");
  }
}

// Data/hora ISO 8601 com fuso de Brasília. Vazio se o NTP ainda não sincronizou
// (nesse caso a API usa o horário do servidor).
String timestampISO() {
  struct tm t;
  if (!getLocalTime(&t, 100)) return "";
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S-03:00", &t);
  return String(buf);
}

// Requisição HTTP(S) genérica. HTTPS sem validação de certificado
// (setInsecure) apenas por ser um protótipo no simulador; em produção
// usaríamos o certificado raiz (CA) da API com setCACert().
bool iniciarHttp(HTTPClient& http, WiFiClientSecure& tls, WiFiClient& plain, const char* url) {
  if (strncmp(url, "https://", 8) == 0) {
    tls.setInsecure();
    return http.begin(tls, url);
  }
  return http.begin(plain, url);
}

void consultarOpenMeteo() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure tls; WiFiClient plain; HTTPClient http;
  if (!iniciarHttp(http, tls, plain, OPEN_METEO_URL)) return;
  http.setTimeout(8000);
  int code = http.GET();
  if (code == 200) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    if (!err) {
      tempOficial = doc["current"]["temperature_2m"] | NAN;
      umidOficial = doc["current"]["relative_humidity_2m"] | NAN;
      Serial.printf("[OPEN-METEO] Osasco agora: %.1fC, %.0f%% de umidade\n", tempOficial, umidOficial);
    } else {
      Serial.printf("[OPEN-METEO] JSON invalido: %s\n", err.c_str());
    }
  } else {
    Serial.printf("[OPEN-METEO] Falha HTTP %d (%s)\n", code, http.errorToString(code).c_str());
  }
  http.end();
}

// Envia UMA medição (uma zona) para POST /api/v1/medicoes
bool enviarMedicao(const LeituraZona& z) {
  if (!z.valida) return false;

  JsonDocument doc;
  doc["device_id"]    = DEVICE_ID;
  doc["zona"]         = z.zona;
  doc["temperatura"]  = round(z.temperatura * 10) / 10.0;
  doc["umidade"]      = round(z.umidade * 10) / 10.0;
  doc["indice_calor"] = round(z.indiceCalor * 10) / 10.0;
  doc["luminosidade"] = round(luminosidadeLux);
  doc["nivel_risco"]  = NOME_RISCO[z.risco];
  doc["delta_ilha_calor"] = round(deltaIlhaCalor * 10) / 10.0;
  if (!isnan(tempOficial)) doc["temperatura_oficial"] = tempOficial;
  String ts = timestampISO();
  if (ts.length()) doc["timestamp"] = ts; else doc["timestamp"] = nullptr;

  String corpo;
  serializeJson(doc, corpo);

  WiFiClientSecure tls; WiFiClient plain; HTTPClient http;
  if (!iniciarHttp(http, tls, plain, API_URL)) {
    Serial.println("[API] URL invalida");
    return false;
  }
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + DEVICE_TOKEN);  // autenticação do dispositivo
  http.addHeader("X-Device-Id", DEVICE_ID);

  int code = http.POST(corpo);
  bool ok = (code >= 200 && code < 300);
  if (ok) {
    Serial.printf("[API] %s enviado (HTTP %d): %s\n", z.zona, code, corpo.c_str());
  } else if (code == 401 || code == 403) {
    Serial.printf("[API] Token do dispositivo REJEITADO (HTTP %d). Verifique DEVICE_TOKEN.\n", code);
  } else if (code < 0) {
    // Fallback: API fora do ar — mantém o funcionamento local e registra no Serial
    Serial.printf("[API] Indisponivel (%s). Dado mantido apenas no log: %s\n",
                  http.errorToString(code).c_str(), corpo.c_str());
  } else {
    Serial.printf("[API] Erro HTTP %d: %s\n", code, http.getString().c_str());
  }
  http.end();
  statusEnvio = ok ? "OK " + String(code) : "falha " + String(code);
  return ok;
}

void enviarTudo() {
  if (WiFi.status() != WL_CONNECTED) {
    statusEnvio = "offline";
    Serial.println("[API] Sem Wi-Fi: medicoes registradas apenas no Serial.");
    conectarWiFi();
    return;
  }
  enviarMedicao(arborizada);
  enviarMedicao(calcadao);
  riscoEnviado = riscoGeral;
  houveEnvio = true;
}

// ---------------------------------------------------------------------------
//  BOTÃO
// ---------------------------------------------------------------------------
void tratarBotao() {
  bool pressionado = digitalRead(PIN_BOTAO) == LOW;
  unsigned long agora = millis();

  if (pressionado && !botaoPressionado) {          // acabou de apertar
    botaoPressionado = true;
    inicioPressao = agora;
    longoTratado = false;
  } else if (pressionado && botaoPressionado) {    // segurando
    if (!longoTratado && agora - inicioPressao > 1000) {
      longoTratado = true;
      Serial.println("[BOTAO] Clique longo: envio forcado para a API");
      lerSensores();
      enviarTudo();
      ultimoEnvio = millis();
    }
  } else if (!pressionado && botaoPressionado) {   // soltou
    botaoPressionado = false;
    if (!longoTratado && agora - inicioPressao > 50) {   // debounce de 50 ms
      telaAtual = (telaAtual + 1) % TOTAL_TELAS;
      atualizarDisplay();
    }
  }
}

// ---------------------------------------------------------------------------
//  SETUP / LOOP
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== SombraViva — Estacao IoT de Ilha de Calor ===");

  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMARELO, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BOTAO, INPUT_PULLUP);
  analogReadResolution(12);

  dhtArborizada.setup(PIN_DHT_ARBORIZADA, DHTesp::DHT22);
  dhtCalcadao.setup(PIN_DHT_CALCADAO, DHTesp::DHT22);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[OLED] Display nao encontrado!");
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(4, 8);
  display.print("SombraViva");
  display.setTextSize(1);
  display.setCursor(4, 34);
  display.print("Ilha de calor urbana");
  display.setCursor(4, 48);
  display.print("Osasco-SP");
  display.display();

  conectarWiFi();
  delay(1000);
  lerSensores();
  ultimaLeitura = millis();
}

void loop() {
  unsigned long agora = millis();

  tratarBotao();

  if (agora - ultimaLeitura >= INTERVALO_LEITURA) {
    ultimaLeitura = agora;
    lerSensores();
    atualizarDisplay();

    // Mudança de faixa (ex.: ATENCAO -> PERIGO ou PERIGO -> ALERTA):
    // envia na hora, sem esperar o ciclo de 30 s, e reinicia a contagem.
    if (houveEnvio && riscoGeral != riscoEnviado) {
      Serial.printf("[API] Risco mudou: %s -> %s. Envio imediato.\n",
                    TEXTO_RISCO[riscoEnviado], TEXTO_RISCO[riscoGeral]);
      riscoEnviado = riscoGeral;   // evita reenvio a cada leitura se estiver offline
      enviarTudo();
      ultimoEnvio = millis();
    }
  }

  if (openMeteoPendente || agora - ultimoOpenMeteo >= INTERVALO_OPEN_METEO) {
    openMeteoPendente = false;
    ultimoOpenMeteo = agora;
    consultarOpenMeteo();
  }

  if (ultimoEnvio == 0 || agora - ultimoEnvio >= INTERVALO_ENVIO) {
    ultimoEnvio = agora == 0 ? 1 : agora;
    enviarTudo();
  }

  atualizarAtuadores();
  delay(10);
}
