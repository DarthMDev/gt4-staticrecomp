# Fatia 108 — de onde veio o quadro extra

Encontramos a escrita que faltava. Durante a primeira notificação da fila,
o modelo cobra seu milissegundo por chamada de serviço. Essa cobrança
completa o período de vídeo do modelo e dispara VBlank, a interrupção que
avisa ao jogo o início de um novo quadro. O manipulador do próprio jogo
incrementa o contador de 3 para 4, antes de a fila liberar a thread principal.

A fila ainda contém o mesmo nó da principal durante essa escrita. Assim,
o quadro extra desse trecho tem uma origem demonstrada no avanço de tempo
e na entrega da interrupção. A observação não encontrou uma duplicação do
nó nem precisou alterar a fila para explicar o incremento.

Na referência, a notificação e a liberação acontecem em outra fase local do
vídeo: o início de VBlank já passou e o evento seguinte é uma atualização
atrasada do GS. Os campos do temporizador da referência permitem distinguir
esse evento de um novo VBlank. O contador do jogo permanece em 3 nesse trecho.

Isso ainda não permite escolher um número menor de milissegundos por chamada.
Já havia uma diferença de fase quando a principal entrou na espera anterior.
Precisamos localizar essa diferença desde um ponto comum do código e incluir
o trabalho realizado entre as chamadas de serviço. Fazer os números dos
contadores coincidirem não demonstra que o tempo decorrido também coincide.

Há um detalhe útil para aprender como o jogo executa: a escrita do contador
está na instrução imediatamente após um desvio. Ela é o slot do desvio e
executa antes de o destino ser aplicado. O observador registrou a transferência
pendente antes da escrita e sua conclusão depois dela, incluindo exatamente
uma instrução de trabalho a mais. Isso confirmou a escrita efetiva.

Os registradores de exceção também precisam de contexto. O modelo instala
diretamente o manipulador, sem executar o vetor da BIOS. Por isso, foi o
contexto salvo pelo kernel que mostrou a interrupção sobre a thread de
atualização e seu ponto de retorno. Ler apenas o registrador EPC não teria
fornecido essa resposta.

O diagnóstico preservou o estado completo de produção em 10 mil serviços,
coincidiu com o interpretador e se repetiu exatamente. As duas capturas da
referência também coincidiram nos 120 estados comuns, inclusive os novos
campos de fase. O jogo ainda não chegou ao boot funcional. A próxima fatia
investiga o trecho anterior à primeira atualização para sustentar uma futura
mudança de relógio com evidência.
