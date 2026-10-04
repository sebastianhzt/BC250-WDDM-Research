Windows DMA access gate candidate - 2026-10-04
=============================================
Build11, controlador instalado, main, modelos RAM y fuentes previas intactos.
NO SYS/INF/CAT, link de driver, instalacion, DMA, PCI, IOCTL o acceso GPU.
La fuente real se prueba con DDIs falsos y se compila /c contra WDK; su policy
de produccion es FALSE permanente ANTES de KeGetCurrentIrql o cualquier DDI.
NO prueba de concurrencia real, PnP, Driver Verifier o seguridad de punteros.

Contrato
--------
Gate residente/alineado, cero una vez, identidad estable. Init exclusivo;
no reset/restart. El lifetime externo mantiene gate y ticket validos desde
ANTES de entrar a CADA API hasta DESPUES de volver, incluidos rechazados,
Stop y Quiesce. Publicacion/retiro del puntero, referencia PDO y barrera PnP
NO se implementan: rundown por si solo no protege un puntero ya liberado.
Solo callers internos confiables, no campos de usuarios ni capacidades.
Un ticket externo cero y unico por llamada. Enter y Leave en MISMO hilo
PASSIVE no arbitrario; no terminar/suspender manualmente hilo con ticket.
No esperar al retirador mientras se tiene el ticket; no invertir orden locks;
no anidar gates. SOLO Stop se permite desde hooks. Quiesce externo requiere
NO referencia propia, tampoco intentos Enter/Leave en progreso en ese hilo;
OwnerThread detecta ticket ya publicado, NO todos los casos de in-flight.
APCs especiales siguen posibles; el caller no debe hacer Quiesce desde ellas.

Enter: region critica -> rundown -> admision RUNNING -> mutex try con timeout
cero -> segunda comprobacion -> ticket canonico. Recursion es BUSY; otro hilo
ocupado tambien BUSY (no cola, fairness o retry implementados). La region
critica, mutex y rundown se mantienen hasta Leave; IRQL permanece PASSIVE.
Leave valida hilo/identidad/ID, limpia metadata y ticket, suelta mutex y luego
rundown; NO toca gate/ticket despues de soltar RunRef. IDs no envuelven.
Stop CAS RUNNING->STOPPING sin esperar; cierra admision nueva. El punto de
admision es la observacion RUNNING despues del mutex: un Stop posterior
puede coexistir con ticket ya admitido, que debe terminar con Leave.
Quiesce requiere Stop, coordina un unico retirador mediante CAS, espera
rundown y marca CLOSED. No restart. Puede esperar indefinidamente si caller
incumple Leave; no timeout que finja terminacion.

NO permiso de liberar DMA
------------------------
Este gate controla llamadas, NO referencias de mapas ni recursos pendientes.
No conoce bridge, reservas VRAM, SG, MDL, map leases o jobs GPU. Quiesce no
libera adaptador ni reserva, no prueba reposo GPU ni ownership W2P. Los
modelos anteriores de mapas/retirada no estan unidos a este gate en kernel.
Composicion fake: candidato DMA anterior permanece HELD tras Leave/Quiesce;
su teardown posterior directo es fixture RAM, NO flujo PnP autorizado.

Un estado inesperado de KeWait (no SUCCESS/TIMEOUT) envenena gate y conserva
RunRef/storage, SIN presumir si el mutex fue adquirido. Sale de region
critica explicita; un mutex adquirido podria seguir retenido por ese hilo.
No intentar liberar, reutilizar, borrar storage, terminar hilo o inferir
recuperacion Windows. La prueba descarta un mundo falso, no limpia OS.

Aceptacion
----------
18 suites RAM, 40 etapas: audit/catalogo + 18 compile/run + dos OBJ WDK.
Un OBJ del candidato DMA previo y otro de este gate. Todos /W4 /WX.
Los modelos RAM NO se compilan kernel. CL/_CL_ no vacios rechazan campana;
ambos macros fake explicitamente /U en compilacion WDK.
Fake threads/rundown/mutex/atomicos son eventos deterministas, NO scheduler,
ABI WDK o prueba de carreras/SMP. Se prueban Stop entre acquire y mutex,
Stop tras mutex, recursion, otro hilo ocupado, Leave ajeno/copias/ID,
coordinador esperando fin simulado, cero side effects con policy cerrada,
IRQL falso, alias, agotamiento y cuarentena; 256 generaciones monotonic.
RESULT guarda todas las fuentes/20 artefactos y AUDIT ocho fuentes/guardas.
Los logs/binarios locales quedan ignorados en output.
En x64 Native Tools Command Prompt VS 2022, sin administrador:
    python -B research\windows\dma-gate-20261004\verify-offline.py

Siguiente: combinar contratos de admision y cleanup separado antes de poder
adquirir un recurso Windows; aun no habilitar esta policy ni integrar KMD.

Procedencia
-----------
Implementacion original Apache-2.0, fixtures propios previos congelados.
Referencias oficiales Microsoft en primary-source.json; no implementacion
externa nueva copiada. research/windows/domain-backend-20261003/CREDITS.txt
y limitaciones/atribuciones a Keshas, D-Ogi y MetalCyan se conservan.
