#include "fila_round_robin.h"

fila_rr* fila_rr_cria(void)
{
    // Aloca o controlador da fila
    fila_rr* fila = malloc(sizeof(fila_rr));
    if (fila == NULL) {
        free(fila);
        exit(1);
    }
    
    fila->inicio = NULL;
    fila->fim = NULL;
    return fila;
}

bool fila_rr_esta_vazia(fila_rr *fila) {
    return (fila == NULL || fila->inicio == NULL);
}

void fila_rr_insere_fim(fila_rr *fila, processo_t *proc) {
    if (fila == NULL || proc == NULL) return;

    node_fila* novo_no = malloc(sizeof(node_fila));
    if (novo_no == NULL) return;

    novo_no->processo = proc;
    novo_no->proximo = NULL;

    // Insere na fila
    if (fila_rr_esta_vazia(fila)) {
        // Fila estava vazia, então o novo nó é o início e o fim
        fila->inicio = novo_no;
        fila->fim = novo_no;
    } else {
        fila->fim->proximo = novo_no; // O fim antigo aponta para o novo
        fila->fim = novo_no;         
    }
}

processo_t* fila_rr_remove_inicio(fila_rr *fila)
{
    if (fila_rr_esta_vazia(fila)) return NULL;

    // Salva o nó do início e o processo
    node_fila* no_removido = fila->inicio;
    processo_t* proc_retorno = no_removido->processo;

    // Avança o nó inicio
    fila->inicio = no_removido->proximo;

    // Verifica se a fila ficou vazia
    if (fila->inicio == NULL) fila->fim = NULL;

    free(no_removido);
    return proc_retorno;
}

void fila_rr_remove_pid(fila_rr* fila, int pid) {
    if (fila_rr_esta_vazia(fila)) return;
    node_fila* no = fila->inicio;
    node_fila* anterior = NULL;
    while (no != NULL && no->processo->pid != pid) {
        anterior = no;
        no = no->proximo;
    }
    if (no == NULL ){
        return;
    }
    if (anterior == NULL) {
        fila_rr_remove_inicio(fila);
    }
    anterior->proximo = no->proximo;
    free(no);
}

void fila_rr_destroi(fila_rr *fila)
{
    if (fila == NULL) return;

    while (!fila_rr_esta_vazia(fila)) fila_rr_remove_inicio(fila);

    free(fila);
}