DMA Windows - candidato aislado, 2026-10-04

ALCANCE: fuente original, pruebas SOLO RAM y compilacion WDK /c /kernel.
NO SYS/INF/CAT, enlace, firma, carga, instalacion ni acceso al hardware.
Build11, controlador instalado, main y modelos anteriores quedan intactos.
No hay comandos de instalacion: este paso NO genera un paquete instalable.

Se prueba bc250_dma_adapter.c directamente mediante inclusion en test-adapter.c.
mock-wdm.h usa tipos y tabla reducidos: NO reproduce el ABI ni el comportamiento
de Windows. La compilacion independiente contra WDK comprueba los tipos reales;
no demuestra adquisicion efectiva, direccion DMA, IRQL real ni concurrencia.
Fuera de BC250_DMA_ADAPTER_MOCK, la politica siempre devuelve FALSE antes de
cualquier llamada de plataforma. No hay interruptor de registro/entorno/IOCTL.
No se debe activar el macro de plataforma ficticia en una compilacion kernel.

Transporte seleccionado (todavia no conectado al propietario CPU):
1. IoGetDmaAdapter: descripcion version 3, PDO y capacidades suministradas por
   futuro llamador PnP confiable. NO se inventa el ancho DMA de BC-250.
   DMA_ADAPTER.Version==1 es normal; se valida Size de DMA_OPERATIONS,
   funciones requeridas y limite conservador de registros para MaximumLength.
2. InitializeDmaTransferContext: contexto unico por objeto. Como restriccion
   conservadora solo se permite UNA solicitud Get por objeto, incluso si falla.
   No se afirma que un contexto pueda reutilizarse tras liberar recursos.
   Cerrar el adaptador y crear un objeto nuevo es necesario para otro intento.
3. GetScatterGatherListEx, DMA_SYNCHRONOUS_CALLBACK, todos los callbacks NULL.
   No hay cola de adquisicion asincrona ni callback que regrese mas tarde bajo
   el contrato documentado. Falta de recursos inmediata no implica espera.
4. FreeAdapterObject(DeallocateObject): recurso retenido, una sola vez.
   PutDmaAdapter: identificador de adaptador, despues de liberar el recurso.
   NO se mezcla con PutScatterGatherList del transporte clasico.

Cancelacion limitada: llamada SERIALIZADA antes de adquirir o despues del
retorno sincrono; bloquea futuras adquisiciones y libera un recurso nunca
expuesto a GPU. NO cancela una llamada en curso, un job, una IRP pendiente ni
implementa CancelAdapterChannel. Cierre con recurso retenido se rechaza.
La limpieza inmediata solo es defendible aqui porque no se exportan direcciones
ni se envia trabajo. NO es un criterio de quiescencia GPU para una integracion.

Contrato pendiente para un adaptador ejecutable:
- NO lock: Busy protege reentrada sincrona, NO dos hilos. El llamador debe
  serializar exclusivamente todas las operaciones, sin elevar IRQL.
- Todas las entradas requieren PASSIVE_LEVEL. Propietario no paginado estable;
  PDO y tabla de operaciones del SO vivos, descripcion inicializada a cero y
  capacidades establecidas con recursos PnP/Windows, no una captura Linux.
- MDL unico confiable, ByteOffset cero, paginas bloqueadas y permisos correctos
  para direccion DMA, buffer estable hasta Free. Aqui no se bloquea/desbloquea,
  referencia/dereferencia ni se asigna un MDL. Las banderas NO prueban propiedad.
- Una violacion del proveedor (sin Put, estado positivo inesperado o fallo con
  lista) deja QUARANTINED: no se adivina liberacion ni ausencia de callback.
  No existe recuperacion de esa violacion; casos ficticios NO validan Windows.
- Lista confiable del SO: 1..64 spans DMA-logical, completos, alineados 4 KiB,
  suma exacta y dentro del ancho declarado (1..48 bits). Se preservan duplicados.
  No se convierten a CPU PA/MC/FB/GPU-VA y no se exportan ni copian al modelo CPU.
- Faltan referencias/rundown PnP, sincronizacion real, MDL real, puente al modelo
  de propietario y estrategia de traduccion/cache/GPU. No integrar simplemente
  los modelos CPU en kernel: sus pilas/lifetimes aun no estan validados alli.
- W2P, propiedad VRAM, GART, firmware, UMA efectiva 4/6 GiB y DMA real siguen
  sin demostracion. No se reutilizan reservas Linux de 6 GiB en Windows de 4.

Reproducir sin Admin desde x64 Native Tools Command Prompt for VS 2022:
  python -B research\windows\dma-windows-20261004\verify-offline.py
Requiere WDK 10.0.26100.0. Deja salida unica en output\dma-windows-*.
El runner rechaza flags ocultos CL/_CL_ y fuerza /UBC250_DMA_ADAPTER_MOCK
en la compilacion WDK, sin plataforma ficticia ni enlace kernel.
Ejecuta auditoria sucesora de ocho archivos, generacion de catalogo, 15 suites
RAM (incluye 14 regresiones), y un OBJ de este candidato. Cualquier error aborta;
RESULT.json solo aparece si todo pasa y los hashes de fuentes no cambian.
La revision Code Reviewer debe preceder cualquier compilacion segun AGENTS.md.

Siguiente: puente simulado entre este transporte y el propietario CPU, incluyendo
retencion hasta liberar referencias. Solo despues disenar PnP/rundown reales;
no activar, firmar o instalar este candidato como paso automatico.

Atribucion: cambios originales, no codigo ni firmware externo copiado.
Se conserva LICENSE y research/windows/domain-backend-20261003/CREDITS.txt
(Keshas, D-Ogi, MetalCyan). Referencias Microsoft en primary-source.json.
