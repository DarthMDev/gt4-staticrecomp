# Slice103 — um retorno pequeno pode bloquear o jogo inteiro

O jogo aumenta temporariamente a prioridade de quem envia uma mensagem.
Guarda o valor que a chamada devolve e depois o usa para restaurar a
prioridade. Na referência,24->1 devolve24. No modelo, a mesma chamada
devolvia sempre0: a restauração colocou a thread de atualização em0.
Ela então continuou tomando o processador da principal.

Nove snapshots do modelo e uma captura independente com repetição60/60
demonstram a origem do valor. Não bastava olhar que a principal estava
acordada: ela podia estar READY sem executar, ou RUN dentro de um handler.
Na referência, seguimos o retorno real até a próxima espera de RPC_BIND.

Esta etapa confirma um erro de contrato anterior à discussão do relógio.
A correção ainda precisa testes e boot fresh para mostrar seu efeito real.
Não prova menu, imagem ou fidelidade completa do PS2. O tutorial também
não amplia dois casos bem medidos para variantes de serviço não observadas.
