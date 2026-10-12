# Fatia 107 — A fila passa corretamente; um quadro entra no meio

O contador do jogo não é uma régua que permite alinhar duas execuções só
porque ambas mostram o número3. É preciso acompanhar o que o jogo está
fazendo: qual rotina chamou o serviço, qual chamada anterior aparece na
pilha e qual thread está esperando na fila.

Nesta fatia acompanhamos o nó de espera da thread principal. Primeiro ele
fica na lista de quem aguarda receber uma mensagem. A atualização retira
esse mesmo nó e o coloca na lista de quem será acordado. As duas execuções
fazem essa passagem e chamam a mesma sequência de prioridade e despertar.
O endereço do nó e alguns IDs diferem entre os ambientes; sua função,
conteúdo e percurso identificam a operação correspondente.

A atualização entra com os dois contadores em3/3 nos dois ambientes. Durante
o trecho da fila, o modelo passa a4/3; a referência continua em3/3. Isso
explica por que a sincronização seguinte recebe entradas diferentes e dá
resultados diferentes. Corrigir a sincronização para esconder esse resultado
deixaria a origem do quadro extra sem explicação.

A conclusão confirmada é estreita: localizamos o trecho onde essa diferença
aparece e verificamos que o mesmo nó passa pelas listas. Ainda falta
identificar a instrução que alterou o contador e a interrupção responsável,
com seu estado de tempo. O retorno privado registrado no meio desse trecho
ajuda a investigar; sozinho, não informa a causa da interrupção.

Também verificamos o observador. A versão definitiva só lê endereços
qualificados de RAM e guarda uma identificação de diagnóstico. Depois de
10mil serviços, seu estado completo é igual ao da execução de produção.
Isso demonstra que essas leituras preservam aquele intervalo. A repetição
da referência conservou120de120estados comuns, inclusive fila, pilha,
temporizadores e contadores. Nenhum desses controles prova, por si só, que
todo o ambiente modelado já reproduz um PS2.

A checagem final também reforçou uma diferença prática: estado do jogo e
identificação da compilação são coisas distintas. Depois de restaurar a
configuração do compilador e reconstruir o executável, os54testes CTest e
97testes Python passaram. O estado salvo de10mil serviços continuou igual;
o checkpoint completo mudou apenas no campo que identifica o commit da
compilação. Por isso guardamos os artefatos anteriores e registramos a nova
identidade. Nesta instalação, a sessão de build precisa do Visual Studio
e da página de códigoUTF8 para detectar corretamente mensagens em português.

A próxima fatia vai olhar a escrita do contador e a origem da interrupção
nesse pequeno trecho. O jogo ainda não alcançou boot funcional; o relógio
e o retorno correto das interrupções continuam preservados até termos uma
mudança sustentada por essas observações.
