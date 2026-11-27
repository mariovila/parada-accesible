# Práctica 2: nodos IoT en ESP32-S3

> Vídeo de la demostración: https://universidadevigo-my.sharepoint.com/:v:/g/personal/mario_vila_alumnado_uvigo_gal/IQCfA4NhqTrMT7SevIVnwnITAU9W6mJ73DiS2GUrrnrh87g?nav=eyJyZWZlcnJhbEluZm8iOnsicmVmZXJyYWxBcHAiOiJPbmVEcml2ZUZvckJ1c2luZXNzIiwicmVmZXJyYWxBcHBQbGF0Zm9ybSI6IldlYiIsInJlZmVycmFsTW9kZSI6InZpZXciLCJyZWZlcnJhbFZpZXciOiJNeUZpbGVzTGlua0NvcHkifX0&e=HKYrTe

Los dos nodos IoT del proyecto pasan de estar simulados en Node-RED a ejecutarse en dos ESP32-S3 reales, conectadas por WiFi al broker MQTT `test.mosquitto.org`. Usan los mismos topics que la práctica 1, así que la nube y el panel de Node-RED funcionan sin cambios.

| Nodo | Sketch | Quién | Placa |
|---|---|---|---|
| Parada P101 | [`parada_P101/parada_P101.ino`](parada_P101/parada_P101.ino) | Mario | ESP32-S3 de Mario |
| Autobús AUT312 | [`autobus_AUT312/autobus_AUT312.ino`](autobus_AUT312/autobus_AUT312.ino) | Manuel | ESP32-S3 de Manuel |
| Dispositivo D07 (baliza BLE) | Sigue en Node-RED, pestaña Dispositivo D07 | |  |
| Node-RED de la P2 | [`node-red/datos_nodered/`](node-red/datos_nodered/) | | |
| Solo la pestaña ESP32, para importarla en otro Node-RED | [`node-red/flujo_esp32_p2.json`](node-red/flujo_esp32_p2.json) | | |

No nos llegaron los sensores, así que **cada ESP32 emula los suyos**: los datos los genera el firmware y las acciones de las personas (girar el mando, pulsar, que llegue un autobús) se dan desde el monitor serie. Los actuadores (altavoz, zumbador y servo) sí reciben su señal en los pines, para cuando se conecten.

## Qué emula cada nodo

### Parada P101

| Elemento | Cómo se emula |
|---|---|
| Ultrasonidos HC-SR04 | Una medida cada 0,5 s. Calzada libre: unos 395 cm con ruido. Autobús detenido: entre 50 y 85 cm, distinto cada vez. A veces pasa un peatón (menos de 1 s, el filtro lo descarta) y 1 de cada 60 medidas se pierde el eco (400 cm). El autobús llega y se va con `v` en el monitor serie |
| Rotary encoder | Se gira con `+` y `-` en el monitor serie y se pulsa con `p` |
| Baliza BLE del usuario | Sigue en Node-RED (pestaña Dispositivo D07). Llega por el topic `sim/ble/P101` con los mismos 4 bytes que el anuncio real |
| Altavoz | La frase sale por el monitor serie y se publica en `parada/P101/altavoz`. En el pin del altavoz suena un pitido corto (880 Hz) |
| LED de la placa | Si es RGB: blanco mientras habla, azul con un autobús detenido, verde con usuario presente y rojo sin conexión. Si es de un color (GPIO21, el verde de nuestras placas): parpadeo rápido mientras habla, lento con un autobús detenido, fijo con usuario presente, apagado en reposo y muy rápido sin conexión |

### Autobús AUT312

| Elemento | Cómo se emula |
|---|---|
| Pulsador del conductor | `p` en el monitor serie, cuando la persona ha subido |
| Zumbador | Pitidos intermitentes de 2,3 kHz en su pin, y el estado por el monitor serie |
| Servo de la rampa | PWM real de 50 Hz. Baja y sube poco a poco, 90 grados en 3 s |
| Pantalla de 8x2 caracteres | Sus dos líneas salen por el monitor serie, por ejemplo `[P101 C15][VISUAL  ]` |
| LED de la placa | Si es RGB: amarillo con asignación, azul intermitente con el zumbador, verde con la rampa bajada y rojo sin conexión. Si es de un color (GPIO21): parpadeo lento con asignación, rápido con el zumbador, fijo con la rampa bajada, apagado en reposo y muy rápido sin conexión |

### Comandos del monitor serie (115200 baudios)

| Placa | Comando | Qué hace |
|---|---|---|
| Parada | `+` / `-` | Gira el mando |
| Parada | `p` | Pulsa el mando |
| Parada | `v` | Un autobús se detiene delante, o se va |
| Autobús | `p` | Pulsa el botón del conductor |
| Autobús | `z` | Zumbador sí/no |
| Autobús | `r` | Rampa abajo/arriba |
| Las dos | `e` | Estado |
| Las dos | `h` | Ayuda |

## Pines

Los pines del Fritzing son los de la FireBeetle 2 ESP32-E. En la S3 no existen los GPIO 22 a 25, y el 26 y el 27 los usa la memoria flash, así que se han cambiado:

| Nodo | Componente | ESP32-S3 | Fritzing (FireBeetle) |
|---|---|---|---|
| Parada | Encoder, canal A | GPIO4 | GPIO14 |
| Parada | Encoder, canal B | GPIO5 | GPIO13 |
| Parada | Pulsador de confirmar | GPIO6 | GPIO25 |
| Parada | HC-SR04 Trig | GPIO7 | GPIO17 |
| Parada | HC-SR04 Echo (con divisor) | GPIO15 | GPIO16 |
| Parada | Altavoz (base del NPN con 1k) | GPIO16 | GPIO26 |
| Autobús | Pulsador del conductor | GPIO4 | GPIO4 |
| Autobús | Zumbador | GPIO5 | GPIO25 |
| Autobús | Servo (señal, alimentación a 5 V) | GPIO6 | GPIO15 |
| Autobús | Pantalla I2C SDA / SCL | GPIO8 / GPIO9 | GPIO21 / GPIO22 |
| Los dos | LED de la placa | GPIO48 o GPIO38 (RGB) y GPIO21 (de un color) | |

De momento el firmware solo usa los pines de los actuadores (altavoz, zumbador y servo) y el LED. Los de los sensores están reservados.

## Topics MQTT

Todos llevan delante `napiot26paradabus7k3/`. Los de la práctica 1 no cambian (ver la página de implementación de la Wiki). Los nuevos son:

| Topic | Quién publica | Retain | Payload |
|---|---|---|---|
| `parada/P101/telemetria` | Parada, cada 5 s | No | `{ distancia_cm, vehiculo, usuario, solicitud, rssi, uptime_s }` |
| `parada/P101/altavoz` | Parada | No | `{ texto }` |
| `parada/P101/conexion` | Parada (con LWT) | Sí | `online` u `offline` |
| `parada/P101/control` | Dashboard (ejercicio 4) | No | `{ accion: "decir", texto }`: la parada lo dice por el altavoz |
| `parada/P101/tiempo` | Node-RED (Open Weather Map) | Sí | `{ aviso }`: la frase de suelo mojado, o `null` |
| `autobus/AUT312/telemetria` | Autobús, cada 5 s | No | `{ lcd: [l1, l2], zumbador, rampa_grados, en_servicio, solicitud, rssi, uptime_s }` |
| `autobus/AUT312/conexion` | Autobús (con LWT) | Sí | `online` u `offline` |
| `autobus/AUT312/control` | Dashboard (ejercicio 4) | No | `{ accion: "zumbador", valor: true/false }` o `{ accion: "rampa", valor: true/false }` |

## Puesta en marcha

### 1. Arduino IDE (cada uno en su ordenador)

1. Instala el paquete **esp32 de Espressif** (Boards Manager), como en el enunciado.
2. En Library Manager, instala **PubSubClient** (Nick O'Leary) y **ArduinoJson** (Benoit Blanchon, versión 7).
3. Abre tu sketch: Mario `parada_P101/parada_P101.ino`, Manuel `autobus_AUT312/autobus_AUT312.ino`.
4. En la carpeta del sketch, copia `secretos.h.ejemplo` como `secretos.h` y pon tu WiFi. `secretos.h` no se sube a GitHub. La red tiene que ser de 2,4 GHz.
5. Selecciona la placa que tengas y su puerto: **DFRobot Firebeetle 2 ESP32-S3** o **ESP32S3 Dev Module**. En las S3 pon **USB CDC On Boot: Enabled** en el menú Tools; si no, el monitor serie no muestra nada.
6. Sube el sketch y abre el monitor serie a **115200 baudios**. Debe salir `WiFi conectada` y `Conectando al broker... conectado`.

### 2. Node-RED

`node-red/datos_nodered` es una copia de la carpeta de la P1 con la pestaña **ESP32 (P2)** ya incluida. Las pestañas virtuales **Parada P101** y **Autobús AUT312** vienen desactivadas, porque esos nodos son las dos placas (si no, habría dos paradas y dos autobuses respondiendo a los mismos topics). Nube, Dispositivo D07 (la baliza del usuario), Panel y ESP32 (P2) están activas.

```bash
cd "P2_ESP32 (Mario y Manuel)/node-red"
(cd datos_nodered && npm install)              # solo la primera vez
node-red --userDir ./datos_nodered
```

Editor en http://127.0.0.1:1880 y dashboard en http://127.0.0.1:1880/dashboard. En la consola deben aparecer las líneas `Connected to broker`. La API key de Open Weather Map se pone igual que en la P1 (Settings, Environment).

No arranques a la vez este Node-RED y el de la P1, ni dos personas del grupo el mismo: comparten prefijo y topics.

Para usarlo en otro Node-RED (por ejemplo, en Codespaces con los flujos de la P1), importa `node-red/flujo_esp32_p2.json` desde el menú, **Import**, y desactiva a mano las dos pestañas virtuales.

## Prueba completa (las dos placas)

Con las pestañas virtuales desactivadas, los botones del dashboard que eran de la parada y del autobús virtuales (Girar mando, Pulsar el mando, Vehículo se detiene, Vehículo se va, Pulsador del conductor) no hacen nada: eso se hace ahora en el monitor serie de cada placa.

1. En el dashboard, **Cargar E1** (C15 a 4 min y L9 a 12 min, con el tiempo acelerado por 6).
2. **Llega con bastón** (grupo Simulación de los dispositivos). En el monitor serie de la parada se ve `[BLE] baliza` y la frase del altavoz con los próximos autobuses.
3. En el monitor serie de la parada, `+` hasta oír la C15 y `p` para solicitarla. El autobús muestra `[LCD] [P101 C15][VISUAL  ]` y su LED indica asignación.
4. A los 40 s de cargar el escenario (entre 35 y 45), `v` en la parada: el ultrasonidos baja a unos 60 cm, se publica la detección y la nube manda `guiar`. Suena el zumbador del autobús.
5. `p` en el monitor serie del autobús: el conductor confirma la recogida y la solicitud se cierra. `v` en la parada para que el autobús se vaya.
6. Repite con **Llega en silla**: en vez del zumbador baja la rampa (el servo).

Si se elige la L9, ese autobús (AUT205) no tiene placa. La parada lo detecta igual, pero en el autobús no pasa nada.

Control desde el dashboard (ejercicio 4): en el grupo **Control de actuadores (ESP32-S3)**, los interruptores **Zumbador del autobús** y **Rampa del autobús bajada** y el cuadro **Altavoz de la parada** actúan sobre las placas en cualquier momento.

## Probar con una sola placa

Sirve para que cada uno pruebe el código antes de juntar las dos ESP32-S3. Se carga un sketch cada vez y el otro nodo vuelve a su pestaña virtual. La pestaña del nodo que está en la placa tiene que seguir desactivada (doble clic en la pestaña, **Enabled** o **Disabled**, y Deploy).

| Sketch en la placa | Pestaña Parada P101 | Pestaña Autobús AUT312 |
|---|---|---|
| Los dos, en dos placas (así viene `datos_nodered`) | Desactivada | Desactivada |
| Solo `parada_P101` | Desactivada | **Activa** |
| Solo `autobus_AUT312` | **Activa** | Desactivada |

Solo la parada: igual que la prueba completa, pero el autobús es el virtual. La tabla Autobús del dashboard muestra `P101 C15 / VISUAL` y, cuando llega, se pulsa **Pulsador del conductor** en el grupo Simulación de los dispositivos.

Solo el autobús: la parada virtual se maneja con los botones del grupo Simulación de los dispositivos.

1. **Cargar E1** y **Llega con bastón**.
2. **Girar mando +1** y **Pulsar el mando**. El monitor serie de la placa muestra `[LCD] [P101 C15][VISUAL  ]`.
3. A los 40 s de cargar el escenario (entre 35 y 45), pulsa **Vehículo se detiene**. La nube manda `guiar` y suena el zumbador de la placa. Escribe `p` en el monitor serie: la solicitud se cierra.
4. Pulsa **Vehículo se va**. Repite con **Llega en silla**: baja la rampa (el servo) y se confirma con `p`.
