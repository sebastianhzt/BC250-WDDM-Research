Windows gate + DMA resource composition candidate - 2026-10-04
============================================================
Build11, driver instalado, main, modelos y fuentes anteriores intactos.
NO SYS/INF/CAT, link/carga/instalacion, PCI/MMIO/GPU o comandos al hardware.
Fuente original standalone /c; policy de produccion FALSE antes de DDIs.
NO prueba de concurrencia real, DMA Windows, PnP/Verifier, W2P o UMA.

Contrato limitado: NO consumidores
---------------------------------
Bundle contiene Gate y Native congelados. Gate controla llamadas, no mapas.
No exporta List, direccion, handle/span, reserva VRAM ni jobs GPU. PROHIBIDO
conectar bridge/modelos/map refs o consumidores a Native: limpieza tras gate
es valida SOLO en este contrato artificial sin consumidores pendientes.
Todos los accesos exclusivamente via APIs bundle, no raw Gate/Native.
Init exclusivo con memoria residente/alineada, cero una vez. Sin reset,
republicacion, reintento Init o anidar gates. PDO/descripcion y MDL locked
validos, estables y alejados de TODO el bundle.
El lifetime externo mantiene bundle/PDO/MDL/tickets vivos desde ANTES de
cada API hasta DESPUES de todos los retornos (tambien fallidos y retirador).
ExWait espera protected accesses, NO el retorno completo de APIs exteriores.
Acquire SUCCESS describe su punto de operacion; NO promete seguir HELD al
observar su retorno si Stop/Retire posteriores ya ocurrieron. No usar campos
internos como capacidad. El caller sigue sin permiso de usar direcciones GPU.

Flujo
-----
Init Gate -> ticket protegido -> Native Open -> Finish -> GateLeave.
Acquire/Release bajo ticket/region critica/mutex PASSIVE no arbitrario.
Finish publica cuarentena/fault ANTES del ultimo GateLeave. Tras Leave
exitoso NO accede bundle; un fallo de Leave conserva referencia y envenena.
SOLO Stop legal desde hooks, sin tocar Native ni liberar; todas APIs PASSIVE.
Retire por coordinador externo sin referencias propias/in-flight: CAS unico,
Stop -> Quiesce -> REVALIDAR FAULT -> NativeCancel -> NativeClose -> RETIRED.
Reentrada desde provider en Open/Get/Free/Put no puede entrar otra llamada
ni retirar prematuramente; durante cleanup el gate ya esta cerrado y sin
rundown, y el CAS mantiene exclusividad del coordinador.
Trabajo admitido puede terminar tras Stop. Release normal queda bloqueado
despues Stop; en este bundle sin consumidores la limpieza la hace Retire.
Con mapas reales esto NO seria suficiente ni una ruta WDDM utilizable.

Fallos
------
Native exactamente TODO cero acredita failed-Open sin handle y no llama
Cancel/Close inexistentes. Fallos conocidos sin recurso quedan retirables.
List invalida sigue limpieza sincrona existente exactamente una vez.
QUARANTINED (pending inesperado/list en fallo/provider sin Put), gate ambiguo,
o cleanup fallido conservan recursos/storage: NO recuperacion, free supuesto,
reset, salida segura de hilo o proof de terminacion. Fault posterior mientras
coordinador espera impide cualquier cleanup cuando vuelve ExWait.
Discard del mundo RAM de fallo es teardown falso, nunca recuperacion Windows.

Pruebas y compilacion
--------------------
Fixtures propios originales reutilizados de gate/native sin cambiar fuentes
previas. Thread IDs/atomicos/rundown son eventos deterministas, NO SMP real.
19 suites RAM / 43 etapas: audit/catalogo + 19 compile/run + 3 OBJ WDK:
native anterior, gate anterior y composicion nueva. /W4 /WX; macros fake /U,
CL/_CL_ no vacios abortan. No RAM models compilan kernel, ningun OBJ enlazado.
Stop dentro Open/Initialize/Get; Free/Put solo tras barrier en Retire;
reentrada, retiro simulado esperando llamada, fault tardio, errores conocidos
y ambiguos, side-effects policy cerrada/IRQL/alias, 1..64 SG entradas.
AUDIT guarda siete hashes/guardas; RESULT todas entradas/22artefactos.
Logs y binarios locales ignorados en output; solo fuentes publicables.
En x64 Native Tools Command Prompt VS 2022, sin administrador:
    python -B research\windows\dma-resource-20261004\verify-offline.py

Siguiente: contrato para consumidores/leases que sobrevivan a una llamada y
limpieza diferida por esos leases. No habilitar policy ni instalar driver.

Procedencia
-----------
Codigo nuevo original Apache-2.0; gate/native y mocks previos congelados.
No nuevas DDIs ni implementacion externa copiada. primary-source.json
enlaza fuentes locales y manifiestos de referencias oficiales Microsoft.
research/windows/domain-backend-20261003/CREDITS.txt y LICENSE preservados:
atribuciones a Keshas, D-Ogi y MetalCyan y sus limitaciones siguen vigentes.
