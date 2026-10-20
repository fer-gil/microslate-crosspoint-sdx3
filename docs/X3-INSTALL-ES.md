# MicroSlate Markdown visual — X3, versión de prueba

Esta es una aplicación ESP32-C3 para Xteink X3 con el controlador UC8253
(792×528). No es una imagen completa de memoria. No incluye CrossPoint,
el cargador de arranque ni una nueva tabla de particiones. No es una compilación
para X4 ni una implementación del controlador UC8279.

Compilar y pasar pruebas en el ordenador no verifica el funcionamiento físico.
La versión nueva debe probarse en el dispositivo antes de considerarla estable.

## Antes de instalar

1. Cierra la nota actual con Esc y espera a que termine el guardado. Carga el X3.
2. Con el equipo apagado, copia **toda la microSD**, incluidos archivos ocultos,
   al Mac. Conserva especialmente `/notes`, `/microslate` y la configuración de
   CrossPoint. Una copia de la memoria interna no contiene las notas de la SD.
3. Conserva la copia completa de 16 MB de la memoria interna y una copia separada
   del MicroSlate anterior. Ambas se guardan solo localmente: contienen ajustes
   privados. Verifica sus tamaños y SHA-256 antes de continuar.
4. Verifica modelo, controlador de pantalla y ambas aplicaciones instaladas.
   El registro de la unidad examinada identificó ESP32-C3 rev. 0.4, X3,
   16 MB, CrossPoint `1.4.1-dev-feature/writer-x3-98979f6a` en `app0`
   y MicroSlate detectado en `app1`. Esto no identifica por sí solo el panel.
   La aplicación MicroSlate extraída de la copia contiene el identificador
   UC8253; su versión interna es `292a14f`. Esto confirma el perfil del firmware
   anterior que el usuario reporta operativo, aunque no prueba la revisión física
   del panel. Si el MicroSlate instalado usa otro controlador, detente.
5. Comprueba la tabla de particiones de **tu unidad**: `app0=0x10000`,
   `app1=0x650000`, cada una de `0x640000` bytes. Verifica que `app1` contiene
   MicroSlate, no CrossPoint, y que CrossPoint arranca y permite abrir MicroSlate.
6. Confirma la forma de regresar de MicroSlate a CrossPoint. Esta actualización
   no cambia el mecanismo de arranque ni añade un menú de retorno.
7. Comprueba `SHA256SUMS.txt` del paquete. El `.bin` debe ser una aplicación
   ESP32-C3 válida y ocupar menos de `0x640000` bytes.

## Actualizar solo MicroSlate por USB

Los siguientes comandos son para ejecutar **después** de las comprobaciones
anteriores. No se ejecutan automáticamente al compilar.

Primero identifica el puerto actual: puede cambiar al reconectar. En el Mac
examinado fue `/dev/cu.usbmodem11301`; no asumas que siempre será ese.
Usa esptool 4.9.0; las versiones 5 cambian los guiones bajos por guiones.

En una terminal situada dentro de la carpeta del paquete, prepara la herramienta
si no tienes esptool 4.9.0 instalado:

```sh
python3 -m venv .tools
. .tools/bin/activate
python3 -m pip install esptool==4.9.0
```

Después:

```sh
python3 -m esptool --chip esp32c3 --port PUERTO_DEL_X3 --baud 460800 read_flash 0x650000 0x640000 microslate-anterior-slot1.bin
shasum -a 256 microslate-anterior-slot1.bin
shasum -a 256 -c SHA256SUMS.txt
```

Si la lectura falla, el tamaño es incorrecto o no puedes identificar la variante,
no continúes. Tras verificar las copias y la compilación, el comando de escritura
limitado a MicroSlate sería:

```sh
python3 -m esptool --chip esp32c3 --port PUERTO_DEL_X3 --baud 460800 write_flash 0x650000 microslate-x3-uc8253-app.bin
python3 -m esptool --chip esp32c3 --port PUERTO_DEL_X3 verify_flash 0x650000 microslate-x3-uc8253-app.bin
```

No uses `erase_flash`, `--erase-all`, `--force`, una imagen combinada en `0x0`,
ni el comando de subida predeterminado de PlatformIO: este último apunta a
`0x10000`, que en esta unidad contiene CrossPoint. No cambies `otadata`, NVS,
las particiones ni los eFuses.

No renombres este archivo a `update.bin` para iniciar una actualización OEM
por SD: no se ha verificado qué partición modificaría ese recorrido en el dual
boot actual. La advertencia antigua de USB bloqueado no debe extrapolarse:
la lectura por USB de esta unidad sí debe verificarse antes de elegir la ruta.
Si el cargador rechaza una escritura, conserva las copias y detente sin forzarla.

Después de verificar la escritura, arranca CrossPoint y abre MicroSlate por el
mecanismo existente. Si hay pantalla en blanco, reinicios, botones erráticos o
fallos de teclado, no sigas escribiendo: vuelve a CrossPoint y restaura solo la
partición anterior de MicroSlate con la copia comprobada.

```sh
python3 -m esptool --chip esp32c3 --port PUERTO_DEL_X3 write_flash 0x650000 microslate-anterior-slot1.bin
python3 -m esptool --chip esp32c3 --port PUERTO_DEL_X3 verify_flash 0x650000 microslate-anterior-slot1.bin
```

La restauración tampoco debe ejecutarse hasta confirmar que la copia pertenece
a esa misma unidad y partición. No restaures toda la memoria como primer paso.

## Prueba en el X3

1. Abre y cierra CrossPoint y MicroSlate para comprobar ambos arranques.
2. Comprueba teclado, flechas, Shift, Ctrl+S y reconexión tras dormir.
3. Crea una nota nueva y verifica el nombre `.md` en la SD. Abre también una
   `.txt` antigua: debe conservar la extensión, incluso al cambiar el título.
4. Usa `prueba-markdown.md` del paquete. Al salir de cada párrafo se ocultan
   los marcadores; al entrar reaparece el texto Markdown. Ctrl+M alterna
   visual/fuente. No modifica los bytes guardados.
5. Revisa encabezados ATX, negritas, cursivas, listas y las referencias `[id]`.
   Las definiciones se muestran donde están escritas; escribe la sección de
   notas al final del documento. No hay ventanas emergentes ni renumeración.
6. Verifica scroll, máquina de escribir, paginación, orientación y modo oscuro.
7. En una nota desechable, escribe, espera más de 10 segundos sin tocar teclas,
   sal y vuelve a abrir. Prueba también el límite de dos minutos escribiendo
   continuamente. No pruebes cortes de alimentación con notas importantes.
8. Sincroniza por Wi-Fi y compara el archivo con la SD, incluidos acentos.
   Para comprobar Pandoc: `pandoc -f markdown+footnotes prueba-markdown.md -o prueba.html`.

Límite de edición: 16 383 bytes por nota. Se rechazan archivos mayores para
impedir guardados truncados. El lector BLE sigue orientado a reportes de teclado
HID de 6 teclas; no implementa todos los formatos NKRO ni una distribución de
teclado española. La entrada física conserva el mapa ASCII estadounidense del
firmware anterior; los archivos UTF-8 se conservan y navegan por caracteres.

Este es un subconjunto de Markdown. Sintaxis no implementada (tablas, enlaces,
matemáticas, etc.) permanece como fuente. Las notas al pie usan `[^id]` y
`[^id]: contenido` con continuaciones indentadas, compatibles con Pandoc.
