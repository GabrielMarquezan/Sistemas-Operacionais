#include "processos.h"

enum estadoProcesso {
    PRONTO,
    BLOQUEADO,
    EXECUTANDO
};

struct processos_t {
    int pid; //número do processo
    // estado da CPU
    int regPC, regA, regX, regERRO;
    estado_p estadoProcesso;
    int pIniMemoria;
};