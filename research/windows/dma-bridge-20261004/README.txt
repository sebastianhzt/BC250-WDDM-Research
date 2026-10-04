Puente DMA - 2026-10-04 - SOLO RAM

Conecta fuente del candidato Windows con propietario CPU exclusivamente usando
mock-wdm.h y proveedores ficticios. No es un puente ejecutable Windows/WDDM.
NO SYS/INF/CAT, enlace, firma, carga, instalacion, MMIO ni transferencia GPU.
Build11, controlador instalado, main y seis etapas anteriores quedan intactos.
El adaptador real conserva su politica FALSE. El puente exige plataforma MOCK
y rechaza _KERNEL_MODE: los modelos CPU NO estan integrados en kernel.

Contrato: objetos inicializados a cero UNA VEZ, identidad estable, buffers y
proveedor confiables/estables/disjuntos, acceso exclusivamente SERIALIZADO.
NO lock: Busy rechaza reentrada sincrona, NO sincroniza dos hilos, PnP ni IRPs.
Todos los objetos y MDL ficticios sobreviven a la ultima referencia/cierre.
No invocar APIs internas directamente ni editar el estado/lista del adaptador.
El puente recibe permiso de inspeccion interno UNICAMENTE en esta simulacion.
La lista ficticia tiene capacidad fija64; no es el ABI variable del WDK.

Ciclo:
- Init abre un adaptador ficticio y conecta el callback Put del propietario.
- Begin admite un unico intento. Acquire retiene el recurso, sin publicarlo.
- Publish copia spans DMA-logical, conserva orden/duplicados y los registra en
  el propietario. No convierte DMA a CPU PA, MC, FB ni GPU-VA ni exporta handles.
- Map adquiere una referencia por mapa (tambien alias). Cancel en READY pasa a
  DRAINING, bloquea nuevos Map pero permite Unmap. NUNCA llama Cancel nativo en
  ese estado: tal llamada liberaria el recurso antes de retirar referencias.
- Release rechaza mapas vivos; tras Unmap exitoso de todos, quita el backing
  numerico y llama Release del transporte una sola vez. Close libera despues
  el identificador del adaptador, no antes.

Casos de cancelacion: antes de Begin, antes de Acquire, entre Acquire/Publish y
despues de Publish. No hay cancelacion concurrente ni callbacks pendientes.
Fallo sin recurso resuelve el ticket solo al volver del transporte sincrono en
estado conocido OPEN/listaNULL. QUARANTINED nunca prueba ausencia de recurso.
Si falla Publish, el propietario consume el cookie y lo devuelve sin mapas.
Si un cleanup nativo inesperadamente falla, el propietario ya pudo olvidar el
backing numerico; el puente informa FAULT y retiene el transporte/MDL. No hay
recuperacion publica para esa violacion. No se oculta el fallo de un Put void.
Close permite limpiar un Init fallido sin handle, NO un fallo tras Begin.
La limpieza manual de mundos ficticios rotos en test-bridge.c es instrumentacion,
NO una estrategia de recuperacion Windows ni autorizacion para saltarse el facade.

Pruebas: proveedor de fuente real con APIs ficticias; orden Free/Put; mapas
alias/referencias; rollback por falta de memoria y fallos en cada escritura;
Unmap fallido retiene referencia y recurso; cookie duplicado/segunda liberacion;
dos propietarios compartiendo sesion; rechazo de buffers solapados y reentrada;
guardas ficticias IRQL/politica y un fallo sintetico de limpieza nativa.
Verificar esto NO valida IRQL real, concurrencia, MDL/PnP/rundown, traduccion,
cache, GPU/TLB/quiescencia, firmware, GART, propiedad VRAM/W2P ni UMA 4/6 GiB.
Unmap CPU NO prueba que la GPU haya terminado. No se activan rutas hardware.

Reproducir desde x64 Native Tools Command Prompt for VS 2022, sin Admin:
  python -B research\windows\dma-bridge-20261004\verify-offline.py
Revision Code Reviewer antes de cada compilacion segun AGENTS.md.
Salida unica output\dma-bridge-*. Auditor sucesor protege fuentes antiguas;
16 suites RAM /35 etapas y un OBJ WDK del candidato ANTERIOR sin cambios, NO
del puente. El runner rechaza CL/_CL_ y usa /UBC250_DMA_ADAPTER_MOCK en ese OBJ.
RESULT.json solo se escribe tras exito completo y hashes de fuentes estables.
No hay paquete instalable ni comandos de instalacion/reinicio para este paso.

Siguiente: disenar por separado la serializacion y referencias/rundown Windows,
con pruebas de eventos primero, sin habilitar ni integrar los modelos CPU en
un controlador cargable. Faltan decisiones WDDM/PnP y limites de pila kernel.

Cambios originales; no se copia codigo ni firmware externo. LICENSE y
research/windows/domain-backend-20261003/CREDITS.txt (Keshas/D-Ogi/MetalCyan)
permanecen intactos. primary-source.json identifica fuentes locales protegidas.
