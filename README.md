# parada-accesible

Una parada de autobús accesible para personas invidentes o en silla de ruedas.

Proyecto de grupo de la asignatura **Novas Arquitecturas e Paradigmas IoT**.

## La idea

La persona lleva un dispositivo pequeño en el bastón o en la silla. La parada lo detecta, le dice por voz qué autobuses llegan en los próximos 20 minutos y, con un mando giratorio, la persona elige el suyo. El sistema avisa a ese autobús, que llega sabiendo qué ayuda tiene que dar: hace sonar un zumbador en la puerta (asistencia visual) o despliega la rampa (movilidad). Si llueve o nieva, la parada avisa además de que el suelo puede estar resbaladizo.

![Arquitectura de Cloud Computing de la parada accesible](<2_documentacion (Mario)/img/arquitectura_cloud.png>)

## Práctica 1: Arquitectura de Cloud Computing con Node-RED

Estado de cada punto del ejercicio:

| Punto | Qué pide | Qué hemos hecho | Estado |
|---|---|---|---|
| 1 | Diseñar la arquitectura de Cloud Computing | Arquitectura en capas (nodos IoT, gateways, Internet, nube, servicios externos y usuario remoto), con casos de uso, requisitos, secuencia de una solicitud y diagrama de estados | Hecho |
| 2 | Implementarla: Node-RED en GitHub, nodos IoT virtuales y MQTT con un broker público | Node-RED con 5 pestañas (Nube, Parada P101, Autobús AUT312, Dispositivo D07 y Panel). Los sensores y actuadores están simulados en el propio Node-RED, siguiendo el Anexo del enunciado, y las pestañas solo se comunican por MQTT a través de `test.mosquitto.org` | Hecho |
| 3 | Dashboard con Dashboard 2.0 | Panel del operador con el estado de la parada y del autobús, gráficas, galga, últimos mensajes MQTT y botones para hacer todas las pruebas sin entrar en el editor | Hecho |
| 4 | Integrar Open Weather Map | Consulta del tiempo actual de Vigo. Si llueve, nieva o ha llovido hace poco, la parada lo avisa por voz al usuario | Hecho |
| 5 | Documentarlo en la Wiki de GitHub | Páginas de diseño, implementación, dashboard y Open Weather Map | Pendiente de subir |

### Sensores y actuadores

Salen del kit Keyestudio 48 en 1, salvo el altavoz. En esta práctica están simulados en Node-RED.

| Dónde | Sensores | Actuadores |
|---|---|---|
| Parada | Rotary encoder, ultrasonidos HC-SR04 y escáner BLE | Altavoz |
| Autobús | Pulsador del conductor | LCD 0802, zumbador y servo de la rampa |
| Dispositivo del usuario | | Baliza BLE |

### Pruebas realizadas

Con los flujos funcionando se comprobaron los tres casos principales:

- **Usuario con bastón:** la parada anuncia los autobuses, el usuario elige la línea C15, el LCD del autobús muestra `[P101 C15][VISUAL  ]`, el zumbador suena al llegar y el conductor cierra la solicitud con el pulsador.
- **Usuario en silla de ruedas:** al llegar el autobús se despliega la rampa, y al irse se recoge y la solicitud se cierra sola.
- **Sin autobuses:** la parada dice que no hay autobuses en los próximos 20 minutos y no se crea ninguna solicitud.

## Estructura del repositorio

| Carpeta | Contenido |
|---|---|
| [`2_documentacion (Mario)/`](<2_documentacion (Mario)>) | [Diseño completo de la arquitectura](<2_documentacion (Mario)/Arquitectura_Parada_Autobus_base.html>) y la descripción de la implementación: [ejercicio 2](<2_documentacion (Mario)/implementacion_ej2.md>), [dashboard](<2_documentacion (Mario)/implementacion_ej3.md>) y [Open Weather Map](<2_documentacion (Mario)/implementacion_ej4.md>) |

Los flujos de Node-RED y la wiki se añadirán al repositorio cuando se suban las demás partes.
