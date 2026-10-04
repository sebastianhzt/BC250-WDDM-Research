SNAPSHOT -> CPU RAM ASSOCIATION (2026-10-04)
SOURCE-ONLY development; Build11 y el driver instalado NO cambian.
NO SYS/INF/CAT, NO firma, NO paquete instalable; NO consumidores reales.
NO MMIO/SDMA/GPU/Windows DMA real; W2P NO queda resuelto.
NO prueba de concurrencia real; NO prueba UMA 4/6 GiB.
Licencia original Apache-2.0; atribucion historica en
research/windows/domain-backend-20261003/CREDITS.txt.
Sin codigo importado de Keshas, D-Ogi o MetalCyan en este incremento.

Objetivo y limites
La fachada nueva usa exclusivamente Snapshot/SnapshotRelease/Stop/Retire PUBLICOS.
Nunca lee Native/Gate/Metadata/Records/MDL/list internos del recurso Capture.
Una referencia AGREGADA de Snapshot protege todos los mapas NUMERICOS CPU.
NO hay una referencia DMA por mapa ni un consumidor real de Windows.
El modelo CPU mantiene sus propias referencias por alias; direccion repetida
en SG conserva orden y multiplicidad, sin inferir propiedad de memoria.
SnapshotRelease original NO sabe nada de mapas CPU: solo esta fachada aplaza
su llamada hasta retirar TODOS los mapas y el registro backing numerico.
Eso NO endurece el contrato del recurso Capture para otros consumidores.

Contrato exclusivo y lifetime externo
Antes de cualquier llamada, todo el bundle, recurso, Session, nodos, MDL/list
originales retenidos por Capture y TODOS los buffers son honestos, validos,
estables, disjuntos y anclados externamente hasta TODOS los retornos y deudas
Snapshot/mapas/retiro. La comprobacion de rangos no prueba punteros arbitrarios,
propiedad OS, rundown de proveedor ni exclusion SMP.
Init una sola vez sobre facade cero y Session READY vacia (sin backing/mapas).
Self exige direccion canonica; copiar/mover no es API, ni capacidad inviolable.
Tras intentar Init: SOLO APIs Sc; se admite Drop de la semilla ordinaria UNA vez,
tambien si Init falla. Nunca raw SnapshotRelease/Retire/Session ni retarget,
edicion de Snapshot, reset o reutilizacion mientras existan obligaciones.
Callbacks allocator honestos, Free infalible; desde hooks SOLO Stop Sc.
Los tests tambien intentan otras llamadas para verificar BUSY, no para admitirlas.
Stop durante Map ya admitido puede dejar terminar la transaccion; no mapas nuevos.
SessionShutdown solo DESPUES de Sc DEAD, no con obligaciones pendientes.

Estados y retiro
Retire cierra admision antes de comprobar mapas. IN_USE mantiene backing y Snapshot.
Cero mapas -> SessionRelease conocido -> SnapshotRelease conocido -> CaptureRetire.
BUSY conserva las fases completadas y NO repite la liberacion backing ni Snapshot.
Semilla aun retenida puede provocar BUSY en CaptureRetire: Drop UNA vez y reintentar.
Init con error conocido y Snapshot cero deja STOPPED; Retire puede drenar.
Init puede fallar DESPUES de publicar Snapshot LIVE: FAULT retiene la referencia,
aunque devuelva error. No ignorar el output ni liberar anclas por ver un error.
Fallo desconocido/corrupcion -> FAULT retenido, sin API de recuperacion.
Fallo de rollback CPU conocido conserva Session consistente; no se pierden mapas.
El transporte no se libera hasta su Retire final confirmado, no solo SnapshotRelease.
Los universos con FAULT de los tests se descartan sin limpiar el transporte ficticio:
eso es instrumentacion RAM, NO estrategia de recuperacion de un recurso real.

Verificacion reproducible
Desde x64 Native Tools VS2022:
python -B research/windows/snapshot-cpu-20261004/verify-offline.py
Requiere revision Code Reviewer PREBUILD; POSTBUILD antes de publicar.
Auditoria exacta de SEIS archivos nuevos, guardas negativas y baseline congelado.
24 suites RAM / 55 etapas; /W4 /WX /O2; cinco OBJ WDK standalone previos
con /c /kernel /Od. La nueva fachada #error _KERNEL_MODE y exige los TRES mocks:
NO se compila como unidad WDK, NO se enlaza/carga/instala.
RESULT/AUDIT/logs/hashes solo en output/snapshot-cpu-* (ignorados por Git).
El verificador preserva fuentes durante la ejecucion y no invoca build.bat.
