# pcf2001-patcher

Parche no oficial y de código abierto para que **PC FÚTBOL 2001** (Dinamic Multimedia, 2000–2001) funcione correctamente en las versiones actuales de Windows.

> **Estado: en desarrollo. Todavía no hay ninguna versión publicada.** Nada de lo que hay en este repositorio está listo para jugar. Esta página explica qué hará el parche y cómo se construye, para que cualquiera pueda seguir y revisar el trabajo.

*English version: [README.md](README.md)*

## Qué arregla

| Problema en Windows actual | Estado |
|---|---|
| El instalador del CD no funciona (*"Acceso denegado"* y otros errores), así que ni siquiera se puede instalar el juego | Sustituido por nuestro propio instalador, `pcf2001-setup.exe`, que además aplica las actualizaciones de Dinamic (en pruebas) |
| El juego se detiene al arrancar con *"No se pudo iniciar DirectX. El programa no puede comenzar."* | Resuelto con el componente gráfico nuevo (en pruebas) |
| La pantalla completa cambia todo el escritorio a 800×600 | Resuelto (en pruebas): el juego se dibuja a su resolución y se escala a tu pantalla manteniendo la proporción |
| En modo ventana, algunos elementos se dibujan en el sitio equivocado | Debería resolverlo el componente gráfico nuevo; aún sin probar |
| Algunos nombres largos de equipos salen como caracteres sin sentido y algunos jugadores aparecen con el nombre de otro (hasta ahora solo se evitaba con el modo de compatibilidad "Windows XP SP2") | Causa encontrada: el juego lee memoria que ya ha liberado. Lo evita una capa de compatibilidad de memoria (en pruebas); los arreglos en el código del juego están en curso |
| El juego puede colgarse al salir | Resuelto con el componente gráfico nuevo (en pruebas) |
| Cuelgues aleatorios que terminan con *"La aplicación no puede continuar"* | Causa encontrada: el juego escribe en memoria que ya ha liberado. La capa de compatibilidad de memoria los reduce mucho (en pruebas); los arreglos en el código del juego están en curso |

También arregla fallos que el juego ya tenía en 2001:

| Problema del juego original | Estado |
|---|---|
| Con las *Vallas* (en *Estadio*) en *Auto* y sin *Encargado del campo*, el juego se cuelga con *"La aplicación no puede continuar"* | Resuelto: las vallas se siguen vendiendo solas, como si las llevara el asistente, como hace el juego con los demás empleados que faltan |
| En las divisiones más bajas, algunas ofertas de TV son absurdas, como 29.873.841 millones de pesetas "por todos los derechos" | Resuelto: las ofertas nuevas para esas divisiones ya no traen cantidad por todos los derechos, igual que las correctas, y las ofertas sin ningún dinero ya no aparecen. Las ofertas que ya estén en una partida guardada se quedan como están |
| En *Estadio → Vallas*, *Rellena* y *Auto* parecen dejar sin vender la última valla de la última zona, hasta pulsar *Rellena* otra vez | Resuelto: la valla sí se vendía, pero la pantalla no se volvía a dibujar; ahora sí |
| En *Estadio → Remodelar → Servicios*, encargar *Cafeterías* la misma semana que *Tienda del club*, *Enfermería* o *Lavabos* le da a la otra obra la duración de las cafeterías, y las dos terminan a la vez | Resuelto: como con los demás servicios, no se pueden encargar cafeterías mientras haya otra obra de servicios pendiente; el juego hace una cada vez |

Los detalles técnicos de cada problema están en [docs/GAME-INTERNALS.md](docs/GAME-INTERNALS.md) (en inglés).

## Qué vas a necesitar

- **Tu propia copia de PC FÚTBOL 2001:** el CD del juego, o una imagen del CD. El juego mantiene su comprobación del CD, y Windows 10/11 monta un archivo `.iso` con un doble clic.
- **Las actualizaciones oficiales de Dinamic:** la de la **versión 3.6** (`PCFPATCH3.6.EXE`) y las 12 de datos (`PCFACT01.exe` a `PCFACT12.exe`). Nuestro instalador las aplica todas; el motivo de admitir solo la 3.6 está en [docs/DESIGN.md](docs/DESIGN.md#d1-target-dinamics-v36-only).
- **Windows 10 u 11.**

Después, dos pasos:

1. **Instala el juego** con `pcf2001-setup.exe` ([docs/INSTALLER.md](docs/INSTALLER.md), en inglés). Lo instala desde tu CD y aplica las actualizaciones de Dinamic. Pide permisos de administrador una vez.
2. **Aplica el parche** con `pcf2001-patch.exe` ([docs/PATCHER.md](docs/PATCHER.md), en inglés). Se puede quitar de la misma forma.

Para jugar no hacen falta permisos de administrador, y nada necesita dgVoodoo, modos de compatibilidad ni ningún programa adicional.

## Qué incluye el parche y qué no

- **Solo incluye trabajo propio:** un componente gráfico nuevo escrito desde cero, una lista de cambios pequeños y documentados a los archivos del juego, y el programa que los aplica.
- **No incluye ningún archivo de Dinamic:** ni ejecutables, ni datos, ni gráficos, ni sonidos, ni vídeos. Solo funciona sobre una copia que ya tengas.
- **Solo modifica los archivos indicados en las notas de cada versión,** después de comprobar que cada uno es exactamente el archivo de la 3.6 que espera. Guarda copias de seguridad y se puede desinstalar.
- **No se conecta a Internet** ni recoge ningún dato.

## Por qué puedes fiarte

El parche es un programa que vas a ejecutar en tu ordenador, así que deberías poder comprobar qué hace:

- **Todo el código fuente está aquí,** con licencia MIT.
- **Puedes compilarlo tú mismo** con herramientas gratuitas de Microsoft ejecutando un único script. Las instrucciones están en [docs/BUILDING.md](docs/BUILDING.md) (en inglés).
- **Las compilaciones son reproducibles:** con el mismo código y las mismas herramientas se obtienen exactamente los mismos archivos, así que puedes comparar tu compilación con la versión publicada.
- **Cada cambio en los archivos del juego estará documentado:** qué archivo, dónde, los bytes originales, los nuevos y por qué.
- **Las versiones las compila GitHub Actions** a partir del código público, nunca el ordenador de un desarrollador, y publican los hashes SHA-256 de todos sus archivos junto con las herramientas usadas. Ver [docs/RELEASING.md](docs/RELEASING.md) (en inglés).

## Cómo informar de un problema

Si el juego se cuelga o algo se ve mal, cuéntalo y adjunta los archivos de registro que hay en la carpeta del juego:

- `dcogl.log`: la sesión actual o la última;
- `dcogl.old.log`: la sesión anterior. Después de un cuelgue es habitual volver a abrir el juego antes de avisar, y este archivo todavía tiene el cuelgue.

Explica qué estabas haciendo cuando pasó y, más o menos, a qué hora: cada línea del registro lleva la hora. Los registros dicen qué versión de Windows, tarjeta gráfica, versión del juego y base de datos tienes, pero nada que te identifique, y el parche nunca los envía a ningún sitio por su cuenta.

## Documentación

La documentación técnica está en inglés:

| Documento | Contenido |
|---|---|
| [docs/DESIGN.md](docs/DESIGN.md) | Objetivos, alcance y todas las decisiones de diseño con su justificación |
| [docs/GAME-INTERNALS.md](docs/GAME-INTERNALS.md) | Lo que hemos aprendido de cómo funciona el juego y por qué falla en Windows actual |
| [docs/BACKEND-INTERFACE.md](docs/BACKEND-INTERFACE.md) | La interfaz de módulos gráficos del juego, que implementa el componente gráfico nuevo |
| [docs/INSTALLER.md](docs/INSTALLER.md) | El instalador del juego: qué instala y cómo |
| [docs/PATCHER.md](docs/PATCHER.md) | El parcheador: qué cambia, y cómo lo aplica y lo quita |
| [docs/BUILDING.md](docs/BUILDING.md) | Cómo compilar todo desde el código fuente |
| [docs/RELEASING.md](docs/RELEASING.md) | Cómo se compilan y publican las versiones |
| [docs/TOOLS.md](docs/TOOLS.md) | Herramientas de desarrollo del repositorio |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Cómo colaborar y qué no se debe subir nunca |

## Aviso legal

Este proyecto no está afiliado, respaldado ni relacionado con Dinamic Multimedia ni con ningún propietario actual de la marca *PC Fútbol* o del contenido del juego. *PC FÚTBOL* es una marca de su respectivo propietario. El parche se distribuye sin ningún archivo del juego y solo funciona con una copia que ya tengas.

El código fuente de este repositorio se publica bajo la [licencia MIT](LICENSE).
