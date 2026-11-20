#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <HX711.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "config.h"

// Definición de Pines
const int PIN_SERVO = 18;
const int PIN_DT = 19;
const int PIN_SCK = 4;

// Objetos
Servo servoCompuerta;
HX711 escala;
LiquidCrystal_I2C lcd(0x27, 16, 2); // Cambiar a 0x3F si la pantalla lo requiere

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long ultimoEnvio = 0;
const long intervaloEnvio = 5000; // Enviar peso cada 5 segundos

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando a Wi-Fi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado.");
}

void callback(char* topic, byte* payload, unsigned int length) {
  String mensaje = "";
  for (unsigned int i = 0; i < length; i++) {
    mensaje += (char)payload[i];
  }

  Serial.print("Mensaje recibido [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(mensaje);

  if (String(topic) == MQTT_TOPIC_CMD && mensaje == "DISPENSAR") {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sirviendo comida");

    servoCompuerta.attach(PIN_SERVO, 500, 2400);
    servoCompuerta.write(90); // Abre compuerta
    delay(3000);              // Tiempo de caída
    servoCompuerta.write(0);  // Cierra compuerta
    delay(200);
    servoCompuerta.detach();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sistema Listo");
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conexión MQTT...");
    if (client.connect("ESP32DispensadorClient", MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("conectado");
      client.subscribe(MQTT_TOPIC_CMD);
    } else {
      Serial.print("falló, rc=");
      Serial.print(client.state());
      Serial.println(" reintentando en 5s");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Inicializar Pantalla LCD
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  delay(200);
  lcd.setCursor(0, 0);
  lcd.print("Iniciando...");

  // Inicializar Servomotor
  ESP32PWM::allocateTimer(0);
  servoCompuerta.setPeriodHertz(50);
  servoCompuerta.attach(PIN_SERVO, 500, 2400);
  servoCompuerta.write(0);
  delay(300);
  servoCompuerta.detach();

  // Inicializar Sensor de Peso
  escala.begin(PIN_DT, PIN_SCK);
  escala.set_scale(420.0);
  delay(500);
  escala.tare();

  // Red
  setup_wifi();
  client.setServer(MQTT_BROKER_IP, MQTT_BROKER_PORT);
  client.setCallback(callback);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sistema Listo");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi();
  }

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long ahora = millis();
  if (ahora - ultimoEnvio > intervaloEnvio) {
    ultimoEnvio = ahora;

    float pesoActual = escala.is_ready() ? escala.get_units(5) : 0.0;
    Serial.print("Peso en plato: ");
    Serial.print(pesoActual, 1);
    Serial.println(" g");

    lcd.setCursor(0, 1);
    lcd.print("Peso: ");
    lcd.print(pesoActual, 1);
    lcd.print("g    ");

    if (client.connected()) {
      char buffer[10];
      dtostrf(pesoActual, 4, 1, buffer);
      client.publish(MQTT_TOPIC_PESO, buffer);

      if (pesoActual < 20.0) {
        client.publish(MQTT_TOPIC_ALERTA, "PLATO_VACIO");
      }
    }
  }
}