#ifndef GRAFO_H
#define GRAFO_H

#include "plan.h"

// Guarda posiciones de actividades dentro del plan
typedef struct {
    size_t *datos;
    size_t cantidad;
    size_t capacidad;
} ListaIndices;

// Cada nodo conoce sus requisitos y las actividades que dependen de el
typedef struct {
    ListaIndices dependencias;
    ListaIndices siguientes;
} Nodo;

typedef struct {
    Nodo *nodos;
    size_t cantidad;
} Grafo;

int construir_grafo(const Plan *plan, Grafo *grafo);
void liberar_grafo(Grafo *grafo);

#endif
