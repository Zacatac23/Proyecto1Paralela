/*
 * Screensaver Fuegos Artificiales 100% SECUENCIAL - Proyecto 1 Computación Paralela y Distribuida
 * UVG - 2024
 *
 * IMPORTANTE: Este código es 100% SECUENCIAL (monohilo / Single-Threaded).
 * NO utiliza OpenMP, Pthreads ni librerías de paralelismo.
 * Ejecuta todas las iteraciones en 1 solo núcleo de CPU para servir como LÍNEA BASE (Baseline).
 *
 * Incluye soporte para:
 *   - Cohetes de Fuegos Artificiales: Despegan desde abajo con estela y estallan en el cielo.
 *   - Imagen de Fondo (Bad Piggies: fondo.bmp)
 *   - Objetos Flotantes con Imagen (Cerditos verdes: cerdito.bmp)
 *
 * Uso: ./avancesc.exe <N>
 *   N = cantidad de partículas a procesar secuencialmente
 *
 * Compilación:
 *   gcc -O2 -std=c11 avances.c -o avancesc.exe -lmingw32 -lSDL2main -lSDL2 -lm
 */

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------- Parámetros y constantes ----------------------
#define ANCHO 800            /* Ancho del canvas */
#define ALTO  600            /* Alto del canvas */

#define GRAVEDAD 0.025f       /* Fuerza de gravedad */
#define VELOCIDAD_MIN 0.4f    /* Velocidad inicial mínima de explosión */
#define VELOCIDAD_MAX 2.4f    /* Velocidad inicial máxima de explosión */
#define RESISTENCIA_AIRE 0.985f
#define LIMITE_FPS 60         /* Límite de FPS (0 para desbloquear FPS y medir rendimiento libre) */

#define CANTIDAD_CERDITOS 10   /* Número de cerditos flotantes en pantalla */

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
    float origenX = 80.0f + (float)(rand() % (ANCHO - 160));
    float alturaExplosion = 60.0f + (float)(rand() % (ALTO / 2 + 50));
    float velSubida = 5.5f + ((float)rand() / RAND_MAX) * 2.5f;

    Uint8 r, g, b;
    int paleta = rand() % 6;
    switch (paleta) {
        case 0: r = 255; g = 50;  b = 50;  break; // Rojo
        case 1: r = 50;  g = 220; b = 255; break; // Cyan
        case 2: r = 255; g = 215; b = 0;   break; // Dorado
        case 3: r = 220; g = 50;  b = 255; break; // Magenta
        case 4: r = 255; g = 140; b = 0;   break; // Naranja
        default:r = 50;  g = 255; b = 100; break; // Verde
    }

    for (int i = 0; i < cantidad; i++) {
        int idx = inicioIndex + i;
        Particula* p = &particulas[idx];
        p->x = origenX;
        p->y = (float)(ALTO + (retrasoGradual * 6)); // Posición de despegue escalonada
        p->origenX = origenX;
        p->origenY = alturaExplosion;
        p->alturaExplosion = alturaExplosion;
        p->vy = -velSubida;
        p->vx = ((float)rand() / RAND_MAX - 0.5f) * 0.3f; // Pequeño desvío horizontal al subir
        p->r = r;
        p->g = g;
        p->b = b;
        p->vida = 1.0f;
        p->decaimiento = 0.003f + ((float)rand() / RAND_MAX) * 0.006f;
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

            float angulo = ((float)rand() / RAND_MAX) * 2.0f * (float)M_PI;
            float velocidad = VELOCIDAD_MIN + ((float)rand() / RAND_MAX) * (VELOCIDAD_MAX - VELOCIDAD_MIN);

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

// Renderiza 1 partícula en pantalla
void dibujarParticula(SDL_Renderer* renderer, const Particula* p) {
    if (p->vida <= 0.0f) return;

    if (p->etapa == ETAPA_SUBIENDO) {
        // Dibuja la estela luminosa ascendente del cohete
        if (p->esLiderCohete && p->y < ALTO) {
            int px = (int)p->x;
            int py = (int)p->y;

            SDL_SetRenderDrawColor(renderer, 255, 230, 150, 255);
            SDL_RenderDrawLine(renderer, px, py, px, py + 14);
            SDL_SetRenderDrawColor(renderer, 255, 120, 30, 200);
            SDL_RenderDrawLine(renderer, px - 1, py + 4, px + 1, py + 16);
        }
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

// Inicializa los cerditos flotantes
void inicializarObjetoFlotante(ObjetoFlotante* obj, int index) {
    obj->x = (float)(rand() % (ANCHO - 100) + 50);
    obj->y = (float)(rand() % (ALTO - 200) + 50);
    obj->vx = ((float)(rand() % 100) / 100.0f - 0.5f) * 1.2f;
    obj->vy = ((float)(rand() % 100) / 100.0f - 0.5f) * 0.8f;
    if (fabsf(obj->vx) < 0.2f) obj->vx = 0.4f;
    obj->angulo = ((float)rand() / RAND_MAX) * 360.0f;
    obj->velocidadAng = ((float)(rand() % 100) / 100.0f - 0.5f) * 1.5f;
    obj->escala = 0.35f + ((float)(rand() % 100) / 100.0f) * 0.25f;
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
        fprintf(stderr, "Uso: %s <N>\n", argv[0]);
        fprintf(stderr, "  N = cantidad de particulas a procesar secuencialmente\n");
        return 1;
    }

    int N = atoi(argv[1]);
    if (N <= 0) {
        fprintf(stderr, "Error: N debe ser un entero positivo. Recibido: %s\n", argv[1]);
        return 1;
    }

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
    srand((unsigned int)time(NULL));

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

    printf("Inicializacion completada. N = %d particulas (%d cohetes) (Ejecución 100%% SECUENCIAL Monohilo).\n", N, numCohetes);
    if (texturaFondo) printf("Fondo 'fondo.bmp' cargado correctamente.\n");
    if (texturaCerdito) printf("Sprite 'cerdito.bmp' cargado correctamente.\n");

    int corriendo = 1;
    SDL_Event evento;

    Uint32 tiempoAnterior = SDL_GetTicks();
    int contadorFrames = 0;
    char titulo[128];

    while (corriendo) {
        Uint32 inicioFrame = SDL_GetTicks();

        while (SDL_PollEvent(&evento)) {
            if (evento.type == SDL_QUIT || 
               (evento.type == SDL_KEYDOWN && evento.key.keysym.sym == SDLK_ESCAPE)) {
                corriendo = 0;
            }
        }

        // --- PROCESAMIENTO SECUENCIAL (Cerditos) ---
        for (int i = 0; i < CANTIDAD_CERDITOS; i++) {
            actualizarObjetoFlotante(&cerditos[i]);
        }

        // --- PROCESAMIENTO SECUENCIAL (Físicas de fuegos artificiales) ---
        for (int i = 0; i < N; i++) {
            actualizarParticula(&particulas[i]);
        }

        // --- VERIFICACIÓN DE RE-LANZAMIENTO DE COHETES ---
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

        // --- RENDERIZADO POR CAPAS ---
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

        for (int i = 0; i < N; i++) {
            dibujarParticula(renderer, &particulas[i]);
        }

        SDL_RenderPresent(renderer);

#if LIMITE_FPS > 0
        Uint32 tiempoFrame = SDL_GetTicks() - inicioFrame;
        Uint32 msPorFrame = 1000 / LIMITE_FPS;
        if (tiempoFrame < msPorFrame) {
            SDL_Delay(msPorFrame - tiempoFrame);
        }
#endif

        contadorFrames++;
        Uint32 tiempoActual = SDL_GetTicks();
        if (tiempoActual - tiempoAnterior >= 1000) {
            double fps = contadorFrames / ((tiempoActual - tiempoAnterior) / 1000.0);
            printf("FPS = %.2f\n", fps);

            snprintf(titulo, sizeof(titulo), "Bad Piggies & Fuegos Artificiales (SECUENCIAL) - N=%d - FPS: %d", N, (int)fps);
            SDL_SetWindowTitle(window, titulo);

            contadorFrames = 0;
            tiempoAnterior = tiempoActual;
        }
    }

    free(particulas);
    if (texturaFondo) SDL_DestroyTexture(texturaFondo);
    if (texturaCerdito) SDL_DestroyTexture(texturaCerdito);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}