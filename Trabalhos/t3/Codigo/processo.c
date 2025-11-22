#include "processo.h"

typedef struct processo_t processo_t;


static int prox_pid = 2;

processo_t* cria_processo(processo_t* processoPai, int tempo_criacao, int tamanho) {
    processo_t* proc = malloc(sizeof(processo_t));
    if (proc == NULL) return NULL;
    
    proc->estadoCorrente = PRONTO;

    if (processoPai == NULL) {
        proc->parentPID = 1; //id do INIT
        proc->pid = 1;
    }
    else{
        proc->parentPID = processoPai->pid;
        proc->pid = prox_pid++;
    }
    proc->prioridade = 0.5;
    proc->quantum = QUANTUM;
    proc->estado_cpu.regERRO = ERR_OK;
    proc->estado_cpu.regA = 0;
    proc->estado_cpu.regX = 0;
    proc->esperando_escrita = false;
    proc->esperando_escrita = false;
    proc->esperando_processo = 0;
    proc->terminal = ((proc->pid - 1) % 4) * 4; // 0 = TERM_A, 1 = TERM_B
    proc->contadorBloqueado = 0;
    proc->contadorExecutando = 0;
    proc->contadorPronto = 0;
    proc->num_preepcoes = 0;
    proc->criacao = tempo_criacao;
    proc->tempo_de_retorno = 0;
    proc->tempo_bloqueado = 0;
    proc->tempo_executando = 0;
    proc->tempo_pronto = 1;
    proc->tempo_de_resposta_total = 0;
    proc->num_respostas = 0;
    proc->ultima_entrada_em_prontidao = tempo_criacao;
    proc->ultima_entrada_em_bloqueio = 0;
    proc->ultima_entrada_em_execucao = 0;
    proc->num_page_faults = 0;
    proc->tabpag = tabpag_cria();
    proc->num_paginas = ceil(tamanho/TAM_PAGINA);
    proc->envelhecimento_paginas = malloc(proc->num_paginas *  sizeof(int));

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

// insere na tabela

processo_t* busca_remove_proc_tabela(processo_t** tabela, int pid) {
    for(int i = 0; i < MAX_PROC; i++) {
        if(tabela[i] != NULL && tabela[i]->pid == pid) {
            processo_t* proc = tabela[i];
            tabela[i] = NULL;
            return proc;
        }
    }

    return NULL;
}