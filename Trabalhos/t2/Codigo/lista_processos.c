#include <stdlib.h>
#include <stdbool.h>
#include "lista_processos.h"

lista_t* lista_cria() {
    return NULL;
}

//insere sempre no início
lista_t* insere(lista_t* lista, processo_t* proc) {
    lista_t* novo = (lista_t*) malloc(sizeof(lista_t));
    //se o malloc falha, simplesmente não insere o processo
    if (novo == NULL) return lista;
    novo->proximo = lista;
    novo->processo = proc;
    return novo;
}

lista_t* remove(lista_t* lista, processo_t* proc) {
    lista_t* anterior = NULL;
    lista_t* temp = lista;
    while (temp != NULL && temp->processo->pid != proc->pid) {
        anterior = temp;
        temp = temp->proximo;
    }
    if (temp == NULL) {
        return lista;
    }
    if (anterior == NULL) {
        return lista->proximo;
    } else {
        anterior->proximo = temp->proximo;
    }
    free(temp);
    return lista;
}

bool lista_vazia(lista_t* lista){
    return lista == NULL || lista->processo == NULL;
}

void lista_libera(lista_t* lista) {
    lista_t* lider = lista;
    while (lider->proximo != NULL){
        lider = lider->proximo;
        free(lista);
        lista = lider;
    }
    free(lista);
}

processo_t* busca_proc_pronto(lista_t* lista) {
    lista_t* temp = lista;
    while (temp != NULL && temp->processo->estadoCorrente != PRONTO) {
        temp = temp->proximo;
    }
    if (temp == NULL) return NULL;
    else return temp->processo;
}

int qdt_processos(lista_t* lista){
    if (lista_vazia(lista)) return 0;
    lista_t* temp = lista;
    int cont = 0;
    while (temp != NULL){
        temp = temp->proximo;
        cont++;
    }
    return cont;
}