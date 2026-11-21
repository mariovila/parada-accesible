# NAIoT - P1

Novas Arquitecturas IoT, práctica 1: parada de autobús accesible sobre Node-RED y MQTT.

Esta carpeta contiene los flujos de los ejercicios 2, 3 y 4: la nube en Node-RED, los nodos IoT simulados dentro del propio Node-RED, la comunicación entre ambos por MQTT a través de test.mosquitto.org, un dashboard hecho con Dashboard 2.0 y un aviso de suelo mojado con el tiempo actual de Open Weather Map.

| Qué | Dónde |
|---|---|
| Flujos de Node-RED | `datos_nodered/flows.json` |
| Descripción de la implementación | `../2_documentacion (Mario)/implementacion_ej2.md` |
| Descripción del dashboard | `../2_documentacion (Mario)/implementacion_ej3.md` |
| Descripción de Open Weather Map | `../2_documentacion (Mario)/implementacion_ej4.md` |
| Diseño base del sistema | `../2_documentacion (Mario)/Arquitectura_Parada_Autobus_base.html` |

## Arrancar

Los comandos se ejecutan desde esta carpeta: `cd "1_flujos_node-red (Manuel)"`.

Hace falta Node.js y Node-RED (probado con Node-RED 5.0.10 y Node.js 26). Los paquetes de la paleta son `@flowfuse/node-red-dashboard` y `node-red-node-openweathermap`, que ya figuran en `datos_nodered/package.json`.

```bash
npm install -g --unsafe-perm node-red          # solo la primera vez
(cd datos_nodered && npm install)              # solo la primera vez: instala Dashboard 2.0 y Open Weather Map
export OPENWEATHER_API_KEY=la_clave_del_grupo  # la API key de Open Weather Map no está en el repositorio
node-red --userDir ./datos_nodered
```

Después abre:

- http://127.0.0.1:1880 para el editor de flujos.
- http://127.0.0.1:1880/dashboard para el dashboard.

En Codespaces es igual: entra en esta carpeta, ejecuta los tres comandos y abre el puerto 1880 desde la pestaña Ports. Para el dashboard, añade `/dashboard` a esa dirección.

Al arrancar, en la consola deben aparecer cinco líneas `Connected to broker`.

No arranques a la vez dos copias con el mismo prefijo (por ejemplo, una en local y otra en Codespaces, o dos personas del grupo). Compartirían broker y topics, y cada una reaccionaría a los mensajes de la otra. Para probar en paralelo, cambiad `TOPIC_PREFIX`.

## Configuración

Todo está en un único sitio: menú de Node-RED, Settings, Environment.

| Variable | Valor | Para qué |
|---|---|---|
| `TOPIC_PREFIX` | `napiot26paradabus7k3` | Va delante de todos los topics |
| `MQTT_BROKER` | `test.mosquitto.org` | Broker |
| `MQTT_PORT` | `1883` | Puerto |
| `FACTOR_TIEMPO` | `6` | Acelera los tiempos del diseño. Con 1 son los reales |

Con `FACTOR_TIEMPO` = 6, cada tiempo del diseño dura la sexta parte:

| Tiempo del diseño | Real | Con factor 6 |
|---|---|---|
| Baliza BLE | 10 s | 1,7 s |
| Margen de los horarios | ±30 s | ±5 s |
| Lista de próximos | 20 min | 200 s |

El aviso al autobús es inmediato: no hay margen de 10 minutos.

La API key de Open Weather Map es la única configuración que no está en `flows.json`, para no publicarla. Node-RED la lee de la variable de entorno del sistema `OPENWEATHER_API_KEY`, que hay que definir antes de arrancar (con `export`, o como secreto del codespace con ese mismo nombre). Sin ella todo funciona menos el aviso de suelo mojado, y la consola muestra `Invalid API Key provided`.

## Pestañas

| Pestaña | Qué simula | Botones (nodos inject) |
|---|---|---|
| Nube | Node-RED en la nube: horarios y gestor de solicitudes | Cargar E1 o E2. Limpiar retenidos |
| Parada P101 | Raspberry Pi con encoder, ultrasonidos, escáner BLE y altavoz. Consulta Open Weather Map cada 30 s | Girar a la derecha, girar a la izquierda, pulsar el mando. Vehículo se detiene, vehículo se va |
| Autobús AUT312 | ESP32 con pulsador, LCD, zumbador y servo | Conductor pulsa el botón |
| Dispositivo D07 | ESP32 del bastón o la silla con baliza BLE | Llega con bastón, llega en silla, se va |
| Panel | Dashboard del operador. Solo escucha por MQTT | Ninguno |

Dónde mirar:

- **Altavoz** (pestaña Parada): la frase que se diría por voz aparece bajo el nodo y en la barra de debug.
- **LCD, Zumbador y Servo** (pestañas de autobús): su estado aparece bajo cada nodo.
- **Gestor de solicitudes** (pestaña Nube): bajo el nodo se ve el reloj del escenario, cuánto falta para el siguiente autobús en segundos reales y el estado de la solicitud.
- La consola donde corre Node-RED muestra las mismas trazas.

Para ver todo el tráfico MQTT desde otro terminal:

```bash
mosquitto_sub -h test.mosquitto.org -v -t 'napiot26paradabus7k3/#' -T 'napiot26paradabus7k3/sim/#'
```

## Dashboard

Está en http://127.0.0.1:1880/dashboard y permite hacer todas las pruebas sin entrar en el editor.

| Grupo | Qué tiene |
|---|---|
| Operador de pruebas | Reloj del escenario, cuánto falta para el siguiente autobús (en segundos reales) y los botones que cargan E1 o E2 |
| Simulación de los dispositivos | Los mismos botones que los inject: llegada y salida del usuario, mando de la parada, vehículo y pulsador del conductor |
| Parada P101 | Solicitud y su estado, último aviso, ultrasonidos y lista de próximos autobuses |
| Minutos hasta el próximo autobús de cada línea | Gráfica de líneas, un punto cada vez que la nube renueva la lista |
| Estado de la solicitud | Gráfica en escalón: 0 sin solicitud, 1 asignada, 2 en parada |
| Minutos al próximo autobús | Galga de 0 a 20 minutos |
| Autobús | Tabla con lo que muestra el LCD y el estado del zumbador y la rampa |
| Solicitudes cerradas | Gráfica de barras con cuántas se han cerrado y cómo |
| Últimos mensajes MQTT | Tabla con los 12 últimos |

En las pruebas de más abajo, cada botón del editor tiene su equivalente en el dashboard con el mismo nombre o uno muy parecido. Cuando «Siguiente autobús» diga que está pasando ahora, pulsa **Vehículo se detiene**.

## Escenarios de horarios

Los tiempos cuentan desde que se pulsa el botón que carga el escenario.

| Escenario | Pasos por P101 | Con factor 6 |
|---|---|---|
| E1 | C15 con AUT312 a los 4 min, L9 con AUT205 a los 12 min | 40 s y 120 s |
| E2 | C15 con AUT312 a los 26 min | Ninguno en la ventana de 20 min al empezar |

AUT205 no tiene pestaña: existe solo en los horarios, para que el menú tenga dos líneas.

## Pruebas paso a paso

Se pueden hacer desde el dashboard o desde el editor: los botones se llaman igual o casi igual.

Entre prueba y prueba, pulsa que el usuario se va y espera 20 s: es lo que tarda la parada en olvidarlo y volver a anunciar los autobuses al siguiente.

El autobús no se mueve solo. Tú haces de autobús pulsando **Vehículo se detiene** cuando «Siguiente autobús» diga que está pasando ahora. Tienes 5 s de margen antes y después de la hora de paso.

### 1. Caso principal

1. Pulsa **Cargar E1**.
2. Pulsa **Llega con bastón**. Altavoz: «Próximos autobuses: línea C15 en 4 minutos; línea L9 en 12 minutos…».
3. Pulsa **Girar mando +1**. Altavoz: «Línea C15, en 4 minutos.».
4. Pulsa **Pulsar el mando**. Altavoz: «Solicitando línea C15.» y «Línea C15 avisada.». El LCD de AUT312 muestra `[P101 C15][VISUAL  ]`.
5. A los 40 s de cargar el escenario (entre 35 y 45), pulsa **Vehículo se detiene**. Altavoz: «Su autobús de la línea C15 ha llegado.». El zumbador de AUT312 pasa a `SONANDO`.
6. Pulsa **Pulsador del conductor**. Zumbador `apagado`, LCD en blanco y solicitud cerrada.
7. Pulsa **Vehículo se va** y que el usuario se va.

### 2. Cierre sin pulsador, en silla de ruedas

1. Pulsa **Cargar E1** y **Llega en silla**.
2. **Girar mando +1** y **Pulsar el mando**. El LCD de AUT312 muestra `[P101 C15][RAMPA   ]`.
3. A los 40 s, pulsa **Vehículo se detiene**. El servo pasa a `90 grados, rampa desplegada`.
4. No pulses el pulsador del conductor. Pulsa **Vehículo se va**: en el acto, el servo vuelve a `0 grados, rampa recogida` y la solicitud se cierra.

### 3. Sin autobuses

1. Pulsa **Cargar E2** y **Llega con bastón**. Altavoz: «No hay autobuses en los próximos 20 minutos.».
2. Gira o pulsa el mando: el altavoz repite la frase y no se publica ninguna solicitud.

### 4. Aviso de suelo mojado (Open Weather Map)

No se puede forzar: depende del tiempo real en Vigo.

1. En la pestaña Parada P101, mira el texto bajo el nodo **Transformar tiempo en mensaje**: dice `suelo mojado (...)` o `suelo seco (...)` según la última consulta.
2. Si dice `suelo mojado`, pulsa **Llega con bastón**. En menos de 30 s el altavoz dice «Atención: está lloviendo, el suelo puede estar resbaladizo…», una sola vez.
3. Si la consola muestra `Invalid API Key provided`, revisa que la variable de entorno `OPENWEATHER_API_KEY` esté definida en el terminal desde el que arrancas Node-RED. Una clave recién creada puede tardar en activarse.

Para que la clave funcione al copiar el proyecto, `datos_nodered` tiene que llevar también `flows_cred.json` y `.config.runtime.json`.

## Limpiar el broker

Los mensajes retenidos se quedan en el broker público al parar Node-RED.

- El botón **Limpiar retenidos y descargar escenario** de la pestaña Nube borra las asignaciones y la lista de próximos.
