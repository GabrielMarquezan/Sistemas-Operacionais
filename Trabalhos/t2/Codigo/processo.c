#include <stdlib.h>
#include <stdbool.h>
#include "cpu.h"
#include "processo.h"

typedef struct processo_t processo_t;

processo_t* cria_processo(processo_t* processoPai) {
    processo_t* proc = malloc(sizeof(processo_t));
    if (proc == NULL) return NULL;
    proc->estadoCorrente = PRONTO;
    if (processoPai == NULL){
        proc->parentPID = 1; //id do INIT
    } else proc->parentPID = processoPai->pid;
    proc->prioridade = QUANTUM;
    proc->regERRO = ERR_OK;
    proc->regA = 0;
    proc->regX = 0;
    proc->pid = prox_pid++;

    return proc;
}

processo_t** processos_cria() {
    processo_t** vet_proc = (processo_t**) malloc(MAX_PROC * sizeof(processo_t*)); 
    for (int i = 0; i< MAX_PROC; i++) {
        vet_proc[i] = NULL;
    }
    return vet_proc;
}

bool mata_processo(processo_t* proc) {

}

