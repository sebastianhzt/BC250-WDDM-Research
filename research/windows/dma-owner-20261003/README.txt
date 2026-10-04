BC250 - contrato simulado de recurso DMA (2026-10-03)
Base protegida: e4c9e605eb8c06c1bcf0368d6d2621c75bc373b0
Rama development. SOLO RAM. No paquete instalable ni driver/Windows DMA owner.

ALCANCE
Preparacion original del lifecycle para una integracion Windows posterior.
Un proveedor simulado entrega un cookie opaco; el modelo registra sus runs DMA
en el backend CPU anterior. NO llama IoGetDmaAdapter, Get/PutScatterGatherList,
GetScatterGatherListEx, CancelAdapterChannel, MDL, MMIO, firmware o GPU.
Ni un tag DMA ni Consumed prueban propiedad Windows. No reserva VRAM W2P.
Los valores de las fixtures son inventados, no mapas UMA de4/6GiB.

CONTRATO
Owner zero-init una vez, direccion estable y acceso exclusivo en TODAS las
llamadas/callbacks junto a su session compartida. Busy rechaza reentrada
sincrona; NO lock, atomics, IRQL, cancel spinlock ni sincronizacion multihilo.
Buffers validos/estables/disjuntos y capacidad honesta. Provider Put infalible,
no modificar/retener internals ni llamar directamente a session para el backing
de este owner. Nunca exponer el cookie/memoria a hardware. CPU Unmap no prueba
quiescencia GPU. Todos owners deben llegar DEAD ANTES de destruir session.
Provider/context/request deben vivir hasta resolver entregas pendientes; no
timeout, reset, reutilizacion de direccion ni borrado forzado como cleanup.

EVENTOS SERIALIZADOS (no sustitutos de las APIs Windows)
Init -> IDLE. Begin consume ID monotono y crea ticket owner+id -> PENDING.
Cancel(PENDING) -> CANCEL_WAIT, sin liberar nada ni confirmar cancelacion OS.
ResolveNoResource -> IDLE SOLO si provider simula que no existe recurso y
no puede llegar callback futuro (cancel confirmado o allocation fallida).
Deliver con ticket actual y cookie noNULL consume el recurso:
  PENDING + runs correctos -> registro CPU + READY (recurso retenido).
  CANCEL_WAIT -> Put exactamente una vez + IDLE, sin registrar ni leer runs.
  lista invalida/registro lleno -> Put exactamente una vez + IDLE.
Return por valor contiene Status y Consumed. Status puede ser error con
Consumed=1: el recurso fue aceptado y limpiado, no llamar Put otra vez.
Consumed=0 deja ownership PREVIO intacto: no consume una nueva entrega,
pero TAMPOCO devuelve al provider un cookie aceptado en una llamada anterior.
Duplicate/stale/BUSY/NULLcookie no cambian ownership. Provider honesto no
entrega el mismo recurso a otro ticket ni usa duplicados como nueva ownership.

Cancel(READY) -> DRAINING: rechaza Map nuevo, permite Unmap de backing propio.
Map falla -> rollback CPU, recurso retenido. Unmap falla -> lease retenido.
Release exige0referencias; unregister CPU ocurre ANTES de Put, con Busy activo
durante callback. Alias del mismo backing mantienen referencias independientes.
Release repetido/stale NO Put. Shutdown rechaza pendientes/recursos y entra
DEAD terminal; no destruye session ni admite reinicializacion.

FUENTES WINDOWS Y LIMITE DE EQUIVALENCIA
Ver primary-source.json. CancelAdapterChannel puede fallar cuando el callback
ya llego o esta por llegar; cancel solicitado no significa recurso liberado.
PutScatterGatherList es el cleanup explicito del flujo clasico. Microsoft
describe liberacion automatica al retornar el callback en GetScatterGatherListEx
y cleanup distinto para synchronous sin callback (FreeAdapterObject).
NO estamos portando esa ABI ni reteniendo sus recursos tras callback: el cookie
simulado presupone un provider con transferencia explicita. Seleccionar y
validar el transporte real Windows sigue pendiente, incluyendo lifetime MDL,
DMA_ADAPTER, direction, IRQL, request context, remove/rundown y quiescencia.

PRUEBAS (revision independiente ANTES de cada build, AGENTS.md)
En x64 Native Tools Command Prompt for VS2022, sin administrador:
  python -B research\windows\dma-owner-20261003\verify-offline.py
30 etapas: auditoria, catalogo,14 EXE RAM compilados /W4 /WX /O2 y ejecutados.
Incluye las13regresiones anteriores sin modificar sus fuentes. Fallo no cero
aborta, RESULT.json solo tras exito y hashes estables. Output unico en
output/dma-owner-<unico>; no publicar esos logs/binarios ni confundirlos con GPU.

Cobertura: cancel antes/despues de entrega, confirmacion sin recurso,
entrega tardia, stale/duplicate, ownership Consumed, direction ambas,
lista malformada y capacidad, session compartida con backing ajeno,
alias+rollback Map/Unmap, cleanup exactonce, reentrada Busy de todas las APIs,
buffers solapados, IDs agotados y Shutdown pendiente/activo/DEAD.

ATRIBUCION
Nuevas fuentes originales de Sebastian, SPDX Apache-2.0 donde aplica.
Creditos/pins/licencias de Keshas-dev, D-Ogi, amethyst8118/MetalCyan y AMD/Linux:
  ../domain-backend-20261003/CREDITS.txt
Referencias, no respaldo. No codigo externo propio ni firmware importado.
Build11/controlador instalado/main permanecen intactos.

PROXIMO LIMITE
Disenar el adapter Windows real SEPARADO: seleccionar flujo compatible con
mapping retenido, validacion de DMA_OPERATIONS y lifetime/cancel/rundown/IRQL.
Primero revision/compilacion sin carga. No instalar ni activar GART/SDMA por
aprobar las fixtures. W2P y mapas efectivos UMA siguen pendientes.
