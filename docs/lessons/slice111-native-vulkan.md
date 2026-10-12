# EXPLAIN — O que o Vulkan já faz

O código estático do jogo continua executando no processador. Quando ele
envia desenhos ao GS, o novo backend agrupa os comandos e a GPU executa
os shaders: escreve a memória gráfica, lê texturas e produz os pixels.
A janela usa essa imagem diretamente, mantendo CPU e GPU com papéis claros.

Usar um framebuffer já desenhado pela CPU e apenas mostrá-lo com Vulkan
não provaria um renderer Vulkan. Aqui os testes comparam a memória escrita
pela GPU com um segundo desenho CPU e os logs confirmam raster CPU desligado
durante o jogo. Capturamos também a janela com a seleção de pista e carro3D.

A primeira versão espera cada trabalho terminar. Isso simplifica o controle
de memória e permite investigar diferenças antes de otimizar. Esperar a fila
ficar vazia não basta para reutilizar tudo relacionado à apresentação: usamos
uma fence específica para saber quando seus recursos estão seguros.

Chegar ao FordKa confirma progresso real de execução e desenho. Não confirma
que uma corrida inteira funciona ou que os pixels reproduzem exatamente um
PS2. A comparação com o renderer CPU mostra uma diferença já no quadro400;
a próxima investigação deve localizar sua primeira causa, mantendo esse
limite explícito. Produção MSVC e seus contratos continuam preservados.
