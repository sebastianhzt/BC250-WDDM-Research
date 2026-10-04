BC250 - segmentos DMA a paginas tipadas (2026-10-03)
Base protegida: 4546263f73c38006a5af338d71de3fcd8510a44f
Rama: development. SOLO RAM, sin paquete instalable ni acceso hardware.

Proximo puente hacia W2P: normalizar segmentos DMA numericos en paginas4K
manteniendo orden, fragmentacion y duplicados. No usa SCATTER_GATHER_LIST,
MDL, DMA_ADAPTER ni APIs Windows: sus descriptores son declaraciones del caller.
No demuestra reserva de VRAM, propiedad OS o traduccion CPU/DMA/GPU.

Expand acepta 1..64 segmentos DMA_LOGICAL y hasta64 paginas totales. Cada
segmento debe tener Start/Bytes4K, Last canonico, span entero dentro del limite
48bits de la sesion y longitud total EXACTA a expected_bytes. No redondea,
trunca, ordena, deduplica ni convierte FB/MC/CPUphysical a DMA. Si un segmento
final es invalido se rechaza toda la lista sin cambiar salida ni contador.
Salida e inputs son validos/estables/disjuntos; capacidad declarada honesta.
Solo se escriben las paginas resultantes, no toda la capacidad de salida.
Los checks de alias cubren la lista de entrada, las paginas efectivamente
escritas y out_count; capacidad de salida no usada no participa del span.

RegisterRuns normaliza primero en stack y delega a Register del adaptador
anterior. Conserva sus guardas de sesion, buffers y referencias. Map/Unmap/
Release mantienen el lifetime CPU anterior. El caller de una futura integracion
Windows tendra que retener el recurso DMA OS hasta retirada real y quiescencia
GPU: este registro numerico NO implementa ni sustituye ese protocolo.
No consumidor kernel/IOCTL, registros, firmware, GART/TLB o rings nuevos.

Despues de revision independiente ANTES de compilar, en x64 Native Tools
Command Prompt for VS2022, sin administrador:
  python -B research\windows\dma-runs-20261003\verify-offline.py
28 etapas: auditoria, catalogo,13 compilaciones /W4 /WX /O2 y13 EXE en RAM.
RESULT.json solo aparece tras todas las etapas0 y hashes de fuentes estables.
Logs/hashes en output/dma-runs-<unico>, no publicarlos ni confundirlos con GPU.
Auditoria acota exactamente5 archivos nuevos respecto a la base protegida.

PRUEBAS
Todas las particiones de dos segmentos de1..64 paginas, limites completos,
dominio equivocado al final, overflow, capacidades, aliases, falta de bytes,
sentinelas/salidas intactas en fallo y registro/map/unmap con rollback de RAM.
Regresiones: adaptador de dominios, modelos CPU previos y control mock.

ATRIBUCION
Implementacion nueva original de Sebastian; SPDX Apache-2.0. Mantener avisos
heredados. Creditos, pins y limites de licencias externos en:
  ../domain-backend-20261003/CREDITS.txt
Keshas-dev: base; D-Ogi y amethyst8118/MetalCyan: referencias de investigacion,
no codigo propio ni firmware importado en esta etapa, no respaldo del proyecto.

PROXIMO PASO
Disenar y revisar un owner Windows de mapping DMA con captura de lifetime,
cancelacion y teardown; primero mock/control pasivo separado. No adquirir DMA
ni instalar sobre el driver de pantalla por aprobar este modelo. Build11 sigue
separada. W2P VRAM y traduccion real permanecen pendientes.
