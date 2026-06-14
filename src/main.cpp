#include <Arduino.h>

#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include "DHTesp.h"

const int servoPin = 13;
const int DHT_PIN = 12;
const int PINO_LDR = 27;

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
  servo.attach(servoPin, 500, 2400);
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);

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
    int ldr = analogRead(PINO_LDR);
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

}

void ler_bomba_agua(){

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

}

void acionar_bomba_agua(){

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
  if (!client.connected()) {
    reconnect();
  }
  
  delay(4000); 
}
