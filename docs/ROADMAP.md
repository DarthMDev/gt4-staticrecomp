# Plano: velocidade, renderização e migração seletiva para código nativo

2026-10-13, após a fatia114/commit e431fb3. Planejamento solicitado pelo dono;
nenhuma implementação, perfil de desempenho ou nova corrida nesta revisão.

## Recomendação

Manter a base híbrida como referência funcional e mudar por partes. Eliminar
toda a emulação não é pré-requisito para separar velocidade e FPS. Recompilar
VU/IOP e separar atualização/renderização resolvem problemas diferentes.
Priorizar capacidade de reprodução do build, medidas confiáveis quando
autorizadas, controle de tempo e separação da apresentação. Depois, investigar
o loop do jogo necessário para gerar imagens adicionais. Migrar subsistemas
quando existir custo demonstrado ou uma dependência concreta da nova arquitetura.

## Evidência atual: Confirmed

Código donor congelado em `private/references/ps2-web-recomp`, commit
1b4808793a12c45125262fd2d5f50a9ec9f7c020; fontes locais no commit e431fb3.
Método: leitura estática das fontes e dos manifests114, sem executar o jogo.

- `runtime/src/system.cpp:85` calcula orçamento de VBlank como relógio EE
  dividido por60 NTSC/50 PAL. É a política atual do runtime, não uma prova da
  cadência exata do hardware ou da física interna do GT4.
- `system.cpp:127–141` chama o callback do host dentro do evento VBlank,
  antes de sinalizar hardware/IOP/kernel e agendar a próxima ocorrência.
- `native/window.cpp:80–106` coleta input e chama `gs::present()` nesse callback.
  `window.cpp:173–174` explicita uma thread e apresentação FIFO.
- `native/native_vulkan.cpp:693` seleciona FIFO. `:858–865` espera a conclusão
  real da apresentação; `:509–513` também espera submissões GPU. Esses pontos
  bloqueiam execução; não medimos sua parcela do custo em corrida.
- `system.cpp:329–336` avança IOP a partir do tempo guest, e `:366–384` executa
  EE/eventos. O áudio SPU2 nasce de jobs do IOP; `native/host_io.cpp:219–273`
  pode aguardar buffers do dispositivo Windows. Áudio é outra fonte de espera.
- VU é interpretada nesta configuração; existe despacho para microprogramas
  compilados em `runtime/src/vu_micro.cpp:788–812`, com registro/cache por
  conteúdo em `:817–854`. `vu_jit_web.cpp:18–30` desativa JIT fora de Emscripten.
  O caminho compilado examinado é VU1; suporte equivalente para VU0 exige audit.
- IOP interpreta instruções e já substitui imports por serviços HLE.
  Ver `ARCHITECTURE.md` para limites e referências adicionais.
- O donor já oferece fila GS e consumidor em `runtime/src/gs/gs.cpp:963–1025`,
  usados pelo host original em `host/window.cpp:310–341`, mas não ativados no
  adapter Win32 atual. `post_present()` limita duas apresentações pendentes;
  reutilizar literalmente ainda pode bloquear o produtor. `start_thread()`
  inicializa a fila; o host é quem cria a thread. Avaliar/reusar antes de criar
  infraestrutura nova, preservando GIF/reset/download/checkpoint em ordem.

## Hipóteses e desconhecidos

**Hypothesis:** interpretar VU pode ter custo significativo em cenas3D.
A queda relatada quando aparecem carros/circuitos também é compatível com
trabalho GS/compute, sincronização ou código EE. O relato não isola a causa.
**Unknown:** custo EE/VU/IOP/GS/áudio, tempo bloqueado, cadência física real,
rotinas que atualizam estado e constroem cena, dependências entre desenho e
gameplay, fidelidade temporal. Não chamar VU de gargalo confirmado.

Recompilar VU/IOP gera código x86-64 mantendo sua semântica PS2. Isso pode
acelerar execução sem eliminar modelos de hardware ou alterar vínculo com FPS.
Substituir serviços por APIs do PC exige contratos separados para DMA/IRQ/RPC,
áudio e cartão; o fato de serem software não significa que precisem ser removidos.

## O resultado que queremos

Três cadências devem ser explícitas: tempo da simulação, produção de cenas e
apresentação no monitor. O relógio monotônico do host rege a velocidade alvo;
os eventos guest conservam ordem/custos e a física mantém passos originais
qualificados. A taxa do monitor não define quantos passos físicos avançam.

| Situação desejada | Comportamento |
|---|---|
| Apresentação30FPS | Simulação continua na cadência original; apresenta menos imagens |
| Apresentação60FPS | Simulação original, apresentação correspondente |
| Apresentação144FPS | Simulação original; imagens novas usam estados visuais interpolados |
| Monitor144Hz com imagens repetidas | Pode preservar velocidade, mas ainda não cumpre144 imagens diferentes por segundo |
| CPU incapaz de simular em tempo real | Não há promessa de100% velocidade; precisa otimizar ou reduzir trabalho independente da simulação |

Não aumentar VBlank/relógio para144 nem trocar indiscriminadamente o delta da
física. Exemplos usam60 apenas para explicar; qualificar a cadência original
antes de congelar um contrato. Medir100% como tempo de jogo/tempo real, além
de contagens de passos e eventos, não como valor do overlay FPS.

## Etapas propostas e critérios de conclusão

### 1. Tornar o código do jogo recompilável de ponta a ponta

Documentar/automatizar geração EE e registro da biblioteca a partir dos inputs
pinados, compilação e integração em diretório novo. Hoje o build reutiliza
`ps2game.lib` privada com hash fixo. Não basta editar adapter para modificar
o loop do GT4. Não exige decompilar o jogo inteiro: traduzido legível e hooks
cirúrgicos são suficientes inicialmente.

Concluir quando um produtor novo reproduz comportamento nos controles de boot,
menu e corrida existentes, identifica todas as ferramentas/inputs e não depende
de uma biblioteca sem receita de geração. Não exigir binário byte-idêntico se
metadados de build mudarem; explicar diferença e comparar comportamento.

### 2. Estabelecer referência funcional e de tempo, quando autorizada

Mesmos inputs/carro/pista/controles, cartão copiado e janelas de observação.
Registrar velocidade guest/real, passos do jogo, input, amostras áudio e custo
EE/VU/IOP/GS; timestamps GPU e esperas de apresentação/readback/áudio. Usar
percentis de frametime e registros coerentes, não uso agregado da CPU/GPU.
Controles sem apresentação/readback devem manter execução e trabalho necessário
do GS; não desligar GPU arbitrariamente e presumir equivalência.

Concluir com uma atribuição suficiente para escolher a primeira mudança,
controles repetíveis e limites explícitos. A prioridade anterior de apenas
registrar problemas permanece: esta etapa ainda não foi executada.

### 3. Desacoplar apresentação do avanço guest

Separar callback VBlank/input/eventos da operação de exibição, preservar a
ordem guest atual e controlar avanço por tempo monotônico. Fazer primeiro a
menor separação segura; adicionar thread só se necessária. Usar imagens com
propriedade/lifetime explícitos e fila limitada; preservar trabalho GS do qual
o jogo depende. Quando o monitor for lento, descartar apresentações antigas,
não eventos físicos/DMA/IRQ nem amostras de áudio.

Não basta remover fence: buffers e semáforos atuais dependem dela. Não jogar
`gs::present()` em outra thread acessando estado global compartilhado. Tratar
input por tempo/ordem de eventos e leitura pelo passo guest, áudio por relógio
e fila limitada, pausa/foco/resize/fechamento sem saltos de simulação.
Definir autoridade única para velocidade e evitar conflito entre pacing host
e contrapressão do áudio; catch-up não pode acumular trabalho ilimitado.

Concluir com testes de agenda usando relógio controlado e limites de exibição
30/60/120/144; mesmas entradas temporizadas devem produzir mesmo estado nos
mesmos passos de simulação. Comparar tempos de corrida, eventos de colisão,
áudio e saves, além das imagens. Controle de hardware real precisa ter folga
de processamento. Isto pode manter100% velocidade sem144 imagens novas.

### 4. Migrar a parte cara comprovada, sem reescrever tudo

Se VU dominar, primeiro usar infraestrutura donor para registrar microprogramas
VU1 AOT do GT4: coletar conteúdo/variantes em cenários qualificados, traduzir,
recompilar e comparar registradores/flags/memória/pacotes GIF com interpretador.
Preservar mudança de código e identidade por conteúdo; não indexar só por PC.
Programa desconhecido mantém caminho interpretado explícito/observável.
VU0 requer contrato e suporte próprios. JIT Windows é alternativa se variações
dinâmicas tornarem AOT pouco adequado; maior superfície de implementação.

Se sincronização/GS dominar, priorizar esse caminho antes de VU. Se IOP dominar,
avaliar AOT dos módulos usados ou substituição HLE de serviços específicos,
mantendo imports, DMA/IRQ/RPC, áudio e cartão. Se EE dominar, analisar geração
e despachos. Cada migração conserva tempo guest e recebe comparação com a
referência; concordância interna não prova fidelidade PS2 por si só.

Concluir cada mudança com ganho medido no mesmo cenário e nenhuma regressão
nos contratos funcionais. IOP integral nativo e remoção geral dos modelos de
hardware ficam adiados até existir justificativa concreta.

### 5. Gerar144 imagens novas mantendo física original

Mapear no código GT4 a atualização de física/IA/colisões/cronômetros/input,
construção de cena/câmera/animação e esperas de vídeo. Provar quais funções
podem ser chamadas sem avançar gameplay ou provocar efeitos duplicados.
Não presumir que o jogo oferece um `update()`/`render()` independente.

Capturar estados visuais anterior/atual e interpolar câmera, poses e objetos
para cada apresentação. Construir/renderizar uma cena nova desses estados;
repetir framebuffer ou pacote GS com posições já calculadas não faz isso.
Separar HUD/cronômetros, efeitos, cutscenes e mudanças de câmera. Não escrever
posições interpoladas de volta na física. Interpolação exige definir/medir
latência e reinicializar histórico em teleporte, pausa e troca de cena.

Começar por uma corrida/câmera, expandir depois. Concluir com cenas distintas
em144FPS, mesmos resultados físicos no mesmo tempo/input, áudio correto e
HUD íntegro. Essa etapa exige conhecimento localizado do jogo e uma interface
de renderização adequada; não exige previamente reescrever todo IOP/VU.

### 6. Progressivo e resolução interna, depois expandir gráficos

Qualificar480p como referência, modificar seleção de saída de maneira compatível
com menus/corrida e implementar escala/resolução interna verdadeira. Cobrir
viewport, render targets, profundidade, texturas, pós-processamento e HUD.
Redimensionar janela não basta. Launcher/config persistente sem sobrescrever
saves; depois ultrawide/câmera/HUD, texturas e efeitos. Pode avançar em paralelo
com renderização independente quando suas interfaces estiverem definidas.

## Escopo e decisão de início

Recomendação de primeiro trabalho: receita de geração EE/build reproduzível.
Depois referência medida e primeira fatia de separação apresentação/tempo;
escolher otimização pelo resultado. Não iniciar uma troca integral para nativo.
O dono pediu planejamento; todas as etapas executáveis acima estão propostas,
não implementadas nem qualificadas por esta revisão.

## Referências externas primárias

- [Glenn Fiedler, Fix Your Timestep!](https://gafferongames.com/post/fix_your_timestep/):
  passo fixo, acumulação/interpolação e necessidade de capacidade de processamento.
- [Khronos, Swap chain](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/01_Presentation/01_Swap_chain.html):
  apresentação/swapchain e cadência do monitor. Não identifica nosso gargalo.

Referências explicam arquitetura geral; não demonstram hooks ou fidelidade GT4.
