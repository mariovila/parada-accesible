/*
 * NODO PARADA P101 (ESP32-S3), práctica 2 de NAPIoT.
 *
 * Hace lo mismo que la pestaña "Parada P101" de Node-RED de la práctica 1, pero
 * en una ESP32-S3 real conectada por WiFi al broker MQTT. Como todavía no
 * tenemos los sensores, el firmware genera sus datos de forma realista:
 *
 *   - Ultrasonidos HC-SR04: una medida cada 0,5 s. Calzada libre a unos 395 cm,
 *     autobús detenido entre 50 y 85 cm, peatones que cruzan un momento y alguna
 *     medida perdida. El autobús llega y se va cuando se escribe "v" en el
 *     monitor serie.
 *   - Rotary encoder: se gira con "+" y "-" en el monitor serie y se pulsa con
 *     "p".
 *   - Altavoz: la frase se escribe por el monitor serie, se publica en MQTT y
 *     suena un pitido corto en el pin del altavoz.
 *
 * La baliza BLE del usuario sigue simulada en Node-RED (pestaña Dispositivo D07):
 * llega por el topic sim/ble/P101 con los mismos 4 bytes que el anuncio real.
 *
 * Todo se maneja a mano desde el monitor serie (115200 baudios, escribe "h").
 * El altavoz también se controla desde el dashboard de Node-RED por el topic
 * parada/P101/control.
 *
 * Librerías (Library Manager): PubSubClient (Nick O'Leary) y ArduinoJson (Benoit Blanchon).
 * Placa: "ESP32S3 Dev Module" del paquete esp32 de Espressif.
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Todo lo que sale por el monitor serie lleva delante [PARADA], para distinguir
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
SalidaConPrefijo Consola("[PARADA] ");
#include <time.h>

// WiFi (tiene que ser de 2,4 GHz). WIFI_SSID y WIFI_PASSWORD están en secretos.h,
// que no se sube a GitHub: copia secretos.h.ejemplo como secretos.h y pon tu red.
#include "secretos.h"

// ---------------------------------------------------------------------------
// Configuración (la misma que las variables de entorno de Node-RED)
// ---------------------------------------------------------------------------
const char* MQTT_BROKER   = "test.mosquitto.org";
const uint16_t MQTT_PORT  = 1883;
const char* TOPIC_PREFIX  = "napiot26paradabus7k3";
const char* PARADA_ID     = "P101";
const int FACTOR_TIEMPO   = 6;       // igual que FACTOR_TIEMPO en Node-RED

const float UMBRAL_CM          = 150;   // por debajo hay un vehículo delante
const uint32_t T_DETENCION_MS  = 1000;  // tiempo seguido por debajo del umbral para darlo por detenido
const uint32_t T_AUSENCIA_MS   = 120000UL / FACTOR_TIEMPO;          // 2 min sin baliza: el usuario se ha ido
const uint32_t PROXIMOS_CADUCA_MS = (60000UL / FACTOR_TIEMPO) + 3000;  // lista de próximos sin renovar
const uint32_t PERIODO_US_MS   = 500;   // muestreo del ultrasonidos
const uint32_t PERIODO_TELEMETRIA_MS = 5000;

// ---------------------------------------------------------------------------
// Pines de la ESP32-S3. Los del Fritzing (FireBeetle ESP32-E) no existen en la
// S3, así que se han cambiado. De momento solo se usan el altavoz y el LED.
// ---------------------------------------------------------------------------
#if CONFIG_IDF_TARGET_ESP32S3
const char* PLACA      = "ESP32-S3";
const int PIN_ENC_A    = 4;    // encoder, canal A (CLK)     [Fritzing: GPIO13]
const int PIN_ENC_B    = 5;    // encoder, canal B (DT)      [Fritzing: GPIO14]
const int PIN_ENC_SW   = 6;    // encoder, pulsador (SW)     [Fritzing: GPIO25]
const int PIN_US_TRIG  = 7;    // HC-SR04 Trig               [Fritzing: GPIO17]
const int PIN_US_ECHO  = 15;   // HC-SR04 Echo con divisor   [Fritzing: GPIO16]
const int PIN_ALTAVOZ  = 16;   // base del transistor (1k)   [Fritzing: GPIO26]
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
String base;   // "napiot26paradabus7k3/parada/P101/"

struct Bus { char linea[8]; int min; };
struct Opcion { char linea[8]; int mins[4]; int n; };

struct {
  bool valida = false;
  uint32_t recibidaMs = 0;
  Bus buses[8];
  int n = 0;
} proximos;

struct {
  bool presente = false;
  char asistencia[12] = "";
  uint16_t id = 0;
  uint32_t ultimaBaliza = 0;
  bool avisoSueloDicho = false;
} usuario;

int menu = -1;
char menuLinea[8] = "";

struct {
  bool activa = false;
  char id[8] = "";
  char linea[8] = "";
  char estado[12] = "";
} solicitud;

// Lo que tiene delante el ultrasonidos (simulado). Se pone y se quita con "v".
struct {
  bool presente = false;
  float distancia = 0;          // a qué distancia se ha parado este vehículo
} vehiculo;
int peatonMuestras = 0;
float peatonCm = 0;

// Filtro del ultrasonidos (igual que el firmware de la P1)
struct { bool cerca = false; uint32_t desde = 0; bool publicado = false; float ultimaCm = 0; } us;

String avisoSuelo = "";          // aviso de Open Weather Map que manda Node-RED
uint32_t finVozMs = 0;           // cuándo acaba de "hablar" el altavoz
uint32_t ultimoMuestreo = 0, ultimaTelemetria = 0, ultimoReintentoMqtt = 0;

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------
float ruidoGauss(float sigma) {
  // Aproximación de una normal: suma de 4 uniformes
  float s = 0;
  for (int i = 0; i < 4; i++) s += random(-1000, 1001) / 1000.0f;
  return s * sigma * 0.866f;
}

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

void decir(const String& texto) {
  Consola.printf("[ALTAVOZ] %s\n", texto.c_str());
  tone(PIN_ALTAVOZ, 880, 120);   // pitido de aviso antes de hablar
  // Unos 14 caracteres por segundo, como una voz sintética
  uint32_t dur = 600 + texto.length() * 70;
  finVozMs = max(finVozMs, (uint32_t)millis()) + dur;
  JsonDocument doc;
  doc["texto"] = texto;
  publicar("altavoz", doc);
}

String enMinutos(const Opcion& o) {
  String s;
  for (int i = 0; i < o.n; i++) {
    if (i > 0) s += (i == o.n - 1) ? " y " : ", ";
    s += "en " + String(o.mins[i]);
  }
  s += (o.n == 1 && o.mins[0] == 1) ? " minuto" : " minutos";
  return s;
}

// Opciones del menú: una por línea. Devuelve -1 si no hay lista reciente de la nube.
int opciones(Opcion* ops) {
  if (!proximos.valida || millis() - proximos.recibidaMs > PROXIMOS_CADUCA_MS) return -1;
  int n = 0;
  for (int i = 0; i < proximos.n; i++) {
    int j = 0;
    while (j < n && strcmp(ops[j].linea, proximos.buses[i].linea) != 0) j++;
    if (j == n) {
      if (n == 8) continue;
      strlcpy(ops[n].linea, proximos.buses[i].linea, sizeof(ops[n].linea));
      ops[n].n = 0;
      n++;
    }
    if (ops[j].n < 4) ops[j].mins[ops[j].n++] = proximos.buses[i].min;
  }
  return n;
}

void anunciar() {
  Opcion ops[8];
  int n = opciones(ops);
  menu = -1; menuLinea[0] = 0;
  if (n < 0) { decir("No hay información de autobuses en este momento."); return; }
  if (n == 0) { decir("No hay autobuses en los próximos 20 minutos."); return; }
  String s = "Próximos autobuses: ";
  for (int i = 0; i < n; i++) {
    if (i) s += "; ";
    s += "línea " + String(ops[i].linea) + " " + enMinutos(ops[i]);
  }
  s += ". Gire el mando para elegir línea y púlselo para solicitarla.";
  decir(s);
}

void decirAvisoSuelo() {
  if (usuario.presente && avisoSuelo.length() && !usuario.avisoSueloDicho) {
    usuario.avisoSueloDicho = true;
    decir(avisoSuelo);
  }
}

// ---------------------------------------------------------------------------
// Sensores: mando (rotary encoder) y ultrasonidos
// ---------------------------------------------------------------------------
void encoderGiro(int giro) {
  Consola.printf("[ENCODER] giro %+d\n", giro);
  if (!usuario.presente) { Consola.println("  Encoder ignorado: no hay ningún usuario presente"); return; }
  Opcion ops[8];
  int n = opciones(ops);
  if (n < 0) { decir("No hay información de autobuses en este momento."); return; }
  if (n == 0) { decir("No hay autobuses en los próximos 20 minutos."); return; }
  menu = menu < 0 ? (giro > 0 ? 0 : n - 1) : (((menu + giro) % n) + n) % n;
  strlcpy(menuLinea, ops[menu].linea, sizeof(menuLinea));
  decir("Línea " + String(ops[menu].linea) + ", " + enMinutos(ops[menu]) + ".");
}

void encoderPulsado() {
  Consola.println("[ENCODER] pulsado");
  if (!usuario.presente) { Consola.println("  Encoder ignorado: no hay ningún usuario presente"); return; }
  if (solicitud.activa) { decir("Ya tiene solicitada la línea " + String(solicitud.linea) + "."); return; }
  if (!menuLinea[0]) { anunciar(); return; }   // sin línea elegida: repite la lista

  snprintf(solicitud.id, sizeof(solicitud.id), "S-%04x", (unsigned)random(0x10000));
  strlcpy(solicitud.linea, menuLinea, sizeof(solicitud.linea));
  strlcpy(solicitud.estado, "enviada", sizeof(solicitud.estado));
  solicitud.activa = true;

  JsonDocument doc;
  doc["solicitud"] = solicitud.id;
  doc["linea"] = solicitud.linea;
  doc["asistencia"] = usuario.asistencia;
  publicar("solicitud", doc);
  decir("Solicitando línea " + String(solicitud.linea) + ".");
  menu = -1; menuLinea[0] = 0;
}

// Devuelve la distancia que mediría el HC-SR04 ahora mismo
float medirDistanciaSimulada() {
  // 1 de cada 60 medidas el eco se pierde: pulseIn da 0 y se toma el alcance máximo
  if (random(60) == 0) return 400.0f;
  if (vehiculo.presente) return vehiculo.distancia + ruidoGauss(0.8f);
  if (peatonMuestras > 0) { peatonMuestras--; return peatonCm + ruidoGauss(3); }
  // De vez en cuando pasa un peatón por delante (menos de 1 s: el filtro lo descarta)
  if (random(300) == 0) {
    peatonMuestras = random(1, 3) - 1;
    peatonCm = random(70, 140);
    Consola.println("[ULTRASONIDOS] pasa un peatón por delante");
    return peatonCm + ruidoGauss(3);
  }
  return 395.0f + ruidoGauss(2.5f);   // calzada libre, cerca del alcance máximo
}

void procesarDistancia(float cm) {
  us.ultimaCm = cm;
  uint32_t ahora = millis();
  bool cerca = cm < UMBRAL_CM;
  if (cerca != us.cerca) { us.cerca = cerca; us.desde = ahora; }
  if (cerca != us.publicado && ahora - us.desde >= T_DETENCION_MS) {
    us.publicado = cerca;
    JsonDocument doc;
    doc["estado"] = cerca ? "detenido" : "libre";
    doc["distancia_cm"] = (int)lroundf(cm);
    publicar("deteccion", doc);
    Consola.printf("[ULTRASONIDOS] %.0f cm: vehículo %s\n", cm, cerca ? "detenido" : "fuera");
  }
}

void vehiculoLlega() {
  vehiculo.presente = true;
  vehiculo.distancia = random(50, 86);   // cada vez para en un sitio un poco distinto
  Consola.printf("[SIMULACIÓN] llega un autobús y se detiene a %.0f cm\n", vehiculo.distancia);
}

void vehiculoSeVa() {
  if (!vehiculo.presente) return;
  vehiculo.presente = false;
  Consola.println("[SIMULACIÓN] el autobús se va");
}

// ---------------------------------------------------------------------------
// Mensajes MQTT recibidos
// ---------------------------------------------------------------------------
void alRecibirBaliza(const uint8_t* b, unsigned len) {
  if (len != 4 || b[0] != 1 || (b[1] != 1 && b[1] != 2)) {
    Consola.println("[BLE] anuncio descartado: no tiene el formato de la baliza");
    return;
  }
  if (usuario.presente) { usuario.ultimaBaliza = millis(); return; }
  usuario.presente = true;
  strlcpy(usuario.asistencia, b[1] == 1 ? "visual" : "movilidad", sizeof(usuario.asistencia));
  usuario.id = (b[2] << 8) | b[3];
  usuario.ultimaBaliza = millis();
  usuario.avisoSueloDicho = false;
  Consola.printf("[BLE] baliza D%02u (%s): usuario presente\n", usuario.id, usuario.asistencia);
  anunciar();
  decirAvisoSuelo();
}

void alRecibirProximos(JsonDocument& doc) {
  JsonArray buses = doc["buses"];
  if (buses.isNull()) { proximos.valida = false; return; }
  // Una lista retenida de una ejecución anterior no vale (necesita la hora por NTP)
  time_t ahora = time(nullptr);
  long ts = doc["ts"] | 0L;
  if (ahora > 1700000000 && ahora - ts > (long)(PROXIMOS_CADUCA_MS / 1000)) {
    Consola.println("[NUBE] lista de próximos antigua (retenida): se ignora");
    proximos.valida = false;
    return;
  }
  proximos.n = 0;
  for (JsonObject b : buses) {
    if (proximos.n == 8) break;
    strlcpy(proximos.buses[proximos.n].linea, b["linea"] | "?", sizeof(proximos.buses[0].linea));
    proximos.buses[proximos.n].min = b["min"] | 0;
    proximos.n++;
  }
  proximos.valida = true;
  proximos.recibidaMs = millis();
}

void alRecibirAviso(JsonDocument& doc) {
  const char* tipo = doc["tipo"] | "";
  const char* id = doc["solicitud"] | "";
  const char* linea = doc["linea"] | "";
  if (!solicitud.activa || strcmp(id, solicitud.id) != 0) return;
  if (!strcmp(tipo, "asignado")) {
    strlcpy(solicitud.estado, "asignada", sizeof(solicitud.estado));
    decir("Línea " + String(linea) + " avisada.");
  } else if (!strcmp(tipo, "llegado")) {
    strlcpy(solicitud.estado, "en parada", sizeof(solicitud.estado));
    decir("Su autobús de la línea " + String(linea) + " ha llegado.");
  } else if (!strcmp(tipo, "cerrada")) {
    Consola.printf("[NUBE] solicitud %s cerrada\n", id);
    solicitud.activa = false;
  }
}

// Control remoto del altavoz desde el dashboard de Node-RED
void alRecibirControl(JsonDocument& doc) {
  const char* accion = doc["accion"] | "";
  if (!strcmp(accion, "decir")) {
    String texto = doc["texto"] | "";
    Consola.println("[DASHBOARD] decir por el altavoz");
    if (texto.length()) decir(texto);
  } else {
    Consola.printf("[DASHBOARD] acción desconocida: %s\n", accion);
  }
}

void alRecibirMqtt(char* topic, byte* payload, unsigned int len) {
  String t(topic);
  String simBle = String(TOPIC_PREFIX) + "/sim/ble/" + PARADA_ID;
  if (t == simBle) { alRecibirBaliza(payload, len); return; }

  if (!t.startsWith(base)) return;
  String tema = t.substring(base.length());
  Consola.printf("  [MQTT <] %s %.*s\n", topic, (int)min(len, 200u), (const char*)payload);

  JsonDocument doc;
  bool ok = len > 0 && !deserializeJson(doc, payload, len);
  if (tema == "proximos") { if (ok) alRecibirProximos(doc); else proximos.valida = false; }
  else if (tema == "aviso" && ok) alRecibirAviso(doc);
  else if (tema == "control" && ok) alRecibirControl(doc);
  else if (tema == "tiempo") {
    avisoSuelo = ok ? String(doc["aviso"] | "") : "";
    decirAvisoSuelo();
  }
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
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
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
  mqtt.subscribe((base + "aviso").c_str(), 1);
  mqtt.subscribe((base + "proximos").c_str(), 1);
  mqtt.subscribe((base + "tiempo").c_str(), 1);
  mqtt.subscribe((base + "control").c_str(), 0);
  mqtt.subscribe((String(TOPIC_PREFIX) + "/sim/ble/" + PARADA_ID).c_str(), 0);
  return true;
}

void publicarTelemetria() {
  JsonDocument doc;
  doc["distancia_cm"] = roundf(us.ultimaCm * 10) / 10;
  doc["vehiculo"] = us.publicado;
  if (usuario.presente) doc["usuario"] = usuario.asistencia; else doc["usuario"] = nullptr;
  if (solicitud.activa) doc["solicitud"] = String(solicitud.linea) + " " + solicitud.estado;
  else doc["solicitud"] = nullptr;
  doc["rssi"] = WiFi.RSSI();
  doc["uptime_s"] = millis() / 1000;
  publicar("telemetria", doc);
}

// ---------------------------------------------------------------------------
// Monitor serie
// ---------------------------------------------------------------------------
void ayuda() {
  Consola.println(F("\nComandos: + / -  girar el mando | p  pulsar el mando | v  autobús se detiene o se va\n"
                   "          e  estado | h  esta ayuda"));
}

void leerSerie() {
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case '+': encoderGiro(+1); break;
      case '-': encoderGiro(-1); break;
      case 'p': encoderPulsado(); break;
      case 'v': if (vehiculo.presente) vehiculoSeVa(); else vehiculoLlega(); break;
      case 'e': Consola.printf("Usuario: %s | solicitud: %s | vehículo: %s | última medida: %.1f cm | MQTT: %s\n",
                              usuario.presente ? usuario.asistencia : "no",
                              solicitud.activa ? solicitud.estado : "no",
                              vehiculo.presente ? "detenido" : "no", us.ultimaCm,
                              mqtt.connected() ? "conectado" : "desconectado"); break;
      case 'h': ayuda(); break;
    }
  }
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1500);
  Consola.printf("\n=== Nodo Parada %s (%s) ===\n", PARADA_ID, PLACA);
  pinMode(PIN_LED_SIMPLE, OUTPUT);
  pinMode(PIN_ALTAVOZ, OUTPUT);
  randomSeed(esp_random());

  base = String(TOPIC_PREFIX) + "/parada/" + PARADA_ID + "/";
  uint64_t mac = ESP.getEfuseMac();
  snprintf(clientId, sizeof(clientId), "napiot-%s-%06llx", PARADA_ID, mac & 0xFFFFFF);

  conectarWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(alRecibirMqtt);
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(30);
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

  // Más de 2 min (del diseño) sin baliza: el usuario se ha ido
  // millis() y no "ahora": la baliza puede haber llegado en mqtt.loop(), después de tomar "ahora"
  if (usuario.presente && millis() - usuario.ultimaBaliza > T_AUSENCIA_MS) {
    Consola.println("[BLE] sin baliza: la parada queda libre para otro usuario");
    usuario.presente = false;
    menu = -1; menuLinea[0] = 0;
  }

  if (ahora - ultimoMuestreo >= PERIODO_US_MS) {
    ultimoMuestreo = ahora;
    procesarDistancia(medirDistanciaSimulada());
  }

  if (mqtt.connected() && ahora - ultimaTelemetria >= PERIODO_TELEMETRIA_MS) {
    ultimaTelemetria = ahora;
    publicarTelemetria();
  }

  // LED: blanco mientras habla, azul con autobús detenido, verde con usuario
  // LED verde (GPIO21): rápido mientras habla, lento con autobús detenido, fijo con usuario
  if (mqtt.connected()) {
    if (ahora < finVozMs)      { led(60, 60, 60); ledSimple(LED_RAPIDO); }   // hablando: blanco
    else if (us.publicado)     { led(0, 0, 80);   ledSimple(LED_LENTO); }    // autobús detenido: azul
    else if (usuario.presente) { led(0, 70, 0);   ledSimple(LED_FIJO); }     // usuario presente: verde
    else                       { led(0, 6, 6);    ledSimple(LED_APAGADO); }  // reposo: cian tenue
  } else {
    ledSimple(LED_SIN_RED);
  }
}
