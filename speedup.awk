# speedup.awk - convierte resultados.csv en tablas de speedup
#
#   awk -f speedup.awk resultados.csv
#
# Entrada: modo,hilos,N,frames,fps,computo_ms,dibujo_ms
#
# Calcula tres cosas distintas, que conviene no confundir:
#   speedup de computo= lo que realmente se paralelizo
#   speedup de FPS    = el frame completo, con el dibujo en serie adentro
#   techo de Amdahl   = 1/(1-p), con p = fraccion del frame que es computo
# Salida en ASCII puro, para que no se rompa en la consola de Windows.

BEGIN {
    FS = ","
    salto = sprintf("%s", "")
}

NR == 1 && $1 == "modo" { next }
NF < 7                  { next }

{
    modo = $1; h = $2 + 0; n = $3 + 0
    fps = $5 + 0; fis = $6 + 0; ren = $7 + 0
    if (fis <= 0) next

    if (!(n in todosN)) { todosN[n] = 1; listaN[++cantN] = n }

    if (modo == "SECUENCIAL") {
        scnt[n]++; sfis[n] += fis; sren[n] += ren; sfps[n] += fps
    } else {
        k = n "|" h
        if (!(k in pcnt)) { pares[++cantPares] = k; hilosPorN[n]++ }
        pcnt[k]++; pfis[k] += fis; pren[k] += ren; pfps[k] += fps
        nDe[k] = n; hDe[k] = h
    }
}

function ordenar(arr, len,   i, j, tmp) {
    for (i = 2; i <= len; i++) {
        tmp = arr[i]
        j = i - 1
        while (j >= 1 && arr[j] + 0 > tmp + 0) { arr[j + 1] = arr[j]; j-- }
        arr[j + 1] = tmp
    }
}

function miles(x,   s, r, i, c) {
    s = sprintf("%d", x); r = ""; c = 0
    for (i = length(s); i >= 1; i--) {
        r = substr(s, i, 1) r
        if (++c % 3 == 0 && i > 1) r = " " r
    }
    return r
}

END {
    if (cantN == 0) {
        print "No hay datos utilizables en el CSV."
        exit 1
    }

    ordenar(listaN, cantN)

    print  "==================================================================================="
    printf " ANALISIS DE SPEEDUP - %s\n", FILENAME
    print  "==================================================================================="
    print  ""

    # ---------------------------------------------------------- promedios
    for (i = 1; i <= cantN; i++) {
        n = listaN[i]
        if (scnt[n] > 0) {
            sfisA[n] = sfis[n] / scnt[n]
            srenA[n] = sren[n] / scnt[n]
            sfpsA[n] = sfps[n] / scnt[n]
        }
    }
    for (i = 1; i <= cantPares; i++) {
        k = pares[i]
        pfisA[k] = pfis[k] / pcnt[k]
        prenA[k] = pren[k] / pcnt[k]
        pfpsA[k] = pfps[k] / pcnt[k]
    }

    # ------------------------------------------- TABLA 1: escalado con N
    print "TABLA 1 - Escalado con N   (para cada N, la mejor corrida paralela)"
    print ""
    print "        N | comput seq | comput par | hilos | speedup |  FPS seq |  FPS par | FPS sp"
    print "----------+------------+------------+-------+---------+----------+----------+-------"

    mejorSp = 0; mejorN = 0; mejorH = 0; equilibrio = 0

    for (i = 1; i <= cantN; i++) {
        n = listaN[i]
        if (!(n in sfisA)) continue

        mejor = -1; mejorHilos = 0
        for (j = 1; j <= cantPares; j++) {
            k = pares[j]
            if (nDe[k] != n) continue
            if (mejor < 0 || pfisA[k] < mejor) { mejor = pfisA[k]; mejorHilos = hDe[k]; mejorK = k }
        }
        if (mejor < 0) continue

        sp = sfisA[n] / mejor
        spf = pfpsA[mejorK] / sfpsA[n]

        printf "%9s | %10.4f | %10.4f | %5d | %6.2fx | %8.2f | %8.2f | %5.2fx\n", \
               miles(n), sfisA[n], mejor, mejorHilos, sp, sfpsA[n], pfpsA[mejorK], spf

        if (sp > mejorSp) { mejorSp = sp; mejorN = n; mejorH = mejorHilos }
        if (sp >= 1.0 && equilibrio == 0) equilibrio = n
    }

    print ""
    print "  computo seq/par en ms por frame. 'FPS sp' es el speedup del frame completo,"
    print "  que incluye el dibujo en serie: por eso es menor que el del computo."
    print ""

    # -------------------------------------- TABLA 2: escalado con hilos
    nEscala = 0; maxH = 0
    for (i = 1; i <= cantN; i++) {
        n = listaN[i]
        if (hilosPorN[n] >= maxH && (n in sfisA)) { maxH = hilosPorN[n]; nEscala = n }
    }

    if (nEscala > 0 && maxH >= 2) {
        print ""
        printf "TABLA 2 - Escalado con hilos, N = %s   (base secuencial: %.4f ms)\n", \
               miles(nEscala), sfisA[nEscala]
        print ""
        print " hilos | computo ms | speedup | eficiencia"
        print "-------+------------+---------+-----------"

        cantH = 0
        for (j = 1; j <= cantPares; j++) {
            k = pares[j]
            if (nDe[k] == nEscala) listaH[++cantH] = hDe[k]
        }
        ordenar(listaH, cantH)

        for (j = 1; j <= cantH; j++) {
            h = listaH[j]
            k = nEscala "|" h
            sp = sfisA[nEscala] / pfisA[k]
            printf "%6d | %10.4f | %6.2fx | %8.1f%%\n", h, pfisA[k], sp, 100 * sp / h
        }
        print ""
        print "  eficiencia = speedup / hilos. Si cae fuerte al subir hilos, el costo de"
        print "  abrir y cerrar la region paralela ya pesa mas que el trabajo repartido."
        print ""
    }

    # ------------------------------------------------------ conclusiones
    print ""
    print "RESUMEN"
    print "-------"

    if (mejorN > 0)
        printf "  mejor speedup de computo: %.2fx  (N = %s, %d hilos)\n", \
               mejorSp, miles(mejorN), mejorH

    if (equilibrio > 0)
        printf "  punto de equilibrio     : N ~ %s  (debajo de eso el paralelo pierde)\n", miles(equilibrio)
    else
        printf "  punto de equilibrio     : no se alcanza en el rango medido\n"

    nRef = listaN[cantN]
    if (nRef in sfisA) {
        p = sfisA[nRef] / (sfisA[nRef] + srenA[nRef])
        printf "  a N = %s: computo %.2f ms + dibujo %.2f ms por frame\n", \
               miles(nRef), sfisA[nRef], srenA[nRef]
        printf "  fraccion paralelizable  : %.2f%%  ->  techo de Amdahl sobre FPS: %.3fx\n", \
               100 * p, 1 / (1 - p)
        print  "  o sea que por mas hilos que se agreguen, el FPS no puede pasar de ese techo."
    }
    print ""
}
