CONTRATO FUTURO DE CONSUMIDOR DMA WINDOWS - 2026-10-04
Baseline ddb76daf248b55303505905f0ac7589a8512f91f / rama development.
Incremento SOLO de diseno y auditoria de fuentes. NO implementa un consumidor.
Build11, controlador instalado, main, build.bat y candidatos anteriores intactos.
NO SYS/INF/CAT, firma, instalacion, IOCTL, MMIO, PCI ni GPU. NO permisos de DMA.
Implementacion original Apache-2.0; atribucion previa en
research/windows/domain-backend-20261003/CREDITS.txt. NO replica codigo externo.

Que queda demostrado y que NO
La asociacion previa preserva una referencia Snapshot agregada mientras existen
mapas/backing NUMERICOS CPU. Su campaña RAM se aprobo en el commit baseline:
24 suites / 55 etapas / 1414 controles nuevos; NO se repite ni se amplia aqui.
Gate controla llamadas; Snapshot controla obligaciones numericas. Ninguno toma
por si solo una referencia PDO, IRP o propiedad de paginas ni prueba reposo GPU.
El consumidor actual no inicia transferencias. Native Cancel es una marca serial
y retorno de recursos, NO CancelAdapterChannel ni cancelacion de trabajo GPU.
El simple Drop, refs0, Stop, timeout o fallo de transferencia NO permite liberar
memoria que aun pueda usar un dispositivo. FAULT retenido = NO recuperacion real.
Cambiar UMA 4/6 GiB no satisface ninguna de estas garantias.

El resultado util de este paso
requirements.json enlaza 20 obligaciones con fuentes protegidas, dependencias,
aceptacion futura y referencias primarias. Los estados design/mock/pending NO
son garantias Windows. real_guarantee_verified sigue false para TODOS.
Es una lista de revision, no una politica consumida por KMD, no aprobacion
automatica de hardware ni justificacion para abrir los tres gates FALSE.
verify-contract.py verifica scope exacto de CINCO archivos y baseline, pins LF,
anclas de evidencia y cuerpos policy originales. Pruebas negativas rechazan
promocion de evidencia, borrar obligaciones, orden roto, ciclos/duplicados,
booleanos ambiguos, rutas ajenas, modificacion de fuentes y permisos nuevos.
PASS significa documento coherente; verdict sigue BLOCKED_FOR_REAL_DMA.

Etapa 1: adquisicion SIN transferencia (A01-A11)
Primero resolver publicacion/retirada del owner con ancla externa; PDO, IRP,
origen y lifetime de MDL, permisos y proceso; capabilities Windows; request
unico; SG inmutable; serializacion real, cancelacion/stop/remove/power.
La entrada usa un MDL de procedencia establecida, nunca un puntero de usuario
convertido en owned por un flag. Una rutina que recibe MDL prestado no puede
desbloquearlo/liberarlo por cuenta propia. Elegir direct I/O vs allocation
propia y documentar quien mantiene memoria/IRP hasta despues del ultimo acceso.
No insertar MmProbeAndLockPages en el IOCTL METHOD_BUFFERED como atajo.
Propuesta siguiente: disenar y probar primero el ancla publica de owner/IRP
(incluidos entrants rechazados y cancelacion), todavia sin GPU ni ejecucion OS.
Solo despues revisar integracion separada acquire/release sin transferencia,
Verifier/leaks y rollback. Este documento NO autoriza esa integracion instalada.

Etapa 2: consumidor y retiro real (T01-T03, G01-G02, R01-R04)
Nunca conectar header snapshot-cpu a KMD: exige mocks y rechaza _KERNEL_MODE.
El contrato version3 sincronico GetScatterGatherListEx SIN ExecutionRoutine
exige retorno explicito de los recursos con FreeAdapterObject. La liberacion
de recursos no sustituye finalizacion del dispositivo ni coherencia final.
Para transferencias futuras: determinar flush/sync, direccion y parametros de
la peticion original; MapRegisterBase necesita origen documentado. NO deducir
ese handle de SG, TransferContext o un numero CPU. La interfaz publica actual
NO lo exporta: revisar una interfaz nueva, no acceder a Native internals.
No llamar FlushAdapterBuffersEx sobre una transferencia aun activa como prueba
de cancelacion/fin. El contrato de final-sync exige quiescencia establecida.
DMA_LOGICAL tampoco demuestra traduccion GPU/MC, PTE visibles ni retiro TLB.
W2P/reserva Windows, firmware y scheduler siguen pruebas independientes pendientes.

Orden futuro conservador (propuesta de diseno, NO receta MMIO)
1 cerrar admision; drenar llamadas sin autoespera
2 probar quiescencia del dispositivo y retirar traducciones GPU
3 retirar mapas CPU/backing; sincronizacion final DMA conocida
4 soltar Snapshot; devolver recursos DMA
5 liberar SOLO MDL/paginas propios y completar IRP una vez
6 retirar el ancla externa cuando todos los accesos/retornos finalicen
Algunas fases solo existen si hubo publicacion/transferencia. No omitirlas porque
el modelo RAM no las represente. Fallo desconocido bloquea fases posteriores.

Reproducir este paso (Python, sin admin ni VS/WDK)
python -B research/windows/dma-consumer-contract-20261004/verify-contract.py
python -B research/windows/dma-consumer-contract-20261004/test-contract.py
Solo genera RESULT.json en output/dma-consumer-contract-*; no abre driver.
compiler_invoked y c_tests_rerun son false. No nueva compilacion del driver.
Fuentes y reporte se revisan antes de publicar SOLO development.
primary-source.json conserva URLs oficiales revisadas 2026-10-04: Get/Free/
FlushAdapterBuffersEx, MDL y remove locks. Criterios GPU propios NO certificados
por esas paginas. El checker NO navega, NO verifica BC250 y NO autoriza hardware.
