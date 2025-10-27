// so.c
// sistema operacional
// simulador de computador
// so25b

// ---------------------------------------------------------------------
// INCLUDES {{{1
// ---------------------------------------------------------------------

#include "so.h"
#include "dispositivos.h"
#include "err.h"
#include "irq.h"
#include "memoria.h"
#include "programa.h"
#include "processo.h"
#include "lista_processos.h"
#include "fila_prioridade.h"
#include "fila_round_robin.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>


// ---------------------------------------------------------------------
// CONSTANTES E TIPOS {{{1
// ---------------------------------------------------------------------

// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 50   // em instruções executadas
#define ESCALONADOR_ATIVO ESC_ROUND_ROBIN
#define CAP_MAX_HEAP 100000

struct so_t {
  cpu_t *cpu;
  mem_t *mem;
  es_t *es;
  console_t *console;
  bool erro_interno;
  processo_t* processoCorrente;
  processo_t** processosCPU;
  int indiceProc;
  int qtdProc;
  bool terminais_ocupados[4]; // 0 = TERM_A, 1 = TERM_B, ...
  fila_rr* fila_proc_prontos;
  fila_prioridade* fila_proc_prioridade;
  int cont_esperando;
  int n_processos_tabela;

  // t2: tabela de processos, processo corrente, pendências, etc
};


// função de tratamento de interrupção (entrada no SO)
static int so_trata_interrupcao(void *argC, int reg_A);
static void so_trata_espera_proc_morrer(so_t* self, int pid_morto);
static void so_chamada_mata_proc(so_t *self);

// funções auxiliares
// carrega o programa contido no arquivo na memória do processador; retorna end. inicial
static int so_carrega_programa(so_t *self, char *nome_do_executavel);
// copia para str da memória do processador, até copiar um 0 (retorna true) ou tam bytes
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender);

static void so_recalcula_prioridade(processo_t* proc) {
  if (proc == NULL) return;
  float t_exec = QUANTUM - proc->quantum;
  proc->prioridade = (proc->prioridade + (t_exec / QUANTUM)) / 2.0;
}

static void so_bloqueia_processo(so_t *self) {
    if (self->processoCorrente != NULL) {
      if(ESCALONADOR_ATIVO == ESC_PRIORIDADE) so_recalcula_prioridade(self->processoCorrente);

      self->processoCorrente->estadoCorrente = BLOQUEADO;
      self->processoCorrente = NULL; 
    }
}

//--------------------------------------------------------------------
// PROCESSOS 
//--------------------------------------------------------------------

bool mata_processo(so_t* self, int pid) {
    int pid_alvo = pid;
    if(pid_alvo == 0) {
      pid_alvo = self->processoCorrente->pid;
    }
    console_printf("Estamos removendo PID=%d",pid_alvo);
    processo_t* proc = busca_remove_proc_tabela(self->processosCPU, pid_alvo);
    if(proc == NULL) return false;

    //mata os filhos
    for (int i=0; i < self->n_processos_tabela; i++) {
      if (self->processosCPU[i] == NULL) continue;
      if (self->processosCPU[i]->parentPID == proc->pid) {
        mata_processo(self, self->processosCPU[i]->pid);
        int pid_morto = self->processosCPU[i]->pid;
        so_trata_espera_proc_morrer(self, pid_morto);
      }
    }

    int pid_morto = proc->pid;
    so_trata_espera_proc_morrer(self, pid_morto);

    fila_rr_remove_pid(self->fila_proc_prontos, proc->pid);
    console_printf("MORRI! PID=%d", proc->pid);
    if (pid_morto == self->processoCorrente->pid) self->processoCorrente = NULL;
    free(proc);
    self->n_processos_tabela--;
    return true;
}

static processo_t* busca_processo_escrita_pendente(so_t* self, int terminal){
  for (int i = 0; i < MAX_PROC; i++) {
    if (self->processosCPU[i] == NULL) continue;
    if (self->processosCPU[i]->terminal == terminal
        && self->processosCPU[i]->esperando_escrita) {
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
          return self->processosCPU[i];
        }
  }
  return NULL;
}

// insere um novo processo na tabela de processos
bool so_insere_processo(so_t* self, processo_t* proc) {
  if (proc == NULL) return false;
  // insere o processo na primeira posição vazia da tabela
  for (int i = 0; i < MAX_PROC; i++) {
    if (self->processosCPU[i] == NULL) {
      console_printf("Inseri o processo %d na posição %d", proc->pid, i);
      self->processosCPU[i] = proc;
      return true;
    }
  }
  // tabela cheia, não conseguiu inserir o processo
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
  proc->regA = dado;
}

static void escreve_dado_tela(so_t* self, processo_t* proc) {
  if (es_escreve(self->es, proc->terminal + TERM_TELA, proc->regX) != ERR_OK) {
    console_printf("SO: problema no acesso à tela");
    self->erro_interno = true;
    return;
  }
  proc->regA = 0;
}


// ---------------------------------------------------------------------
// CRIAÇÃO {{{1
// ---------------------------------------------------------------------

so_t *so_cria(cpu_t *cpu, mem_t *mem, es_t *es, console_t *console)
{
  so_t *self = malloc(sizeof(*self));
  if (self == NULL) return NULL;
  self->processoCorrente=NULL;
  self->processosCPU = cria_vetor_processos();
  memset(self->terminais_ocupados, 0, sizeof(self->terminais_ocupados));
  self->cont_esperando = 0;
  self->cpu = cpu;
  self->mem = mem;
  self->es = es;
  self->console = console;
  self->erro_interno = false;
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

  if (self->processoCorrente != NULL && self->processoCorrente->estadoCorrente == EXECUTANDO) {
    self->processoCorrente->estadoCorrente = PRONTO;
  }

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
  if (mem_le(self->mem, CPU_END_A, &self->processoCorrente->regA) != ERR_OK
      || mem_le(self->mem, CPU_END_PC, &self->processoCorrente->regPC) != ERR_OK
      || mem_le(self->mem, CPU_END_erro, &self->processoCorrente->regERRO) != ERR_OK
      || mem_le(self->mem, 59, &self->processoCorrente->regX) != ERR_OK) {
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
          proc->estadoCorrente = PRONTO;
          proc->esperando_leitura = false;

          if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) fila_rr_insere_fim(self->fila_proc_prontos, proc);
          else inserir(self->fila_proc_prioridade, proc);
        
        }
      } else if (escrita) {
        //console_printf("ENTREI PARA VER SE ACHO AMIGUINHOS");
        proc = busca_processo_escrita_pendente(self, i*4);
        if (proc != NULL) {
          //console_printf("AMIGINHOS!!! PID=%d",proc->pid);
          escreve_dado_tela(self, proc);
          proc->estadoCorrente = PRONTO;
          proc->esperando_escrita = false;

          if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) fila_rr_insere_fim(self->fila_proc_prontos, proc);
          else inserir(self->fila_proc_prioridade, proc);
        }
      }
      // libera o terminal
      self->terminais_ocupados[i] = !(escrita && leitura);
    }
  }
}

static void so_escalona(so_t *self)
{
  // escolhe o próximo processo a executar, que passa a ser o processo
  //   corrente; pode continuar sendo o mesmo de antes ou não
  // t2: na primeira versão, escolhe um processo pronto caso o processo
  //   corrente não possa continuar executando, senão deixa o mesmo processo.
  //   depois, implementa um escalonador melhor
  // --- MODIFICAÇÃO RR ---
  // Lógica de escalonamento Round-Robin

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
      //console_printf("ESCALONANDO: PID=%d ", proximo->pid);      
      self->processoCorrente = proximo;
      self->processoCorrente->estadoCorrente = EXECUTANDO;
      self->processoCorrente->quantum = QUANTUM;
    } else {
      console_printf("SO: Fila vazia");
      self->processoCorrente = NULL;
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

  self->processoCorrente->estadoCorrente = EXECUTANDO;

  if (mem_escreve(self->mem, CPU_END_A, self->processoCorrente->regA) != ERR_OK
      || mem_escreve(self->mem, CPU_END_PC, self->processoCorrente->regPC) != ERR_OK
      || mem_escreve(self->mem, CPU_END_erro, self->processoCorrente->regERRO) != ERR_OK
      || mem_escreve(self->mem, 59, self->processoCorrente->regX) != ERR_OK) {
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
      so_trata_reset(self);
      break;
    case IRQ_SISTEMA:
      so_trata_irq_chamada_sistema(self);
      break;
    case IRQ_ERR_CPU:
      so_trata_irq_err_cpu(self);
      break;
    case IRQ_RELOGIO:
      so_trata_irq_relogio(self);
      break;
    default:
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
  int ender = so_carrega_programa(self, "trata_int.maq");
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

  processo_t* init = cria_processo(NULL);
  
  // coloca o programa init na memória
  ender = so_carrega_programa(self, "init.maq");
  if (ender != 100) {
    console_printf("SO: problema na carga do programa inicial");
    self->erro_interno = true;
    return;
  }
  init->regPC = ender;
  if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) fila_rr_insere_fim(self->fila_proc_prontos, init);
  else inserir(self->fila_proc_prioridade, init);
  so_insere_processo(self, init);
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
  err_t err = self->processoCorrente->regERRO;
  console_printf("ERRO DE CPU: PID=%d, Codigo de Erro=%d (%s)", 
               self->processoCorrente->pid, err, err_nome(err));
  // se o processo tenta acessar um discpositivo ocupado, o processo bloqueia
  if (err == ERR_OCUP) {
    self->processoCorrente->estadoCorrente = BLOQUEADO;
    self->processoCorrente = NULL;
    return;
  }
  if (err == ERR_INSTR_INV || err == ERR_END_INV ||
      err == ERR_INSTR_PRIV || err == ERR_OP_INV ||
      err == ERR_DISP_INV) {
      so_chamada_mata_proc(self);
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
  
    if(ESCALONADOR_ATIVO == ESC_PRIORIDADE) so_recalcula_prioridade(self->processoCorrente);
    self->processoCorrente->estadoCorrente = PRONTO;
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
  int id_chamada = self->processoCorrente->regA;
  //console_printf("SO: chamada de sistema %d", id_chamada);
  switch (id_chamada) {
    case SO_LE:
      so_chamada_le(self);
      break;
    case SO_ESCR:
      so_chamada_escr(self);
      break;
    case SO_CRIA_PROC:
      so_chamada_cria_proc(self);
      break;
    case SO_MATA_PROC:
      so_chamada_mata_proc(self);
      break;
    case SO_ESPERA_PROC:
      so_chamada_espera_proc(self);
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
  // então bloqueia o processo que fez a chamada
  le_dado_teclado(self, self->processoCorrente);
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
  processo_t* novo_proc = cria_processo(self->processoCorrente);
  // em X está o endereço onde está o nome do arquivo
  int ender_proc;
  ender_proc = self->processoCorrente->regX;
  char nome[100];
  if (copia_str_da_mem(100, nome, self->mem, ender_proc)) {
    int ender_carga = so_carrega_programa(self, nome);
    if (ender_carga > 0) {
      // t2: deveria escrever no PC do descritor do processo criado
      // se aconteceu algum erro ao carregar o programa da memória, o programa morre
      // então o processo também morre
      // deveria escrever -1 (se erro) ou o PID do processo criado (se OK) no reg A
      //   do processo que pediu a criação
      novo_proc->regPC = ender_carga;
      self->processoCorrente->regA = novo_proc->pid;
      if (!so_insere_processo(self, novo_proc)) {
        mata_processo(self, novo_proc->pid);
        self->processoCorrente->regA = -1;
      }

      if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) fila_rr_insere_fim(self->fila_proc_prontos, novo_proc); // Insere no fim da fila do escalonador
      else inserir(self->fila_proc_prioridade, novo_proc);

      return;
    } else {
      mata_processo(self, novo_proc->pid);
      self->processoCorrente->regA = -1;
    }
  } else {
    mata_processo(self, novo_proc->pid);
    self->processoCorrente->regA = -1;
  }
}

static void so_trata_espera_proc_morrer(so_t* self, int pid_morto) {
  if (pid_morto == 0) return;
  for(int i = 0; i < MAX_PROC; i++) {
    processo_t* proc = self->processosCPU[i];
    
    if((proc != NULL && proc->estadoCorrente == BLOQUEADO) && proc->esperando_processo == pid_morto){
      proc->estadoCorrente = PRONTO;
      proc->esperando_processo = 0;
      proc->regA = 0;

      if(ESCALONADOR_ATIVO == ESC_ROUND_ROBIN) fila_rr_insere_fim(self->fila_proc_prontos, proc); // Insere no fim da fila do escalonador
      else inserir(self->fila_proc_prioridade, proc);
    }
  }
}

static void so_chamada_mata_proc(so_t *self) {
  // tratar aqui a pendência de quando um processo está esperando outro morrer
  console_printf("QUERO MORRER PID=%d", self->processoCorrente->regX == 0 ? self->processoCorrente->pid : self->processoCorrente->regX);
  if (mata_processo(self, self->processoCorrente->regX)){
    if (self->processoCorrente != NULL) {
        self->processoCorrente->regA = 0;
    }
  }
  else {
    self->processoCorrente->regA = -1; // Não conseguiu matar o processo?
    return;
  }
}

static void so_chamada_espera_proc(so_t *self)
{
  int pid_alvo = self->processoCorrente->regX;
  if (pid_alvo == self->processoCorrente->pid) {
      self->processoCorrente->regA = -1; // Esperando por si mesmo
      return;
  }

  processo_t* proc_alvo = busca_proc_na_tabela(self->processosCPU, pid_alvo);
  if(proc_alvo == NULL) {
    for(int x=0;x<5;x++){
      console_printf("Meu PID=%d", (self->processosCPU[x]!=NULL)?self->processosCPU[x]->pid:-1);      
    }
    console_printf("Processo PID=%d quer esperar o %d que já morreu e eu n gostei!!!!",self->processoCorrente->pid,pid_alvo);
    self->processoCorrente->regA = -1; // Processo não existe (ou já morreu)
    return;
  }

  console_printf("Processo PID=%d quer esperar o %d",self->processoCorrente->pid,pid_alvo);
  self->processoCorrente->esperando_processo = pid_alvo;
  so_bloqueia_processo(self);
}


// ---------------------------------------------------------------------
// CARGA DE PROGRAMA {{{1
// ---------------------------------------------------------------------

// carrega o programa na memória
// retorna o endereço de carga ou -1
static int so_carrega_programa(so_t *self, char *nome_do_executavel)
{
  // programa para executar na nossa CPU
  programa_t *prog = prog_cria(nome_do_executavel);
  if (prog == NULL) {
    console_printf("Erro na leitura do programa '%s'\n", nome_do_executavel);
    return -1;
  }

  int end_ini = prog_end_carga(prog);
  int end_fim = end_ini + prog_tamanho(prog);

  for (int end = end_ini; end < end_fim; end++) {
    if (mem_escreve(self->mem, end, prog_dado(prog, end)) != ERR_OK) {
      console_printf("Erro na carga da memória, endereco %d\n", end);
      return -1;
    }
  }

  prog_destroi(prog);
  //console_printf("SO: carga de '%s' em %d-%d", nome_do_executavel, end_ini, end_fim);
  return end_ini;
}


// ---------------------------------------------------------------------
// ACESSO À MEMÓRIA DOS PROCESSOS {{{1
// ---------------------------------------------------------------------

// copia uma string da memória do simulador para o vetor str.
// retorna false se erro (string maior que vetor, valor não char na memória,
//   erro de acesso à memória)
// t2: deveria verificar se a memória pertence ao processo
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender)
{
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