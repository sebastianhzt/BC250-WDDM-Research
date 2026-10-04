BC250 - Dominios de direcciones, etapa CPU aislada (2026-10-03)
Base protegida: 62b7edeab0f7e3aeec92f7743189062199101dc7
Rama local: research/address-domains-20261003

OBJETIVO
Contrastar una idea util de MetalCyan con las fuentes Linux recopiladas y
convertirla en un modelo original comprobable en RAM. No se copia codigo,
firmware ni licencias de terceros a esta implementacion.

La formula Linux MC->fisico GPU usa: direccion - baseMC + baseFisicaGPU.
El inverso es algebra del modelo, no una funcion Linux pa2mc identificada.
El manifiesto primary-source.json fija hashes y lineas de fuentes exactas.
El enlace publico Linux v6.18 es comparacion, NO ese archivo experimental.

DOMINIOS EXPLICITOS
- VRAM_OFFSET: desplazamiento dentro del intervalo descrito por el llamador.
- VRAM_MC: direccion MC dentro del mismo intervalo.
- FB_PHYSICAL: fisico desde la vista GPU en Linux; NO CPU fisico Windows.
- CPU_PHYSICAL, DMA_LOGICAL, GPU_VIRTUAL y CPU_VIRTUAL: NO convertibles.

Ninguna direccion obtiene su dominio por ser numericamente pequena/grande.
La igualdad numerica NO permite retaggear CPU fisico como DMA o VRAM.
Los modelos antiguos conservan sus propias etiquetas; no existe un cast
automatico entre ambos modelos ni un consumidor de este header en el KMD.

CONTRATO
Layout: bases/tamano alineados a 4096, tamano no cero, ambos intervalos
completos dentro de 48 bits inclusive. Cada conversion revalida el layout.
Span: tamano no cero, rango completo dentro de VRAM, alineacion explicita
potencia de dos 1..4096. No se exige que el tamano sea multiplo de esa
alineacion: son spans aritmeticos, NO entradas hardware de tablas.
Las salidas quedan intactas si falla una precondicion. La suma usa 64 bits
con comprobaciones previas: no se hace OR entre base y desplazamiento ni
se mascara un overflow para producir una direccion aparentemente valida.
FB_PHYSICAL incluye el offset agregado XGMI si el llamador lo declara; el
modelo no descubre nodos, recursos, BARs ni configuracion real.

PRECAUCION PTE/PDE
En gmc_v10_0, get_vm_pde convierte SOLO un PDE no-leaf sin SYSTEM y tiene
reglas adicionales de translate_further. get_vm_pte trabaja sobre FLAGS,
no recibe un puntero a direccion. No hay regla universal de convertir
todo PTE VRAM; APU tampoco significa que SYSTEM deba activarse siempre.
Esta etapa no construye PTE/PDE, no aplica caches ni invalida TLBs.
No se adopta la heuristica de rangos/passthrough ni los timeouts que siguen
adelante de MetalCyan. Su utilidad aqui fue orientar una comprobacion.

PRUEBAS Y EJECUCION
Primero, revision independiente de fuentes ANTES de compilar.
Desde x64 Native Tools Command Prompt for VS 2022, sin administrador:
  python -B research\windows\address-domains-20261003\verify-offline.py
Solo se ejecutan EXE de pruebas en RAM: modelo nuevo, nueve regresiones
CPU previas y mock del control pasivo. MSVC usa /W4 /WX /O2. No WDK, /kernel,
link de driver, firma, instalacion, IOCTL real ni acceso al hardware.
Se preservan comandos, stdout/stderr crudos, codigos e historial, hashes
de entradas/artefactos en output/address-domains-<unico>. RESULT.json solo
aparece tras todas las etapas correctas y fuentes sin cambios. Fallo aborta.
El auditor exige exactamente los seis archivos nuevos respecto a la base,
sin modificar codigo anterior/controlador/build11, y comprueba 16 controles
negativos de fuente. Esto NO es un certificado de seguridad del driver.

QUE SIGUE BLOQUEADO
W2P: una descripcion Linux NO reserva memoria para Windows. Faltan contrato
de asignacion/propiedad/lifetime Windows, traduccion DMA real y recuperacion.
No hay GPU execution, aceleracion Vulkan/Metal ni ring/GART funcional nuevos.
No hay paquete instalable: no ejecutar pnputil, devcon ni build.bat aqui.
El driver instalado y la copia estable build11 no se tocan. Sin push a GitHub.
