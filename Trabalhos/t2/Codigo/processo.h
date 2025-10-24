#ifndef PROCESSOS_H
#define PROCESSOS_H

#define MAX_PROC 100
#define QUANTUM 20

#include "lista_processos.h"

typedef enum estadoProcesso estado_p;

typedef struct processo_t processo_t;
struct processo_t {
    int pid; //número do processo
    int parentPID; //id do processo pai
    lista_t* proc_filhos; // Árvore de filhos
    int num_filhos;
    // estado da CPU
    int regPC, regA, regX, regERRO;
    estado_p estadoCorrente;
    int pIniMemoria;
    int prioridade;
    int quantum;
    //memória
};

enum estadoProcesso {
    PRONTO,
    BLOQUEADO,
    EXECUTANDO
};

static int prox_pid = 1;

processo_t* cria_processo(processo_t* processoPai);
int mata_processo(processo_t* proc, processo_t** tabela);
processo_t** cria_vetor_processos();

#endif