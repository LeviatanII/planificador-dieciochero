#ifndef PLAN_H
#define PLAN_H

#include <stddef.h>

typedef struct {
    char *id;
    char *nombre;
    int tiempo_ms;
    char *dependencias;
} Actividad;

typedef struct {
    Actividad *actividades;
    size_t cantidad;
    size_t capacidad;
} Plan;

int cargar_plan(const char *ruta, Plan *plan);
void mostrar_plan(const Plan *plan);
void liberar_plan(Plan *plan);

#endif
