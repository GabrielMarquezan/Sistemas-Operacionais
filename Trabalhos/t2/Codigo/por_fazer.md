# Tarefas

## Parte I

- [X] crie um tipo de dados que é uma estrutura que contém as informações a respeito de um processo
- [X] crie uma tabela de processos, que é um vetor dessas estruturas
- [X] inicialize a primeira entrada nessa tabela (o primeiro processo) na criação do init
- [X] crie uma variável que designe o processo em execução. Faça de tal forma que tenha suporte a não ter nenhum processo em execução
- [X] altere `so_salva_estado_da_cpu` e `so_despacha` para salvar o estado do processador na tabela de processos (na entrada correspondente ao processo em execução) e para recuperar o estado do processador a partir da tabela
- [X] implemente a função do escalonador (`so_escalona`). Ela escolhe o próximo processo a executar (altera a variável que designa o processo em execução). Pode ser bem simples: se o processo que estava em execução estiver pronto continua sendo executado e se não, escolhe o primeiro que encontrar na tabela que esteja pronto. 
- [ ] implemente as chamadas de criação e morte de processos
- [ ] altere as chamadas de E/S, para usar um terminal diferente dependendo do pid do processo
- [ ] o pid do processo não é a mesma coisa que sua entrada na tabela: quando um processo termina sua entrada na tabela pode ser reutilizada por outro processo, o pid não, é uma espécie de número de série do processo.