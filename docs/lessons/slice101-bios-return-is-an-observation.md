# Slice 101 — observar o retorno antes de escolher uma regra

## Objetivo e motivação

Na fatia100, o interpretador do PCSX2 parava mais tarde dentro do BIOS,
sem mostrar a primeira exceção. Sabíamos que havia um problema, mas ainda
não sabíamos se a chamada era atendida nem para onde o BIOS tentava voltar.
Escolher uma regra de retomada nesse ponto seria um palpite.

A [evidência da fatia101](../reverse-engineering/slice101-interpreter-trap-observation.md)
usa os símbolos oficiais do mesmo executável, conferidos por hash e pela
identidade interna do PDB. Um observador original acompanha escritas em PC,
EPC e Cause no processo isolado. Isso permite ver decisões do interpretador
mesmo quando suas paradas por endereço guest não estão habilitadas na build.

## Um exemplo trabalhado

O programa original coloca um BEQL tomado em `B=0x100060` e um SYSCALL
GetThreadId em `S=B+4=0x100064`. Antes da chamada, `v1=0x2F` identifica o
serviço e `v0=-1` permite distinguir um resultado real.

1. O interpretador entra no SYSCALL com `branch=1`. Seu campo PC já vale
   `0x100068`: ele foi incrementado antes de executar a palavra em S.
2. A exceção seleciona vetor `0x80000180`, EPC=B, BD=1 e EXL=1. O desvio
   pendente é limpo; o destino tomado não substitui esse vetor.
3. O BIOS atende a chamada e produz `v0=1`. Ele muda EPC para S e usa
   `v1` no cálculo de despacho: `0x2F << 2 = 0xBC`.
4. A instrução ERET está em `0x80000328`; na entrada da função host,
   PC já vale `0x8000032C`. Ao retornar da função, PC=S e EXL=0.
5. A palavra SYSCALL em S executa novamente, agora com `v1=0xBC`, sem o
   desvio pendente. A segunda exceção tem EPC=S e BD=0.
6. O despacho seguinte escala `0xBC << 2 = 0x2F0`; o salto observado
   seleciona PC=0 e vem uma exceção de TLB, Cause=8/EPC=0. A janela limitada
   termina em código BIOS, sem o marcador de continuação do programa.

**Confirmado para essa referência:** a primeira chamada foi atendida e o
BIOS escolheu o próprio slot como retorno. **Alta confiança:** o cálculo
de tabela da segunda chamada levou ao destino zero. Não lemos os bytes
da entrada da tabela, portanto essa leitura específica não está comprovada.

O controle simples e os caminhos comuns não tomados distinguem esse caso:
retorno em `S+4=0x100068`, EXL=0, `v0=1` e marcador posterior `s1=0x11`.
Os controles com uma soma no slot separam seu efeito da chamada; BEQL falso
tem a anulação prevista pela fonte. Os resultados da matriz de treze controles
e suas identidades estão na evidência, não neste índice didático.

## O que cada observação significa

| Observação | O que permite afirmar | Limite |
|---|---|---|
| Escrita host em PC/EPC/Cause | O campo recebeu aquele valor naquele ponto | Não conta instruções guest concluídas |
| PC escrito dentro de ERET | O retorno começou a selecionar um endereço | O status pode ainda não ter sido atualizado |
| Retorno da função host ERET | PC e status depois de seus efeitos | A próxima palavra guest ainda não foi executada |
| Marcador guest posterior | A continuação produziu um efeito real | Não prova fidelidade ao console físico |
| Janela de falha limitada | A cadeia observada até aquele limite | Não prova falha irreversível |

## Caminhos rejeitados e consequência

Não usamos Step Into como traço de uma palavra. Os primeiros pilotos também
não viraram evidência final: uma suposição errada sobre byte host foi
recusada antes da instrumentação, e o observador inicial precisava de guards
para não consumir exceções host desconhecidas. A matriz foi repetida depois
dessas correções nos treze caminhos syscall/efeito. Capturas recusadas foram
preservadas separadamente.

Esses resultados não escolhem uma política de retomada para o GT4Recomp.
A divergência de serviço falso em slot likely, nativo5/1 versus
interpretador1/0, envolve outro contrato: atendimento e estado pendente no
nosso runtime. Copiar a falha da referência ou escolher S+4 por conveniência
não resolve esse contrato. A [decisão0041](../decisions/0041-qualify-reference-branch-events.md)
continua valendo, complementada pela
[decisão0042](../decisions/0042-symbol-qualified-interpreter-traps.md).

O resultado prático é reduzir uma incerteza: agora conhecemos a sequência
que causou a parada daquele controle. O relógio do jogo continua sem uma
medição correspondente do intervalo real de atualização1000us/principal.

## Perguntas de compreensão

- Por que `eret_before.pc=0x8000032C` não muda o endereço da instrução ERET?
- Qual observação mostra que a primeira chamada foi atendida?
- Por que a segunda chamada usa `v1=0xBC`, e não `0x2F`?
- O que falta para transformar essa referência em uma política de produção?
