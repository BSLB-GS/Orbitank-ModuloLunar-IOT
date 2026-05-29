#include <Arduino.h>
#include <DHT.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUDP.h>

// ─── Configuração WiFi e Backend ─────────────────────────
#define WIFI_SSID        "Wokwi-GUEST"
#define WIFI_PASSWORD    ""
#define BACKEND_URL      "http://webhook.site/1fed556c-cd0a-4479-b260-fa304fed0f5b"  //!TROCAR O IP!!!!!!!!!!

// ─── Pinos ───────────────────────────────────────────────
#define PIN_BTN_AGUA     32
#define PIN_BTN_ENERGIA  33
#define PIN_DHT          4
#define PIN_BTN_ELETRO   18
#define PIN_BTN_EMERG    19
#define PIN_LED_VERDE    25
#define PIN_LED_AMARELO  26
#define PIN_LED_VERMELHO 27
#define PIN_BUZZER       14

// ─── Sensores ────────────────────────────────────────────
#define DHT_TYPE DHT22
DHT dht(PIN_DHT, DHT_TYPE);

// OLED 128x64 via I2C (SDA=21, SCL=22)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

// ─── Variáveis de estado ──────────────────────────────────
float nivelAgua    = 0;
float nivelEnergia = 50;
float temperatura  = 0;
float umidade      = 0;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

bool eletroliseAtiva  = false;
bool modoEmergencia   = false;
bool eletroliseBloqueada = false;
String motivoBloqueio = "";

bool btnEletroAnterior = false;
bool btnEmergAnterior  = false;

#define DEBOUNCE_MS 50

bool rawEletro    = false;
bool rawEmerg     = false;
bool estEletro    = false;
bool estEmerg     = false;

unsigned long tMudancaEletro = 0;
unsigned long tMudancaEmerg  = 0;

enum StatusEstacao { OPERACIONAL, ATENCAO, CRITICO };
StatusEstacao statusAtual = OPERACIONAL;

float h2Produzido = 0;
float o2Produzido = 0;

// ─── Temporização não-bloqueante ─────────────────────────
unsigned long ultimoLoop      = 0;
unsigned long ultimoBuzzer    = 0;
unsigned long tempoBloqueio   = 0;
unsigned long ultimoEnvioPost = 0;
bool buzzerLigado             = false;
bool mostrarBloqueio          = false;

#define INTERVALO_LOOP     500
#define INTERVALO_BUZZER   200
#define DURACAO_BLOQUEIO   3000
#define INTERVALO_POST     60000   // 60 segundos

// ─── Helpers de status ───────────────────────────────────
String getModuleStatus() {
  if (modoEmergencia)       return "EMERGENCY";
  if (statusAtual == CRITICO)  return "CRITICAL";
  if (statusAtual == ATENCAO)  return "WARNING";
  return "OPERATIONAL";
}

String getRiskLevel() {
  if (modoEmergencia)          return "HIGH";
  if (statusAtual == CRITICO)  return "HIGH";
  if (statusAtual == ATENCAO)  return "MEDIUM";
  return "LOW";
}

bool isAlertActive() {
  return (modoEmergencia || statusAtual == CRITICO || statusAtual == ATENCAO);
}

String getAlertType() {
  if (modoEmergencia)                return "EMERGENCY_STOP";
  if (nivelEnergia < 20)             return "LOW_ENERGY";
  if (nivelAgua < 6)                 return "LOW_WATER";
  if (temperatura > 60.0)            return "HIGH_TEMPERATURE";
  if (statusAtual == ATENCAO)        return "RESOURCE_WARNING";
  return "NONE";
}

String getAlertMessage() {
  if (modoEmergencia)                return "Modo de emergencia ativado manualmente";
  if (nivelEnergia < 20)             return "Nivel de energia critico: abaixo de 20%";
  if (nivelAgua < 6)                 return "Nivel de agua insuficiente: abaixo de 6%";
  if (temperatura > 60.0)            return "Temperatura elevada detectada";
  if (nivelEnergia < 50)             return "Energia abaixo de 50%, atencao recomendada";
  if (nivelAgua < 25)                return "Nivel de agua baixo, reabastecimento recomendado";
  return "";
}

String getAlertSeverity() {
  if (modoEmergencia)          return "CRITICAL";
  if (statusAtual == CRITICO)  return "HIGH";
  if (statusAtual == ATENCAO)  return "MEDIUM";
  return "NONE";
}

// ─── Envio POST ──────────────────────────────────────────
void enviarTelemetria() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] WiFi desconectado, pulando envio.");
    return;
  }

 
  timeClient.update();
  unsigned long ts = timeClient.getEpochTime(); // Unix timestamp em segundos

  StaticJsonDocument<768> doc;
  doc["deviceId"]                  = 1;
  doc["stationCode"]               = 1;
  doc["timestamp"]                 = ts; //ts em segundos (long)
  doc["iceLevelPercent"]           = nivelAgua;
  doc["waterLevelPercent"]         = nivelAgua;
  doc["hydrogenLevelPercent"]      = h2Produzido;   
  doc["oxygenLevelPercent"]        = o2Produzido;   
  doc["energyLevelPercent"]        = nivelEnergia;
  doc["temperatureCelsius"]        = temperatura;
  doc["humidityPercent"]           = umidade;
  doc["electrolysisActive"]        = eletroliseAtiva;
  doc["electrolysisBlocked"]       = eletroliseBloqueada;
  doc["blockReason"]               = motivoBloqueio;
  doc["hydrogenGeneratedPercent"]  = h2Produzido;
  doc["oxygenGeneratedPercent"]    = o2Produzido;
  doc["emergencyMode"]             = modoEmergencia;
  doc["moduleStatus"]              = getModuleStatus();
  doc["riskLevel"]                 = getRiskLevel();
  doc["alertActive"]               = isAlertActive();
  doc["alertType"]                 = getAlertType();
  doc["alertMessage"]              = getAlertMessage();
  doc["alertSeverity"]             = getAlertSeverity();

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.begin(BACKEND_URL);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    Serial.printf("[HTTP] POST enviado. Código: %d\n", httpCode);
    if (httpCode == HTTP_CODE_OK || httpCode == 201) {
      Serial.println("[HTTP] Telemetria aceita pelo servidor.");
    }
  } else {
    Serial.printf("[HTTP] Falha no envio: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}

// ─── Setup ───────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN_AGUA,     INPUT_PULLDOWN);
  pinMode(PIN_BTN_ENERGIA,  INPUT_PULLDOWN);
  pinMode(PIN_BTN_ELETRO,   INPUT_PULLDOWN);
  pinMode(PIN_BTN_EMERG,    INPUT_PULLDOWN);
  pinMode(PIN_LED_VERDE,    OUTPUT);
  pinMode(PIN_LED_AMARELO,  OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_BUZZER,       OUTPUT);

  digitalWrite(PIN_LED_VERDE,    LOW);
  digitalWrite(PIN_LED_AMARELO,  LOW);
  digitalWrite(PIN_LED_VERMELHO, LOW);
  digitalWrite(PIN_BUZZER,       LOW);

  dht.begin();

  oled.begin();
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(20, 30, "LunarFuel Alpha");
  oled.drawStr(28, 45, "Iniciando...");
  oled.sendBuffer();

  // Conecta ao WiFi
  Serial.printf("Conectando ao WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi conectado! IP: %s\n", WiFi.localIP().toString().c_str());
    timeClient.begin();       
    timeClient.update();
  } else {
    Serial.println("\nFalha ao conectar ao WiFi. Operando offline.");
  }

  delay(1000);
}

// ─── Leitura dos botões ──────────────────────────────────
void lerBotoes() {
  bool leituraEletro = digitalRead(PIN_BTN_ELETRO) == HIGH;
  bool leituraEmerg  = digitalRead(PIN_BTN_EMERG)  == HIGH;

  if (leituraEletro != rawEletro) {
    rawEletro      = leituraEletro;
    tMudancaEletro = millis();
  }
  if ((millis() - tMudancaEletro) >= DEBOUNCE_MS) {
    if (rawEletro && !estEletro) {
      if (eletroliseAtiva) {
        eletroliseAtiva      = false;
        eletroliseBloqueada  = false;
        motivoBloqueio       = "";
      } else if (nivelEnergia >= 20 && nivelAgua >= 6) {
        eletroliseAtiva      = true;
        eletroliseBloqueada  = false;
        motivoBloqueio       = "";
      } else {
        eletroliseBloqueada = true;
        motivoBloqueio = (nivelEnergia < 20)
          ? "Energia critica (min 20%)"
          : "Agua insuficiente (min 6%)";
        mostrarBloqueio = true;
        tempoBloqueio   = millis();
      }
    }
    estEletro = rawEletro;
  }

  if (leituraEmerg != rawEmerg) {
    rawEmerg      = leituraEmerg;
    tMudancaEmerg = millis();
  }
  if ((millis() - tMudancaEmerg) >= DEBOUNCE_MS) {
    if (rawEmerg && !estEmerg) {
      modoEmergencia = !modoEmergencia;
      if (modoEmergencia) eletroliseAtiva = false;
    }
    estEmerg = rawEmerg;
  }
}

// ─── Leitura e Simulação ─────────────────────────────────
void lerSensores() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperatura = t;
  if (!isnan(h)) umidade     = h;

  if (eletroliseAtiva) {
    nivelAgua    -= 1.0;
    nivelEnergia -= 1.5;
    h2Produzido  += 0.5;
    o2Produzido  += 1.0;
    if (nivelAgua < 0)    nivelAgua    = 0;
    if (nivelEnergia < 0) nivelEnergia = 0;
  } else {
    if (digitalRead(PIN_BTN_AGUA) == HIGH) {
      nivelAgua += 5.0;
      if (nivelAgua > 100.0) nivelAgua = 100.0;
    }
    if (digitalRead(PIN_BTN_ENERGIA) == HIGH) {
      nivelEnergia += 5.0;
      if (nivelEnergia > 100.0) nivelEnergia = 100.0;
    }
  }
}

// ─── Lógica de status ────────────────────────────────────
void atualizarStatus() {
  if (modoEmergencia) { statusAtual = CRITICO; return; }

  if (nivelEnergia < 20 || nivelAgua < 6) {
    if (eletroliseAtiva) eletroliseAtiva = false;
    if (nivelEnergia < 20) { statusAtual = CRITICO; return; }
  }

  if (nivelEnergia < 50) { statusAtual = ATENCAO; return; }
  if (nivelAgua    < 25) { statusAtual = ATENCAO; return; }
  if (temperatura > 60.0){ statusAtual = ATENCAO; return; }

  statusAtual = OPERACIONAL;
}

// ─── LEDs e buzzer ────────────────────────────────────────
void atualizarSaidas() {
  digitalWrite(PIN_LED_VERDE,    LOW);
  digitalWrite(PIN_LED_AMARELO,  LOW);
  digitalWrite(PIN_LED_VERMELHO, LOW);

  switch (statusAtual) {
    case OPERACIONAL:
      digitalWrite(PIN_LED_VERDE, HIGH);
      digitalWrite(PIN_BUZZER, LOW);
      buzzerLigado = false;
      break;
    case ATENCAO:
      digitalWrite(PIN_LED_AMARELO, HIGH);
      digitalWrite(PIN_BUZZER, LOW);
      buzzerLigado = false;
      break;
    case CRITICO:
      digitalWrite(PIN_LED_VERMELHO, HIGH);
      if (millis() - ultimoBuzzer >= INTERVALO_BUZZER) {
        ultimoBuzzer = millis();
        buzzerLigado = !buzzerLigado;
        digitalWrite(PIN_BUZZER, buzzerLigado ? HIGH : LOW);
      }
      break;
  }
}

// ─── Telas ────────────────────────────────────────────────
void telaPrincipal() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 10, "LunarFuel Alpha");
  oled.drawHLine(0, 13, 128);
  char buf[22];
  snprintf(buf, sizeof(buf), "Agua:    %3d%%", (int)nivelAgua);
  oled.drawStr(0, 26, buf);
  snprintf(buf, sizeof(buf), "Energia: %3d%%", (int)nivelEnergia);
  oled.drawStr(0, 38, buf);
  snprintf(buf, sizeof(buf), "Temp:    %.1fC", temperatura);
  oled.drawStr(0, 50, buf);
  if (modoEmergencia)           oled.drawStr(0, 62, "!! EMERGENCIA !!");
  else if (statusAtual == CRITICO) oled.drawStr(0, 62, "Status: CRITICO        ");
  else if (statusAtual == ATENCAO) oled.drawStr(0, 62, "Status: ATENCAO      ");
  else                             oled.drawStr(0, 62, "Status: OPERACIONAL  ");
  oled.sendBuffer();
}

void telaEletrolise() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 10, ">> Eletrolise ON");
  oled.drawHLine(0, 13, 128);
  char buf[22];
  snprintf(buf, sizeof(buf), "Agua:    %3d%%", (int)nivelAgua);
  oled.drawStr(0, 26, buf);
  snprintf(buf, sizeof(buf), "Energia: %3d%%", (int)nivelEnergia);
  oled.drawStr(0, 38, buf);
  snprintf(buf, sizeof(buf), "H2:      %5.1f%%", h2Produzido);
  oled.drawStr(0, 50, buf);
  snprintf(buf, sizeof(buf), "O2:      %5.1f%%", o2Produzido);
  oled.drawStr(0, 62, buf);
  oled.sendBuffer();
}

void telaBloqueio() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 10, "Eletrolise");
  oled.drawStr(0, 22, "BLOQUEADA");
  oled.drawHLine(0, 26, 128);
  if (nivelEnergia < 20) {
    oled.drawStr(0, 40, "Energia critica");
    oled.drawStr(0, 52, "Min: 20%");
  } else if (nivelAgua < 6) {
    oled.drawStr(0, 40, "Agua insuficiente");
    oled.drawStr(0, 52, "Min: 6%");
  } else {
    oled.drawStr(0, 40, "Recursos baixos");
  }
  oled.sendBuffer();
}

void atualizarDisplay() {
  if (mostrarBloqueio) {
    if (millis() - tempoBloqueio < DURACAO_BLOQUEIO) {
      telaBloqueio(); return;
    } else {
      mostrarBloqueio = false;
    }
  }
  if (eletroliseAtiva) telaEletrolise();
  else                 telaPrincipal();
}

// ─── Loop principal ───────────────────────────────────────
void loop() {
  lerBotoes();
  atualizarSaidas();

  if (millis() - ultimoLoop >= INTERVALO_LOOP) {
    ultimoLoop = millis();
    lerSensores();
    atualizarStatus();
    atualizarSaidas();
    atualizarDisplay();
  }

  // Envio POST a cada 60 segundos
  if (millis() - ultimoEnvioPost >= INTERVALO_POST) {
    ultimoEnvioPost = millis();
    enviarTelemetria();
  }
}