/*
 * NODO AUTOBÚS AUT312 (ESP32-S3), práctica 2 de NAPIoT.
 *
 * Hace lo mismo que la pestaña "Autobús AUT312" de Node-RED de la práctica 1,
 * pero en una ESP32-S3 real conectada por WiFi al broker MQTT. Como todavía no
 * tenemos los componentes:
 *
 *   - Pulsador del conductor: "p" en el monitor serie, cuando la persona ha
 *     subido.
 *   - Zumbador: pitidos intermitentes en su pin (se oirían con el zumbador del
 *     kit conectado) y su estado por el monitor serie.
 *   - Servo de la rampa: señal PWM de 50 Hz real en su pin. La rampa baja y sube
 *     poco a poco (unos 3 s), como el servo de verdad.
 *   - LCD 8x2: sus dos líneas se escriben por el monitor serie.
 *   - LED RGB de la placa: amarillo con asignación, azul con el zumbador sonando,
 *     verde con la rampa bajada, rojo si no hay conexión.
 *   - LED verde de GPIO21 (un solo color): parpadeo lento con asignación, rápido con
 *     el zumbador, fijo con la rampa bajada, apagado en reposo y muy rápido sin conexión.
 *
 * Todo se maneja a mano desde el monitor serie (115200 baudios, escribe "h").
 * Los actuadores (zumbador y rampa) también se controlan desde el dashboard de
 * Node-RED por el topic autobus/AUT312/control.
 *
 * Librerías (Library Manager): PubSubClient (Nick O'Leary) y ArduinoJson (Benoit Blanchon).
 * Placa: "ESP32S3 Dev Module" del paquete esp32 de Espressif.
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Todo lo que sale por el monitor serie lleva delante [AUTOBUS], para distinguir
// las dos placas cuando se ven los dos monitores a la vez.
class SalidaConPrefijo : public Print {
  const char* prefijo;
  bool inicioDeLinea = true;
public:
  explicit SalidaConPrefijo(const char* p) : prefijo(p) {}
  size_t write(uint8_t c) override { return write(&c, 1); }
  // Se manda cada trozo de línea de una vez: byte a byte el USB pierde caracteres
  size_t write(const uint8_t* buf, size_t n) override {
    size_t i = 0;
    while (i < n) {
      if (inicioDeLinea && buf[i] != '\n' && buf[i] != '\r') { Serial.print(prefijo); inicioDeLinea = false; }
      size_t j = i;
      while (j < n && buf[j] != '\n') j++;
      if (j < n) { j++; inicioDeLinea = true; }   // incluye el salto de línea
      Serial.write(buf + i, j - i);
      i = j;
    }
    return n;
  }
};
SalidaConPrefijo Consola("[AUTOBUS] ");

// WiFi (tiene que ser de 2,4 GHz). WIFI_SSID y WIFI_PASSWORD están en secretos.h,
// que no se sube a GitHub: copia secretos.h.ejemplo como secretos.h y pon tu red.
#include "secretos.h"

// ---------------------------------------------------------------------------
// Configuración (la misma que las variables de entorno de Node-RED)
// ---------------------------------------------------------------------------
const char* MQTT_BROKER  = "test.mosquitto.org";
const uint16_t MQTT_PORT = 1883;
const char* TOPIC_PREFIX = "napiot26paradabus7k3";
const char* BUS_ID       = "AUT312";
const uint32_t PERIODO_TELEMETRIA_MS = 5000;

// ---------------------------------------------------------------------------
// Pines de la ESP32-S3. Los del Fritzing (FireBeetle ESP32-E) no existen en la
// S3, así que se han cambiado.
// ---------------------------------------------------------------------------
#if CONFIG_IDF_TARGET_ESP32S3
const char* PLACA       = "ESP32-S3";
const int PIN_PULSADOR  = 4;    // pulsador del kit (no se lee: se pulsa con "p") [Fritzing: GPIO4]
const int PIN_ZUMBADOR  = 5;    // zumbador                                   [Fritzing: GPIO25]
const int PIN_SERVO     = 6;    // señal del servo (alimentado a 5 V)         [Fritzing: GPIO15]
const int PIN_I2C_SDA   = 8;    // pantalla I2C (no se usa todavía)            [Fritzing: GPIO21]
const int PIN_I2C_SCL   = 9;    //                                             [Fritzing: GPIO27]
#else
#error "Placa no soportada: elige ESP32S3 Dev Module"
#endif
// LED de estado. No sabemos el modelo exacto de placa, así que se manda a los
// sitios habituales (en la placa que no lo tenga, ese pin queda libre):
//   RGB en GPIO48 (ESP32-S3-DevKitC-1 y clones N16R8) o GPIO38 (DevKitC-1 v1.1)
//   LED de un color en GPIO21 (FireBeetle 2 ESP32-S3 y nuestras ESP32-S3 N16R8)
const int PINES_LED_RGB[] = { 48, 38 };
const int PIN_LED_SIMPLE  = 21;

// ---------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
char clientId[32];
String base;   // "napiot26paradabus7k3/autobus/AUT312/"

struct {
  bool hay = false;
  char solicitud[8] = "";
  char parada[8] = "";
  char linea[8] = "";
  char asistencia[12] = "";
} asignacion;

bool enServicio = false;
bool zumbador = false;
bool rampa = false;             // posición pedida: true = desplegada (90 grados)
float anguloServo = 0;          // posición actual del servo
char lcd[2][9] = { "", "" };

uint32_t ultimaTelemetria = 0, ultimoReintentoMqtt = 0;

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------
void led(uint8_t r, uint8_t g, uint8_t b) {
  static uint32_t actual = 0xFFFFFFFF;   // solo se escribe si cambia el color
  uint32_t c = (r << 16) | (g << 8) | b;
  if (c == actual) return;
  actual = c;
  for (int p : PINES_LED_RGB) rgbLedWrite(p, r, g, b);
}

// LED verde de GPIO21: es de un solo color, así que cada estado se distingue por cómo parpadea
enum PatronLed { LED_APAGADO, LED_FIJO, LED_LENTO, LED_RAPIDO, LED_SIN_RED };
void ledSimple(uint8_t p) {
  uint32_t t = millis();
  bool on = p == LED_FIJO
         || (p == LED_LENTO   && (t / 500) % 2)    // 1 Hz
         || (p == LED_RAPIDO  && (t / 120) % 2)    // unos 4 Hz
         || (p == LED_SIN_RED && (t / 60) % 2);    // muy rápido
  static int ultimo = -1;
  if (on != ultimo) { digitalWrite(PIN_LED_SIMPLE, on ? HIGH : LOW); ultimo = on; }
}

void publicar(const char* tema, JsonDocument& doc) {
  char buf[512];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  String topic = base + tema;
  mqtt.publish(topic.c_str(), (const uint8_t*)buf, n, false);
  Consola.printf("  [MQTT >] %s %s\n", topic.c_str(), buf);
}

// ---------------------------------------------------------------------------
// Actuadores
// ---------------------------------------------------------------------------
void escribirServo(float grados) {
  // 50 Hz: 0 grados = 0,5 ms, 180 grados = 2,5 ms. Resolución de 14 bits (16384 = 20 ms)
  float us = 500 + grados * 2000.0f / 180.0f;
  ledcWrite(PIN_SERVO, (uint32_t)(us * 16384.0f / 20000.0f));
}

void ponerZumbador(bool on) {
  if (on == zumbador) return;
  zumbador = on;
  if (!on) noTone(PIN_ZUMBADOR);
  Consola.printf("[ZUMBADOR] %s\n", on ? "SONANDO" : "apagado");
}

void ponerRampa(bool desplegada) {
  if (desplegada == rampa) return;
  rampa = desplegada;
  Consola.printf("[SERVO] %s la rampa (hacia %d grados)\n", desplegada ? "bajando" : "subiendo", desplegada ? 90 : 0);
}

void actualizarLcd() {
  char l1[9], l2[9];
  if (asignacion.hay) {
    snprintf(l1, sizeof(l1), "%s %s", asignacion.parada, asignacion.linea);
    snprintf(l2, sizeof(l2), "%s", strcmp(asignacion.asistencia, "movilidad") == 0 ? "RAMPA" : "VISUAL");
  } else {
    l1[0] = 0; l2[0] = 0;
  }
  if (strcmp(l1, lcd[0]) || strcmp(l2, lcd[1])) {
    strlcpy(lcd[0], l1, 9); strlcpy(lcd[1], l2, 9);
    Consola.printf("[LCD] [%-8s][%-8s]\n", lcd[0], lcd[1]);
  }
}

// Pitidos intermitentes (0,4 s sí, 0,4 s no) mientras el zumbador está activo
void tareaZumbador() {
  static uint32_t ultimo = 0;
  static bool fase = false;
  if (!zumbador) return;
  if (millis() - ultimo >= 400) {
    ultimo = millis();
    fase = !fase;
    if (fase) tone(PIN_ZUMBADOR, 2300); else noTone(PIN_ZUMBADOR);
  }
}

// El servo se mueve 1,5 grados cada 50 ms: 90 grados en 3 s
void tareaServo() {
  static uint32_t ultimo = 0;
  if (millis() - ultimo < 50) return;
  ultimo = millis();
  float objetivo = rampa ? 90 : 0;
  if (anguloServo == objetivo) return;
  anguloServo += (objetivo > anguloServo) ? min(1.5f, objetivo - anguloServo) : -min(1.5f, anguloServo - objetivo);
  escribirServo(anguloServo);
  if (anguloServo == objetivo)
    Consola.printf("[SERVO] %.0f grados, rampa %s\n", anguloServo, rampa ? "desplegada" : "recogida");
}

void parar() {
  ponerZumbador(false);
  ponerRampa(false);
  enServicio = false;
}

// ---------------------------------------------------------------------------
// Sensor: pulsador del conductor
// ---------------------------------------------------------------------------
void pulsador() {
  Consola.println("[PULSADOR] el conductor pulsa el botón");
  if (enServicio && asignacion.hay) {
    JsonDocument doc;
    doc["tipo"] = "recogido";
    doc["solicitud"] = asignacion.solicitud;
    publicar("evento", doc);
    Consola.printf("  recogida confirmada (%s)\n", asignacion.solicitud);
    parar();
  } else {
    Consola.println("  pulsador ignorado: no hay ningún servicio en curso");
  }
}

// ---------------------------------------------------------------------------
// Mensajes MQTT recibidos
// ---------------------------------------------------------------------------
void alRecibirAsistencia(JsonDocument& doc, bool ok) {
  const char* sol = ok ? (doc["solicitud"] | "") : "";
  if (sol[0]) {
    asignacion.hay = true;
    strlcpy(asignacion.solicitud, sol, sizeof(asignacion.solicitud));
    strlcpy(asignacion.parada, doc["parada"] | "", sizeof(asignacion.parada));
    strlcpy(asignacion.linea, doc["linea"] | "", sizeof(asignacion.linea));
    strlcpy(asignacion.asistencia, doc["asistencia"] | "visual", sizeof(asignacion.asistencia));
    Consola.printf("[NUBE] asignación: %s %s, %s\n", asignacion.parada, asignacion.linea, asignacion.asistencia);
  } else {
    // Retenido borrado: ya no hay solicitud
    if (asignacion.hay) Consola.println("[NUBE] asignación retirada");
    asignacion.hay = false;
    parar();
  }
  actualizarLcd();
}

void alRecibirOrden(JsonDocument& doc) {
  const char* tipo = doc["tipo"] | "";
  const char* sol = doc["solicitud"] | "";
  // Solo se obedecen las órdenes de la solicitud asignada
  if (!asignacion.hay || strcmp(sol, asignacion.solicitud) != 0) {
    Consola.printf("[NUBE] orden %s ignorada: no es de la solicitud asignada\n", tipo);
    return;
  }
  Consola.printf("[NUBE] orden %s (%s)\n", tipo, sol);
  if (!strcmp(tipo, "guiar")) {
    ponerZumbador(true);
    enServicio = true;   // el conductor pulsa cuando la persona ha subido
  } else if (!strcmp(tipo, "rampa")) {
    ponerRampa(true);
    enServicio = true;   // el conductor pulsa cuando la persona ha subido por la rampa
  } else if (!strcmp(tipo, "fin")) {
    parar();
  }
}

// Control remoto de los actuadores desde el dashboard de Node-RED
void alRecibirControl(JsonDocument& doc) {
  const char* accion = doc["accion"] | "";
  bool valor = doc["valor"] | false;
  Consola.printf("[DASHBOARD] %s: %s\n", accion, valor ? "sí" : "no");
  if (!strcmp(accion, "zumbador")) ponerZumbador(valor);
  else if (!strcmp(accion, "rampa")) ponerRampa(valor);
  else Consola.println("  acción desconocida");
}

void alRecibirMqtt(char* topic, byte* payload, unsigned int len) {
  Consola.printf("  [MQTT <] %s %.*s\n", topic, (int)min(len, 200u), (const char*)payload);
  String t(topic);
  if (!t.startsWith(base)) return;
  String tema = t.substring(base.length());
  JsonDocument doc;
  bool ok = len > 0 && !deserializeJson(doc, payload, len);
  if (tema == "asistencia") alRecibirAsistencia(doc, ok);
  else if (tema == "orden" && ok) alRecibirOrden(doc);
  else if (tema == "control" && ok) alRecibirControl(doc);
}

// ---------------------------------------------------------------------------
// WiFi y MQTT
// ---------------------------------------------------------------------------
void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Consola.printf("Conectando a la WiFi %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    led(80, 0, 0); digitalWrite(PIN_LED_SIMPLE, HIGH); delay(250);
    led(0, 0, 0);  digitalWrite(PIN_LED_SIMPLE, LOW);  delay(250);
    Consola.print(".");
  }
  Consola.printf("\nWiFi conectada. IP: %s\n", WiFi.localIP().toString().c_str());
}

bool conectarMqtt() {
  String lwt = base + "conexion";
  Consola.printf("Conectando al broker %s como %s... ", MQTT_BROKER, clientId);
  if (!mqtt.connect(clientId, nullptr, nullptr, lwt.c_str(), 1, true, "offline")) {
    Consola.printf("error %d\n", mqtt.state());
    return false;
  }
  Consola.println("conectado");
  mqtt.publish(lwt.c_str(), "online", true);
  mqtt.subscribe((base + "asistencia").c_str(), 1);
  mqtt.subscribe((base + "orden").c_str(), 1);
  mqtt.subscribe((base + "control").c_str(), 0);
  return true;
}

void publicarTelemetria() {
  JsonDocument doc;
  JsonArray l = doc["lcd"].to<JsonArray>();
  l.add(lcd[0]); l.add(lcd[1]);
  doc["zumbador"] = zumbador;
  doc["rampa_grados"] = (int)anguloServo;
  doc["en_servicio"] = enServicio;
  if (asignacion.hay) doc["solicitud"] = asignacion.solicitud; else doc["solicitud"] = nullptr;
  doc["rssi"] = WiFi.RSSI();
  doc["uptime_s"] = millis() / 1000;
  publicar("telemetria", doc);
}

// ---------------------------------------------------------------------------
// Monitor serie
// ---------------------------------------------------------------------------
void ayuda() {
  Consola.println(F("\nComandos: p  pulsar el botón del conductor | z  zumbador sí/no | r  rampa arriba/abajo\n"
                   "          e  estado | h  esta ayuda"));
}

void leerSerie() {
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'p': pulsador(); break;
      case 'z': ponerZumbador(!zumbador); break;
      case 'r': ponerRampa(!rampa); break;
      case 'e': Consola.printf("Asignación: %s %s %s | en servicio: %s | zumbador: %s | rampa: %.0f grados | MQTT: %s\n",
                              asignacion.hay ? asignacion.parada : "-", asignacion.hay ? asignacion.linea : "",
                              asignacion.hay ? asignacion.asistencia : "", enServicio ? "sí" : "no",
                              zumbador ? "sí" : "no", anguloServo, mqtt.connected() ? "conectado" : "desconectado"); break;
      case 'h': ayuda(); break;
    }
  }
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1500);
  Consola.printf("\n=== Nodo Autobús %s (%s) ===\n", BUS_ID, PLACA);
  pinMode(PIN_LED_SIMPLE, OUTPUT);
  ledcAttach(PIN_SERVO, 50, 14);
  escribirServo(0);   // rampa recogida al arrancar
  randomSeed(esp_random());

  base = String(TOPIC_PREFIX) + "/autobus/" + BUS_ID + "/";
  uint64_t mac = ESP.getEfuseMac();
  snprintf(clientId, sizeof(clientId), "napiot-%s-%06llx", BUS_ID, mac & 0xFFFFFF);

  conectarWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(alRecibirMqtt);
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(30);
  actualizarLcd();
  ayuda();
}

void loop() {
  uint32_t ahora = millis();

  if (WiFi.status() != WL_CONNECTED) conectarWiFi();
  if (!mqtt.connected()) {
    led(80, 0, 0);
    if (ahora - ultimoReintentoMqtt > 3000) { ultimoReintentoMqtt = ahora; conectarMqtt(); }
  }
  mqtt.loop();
  leerSerie();

  tareaZumbador();
  tareaServo();

  if (mqtt.connected() && ahora - ultimaTelemetria >= PERIODO_TELEMETRIA_MS) {
    ultimaTelemetria = ahora;
    publicarTelemetria();
  }

  // LED verde (GPIO21): rápido con el zumbador, fijo con la rampa bajada, lento con asignación
  if (mqtt.connected()) {
    bool parpadeo = (ahora / 400) % 2;
    if (zumbador)                     { led(0, 0, parpadeo ? 90 : 0); ledSimple(LED_RAPIDO); }   // zumbador: azul intermitente
    else if (rampa || anguloServo > 0) { led(0, 70, 0);               ledSimple(LED_FIJO); }     // rampa bajada: verde
    else if (asignacion.hay)          { led(70, 45, 0);               ledSimple(LED_LENTO); }    // asignación: amarillo
    else                              { led(0, 6, 6);                 ledSimple(LED_APAGADO); }  // reposo: cian tenue
  } else {
    ledSimple(LED_SIN_RED);
  }
}
