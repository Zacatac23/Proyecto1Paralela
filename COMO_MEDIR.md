# Cómo correr las mediciones

Instrucciones para quien vaya a sacar los números finales del proyecto.
**Leer la sección de temperatura antes de correr nada** — es la que arruinó los primeros intentos.

## 1. Requisitos

Hace falta SDL2 instalado en el mismo toolchain que el `gcc` que vas a usar.
Con MSYS2, desde su terminal:

```bash
pacman -S mingw-w64-ucrt-x86_64-SDL2
```

Además hacen falta `make` y `awk` (`gawk`), que MSYS2 ya trae.

## 2. Compilar

Desde la carpeta del repo:

```bash
make
```

Genera seis binarios:

| binario | qué es |
|---|---|
| `avancesc.exe` | la versión original 100% secuencial (línea base histórica) |
| `avances_omp.exe` | el screensaver paralelo, con tope de 60 FPS — para verlo, no para medir |
| `bench_seq.exe` | build de medición **sin** `-fopenmp` |
| `bench_par.exe` | build de medición **con** `-fopenmp` |
| `bench_seq_viejo.exe` / `bench_par_viejo.exe` | los mismos, pero con el dibujo viejo (ver 4b) |

`bench_seq` y `bench_par` salen del **mismo** `avances_omp.c`. Lo único que los diferencia
es la bandera `-fopenmp`. Eso es lo que hace justa la comparación, así que no lo cambies.

Al compilar `bench_seq.exe` gcc avisa `ignoring #pragma omp`. **Ese aviso es correcto y esperado**:
es la señal de que ese binario quedó monohilo.

## 3. Temperatura — lo importante

Las mediciones se hicieron primero en una laptop (i9-14900HX) y salieron mal. El motivo:
**la CPU se calienta y baja la frecuencia**, así que cada corrida sale peor que la anterior.
Medido en esa máquina, a N = 200 000 con 8 hilos:

| estado de la máquina | cómputo por frame |
|---|---|
| en reposo (varios minutos sin carga) | 0.79 – 0.91 ms |
| después de un barrido completo | 2.71 – 2.92 ms |

Es una diferencia de **3×** que no tiene nada que ver con el código. Y como el barrido de hilos
corre 1, 2, 4, 8, 16 y 32 en ese orden, el sesgo cae siempre sobre los últimos: parece que
agregar hilos empeora, cuando en realidad es que la máquina se fue calentando.

Por eso:

- **Preferí una máquina de escritorio.** No sufre esto, o mucho menos.
- Si sólo hay laptop: enchufada, plan de energía en alto rendimiento, y con la pausa de
  enfriamiento bien arriba (`PAUSA=180`).
- **Cerrá todo antes de medir**: juegos, launchers (el cliente de League pesa), Spotify,
  navegador. La GPU también entra en el presupuesto térmico.
- Si el repo está dentro de OneDrive, esperá a que termine de sincronizar los `.exe` recién
  compilados. Sincronizando, los tiempos de dibujo se triplican.

## 4. Medir

```bash
make bench
```

Corre la batería completa y después imprime las tablas de speedup. Con la pausa por defecto
(60 s entre corridas) tarda alrededor de 25 minutos. Escribe `resultados.csv`.

Parámetros que podés ajustar:

```bash
make bench PAUSA=180                    # más enfriamiento (laptop)
make bench PAUSA=10                     # menos (escritorio bien ventilado)
make bench SEGUNDOS=20                  # corridas más largas, menos ruido
make bench N_LISTA="50000 100000 200000"
make bench HILOS_LISTA="1 2 4 8"
```

Para comprobar que la cadena funciona sin esperar 25 minutos:

```bash
make bench-rapido    # ~1 min, sin pausas. NO sirve para sacar numeros.
```

## 4b. Medir tambien el dibujo viejo (para la tabla antes/despues)

El resultado principal del proyecto es que **arreglar el dibujo cambio el techo de Amdahl**. Para
que esa comparacion valga, las dos mitades tienen que medirse en la misma maquina:

```bash
make bench-viejo    # misma bateria, pero con el dibujo original punto por punto
```

Escribe `resultados_render_viejo.csv`. El binario lo dice en su propio resumen
(`dibujo : clasico (1 llamada por punto)` contra `dibujo : agrupado`), asi que no hay forma de
confundir cual produjo cual CSV.

Los dos caminos conviven en el mismo `avances_omp.c` detras de `-DRENDER_AGRUPADO=0`. No hay dos
versiones del programa que puedan divergir.

Referencia medida en la laptop de desarrollo, N = 100 000 con 8 hilos, las dos corridas seguidas:

| | dibujo por frame | FPS |
|---|---|---|
| clasico | 16.07 ms | 59.4 |
| agrupado | 1.35 ms | 521.3 |

## 5. Verificar que los números tienen sentido

`make speedup` recalcula las tablas desde el CSV sin volver a medir.

Señales de que la corrida salió contaminada y hay que repetirla:

- **N chicos con speedup de FPS muy lejos de 1.00×.** Debajo de N = 50 000 el
  `if(N >= UMBRAL_PARALELO)` hace que el binario paralelo ejecute exactamente el mismo camino
  que el secuencial. Si ahí ves 0.42× o 1.15×, eso es ruido puro, no paralelismo.
- **El `dibujo` distinto entre secuencial y paralelo para el mismo N.** Es el mismo código en
  serie en los dos: si difieren mucho, la máquina cambió de estado entre una corrida y otra.
- **La eficiencia sube al agregar hilos.** Debería bajar siempre.

## 6. Qué reportar

`make speedup` separa tres cosas que conviene no mezclar:

- **speedup de cómputo** — lo que realmente se paralelizó (física + relanzamiento + agrupado
  de puntos). Es el número que mide el trabajo hecho.
- **speedup de FPS** — el frame completo, con el dibujo en serie adentro. Siempre menor.
- **techo de Amdahl** — el máximo que puede dar el FPS con la fracción paralelizable medida,
  por más hilos que se agreguen.

Reportá los tres. El interesante del proyecto es que el techo de Amdahl **cambió** cuando se
arregló el dibujo: antes la parte paralelizable era el 3% del frame y el techo 1.03×; después
del agrupado de puntos pasó a ser más de la mitad del frame, con techo por encima de 2×.

Los dos CSV que produzcas (`resultados.csv` y `resultados_render_viejo.csv`) son los del informe;
commiteálos cuando estés conforme con ellos.

En `mediciones_laptop/` quedaron las corridas de desarrollo. **No las uses**: se tomaron en la
laptop con throttling y están sesgadas. Están sólo como registro de lo que se investigó.

Un detalle al comparar con ellas: en el CSV del dibujo viejo las columnas 6 y 7 se llamaban
`fisica_ms` y `render_ms`; ahora son `computo_ms` y `dibujo_ms`, que no significan lo mismo —
`computo` incluye el agrupado de puntos, que antes no existía.
