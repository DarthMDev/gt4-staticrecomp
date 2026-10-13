# Slice112 — Por que conectar áudio e controle exige contratos pequenos

O jogo já produz som e consulta um controle de PS2. O trabalho desta fatia é
ligar essas duas interfaces ao Windows sem mudar o tempo ou a lógica do jogo.

Um bloco de áudio contém32 instantes, cada um com uma amostra esquerda e outra
direita. A memória pertence ao produtor e deixa de valer depois da chamada.
Por isso copiamos imediatamente para buffers nossos. O Windows só devolve a
posse quando marca o cabeçalho como concluído. Um evento avisa que algo mudou;
ele não conta quantos buffers terminaram. Verificar todos os cabeçalhos evita
perder notificações agrupadas. Cancelar o dispositivo devolve a posse, mas não
prova que as amostras foram tocadas: conclusão e cancelamento têm contadores
separados. Ao sair, desligamos primeiro a função de entrega do jogo.

No controle, nomes parecidos não garantem o mesmo formato. O PS2 espera os eixos
direitos antes dos esquerdos e usa128 como centro. O eixo vertical do XInput
tem o sinal contrário; a inversão precisa ocorrer em inteiro maior para aceitar
-32768. Pressão é indexada pelo botão PS2. Motores também são diferentes:
o pequeno liga/desliga, enquanto o grande tem intensidade. Ao perder foco ou
desconectar, liberamos botões e motores para não deixar uma ação presa.

Os testes sem controle físico verificam essa tradução, mas não comprovam que
um aparelho recebeu vibração. Da mesma forma, um WAV com som e buffers consumidos
pelo Windows comprovam geração/entrega, mas não que alguém ouviu áudio contínuo.
Se o jogo produz som mais lentamente que o dispositivo consome, aparecem
intervalos vazios. Trocar o tempo do jogo por uma suposição esconderia o problema;
medir e corrigir a causa preserva a qualidade da recompilação.

O cartão usado no teste manual é uma cópia privada independente. Guardar uma
cópia antes de cada abertura permite inspecionar ou recuperar mudanças. A trava
precisa permanecer no processo que escreve, pois fechar somente o lançador não
garante que o jogo terminou. Nenhum arquivo original de evidência é um save de
uso cotidiano.
