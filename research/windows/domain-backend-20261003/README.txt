BC250 - adaptador de dominios al backend CPU (2026-10-03)
Base protegida: 030f67f174c0d391508db9b0c44f070ec57754df
Rama de investigacion: research/domain-backend-20261003

El nuevo header une dos modelos propios, sin modificar sus implementaciones:
address-domains-20261003 y vm-cpu-session-20261003. Solo pruebas en RAM.

PreviewVram comprueba el span completo y calcula FB_PHYSICAL usando AdConvert.
Es solo aritmetica. Su resultado NO autoriza registro DMA ni acceso Windows.
Register admite unicamente paginas AD_DMA_LOGICAL (etiqueta5), con Start4K,
Bytes4096 y Last canonico en el limite numerico de la sesion. Traduce solamente
el vocabulario hacia BC250_ADDRESS_DMA_LOGICAL (etiqueta3). No transforma las
direcciones ni infiere traducciones de una igualdad numerica.
AD_FB_PHYSICAL tambien vale3: por eso un cast directo seria incorrecto. Cada
elemento debe superar el filtro de dominio ANTES de registrar la lista.
OFFSET/MC/FB/CPUphysical/CPUvirtual/GPUvirtual son rechazados como backing.

Map requiere descriptor AD_GPU_VIRTUAL (etiqueta6), alineado4K, Last correcto,
rango dentro del layout de la sesion y Bytes=PageCount*4096 del backing valido.
Las guardas antiguas de permisos, referencias, alias y rollback se conservan.
Se heredan punteros validos, estables/disjuntos, acceso exclusivo y contrato de
callbacks: la API no detecta memoria arbitraria ni ofrece sincronizacion real.
Las negativas de preflight dejan sesion/salida/journal sin cambios. Un Map
admitido que falle conserva el rollback antiguo: LastLeaseId puede avanzar.
Un descriptor/tag honesto sigue SIN demostrar propiedad OS de esa memoria.

Ejecutar tras revision independiente ANTES de compilar, en x64 Native Tools
Command Prompt for VS2022 (sin administrador):
  python -B research\windows\domain-backend-20261003\verify-offline.py
La campana exige auditoria de fuentes, catalogo y 12 EXE de RAM con /W4 /WX:
adaptador nuevo, nueve regresiones CPU, modelo de dominios y control mock.
Son26etapas; cualquier retorno no cero aborta. Logs/comandos/codigos y hashes
quedan en output/domain-backend-<unico>; RESULT.json solo tras exito completo.
No WDK, /kernel, enlazado/firma/carga/instalacion de driver o IOCTL hardware.
Build11 y el driver instalado permanecen fuera de este trabajo.

PUBLICACION
El usuario autorizo publicar SOLO la rama de investigacion del fork
sebastianhzt/BC250-WDDM-Research, no main ni release. Antes de push: todos los
tests y hashes correctos, revision independiente de fuentes y resultados,
revisar TODOS los commits nuevos respecto a origin/main, datos privados y
atribucion. No se suben output/logs/binarios ni fuentes externas completas.
CREDITS.txt describe los tres proyectos y sus licencias distintas. El README
raiz conserva debajo todo texto upstream (solo agrega un newline final donde
upstream no lo tenia) y avisa que NO son nuestras pruebas.
Las afirmaciones antiguas no se convierten en resultados verificados del fork.

PENDIENTE W2P
Este adaptador no registra VRAM ni forma hojas PTE VRAM. Hacer eso necesita
contrato de reserva/lifetime Windows y evidencia separada de traduccion y
encoding. No habilitar rings, GART/VM, firmware ni TLB por pasar estos tests.
