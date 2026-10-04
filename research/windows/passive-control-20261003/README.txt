CONTROL PASIVO INDEPENDIENTE / SOLO PRUEBAS OFFLINE - 2026-10-03

Base protegida: db19baca9b2cc32f5402170d69a50a156674113d.
Esta etapa agrega fuentes originales sin cambiar el KMD de pantalla, Build11,
los modelos CPU ni las 77 barreras del port anterior. No cargar ni instalar.

bc250_control.c es una entrada WDK separada, no una nueva rama dentro del
DriverEntry heredado. Nombres propios BC250ResearchControlV1 y GUID propio.
No registra DDIs, no detecta la GPU y no abre PCI, MMIO, firmware, VRAM o DMA.
Los handlers y unload se preparan antes de crear el dispositivo. Colisiones
fallan sin buscar/borrar objetos ajenos; un fallo al crear el enlace borra solo
el dispositivo obtenido por esta entrada. Se publica listo al final.

Unico IOCTL: BC250_CONTROL_QUERY_METADATA, METHOD_BUFFERED/FILE_READ_ACCESS,
sin entrada, salida minima de 32 bytes. Version/politica y permisos GPU=0;
no direcciones, punteros, capacidades hardware inventadas ni estado de la GPU.
Rechaza subrutas/directorios y todos los otros comandos. Respuesta inicializada
a cero; no tareas pendientes, timers, callbacks asynchronos o estado por handle.

Seguridad: IoCreateDeviceSecure con FILE_DEVICE_SECURE_OPEN y SDDL predeterminado
SYSTEM+Administrators. Windows permite overrides administrativos de clase:
los mocks NO validan ACL efectiva, I/O Manager, IRQL real, carreras o descarga
en el kernel. Un fallo OS al eliminar el enlace puede dejarlo residual;
unload no puede propagar dicho error. No reclamar recuperacion kernel perfecta.

Tras revision independiente, en x64 Native Tools Command Prompt VS2022:
  python -B research\windows\passive-control-20261003\verify-offline.py
No necesita administrador. Requiere encabezados WDK 10.0.26100.0 para /c.
Los tests ejecutan la fuente de control real con APIs falsas en RAM y nueve
regresiones de modelos CPU. La unidad kernel se compila con /c: NO enlace SYS,
firma, certificado, paquete, servicio, registro, reinicio o instalacion.
RESULT.json solo aparece tras exito completo y comprobacion de hashes.

NO ejecutar los antiguos build.bat/instaladores. Sus auditores mantienen su
scope anterior y rechazaran estos nuevos archivos; audit-control.py es el
sucesor separado con baseline protegido. Logs/binarios locales en output,
nunca exportados con las fuentes. MetalCyan se analizo solo como referencia:
ver METALCYAN-REFERENCE.txt. No se copiaron codigo ni blobs.

Siguiente puerta antes de un candidato cargable: revisar enlace/entrada WDK,
seguridad efectiva y lifetime/concurrencia con evidencia kernel y un plan de
recuperacion especifico. W2P sigue requiriendo propiedad/reserva Windows real;
un control de metadatos no la crea. No es aceleracion Vulkan, Metal ni WDDM.

Documentacion oficial:
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdmsec/nf-wdmsec-wdmlibiocreatedevicesecure
https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/controlling-device-namespace-access
