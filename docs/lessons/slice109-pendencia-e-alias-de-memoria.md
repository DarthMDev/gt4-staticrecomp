# Fatia109 — um quadro pode vir de uma interrupção anterior

Na referência, o jogo registra o tratador e habilita a causa2 com contador0.
A interrupção já está pendente nesse momento. O primeiro incremento aparece
antes do próximo quadro programado da fase de vídeo atual. Por isso não
basta contar quadros para alinhar o tempo: um quadro guest pode refletir
uma pendência criada antes da instalação do tratador.

O modelo também tem bit2 de status ligado antes do registro, mas nenhuma
entrada correspondente na fila. Sua primeira entrega desta janela vem de
uma cobrança posterior. Ainda precisamos descobrir a história desse bit:
inserir nova entrada somente porque o status está ligado poderia repetir
uma interrupção que já foi entregue e ficou sem acknowledgement no modelo.

O monitor da referência ensinou outra coisa: dois endereços do computador
podem apontar para os mesmos bytes PS2. Ler pelo backing mostra o contador
correto, mas observar escritas apenas naquele endereço não detecta as feitas
pela alias usada pelo compilador. PDB e registro host calibraram a alias;
endereço, largura, valor e bloco nativo identificaram quatro writes reais.

O PC guardado pelo compilador pode estar atrasado. O estado dizia4AB460,
mas o store comprovado era4AB4AC. No reset AOT, dizia107730, início do caller,
enquanto o SQ estava no memset5A4924. A identidade de uma instrução exige
qualificação própria, não simplesmente copiar o PC mostrado num snapshot.

Agora sabemos onde o quadro extra do modelo nasce antes da espera da mensagem.
O relógio e a IRQ seguem intactos enquanto atribuímos a pendência anterior;
essa evidência vai orientar uma correção verificável, não um wake artificial.
