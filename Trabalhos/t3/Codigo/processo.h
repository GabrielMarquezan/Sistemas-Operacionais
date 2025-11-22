#ifndef PROCESSOS_H
#define PROCESSOS_H

#include <stdbool.h>
#include <stdlib.h>
#include "cpu.h"
#include "tabpag.h"
#include "mmu.h"
#include <math.h>

#define MAX_PROC 100
#define QUANTUM 20

typedef enum estadoProcesso estado_t;
typedef struct lista lista_t;

enum estadoProcesso {
    PRONTO,
    BLOQUEADO,
    EXECUTANDO
};

typedef struct processo_t {
    int pid; //número do processo
    int parentPID; //id do processo pai
    // estado da CPU
    cpu_estado_t estado_cpu;
    estado_t estadoCorrente;
    float prioridade;
    int quantum;
    //memória
    int pIniMemoria;
    int pFimMemoria;
    tabpag_t* tabpag;
    // entrada/saida
    int terminal;
    bool esperando_leitura;
    bool esperando_escrita;
    // metricas
    int esperando_processo;
    int contadorPronto;
    int contadorBloqueado;
    int contadorExecutando;
    int num_preepcoes;
    int criacao;
    int tempo_de_retorno;
    int ultima_entrada_em_bloqueio;
    int tempo_bloqueado;
    int ultima_entrada_em_execucao;
    int tempo_executando;
    int ultima_entrada_em_prontidao;
    int tempo_pronto;
    int tempo_de_resposta_total;
    int num_respostas;
    //disco
    int end_disco;
    int tam_em_mem;
    int num_page_faults;
    int tamanho;
    int num_paginas;
    int* envelhecimento_paginas;;

} processo_t;

#include "lista_processos.h"

processo_t* busca_proc_na_tabela(processo_t** tabela, int pid);
processo_t* cria_processo(processo_t* processoPai, int tempo_criacao, int end_carga, int tamanho);
//int mata_processo(processo_t* proc, processo_t** tabela);
processo_t** cria_vetor_processos();
processo_t* busca_remove_proc_tabela(processo_t** tabela, int pid);

#endif