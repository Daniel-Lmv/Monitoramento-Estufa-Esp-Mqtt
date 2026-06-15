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

// Variáveis de Estado dos Atuadores
bool LED_State = false;
bool bombaLigada = false; 
bool ventiladorLigado = false;

// Variáveis de Controle Remoto Temporizado (MQTT)
unsigned long tempoComandoRemotoBomba = 0;
unsigned long tempoComandoRemotoVentilador = 0;
unsigned long tempoComandoRemotoLED = 0;

bool bombaEmModoRemoto = false;
bool ventiladorEmModoRemoto = false;
bool ledEmModoRemoto = false;

const unsigned long DURACAO_COMANDO_REMOTO = 10000;

// Variáveis LDR
const float GAMMA = 0.7;
const float RL10 = 50;

// Variáveis Nível de Água Simulada
float nivelAgua = 50.0;       
const float TAXA_CONSUMO = 1.5;
const float TAXA_ENCHIMENTO = 4.0;

// Configurações de Rede e MQTT
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "172.20.10.2";

// Variável para controlar a simulação de água a cada 1 segundo,
unsigned long tempoAnteriorAgua = 0;
const long intervaloAgua = 1000;

// Controle de Tempo para Leitura e Automação (Mais rápido)
unsigned long tempoAnteriorLeitura = 0;
const long intervaloLeitura = 1000; // Lê os sensores e roda automação a cada 1 segundo

// Controle de Tempo para Publicação MQTT
unsigned long tempoAnteriorPublicacao = 0;
const long intervaloPublicacao = 5000; // Publica no MQTT a cada 10 segundos

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
void verificar_temporizadores_remotos();
void callback(char* topic, byte* payload, unsigned int length);

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
  client.setCallback(callback);
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
  bombaLigada = true;
  digitalWrite(PIN_DIR_MOTOR, LOW);

  for(int x = 0; x < stepsPerRevolution; x++){
    digitalWrite(PIN_STEP_MOTOR, HIGH);
    delayMicroseconds(2000);
    digitalWrite(PIN_STEP_MOTOR, LOW);
    delayMicroseconds(2000);
  }
  
}

void desligar_bomba_agua() {
  bombaLigada = false;
}

void acionar_ventilador() {
  ventiladorLigado = true;
  int pos = 0;
  int count = 0;
  while(count != 3){
    for (pos = 0; pos <= 180; pos += 10) { servo.write(pos); delay(50); }
    for (pos = 180; pos >= 0; pos -= 10) { servo.write(pos); delay(50); }
    count += 1;
  }
}

void desligar_ventilador() {
  ventiladorLigado = false;
  servo.write(0);
}

void controlar_led(bool estado) {
  LED_State = estado;
  digitalWrite(PIN_LED, LED_State);
}

// --- FUNÇÃO CALLBACK: PROCESSA COMANDOS VINDOS DO BROKER ---
void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  
  String strTopic = String(topic);
  Serial.printf("\n[MQTT] Comando recebido no tópico [%s]: %s\n", topic, msg.c_str());

  // Comando para a Bomba
  if (strTopic == "comando/estufa2/bomba" && msg == "1") {
    Serial.println("-> Acionamento Remoto: Ligando Bomba por 10s...");
    bombaEmModoRemoto = true;
    tempoComandoRemotoBomba = millis();
    acionar_bomba_agua();
  }
  // Comando para o Ventilador
  else if (strTopic == "comando/estufa2/ventilador" && msg == "1") {
    Serial.println("-> Acionamento Remoto: Ligando Ventilador por 10s...");
    ventiladorEmModoRemoto = true;
    tempoComandoRemotoVentilador = millis();
    acionar_ventilador();
  }
  // Comando para o LED
  else if (strTopic == "comando/estufa2/led") {
    if (msg == "1") {
      Serial.println("-> Acionamento Remoto: Ligando LED por 10s...");
      ledEmModoRemoto = true;
      tempoComandoRemotoLED = millis();
      controlar_led(true);
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentando conexão MQTT...");
    if (client.connect("ESP32-Estufa1")) {
      Serial.println("Conectado ao Broker MQTT!");

      client.subscribe("comando/estufa2/bomba");
      client.subscribe("comando/estufa2/ventilador");
      client.subscribe("comando/estufa2/led");
      Serial.println("-> Inscrito nos tópicos de comando com sucesso!");

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
    client.publish("monitoramento/estufa2/temperatura", String(temp, 2).c_str());
    client.publish("monitoramento/estufa2/umidade", String(hum, 1).c_str());
    client.publish("monitoramento/estufa2/luminosidade", String(lux, 1).c_str());
    client.publish("monitoramento/estufa2/gas", String(gas).c_str());
    client.publish("monitoramento/estufa2/nivel_agua", String(agua, 1).c_str());
    
    // Publicação do Estado Atual dos Atuadores (1 = Ligado, 0 = Desligado)
    client.publish("monitoramento/estufa2/estado_bomba", bombaLigada ? "1" : "0");
    client.publish("monitoramento/estufa2/estado_ventilador", ventiladorLigado ? "1" : "0");
    client.publish("monitoramento/estufa2/estado_led", LED_State ? "1" : "0");

    Serial.println("-> Dados publicados no MQTT com sucesso!");
  }
}

void controlar_automacao(float temp, float agua, float lux, int gas) {
  
// --- 1. AUTOMAÇÃO DA BOMBA ---
  if (!bombaEmModoRemoto) { // Só roda se NÃO estiver sob comando MQTT
    if (agua < 30.0 && !bombaLigada) {
      Serial.println("-> [AUTOMAÇÃO] Água baixa. Ligando bomba...");
      bombaLigada= true;
    } 
    else if (agua > 85.0 && bombaLigada) {
      Serial.println("-> [AUTOMAÇÃO] Nível seguro atingido. Desligando bomba...");
      desligar_bomba_agua(); 
    }

    if(bombaLigada){
      acionar_bomba_agua(); 
    }
  }

  // --- 2. AUTOMAÇÃO DA LÂMPADA ---
  if (!ledEmModoRemoto) { // Só roda se NÃO estiver sob comando MQTT
    if (lux < 200.0 && !LED_State) {
      Serial.println("-> [AUTOMAÇÃO] Luminosidade baixa! Acendendo lâmpada...");
      controlar_led(true);
    } 
    else if (lux >= 400.0 && LED_State) {
      Serial.println("-> [AUTOMAÇÃO] Luminosidade alta. Desligando lâmpada...");
      controlar_led(false);
    }
  }

  // --- 3. AUTOMAÇÃO DO VENTILADOR ---
  if (!ventiladorEmModoRemoto) { // Só roda se NÃO estiver sob comando MQTT
    if (temp > 32.0 || gas > 15) {
      if (temp > 32.0 && gas > 15) Serial.println("-> [ALERTA MÁXIMO] Temperatura E Gás altos!");
      else if (temp > 32.0) Serial.println("-> [AUTOMAÇÃO] Temperatura alta. Ativando ventilador...");
      else Serial.println("-> [ALERTA] Vazamento de gás! Ativando ventilador...");
      acionar_ventilador(); 
    } else{
      ventiladorLigado = false;
    }
  }
}

void verificar_temporizadores_remotos() {
  unsigned long agora = millis();

  // Verifica tempo da Bomba Remota
  if (bombaEmModoRemoto && (agora - tempoComandoRemotoBomba >= DURACAO_COMANDO_REMOTO)) {
    Serial.println("-> [TEMPO ESGOTADO] Comando remoto da Bomba finalizado. Devolvendo para automação.");
    bombaEmModoRemoto = false;
    desligar_bomba_agua(); // Estado padrão de retorno seguro
  }

  // Verifica tempo do Ventilador Remoto
  if (ventiladorEmModoRemoto && (agora - tempoComandoRemotoVentilador >= DURACAO_COMANDO_REMOTO)) {
    Serial.println("-> [TEMPO ESGOTADO] Comando remoto do Ventilador finalizado. Devolvendo para automação.");
    ventiladorEmModoRemoto = false;
    desligar_ventilador();
  }

  // Verifica tempo do LED Remoto
  if (ledEmModoRemoto && (agora - tempoComandoRemotoLED >= DURACAO_COMANDO_REMOTO)) {
    Serial.println("-> [TEMPO ESGOTADO] Comando remoto do LED finalizado. Devolvendo para automação.");
    ledEmModoRemoto = false;
    // O LED simplesmente volta a aceitar o controle da automação no próximo ciclo
  }
}

void loop() {
  // Mantém MQTT ativo
  if (!client.connected()) reconnect();
  client.loop(); 

  // Simulação contínua do nível da água
  atualizar_nivel_agua();

  verificar_temporizadores_remotos();

  unsigned long tempoAtual = millis();

  // --- CRONÔMETRO 1: LEITURA E AUTOMAÇÃO (A cada 1 segundo) ---
  if (tempoAtual - tempoAnteriorLeitura >= intervaloLeitura) {
    tempoAnteriorLeitura = tempoAtual; 

    // Atualiza as variáveis globais com os dados novos
    t = ler_temp(); h = ler_humidade(); l = obter_luminosidade(); g = obter_gas();
    
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