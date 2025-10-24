#ifndef PROCESSOS_H
#define PROCESSOS_H

#define MAX_PROC 100
#define QUANTUM 20

typedef enum estadoProcesso estado_p;

typedef struct processo_t processo_t;
struct processo_t {
    int pid; //número do processo
    int parentPID; //id do processo pai
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

processo_t** processos_cria();

#endif