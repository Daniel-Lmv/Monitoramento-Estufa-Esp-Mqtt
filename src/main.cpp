#include <Arduino.h>

#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include "DHTesp.h"

const int PIN_SERVO = 13;
const int PIN_DHT = 12;
const int PIN_LDR = 27;
const int PIN_LED = 23;
bool LED_State = false;
const int PIN_DIR_MOTOR = 15;
const int PIN_STEP_MOTOR = 2;
const int stepsPerRevolution = 200;

const float GAMMA = 0.7;
const float RL10 = 50;

const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* mqtt_server = "192.168.0.76";

Servo servo;
DHTesp dhtSensor;
WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);
  servo.attach(PIN_SERVO, 500, 2400);
  dhtSensor.setup(PIN_DHT, DHTesp::DHT22);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_DIR_MOTOR, OUTPUT);
  pinMode(PIN_STEP_MOTOR, OUTPUT);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  client.setServer(mqtt_server, 1883);
}

void ler_temp_humidade(){
  TempAndHumidity  data = dhtSensor.getTempAndHumidity();
  Serial.println("Temp: " + String(data.temperature, 2) + "°C");
  Serial.println("Humidity: " + String(data.humidity, 1) + "%");
  Serial.println("---");
}

void ler_luminosidade(){
    int ldr = analogRead(PIN_LDR);
    float brilho;

    ldr = map(ldr, 4095, 0, 1024, 0);

    if (ldr <= 0)
        return;

    float tensao = ldr / 1024.0 * 5.0;

    if (tensao >= 4.99)
        return;

    float resistencia = 2000 * tensao / (1 - tensao / 5);

    brilho = pow(RL10 * 1000.0 * pow(10.0, GAMMA) / resistencia, 1.0 / GAMMA);
    Serial.println("Lux: " + String(brilho, 1));
}

void ler_gas(){
  int gasvalue = analogRead(32);
  
  int porcentagemGas = map(gasvalue, 843, 4041, 0, 100);
  
  Serial.print("Leitura Bruta: ");
  Serial.print(gasvalue);
  
  Serial.print("  |  Nivel de Gas: ");
  Serial.print(porcentagemGas);
  Serial.println("%");
}

void ler_nivel_agua(){

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

void acionar_lampada(){
  LED_State = !LED_State;
  digitalWrite(PIN_LED, LED_State);
  Serial.print("Estou aqui");
}

void acionar_bomba_agua(){
  digitalWrite(PIN_DIR_MOTOR, LOW);

  for(int x= 0; x < stepsPerRevolution; x++)
  {
    digitalWrite(PIN_STEP_MOTOR, HIGH);
    delayMicroseconds(2000);
    digitalWrite(PIN_STEP_MOTOR, LOW);
    delayMicroseconds(2000);
  } 
  delay(100);
}

void reconnect() {
  while (!client.connected()) {
    client.connect("ESP32-Estufa1");
  }
}

void publicar(){
  client.publish("casa/sala/temperatura", "teste");
}

void loop() {
  /**
  if (!client.connected()) {
    reconnect();
  } 
  */

  ler_gas();
  delay(4000); 
}
