// so.c
// sistema operacional
// simulador de computador
// so25b

#include "so.h"

// ---------------------------------------------------------------------
// CONSTANTES E TIPOS {{{1
// ---------------------------------------------------------------------

// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 50   // em instruções executadas
#define ESCALONADOR_ATIVO ESC_ROUND_ROBIN
#define CAP_MAX_HEAP 100
#define NENHUM_PROCESSO -1
#define ALGUM_PROCESSO 0

static bool alterar_tempo_pronto = true;

struct so_t {
  cpu_t *cpu;
  mem_t *mem;
  mem_t *disco;
  mmu_t *mmu;
  es_t *es;
  console_t *console;
  bool erro_interno;
  processo_t* processoCorrente;
  processo_t** processosCPU;
  int qtdProc;
  int qtd_processos_criados;
  bool todos_proc_bloqueados;
  int tempo_total_todos_bloqueados;
  int ultimo_tempo_todos_bloqueados;
  bool terminais_ocupados[4]; // 0 = TERM_A, 1 = TERM_B, ...
  fila_rr* fila_proc_prontos;
  fila_prioridade* fila_proc_prioridade;
  int cont_esperando;
  int num_irq_relogio;
  int num_irq_reset;
  int num_irq_desconhecida;
  int num_irq_chamada_sistema;
  int num_irq_err_cpu;
  int num_preempcoes;
  int tempo_retorno_processos[MAX_PROC+1];
  int preempcoes_por_processo[MAX_PROC+1];
  int tempo_processos_bloqueados[MAX_PROC+1];
  int tempo_processos_prontos[MAX_PROC+1];
  int tempo_processos_executando[MAX_PROC+1];
  int tempo_resposta_processos[MAX_PROC+1];
  int num_bloqueios_por_processo[MAX_PROC+1];
  int num_prontos_por_processo[MAX_PROC+1];
  int num_execucoes_por_processo[MAX_PROC+1];
  int total_frames;
  bool* frames_livres;
  int tempo_disco_livre;
  int proximo_end_livre_disco;
};


// função de tratamento de interrupção (entrada no SO)
static int so_trata_interrupcao(void *argC, int reg_A);
static void so_trata_espera_proc_morrer(so_t* self, int pid_morto);
static void so_chamada_mata_proc(so_t *self);

// funções auxiliares
// carrega o programa contido no arquivo na memória do processador; retorna end. inicial
static int so_carrega_programa(so_t *self, char *nome_do_executavel, int* endFim, int *tamanho);
// copia para str da memória do processador, até copiar um 0 (retorna true) ou tam bytes
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender, processo_t* proc);

static void so_recalcula_prioridade(processo_t* proc) {
  if (proc == NULL) return;
  console_printf("SO: recalculando prioridade do processo");
  float t_exec = QUANTUM - proc->quantum;
  proc->prioridade = (proc->prioridade + (t_exec / QUANTUM)) / 2.0;
}

static void so_verifica_todos_bloqueados(so_t* self) {
  console_printf("SO: verficando se todos estão bloqueados");
  for(int i = 0; i < MAX_PROC; i++) {
    if(self->processosCPU[i] != NULL && self->processosCPU[i]->estadoCorrente != BLOQUEADO) self->todos_proc_bloqueados = false;
  }
  self->todos_proc_bloqueados = true;
  self->ultimo_tempo_todos_bloqueados = self->cpu->relogio->agora;
}

static void so_bloqueia_processo(so_t *self) {
    if (self->processoCorrente != NULL) {
      console_printf("SO: bloqueando processo");
      if(ESCALONADOR_ATIVO == ESC_PRIORIDADE) so_recalcula_prioridade(self->processoCorrente);
      self->processoCorrente->tempo_executando += self->cpu->relogio->agora - self->processoCorrente->ultima_entrada_em_execucao;
      self->processoCorrente->ultima_entrada_em_bloqueio = self->cpu->relogio->agora;
      self->processoCorrente->estadoCorrente = BLOQUEADO;
      self->processoCorrente->contadorBloqueado++;
      self->processoCorrente = NULL; 
      so_verifica_todos_bloqueados(self);
    }
}

//--------------------------------------------------------------------
// PROCESSOS 
//--------------------------------------------------------------------

bool mata_processo(so_t* self, int pid) {
    int pid_alvo = pid;
    if(pid_alvo == 0) {
      if(self->processoCorrente == NULL) return false;
      pid_alvo = self->processoCorrente->pid;
    }
    console_printf("SO: matando processo com PID = %d", pid_alvo);
    processo_t* proc = busca_remove_proc_tabela(self->processosCPU, pid_alvo);
    if(proc == NULL) {
      console_printf("SO: processo não encontrado na tabela");
      return false;
    }
    
    free(proc->envelhecimento_paginas);
    tabpag_destroi(proc->tabpag);

    int agora = self->cpu->relogio->agora;
    switch (proc->estadoCorrente) {
      case EXECUTANDO:
          proc->tempo_executando += agora - proc->ultima_entrada_em_execucao;
          break;
      case PRONTO: 
          proc->tempo_pronto += agora - proc->ultima_entrada_em_prontidao;
          break;
      case BLOQUEADO:
          proc->tempo_bloqueado += agora - proc->ultima_entrada_em_bloqueio;
          break;
    }

    //mata os filhos
    console_printf("SO: realizando morte em cascata");
    for (int i=0; i < MAX_PROC; i++) {
      if (self->processosCPU[i] == NULL) continue;
      if (self->processosCPU[i]->parentPID == proc->pid) {
        mata_processo(self, self->processosCPU[i]->pid);
        int pid_morto = self->processosCPU[i]->pid;
        so_trata_espera_proc_morrer(self, pid_morto);
      }
    }

    int pid_morto = proc->pid;
    so_trata_espera_proc_morrer(self, pid_morto);

    if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) {
      console_printf("SO: removendo processo da fila de prontos");
      fila_rr_remove_pid(self->fila_proc_prontos, proc->pid);
    } else fila_remove_proc_pid(self->fila_proc_prioridade, proc);

    if (proc == self->processoCorrente) self->processoCorrente = NULL;
    if(proc->pid < MAX_PROC){
      proc->tempo_de_retorno = self->cpu->relogio->agora - proc->criacao;
      self->tempo_retorno_processos[pid_morto] = proc->tempo_de_retorno;
      self->preempcoes_por_processo[pid_morto] = proc->num_preepcoes;
      self->tempo_processos_bloqueados[pid_morto] = proc->tempo_bloqueado;
      self->tempo_processos_prontos[pid_morto] = proc->tempo_pronto;
      self->tempo_processos_executando[pid_morto] = proc->tempo_executando;
      if(proc->num_respostas > 0) {
        self->tempo_resposta_processos[pid_morto] = proc->tempo_de_resposta_total / proc->num_respostas;
      }
      else self->tempo_resposta_processos[pid_morto] = -1; // Indica que nunca executou
      self->num_bloqueios_por_processo[pid_morto] = proc->contadorBloqueado;
      self->num_prontos_por_processo[pid_morto] = proc->contadorPronto;
      self->num_execucoes_por_processo[pid_morto] = proc->contadorExecutando;
    }
    free(proc);
    console_printf("SO: processo com PID = %d morto com sucesso", pid_morto);
    self->qtdProc--;
    return true;
}

static processo_t* busca_processo_escrita_pendente(so_t* self, int terminal){
  for (int i = 0; i < MAX_PROC; i++) {
    if (self->processosCPU[i] == NULL) continue;
    if (self->processosCPU[i]->terminal == terminal
        && self->processosCPU[i]->esperando_escrita) {
          console_printf("SO: buscando processo esperando escrita");
          return self->processosCPU[i];
        }
  }
  return NULL;
}

static processo_t* busca_processo_leitura_pendente(so_t* self, int terminal){
  for (int i = 0; i < MAX_PROC; i++) {
    if (self->processosCPU[i] == NULL) continue;
    if (self->processosCPU[i]->terminal == terminal
        && self->processosCPU[i]->esperando_leitura) {
          console_printf("SO: buscando processo esperando leitura");
          return self->processosCPU[i];
        }
  }
  return NULL;
}

// insere um novo processo na tabela de processos
bool so_insere_processo(so_t* self, processo_t* proc) {
  if (proc == NULL){
    console_printf("SO: impossivel inserir processo. Processo é NULL!");
    return false;
  }

  // insere o processo na primeira posição vazia da tabela
  for (int i = 0; i < MAX_PROC; i++) {
    if (self->processosCPU[i] == NULL) {
      console_printf("SO: Processo com PID %d inserido na posição %d da tabela", proc->pid, i);
      self->processosCPU[i] = proc;
      self->qtdProc++;
      return true;
    }
  }
  console_printf("SO: tabela cheia, impossível inserir processo!");
  return false;
}

// TERMINAL

static bool checa_terminal_ok(so_t* self, int terminal) {
  int estado;
  if (es_le(self->es, terminal, &estado) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado do terminal");
    self->erro_interno = true;
    return false;
  }

  return estado == 1;
}

static void le_dado_teclado(so_t* self, processo_t* proc) {
  int dado;
  if (es_le(self->es, proc->terminal + TERM_TECLADO, &dado) != ERR_OK) {
    console_printf("SO: problema no acesso ao teclado");
    self->erro_interno = true;
    return;
  }
  // escreve no reg A do processo corrente
  // (na verdade, na posição onde o processador vai pegar o A quando retornar da int)
  proc->estado_cpu.regA = dado;
}

static void escreve_dado_tela(so_t* self, processo_t* proc) {
  if (es_escreve(self->es, proc->terminal + TERM_TELA, proc->estado_cpu.regX) != ERR_OK) {
    console_printf("SO: problema no acesso à tela");
    self->erro_interno = true;
    return;
  }
  proc->estado_cpu.regA = 0;
}


// ---------------------------------------------------------------------
// CRIAÇÃO {{{1
// ---------------------------------------------------------------------

so_t *so_cria(cpu_t *cpu, mem_t *mem, mem_t *disco, mmu_t *mmu,
              es_t *es, console_t *console)
{
  console_printf("INICIALIZANDO SISTEMA OPERACIONAL");
  so_t *self = malloc(sizeof(*self));
  if (self == NULL) return NULL;
  self->processoCorrente=NULL;
  self->processosCPU = cria_vetor_processos();
  memset(self->terminais_ocupados, 0, sizeof(self->terminais_ocupados));
  self->cont_esperando = 0;
  self->qtdProc = 0;
  self->qtd_processos_criados = 0;
  self->todos_proc_bloqueados = false;
  self->cpu = cpu;
  self->mem = mem;
  self->mmu = mmu;
  self->disco = disco;
  self->es = es;
  self->console = console;
  self->erro_interno = false;
  self->num_irq_relogio = 0;
  self->num_irq_reset = 0;
  self->num_irq_desconhecida = 0;
  self->num_irq_chamada_sistema = 0;
  self->num_irq_err_cpu = 0;
  self->num_preempcoes = 0;
  self->tempo_total_todos_bloqueados = 0;
  self->ultimo_tempo_todos_bloqueados = 0;

  for(int i = 0; i <= MAX_PROC; i++) {
    self->num_bloqueios_por_processo[i] = 0;
    self->num_execucoes_por_processo[i] = 0;
    self->num_prontos_por_processo[i] = 0;
    self->tempo_processos_bloqueados[i] = 0;
    self->tempo_processos_executando[i] = 0;
    self->tempo_processos_prontos[i] = 0;
    self->tempo_resposta_processos[i] = 0;
    self->tempo_retorno_processos[i] = 0;
  }

  self->total_frames = ceil(mem->tam / TAM_PAGINA);
  self->frames_livres = malloc(self->total_frames * sizeof(bool));
  for(int i = 0; i < self->total_frames; i++) self->frames_livres[i] = true;
  self->tempo_disco_livre = 0;
  self->proximo_end_livre_disco = 0;

  if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) {
    self->fila_proc_prontos = fila_rr_cria();
    self->fila_proc_prioridade = NULL;
  }
  else {
    self->fila_proc_prontos = NULL;
    self->fila_proc_prioridade = fila_cria(CAP_MAX_HEAP);
  }

  // quando a CPU executar uma instrução CHAMAC, deve chamar a função
  //   so_trata_interrupcao, com primeiro argumento um ptr para o SO
  cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);

  console_printf("SISTEMA OPERACIONAL INICIALIZADO COM SUCESSO");
  return self;
}

void so_destroi(so_t *self)
{
  cpu_define_chamaC(self->cpu, NULL, NULL);
  if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) fila_rr_destroi(self->fila_proc_prontos);
  else fila_destroi(self->fila_proc_prioridade);
  free(self);
}


// ---------------------------------------------------------------------
// TRATAMENTO DE INTERRUPÇÃO {{{1
// ---------------------------------------------------------------------

// funções auxiliares para o tratamento de interrupção
static void so_salva_estado_da_cpu(so_t *self);
static void so_trata_irq(so_t *self, int irq);
static void so_trata_pendencias(so_t *self);
static void so_escalona(so_t *self);
static int so_despacha(so_t *self);

// função a ser chamada pela CPU quando executa a instrução CHAMAC, no tratador de
//   interrupção em assembly
// essa é a única forma de entrada no SO depois da inicialização
// na inicialização do SO, a CPU foi programada para chamar esta função para executar
//   a instrução CHAMAC
// a instrução CHAMAC só deve ser executada pelo tratador de interrupção
//
// o primeiro argumento é um ponteiro para o SO, o segundo é a identificação
//   da interrupção
// o valor retornado por esta função é colocado no registrador A, e pode ser
//   testado pelo código que está após o CHAMAC. No tratador de interrupção em
//   assembly esse valor é usado para decidir se a CPU deve retornar da interrupção
//   (e executar o código de usuário) ou executar PARA e ficar suspensa até receber
//   outra interrupção
static int so_trata_interrupcao(void *argC, int reg_A)
{
  so_t *self = argC;
  irq_t irq = reg_A;
  // esse print polui bastante, recomendo tirar quando estiver com mais confiança
  console_printf("SO: recebi IRQ %d (%s)", irq, irq_nome(irq));
  // salva o estado da cpu no descritor do processo que foi interrompido
  so_salva_estado_da_cpu(self);

  // faz o atendimento da interrupção
  so_trata_irq(self, irq);
  // faz o processamento independente da interrupção
  so_trata_pendencias(self);
  // escolhe o próximo processo a executar
  so_escalona(self);
  // recupera o estado do processo escolhido
  return so_despacha(self);
}

static void so_salva_estado_da_cpu(so_t *self)
{
  // t2: salva os registradores que compõem o estado da cpu no descritor do
  //   processo corrente. os valores dos registradores foram colocados pela
  //   CPU na memória, nos endereços CPU_END_PC etc. O registrador X foi salvo
  //   pelo tratador de interrupção (ver trata_irq.asm) no endereço 59
  // se não houver processo corrente, não faz nada
  if (self->processoCorrente == NULL){
    return;
  }
  if (mem_le(self->mem, CPU_END_A, &self->processoCorrente->estado_cpu.regA) != ERR_OK
      || mem_le(self->mem, CPU_END_PC, &self->processoCorrente->estado_cpu.regPC) != ERR_OK
      || mem_le(self->mem, CPU_END_erro, &self->processoCorrente->estado_cpu.regERRO) != ERR_OK
      || mem_le(self->mem, 59, &self->processoCorrente->estado_cpu.regX) != ERR_OK) {
    console_printf("SO: erro na leitura dos registradores");
    self->erro_interno = true;
  }
}

static void so_trata_pendencias(so_t *self)
{
  // t2: realiza ações que não são diretamente ligadas com a interrupção que
  //   está sendo atendida:
  // - E/S pendente
  // - contabilidades
  // - etc
  //checa se algum terminal que foi pedido está livre => se algum terminal marcado como 1 ficou livre
  for (int i = 0; i < 4; i++) {
    processo_t* proc = NULL;
    int leitura = 0, escrita = 0;
    if (self->terminais_ocupados[i]) {
      // le se tem dado no teclado
      leitura = checa_terminal_ok(self, (i * 4) + TERM_TECLADO_OK);
      // escreve se n tem nada sendo escrito
      escrita = checa_terminal_ok(self, (i * 4) + TERM_TELA_OK);
      //console_printf("Leitura = %d, escrita = %d", leitura, escrita);
      if (leitura) {
        proc = busca_processo_leitura_pendente(self, i*4);
        if (proc != NULL) {
          le_dado_teclado(self, proc);
          proc->tempo_bloqueado += self->cpu->relogio->agora - proc->ultima_entrada_em_bloqueio;
          proc->estadoCorrente = PRONTO;
          proc->ultima_entrada_em_prontidao = self->cpu->relogio->agora;
          alterar_tempo_pronto = true;
          proc->contadorPronto++;
          proc->esperando_leitura = false;

          if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) fila_rr_insere_fim(self->fila_proc_prontos, proc);
          else inserir(self->fila_proc_prioridade, proc);
          
          if(self->todos_proc_bloqueados == true) {
            self->todos_proc_bloqueados = false;
            self->tempo_total_todos_bloqueados += self->cpu->relogio->agora - self->ultimo_tempo_todos_bloqueados;
          }
        }
      } else if (escrita) {
        //console_printf("ENTREI PARA VER SE ACHO AMIGUINHOS");
        proc = busca_processo_escrita_pendente(self, i*4);
        if (proc != NULL) {
          //console_printf("AMIGINHOS!!! PID=%d",proc->pid);
          escreve_dado_tela(self, proc);
          proc->tempo_bloqueado += self->cpu->relogio->agora - proc->ultima_entrada_em_bloqueio;
          proc->estadoCorrente = PRONTO;
          proc->ultima_entrada_em_prontidao = self->cpu->relogio->agora;
          alterar_tempo_pronto = true;
          proc->contadorPronto++;
          proc->esperando_escrita = false;

          if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) fila_rr_insere_fim(self->fila_proc_prontos, proc);
          else inserir(self->fila_proc_prioridade, proc);
        
          if(self->todos_proc_bloqueados == true) {
            self->todos_proc_bloqueados = false;
            self->tempo_total_todos_bloqueados += self->cpu->relogio->agora - self->ultimo_tempo_todos_bloqueados;
          }
        }
      }
      // libera o terminal
      self->terminais_ocupados[i] = !(escrita && leitura);
    }
  }
}

static bool so_verifica_tabela_vazia(so_t* self) {
  bool tabela_vazia = true;
  for(int i = 0; i < MAX_PROC; i++) {
    if(self->processosCPU[i] != NULL) tabela_vazia = false;
  }

  return tabela_vazia;
}

static void so_escalona(so_t *self)
{
  // escolhe o próximo processo a executar, que passa a ser o processo
  //   corrente; pode continuar sendo o mesmo de antes ou não
  // t2: na primeira versão, escolhe um processo pronto caso o processo
  //   corrente não possa continuar executando, senão deixa o mesmo processo.
  //   depois, implementa um escalonador melhor
  
  if (self->processoCorrente != NULL && self->processoCorrente->estadoCorrente == PRONTO) {
    if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) fila_rr_insere_fim(self->fila_proc_prontos, self->processoCorrente);
    else inserir(self->fila_proc_prioridade, self->processoCorrente);
    self->processoCorrente = NULL;
  }
  
  if (self->processoCorrente == NULL) {
    processo_t* proximo = NULL;
    if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) proximo = fila_rr_remove_inicio(self->fila_proc_prontos);
    else proximo = remover(self->fila_proc_prioridade);

    if (proximo != NULL) {
      console_printf("ESCALONANDO: PID=%d ", proximo->pid);      
      self->processoCorrente = proximo;
      self->processoCorrente->estadoCorrente = EXECUTANDO;
      self->processoCorrente->contadorExecutando++;
      self->processoCorrente->quantum = QUANTUM;
    } else {
      console_printf("SO: Fila vazia");
      if(so_verifica_tabela_vazia(self)) {
        self->cpu->fim_do_programa = true;
        console_printf("Numero de processos criados: %d", self->qtd_processos_criados);
        console_printf("Tempo total de execucao (instrucoes): %d", self->cpu->relogio->agora);
        console_printf("Tempo ocioso total: %d", self->tempo_total_todos_bloqueados);
        console_printf("Numero de interrupcoes de relogio: %d", self->num_irq_relogio);
        console_printf("Numero de interrupcoes de chamada de sistema: %d", self->num_irq_chamada_sistema);
        console_printf("Numero de interrupcoes de erro de cpu: %d", self->num_irq_err_cpu);
        console_printf("Numero de interrupcoes de reset: %d", self->num_irq_reset);
        console_printf("Numero de interrupcoes desconhecidas: %d", self->num_irq_desconhecida);
        console_printf("Numero de preempcoes: %d", self->num_preempcoes);
        for(int i = 1; i <= self->qtd_processos_criados; i++) {
          console_printf("\n\n");
          console_printf("////////////////////////////////////////////////////////////////////////////");
          console_printf("                             PROCESSO COM PID %d                          ", i);
          console_printf("////////////////////////////////////////////////////////////////////////////");
          console_printf("\n\n");
          console_printf("Tempo de bloqueio do processo com PID %d: %d", i, self->tempo_processos_bloqueados[i]);
          console_printf("Tempo de prontidao do processo com PID %d: %d", i, self->tempo_processos_prontos[i]);
          console_printf("Tempo de execucao do processo com PID %d: %d", i, self->tempo_processos_executando[i]);
          console_printf("Tempo de retorno do processo com PID %d: %d", i, self->tempo_retorno_processos[i]);
          console_printf("Tempo medio de resposta do processo com PID %d: %d", i, self->tempo_resposta_processos[i]);
          console_printf("Numero de preempcoes do processo com PID %d: %d", i, self->preempcoes_por_processo[i]);
          console_printf("Numero de bloqueios do processo com PID %d: %d", i, self->num_bloqueios_por_processo[i]);
          console_printf("Numero de prontidoes do processo com PID %d: %d", i, self->num_prontos_por_processo[i]);
          console_printf("Numero de execucoes do processo com PID %d: %d", i, self->num_execucoes_por_processo[i]);
        }
      }
    }
  }
}

static int so_despacha(so_t *self)
{
  // t2: se houver processo corrente, coloca o estado desse processo onde ele
  //   será recuperado pela CPU (em CPU_END_PC etc e 59) e retorna 0,
  //   senão retorna 1
  // o valor retornado será o valor de retorno de CHAMAC, e será colocado no 
  //   registrador A para o tratador de interrupção (ver trata_irq.asm).
  if (self->processoCorrente == NULL) return 1;

  if(alterar_tempo_pronto) {
    self->processoCorrente->tempo_pronto += self->cpu->relogio->agora - self->processoCorrente->ultima_entrada_em_prontidao;
    alterar_tempo_pronto = false;
  }
  self->processoCorrente->ultima_entrada_em_execucao = self->cpu->relogio->agora;
  self->processoCorrente->num_respostas++;
  self->processoCorrente->tempo_de_resposta_total += self->cpu->relogio->agora - self->processoCorrente->ultima_entrada_em_prontidao;

  mmu_define_tabpag(self->mmu, self->processoCorrente->tabpag);

  if (mem_escreve(self->mem, CPU_END_A, self->processoCorrente->estado_cpu.regA) != ERR_OK
      || mem_escreve(self->mem, CPU_END_PC, self->processoCorrente->estado_cpu.regPC) != ERR_OK
      || mem_escreve(self->mem, CPU_END_erro, self->processoCorrente->estado_cpu.regERRO) != ERR_OK
      || mem_escreve(self->mem, 59, self->processoCorrente->estado_cpu.regX) != ERR_OK) {
    console_printf("SO: erro na escrita dos registradores");
    self->erro_interno = true;
  }
  if (self->erro_interno) return 1;
  else return 0;
}


// ---------------------------------------------------------------------
// TRATAMENTO DE UMA IRQ {{{1
// ---------------------------------------------------------------------

// funções auxiliares para tratar cada tipo de interrupção
static void so_trata_reset(so_t *self);
static void so_trata_irq_chamada_sistema(so_t *self);
static void so_trata_irq_err_cpu(so_t *self);
static void so_trata_irq_relogio(so_t *self);
static void so_trata_irq_desconhecida(so_t *self, int irq);

static void so_trata_irq(so_t *self, int irq)
{
  // verifica o tipo de interrupção que está acontecendo, e atende de acordo
  switch (irq) {
    case IRQ_RESET:
      self->num_irq_reset++;
      so_trata_reset(self);
      break;
    case IRQ_SISTEMA:
      self->num_irq_chamada_sistema++;
      so_trata_irq_chamada_sistema(self);
      break;
    case IRQ_ERR_CPU:
      self->num_irq_err_cpu++;
      so_trata_irq_err_cpu(self);
      break;
    case IRQ_RELOGIO:
      self->num_irq_relogio++;
      so_trata_irq_relogio(self);
      break;
    default:
      self->num_irq_desconhecida++;
      so_trata_irq_desconhecida(self, irq);
  }
}

// chamada uma única vez, quando a CPU inicializa
static void so_trata_reset(so_t *self)
{
  // coloca o tratador de interrupção na memória
  // quando a CPU aceita uma interrupção, passa para modo supervisor,
  //   salva seu estado à partir do endereço CPU_END_PC, e desvia para o
  //   endereço CPU_END_TRATADOR
  // colocamos no endereço CPU_END_TRATADOR o programa de tratamento
  //   de interrupção (escrito em asm). esse programa deve conter a
  //   instrução CHAMAC, que vai chamar so_trata_interrupcao (como
  //   foi definido na inicialização do SO)
  int tamanho = 0;
  int ender = so_carrega_programa(self, "trata_int.maq", NULL, &tamanho);
  if (ender != CPU_END_TRATADOR) {
    console_printf("SO: problema na carga do programa de tratamento de interrupção");
    self->erro_interno = true;
  }

  // programa o relógio para gerar uma interrupção após INTERVALO_INTERRUPCAO
  if (es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO) != ERR_OK) {
    console_printf("SO: problema na programação do timer");
    self->erro_interno = true;
  }

  // t2: deveria criar um processo para o init, e inicializar o estado do
  //   processador para esse processo com os registradores zerados, exceto
  //   o PC e o modo.
  // como não tem suporte a processos, está carregando os valores dos
  //   registradores diretamente no estado da CPU mantido pelo SO; daí vai
  //   copiar para o início da memória pelo despachante, de onde a CPU vai
  //   carregar para os seus registradores quando executar a instrução RETI
  //   em bios.asm (que é onde está a instrução CHAMAC que causou a execução
  //   deste código

  processo_t* init = cria_processo(NULL, self->cpu->relogio->agora, ender, tamanho);
  self->qtd_processos_criados++;
  // coloca o programa init na memória
  int enderFim = -1;
  ender = so_carrega_programa(self, "init.maq", &enderFim, &tamanho);
  if (ender != 100) {
    console_printf("SO: problema na carga do programa inicial");
    self->erro_interno = true;
    return;
  }
  init->estado_cpu.regPC = ender;
  init->pIniMemoria = ender;
  init->pFimMemoria = enderFim;
  if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) fila_rr_insere_fim(self->fila_proc_prontos, init);
  else inserir(self->fila_proc_prioridade, init);
  so_insere_processo(self, init);
}

static int busca_quadro_livre_mem(so_t *self) {
  int num_quadros = ceil(self->mem->tam / TAM_PAGINA);
  for(int i = 0; i < num_quadros; i++){
    if(self->frames_livres[i]) return i;
  }
  return -1;
}

static int busca_pagina_mais_velha(processo_t *proc) {
  int pagina_mais_velha = -1;
  int aux = -1;
  for(int i = 0; i < proc->num_paginas; i++) {
    if(tabpag_traduz(proc->tabpag, i, &aux) == ERR_OK) {
      if(pagina_mais_velha < proc->envelhecimento_paginas[i]) {
        pagina_mais_velha = proc->envelhecimento_paginas[i];
      }
    }
  }
  return pagina_mais_velha;
}


static void atualiza_least_recently_used(processo_t *proc) {
  int aux = -1;
  for(int i = 0; i < proc->num_paginas; i++) {
    if(tabpag_traduz(proc->tabpag, i, &aux) == ERR_OK) {
      bool acessou = tabpag_bit_acesso(proc->tabpag, i);

      if(acessou) {
        proc->envelhecimento_paginas[i]--;
        tabpag_zera_bit_acesso(proc->tabpag, i);  
      }
      else proc->envelhecimento_paginas[i]++;
    }
  }
}

static void trata_page_fault(so_t *self) {
  int quadro_livre = busca_quadro_livre_mem(self);
  int end_faltante = self->processoCorrente->estado_cpu.regComplemento;
  int pagina_faltante = end_faltante / TAM_PAGINA; 
  int end_disco = pagina_faltante * TAM_PAGINA + self->processoCorrente->end_disco;
  int vai_pra_mem[TAM_PAGINA];
  int end_mem;

  for(int i = 0; i < TAM_PAGINA; i++) {
    mem_le(self->disco, i + end_disco, &vai_pra_mem[i]);
  }

  if(quadro_livre < 0) {
    int pagina_vitima = busca_pagina_mais_velha(self->processoCorrente);
    int vai_pro_disco[TAM_PAGINA];

    if(tabpag_traduz(self->processoCorrente->tabpag, pagina_vitima, &quadro_livre) == ERR_OK) {
      if(tabpag_bit_alteracao(self->processoCorrente->tabpag, pagina_vitima)) {  
        end_mem = quadro_livre * TAM_PAGINA;
        end_faltante = self->processoCorrente->end_disco + pagina_vitima * TAM_PAGINA;
        
        for(int i = 0; i < TAM_PAGINA; i++) {
          mem_le(self->mem, i + end_mem, &vai_pro_disco[i]);
          mem_escreve(self->disco, end_faltante + i, vai_pro_disco[i]);
        }
      }

      tabpag_invalida_pagina(self->processoCorrente->tabpag, pagina_vitima);
    }
  }

  end_mem = quadro_livre * TAM_PAGINA;

  for(int i = 0; i < TAM_PAGINA; i++) {
    mem_escreve(self->mem, end_mem + i, vai_pra_mem[i]);
  }
  tabpag_define_quadro(self->processoCorrente->tabpag, pagina_faltante, quadro_livre);
}

// interrupção gerada quando a CPU identifica um erro
static void so_trata_irq_err_cpu(so_t *self)
{
  // Ocorreu um erro interno na CPU
  // O erro está codificado em CPU_END_erro
  // Em geral, causa a morte do processo que causou o erro
  // Ainda não temos processos, causa a parada da CPU
  // t2: com suporte a processos, deveria pegar o valor do registrador erro
  //   no descritor do processo corrente, e reagir de acordo com esse erro
  //   (em geral, matando o processo)
  if(self->processoCorrente == NULL) {
    console_printf("SO: erro de CPU enquanto parada");
    return;
  }

  err_t err = self->processoCorrente->estado_cpu.regERRO;
  
  console_printf("ERRO DE CPU: PID=%d, Codigo de Erro=%d (%s)", 
               self->processoCorrente->pid, err, err_nome(err));
  // se o processo tenta acessar um discpositivo ocupado, o processo bloqueia
  if (err == ERR_OCUP) {
    self->processoCorrente->tempo_executando += self->cpu->relogio->agora - self->processoCorrente->ultima_entrada_em_execucao;
    self->processoCorrente->ultima_entrada_em_bloqueio = self->cpu->relogio->agora;
    self->processoCorrente->estadoCorrente = BLOQUEADO;
    self->processoCorrente->contadorBloqueado++;
    self->processoCorrente = NULL;
    return;
  }
  else if (err == ERR_INSTR_INV || err == ERR_END_INV ||
      err == ERR_INSTR_PRIV || err == ERR_OP_INV ||
      err == ERR_DISP_INV) {
      so_chamada_mata_proc(self);
  }
  else if(err == ERR_PAG_AUSENTE) {
    int end_err = self->processoCorrente->estado_cpu.regComplemento;
    
    if(end_err < 0 || end_err > self->processoCorrente->tamanho) {
        self->processoCorrente->estado_cpu.regA = 0;
        so_chamada_mata_proc(self);
    }
    else{
      trata_page_fault(self);
    }
  }
}

// interrupção gerada quando o timer expira
static void so_trata_irq_relogio(so_t *self) {
  // rearma o interruptor do relógio e reinicializa o timer para a próxima interrupção
  err_t e1, e2;
  e1 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO, 0); // desliga o sinalizador de interrupção
  e2 = es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO);
  if (e1 != ERR_OK || e2 != ERR_OK) {
    console_printf("SO: problema da reinicialização do timer");
    self->erro_interno = true;
  }

  if (self->processoCorrente != NULL) {
    self->processoCorrente->quantum--;

    
    if(self->processoCorrente->quantum <= 0) {
      console_printf("SO: preempção por esgotamento de quantum do PID %d", self->processoCorrente->pid);
      self->num_preempcoes++;
      self->processoCorrente->num_preepcoes++;

      if(ESCALONADOR_ATIVO == ESC_PRIORIDADE) so_recalcula_prioridade(self->processoCorrente);
      self->processoCorrente->tempo_executando += self->cpu->relogio->agora - self->processoCorrente->ultima_entrada_em_execucao;
      self->processoCorrente->estadoCorrente = PRONTO;
      self->processoCorrente->contadorPronto++;
      self->processoCorrente->ultima_entrada_em_prontidao = self->cpu->relogio->agora;
      alterar_tempo_pronto = true;
      if(self->todos_proc_bloqueados == true) {
        self->todos_proc_bloqueados = false;
        self->tempo_total_todos_bloqueados += self->cpu->relogio->agora - self->ultimo_tempo_todos_bloqueados;
      }
    }

    atualiza_least_recently_used(self->processoCorrente);
  }

  // t2: deveria tratar a interrupção
  //   por exemplo, decrementa o quantum do processo corrente, quando se tem
  //   um escalonador com quantum

  //console_printf("SO: interrupcao do relogio (tratada)");
}

// foi gerada uma interrupção para a qual o SO não está preparado
static void so_trata_irq_desconhecida(so_t *self, int irq)
{
  console_printf("SO: não sei tratar IRQ %d (%s)", irq, irq_nome(irq));
  self->erro_interno = true;
  if (self->processoCorrente != NULL) so_chamada_mata_proc(self); 
}


// ---------------------------------------------------------------------
// CHAMADAS DE SISTEMA {{{1
// ---------------------------------------------------------------------

// funções auxiliares para cada chamada de sistema
static void so_chamada_le(so_t *self);
static void so_chamada_escr(so_t *self);
static void so_chamada_cria_proc(so_t *self);
static void so_chamada_mata_proc(so_t *self);
static void so_chamada_espera_proc(so_t *self);

static void so_trata_irq_chamada_sistema(so_t *self)
{
  // a identificação da chamada está no registrador A
  // t2: com processos, o reg A deve estar no descritor do processo corrente
  if(self->processoCorrente == NULL) {
    console_printf("SO: chamada de sistema enquanto CPU esta parada");
    return;
  }
  int id_chamada = self->processoCorrente->estado_cpu.regA;
  //console_printf("SO: chamada de sistema %d", id_chamada);
  switch (id_chamada) {
    case SO_LE:
      so_chamada_le(self);
      console_printf("Recebi chamada LEITURA!");
      break;
    case SO_ESCR:
      so_chamada_escr(self);
      console_printf("Recebi chamada ESCRITA!");
      break;
    case SO_CRIA_PROC:
      so_chamada_cria_proc(self);
      console_printf("Recebi chamada CRIA PROCESSO!");
      break;
    case SO_MATA_PROC:
      so_chamada_mata_proc(self);
      console_printf("Recebi chamada MATA PROCESSO!");
      break;
    case SO_ESPERA_PROC:
      so_chamada_espera_proc(self);
      console_printf("Recebi chamada ESPERA PROCESSO!");
      break;
    default:
      console_printf("SO: chamada de sistema desconhecida (%d)", id_chamada);
      mata_processo(self, self->processoCorrente->pid);
      self->processoCorrente = NULL;
      self->erro_interno = true;
  }
}

// implementação da chamada se sistema SO_LE
// faz a leitura de um dado da entrada corrente do processo, coloca o dado no reg A
static void so_chamada_le(so_t *self)
{
  bool estado = checa_terminal_ok(self, self->processoCorrente->terminal + TERM_TECLADO_OK);
  if (estado == false) {
    self->processoCorrente->esperando_leitura = true;
    self->terminais_ocupados[(self->processoCorrente->terminal)/4] = true;
    so_bloqueia_processo(self);
  } // teclado não está pronto para a leitura
  else le_dado_teclado(self, self->processoCorrente); // então bloqueia o processo que fez a chamada
}

// implementação da chamada se sistema SO_ESCR
// escreve o valor do reg X na saída corrente do processo
static void so_chamada_escr(so_t *self)
{
  bool estado = checa_terminal_ok(self, self->processoCorrente->terminal + TERM_TELA_OK);
  if (estado == false) {
    self->processoCorrente->esperando_escrita = true;
    self->terminais_ocupados[(self->processoCorrente->terminal)/4] = true;
    so_bloqueia_processo(self);
    return;
  }
  escreve_dado_tela(self, self->processoCorrente);
}


static void so_chamada_cria_proc(so_t *self) {
  int tamanho = 0;
  int ender_proc;
  ender_proc = self->processoCorrente->estado_cpu.regX; // em X está o endereço onde está o nome do arquivo
  char nome[100];
  if (copia_str_da_mem(100, nome, self->mem, ender_proc, self->processoCorrente)) {
    int enderFim = -1;
    int ender_carga = so_carrega_programa(self, nome, &enderFim, &tamanho);
    processo_t* novo_proc = cria_processo(self->processoCorrente, self->cpu->relogio->agora, ender_carga, tamanho);
    if (novo_proc == NULL) {
      console_printf("Erro na criação do processo!");
      return;
    }
    console_printf("Processo criado com PID %d e filho do proc com PID %d", novo_proc->pid, novo_proc->parentPID);
    self->qtd_processos_criados++;
    console_printf("ENDER_FIM = %d", enderFim);
    console_printf("ENDER_CARGA = %d", ender_carga);
    if (ender_carga > 0 && enderFim != -1) {
      // t2: deveria escrever no PC do descritor do processo criado
      // se aconteceu algum erro ao carregar o programa da memória, o programa morre
      // então o processo também morre
      // deveria escrever -1 (se erro) ou o PID do processo criado (se OK) no reg A
      //   do processo que pediu a criação
      novo_proc->estado_cpu.regPC = ender_carga;
      novo_proc->pIniMemoria = ender_carga;
      novo_proc->pFimMemoria = enderFim;
      self->processoCorrente->estado_cpu.regA = novo_proc->pid;
      if (!so_insere_processo(self, novo_proc)) {
        console_printf("IMPOSSIVEL INSERIR NA TABELA. MATANDO PROCESSO");
        mata_processo(self, novo_proc->pid);
        self->processoCorrente->estado_cpu.regA = -1;
      }

      if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) fila_rr_insere_fim(self->fila_proc_prontos, novo_proc); // Insere no fim da fila do escalonador
      else inserir(self->fila_proc_prioridade, novo_proc);

      
      return;
    } else {
      console_printf("Ender carga <= 0 ou enderFim != -1");
      mata_processo(self, novo_proc->pid);
      self->processoCorrente->estado_cpu.regA = -1;
    }
  } else {
    console_printf("Erro ao copiar string da memoria");
    self->processoCorrente->estado_cpu.regA = -1;
  }
}

static void so_trata_espera_proc_morrer(so_t* self, int pid_morto) {
  if (pid_morto == 0) return;
  for(int i = 0; i < MAX_PROC; i++) {
    processo_t* proc = self->processosCPU[i];
    
    if((proc != NULL && proc->estadoCorrente == BLOQUEADO) && proc->esperando_processo == pid_morto){
      proc->tempo_bloqueado += self->cpu->relogio->agora - proc->ultima_entrada_em_bloqueio;
      proc->estadoCorrente = PRONTO;
      proc->ultima_entrada_em_prontidao = self->cpu->relogio->agora;
      alterar_tempo_pronto = true;
      proc->contadorPronto++;
      proc->esperando_processo = 0;
      proc->estado_cpu.regA = 0;

      if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) fila_rr_insere_fim(self->fila_proc_prontos, proc); // Insere no fim da fila do escalonador
      else inserir(self->fila_proc_prioridade, proc);

      if(self->todos_proc_bloqueados == true) {
        self->todos_proc_bloqueados = false;
        self->tempo_total_todos_bloqueados += self->cpu->relogio->agora - self->ultimo_tempo_todos_bloqueados;
      }
    }
  }
}

static void so_chamada_mata_proc(so_t *self) {
  // tratar aqui a pendência de quando um processo está esperando outro morrer
  console_printf("QUERO MORRER PID=%d", self->processoCorrente->estado_cpu.regX == 0 ? self->processoCorrente->pid : self->processoCorrente->estado_cpu.regX);
  if (mata_processo(self, self->processoCorrente->estado_cpu.regX)){
    if (self->processoCorrente != NULL) self->processoCorrente->estado_cpu.regA = 0;
  }
  else {
    self->processoCorrente->estado_cpu.regA = -1; // Não conseguiu matar o processo?
    return;
  }
}

static void so_chamada_espera_proc(so_t *self)
{
  int pid_alvo = self->processoCorrente->estado_cpu.regX;
  if (pid_alvo == self->processoCorrente->pid) {
      self->processoCorrente->estado_cpu.regA = -1; // Esperando por si mesmo
      return;
  }

  processo_t* proc_alvo = busca_proc_na_tabela(self->processosCPU, pid_alvo);
  if(proc_alvo == NULL) {
    for(int x=0;x<5;x++){
      console_printf("Meu PID=%d", (self->processosCPU[x]!=NULL)?self->processosCPU[x]->pid:-1);      
    }
    console_printf("Processo PID=%d quer esperar o %d que já morreu e eu n gostei!!!!",self->processoCorrente->pid,pid_alvo);
    self->processoCorrente->estado_cpu.regA = -1; // Processo não existe (ou já morreu)
    return;
  }

  console_printf("Processo PID=%d quer esperar o %d",self->processoCorrente->pid,pid_alvo);
  self->processoCorrente->esperando_processo = pid_alvo;
  so_bloqueia_processo(self);
}


// ---------------------------------------------------------------------
// CARGA DE PROGRAMA {{{1
// ---------------------------------------------------------------------

static int so_carrega_programa(so_t *self, char *nome_do_executavel, int* enderFim, int* tamanho)
{
  programa_t *prog = prog_cria(nome_do_executavel);
  if (prog == NULL) {
    console_printf("Erro na leitura do programa '%s'\n", nome_do_executavel);
    return -1;
  }

  int end_virt_ini = prog_end_carga(prog);
  *tamanho = prog_tamanho(prog);
  int end_virt_fim = end_virt_ini + *tamanho;
  int end_fisico_carga; 
  
  mem_t *memoria_destino;
  char disco_ou_mem[] = "memoria";

  if(strcmp(nome_do_executavel, "trata_int.maq") < 0) {
    memoria_destino = self->disco;
    strcpy(disco_ou_mem, "disco");
    
    end_fisico_carga = self->proximo_end_livre_disco;
    self->proximo_end_livre_disco += *tamanho;
  }
  else {
    memoria_destino = self->mem;
    end_fisico_carga = end_virt_ini; 
  }

  for (int end_virt = end_virt_ini; end_virt < end_virt_fim; end_virt++) {
    int deslocamento = end_virt - end_virt_ini;
    int dado = prog_dado(prog, end_virt);
    if (mem_escreve(memoria_destino, end_fisico_carga + deslocamento, dado) != ERR_OK) {
      console_printf("Erro na carga do %s, endereco %d\n", disco_ou_mem, end_virt);
      prog_destroi(prog);
      return -1;
    }
  }

  prog_destroi(prog);
  console_printf("SO: carga de '%s' em %s end_fisico: %d", nome_do_executavel, disco_ou_mem, end_fisico_carga);

  if (enderFim != NULL) *enderFim = end_virt_fim;
 
  return end_fisico_carga;
}


// ---------------------------------------------------------------------
// ACESSO À MEMÓRIA DOS PROCESSOS {{{1
// ---------------------------------------------------------------------

// static bool so_copia_str_do_processo(so_t *self, int tam, char str[tam], int end_virt, processo_t *proc) 
// OBS: Ajustei o tipo de 'proc' para ponteiro, que é o padrão em C para structs grandes, 
// mas se o seu código original usa valor, mantenha como estava. Assumirei que 'proc' é o processoCorrente.

// copia uma string da memória do simulador para o vetor str.
// retorna false se erro (string maior que vetor, valor não char na memória,
//   erro de acesso à memória)
// t2: deveria verificar se a memória pertence ao processo
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender, processo_t* proc)
{
  console_printf("pIniMemoria = %d, pFimMemoria = %d", proc->pIniMemoria, proc->pFimMemoria);
  if (ender < proc->pIniMemoria || ender > proc->pFimMemoria) {
    console_printf("ender < proc->pIniMemoria || ender > proc->pFimMemoria");
    return false;
  }
  if (tam > proc->pFimMemoria - proc->pIniMemoria) {
    console_printf("tam = %d", tam);
    console_printf("proc->pFimMemoria - proc->pIniMemoria = %d", proc->pFimMemoria - proc->pIniMemoria);
    console_printf("String maior do que a memoria disponivel para o processo");
    return false;
  }
  for (int indice_str = 0; indice_str < tam; indice_str++) {
    int caractere;
    if (mem_le(mem, ender + indice_str, &caractere) != ERR_OK) {
      return false;
    }
    if (caractere < 0 || caractere > 255) {
      return false;
    }
    str[indice_str] = caractere;
    if (caractere == 0) {
      return true;
    }
  }
  // estourou o tamanho de str
  return false;
}

// vim: foldmethod=marker