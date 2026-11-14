<!-- coderabbit-ai-generated:v1 -->

# XboxPad 2.11 y Configurator 1.0

Plugin USB Xbox One/Series para juegos PS4 con GoldHEN. La base 2.10 fue probada por el usuario en Dead Cells CUSA10484, firmware 11.50. La nueva 2.11 y el PKG requieren validación física.

## Instalar y configurar

1. Conserva el DualShock conectado e inicia GoldHEN con carga de plugins habilitada.
2. Instala el PKG XboxPad Configurator mediante el instalador de paquetes de GoldHEN. Cierra el juego y abre el configurador.
3. En Instalación pulsa Cruz en **Instalar / actualizar PRX**. El anterior se guarda como `/data/GoldHEN/plugins/xbox_pad.prx.bak`.
4. En Perfiles elige el CUSA del juego. La cruceta izquierda/derecha cambia el dígito; Cuadrado cambia de dígito. Dead Cells CUSA10484 aparece inicialmente.
5. Activa el CUSA desde Instalación. Esta acción quita las entradas globales de xbox_pad para evitar cargarlo en otras aplicaciones; conserva los demás plugins y guarda `plugins.ini.xbox_pad.bak`. Si el INI tiene secciones duplicadas o encabezados mezclados, corrígelo por FTP antes de reintentar.
6. Edita el perfil global o el perfil del CUSA, configura Botones y Sticks y pulsa Triángulo para guardar.
7. Cierra el configurador y abre el juego. Si el Xbox no recibe entradas al iniciar, prueba desconectarlo y volverlo a conectar y guarda el log para el diagnóstico.

L1/R1 cambian de pantalla. Cruz ejecuta la acción seleccionada. El asterisco indica cambios pendientes. La instalación del PRX necesita reiniciar el juego. El configurador se maneja con DualShock y no abre el USB Xbox.

## Configuración

Archivo compartido: `/data/GoldHEN/xbox_pad/config.ini`. `version=1`, `diagnostic=0` preceden `[global]`; los perfiles por CUSA contienen una copia completa de los ajustes globales al crearlos y después se editan independientemente. Hasta 32 perfiles. Una configuración inválida conserva la anterior; al arrancar sin configuración se usan valores predeterminados.

Mapas `map0` a `map16`: A, B, X, Y, arriba, derecha, abajo, izquierda, LB, RB, LS, RS, Menu, View, Xbox, LT, RT. Destinos 0–16: Cruz, Círculo, Cuadrado, Triángulo, arriba, derecha, abajo, izquierda, L1, R1, L3, R3, Options, clic touchpad, bit PS, L2, R2. Destino 17 desactiva. La salida del bit PS solo llega al juego: no implementa el menú del sistema.

`dead_left` / `dead_right`: 0–64, predeterminado 16; `invert0`–`invert3`: inversión de cada eje de salida; `swap`: intercambia sticks. Fuera de la zona muerta el eje conserva su amplitud. Los gatillos preservan presión al asignarse a L2/R2; botones digitales producen presión completa. Las entradas Xbox se mapean antes de combinarse con el DualShock.

El plugin recarga cada segundo desde el hilo USB. Lecturas originales fallidas o desconectadas se conservan sin inventar datos válidos, a diferencia del comportamiento experimental 2.10. El plugin solo se inicia en títulos CUSA y rechaza la descarga mientras el juego está ejecutándose.

## Compilar

Linux x86-64: clang, lld, llvm, g++, make, git, curl, python3 y dpkg-deb.

```sh
bash scripts/setup.sh /tmp/xbox-toolchain
export OO_PS4_TOOLCHAIN=/tmp/xbox-toolchain/oo/OpenOrbis/PS4Toolchain
export GOLDHEN_SDK=/tmp/xbox-toolchain/sdk
export LD_LIBRARY_PATH=/tmp/xbox-toolchain/compat/usr/lib/x86_64-linux-gnu
bash scripts/test.sh
bash scripts/build.sh
python3 scripts/check_exports.py build/sdk/build/source.oelf
```

OpenOrbis v0.5.4 y SDK GoldHEN bcea3c7ef01dac6d7f9f49ebf9e90fe66d86f5f7 están fijados. El empaquetador usa OpenSSL 1.1 aislado y `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1`. Salidas: `build/sdk/xbox_pad.prx`, `build/IV0000-XBPAD0001_00-XBOXPADCONFIG001.pkg` y `build/SHA256SUMS`. GitHub Actions publica estos archivos cuando pasan pruebas, compilación y validación.

## Prueba en consola pendiente

Comprobar inicio con Xbox conectado, reconexión, botones/sticks/gatillos, reasignación A→Círculo, DualShock sin remapear, perfil por juego frente al global, persistencia al reiniciar y restauración del PRX. El sandbox comprueba compilación, paquetes y simulaciones; no ejecuta PS4.

## Créditos

GoldHEN Plugins SDK y OpenOrbis Toolchain; el renderer se compila desde las muestras OpenOrbis. Gontserrat se incluye con OFL. El icono geométrico se generó para este proyecto con CodeRabbit. ACK del botón guía contrastado con el controlador Linux xpad (`xpadone_ack_mode_report`): https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c . Conservamos GPLv3 para el proyecto y sus componentes derivados; los avisos originales se mantienen en las dependencias.
