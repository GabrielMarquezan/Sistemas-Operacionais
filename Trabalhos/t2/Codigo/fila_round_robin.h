#ifndef FILA_RR_H
#define FILA_RR_H

#include <stdbool.h>
#include <stdlib.h>
#include "processo.h"

typedef struct Fila {
    processo_t* processo;
    struct Fila* proximo;
} node_fila;

// ESTRUTURA CONTROLADORA
typedef struct Fila_rr {
    node_fila* inicio;
    node_fila* fim;
} fila_rr;

fila_rr* fila_rr_cria(void);
void fila_rr_destroi(fila_rr *fila);
void fila_rr_insere_fim(fila_rr *fila, processo_t *proc);
processo_t* fila_rr_remove_inicio(fila_rr *fila);
bool fila_rr_esta_vazia(fila_rr *fila);
void fila_rr_remove_pid(fila_rr* fila, int pid);

#endif