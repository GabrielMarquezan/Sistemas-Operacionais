#ifndef FILA_PRIORIDADE_H
#define FILA_PRIORIDADE_H

#include <stdbool.h> 
#include <stdlib.h> 
#include "processo.h"

typedef struct Fila_Prioridade fila_prioridade;

fila_prioridade* fila_cria(int capacidade_max);
void fila_destroi(fila_prioridade* heap);
bool inserir(fila_prioridade* heap, processo_t* proc);
processo_t* remover(fila_prioridade* heap);
processo_t* topo(fila_prioridade* heap);
int quantidade_elementos(fila_prioridade* heap);
bool fila_remove_proc_pid(fila_prioridade* heap, processo_t* proc_alvo);

#endif