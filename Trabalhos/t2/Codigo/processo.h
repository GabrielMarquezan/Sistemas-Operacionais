#ifndef PROCESSOS_H
#define PROCESSOS_H

#include <stdbool.h>

#define MAX_PROC 100
#define QUANTUM 20

#include "lista_processos.h"

typedef enum estadoProcesso estado_t;

typedef struct processo_t processo_t;
struct processo_t {
    int pid; //número do processo
    int parentPID; //id do processo pai
    lista_t* proc_filhos; // Árvore de filhos
    int num_filhos;
    // estado da CPU
    int regPC, regA, regX, regERRO;
    estado_t estadoCorrente;
    int pIniMemoria;
    float prioridade;
    int quantum;
    //memória
    // entrada/saida
    int terminal;
    bool esperando_leitura;
    bool esperando_escrita;
    int esperando_processo;
};

enum estadoProcesso {
    PRONTO,
    BLOQUEADO,
    EXECUTANDO
};

static int prox_pid = 2;

processo_t* busca_proc_na_tabela(processo_t** tabela, int pid);
processo_t* cria_processo(processo_t* processoPai);
int mata_processo(processo_t* proc, processo_t** tabela);
processo_t** cria_vetor_processos();

#endif