BC250 - asociacion RAM de mapas CPU con leases persistentes, 2026-10-04
SPDX-License-Identifier: Apache-2.0

Nuevo adaptador RAM-only. NO mapas Windows/GPU, NO DMA real, NO W2P resuelto.
Se asocian mapas de la Session CPU congelada con referencias de la variante
Leases congelada mediante SOLO APIs publicas. No se inspecciona Native/Gate,
no se copia List/MDL/direcciones ni se cambia su contrato. Las paginas CPU son
numeros SINTETICOS independientes: no prueban pertenencia al recurso DMA.
Leases sigue sin consumidores reales. El adaptador requiere tres macros MOCK
y rechaza _KERNEL_MODE con #error. NUNCA compilar esta fachada como kernel.
Politicas de produccion de los candidatos originales permanecen FALSE.
NO SYS/INF/CAT ni paquete instalable. Build11, driver instalado, registro,
PnP y hardware quedan intactos; UMA 4/6 GiB y traduccion no validados.

Contrato de lifetime externo
Fachada cero una vez, direcciones estables, acceso EXCLUSIVO durante TODA llamada.
Resource adquirido falso, Session CPU consistente sin mapas existentes,
backing numerico previamente registrado y seed valido; Init retiene una raiz
propia. El caller puede soltar UNA VEZ su seed despues de Init correcto; nada
mas de acceso raw a Session/Leases, campos o handles internos. Solo fachada
Map/Unmap/Drain/Stop/Retire. Retire de Session tras retiro correcto de fachada.
Objetos, nodos, PDO/MDL bloqueado y todos buffers permanecen validos antes de
entrada, durante retornos y todas las obligaciones persistentes. Fachada,
Resource, Session/nodos, MDL/lista y buffers disjuntos, estables y confiables.
Validar rangos no prueba que un puntero sea valido ni es frontera de seguridad.
SOLO Stop de fachada es legal desde callbacks. BUSY es reentrada sincrona,
NO prueba de concurrencia real, atomicidad, scheduler, IRQL/PnP real o rundown
de proveedor. No reiniciar/reinicializar/reutilizar identidad con handles viejos.

Orden
Se reserva record y se retiene Dma ANTES de Map CPU; commit devuelve handle
propio, no handle DMA. Cada alias de VA conserva una referencia independiente.
Fallo Map con Session consistente revierte referencia provisional; contadores
ID pueden avanzar, jamas se rebobinan. Si Drop es BUSY, conserva PENDING_DROP
sin publicar salida, detiene nuevos mapas y Drain permite completar la deuda.
Rollback incierto/corrupto conserva referencia y FAULT, sin recuperacion.
Unmap CPU se completa ANTES de Drop. Unmap fallido conserva arbol, mapa y lease.
CPU Unmap correcto + Drop BUSY elimina el handle CPU y guarda PENDING_DROP;
reintentar Unmap/Drain solo repite Drop, no vuelve a tocar el arbol.
Stop desde callback permite acabar Map preadmitido, cierra nuevos y drena.
15 mapas como maximo si solo queda la raiz: los otros leases externos reducen
capacidad de los 16 slots del Resource. IDs monotonicos, salida cero obligatoria,
duplicados/viejos y alias fachada/Resource/Session/nodos/journal rechazados.
Retire drena pendientes y espera todos los mapas. Recuerda BackingReleased y
RootDropped: no repite etapas irreversibles si otro lease externo mantiene
Native LeasesRetire en BUSY. Root permanece hasta retirar todo backing CPU.
Drop final NO libera: SOLO coordinador LeasesRetire hace cleanup conocido.
Session vacia NO demuestra Native libre ni motor GPU detenido. FAULT retiene
obligaciones; destruccion/restauracion de fixture RAM en tests NO es recovery.

Pruebas
Consola x64 Native Tools VS2022, WDK10.0.26100.0. verify-offline.py rechaza CL/_CL_.
21 suites RAM, 48 etapas, 21 EXE + cuatro OBJ /c/kernel de candidatos CONGELADOS.
Fachada RAM no se compila como OBJ kernel. Todos /W4 /WX; hashes antes/despues,
guardas mutadas, 7 fuentes y logs locales output/dma-map-leases-*.
Code Reviewer antes de CUALQUIER compilacion segun AGENTS.md.
Regresiones anteriores separadas; tests nuevos incluyen fallos por asignacion/
escritura, reentrada, alias VA, Stop en hook, saturacion/IDstale, pendiente Drop,
retiro por subpasos y retencion con metadata inconsistente.

Upstream
UPSTREAM-REVIEW.txt registra heads y limites actuales de D-Ogi/Mesa, incluyendo
recursos propios/prestados y fences de paging. Referencias de diseno solamente;
NO copia de codigo ni ABI externo. Attribution y CREDITS.txt previos intactos.
Todo codigo nuevo es original. Publicacion solo development, no main/release.
