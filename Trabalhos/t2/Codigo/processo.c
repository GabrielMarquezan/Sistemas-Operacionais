#include <stdlib.h>
#include "cpu.h"
#include "processo.h"

typedef struct processo_t processo_t;

processo_t* cria_processo(processo_t* processoPai, int pid) {
    processo_t* proc = malloc(sizeof(processo_t));
    if (proc == NULL) return NULL;
    proc->estadoCorrente = PRONTO;
    if (processoPai = NULL){
        proc->parentPID = 1; //id do INIT
    } else proc->parentPID = processoPai->pid;
    proc->prioridade = QUANTUM;
    proc->regERRO = ERR_OK;
    proc->regA = 0;
    proc->regX = 0;

    return proc;
}

