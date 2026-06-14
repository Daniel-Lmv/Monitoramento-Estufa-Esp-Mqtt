#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include "DHTesp.h"

// Definições de Pinos
const int PIN_SERVO = 13;
const int PIN_DHT = 12;
const int PIN_LDR = 33;
const int PIN_LED = 23;
const int PIN_DIR_MOTOR = 15;
const int PIN_STEP_MOTOR = 2;
const int stepsPerRevolution = 200;

bool LED_State = false;

// Variáveis LDR
const float GAMMA = 0.7;
const float RL10 = 50;

// Variáveis Nível de Água Simulada
float nivelAgua = 50.0;       
const float TAXA_CONSUMO = 1.5;
const float TAXA_ENCHIMENTO = 4.0;
bool bombaLigada = false; // Controle de estado da bomba 

// Configurações de Rede e MQTT
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "192.168.0.76";

// Variável para controlar a simulação de água a cada 1 segundo
unsigned long tempoAnteriorAgua = 0;
const long intervaloAgua = 1000;

// Controle de Tempo para Leitura e Automação (Mais rápido)
unsigned long tempoAnteriorLeitura = 0;
const long intervaloLeitura = 1000; // Lê os sensores e roda automação a cada 1 segundo

// Controle de Tempo para Publicação MQTT
unsigned long tempoAnteriorPublicacao = 0;
const long intervaloPublicacao = 10000; // Publica no MQTT a cada 10 segundos

// Variáveis globais para armazenar as últimas leituras (compartilhadas entre as funções)
float t = 24.0, h = 50.0, l = 500.0;
int g = 0;

Servo servo;
DHTesp dhtSensor;
WiFiClient espClient;
PubSubClient client(espClient);

void ler_temp_humidade();
void ler_luminosidade();
void ler_gas();
void ler_nivel_agua();
void acionar_ventilador();
void acionar_lampada();
void acionar_bomba_agua();
void reconnect();
void publicar_dados(float temp, float hum, float lux, int gas, float agua);
void controlar_automacao(float temp, float agua, float lux, int gas);

void setup() {
  Serial.begin(115200);
  servo.attach(PIN_SERVO, 500, 2400);
  dhtSensor.setup(PIN_DHT, DHTesp::DHT22);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_DIR_MOTOR, OUTPUT);
  pinMode(PIN_STEP_MOTOR, OUTPUT);

  Serial.print("Conectando ao WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi Conectado!");

  client.setServer(mqtt_server, 1883);
}

float ler_temp() {
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  return data.temperature;
}

float ler_humidade() {
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  return data.humidity;
}

float obter_luminosidade(){
    int ldr = analogRead(PIN_LDR);
    ldr = map(ldr, 4095, 0, 1024, 0);
    if (ldr <= 0) return 0;
    float tensao = ldr / 1024.0 * 5.0;
    if (tensao >= 4.99) return 0;
    float resistencia = 2000 * tensao / (1 - tensao / 5);
    return pow(RL10 * 1000.0 * pow(10.0, GAMMA) / resistencia, 1.0 / GAMMA);
}

int obter_gas(){
  int gasvalue = analogRead(32);
  return map(gasvalue, 843, 4041, 0, 100);
}

void atualizar_nivel_agua(){
  unsigned long tempoAtual = millis();
  
  // A simulação agora só avança se passar 1 segundo
  if (tempoAtual - tempoAnteriorAgua >= intervaloAgua) {
    tempoAnteriorAgua = tempoAtual; // Reseta o cronômetro da água

    if (bombaLigada) {
      nivelAgua = nivelAgua + TAXA_ENCHIMENTO - TAXA_CONSUMO;
    } else {
      nivelAgua = nivelAgua - TAXA_CONSUMO;
    }
    
    // Limita as variáveis entre 0 e 100
    if (nivelAgua > 100.0) nivelAgua = 100.0;
    if (nivelAgua < 0.0)   nivelAgua = 0.0;
  }
}

void acionar_bomba_agua(){
  bombaLigada = true; // Sinaliza para a simulação que está enchendo
  digitalWrite(PIN_DIR_MOTOR, LOW);

  for(int x= 0; x < stepsPerRevolution; x++) {
    digitalWrite(PIN_STEP_MOTOR, HIGH);
    delayMicroseconds(2000);
    digitalWrite(PIN_STEP_MOTOR, LOW);
    delayMicroseconds(2000);
  } 
}

void desligar_bomba_agua() {
  bombaLigada = false;
}

void acionar_ventilador(){
  int pos = 0;
  int count = 0;

  while(count != 3){
    for (pos = 0; pos <= 180; pos += 10) {
      servo.write(pos);
      delay(50);
    }
    for (pos = 180; pos >= 0; pos -= 10) {
      servo.write(pos);
      delay(50);
    }
    count += 1;
  }
  
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentando conexão MQTT...");
    if (client.connect("ESP32-Estufa1")) {
      Serial.println("Conectado ao Broker MQTT!");
    } else {
      Serial.print("Falhou, rc=");
      Serial.print(client.state());
      Serial.println(" Tentando novamente em 2 segundos...");
      delay(2000); // Pequeno delay na tentativa de reconexão
    }
  }
}

void publicar_dados(float temp, float hum, float lux, int gas, float agua){
  if (client.connected()) {
    client.publish("monitoramento/estufa1/temperatura", String(temp, 2).c_str());
    client.publish("monitoramento/estufa1/umidade", String(hum, 1).c_str());
    client.publish("monitoramento/estufa1/luminosidade", String(lux, 1).c_str());
    client.publish("monitoramento/estufa1/gas", String(gas).c_str());
    client.publish("monitoramento/estufa1/nivel_agua", String(agua, 1).c_str());
    
    Serial.println("-> Dados publicados no MQTT com sucesso!");
  }
}

void controlar_automacao(float temp, float agua, float lux, int gas) {
  
  // --- 1. CONTROLE DO NÍVEL DE ÁGUA ---
  if (agua < 30.0) {
    // Se a bomba já estiver ligada, não printa de novo
    if (!bombaLigada) {
      Serial.println("-> [AUTOMAÇÃO] Água baixa. Ligando bomba...");
    }
    acionar_bomba_agua(); // Ativa a bomba física e muda 'bombaLigada' para true
  } 
  else if (agua > 85.0) {
    if (bombaLigada) {
      Serial.println("-> [AUTOMAÇÃO] Nível seguro atingido. Desligando bomba...");
      desligar_bomba_agua(); // Muda 'bombaLigada' para false
    }
  }

  // --- 2. CONTROLE DA LÂMPADA (Luminosidade) ---
  // Se a luminosidade estiver baixa (ex: menos de 200 Lux) e a lâmpada estiver apagada
  if (lux < 200.0 && !LED_State) {
    Serial.println("-> [AUTOMAÇÃO] Luminosidade baixa! Acendendo lâmpada...");
    LED_State = true;
    digitalWrite(PIN_LED, LED_State);
  } 
  // Se a luminosidade estiver alta (ex: mais de 400 Lux) e a lâmpada estiver acesa
  else if (lux >= 400.0 && LED_State) {
    Serial.println("-> [AUTOMAÇÃO] Luminosidade alta detectada. Desligando lâmpada...");
    LED_State = false;
    digitalWrite(PIN_LED, LED_State);
  }

  // --- 3. CONTROLE DO VENTILADOR (Temperatura OU Gás) ---
  // Liga se a temperatura estiver alta (ex: > 32°C) OU se houver gás detectado (ex: > 15%)
  if (temp > 32.0 || gas > 15) {
    if (temp > 32.0 && gas > 15) {
      Serial.println("-> [ALERTA MÁXIMO] Temperatura E Gás altos! Ativando ventilador de emergência...");
    } else if (temp > 32.0) {
      Serial.println("-> [AUTOMAÇÃO] Temperatura alta. Ativando ventilador...");
    } else {
      Serial.println("-> [ALERTA] Vazamento de gás! Ativando ventilador para dissipar...");
    }
    
    acionar_ventilador(); 
  }
}

void loop() {
  // Mantém MQTT ativo
  //if (!client.connected()) reconnect();
  //client.loop(); 

  // Simulação contínua do nível da água
  atualizar_nivel_agua();

  unsigned long tempoAtual = millis();

  // --- CRONÔMETRO 1: LEITURA E AUTOMAÇÃO (A cada 1 segundo) ---
  if (tempoAtual - tempoAnteriorLeitura >= intervaloLeitura) {
    tempoAnteriorLeitura = tempoAtual; 

    // Atualiza as variáveis globais com os dados novos
    t = ler_temp();
    h = ler_humidade();
    l = obter_luminosidade();
    g = obter_gas();
    
    Serial.println("");
    Serial.printf("Temp: %.2f C | Lux: %.1f lx | Gás: %d%% | Água: %.1f%%\n", t, l, g, nivelAgua);
    Serial.println("");
    // Roda a automação imediatamente para proteger o sistema rápido
    controlar_automacao(t, nivelAgua, l, g);
  }

  // --- CRONÔMETRO 2: PUBLICAÇÃO MQTT (A cada 10 segundos) ---
  if (tempoAtual - tempoAnteriorPublicacao >= intervaloPublicacao) {
    tempoAnteriorPublicacao = tempoAtual; 

    // Print de Monitoramento local
    Serial.println("=======================================================");
    Serial.println(">> ENVIANDO RELATÓRIO PERIÓDICO PARA O BROKER <<");
    Serial.printf("Temp: %.2f C | Lux: %.1f lx | Gás: %d%% | Água: %.1f%%", t, l, g, nivelAgua);
    Serial.println("");
    Serial.println("=======================================================");
    

    // Publica os últimos dados salvos
    publicar_dados(t, h, l, g, nivelAgua);
  }
}