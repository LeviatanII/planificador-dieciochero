#ifndef EJECUTOR_H
#define EJECUTOR_H

#include "grafo.h"

// Devuelve 1 si termina todo el plan y 0 si ocurre un error
int ejecutar_plan(const Plan *plan, const Grafo *grafo, int k);

#endif
