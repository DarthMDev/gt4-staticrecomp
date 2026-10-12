# Slice110 — por que um runtime coerente acelera o boot

Contar funções não mede quanto falta para o jogo funcionar. Traduzir o código
da CPU principal resolve um lado; esse código ainda depende de disco, CPU IOP,
módulos, transportes, timers, controle, cartão e caminhos gráficos coerentes.
O estudo0051 compilou esse conjunto externo completo, mantendo uma identidade
separada da produção. Não bastaria importar isoladamente respostas de syscalls.

Nesta fatia, o jogo respondeu à entrada, escreveu um nome, formatou cartão
privado e persistiu seus dados. Uma nova execução relendo uma cópia chegou
ao menu Arcade e à escolha de circuitos/carros. As imagens são saída guest
desenhada pelo software GS do doador. Isso comprova uma cadeia funcional
mais ampla que milhares de chamadas de serviço sem erro.

Há três verificações diferentes. Hashes identificam exatamente o que foi
executado. Capturas antes/depois e comandos qualificam os efeitos de entrada.
Um teste independente de contratos ainda é necessário para afirmar fidelidade
a PS2; dois motores que compartilham serviços podem compartilhar o mesmo erro.
Não misturamos checkpoints do piloto e de produção, nem adotamos seus retornos
padrão de sucesso como implementação correta.

O exit0 do processo também não basta: o runtime externo pode devolvê-lo depois
de faltar código guest. O launcher verifica o motivo e o horizonte efetivamente
encerrado. Mesmo esse gate só qualifica execução; menu precisa ser observado,
e cartão persistido precisa ser relido em outra execução. Corrida, estabilidade
ampla, áudio e Vulkan exigem próximos testes próprios.
