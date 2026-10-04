BC250 - variante DMA con referencias persistentes, 2026-10-04
SPDX-License-Identifier: Apache-2.0

Alcance
Variante nueva e independiente que compone Gate + Native congelados. El Resource
anterior prohibe consumidores y NO se modifica ni se amplian sus garantias.
Referencias exclusivamente de metadatos confiables: NO consumidores reales,
NO mapeos, direcciones GPU/MC, trabajos SDMA, referencias OS ni frontera de
seguridad. No hay API que exporte List, MDL o direcciones; son datos privados.
La politica de ejecucion en produccion siempre devuelve FALSE antes de DDIs.
NO SYS/INF/CAT; NO paquete instalable, carga, registro, PnP o acceso al hardware.
Build11 y controlador instalado quedan intactos. UMA 4/6 GiB y W2P no validados.

Contrato
Init exclusivo, almacenamiento alineado/residente/cero una sola vez; sin reset
ni reinicio. lifetime externo del objeto, PDO, MDL bloqueado y buffers desde
ANTES de cada entrada hasta TODOS los retornos y durante TODAS las referencias.
El contador no implementa publicacion/delecion de objetos ni mantiene por si
solo memoria OS. Handles confiables, buffers estables; no acceso a campos crudos.
Una referencia representa la obligacion de no retirar el recurso; no concede
derecho a usar memoria/DMA. FAULT invalida cualquier promesa de estado operativo.
SOLO Stop es legal desde hooks/llamadas en curso. Coordinador externo de Retire
no conserva referencias propias ni operaciones en vuelo. Todos los participantes
son hilos PASSIVE no arbitrarios, sin inversion de locks ni salida con locks.
No se demuestra que un motor GPU haya terminado ni se implementa cancelacion
asincrona. No reutilizar esta variante en el controlador sin otra revision.

Flujo
Acquire publica la referencia inicial contada ANTES del ultimo GateLeave,
incluso si Stop ocurre dentro del proveedor tras admitir la llamada.
Retain exige admision activa y otra referencia valida. Hasta 16 slots, IDs
monotonicos sin wrap; salida completamente cero y disjunta de entrada, objeto,
MDL y lista SG. Referencias antiguas/duplicadas no decrementan el contador.
Gate -> Metadata. Drop solo Metadata; no espera Gate ni lee estado Native.
Metadatos guardan limites CPU privados del MDL/lista para rechazar alias.
Mutex de metadatos try-only con recursion rechazada; BUSY requiere reintento,
sin garantia de equidad. Regiones criticas equilibradas en rutas conocidas.
Stop cierra nuevas admisiones, no libera.
Un Stop ya iniciado que se retrasa hasta RETIRED no envenena el estado terminal;
un Stop NUEVO tras RETIRED se rechaza sin cambiarlo.
Retire espera primero el rundown de
Gate, luego toma Metadata y vuelve a validar DRAINED/FAULT y contabilidad.
Si quedan referencias: BUSY en DRAINED, conserva MDL/lista/adaptador, permite
Drop y otro intento Retire; nunca reabre Gate.
Drop nunca libera DMA, ni siquiera al soltar la ultima referencia.
Solo Retire con cero referencias hace Cancel y Close, una vez. Estado RETIRED
terminal. Error incierto/cuarentena/contabilidad corrupta conserva recursos;
Drop de referencias validas en FAULT no recupera ni libera Native.
Si una salida se rechaza tras adquirir Native, no se publica referencia y se
conserva el recurso hasta retirada conocida o FAULT; no liberar desde el error.

Pruebas y limites
verify-offline.py requiere consola x64 Native Tools VS 2022 y WDK 10.0.26100.0,
rechaza CL/_CL_ ocultos, valida negativos y conserva hashes antes/despues.
20 suites RAM / 46 etapas / 20 EXE + cuatro OBJ WDK aislados (/c /kernel).
Los EXE emplean DDIs/hilos falsos; los OBJ solo validan tipos contra headers WDK,
no enlazan driver ni prueban ejecucion kernel.
NO prueba de concurrencia real, IRQL real, PnP/rundown real o propiedad W2P.
Simulaciones: parada durante publicacion, drenaje persistente, saturacion,
IDs antiguos, alias, salidas activas, exclusion/reentrada, ultima llamada que
completa durante rundown, cierre concurrente simulado, FAULT tardio y fallos.
El auditor usa inventario de llamadas, orden, pines y mutaciones de guardas:
controles complementarios, no demostracion formal de correccion concurrente.
Resultados/logs/binarios permanecen locales bajo output/dma-leases-*.
Cada compilacion requiere revision previa por Code Reviewer segun AGENTS.md.

Procedencia
Codigo original del proyecto; fixtures propios derivados de etapas congeladas.
No se copia codigo externo nuevo. CREDITS.txt y licencias previas se conservan.
primary-source.json referencia contratos locales y fuentes oficiales previamente
revisadas. Publicacion solo de siete fuentes en development; main no cambia.
