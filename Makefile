# Makefile - Proyecto 1, Computacion Paralela y Distribuida (UVG)
#
# Correr desde una shell tipo MSYS2 / Git Bash (usa sh para los comandos).
#
#   make                 compila todo
#   make bench           corre la bateria completa y muestra los speedups
#   make bench-rapido    version corta, para verificar que todo anda
#   make escala-n        solo el barrido de N
#   make escala-hilos    solo el barrido de hilos
#   make speedup         recalcula las tablas desde el CSV que ya exista
#   make correr          lanza el screensaver paralelo
#   make limpiar         borra binarios y resultados
#
# Parametros (se pueden pisar en la linea de comandos):
#   make bench SEGUNDOS=20 N_LISTA="50000 100000" HILOS_LISTA="1 2 4 8"
#   make correr N=8000

# Segun como se lance make (MSYS2, Git Bash, cmd), TMP/TEMP pueden llegar
# apuntando a C:\Windows y gcc falla al crear sus archivos temporales. Usamos
# un temporal propio del proyecto, en ruta relativa para que lo entiendan tanto
# el make de MSYS2 como el gcc nativo.
export TMPDIR := .tmp
export TMP    := .tmp
export TEMP   := .tmp

CC      := gcc
CFLAGS  := -O2 -std=c11
WARN    := -Wall
SDL     := -lmingw32 -lSDL2main -lSDL2 -lm
OMP     := -fopenmp
MEDIR   := -DLIMITE_FPS=0

FUENTE_BASE := avances.c
FUENTE_OMP  := avances_omp.c
CSV         := resultados.csv

# ---- parametros de las corridas ----
N_LISTA     ?= 1000 5000 20000 50000 100000 200000
HILOS_LISTA ?= 1 2 4 8 16 32
N_FIJO      ?= 200000
SEGUNDOS    ?= 12
# Reposo antes de cada corrida. En laptop hace falta: sin esto la CPU se calienta
# y cada corrida sale peor que la anterior, sesgando el barrido de hilos.
PAUSA       ?= 60
N           ?= 5000
HILOS       ?= 8

# Opciones que recibe benchmark.sh, armadas desde las variables de arriba.
OPCIONES := --n-lista "$(N_LISTA)" --hilos-lista "$(HILOS_LISTA)" \
            --n-fijo $(N_FIJO) --hilos $(HILOS) --segundos $(SEGUNDOS) --pausa $(PAUSA)

BINARIOS := avancesc.exe avances_omp.exe bench_seq.exe bench_par.exe \n            bench_seq_viejo.exe bench_par_viejo.exe

.PHONY: all todo bench bench-viejo bench-rapido escala-n escala-hilos speedup correr correr-base verificar limpiar clean ayuda

all: $(BINARIOS)
todo: all

# ---------------------------------------------------------------- compilacion

# Linea base original, 100% secuencial. No lleva OpenMP nunca.
avancesc.exe: $(FUENTE_BASE)
	@mkdir -p .tmp
	$(CC) $(CFLAGS) $(WARN) $< -o $@ $(SDL)

# Screensaver paralelo, con el tope de 60 FPS puesto (para mirarlo, no para medir).
avances_omp.exe: $(FUENTE_OMP)
	@mkdir -p .tmp
	$(CC) $(CFLAGS) $(WARN) $(OMP) $< -o $@ $(SDL)

# Binarios de medicion: sin tope de FPS. Salen del MISMO fuente; lo unico que
# cambia es -fopenmp. Eso es lo que hace justa la comparacion.
bench_par.exe: $(FUENTE_OMP)
	@mkdir -p .tmp
	$(CC) $(CFLAGS) $(OMP) $(MEDIR) $< -o $@ $(SDL)

bench_seq.exe: $(FUENTE_OMP)
	@echo "  (los avisos de 'ignoring #pragma omp' son lo esperado: build secuencial)"
	@mkdir -p .tmp
	$(CC) $(CFLAGS) $(MEDIR) $< -o $@ $(SDL)

# Los mismos dos binarios pero con el DIBUJO VIEJO (una llamada SDL por punto).
# Sirven para medir el antes y el despues del agrupado en la misma maquina, que es
# la unica forma de que esa comparacion signifique algo.
VIEJO := -DRENDER_AGRUPADO=0

bench_par_viejo.exe: $(FUENTE_OMP)
	@mkdir -p .tmp
	$(CC) $(CFLAGS) $(OMP) $(MEDIR) $(VIEJO) $< -o $@ $(SDL)

bench_seq_viejo.exe: $(FUENTE_OMP)
	@mkdir -p .tmp
	$(CC) $(CFLAGS) $(MEDIR) $(VIEJO) $< -o $@ $(SDL)

# ---------------------------------------------------------------- mediciones

bench: bench_seq.exe bench_par.exe
	@echo "Cerra lo que puedas antes de medir: OneDrive sincronizando los .exe"
	@echo "recien compilados falsea los tiempos de render."
	@./benchmark.sh --modo completo --salida $(CSV) $(OPCIONES)
	@$(MAKE) --no-print-directory speedup

# Version corta, para comprobar que toda la cadena anda. Sin pausas y con corridas
# de 6s: sirve para ver que compila y mide, NO para sacar numeros.
bench-rapido: bench_seq.exe bench_par.exe
	@./benchmark.sh --modo completo --salida $(CSV) --n-lista "5000 100000" --hilos-lista "1 4 8" --n-fijo 100000 --hilos 8 --segundos 6 --pausa 0
	@$(MAKE) --no-print-directory speedup

# La misma bateria con el dibujo viejo, para la tabla antes/despues.
bench-viejo: bench_seq_viejo.exe bench_par_viejo.exe
	@./benchmark.sh --modo completo --salida resultados_render_viejo.csv $(OPCIONES) --seq bench_seq_viejo.exe --par bench_par_viejo.exe
	@$(MAKE) --no-print-directory speedup CSV=resultados_render_viejo.csv

escala-n: bench_seq.exe bench_par.exe
	@./benchmark.sh --modo n --salida $(CSV) $(OPCIONES)
	@$(MAKE) --no-print-directory speedup

escala-hilos: bench_seq.exe bench_par.exe
	@./benchmark.sh --modo hilos --salida $(CSV) $(OPCIONES)
	@$(MAKE) --no-print-directory speedup

speedup: $(CSV)
	@awk -f speedup.awk $(CSV)

$(CSV):
	@echo "No hay $(CSV) todavia. Corre 'make bench' o 'make bench-rapido'."
	@exit 1

# ---------------------------------------------------------------- utilidades

correr: avances_omp.exe
	./avances_omp.exe $(N) $(HILOS)

correr-base: avancesc.exe
	./avancesc.exe $(N)

# Chequeo rapido de que las dos compilaciones andan y dan numeros coherentes.
verificar: bench_seq.exe bench_par.exe
	@echo "--- secuencial, N=$(N) ---"
	@./bench_seq.exe $(N) 1 6 | grep '^CSV' || echo "FALLO"
	@echo "--- paralelo x$(HILOS), N=$(N) ---"
	@./bench_par.exe $(N) $(HILOS) 6 | grep '^CSV' || echo "FALLO"

limpiar:
	rm -rf $(BINARIOS) $(CSV) .tmp

clean: limpiar

ayuda:
	@echo "make              compila todo"
	@echo "make bench        bateria completa + speedups (dibujo agrupado)"
	@echo "make bench-viejo  la misma bateria con el dibujo viejo, para comparar"
	@echo "make bench-rapido bateria corta                (~1 min)"
	@echo "make escala-n     solo barrido de N"
	@echo "make escala-hilos solo barrido de hilos"
	@echo "make speedup      recalcula tablas desde $(CSV)"
	@echo "make correr       lanza el screensaver paralelo (N=$(N) HILOS=$(HILOS))"
	@echo "make verificar    corrida corta de las dos versiones"
	@echo "make limpiar      borra binarios y resultados"

