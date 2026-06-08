# 🚀 Orbitank Alpha — Sistema de Telemetria e Controle de Eletrólise

O **Orbitank** é um protótipo de sistema embarcado projetado para gerenciar e monitorar uma estação automatizada de eletrólise para geração de combustível lunar (Hidrogênio e Oxigênio) a partir de água simulada. O projeto integra sensores, atuadores, display local e conectividade de rede para criar um gêmeo digital interativo.

> ⚠️ **Nota Importante:** Este projeto foi projetado e otimizado exclusivamente para fins de **simulação acadêmica/educacional** (ex: Wokwi). Ele contém configurações específicas para ambientes virtuais e não se destina ao uso em hardware real sem as devidas modificações de segurança e infraestrutura de rede.

---

## 📋 Sumário
1. [Funcionamento do Sistema](#-funcionamento-do-sistema)
2. [Arquitetura de Hardware Simulado](#-arquitetura-de-hardware-simulado)
3. [Mapeamento de Pinos (Pinout)](#-mapeamento-de-pinos-pinout)
4. [Lógica de Estados e Alertas](#-lógica-de-estados-e-alertas)
5. [Documentação da API Local (Endpoints)](#-documentação-da-api-local-endpoints)
6. [Integração com Nuvem (Payload de Telemetria)](#-integração-com-nuvem-payload-de-telemetria)
7. [Como Executar a Simulação](#-como-executar-a-simulação)

---

## ⚙️ Funcionamento do Sistema

O firmware roda em um laço de controle não-bloqueante baseado na função `millis()`, garantindo que a amostragem de dados, a interface com o usuário e a comunicação de rede ocorram de maneira assíncrona e fluida.

### O Processo de Eletrólise
* **Ativação:** Pressionando o Botão de Eletrólise, o processo inicia se o sistema tiver **Água $\ge$ 6%** e **Energia $\ge$ 20%**.
* **Consumo e Produção:** Enquanto ativa, a eletrólise consome **1.0% de água** e **1.5% de energia** por ciclo de amostragem (500ms), gerando em contrapartida **0.5% de $H_2$** e **1.0% de $O_2$**.
* **Bloqueio de Segurança:** Se os recursos caírem abaixo do limite mínimo durante o processo, a eletrólise é interrompida imediatamente, e uma tela de bloqueio temporária (3 segundos) é exibida no display OLED detalhando o motivo.

### Controle de Recursos (Modo Manual)
Como se trata de uma simulação, quando a eletrólise está desligada, o usuário pode interagir com os botões de reabastecimento para incrementar artificialmente os níveis de água (+5.0%) e energia (+5.0%).

### Parada de Emergência
O sistema conta com uma rotina de interrupção lógica através do Botão de Emergência. Ao ser acionado, ele desliga imediatamente a eletrólise (caso ativa) e força a estação a entrar em estado **CRÍTICO**, ativando alarmes visuais e sonoros até que o botão seja pressionado novamente.

---

## 🏗️ Arquitetura de Hardware Simulado

O ecossistema periférico simula os seguintes componentes conectados a uma placa **ESP32**:

* **Microcontrolador:** ESP32 (módulo Wi-Fi e processamento principal).
* **Sensor de Clima:** DHT22 (Leitura de temperatura ambiente e umidade relativa).
* **Interface Visual Local:** Display OLED SSD1306 (128x64 pixels, comunicação via barramento I2C).
* **Atuadores de Alerta:** 3 LEDs coloridos (Verde, Amarelo, Vermelho) e 1 Buzzer Piezoelétrico.
* **Entradas de Controle:** 4 Botões do tipo Push-Button configurados com resistor *Pulldown* interno.

---

## 📌 Mapeamento de Pinos (Pinout)

| Componente | Pino ESP32 | Tipo de Sinal | Descrição / Função |
| :--- | :---: | :---: | :--- |
| **DHT22** | `GPIO 4` | Digital (I/O) | Sensor de Temperatura e Umidade ambiente |
| **OLED SDA** | `GPIO 21` | Digital (I2C) | Linha de Dados do Display |
| **OLED SCL** | `GPIO 22` | Digital (I2C) | Linha de Clock do Display |
| **BTN_AGUA** | `GPIO 32` | Entrada (Pulldown) | Incrementa simuladamente +5% de Água |
| **BTN_ENERGIA**| `GPIO 33` | Entrada (Pulldown) | Incrementa simuladamente +5% de Energia |
| **BTN_ELETRO** | `GPIO 18` | Entrada (Pulldown) | Alterna o estado da Eletrólise (On/Off) |
| **BTN_EMERG** | `GPIO 19` | Entrada (Pulldown) | Ativa/Desativa o modo de emergência global |
| **LED_VERDE** | `GPIO 25` | Saída Digital | Indica status **OPERACIONAL** |
| **LED_AMARELO**| `GPIO 26` | Saída Digital | Indica status de **ATENÇÃO** |
| **LED_VERMELHO**| `GPIO 27` | Saída Digital | Indica status **CRÍTICO** / Emergência |
| **BUZZER** | `GPIO 14` | Saída Digital | Alarme sonoro oscilante para eventos críticos |

---

## 🚦 Lógica de Estados e Alertas

O gerenciador de segurança do firmware classifica a telemetria em três faixas operacionais baseadas em severidade:

### 🟢 OPERACIONAL
* **Condição:** Recursos normais (Energia $\ge$ 50%, Água $\ge$ 25% e Temperatura $\le$ 60°C).
* **Comportamento:** LED Verde ligado. Demais saídas desligadas.

### 🟡 ATENÇÃO (WARNING)
* **Condição:** Energia abaixo de 50% **OU** Água abaixo de 25% **OU** Temperatura acima de 60°C.
* **Comportamento:** LED Amarelo ligado. Emite avisos preventivos na API e no Dashboard.

### 🔴 CRÍTICO (CRITICAL / EMERGENCY)
* **Condição:** Modo de emergência ativado manualmente **OU** Energia abaixo de 20% **OU** Água abaixo de 6%.
* **Comportamento:** LED Vermelho ligado. O Buzzer é acionado de forma intermitente (intervalos de 200ms) gerando um efeito sonoro de alerta.

---

## 🌐 Documentação da API Local (Endpoints)

Ao se conectar à rede Wi-Fi local, o ESP32 levanta um servidor HTTP na porta 80. Os endpoints disponíveis para consulta interna via navegador ou ferramentas como Postman são:

### 1. Dashboard Web
* **Rota:** `GET /`
* **Tipo de resposta:** `text/html`
* **Descrição:** Interface web simplificada com atualização automática em tempo real (Refresh a cada 2 segundos) contendo o sumário visual de todos os sensores e o estado operacional da planta.

### 2. Dados de Telemetria Geral
* **Rota:** `GET /api/telemetria`
* **Tipo de resposta:** `application/json`
* **Exemplo de Resposta:**
```json
{
  "agua": 75.0,
  "energia": 48.5,
  "temperatura": 26.4,
  "umidade": 62.0,
  "h2Produzido": 5.0,
  "o2Produzido": 10.0
}
```

### 3. Status do Sistema
* **Rota:** `GET /api/status`
* **Tipo de resposta:** `application/json`
* **Exemplo de Resposta:**
```json
{
  "statusModulo": "ONLINE",
  "nivelRisco": "LOW",
  "eletroliseAtiva": true,
  "eletroliseBloqueada": false
}
```

### 4. Alertas e Emergências
* **Rota:** `GET /api/alertas`
* **Tipo de resposta:** `application/json`
* **Exemplo de Resposta:**
```json
{
  "alertaAtivo": true,
  "tipoAlerta": "LOW_ENERGY",
  "mensagemAlerta": "Nivel de energia critico: abaixo de 20%",
  "modoEmergencia": false
}
```

## Integração com a API Java

Além dos endpoints locais, o ESP32 envia automaticamente um POST para a API Java a cada 60 segundos.

- **Rota externa:** `POST /iot/telemetry`
- **URL:** `https://orbitank-javaadvanced-gs.onrender.com/iot/telemetry`
- **Formato:** JSON

Exemplo de payload:

```json
{
  "deviceId": "ESP32-STATION-01",
  "stationCode": "1",
  "timestamp": 1780498800,
  "iceLevelPercent": 80.0,
  "waterLevelPercent": 50.0,
  "hydrogenLevelPercent": 25.0,
  "oxygenLevelPercent": 90.0,
  "energyLevelPercent": 72.5,
  "temperatureCelsius": 25.3,
  "humidityPercent": 61.0,
  "electrolysisActive": false,
  "electrolysisBlocked": false,
  "blockReason": "",
  "hydrogenGeneratedPercent": 25.0,
  "oxygenGeneratedPercent": 90.0,
  "emergencyMode": false,
  "moduleStatus": "ONLINE",
  "riskLevel": "LOW",
  "alertActive": false,
  "alertType": "NONE",
  "alertMessage": "",
  "alertSeverity": "NONE"
}

### Link DashBoard
http://localhost:8080/
