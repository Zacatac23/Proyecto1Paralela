#!/usr/bin/env bash
# Bateria de pruebas: secuencial vs paralelo.
#
#   ./benchmark.sh [opciones]
#
#     --modo completo|n|hilos   que barrido correr        (def: completo)
#     --salida ARCHIVO          CSV de salida             (def: resultados.csv)
#     --n-lista "1000 5000"     N para el barrido de N    (def: 1000 ... 200000)
#     --hilos-lista "1 2 4"     hilos para su barrido     (def: 1 2 4 8 16 32)
#     --n-fijo N                N del barrido de hilos    (def: 200000)
#     --hilos H                 hilos del barrido de N    (def: 8)
#     --segundos S              duracion de cada corrida  (def: 12)
#     --pausa S                 reposo ANTES de cada corrida, para que la CPU se
#                               enfrie (def: 0)
#     --seq EXE / --par EXE     que binarios usar (def: bench_seq.exe / bench_par.exe).
#                               Sirve para medir el render viejo: --seq bench_seq_viejo.exe
#
# Todo se pasa por argumentos y no por variables de entorno: el make de MSYS2
# no propaga el entorno a los procesos hijos de forma confiable.
#
# Requiere bench_seq.exe y bench_par.exe, que salen del MISMO avances_omp.c;
# lo unico que los diferencia es -fopenmp. Ver el Makefile.

set -u

MODO=completo
SALIDA=resultados.csv
N_LISTA="1000 5000 20000 50000 100000 200000"
HILOS_LISTA="1 2 4 8 16 32"
N_FIJO=200000
HILOS=8
SEGUNDOS=12
PAUSA=0
EXE_SEQ=bench_seq.exe
EXE_PAR=bench_par.exe

while [ $# -gt 0 ]; do
    case "$1" in
        --modo)        MODO="$2";        shift 2 ;;
        --salida)      SALIDA="$2";      shift 2 ;;
        --n-lista)     N_LISTA="$2";     shift 2 ;;
        --hilos-lista) HILOS_LISTA="$2"; shift 2 ;;
        --n-fijo)      N_FIJO="$2";      shift 2 ;;
        --hilos)       HILOS="$2";       shift 2 ;;
        --segundos)    SEGUNDOS="$2";    shift 2 ;;
        --pausa)       PAUSA="$2";       shift 2 ;;
        --seq)         EXE_SEQ="$2";     shift 2 ;;
        --par)         EXE_PAR="$2";     shift 2 ;;
        -h|--ayuda)    sed -n '2,20p' "$0"; exit 0 ;;
        *)             echo "Opcion desconocida: $1" >&2; exit 1 ;;
    esac
done

for exe in "$EXE_SEQ" "$EXE_PAR"; do
    if [ ! -x "$exe" ]; then
        echo "Falta $exe. Corre 'make' primero." >&2
        exit 1
    fi
done

echo "modo,hilos,N,frames,fps,computo_ms,dibujo_ms" > "$SALIDA"

corrida() {   # corrida <exe> <N> <hilos> <segundos>
    local exe=$1 n=$2 h=$3 s=$4 linea

    # Enfriamiento. En una CPU de laptop, 19 corridas seguidas al 100% bajan la
    # frecuencia y cada corrida sale peor que la anterior: sin esta pausa el
    # barrido de hilos queda sesgado contra los ultimos valores medidos.
    if [ "$PAUSA" -gt 0 ]; then
        printf '  (enfriando %ss)
' "$PAUSA"
        sleep "$PAUSA"
    fi

    printf '  %-14s N=%-7s hilos=%-3s ... ' "$exe" "$n" "$h"
    linea=$(./"$exe" "$n" "$h" "$s" 2>&1 | grep '^CSV' | head -1)
    if [ -z "$linea" ]; then
        echo "SIN DATOS"
        return 1
    fi
    echo "${linea#CSV,}" >> "$SALIDA"
    echo "${linea#CSV,}" | awk -F, '{printf "fps=%-9.2f computo=%.4f ms\n", $5, $6}'
}

barrido_n() {
    echo "=== escalado con N (secuencial vs paralelo x$HILOS, ${SEGUNDOS}s por corrida) ==="
    for n in $N_LISTA; do
        corrida "$EXE_SEQ" "$n" 1 "$SEGUNDOS"
        corrida "$EXE_PAR" "$n" "$HILOS" "$SEGUNDOS"
    done
}

barrido_hilos() {
    echo "=== escalado con hilos (N = $N_FIJO, ${SEGUNDOS}s por corrida) ==="
    corrida "$EXE_SEQ" "$N_FIJO" 1 "$SEGUNDOS"
    for h in $HILOS_LISTA; do
        corrida "$EXE_PAR" "$N_FIJO" "$h" "$SEGUNDOS"
    done
}

case "$MODO" in
    completo) barrido_n; echo ""; barrido_hilos ;;
    n)        barrido_n ;;
    hilos)    barrido_hilos ;;
    *)        echo "Modo desconocido: $MODO (usa completo, n o hilos)" >&2; exit 1 ;;
esac

echo ""
echo "Resultados crudos en $SALIDA"
