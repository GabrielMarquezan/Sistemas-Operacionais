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
        processoPai->proc_filhos = insere(processoPai->proc_filhos, proc);
        processoPai->num_filhos++;
        proc->parentPID = processoPai->pid;
        proc->pid = prox_pid++;
    }
    
    proc->num_filhos = 0;
    proc->proc_filhos = NULL;
    proc->prioridade = 0.5;
    proc->quantum = QUANTUM;
    proc->regERRO = ERR_OK;
    proc->regA = 0;
    proc->regX = 0;
    proc->esperando_escrita = false;
    proc->esperando_escrita = false;
    proc->esperando_processo = 0;
    proc->terminal = (proc->pid % 4) * 4; // 0 = TERM_A, 1 = TERM_B

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
