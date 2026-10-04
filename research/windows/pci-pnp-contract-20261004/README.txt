PCI/PnP -- CONTRATO AISLADO SOLO RAM -- 2026-10-04
================================================
No es Build23 ni paquete instalable. No SYS/INF/CAT, firma, carga, registro,
BCD, IOCTLs, PCI fisico, MMIO, mapeo, DMA, firmware o acceso al hardware.
Build22 instalada y sus fuentes, Build21 y Build11 permanecen intactas.
Nunca ejecutar el build.bat de la raiz, que usa otro controlador legado.

OBJETIVO
Modelar antes de implementar un adaptador real:
lease STARTED -> interfaz ya referenciada -> leer 64 bytes ficticios ->
revalidar epoch/generacion -> dereference -> revalidar -> soltar lease.
Rechaza lecturas cortas/largas, identidad distinta de 1002:13FE Display,
cabeceras distintas de tipo0, metadatos obsoletos y referencias incompletas.
No hay llamada adicional InterfaceReference: QUERY_INTERFACE ya entrega una.
SetBusData/GetDmaAdapter/TranslateBusAddress nunca se invocan.
La API es one-shot por objeto; no reset ni retry. Reentrada sincrona se rechaza.
Si un proveedor viola el contrato de adquisicion (PENDING, datos sucios en
fallo o falta Dereference en exito), QUARANTINED retiene anclas inciertas:
NO adivina limpieza ni permite retry. Esto no implementa recuperacion real.
Los fallos normales limpian interfaz antes de soltar el lease.

CORRELACION
Parser original de header sintetico tipo0: BAR64 consume el siguiente slot,
BAR IO se ignora; MEM32/64 se compara con RawBase y prefetch PnP, NO con
TranslatedBase. Ordinal descriptor no se convierte por posicion en BAR.
Rechaza rangos raw/translated superpuestos, overflow, ambiguedad, entradas
sin relacion, estados/generaciones/epochs distintos y tipos BAR reservados.
TranslatedBase se conserva sin conversion de dominio. Length es el valor
suministrado por PnP, NO un size BAR medido. No writes de sizing/probing.
VramOwnership y DmaAuthorized siempre cero, incluso con Valid=1.
Datos basados en los numeros observados Build22 son fixtures SINTETICOS,
no un volcado PCI real, ni se presume que la disposicion BAR sea demostrada.

LIMITES / TRABAJO PENDIENTE
Fuera BC250_PCI_CONTRACT_MOCK, Init/Observe devuelven NOT_SUPPORTED antes de
cualquier callback. BC250_PCI_TYPES_MOCK solo sustituye tipos para probar
la puerta cerrada. WDK /c fuerza ambos macros Undef; no kernel link/load.
El parser es puro, puede calcular relaciones, no lee dispositivos.
mock-wdm.h es ABI reducido; WDK /c independiente verifica tipos nativos.
Acquire/StillStarted/Release/Query son FAKES; no implementan IRPs, esperas,
remove-lock, referencias PDO ni query-interface del SO. Retencion real no
garantiza que hardware siga disponible. El contrato exige estado STARTED,
epoch y generation no reutilizables durante lease; al agotarse deben rechazarse.
Los eventos fake detectan cambios mediante StillStarted FALSE, no prueban
que el sampler real capture state/epoch de forma coherente.
Checks antes/despues detectan transiciones conocidas, NO evitan una lectura
en curso durante STOP/surprise/power ni cancelan trabajo. Incluso la ultima
validacion seguida de publish puede quedar obsoleta: resultado historico,
no instantanea global, permiso de mapping ni prueba de quiescencia.
Todos los objetos/contextos/punteros confiables, residentes y no alias deben
permanecer anclados externamente; APIs serializadas por el llamador a PASSIVE.
Busy no es lock interhilos. No integrar al driver sin nuevo diseno PnP/rundown,
control STOP/remove/power, sampler epoch/generation real y revision independiente.
Falta adaptador Microsoft BUS_INTERFACE_STANDARD: target stack correcto,
GUID/version/size, IRP con error inicial, lifetime/completion/sin locks en waits.
No se habilita automaticamente porque una prueba simulada pase.

PRUEBAS
VS2022 x64 Native Tools / Python3, sin Admin:
  verify-offline.bat C:\ruta\python.exe
Code Reviewer obligatorio ANTES de cada compilacion por AGENTS.md.
Misma fuente incluida directamente en test-pci.c y test-blocked.c, O2/Od:
matriz 9 momentos x 9 interfaces x 66 longitudes, reentrada y limpieza,
STOP/START fake, query fallo/PENDING, invalidacion en dereference,
mutacion bit a bit header64, raw != translated, overflow/duplicados,
ultimo BAR64 invalido. Provider defectos no se confunden con pruebas Windows.
12 etapas: cuatro EXE RAM y dos OBJ WDK, logs/import-symbols/hashes unicos.
RESULT.json solo tras exito completo, fuentes inmutables y Build21/22 raw39
protegidos. Runner rechaza flags CL/_CL_/LINK y deltas tracked fuera del paso.
No ejecutar tools del driver instalado como parte de esta campana.

ATRIBUCION
Codigo original Sebastian, Apache-2.0 heredada; conserva LICENSE/CREDITS raiz.
Keshas-dev proyecto base; D-Ogi y MetalCyan referencias de investigacion,
sin copiar codigo/firmware de ellos en este modulo.
Fuentes primarias Microsoft en primary-source.json; no se copia su ejemplo.
