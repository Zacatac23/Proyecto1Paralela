/*
 * Screensaver Fuegos Artificiales PARALELO (OpenMP) - Proyecto 1 Computacion Paralela y Distribuida
 * UVG - 2024
 *
 * Version paralela de avances.c. Se paraleliza UNICAMENTE el computo:
 *   - la fisica de las N particulas (actualizarParticula)
 *   - el escaneo y relanzamiento de cohetes
 * El renderizado NO se paraleliza: SDL_Renderer solo puede usarse desde el hilo
 * que lo creo, asi que todo el dibujo se queda en el hilo principal.
 *
 * Este archivo compila de las dos formas, con el MISMO codigo e instrumentacion:
 *   Paralelo:   gcc -O2 -std=c11 -fopenmp avances_omp.c -o par.exe -lmingw32 -lSDL2main -lSDL2 -lm
 *   Secuencial: gcc -O2 -std=c11          avances_omp.c -o seq.exe -lmingw32 -lSDL2main -lSDL2 -lm
 * Sin -fopenmp los #pragma se ignoran y queda monohilo: es la linea base del speedup.
 *
 * Uso: ./par.exe <N> [hilos] [segundos]
 *   N        = cantidad de particulas
 *   hilos    = numero de hilos OpenMP (0 o ausente = los que decida OpenMP)
 *   segundos = corre ese tiempo y sale solo con un resumen (0 = hasta ESC)
 *
 * Mide por separado el tiempo de fisica y el de render de cada frame, que es lo
 * que hace falta para calcular el speedup de la parte que si se paralelizo.
 */

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------- Generador aleatorio por hilo ----------------------
/*
 * aleatorioEntero() es estado global compartido: llamado desde varios hilos, o serializa
 * o devuelve secuencias distintas segun la implementacion, y en ambos casos
 * arruina la medicion. Cada hilo lleva aqui su propio xorshift32, y cada estado
 * ocupa una linea de cache completa para que dos hilos no se peleen por la misma
 * (false sharing).
 */
#define MAX_HILOS 256

typedef struct {
    unsigned int estado;
    char relleno[60];      /* completa 64 bytes = 1 linea de cache */
} EstadoAleatorio;

static EstadoAleatorio generadores[MAX_HILOS];

static inline int hiloActual(void) {
#ifdef _OPENMP
    return omp_get_thread_num();
#else
    return 0;
#endif
}

static inline unsigned int siguienteAleatorio(void) {
    unsigned int* s = &generadores[hiloActual()].estado;
    unsigned int x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/* Equivalente a aleatorioEntero(): entero no negativo */
static inline int aleatorioEntero(void) {
    return (int)(siguienteAleatorio() >> 1);
}

/* Equivalente a aleatorioFloat(): flotante en [0, 1) */
static inline float aleatorioFloat(void) {
    return (float)(siguienteAleatorio() >> 8) * (1.0f / 16777216.0f);
}

static void sembrarGeneradores(unsigned int semilla) {
    for (int i = 0; i < MAX_HILOS; i++) {
        unsigned int s = semilla + (unsigned int)i * 2654435761u;
        if (s == 0) s = 0x9E3779B9u;   /* xorshift32 no admite estado cero */
        generadores[i].estado = s;
    }
}

// ---------------------- Parámetros y constantes ----------------------
#define ANCHO 800            /* Ancho del canvas */
#define ALTO  600            /* Alto del canvas */

#define GRAVEDAD 0.025f       /* Fuerza de gravedad */
#define VELOCIDAD_MIN 0.4f    /* Velocidad inicial mínima de explosión */
#define VELOCIDAD_MAX 2.4f    /* Velocidad inicial máxima de explosión */
#define RESISTENCIA_AIRE 0.985f
/*
 * Debajo de este N no compensa abrir la region paralela: el fork/join de OpenMP
 * cuesta ~0.1-0.15 ms por frame en Windows, mas que la fisica completa. Medido en
 * esta maquina el punto de equilibrio esta cerca de N = 50000 (ver resultados.csv).
 * Con la clausula if() la version paralela nunca es mas lenta que la secuencial.
 */
#define UMBRAL_PARALELO 50000

#ifndef LIMITE_FPS
#define LIMITE_FPS 60         /* Tope de FPS. Compilar con -DLIMITE_FPS=0 para medir. */
#endif

#define CANTIDAD_CERDITOS 10   /* Número de cerditos flotantes en pantalla */

/*
 * RENDER_AGRUPADO elige como se dibujan las explosiones:
 *   1 (por defecto) = puntos agrupados por color+alfa, un SDL_RenderDrawPoints por cubo
 *   0               = el dibujo original, una llamada SDL por punto
 * Existe para poder medir el ANTES y el DESPUES en la misma maquina, que es la unica
 * forma de que la comparacion signifique algo. Compilar con -DRENDER_AGRUPADO=0.
 */
#ifndef RENDER_AGRUPADO
#define RENDER_AGRUPADO 1
#endif

/*
 * ---------------------- Agrupado de puntos para el dibujo ----------------------
 * SDL_RenderDrawPoints() manda un arreglo entero de puntos en una sola llamada, pero
 * usa un unico color por llamada. Por eso los puntos se agrupan en "cubos" segun su
 * color de paleta y su nivel de alfa (derivado de la vida), y despues se dibuja un
 * cubo por llamada. Con esto el frame pasa de ~1.2 millones de llamadas SDL a unos
 * cientos, y el trabajo de agrupar es un recorrido O(N) que se hace en paralelo.
 */
#define NUM_PALETAS   6
#define ALFA_NIVELES  16
#define NUM_CUBOS     (NUM_PALETAS * ALFA_NIVELES)   /* 96 */

/* Central + cruz de 4 vecinos: el maximo que puede aportar una particula. */
#define MAX_PUNTOS_POR_PARTICULA 5

/* Una sola fuente de verdad para los colores: la usan el lanzamiento y el dibujo. */
static const Uint8 PALETAS[NUM_PALETAS][3] = {
    { 255,  50,  50 },   /* Rojo    */
    {  50, 220, 255 },   /* Cyan    */
    { 255, 215,   0 },   /* Dorado  */
    { 220,  50, 255 },   /* Magenta */
    { 255, 140,   0 },   /* Naranja */
    {  50, 255, 100 }    /* Verde   */
};

// ---------------------- Estados de Partícula ----------------------
typedef enum {
    ETAPA_INACTIVA,
    ETAPA_SUBIENDO,
    ETAPA_EXPLOTANDO
} EtapaParticula;

// ---------------------- Estructura de Partícula ----------------------
typedef struct {
    float x, y;             // Posición actual
    float vx, vy;           // Velocidad en X y Y
    Uint8 r, g, b;          // Color RGB
    Uint8 paleta;           // Indice en PALETAS[]. Cae en el byte de relleno que ya
                            // existia detras de r,g,b: la struct sigue pesando 48
                            // bytes y el bucle de fisica no lee ni un byte de mas.
    float vida;             // Vida restante (1.0f -> 0.0f)
    float decaimiento;      // Tasa de desvanecimiento por frame
    float origenX, origenY; // Centro de lanzamiento/explosión
    float alturaExplosion;  // Altura Y de estallido
    EtapaParticula etapa;   // SUBIENDO o EXPLOTANDO
    int esLiderCohete;      // 1 si dibuja la estela del cohete que sube
} Particula;

// ---------------------- Estructura de Objeto Flotante ----------------------
typedef struct {
    float x, y;          // Posición actual
    float vx, vy;        // Velocidad horizontal y vertical
    float angulo;        // Ángulo de oscilación/rotación
    float velocidadAng;  // Velocidad de rotación/balanceo
    float escala;        // Escala del gráfico
    int ancho, alto;     // Dimensiones del sprite
} ObjetoFlotante;

// Lanza un cohete que sube desde abajo y luego explota
void lanzarCohete(Particula* particulas, int inicioIndex, int cantidad, int retrasoGradual) {
    float origenX = 80.0f + (float)(aleatorioEntero() % (ANCHO - 160));
    float alturaExplosion = 60.0f + (float)(aleatorioEntero() % (ALTO / 2 + 50));
    float velSubida = 5.5f + (aleatorioFloat()) * 2.5f;

    int paleta = aleatorioEntero() % NUM_PALETAS;
    Uint8 r = PALETAS[paleta][0];
    Uint8 g = PALETAS[paleta][1];
    Uint8 b = PALETAS[paleta][2];

    for (int i = 0; i < cantidad; i++) {
        int idx = inicioIndex + i;
        Particula* p = &particulas[idx];
        p->x = origenX;
        p->y = (float)(ALTO + (retrasoGradual * 6)); // Posición de despegue escalonada
        p->origenX = origenX;
        p->origenY = alturaExplosion;
        p->alturaExplosion = alturaExplosion;
        p->vy = -velSubida;
        p->vx = (aleatorioFloat() - 0.5f) * 0.3f; // Pequeño desvío horizontal al subir
        p->r = r;
        p->g = g;
        p->b = b;
        p->paleta = (Uint8)paleta;
        p->vida = 1.0f;
        p->decaimiento = 0.003f + (aleatorioFloat()) * 0.006f;
        p->etapa = ETAPA_SUBIENDO;
        p->esLiderCohete = (i == 0) ? 1 : 0;
    }
}

// Actualiza la física de 1 partícula secuencialmente
void actualizarParticula(Particula* p) {
    if (p->vida <= 0.0f) return;

    if (p->etapa == ETAPA_SUBIENDO) {
        p->x += p->vx;
        p->y += p->vy;

        // Cuando alcanza la altura deseada, estalla en fuegos artificiales
        if (p->y <= p->alturaExplosion) {
            p->etapa = ETAPA_EXPLOTANDO;

            float angulo = (aleatorioFloat()) * 2.0f * (float)M_PI;
            float velocidad = VELOCIDAD_MIN + (aleatorioFloat()) * (VELOCIDAD_MAX - VELOCIDAD_MIN);

            p->vx = cosf(angulo) * velocidad;
            p->vy = sinf(angulo) * velocidad;
        }
    } else if (p->etapa == ETAPA_EXPLOTANDO) {
        // Dispersión con resistencia del aire y gravedad
        p->x += p->vx;
        p->y += p->vy;
        p->vy += GRAVEDAD;
        p->vx *= RESISTENCIA_AIRE;
        p->vy *= RESISTENCIA_AIRE;

        p->vida -= p->decaimiento;
    }
}

/*
 * Dibuja la estela del cohete que sube. Es lo unico que queda dibujandose una
 * particula por vez: solo hay un lider por cohete (N/100 particulas), asi que a
 * N = 200000 son ~2000 y no vale la pena agrupar. Ademas SDL_RenderDrawLines()
 * dibuja una polilinea conectada, no segmentos sueltos, asi que no serviria.
 * Las explosiones ya no pasan por aca: van agrupadas, ver clasificarPuntos().
 */
void dibujarEstelaCohete(SDL_Renderer* renderer, const Particula* p) {
    if (p->vida <= 0.0f) return;
    if (p->etapa != ETAPA_SUBIENDO) return;
    if (!p->esLiderCohete || p->y >= ALTO) return;

    int px = (int)p->x;
    int py = (int)p->y;

    SDL_SetRenderDrawColor(renderer, 255, 230, 150, 255);
    SDL_RenderDrawLine(renderer, px, py, px, py + 14);
    SDL_SetRenderDrawColor(renderer, 255, 120, 30, 200);
    SDL_RenderDrawLine(renderer, px - 1, py + 4, px + 1, py + 16);
}

/*
 * Dibujo original: un SDL_SetRenderDrawColor y hasta 5 SDL_RenderDrawPoint por
 * particula. A N = 200000 son del orden de 1.2 millones de llamadas por frame.
 * Se conserva solo para poder medirlo contra el agrupado en la misma maquina.
 */
void dibujarParticulaClasica(SDL_Renderer* renderer, const Particula* p) {
    if (p->vida <= 0.0f) return;

    if (p->etapa == ETAPA_SUBIENDO) {
        dibujarEstelaCohete(renderer, p);
    } else if (p->etapa == ETAPA_EXPLOTANDO) {
        Uint8 alpha = (Uint8)((p->vida < 0.0f ? 0.0f : p->vida) * 255.0f);
        SDL_SetRenderDrawColor(renderer, p->r, p->g, p->b, alpha);

        int px = (int)p->x;
        int py = (int)p->y;

        SDL_RenderDrawPoint(renderer, px, py);
        if (p->vida > 0.4f) {
            SDL_RenderDrawPoint(renderer, px + 1, py);
            SDL_RenderDrawPoint(renderer, px - 1, py);
            SDL_RenderDrawPoint(renderer, px, py + 1);
            SDL_RenderDrawPoint(renderer, px, py - 1);
        }
    }
}

#if RENDER_AGRUPADO

/* Nivel de alfa (0..ALFA_NIVELES-1) al que pertenece una vida dada. */
static inline int nivelAlfa(float vida) {
    int nivel = (int)(vida * ALFA_NIVELES);
    if (nivel < 0) nivel = 0;
    if (nivel >= ALFA_NIVELES) nivel = ALFA_NIVELES - 1;
    return nivel;
}

/* Cuantos puntos aporta una particula al dibujo: 0, 1 o 5. */
static inline int puntosDeParticula(const Particula* p) {
    if (p->vida <= 0.0f || p->etapa != ETAPA_EXPLOTANDO) return 0;
    return (p->vida > 0.4f) ? 5 : 1;
}

/*
 * Clasifica las particulas [lo, hi) en cubos (color x nivel de alfa) y escribe sus
 * puntos en la franja del buffer que le toca a este hilo. Es un counting sort local:
 *
 *   1. contar cuantos puntos va a tener cada cubo
 *   2. suma prefija -> donde empieza cada cubo dentro de la franja
 *   3. escribir los puntos en su lugar
 *
 * Cada hilo es dueño exclusivo de su franja, asi que no hace falta ninguna
 * sincronizacion: ni secciones criticas, ni atomicos.
 */
static void clasificarPuntos(const Particula* particulas, int lo, int hi,
                             SDL_Point* franja, int capacidad, int* cuenta, int* offset) {
    for (int c = 0; c < NUM_CUBOS; c++) cuenta[c] = 0;

    for (int i = lo; i < hi; i++) {
        int n = puntosDeParticula(&particulas[i]);
        if (n > 0) {
            cuenta[particulas[i].paleta * ALFA_NIVELES + nivelAlfa(particulas[i].vida)] += n;
        }
    }

    int acumulado = 0;
    for (int c = 0; c < NUM_CUBOS; c++) {
        offset[c] = acumulado;
        acumulado += cuenta[c];
    }

#ifdef VERIFICAR_PUNTOS
    /* La franja tiene que alcanzar justo: 5 puntos por particula del rango. */
    if (acumulado > capacidad) {
        fprintf(stderr, "FALLO: hilo escribiria %d puntos en una franja de %d\n",
                acumulado, capacidad);
        abort();
    }
#else
    (void)capacidad;
#endif

    /* Se reutiliza offset[] como cursor de escritura y al final se restaura. */
    int cursor[NUM_CUBOS];
    for (int c = 0; c < NUM_CUBOS; c++) cursor[c] = offset[c];

    for (int i = lo; i < hi; i++) {
        const Particula* p = &particulas[i];
        int n = puntosDeParticula(p);
        if (n == 0) continue;

        int cubo = p->paleta * ALFA_NIVELES + nivelAlfa(p->vida);
        int px = (int)p->x;
        int py = (int)p->y;
        int k = cursor[cubo];

        franja[k].x = px;      franja[k].y = py;      k++;
        if (n == 5) {
            franja[k].x = px + 1;  franja[k].y = py;      k++;
            franja[k].x = px - 1;  franja[k].y = py;      k++;
            franja[k].x = px;      franja[k].y = py + 1;  k++;
            franja[k].x = px;      franja[k].y = py - 1;  k++;
        }
        cursor[cubo] = k;
    }

#ifdef VERIFICAR_PUNTOS
    /* Cada cubo tiene que haber quedado exactamente lleno, sin pisar al vecino. */
    for (int c = 0; c < NUM_CUBOS; c++) {
        if (cursor[c] != offset[c] + cuenta[c]) {
            fprintf(stderr, "FALLO: cubo %d cerro en %d, esperado %d\n",
                    c, cursor[c], offset[c] + cuenta[c]);
            abort();
        }
    }
#endif
}

#endif  /* RENDER_AGRUPADO */

// Inicializa los cerditos flotantes
void inicializarObjetoFlotante(ObjetoFlotante* obj, int index) {
    obj->x = (float)(aleatorioEntero() % (ANCHO - 100) + 50);
    obj->y = (float)(aleatorioEntero() % (ALTO - 200) + 50);
    obj->vx = ((float)(aleatorioEntero() % 100) / 100.0f - 0.5f) * 1.2f;
    obj->vy = ((float)(aleatorioEntero() % 100) / 100.0f - 0.5f) * 0.8f;
    if (fabsf(obj->vx) < 0.2f) obj->vx = 0.4f;
    obj->angulo = (aleatorioFloat()) * 360.0f;
    obj->velocidadAng = ((float)(aleatorioEntero() % 100) / 100.0f - 0.5f) * 1.5f;
    obj->escala = 0.35f + ((float)(aleatorioEntero() % 100) / 100.0f) * 0.25f;
    obj->ancho = 120;
    obj->alto = 120;
}

// Actualiza movimiento y flotación de un cerdito
void actualizarObjetoFlotante(ObjetoFlotante* obj) {
    obj->x += obj->vx;
    obj->y += obj->vy + sinf(obj->angulo * (float)M_PI / 180.0f) * 0.3f;
    obj->angulo += obj->velocidadAng;

    int renderW = (int)(obj->ancho * obj->escala);
    int renderH = (int)(obj->alto * obj->escala);

    if (obj->x <= 0 || obj->x + renderW >= ANCHO) {
        obj->vx = -obj->vx;
    }
    if (obj->y <= 0 || obj->y + renderH >= ALTO - 50) {
        obj->vy = -obj->vy;
    }
}

// Renderiza un cerdito flotante
void dibujarObjetoFlotante(SDL_Renderer* renderer, SDL_Texture* textura, const ObjetoFlotante* obj) {
    int renderW = (int)(obj->ancho * obj->escala);
    int renderH = (int)(obj->alto * obj->escala);

    SDL_Rect dest = { (int)obj->x, (int)obj->y, renderW, renderH };

    if (textura != NULL) {
        SDL_RenderCopyEx(renderer, textura, NULL, &dest, (double)obj->angulo, NULL, SDL_FLIP_NONE);
    } else {
        SDL_SetRenderDrawColor(renderer, 50, 205, 50, 255);
        SDL_RenderFillRect(renderer, &dest);
    }
}

// Carga imagen BMP y genera la textura en SDL2
SDL_Texture* cargarTexturaBMP(SDL_Renderer* renderer, const char* ruta, int aplicarColorKey) {
    SDL_Surface* surface = SDL_LoadBMP(ruta);
    if (surface == NULL) {
        printf("Aviso: No se pudo cargar la imagen '%s': %s. Se usara representacion alternativa.\n", ruta, SDL_GetError());
        return NULL;
    }

    if (aplicarColorKey) {
        Uint32 key = SDL_MapRGB(surface->format, 255, 255, 255);
        SDL_SetColorKey(surface, SDL_TRUE, key);
    }

    SDL_Texture* textura = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);

    if (textura == NULL) {
        printf("Error al crear textura desde surface '%s': %s\n", ruta, SDL_GetError());
    }

    return textura;
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        fprintf(stderr, "Uso: %s <N> [hilos] [segundos]\n", argv[0]);
        fprintf(stderr, "  N        = cantidad de particulas\n");
        fprintf(stderr, "  hilos    = hilos OpenMP (0 o ausente = automatico)\n");
        fprintf(stderr, "  segundos = duracion de la corrida (0 o ausente = hasta ESC)\n");
        return 1;
    }

    int N = atoi(argv[1]);
    if (N <= 0) {
        fprintf(stderr, "Error: N debe ser un entero positivo. Recibido: %s\n", argv[1]);
        return 1;
    }

    int hilosPedidos = (argc > 2) ? atoi(argv[2]) : 0;
    int segundosLimite = (argc > 3) ? atoi(argv[3]) : 0;

#ifdef _OPENMP
    if (hilosPedidos > 0) omp_set_num_threads(hilosPedidos);
    int hilosUsados = omp_get_max_threads();
    const char* modo = "PARALELO";
#else
    (void)hilosPedidos;
    int hilosUsados = 1;
    const char* modo = "SECUENCIAL";
#endif

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Error al inicializar SDL: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Screensaver Bad Piggies & Fuegos Artificiales (100% SECUENCIAL) - FPS: 0",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        ANCHO, ALTO, SDL_WINDOW_SHOWN
    );
    if (window == NULL) {
        fprintf(stderr, "Error al crear ventana: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) {
        fprintf(stderr, "Error al crear renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    sembrarGeneradores((unsigned int)time(NULL));

    SDL_Texture* texturaFondo = cargarTexturaBMP(renderer, "fondo.bmp", 0);
    SDL_Texture* texturaCerdito = cargarTexturaBMP(renderer, "cerdito.bmp", 1);

    ObjetoFlotante cerditos[CANTIDAD_CERDITOS];
    for (int i = 0; i < CANTIDAD_CERDITOS; i++) {
        inicializarObjetoFlotante(&cerditos[i], i);
    }

    // ---------------- Reserva de memoria secuencial para N partículas ----------------
    Particula* particulas = (Particula*)malloc(N * sizeof(Particula));
    if (particulas == NULL) {
        fprintf(stderr, "Error al asignar memoria para %d particulas.\n", N);
        if (texturaFondo) SDL_DestroyTexture(texturaFondo);
        if (texturaCerdito) SDL_DestroyTexture(texturaCerdito);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /*
     * Buffers del dibujo agrupado. Se reservan una sola vez: dentro del frame no se
     * pide memoria. Cada hilo escribe en la franja [5*lo, 5*hi) que le corresponde
     * segun la particion de indices, asi que las franjas son disjuntas por
     * construccion y no hay que sincronizar nada.
     *
     * Los contadores por hilo ocupan NUM_CUBOS*4 = 384 bytes = 6 lineas de cache
     * exactas, asi que la fila de un hilo nunca comparte linea con la del vecino
     * (false sharing). El alignas lo garantiza para la primera fila.
     */
#if RENDER_AGRUPADO
    SDL_Point* puntos = (SDL_Point*)malloc((size_t)N * MAX_PUNTOS_POR_PARTICULA * sizeof(SDL_Point));
    static _Alignas(64) int cuentaPorHilo[MAX_HILOS][NUM_CUBOS];
    static _Alignas(64) int offsetPorHilo[MAX_HILOS][NUM_CUBOS];

    if (puntos == NULL) {
        fprintf(stderr, "Error al asignar memoria para los puntos de dibujo.\n");
        free(particulas);
        if (texturaFondo) SDL_DestroyTexture(texturaFondo);
        if (texturaCerdito) SDL_DestroyTexture(texturaCerdito);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Sin corridas previas los contadores tienen que estar en cero. */
    for (int t = 0; t < MAX_HILOS; t++) {
        for (int c = 0; c < NUM_CUBOS; c++) cuentaPorHilo[t][c] = 0;
    }
#endif  /* RENDER_AGRUPADO */

    // Tamaño de grupo de partículas por cohete
    int particulasPorCohete = (N < 200) ? 50 : 100;
    int numCohetes = (N + particulasPorCohete - 1) / particulasPorCohete;

    // Inicializar cohetes en ráfagas desfasadas
    for (int c = 0; c < numCohetes; c++) {
        int inicio = c * particulasPorCohete;
        int cantidad = particulasPorCohete;
        if (inicio + cantidad > N) cantidad = N - inicio;

        lanzarCohete(particulas, inicio, cantidad, c % 12);
    }

    printf("Inicializacion completada. N = %d particulas (%d cohetes). Modo %s con %d hilo(s).\n",
           N, numCohetes, modo, hilosUsados);
    if (texturaFondo) printf("Fondo 'fondo.bmp' cargado correctamente.\n");
    if (texturaCerdito) printf("Sprite 'cerdito.bmp' cargado correctamente.\n");

    int corriendo = 1;
    SDL_Event evento;

    Uint32 tiempoAnterior = SDL_GetTicks();
    int contadorFrames = 0;
    char titulo[160];

    /*
     * Instrumentacion: separar la parte que se paraleliza de la que no.
     *   computo = region paralela completa: fisica + relanzamiento + agrupado de puntos
     *   dibujo  = las llamadas a SDL, que siguen en serie por obligacion
     * Esa es exactamente la particion que pide la ley de Amdahl.
     */
    Uint64 frecuencia = SDL_GetPerformanceFrequency();
    double acumComputo = 0.0, acumDibujo = 0.0;     // ventana de 1 segundo
    double totalComputo = 0.0, totalDibujo = 0.0;   // acumulado para el resumen
    long totalFrames = 0;

#if RENDER_AGRUPADO
    // Cuantos hilos formo realmente el equipo. Con N < UMBRAL_PARALELO es 1, y el
    // bucle de dibujo necesita saberlo para recorrer solo las franjas que se usaron.
    int hilosEnUso = 1;
#endif

    // Los primeros segundos no cuentan: todos los cohetes van subiendo y todavia no
    // hay explosiones, asi que el frame es mucho mas barato que el estado estable.
    const Uint32 CALENTAMIENTO_MS = 2000;
    Uint32 tiempoInicio = SDL_GetTicks();
    Uint32 inicioMedicion = 0;

    while (corriendo) {
#if LIMITE_FPS > 0
        Uint32 inicioFrame = SDL_GetTicks();
#endif

        while (SDL_PollEvent(&evento)) {
            if (evento.type == SDL_QUIT || 
               (evento.type == SDL_KEYDOWN && evento.key.keysym.sym == SDLK_ESCAPE)) {
                corriendo = 0;
            }
        }

        Uint64 marcaInicio = SDL_GetPerformanceCounter();

        // --- CERDITOS: se quedan en serie ---
        // Son CANTIDAD_CERDITOS = 10 elementos fijos. Repartir 10 iteraciones entre
        // hilos cuesta mas en sincronizacion que lo que ahorra.
        for (int i = 0; i < CANTIDAD_CERDITOS; i++) {
            actualizarObjetoFlotante(&cerditos[i]);
        }

        // --- COMPUTO EN PARALELO ---
        // Una sola region paralela para los dos bucles: abrir y cerrar el equipo de
        // hilos cuesta decenas de microsegundos, y a 1000+ FPS eso se paga en cada
        // frame. Con un solo #pragma omp parallel se paga una vez y los dos "for"
        // reutilizan el mismo equipo. La barrera implicita entre ellos es necesaria:
        // el relanzamiento tiene que ver las vidas ya actualizadas.
        #pragma omp parallel if(N >= UMBRAL_PARALELO)
        {
            // Fisica: cada particula solo lee y escribe su propia struct. Sin
            // dependencias, sin reducciones, sin secciones criticas.
            #pragma omp for schedule(static)
            for (int i = 0; i < N; i++) {
                actualizarParticula(&particulas[i]);
            }

            // Relanzamiento: la unidad de reparto es el COHETE, no la particula.
            // Cada iteracion toca solo su rango [inicio, inicio+cantidad), asi que
            // los hilos no se pisan. dynamic porque el coste es irregular: con el
            // cohete vivo el break corta enseguida, y solo el que se apago paga el
            // relanzamiento completo.
            #pragma omp for schedule(dynamic, 4)
            for (int c = 0; c < numCohetes; c++) {
                int inicio = c * particulasPorCohete;
                int cantidad = particulasPorCohete;
                if (inicio + cantidad > N) cantidad = N - inicio;

                int coheteActivo = 0;
                for (int i = 0; i < cantidad; i++) {
                    if (particulas[inicio + i].vida > 0.0f) {
                        coheteActivo = 1;
                        break;
                    }
                }

                // Si todas las partículas del cohete se apagaron, lanzar uno nuevo
                if (!coheteActivo) {
                    lanzarCohete(particulas, inicio, cantidad, 0);
                }
            }

#if RENDER_AGRUPADO
            // Agrupado de los puntos a dibujar. Va DESPUES del relanzamiento porque
            // tiene que ver las particulas ya renacidas; la barrera implicita al
            // final del "omp for" de arriba lo garantiza sin trabajo extra.
            //
            // Aca la particion es manual y no con "omp for": cada hilo necesita
            // saber cual es SU rango para escribir en su propia franja del buffer.
            {
                int t = hiloActual();
                int equipo = 1;
#ifdef _OPENMP
                equipo = omp_get_num_threads();
#endif
                // Solo el hilo 0 lo escribe, y nadie lo lee hasta despues de la
                // barrera de cierre de la region. Sin pragma: "omp master" quedo
                // deprecado en OpenMP 5.1 y "masked" no existe en compiladores viejos.
                if (t == 0) hilosEnUso = equipo;

                int lo = (int)((long long)N * t / equipo);
                int hi = (int)((long long)N * (t + 1) / equipo);

                clasificarPuntos(particulas, lo, hi,
                                 puntos + (size_t)lo * MAX_PUNTOS_POR_PARTICULA,
                                 (hi - lo) * MAX_PUNTOS_POR_PARTICULA,
                                 cuentaPorHilo[t], offsetPorHilo[t]);
            }
#endif  /* RENDER_AGRUPADO */
        }

        Uint64 marcaComputo = SDL_GetPerformanceCounter();

        // --- RENDERIZADO POR CAPAS (SIEMPRE EN SERIE) ---
        // SDL_Renderer solo puede usarse desde el hilo que lo creo. Nada de esto se
        // paraleliza, y por eso limita el speedup total (ley de Amdahl).
        if (texturaFondo != NULL) {
            SDL_RenderCopy(renderer, texturaFondo, NULL, NULL);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 60);
            SDL_RenderFillRect(renderer, NULL);
        } else {
            SDL_SetRenderDrawColor(renderer, 5, 5, 15, 255);
            SDL_RenderClear(renderer);
        }

        for (int i = 0; i < CANTIDAD_CERDITOS; i++) {
            dibujarObjetoFlotante(renderer, texturaCerdito, &cerditos[i]);
        }

#if RENDER_AGRUPADO
        // Estelas de los cohetes que suben. Se recorren los COHETES, no las N
        // particulas: el lider siempre es el primero de su grupo, asi que basta con
        // tocar numCohetes structs en vez de releer el arreglo entero en serie.
        for (int c = 0; c < numCohetes; c++) {
            dibujarEstelaCohete(renderer, &particulas[c * particulasPorCohete]);
        }

        // Explosiones: un SDL_RenderDrawPoints por cubo no vacio. Cota superior
        // hilos * NUM_CUBOS llamadas (768 con 8 hilos), contra las ~1.2 millones
        // que costaba dibujar punto por punto.
        for (int t = 0; t < hilosEnUso; t++) {
            int lo = (int)((long long)N * t / hilosEnUso);
            SDL_Point* franja = puntos + (size_t)lo * MAX_PUNTOS_POR_PARTICULA;

            for (int c = 0; c < NUM_CUBOS; c++) {
                int cantidad = cuentaPorHilo[t][c];
                if (cantidad == 0) continue;

                const Uint8* color = PALETAS[c / ALFA_NIVELES];
                Uint8 alfa = (Uint8)((((c % ALFA_NIVELES) + 0.5f) / ALFA_NIVELES) * 255.0f);

                SDL_SetRenderDrawColor(renderer, color[0], color[1], color[2], alfa);
                SDL_RenderDrawPoints(renderer, franja + offsetPorHilo[t][c], cantidad);
            }
        }
#else
        /* Camino original: una pasada sobre las N particulas, punto por punto. */
        for (int i = 0; i < N; i++) {
            dibujarParticulaClasica(renderer, &particulas[i]);
        }
#endif  /* RENDER_AGRUPADO */

        SDL_RenderPresent(renderer);

        Uint64 marcaDibujo = SDL_GetPerformanceCounter();
        double msComputo = (double)(marcaComputo - marcaInicio) * 1000.0 / (double)frecuencia;
        double msDibujo  = (double)(marcaDibujo - marcaComputo) * 1000.0 / (double)frecuencia;
        acumComputo += msComputo;
        acumDibujo  += msDibujo;

#if LIMITE_FPS > 0
        Uint32 tiempoFrame = SDL_GetTicks() - inicioFrame;
        Uint32 msPorFrame = 1000 / LIMITE_FPS;
        if (tiempoFrame < msPorFrame) {
            SDL_Delay(msPorFrame - tiempoFrame);
        }
#endif

        contadorFrames++;
        Uint32 tiempoActual = SDL_GetTicks();

        int midiendo = (tiempoActual - tiempoInicio) >= CALENTAMIENTO_MS;
        if (midiendo) {
            if (inicioMedicion == 0) inicioMedicion = tiempoActual;
            totalComputo += msComputo;
            totalDibujo  += msDibujo;
            totalFrames++;
        }

        if (tiempoActual - tiempoAnterior >= 1000) {
            double fps = contadorFrames / ((tiempoActual - tiempoAnterior) / 1000.0);
            printf("FPS = %7.2f | computo = %7.3f ms | dibujo = %7.3f ms | hilos = %d%s\n",
                   fps,
                   acumComputo / contadorFrames,
                   acumDibujo / contadorFrames,
                   hilosUsados,
                   midiendo ? "" : "   (calentamiento)");
            fflush(stdout);

            snprintf(titulo, sizeof(titulo),
                     "Bad Piggies & Fuegos Artificiales (%s x%d) - N=%d - FPS: %d",
                     modo, hilosUsados, N, (int)fps);
            SDL_SetWindowTitle(window, titulo);

            contadorFrames = 0;
            acumComputo = 0.0;
            acumDibujo = 0.0;
            tiempoAnterior = tiempoActual;
        }

        if (segundosLimite > 0 &&
            (tiempoActual - tiempoInicio) >= (Uint32)segundosLimite * 1000u) {
            corriendo = 0;
        }
    }

    // ---------------- Resumen para el calculo de speedup ----------------
    if (totalFrames > 0) {
        double segundos = (double)(SDL_GetTicks() - inicioMedicion) / 1000.0;
        printf("\n=== RESUMEN ===\n");
        printf("dibujo          : %s\n", RENDER_AGRUPADO ? "agrupado" : "clasico (1 llamada por punto)");
        printf("modo            : %s\n", modo);
        printf("hilos           : %d\n", hilosUsados);
        printf("N               : %d particulas (%d cohetes)\n", N, numCohetes);
        printf("frames medidos  : %ld en %.2f s\n", totalFrames, segundos);
        printf("FPS promedio    : %.2f\n", (double)totalFrames / segundos);
        printf("computo promedio: %.4f ms/frame  (paralelizable)\n", totalComputo / (double)totalFrames);
        printf("dibujo promedio : %.4f ms/frame  (siempre en serie)\n", totalDibujo / (double)totalFrames);
        printf("CSV,%s,%d,%d,%ld,%.4f,%.4f,%.4f\n",
               modo, hilosUsados, N, totalFrames,
               (double)totalFrames / segundos,
               totalComputo / (double)totalFrames,
               totalDibujo / (double)totalFrames);
        fflush(stdout);
    }

#if RENDER_AGRUPADO
    free(puntos);
#endif
    free(particulas);
    if (texturaFondo) SDL_DestroyTexture(texturaFondo);
    if (texturaCerdito) SDL_DestroyTexture(texturaCerdito);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}