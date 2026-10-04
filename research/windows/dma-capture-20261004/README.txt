BC250 - contrato de captura de solicitud DMA, 2026-10-04
SPDX-License-Identifier: Apache-2.0

Variante NUEVA, separada de Native/Gate/Resource/Leases y modelos CPU congelados.
NO consumidores reales, NO maps/VM/jobs SDMA/GPU. Solo valores numericos de
DMA_LOGICAL copiados a una salida confiable; NO CPU PA/MC/FB/GPU VA.
NO SYS/INF/CAT; NO paquete instalable. Build11, driver instalado, main, registro,
PnP y hardware intactos. W2P/UMA/IOMMU/traduccion/rundown real NO validados.
Politica de produccion FALSE antes de DDIs; no enlazar/cargar esta variante.
NO prueba de concurrencia real. Cinco OBJ /c/kernel son solo chequeo de tipos.

Problema que aborda
Native congelado no guarda longitud original. La instrumentacion previa solo
comparaba el total SG con MaximumLength y dependia del contrato de lista estable.
La nueva variante guarda Offset, Bytes, Direction y ancho en Acquire exitoso,
bajo Metadata, ANTES de publicar referencia inicial. No reconstruye solicitud
desde SG. Esa metadata privada permanece inmutable hasta retiro exitoso.

Contrato de captura
Snapshot exige referencia ORDINARIA viva, expected{Offset,Bytes,Direction}
coincidente, salida ENTERA cero y disjunta. Gate -> Metadata; revalida identidad,
estado Native HELD, MDL/pines/bounds/ancho y suma SG EXACTA == Request.Bytes.
Admite solo 4KiB completos, hasta 64 paginas. Orden y duplicados conservados.
Primera pasada valida todo; segunda copia a salida sin workspace grande de
stack ni DDI de asignacion. Elements de WDK es variable, no array fijo64.
El proveedor adquirido es confiable/inmutable y su memoria permanece anclada;
no hay verificacion independiente del origen fisico de cada numero DMA.

La salida y su referencia contada se publican bajo Metadata ANTES de GateLeave.
Incluye Self, owner/slot/ID de referencia, solicitud original, limite/ancho,
cuenta de paginas/elementos y valores tipados copiados; NO punteros SG/MDL.
record.Snapshot fija la direccion CANONICA del buffer. Retain/Drop ordinarios
rechazan referencias snapshot, incluso copiadas. Snapshot desde otra Snapshot
tambien se rechaza. No mapeo ni consumidor acoplado en este paso.
Si Finish falla DESPUES de publicacion, la salida LIVE y referencia permanecen:
error de retorno NO significa salida cero ni permiso de reset. Release de una
obligacion valida sigue posible en FAULT con metadata coherente, SIN recovery.
Si falla ANTES de publicar, salida cero y contador sin cambios; corrupcion de
procedencia pone FAULT y retiene transporte, no adivina limpieza.

Retiro
SnapshotRelease es metadata-only, permitido tras STOP/GateCLOSED y en FAULT
coherente. Verifica Self + Owner/Slot/Id + puntero canonico de record.Snapshot.
Copia/movimiento no puede liberar original; Self NO frontera de seguridad.
BUSY conserva salida/ref para retry. Exito terminaliza ese buffer, no reutilizar.
Drop nunca libera Native. Retire BUSY conserva transporte mientras exista
cualquier referencia. Solo Retire conocido con cero refs cancela/cierra una vez.
SOLO Stop en hooks. Nunca callback de usuario ni exposicion de contexto Native.

Contrato de lifetime externo
Objeto/PDO/MDL bloqueado/lista proveedor y TODOS buffers residentes alineados,
VALIDOS, CONFIABLES, estables y disjuntos desde ANTES de entrar hasta TODOS los
retornos MAS todas referencias. Lease no implementa publicacion/delecion de
objetos ni ancla automaticamente memoria OS o buffer Snapshot.
Mismo buffer: acceso exclusivo durante TODA llamada. No raw fields ni otro
cliente del Native/Gate privado. PASSIVE, no locks invertidos/nested gates.
Check de rangos NO prueba validez de punteros arbitrarios; IDs/Self confiables.
Reset/restauracion de mundos RAM negativos NO es recuperacion real.

Pruebas
23 suites RAM / 53 etapas / 23 EXE y cinco OBJ WDK /c/kernel sin enlazar.
Todos /W4 /WX; RAM /O2 y OBJ /Od; WDK10.0.26100.0 y VS2022 x64 Native Tools.
Code Reviewer ANTES de CADA build. verify-offline.py rechaza CL/_CL_ ocultos,
pasa negativos, fija hashes de entradas antes/despues y salidas completas.
Regresion renombrada de nuestro test-leases MAS snapshot: longitud/offset/
direction original contra proveedor FALSO, request esperado incorrecto,
segmentos discontinuos/duplicados,64paginas,15snapshots+seed,ID agotado,
copy/canonical/aliases,Stop/publicacion,Finish FAULT y Release BUSY/retry.
Logs/binarios solo locales output/dma-capture-*. Fuentes SOLO development.
Auxiliares admiten LF/CRLF; C/H LF segun .gitattributes, hashes de bytes reales.
Auditor de inventario/orden/mutaciones complementario, NO prueba formal.

Procedencia
Derivacion de nuestras fuentes Apache-2.0; CREDITS.txt/licencias intactos.
No codigo externo nuevo importado. primary-source.json identifica baseline.
Microsoft documenta Offset/Length/WriteToDevice y SG Ex; NO prueba este modelo:
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nc-wdm-pget_scatter_gather_list_ex

Siguiente limite
Todavia no se conecta Snapshot a mapas CPU ni al driver; esa asociacion necesita
adaptacion y revision propias. Este paso NO autoriza DMA real ni memoria W2P.
