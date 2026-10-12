# Slice104 — corrigir um erro sem declarar o boot resolvido

Corrigimos o retorno que o jogo usa para desfazer uma mudança temporária
de prioridade. Antes ele restaurava0; agora restaura24, como na referência.
O retorno também precisa sobreviver quando a chamada troca a thread em
execução: por isso escrevemos o valor antes de salvar o contexto.

Os testes passaram, mas o boot novo mostrou a principal ainda esperando.
O relógio continua cobrando1ms por serviço, e a thread de atualização ainda
consegue retomá-la antes da continuação normal. Portanto, corrigir a
prioridade foi necessário, mas não bastou para resolver a próxima fronteira.

O modo estrito também revelou um pedido ao IOP que o modelo ainda responde
sem contrato validado. A comparação com nosso interpretador não detecta
isso porque ambos usam o mesmo modelo. Precisamos observar a resposta
independente, implementar o protocolo comprovado e repetir o boot.
