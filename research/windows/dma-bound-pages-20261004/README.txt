BC250 - procedencia de paginas desde SG adquirida FALSA, 2026-10-04
SPDX-License-Identifier: Apache-2.0

Instrumentacion FIXTURE-ONLY de RAM, NO API de direcciones del driver.
Build11, driver instalado, main, registro, PnP y hardware intactos.
NO SYS/INF/CAT, NO paquete instalable. NO DMA real, NO W2P resuelto.
Las politicas de produccion permanecen FALSE; tres MOCK obligatorios y
#error _KERNEL_MODE. NO compilar esta instrumentacion como kernel.

Progreso
El paso anterior unia referencias con paginas numericas independientes.
Ahora Capture retiene un lease PUBLICO antes de inspeccionar la lista SG
del proveedor FALSO bajo Gate -> Metadata. Verifica identidad, Native HELD,
RequestIssued, punteros privados coherentes y bounds de la lista.
Copia TODOS los valores, no devuelve punteros SG/MDL. Conserva orden,
segmentos discontinuos y ocurrencias duplicadas como los validadores anteriores.
Expande solo paginas completas de 4KiB, <=64, dentro del ancho declarado.
Los numeros conservan DMA_LOGICAL: NO conversion a CPU PA/FB/MC/GPU VA.

Register vuelve a capturar y compara snapshot completo ANTES de mutar Session.
No acepta array de paginas arbitrario ni permite retarget del Capture.
El backing CPU resultante contiene exactamente la lista capturada, no inputs
independientes. Attach usa la fachada congelada; Map compara backing y snapshot.
Cada alias VA mantiene lease propio ademas del hold de Capture y raiz de fachada.
Copy-after-lock independencia NO demuestra que un proveedor real sea honesto.
Native congelado NO conserva longitud original de la solicitud. El snapshot
suma SG y limita por MaximumLength; no revalida esa longitud original de forma
independiente. Depende de Acquire correcto y lista inmutable del proveedor.

Retiro
Release temprano se niega ante mapas, PENDING_DROP, backing/raiz pendientes
o estado de fachada desconocido. MlRetire retira backing/raiz pero devuelve
BUSY porque Capture conserva su hold. Tras retiro conocido, BackingReleased
y RootDropped deben confirmar ambas fases. Solo entonces Capture Release puede
soltarlo; un Drop BUSY conserva el hold y permite retry, sin segundo unregister.
Drop final NO libera Native: se reintenta MlRetire para cleanup conocido.
Capture sin asociar puede soltar su hold; Register fallido conocido deja
Capture para retry/Release, sin backing. Register correcto pero sin Attach
retira backing antes de Drop, con BackingReleased para retries.
FAULT/rollback incierto conserva obligaciones, SIN recovery ni reset.

Contrato de lifetime externo
Zero una vez, Self estable, todos objetos/buffers VALIDOS, CONFIABLES y
disjuntos (incluye estructura Source ENTERA frente a MDL/lista del proveedor,
Resource, Session/nodos, fachada, out y journal). Permanecen validos desde
ANTES de entrar hasta TODOS retornos y todos holds/mapas/deudas.
Acceso EXCLUSIVO durante TODA llamada, tambien al Resource y Session.
Solo MlStop permitido desde hooks. No otro cliente ni raw Session/Leases/campos;
el caller puede soltar UNA VEZ su seed despues de Capture correcto.
Checks de rangos NO validan punteros arbitrarios ni son frontera de seguridad.
Self/IDs/generaciones son metadata confiable, NO capabilities no falsificables.
Acceso privado en BpRead es excepcional instrumentacion de TEST, no extension
de los contratos de produccion congelados. No borrowed pointer tras Unlock.
No scheduler/PnP/concurrencia/IRQL real/rundown del proveedor verificados.
Destruir/restaurar mundos RAM negativos NO es recuperacion real.

Pruebas
22 suites RAM, 50 etapas, 22 EXE + 4 OBJ WDK congelados /c/kernel, sin enlace.
Todos /W4 /WX; RAM /O2, OBJ /Od. Code Reviewer ANTES de cada compilacion.
Snapshot desde SG multi-run/discontinuo/duplicado y 64 paginas, valores de PTE
con oracle independiente, fallos Map/Unmap, release prematuro, retries Drop,
limites Session mas estrechos, stale/copy/alias/politica/Stop y corrupcion SG.
Regresiones previas separadas; hashes entradas/salidas y controles negativos.
verify-offline.py se ejecuta desde x64 Native Tools VS2022 y exige WDK26100.
Logs locales output/dma-bound-pages-*; fuentes publicadas SOLO development.
La auditoria admite LF/CRLF en auxiliares Python/JSON/TXT para clones Windows;
exige LF en C/H segun .gitattributes congelado y siempre hashea bytes reales.

Fuentes y limites
Codigo original/reuso de nuestros fixtures Apache-2.0; CREDITS.txt intacto.
No codigo ni ABI externo importado. primary-source.json fija baseline y fuentes.
Microsoft documenta SG Ex y el uso de recursos DMA, NO prueba este modelo:
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nc-wdm-pget_scatter_gather_list_ex
https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/performing-dma-in-64-bit-windows

Siguiente limite
Este PASS demuestra dataflow del proveedor SIMULADO, no memoria reservada por
Windows ni igualdad DMA-logical/CPU-PA/MC, IOMMU/GPU-TLB/fences/UMA 4/6GiB.
Una API de exportacion real requiere nuevo contrato, lifetime y pruebas aparte;
no convertir BpRead ni los modelos CPU en driver por cambiar un #define.
