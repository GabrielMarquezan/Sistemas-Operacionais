#ifndef PROCESSOS_H
#define PROCESSOS_H

#define MAX_PROC 100

typedef enum estadoProcesso estado_p;

typedef struct processos_t processos_t;
struct processos_t {
    int pid; //número do processo
    int parentPID; //id do processo pai
    // estado da CPU
    int regPC, regA, regX, regERRO;
    estado_p estadoCorrente;
    int pIniMemoria;
    int prioridade;
    //memória
};

enum estadoProcesso {
    PRONTO,
    BLOQUEADO,
    EXECUTANDO
};


#endif