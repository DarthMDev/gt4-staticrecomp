# Fatia102 — medir o tempo completo e reconhecer a fase do boot

O relógio do PCSX2 pode estar atrasado em relação ao trecho que acabou de
executar. Seu recompilador soma parte do custo ao terminar um bloco. Parar
na entrada de um serviço e na entrada do retorno do BIOS pode deixar esse
custo de fora. Por isso capturamos também a primeira escrita do relógio
após cada retorno e conferimos a instrução responsável com outra ferramenta.

Nas duas esperas de2000us observadas, o intervalo entre as fronteiras do
vetor e do contexto escolhido foi1110 ciclos do modelo do PCSX2. Quase
toda a espera ainda estava disponível. Isso qualifica uma observação da
referência; não fornece um custo universal de serviço nem uma medição do PS2
físico. A nossa espera problemática é outra:1000us dentro do loop de atualização.

Essa espera de1000us não apareceu na coleta limitada. A referência passou
por89 esperas de quadro. Também vimos uma mensagem acordar o contexto da
principal e uma espera de quadro selecionar sua continuação. No nosso modelo,
o loop já está atrasado e a cobrança do serviço consome o1ms antes que a
principal execute qualquer instrução. Precisamos comparar o caminho anterior
a essa diferença, além do contador do relógio.

Uma repetição com menos pontos de observação confirmou os mesmos54 estados
iniciais, inclusive argumentos, relógio e contexto. Isso aumenta a confiança
nessa sequência, sem provar equivalência de toda a máquina.

A próxima fatia segue a principal após a mensagem: qual trabalho ela executa
e onde volta a bloquear? Essa resposta ajuda a distinguir diferença de fase,
serviço e tempo. O boot do nosso projeto ainda não avançou nesta fatia;
ganhamos evidência para escolher a correção sem fabricar progresso.
