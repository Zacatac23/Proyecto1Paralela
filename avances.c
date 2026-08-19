/*
 * Screensaver Fuegos Artificiales 100% SECUENCIAL - Proyecto 1 Computacion Paralela y Distribuida
 * UVG - 2024
 *
 * IMPORTANTE: Este codigo es 100% SECUENCIAL (monohilo / Single-Threaded).
 * NO utiliza OpenMP, Pthreads ni librerias de paralelismo.
 * Ejecuta todas las iteraciones en 1 solo núcleo de CPU para servir como LINEA BASE (Baseline).
 *
 * Uso: ./avances_c.exe <N>
 *   N = cantidad de particulas a procesar secuencialmente
 *
 * Compilacion:
 *   gcc -O2 -std=c11 avances.c -o avances_c.exe -lmingw32 -lSDL2main -lSDL2 -lm
 */

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------- Parametros y constantes ----------------------
#define ANCHO 800            /* Ancho del canvas */
#define ALTO  600            /* Alto del canvas */

#define GRAVEDAD 0.025f       /* Fuerza de gravedad */
#define VELOCIDAD_MIN 0.4f    /* Velocidad inicial minima */
#define VELOCIDAD_MAX 2.2f    /* Velocidad inicial maxima */
#define RESISTENCIA_AIRE 0.985f
#define LIMITE_FPS 60         /* Limite de FPS (0 para desbloquear FPS y medir rendimiento libre) */

// ---------------------- Estructura de Particula ----------------------
typedef struct {
    float x, y;          // Posicion actual
    float vx, vy;        // Velocidad en X y Y
    Uint8 r, g, b;       // Color RGB
    float vida;          // Vida restante (1.0f -> 0.0f)
    float decaimiento;   // Tasa de desvanecimiento por frame
    float origenX, origenY; // Centro de la explosion
} Particula;

// Inicializa o reinicia una particula de forma independiente
void respawnParticula(Particula* p) {
    // Si no tiene centro asignado o renace, se define un origen de explosion
    if (p->vida <= 0.0f || p->origenX == 0.0f) {
        p->origenX = 100.0f + (float)(rand() % (ANCHO - 200));
        p->origenY = 60.0f + (float)(rand() % (ALTO / 2));
        
        int paleta = rand() % 5;
        switch (paleta) {
            case 0: p->r = 255; p->g = 50;  p->b = 50;  break; // Rojo
            case 1: p->r = 50;  p->g = 220; p->b = 255; break; // Cyan
            case 2: p->r = 255; p->g = 215; p->b = 0;   break; // Dorado
            case 3: p->r = 220; p->g = 50;  p->b = 255; break; // Magenta
            default:p->r = 50;  p->g = 255; p->b = 100; break; // Verde
        }
    }

    p->x = p->origenX;
    p->y = p->origenY;

    float angulo = ((float)rand() / RAND_MAX) * 2.0f * (float)M_PI;
    float velocidad = VELOCIDAD_MIN + ((float)rand() / RAND_MAX) * (VELOCIDAD_MAX - VELOCIDAD_MIN);

    p->vx = cosf(angulo) * velocidad;
    p->vy = sinf(angulo) * velocidad;

    p->vida = 1.0f;
    p->decaimiento = 0.003f + ((float)rand() / RAND_MAX) * 0.006f;
}

// Actualiza la fisica de 1 particula (Ejecucion monohilo secuencial)
void actualizarParticula(Particula* p) {
    if (p->vida <= 0.0f) {
        respawnParticula(p);
        return;
    }

    // Calculos de posicion y velocidad secuenciales
    p->x += p->vx;
    p->y += p->vy;
    p->vy += GRAVEDAD;
    p->vx *= RESISTENCIA_AIRE;
    p->vy *= RESISTENCIA_AIRE;

    p->vida -= p->decaimiento;
}

// Renderiza 1 particula en pantalla
void dibujarParticula(SDL_Renderer* renderer, const Particula* p) {
    if (p->vida <= 0.0f) return;

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

int main(int argc, char* argv[]) {

    // ---------------- Captura y validacion de argumentos ----------------
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

    // ---------------- Inicializacion de SDL ----------------
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Error al inicializar SDL: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Screensaver Fuegos Artificiales (100% SECUENCIAL Monohilo) - FPS: 0",
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

    // ---------------- Reserva de memoria secuencial para N particulas ----------------
    Particula* particulas = (Particula*)malloc(N * sizeof(Particula));
    if (particulas == NULL) {
        fprintf(stderr, "Error al asignar memoria para %d particulas.\n", N);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    for (int i = 0; i < N; i++) {
        particulas[i].vida = 0.0f;
        particulas[i].origenX = 0.0f;
        particulas[i].origenY = 0.0f;
        respawnParticula(&particulas[i]);
        particulas[i].vida = ((float)rand() / RAND_MAX); // Vida inicial aleatoria
    }

    printf("Inicializacion completada. N = %d particulas (Ejecución 100%% SECUENCIAL Monohilo).\n", N);

    // ---------------- Loop principal (1 solo hilo de CPU) ----------------
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

        // --- PROCESAMIENTO SECUENCIAL (Iteracion una por una en 1 hilo) ---
        for (int i = 0; i < N; i++) {
            actualizarParticula(&particulas[i]);
        }

        // --- RENDERIZADO SECUENCIAL ---
        SDL_SetRenderDrawColor(renderer, 5, 5, 15, 60);
        SDL_RenderFillRect(renderer, NULL);

        for (int i = 0; i < N; i++) {
            dibujarParticula(renderer, &particulas[i]);
        }

        SDL_RenderPresent(renderer);

        // --- CONTROL OPTATIVO DE FPS ---
#if LIMITE_FPS > 0
        Uint32 tiempoFrame = SDL_GetTicks() - inicioFrame;
        Uint32 msPorFrame = 1000 / LIMITE_FPS;
        if (tiempoFrame < msPorFrame) {
            SDL_Delay(msPorFrame - tiempoFrame);
        }
#endif

        // --- MEDICION DE FPS ---
        contadorFrames++;
        Uint32 tiempoActual = SDL_GetTicks();
        if (tiempoActual - tiempoAnterior >= 1000) {
            double fps = contadorFrames / ((tiempoActual - tiempoAnterior) / 1000.0);
            printf("FPS = %.2f\n", fps);

            snprintf(titulo, sizeof(titulo), "Screensaver Fuegos Artificiales (SECUENCIAL Monohilo) - N=%d - FPS: %d", N, (int)fps);
            SDL_SetWindowTitle(window, titulo);

            contadorFrames = 0;
            tiempoAnterior = tiempoActual;
        }
    }

    // ---------------- Liberacion de recursos ----------------
    free(particulas);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}