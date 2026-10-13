# Slice113 — proporção, resolução e ritmo são coisas diferentes

Uma janela16:9 precisa de duas configurações coerentes. O Vulkan enquadra a
imagem no retângulo certo; o GT4 precisa selecionar sua própria opção16:9,
que atualiza a projeção e a posição do HUD. Esticar uma imagem4:3 não aumenta
o campo de visão. Por isso usamos a interface original do jogo e seu save,
além de conferir a flag aplicada à corrida no código original.

480p e1080i são modos de saída. Não significam necessariamente que o jogo
desenha640x480 ou1920x1080 pixels. DISPLAY/MAGH/MAGV definem a imagem ativa;
SMODE2 controla a leitura de linhas. A prévia de480p produziu640x448, e a de
1080i produziu576x960. O menu voltou ao modo normal após aceitar a prévia.
Uma corrida precisa ser observada separadamente: o nome do arquivo de teste
e uma conclusão normal do processo não comprovam seu modo de vídeo.

Os testes sintéticos verificam pixels, limites de memória e apresentação em
vários tamanhos. Não demonstram fidelidade temporal de campos entrelaçados
nem que o jogo já renderiza internamente emHD/4K. Essas melhorias precisarão
qualificar a escala, os efeitos e a composição de interface.

144FPS também exige trabalho diferente: a apresentação poderá ter cadência
própria enquanto a simulação mantém100% da velocidade. Repetir a mesma imagem
em144Hz não cria144 imagens novas; acelerar o relógio do jogo pode acelerar
física e outros sistemas. O plano futuro deve identificar essas dependências
e testar a velocidade efetiva. O dono pediu registrar a lentidão3D e o frametime
por enquanto; esta fatia não altera sincronização nem inicia otimização.
