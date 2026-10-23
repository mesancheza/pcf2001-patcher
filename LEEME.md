# pcf2001-patcher

Parche no oficial y de código abierto para que **PC FÚTBOL 2001** (Dinamic Multimedia, 2000–2001) funcione correctamente en las versiones actuales de Windows.

> **Estado: en desarrollo. Todavía no hay ninguna versión publicada.** Nada de lo que hay en este repositorio está listo para jugar. Esta página explica qué hará el parche y cómo se construye, para que cualquiera pueda seguir y revisar el trabajo.

*English version: [README.md](README.md)*

## Qué arregla

| Problema en Windows actual | Estado |
|---|---|
| El juego se detiene al arrancar con *"No se pudo iniciar DirectX. El programa no puede comenzar."* | Causa encontrada, solución en curso |
| La pantalla completa cambia todo el escritorio a 800×600 | Previsto: el juego se dibuja a su resolución y se escala a tu pantalla manteniendo la proporción |
| En modo ventana, algunos elementos se dibujan en el sitio equivocado | Previsto: lo resuelve el mismo cambio |
| Algunos nombres largos de equipos salen como caracteres sin sentido y algunos jugadores aparecen con el nombre de otro (hasta ahora solo se evitaba con el modo de compatibilidad "Windows XP SP2") | En investigación; se arreglará en el código del juego |
| El juego puede colgarse al salir | Causa encontrada (al cerrarse, el juego usa objetos gráficos que ya ha liberado); la solución irá en el componente gráfico nuevo |
| Otros cuelgues aleatorios que han reportado los jugadores | Se investigarán después de lo anterior |

Los detalles técnicos de cada problema están en [docs/GAME-INTERNALS.md](docs/GAME-INTERNALS.md) (en inglés).

## Qué vas a necesitar

- **Tu propia copia de PC FÚTBOL 2001, con la actualización oficial de Dinamic a la versión 3.6** (`PCFPATCH3.6.EXE`). Cuando está instalada, el menú principal muestra "V3.6" en la esquina superior derecha. Solo se admite la 3.6; el motivo está en [docs/DESIGN.md](docs/DESIGN.md#d1-target-dinamics-v36-only).
- **El CD del juego, o una imagen del CD montada como unidad.** El parche mantiene la comprobación del CD, y Windows 10/11 monta un archivo `.iso` con un doble clic.
- **Windows 10 u 11.**

El parche **no** necesita dgVoodoo, modos de compatibilidad, permisos de administrador ni ningún programa adicional.

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
- **Cada versión publicará los hashes SHA-256** de todos sus archivos.

## Documentación

La documentación técnica está en inglés:

| Documento | Contenido |
|---|---|
| [docs/DESIGN.md](docs/DESIGN.md) | Objetivos, alcance y todas las decisiones de diseño con su justificación |
| [docs/GAME-INTERNALS.md](docs/GAME-INTERNALS.md) | Lo que hemos aprendido de cómo funciona el juego y por qué falla en Windows actual |
| [docs/BACKEND-INTERFACE.md](docs/BACKEND-INTERFACE.md) | La interfaz de módulos gráficos del juego, que implementa el componente gráfico nuevo |
| [docs/BUILDING.md](docs/BUILDING.md) | Cómo compilar todo desde el código fuente |
| [docs/TOOLS.md](docs/TOOLS.md) | Herramientas de desarrollo del repositorio |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Cómo colaborar y qué no se debe subir nunca |

## Aviso legal

Este proyecto no está afiliado, respaldado ni relacionado con Dinamic Multimedia ni con ningún propietario actual de la marca *PC Fútbol* o del contenido del juego. *PC FÚTBOL* es una marca de su respectivo propietario. El parche se distribuye sin ningún archivo del juego y solo funciona con una copia que ya tengas.

El código fuente de este repositorio se publica bajo la [licencia MIT](LICENSE).
