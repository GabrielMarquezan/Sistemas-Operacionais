#include <stdlib.h>
#include "cpu.h"
#include "processo.h"
#include "lista_processos.h"

typedef struct processo_t processo_t;


static int prox_pid = 2;

processo_t* cria_processo(processo_t* processoPai) {
    processo_t* proc = malloc(sizeof(processo_t));
    if (proc == NULL) return NULL;
    
    proc->estadoCorrente = PRONTO;

    if (processoPai == NULL) {
        proc->parentPID = 1; //id do INIT
        proc->pid = 1;
    }
    else{
        proc->parentPID = processoPai->pid;
        proc->pid = prox_pid++;
    }
    proc->prioridade = 0.5;
    proc->quantum = QUANTUM;
    proc->regERRO = ERR_OK;
    proc->regA = 0;
    proc->regX = 0;
    proc->esperando_escrita = false;
    proc->esperando_escrita = false;
    proc->esperando_processo = 0;
    proc->terminal = ((proc->pid - 1) % 4) * 4; // 0 = TERM_A, 1 = TERM_B
    proc->contadorBloqueado = 0;
    proc->contadorExecutando = 0;
    proc->contadorPronto = 1;

    return proc;
}

processo_t** cria_vetor_processos() {
    processo_t** vet_proc = (processo_t**) malloc(MAX_PROC * sizeof(processo_t*)); 
    for (int i = 0; i< MAX_PROC; i++) {
        vet_proc[i] = NULL;
    }
    return vet_proc;
}

processo_t* busca_proc_na_tabela(processo_t** tabela, int pid) {
    for(int i = 0; i < MAX_PROC; i++) {
        if(tabela[i] != NULL && tabela[i]->pid == pid) return tabela[i];
    }

    return NULL;
}

// insere na tabela

processo_t* busca_remove_proc_tabela(processo_t** tabela, int pid) {
    for(int i = 0; i < MAX_PROC; i++) {
        if(tabela[i] != NULL && tabela[i]->pid == pid) {
            processo_t* proc = tabela[i];
            tabela[i] = NULL;
            return proc;
        }
    }

    return NULL;
}
