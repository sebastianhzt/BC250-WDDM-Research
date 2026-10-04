DMA lifecycle - eventos serializados SOLO RAM - 2026-10-04
=========================================================
Build11 y el controlador instalado no cambian. NO SYS/INF/CAT ni instalacion.
El puente, proveedor candidato, modelos previos y main quedan intactos.
NO lock, atomicos, threads, IRP, remove-lock, rundown real o cancelacion asincrona.
El facade no se compila para kernel: hereda los #error del puente RAM.

Contrato y secuencia
-------------------
Adjuntar una vez a un puente exclusivamente poseido; despues NO llamar al
puente directamente. Objetos, session y buffers validos permanecen estables
y separados hasta terminar todas las llamadas y usos de tickets.
Un ticket es metadata interna confiable, NO referencia OS ni frontera de seguridad.
16 slots, IDs monotonicos sin wrap: Enter -> una llamada Start/Map/Unmap -> End.
La llamada consume Used despues de validar sus buffers externos; un rechazo
previo no consume Used. El resultado delegado, incluso error, si lo consume.
Tambien los tickets sin usar deben terminar con End; no expiran por tiempo.
RequestStop cierra nuevas admisiones START/MAP; trabajo ya admitido puede
ejecutarse despues. UNMAP sigue admitido para limpieza. End NO libera DMA.
Solo RequestStop puede entrar desde hook sincrono; latching de un evento,
NO simulacion de ejecucion paralela ni validacion de proteccion de memoria.

Drain requiere STOP y Active=0 ANTES de cancelar el puente. Despues espera
References=0 de TODOS los mapas; quitar un ticket no elimina un mapa.
Unmap fallido con rollback mantiene mapa, lease y proveedor; nuevo ticket
de limpieza permite reintento. El puente retira backing antes de FreeAdapterObject
y cierra adaptador despues; una retirada no cierra la session compartida.
REMOVED es terminal, no hay restart. FAULT conserva estado ambiguo/MDL:
End puede reconocer fin de llamada, pero Drain no inventa recuperacion.
Teardown del fixture de fallo es destruccion explicita de un mundo RAM sin
callbacks externos; no demuestra que STATUS_PENDING sea cancelable en Windows.
NO prueba de reposo GPU, traduccion DMA/MC, propiedad VRAM W2P o estabilidad UMA.
No reutilizar datos de Linux 6 GiB como propiedad demostrada en Windows 4 GiB.

Pruebas
-------
Parada dentro de Get y hooks de PTE; reentrada bloqueada salvo RequestStop;
tickets activos frente a mapas persistentes; rollback en cada escritura de
unmap; slots llenos, IDs agotados, tokens viejos/ajenos, alias de buffers,
policy/IRQL falsos y metadata corrupta; fases OPEN/PENDING/HELD;
dos proveedores compartiendo session; fallo ambiguo retenido.
17 suites RAM, 37 etapas (auditoria, catalogo, 17 compile/run, 1 OBJ WDK).
Todas con /W4 /WX. El unico OBJ WDK es el candidato anterior, NO este facade.
CL/_CL_ no vacios rechazan la campana. Cada comando conserva stdout/stderr,
argv y codigo; un fallo aborta, sin RESULT.json de aceptacion.
AUDIT.json guarda 6 hashes y controles negativos. RESULT.json guarda hashes
de todas las entradas y 18 artefactos; resultados locales ignorados en output.

En x64 Native Tools Command Prompt for VS 2022 (NO administrador requerido):
    python -B research\windows\dma-lifecycle-20261004\verify-offline.py

Paso posterior: disenar el contrato de serializacion y rundown Windows real,
sin introducir todavia este modelo en DriverEntry, PnP o el driver instalado.

Procedencia
-----------
Implementacion original del proyecto; fixtures originales propios reutilizados
del puente y backend CPU congelados. No se copia implementacion externa nueva.
Licencia Apache-2.0 del codigo nuevo; atribuciones y limitaciones anteriores
en research/windows/domain-backend-20261003/CREDITS.txt permanecen vigentes.
primary-source.json enlaza fuentes locales y manifiesto previo de DDIs.
