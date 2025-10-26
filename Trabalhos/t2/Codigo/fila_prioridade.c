#include "processo.h"
#include "fila_prioridade.h"
#include <stdlib.h>
#include <stdbool.h>

#define MAX_CAP 100000

//É mais fácil coordenar as filas se só forem inseridos processos PRONTOS
//Para usar isso aq tudo, só precisa o "cria", "destrói", "insere", "remove" e o "topo", no máximo

typedef struct Fila_Prioridade {
    int qtd_elementos;
    int capacidade;
    processo_t** arr;
} fila_prioridade;

fila_prioridade* fila_cria(int capacidade_max){
    fila_prioridade* heap = (fila_prioridade*) malloc(sizeof(fila_prioridade));
    heap->qtd_elementos = 0;
    //como um heap começa no índice 1, é preciso adicionar 1 na capacidade;
    heap->capacidade = capacidade_max+1;
    heap->arr = (processo_t**) malloc(sizeof(processo_t)*(capacidade_max+1));
    return heap;
}

void fila_destroi(fila_prioridade* heap){
    free(heap->arr);
    free(heap);
}

static void troca(fila_prioridade* heap, int a, int b){
    processo_t* p1 = heap->arr[a];
    heap->arr[a] = heap->arr[b];
    heap->arr[b] = p1;
}

void bubble_down(fila_prioridade* heap, int pai){
    int n = heap->qtd_elementos;
    int filho_esq = 2*pai;

    if (filho_esq >= n) return;
    
    int filho_dir = 2*pai + 1;
    int maior_filho = filho_esq;

    if (maior_filho < n && heap->arr[filho_esq]->quantum < heap->arr[filho_dir]->quantum){
        maior_filho = filho_dir;
    }
    if (maior_filho <= n && heap->arr[maior_filho]->quantum > heap->arr[pai]->quantum){
        troca(heap, maior_filho, pai);
        bubble_down(heap, maior_filho);
    }

}

void bubble_up(fila_prioridade* heap, int atual){
    int n = heap->qtd_elementos;
    if (atual <= 1) return;
    int pai = atual/2;
    if (heap->arr[atual]->quantum > heap->arr[pai]->quantum){
        troca(heap, atual, pai);
        bubble_up(heap, pai);
    }
}

bool inserir(fila_prioridade* heap, processo_t* proc){
    int posicao = heap->qtd_elementos + 1;
    if (posicao > heap->capacidade) return false; //erro ao inserir
    heap->arr[posicao] = proc;
    heap->qtd_elementos++;
    bubble_up(heap, posicao);
    return true;
}

//remove o processo com maior prioridade
processo_t* remover(fila_prioridade* heap){
    if (heap->qtd_elementos < 1) return NULL; //não há processos na fila
    processo_t* temp = heap->arr[1]; // pega sempre o processo com prioridade mais alta
    heap->arr[1] = heap->arr[heap->qtd_elementos];
    heap->arr[heap->qtd_elementos] = NULL;
    heap->qtd_elementos--;
    bubble_down(heap, 1);
    return temp;
}

//olha o topo do heap (não remove)
processo_t* topo(fila_prioridade* heap){
    if (heap->qtd_elementos >=1) return heap->arr[1];
    else return NULL;
}