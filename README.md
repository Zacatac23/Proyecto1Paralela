# Proyecto 1 - Computación Paralela y Distribuida (UVG)

Screensaver de fuegos artificiales con SDL2 (fondo *Bad Piggies* y cerditos flotantes)
usado como caso de estudio para medir el speedup de paralelizar con **OpenMP** el
cómputo de física de partículas, contra una versión 100% secuencial.

## Qué contiene el repo

| archivo / carpeta | qué es |
|---|---|
| `avances.c` | versión original 100% secuencial (línea base histórica, monohilo, sin OpenMP) |
| `avances_omp.c` | mismo screensaver, pero instrumentado; compila en modo paralelo (`-fopenmp`) o secuencial con el mismo fuente |
| `Makefile` | compila los 6 binarios y orquesta las mediciones |
| `benchmark.sh` | corre la batería de pruebas (barrido de `N` y de hilos) y escribe un CSV |
| `speedup.awk` | convierte el CSV en tablas de speedup de cómputo, speedup de FPS y techo de Amdahl |
| `COMO_MEDIR.md` | guía paso a paso para reproducir las mediciones (incluye advertencias de *thermal throttling*) |
| `mediciones_laptop/` | mediciones de desarrollo tomadas en una laptop con throttling — **no usar para el informe final**, ver `LEEME.md` adentro |
| `fondo.bmp`, `cerdito.bmp` | assets gráficos que carga el screensaver |

## Requisitos

- `gcc` con SDL2 disponible en el toolchain (en MSYS2: `pacman -S mingw-w64-ucrt-x86_64-SDL2`)
- `make` y `awk` (`gawk`)
- Correr todo desde una shell tipo MSYS2 / Git Bash

## Compilar

```bash
make
```

Genera seis binarios:

- `avancesc.exe` — línea base secuencial
- `avances_omp.exe` — screensaver paralelo para ver en pantalla (con tope de 60 FPS)
- `bench_seq.exe` / `bench_par.exe` — builds de medición (sin tope de FPS), salen del mismo `avances_omp.c`, solo cambia `-fopenmp`
- `bench_seq_viejo.exe` / `bench_par_viejo.exe` — igual, pero con el dibujo original (para comparar antes/después del render agrupado)

## Correr el screensaver

```bash
make correr N=8000 HILOS=8      # versión paralela
make correr-base N=8000         # versión secuencial
```

## Qué se paralelizó

`avances_omp.c` abre una sola región `#pragma omp parallel` por frame que reparte tres bloques:

1. **Física de partículas** (`actualizarParticula`) — `schedule(static)`, cada partícula es independiente.
2. **Relanzamiento de cohetes** — `schedule(dynamic, 4)`, porque el costo por cohete es irregular.
3. **Clasificación de puntos para el dibujo agrupado** — partición manual por rango de índices, sin sincronización porque cada hilo escribe en su propia franja del buffer.

El **renderizado** (llamadas a `SDL_Renderer`) se queda siempre en un solo hilo porque SDL solo
permite usarse desde el hilo que creó el renderer.

Otros detalles de la implementación:

- Generador aleatorio propio (xorshift32) con un estado por hilo alineado a línea de caché, para evitar *false sharing*.
- `UMBRAL_PARALELO = 50000`: por debajo de ese `N` no se abre la región paralela (el fork/join sale más caro que el trabajo).
- Dibujo "agrupado": los puntos se agrupan por color + nivel de alfa en 96 "cubos" y se dibujan con `SDL_RenderDrawPoints`, bajando de ~1.2M llamadas SDL por frame a unos cientos.

## Medir rendimiento

```bash
make bench           # batería completa (~25 min con pausas de enfriamiento) -> resultados.csv
make bench-rapido     # versión corta para verificar que la cadena funciona (no usar para reportar)
make bench-viejo      # misma batería con el dibujo original, para la comparación antes/después
make speedup          # recalcula las tablas desde el CSV existente, sin volver a medir
```

`speedup.awk` reporta tres números que conviene no mezclar: **speedup de cómputo** (lo que
realmente se paralelizó), **speedup de FPS** (el frame completo, con el dibujo en serie) y el
**techo de Amdahl** según la fracción paralelizable medida.

Antes de medir, leer **`COMO_MEDIR.md`** — documenta un problema real de *thermal throttling* en
laptop que sesgó las primeras corridas (hasta 3× de diferencia entre máquina en reposo y después
de un barrido).


