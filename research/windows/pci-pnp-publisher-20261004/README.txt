PCI/PnP PUBLISHER -- CANDIDATO AISLADO -- 2026-10-04
==================================================
NO Build23, SYS/INF/CAT, instalacion, IOCTL, PCI fisico, MMIO, DMA ni firmware.
Build22 instalada y sus retornos siguen intactos. La politica nativa siempre
FALSE, sin registro/environment/IOCTL para habilitarla.

AVANCE
Publisher raiz con spinlock corto protege Current/Draining y registro de
16 tickets exactos (direccion+hilo+id64) que conservan el Binding durante
TODOS los returns de Owner, incluidos rechazados. Tickets no copiables.
PinRead Busy retiene ancla durante callbacks y copia final; Unpin BUSY falla.
Callbacks, Owner APIs, rundown y esperas NUNCA bajo spinlock.
Binding contiene Owner anterior y recursos inmutables de una misma Generation/
Epoch. PinRead compone observacion y recursos y usa Correlate congelado.
Copia final historica se acepta bajo lock sólo si sigue publicado y Live.
Una transicion observada invalida resultado; no snapshot atomico de hardware.
Close retira Current y retiene Draining/Closing; OwnerStop fuera lock.
Retire sólo Draining sin pins: BUSY sin esperar si cualquier pin sigue vivo.
OwnerRetire fuera lock; exito desprende binding, fallo/cuarentena retiene anclas.
Nuevo Publish exige old detach y Generation Y Epoch estrictamente mayores.
Los ids conservan high-water incluso tras detach; cero/MAX/wrap rechazados.
Owner sigue one-shot: 16 pins NO significan 16 consultas exitosas.

PRECONDICIONES / LO QUE NO DEMUESTRA
Root debe permanecer anclado externamente antes de toda API hasta todos sus
returns, incluso fallidos. Esto no resuelve publicar/borrar el propio Root.
Prepare Y Publish del mismo Binding son exclusivos del productor; elegir un
unico Root. Prohibido Publish concurrente cross-root (locks distintos), otras
APIs Owner/Prepare/acceso producer mientras se transfiere. No CAS global real.
Producer mantiene backing storage+backend/contexto/PDO desde Prepare, cede
gestion exclusiva al Root tras Publish y sólo recupera storage DESPUES de
Retire exitoso retornado. NO allocator, ObReference/DereferenceObject, ancla
PDO real, extension WDM, publicacion/cancel/restart Windows implementados.
Failed Prepare y Fault no permiten destruir referencias inciertas.
Binding/Root/pin/output/code resident/aligned/kernel-owned/noalias/trusted.
No campos o callbacks user-controlled, pin compartido ni APC reentry/thread-exit.
Nadie llama Owner/Session/Gate directamente ni modifica Binding tras Publish.
Sólo Close permitido en hooks backend. Ningun lock externo anidado/removerwait.
Gate anterior conserva mutex/critical region durante backend; no dependencias
de APC propio. Completion/lock-order con consultas IRP reales sigue pendiente.
Closed/Draining no cancela read en curso ni prueba disponibilidad fisica.
Detach no es permiso GPU/DMA/VRAM ni prueba quietud mapas/jobs/MDLs.
Eventos son seriales fake: no validacion SMP/atomics/rundown/IRQL Windows real.
Resources es tuple sintético inmutable; NO sampler del State/StateEpoch ni
ResourceSnapshot actual Build22. No convertir coincidencia en ownership.

PRUEBAS
Code Reviewer PREBUILD antes de cada compiler, sin Admin.
verify-offline.bat C:\ruta\python.exe en su carpeta dentro del repo completo.
40 etapas /O2 y /Od /W4 /WX /GS:
12 EXE RAM: publisher, publisher closed, admission, PCI, PCI closed, gate.
8 OBJ WDK /c: publisher/admission/PCI/gate porperfil, macros fake /U.
Sin SYS link, carga, instalacion, IOCTL ni lectura fisica.
Matriz6razones x7momentos x6modos x2reentry =504 escenarios/perfil:
stop durante acquire/still/query/read/deref/release; short, missingderef,
dirtyfailure, pending y BAR/resource mismatch. Tambien cierre en curso,
retire mientras pinned/busy, 16pins/capacidad, wrongthread/copied/staleid,
retirada y nueva generacion, epoch reutilizado, id agotado, one-shot,
IRQL rechazado y nativePolicyFALSE todas8APIs sin calls.
Fakes Ke spin sólo modelan exclusividad serial/IRQLsimulado.
Regresiones anteriores completas se ejecutan aparte SIN editar predecesores.
10 fuentes y75 fuentes protegidas snapshot SHA antes/despues. RESULT sólo
en campaña completa sin cambios. Output unico output/pci-pnp-publisher-*.
No se debe ejecutar la copia D: fuera del repositorio: necesita predecesores.

SIGUIENTE
Adaptador de captura coherente de PnP/power/recursos y ancla real de PDO/backing
storage, con referencias/completion/lock-order revisados; primero simulado.
No activar publisher u objetos en el driver sólo por pasar CPU o WDK /c.
Mantener Build22 DISARMED mientras W2P no demuestre ownership y traduccion.

ATRIBUCION
Sebastian, Apache-2.0 proyecto basado en Keshas-dev; LICENSE/CREDITS intactos.
Fakes y modulos previos originales del proyecto; no codigo o blobs externos
añadidos desde D-Ogi/MetalCyan, que siguen como referencias.
Contratos Microsoft en primary-source.json.
