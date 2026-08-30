# Mediciones de la laptop de desarrollo — NO usar para el informe

Estos dos CSV son el registro de lo que se midio durante el desarrollo, en una laptop
i9-14900HX que **hace throttling termico**. Se guardan como historia, no como resultado.

| archivo | que contiene |
|---|---|
| `render_viejo.csv` | barrido con el dibujo original, una llamada SDL por punto |
| `render_agrupado.csv` | barrido con el dibujo agrupado |

Por que no sirven: bajo corridas consecutivas la CPU baja de frecuencia y cada corrida sale
peor que la anterior. La misma configuracion (N = 200 000, 8 hilos) dio 0.79 ms con la maquina
en reposo y 2.92 ms despues de un barrido completo. Como el barrido de hilos va 1, 2, 4, 8,
16, 32 en ese orden, el sesgo cae siempre sobre los ultimos y falsea la curva de escalabilidad.

Ademas `render_agrupado.csv` se tomo con el cliente de League y Spotify abiertos, que costaron
alrededor de un 45% de rendimiento.

Los numeros buenos se generan con `make bench` y `make bench-viejo` en una maquina que no
haga throttling. Ver `COMO_MEDIR.md`.
