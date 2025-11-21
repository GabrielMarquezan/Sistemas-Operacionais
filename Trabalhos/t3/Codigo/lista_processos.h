#ifndef LISTA_H
#define LISTA_H
#include "processo.h"

typedef struct lista lista_t;

struct lista {
    lista_t* proximo;
    processo_t* processo;
};

lista_t* lista_cria();

lista_t* insere(lista_t* lista, processo_t* proc);

lista_t* remove_lista(lista_t* lista, processo_t* proc);

void lista_libera(lista_t* lista);

bool lista_vazia(lista_t* lista);

processo_t* busca_proc_pronto(lista_t* lista);

#endif