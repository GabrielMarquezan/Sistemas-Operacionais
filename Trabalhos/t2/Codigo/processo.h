#ifndef PROCESSOS_H
#define PROCESSOS_H

#include <stdbool.h>

#define MAX_PROC 100
#define QUANTUM 20

typedef enum estadoProcesso estado_t;
typedef struct processo_t processo_t;
typedef struct lista lista_t;

enum estadoProcesso {
    PRONTO,
    BLOQUEADO,
    EXECUTANDO,
    FINALIZADO
};

struct processo_t {
    int pid; //número do processo
    int parentPID; //id do processo pai
    // estado da CPU
    int regPC, regA, regX, regERRO;
    estado_t estadoCorrente;
    int pIniMemoria;
    int pFimMemoria;
    float prioridade;
    int quantum;
    //memória
    // entrada/saida
    int terminal;
    bool esperando_leitura;
    bool esperando_escrita;
    int esperando_processo;
    int contadorPronto;
    int contadorBloqueado;
    int contadorExecutando;
    float criacao;
};

processo_t* busca_proc_na_tabela(processo_t** tabela, int pid);
processo_t* cria_processo(processo_t* processoPai);
//int mata_processo(processo_t* proc, processo_t** tabela);
processo_t** cria_vetor_processos();
processo_t* busca_remove_proc_tabela(processo_t** tabela, int pid);

#endif