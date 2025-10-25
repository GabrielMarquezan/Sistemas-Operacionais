#ifndef PROCESSOS_H
#define PROCESSOS_H

#include <stdbool.h>

#define MAX_PROC 100
#define QUANTUM 20

typedef enum estadoProcesso estado_t;

typedef struct processo_t processo_t;
struct processo_t {
    int pid; //número do processo
    int parentPID; //id do processo pai
    // estado da CPU
    int regPC, regA, regX, regERRO;
    estado_t estadoCorrente;
    int pIniMemoria;
    int prioridade;
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

processo_t* cria_processo(processo_t* processoPai);

processo_t** processos_cria();

#endif