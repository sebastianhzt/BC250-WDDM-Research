OWNER/REQUEST ANCHOR -- RAM ONLY -- 2026-10-04
Baseline 6e714d55de52cd8513790b7a02164060be0ebdf7, development.
Nuevo modelo ORIGINAL Apache-2.0; NO consumidor kernel.
Build11 instalado y checkpoint separados NO modificados. NO SYS/INF/CAT.
NO PDO/IRP/MDL reales, remove locks, cancel routines, DPC, MMIO o DMA.
NO W2P/UMA/transferencias/GPU/concurrencia real. NO paquete instalable.
Atribucion existente: research/windows/domain-backend-20261003/CREDITS.txt.

Contrato exacto
Router y TODOS los buffers validos/alineados/estables/disjuntos externamente
anclados ANTES de CADA API hasta TODOS los retornos y deudas, incluidos
rejected entrants, retiradores, worker y callback. El root ancla NO lo toma
este modelo: publicacion/PnP/exclusividad real siguen pendientes.
Self y IDs no son capacidades inviolables ni verifican punteros arbitrarios.
Cero una vez y terminal, NO copies/reset/restart/reuso de direcciones.
SOLO APIs publicas: no helpers ni modificar registros/campos.
Whole-call exclusive eventos seriales; NO prueba de carreras SMP o IRQL.
DEAD es estado LOGICO: no free router real, no liberar el ancla por ver DEAD
antes de volver de todos los accesos. Nunca tocar owner/token tras Leave.
OWNED/BORROWED son declaraciones de fixture, NO ownership Windows.

Secuencia simulada
Begin publica deuda de llamada ANTES de Admit. Stop cierra Admit/request nuevo,
NO elimina tickets, workers, device debt o callbacks. Begin tardio aun obtiene
ticket rechazable mientras root no sea DEAD. Ese ticket debe terminar Leave.
Fallos Begin sin ticket requieren root externo hasta volver; no prueban rundown.
Solicitud publicada mantiene memoria declarada aunque la llamada ya tenga Leave.
Cancel solo marca INTENCION; duplicado es idempotente antes de Finish.
Worker/deuda de dispositivo/callback se contabilizan aparte. DeviceDebt=0 es
evento FICTICIO: NO fence ni reposo GPU real. Callback ya existente protege
solicitud hasta CallbackLeave; Finish falla aunque worker y device debt sean cero.
Finish exacto una vez incrementa solo OwnedFreed O BorrowedReturned; no free
real ni unlock prestado. Retire requiere STOPPED, cero llamadas, solicitudes,
workers/deudas y callbacks. Corrupcion/FAULT conserva obligaciones; sin recovery.
Fakes de FAULT/exhaustion se descartan como universos independientes, NO limpieza OS.

Verificacion
Code Reviewer PREBUILD antes de cada compiler; POSTBUILD antes de publicar.
python -B research/windows/owner-anchor-20261004/verify-offline.py
Desde x64 Native Tools VS2022, sin admin. Una suite NUEVA RAM /W4/WX/O2 y /Od
(dos builds, dos ejecuciones) + auditoria. Sin WDK, link/load de driver o install.
Las 24 suites anteriores NO se repiten; sus fuentes/contratos estan congelados.
Auditor scope SEIS fuentes, pins/guardas/negativos. Fuente header exige
BC250_OWNER_ANCHOR_RAM_ONLY y rechaza _KERNEL_MODE; policy RAM antes de punteros.
Artefactos/logs locales solo en output/owner-anchor-* (ignorados).
PASS de esta suite NO satisface A01/A02/A09/A10 como garantias Windows reales.

Cuando reemplazar Build11
Hoy sigue instalada build11/id11, SYS hash399027BFBFE670CF75B0DAE4B3FF319031133181B2B5090637BA0C7E4734F030
(comprobacion local de archivo/registro, sin hablar con la GPU).
Hay paquetes historicos13/14 firmados, pero su propia revision BLOQUEA instalacion;
firma correcta NO demuestra seguridad. El lab privado contiene cambios posteriores
no empaquetados: no se copia ciegamente ni se ejecuta build.bat de auto-install.
La rama publicada NO contiene src/kmd/amdbc250_dream_pnp.c ni su header del
laboratorio privado; build.bat heredado invoca pnputil /install. No confundir
publicacion de modelos con exportacion de todo el KMD/PnP listo para instalar.
Este paso NO prepara ni autoriza una build instalable y NO tiene fecha prometida.
Se puede preparar una nueva build DIAGNOSTICA aislada manteniendo DMA/GPU desarmados
sin esperar aceleracion, pero primero: revisar delta KMD/PnP completo, deny IOCTL,
INF/ACL/no-autoinit; enlazar, revisar/firma CAT/membresia, rollback Build11 conservado
y recuperacion vigente. Luego dar comandos exactos, sin instalar automaticamente.
Una build con DMA ACTIVO requiere ademas las obligaciones runtime pendientes
del contrato20, propiedad Windows/traduccion, quiescencia/sync/retiro reales.
No confundir diagnostica reemplazable con progreso de aceleracion.

Siguiente tecnico
Relacionar este contrato con una barrera de publicacion/retirada real de owner,
ownership IRP/MDL y cancelacion, primero revision de una integracion separada
acquire/release SIN transferencia. Los gates congelados permanecen FALSE.
