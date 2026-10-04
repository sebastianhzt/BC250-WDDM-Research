PCI/PnP ADMISION -- COMPOSICION AISLADA -- 2026-10-04
==================================================
No Build23, paquete instalable, SYS/INF/CAT, firmware, PCI fisico, MMIO, DMA,
registro/BCD ni pruebas contra driver. Build22 y sus retornos no se modifican.
Todo permanece SIN integrar al PnP/power real del controlador.

AVANCE
Reutiliza SIN editar bc250_dma_gate (mutex+critical region+rundown) y
bc250_pci_contract (lease+interfaz+header sintético) del progreso anterior.
Owner con Generation/Epoch inmutables; Gate ticket abarca acquire/query/read/
dereference/release. Cleanup normal: interfaz -> lease backend -> ticket gate.
Wrapper valida que el backend devuelve la misma generacion/epoch antes query.
Observe solo un intento por Owner (Session one-shot); no repetir, reset/retry.
Stop marca razon atomicamente y cierra admision: query-stop, query-remove,
stop, surprise-removal, D0-exit/power-down y remove. Eventos son SOLO fake,
no handlers ni decisiones de IRP ni callbacks power del Windows real.
Mientras gate esta detenido, wrapper rechaza siguientes pasos; una lectura
ya admitida puede seguir en curso, pero se descarta su resultado conocido
obsoleto. Retire solo coordinador EXTERNO sin intento/ticket propio pendiente;
drena llamadas admitidas, verifica ausencia de refs conocidas, marca RETIRED.
Retire en hook/hilo propietario falla BUSY ANTES de reclamar fase.
Stop no toca payload mutable de Session, sólo campos atomic y Gate state.
Quarantine impide retiro: retiene anclas backend inciertas. Soltar ticket NO
libera esas anclas ni autoriza borrar storage. No recuperacion de proveedor roto.
Rundown exitoso no libera mapas/memoria, no prueba quietud de hardware o DMA.

CONTRATO EXTERNO / LIMITES
Owner+backend/contexto y todos los punteros resident/alineados/noalias deben
estar anclados externamente ANTES de API hasta TODOS sus returns (tambien
rechazados y retirador). No se resuelve publicacion/unpublication de pointer.
Sin copiarlos, sin publicar campos Session o Gate a otros consumidores.
Init una vez en almacenamiento cero; sin reinit/cancel-reopen del mismo Owner.
Nuevo START/D0 o cancel requiere NUEVO objeto ya anclado, ids estrictamente
nuevos sin wrap/reuso y retirada/unpublication coordinada del anterior.
Esto es restriccion del candidato, NO flujo cancel/restart real implementado.
No sampler real: ids inmutables+Gate closed son contrato, no lectura atomica
del State/StateEpoch existente en Build22. Falta publisher/controlador PnP.
Todas APIs PASSIVE/nonarbitrary, no thread-exit mientras hay ticket,
ni dependencias de APC propio, wait sobre remover ni nested gates.
Gate mutex y critical region siguen retenidos durante callbacks backend; una
futura consulta sincrona debe demostrar que no necesita APC/remover ni lockorder.
No se llama con BindingLock. No espera ni IRPs reales, ni PnP/power forwarding.
Stop no cancela read en curso ni demuestra disponibilidad fisica despues de
surprise removal. Antes/despues/publish son historicos y pueden quedar obsoletos
tras ultimo check. Fault/quarantine nunca permite liberar anchors inciertos.
No sostener referencias propias al entrar en Retire. Coordinador conserva
ancla externa, no carga mutex/ticket. Rundown de llamadas no cubre clientes
futuros/direcciones/MDLs/maps/jobs ni convierte resource ownership.
Real Policy siempreFALSE, sin interruptor. Ambos modulos anteriores tambien
tienen puertas permanentes cerradas. Fakes parametrizan policy sólo en RAM.

PRUEBAS / REPRODUCCION
Code Reviewer obligatorio PREBUILD antes de cada compiler por AGENTS.md.
Desde VS2022 x64 / Python3, sin Admin:
  verify-offline.bat C:\ruta\python.exe
Runner unico output/pci-pnp-admission-*; 28 etapas O2/Od /W4 /WX /GS.
8 EXE RAM (composicion, PCI regresion, PCI blocked, gate regresion) y6 OBJ WDK
(/c admission+PCI+gate porperfil). Ningun link kernel o carga/instalacion.
Fakes adaptados de test-gate.c en mock-admission-gate.c con procedencia:
solo cambia include relativo; usa test-adapter.c y gate.c originales.
La regresion gate completa se ejecuta por separado sin editar el predecesor.
Eventos seriales de scheduler/thread index,
NO hilos/atomics/IRQL/referencias/rundown reales ni PnP/power real.
Matriz6razones x7momentos x4interfaces x2reentry x2short=672escenarios/perfil.
Tambien Stop antesEnter/enrundown/enmutex, epoch distinto, PENDING/quarantine,
false policy todasAPIs, self-retire, rechazo nuevoEnter y dobleInit.
Regresiongate incluye escenario coordinador fake con llamada retenida; no
confundir con esperar una consulta Windows real en progreso.
Predecesores Build21/22, PCI contract, gate ytransportDMA hashes antes/despues.
Fuentes/artefactos/logs guardados, RESULT sólo si todasetapasexito y sin cambios.

SIGUIENTE
Publisher/generation y sampler coherente con PnP/power reales, lock-order/
completion/rundown de consultas en stack correcto antes de backend hardware.
No incluir o activar estos OBJ en SYS sólo porque la campana RAM pasa.
No instalación nueva en este paso; conservar Build22 como checkpoint.

ATRIBUCION
Original Sebastian, Apache-2.0 proyecto Keshas-dev; conserva LICENSE/CREDITS.
D-Ogi/MetalCyan referencias, no se copio codigo/firmware externo.
Contrato Microsoft de rundown documentado en primary-source.json.
