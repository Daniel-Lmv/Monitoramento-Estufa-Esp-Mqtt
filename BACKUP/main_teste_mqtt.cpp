#include <WiFi.h>
#include <PubSubClient.h>

const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* mqtt_server = "192.168.0.76";

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  client.setServer(mqtt_server, 1883);
}

void reconnect() {
  while (!client.connected()) {
    client.connect("ESP32Publisher");
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }

  float temperatura = random(2000, 3000) / 100.0;

  client.publish(
      "casa/sala/temperatura",
      String(temperatura).c_str()
  );

  Serial.println(temperatura);

  delay(2000);
}