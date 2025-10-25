#include <stdlib.h>
#include "cpu.h"
#include "processo.h"

typedef struct processo_t processo_t;

processo_t* cria_processo(processo_t* processoPai) {
    processo_t* proc = malloc(sizeof(processo_t));
    if (proc == NULL) return NULL;
    
    proc->estadoCorrente = PRONTO;

    if (processoPai == NULL) {
        proc->parentPID = 0; //id do INIT
        proc->pid = 1;
    }
    else{
        lista_t* novo_filho = malloc(sizeof(lista_t));
        novo_filho->processo = proc;
        novo_filho->proximo = NULL;
        
        // Se o pai não tem filhos ainda, adiciona esse
        // Se já tiver, percorre até o fim da lista e insere
        if(processoPai->proc_filhos == NULL) processoPai->proc_filhos = novo_filho;
        else {
            lista_t* no_atual = processoPai->proc_filhos;
            lista_t* no_anterior = no_atual;
            while(no_atual != NULL) {
                no_anterior = no_atual;
                no_atual = no_atual->proximo;
            }

            no_anterior->proximo = novo_filho;
        }
        
        processoPai->num_filhos++;
        proc->parentPID = processoPai->pid;
    }
    
    proc->num_filhos = 0;
    proc->proc_filhos = NULL;
    proc->prioridade = QUANTUM;
    proc->regERRO = ERR_OK;
    proc->regA = 0;
    proc->regX = 0;
    proc->pid = prox_pid++;
    proc->esperando_escrita = false;
    proc->esperando_escrita = false;
    proc->esperando_processo = 0;
    proc->terminal = (proc->pid%4) * 4; // 0 = TERM_A, 1 = TERM_B
    if (processoPai == NULL){
        proc->parentPID = 0; //id do INIT
    } else proc->parentPID = processoPai->pid;

    return proc;
}

processo_t** cria_vetor_processos() {
    processo_t** vet_proc = (processo_t**) malloc(MAX_PROC * sizeof(processo_t*)); 
    for (int i = 0; i< MAX_PROC; i++) {
        vet_proc[i] = NULL;
    }
    return vet_proc;
}

int mata_processo(processo_t* proc, processo_t** tabela) {
    //Mata um processo e recursivamente mata seus filhos em cascata
    if (proc == NULL) return 0;

    int processos_mortos_total = 0;
    lista_t* no_atual = proc->proc_filhos;

    // Itera sobre lista de filhos do processo principal
    while (no_atual != NULL) {
        processo_t* filho_a_matar = no_atual->processo;
        lista_t* proximo_no = no_atual->proximo;

        // Mata os filhos dos filhos recursivamente
        processos_mortos_total += mata_processo(filho_a_matar, tabela);
        free(no_atual); 
        no_atual = proximo_no;
    }
    
    for(int i = 0; i < MAX_PROC; i++) {
        if(tabela[i] == proc) {
            tabela[i] = NULL;
            break; 
        }
    }

    free(proc);
    return processos_mortos_total + 1;
}

