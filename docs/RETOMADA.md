# Retomada do GT4Recomp — troca de modelo

**Atualização — 2026-10-13, fatia112/áudio e XInput.** A pedido do dono,
customização Vulkan ficou para depois. Piloto Clang tem áudio real estéreo48k,
controle XInput com botões/analógicos/pressão/vibração e teclado. Controle físico
não está conectado; o dono autorizou prosseguir sem ele. Fixtures passam,
54CTest e114Python(108/6) verdes. Jogar-GT4.cmd abre janela, verifica hashes,
mantém cartão privado persistente e backup por sessão; fechar/reabrir normal
confirmado. Guarda Vulkan rejeitava lote vazio válido: correção permitiu6000
quadros normais em Nürburgring,48capturas anteriores idênticas, cronômetro real
contando. Teste9000 também termina normal,60capturas comuns e prefixo PCM exatos;
direção/freio/reversa compatíveis com comandos observados, sem isolamento causal
completo e com colisão. Áudio gerado/consumido pelo Windows, mas filas esvaziam
com a lentidão atual. Próxima113: teste manual e medir esses gargalos; estabilidade, áudio acústico,
controle físico e saves completos permanecem abertos. Relatório112/0054/manifest
e STATUS são atuais; os próximos passos escritos nos blocos abaixo são históricos.

**Atualização — 2026-10-12, fatia111/Vulkan nativo.** Desenho real por compute
na GPU e apresentação Win32 confirmados no piloto Clang separado. Menus,
Nürburgring e FordKa3D na janela;3000quadros terminam normalmente.
15/15capturas Vulkan headless/janela coincidem. Fixtures de4MiB/noveformatos,
readback/snapshot/resize e gates verdes; produção/seed preservados.
Diferença CPU/GPU aparece no quadro400, causa aberta; validação Khronos
ainda indisponível, escala1/interlace temporal/áudio/XInput são limites.
Próxima112: divergência de pixels e qualificação de corrida/carregamento;
consultar STATUS/decisão0053/relatório111/manifest111 antes de modificar.

**Atualização — 2026-10-12, fatia110/piloto externo.** Menu principal e
navegação Arcade/circuitos/carros confirmados no Windows com nossos inputs.
Nome L/cartão novo foram criados pelo jogo; nova execução com cópia do cartão
reconhece os dados. Repetição1500 encerra normal e14capturas são idênticas;
probe longo expira180s/última2700, sem corrida qualificada. FordKa3D real
observado; GT Mode abre mapa e L's Home/dia1/Cr10000, confirmando nome relido.
Launcher rastreável com hashes/seed privado e gate de motivo,
54CTest e104Python(98/6) verdes. Produção preservada; integração coerente
Windows/Win32/GPU/Vulkan é próxima prioridade. Relatório110/0052/manifest
completam esta atualização; bloco0051 abaixo é o checkpoint anterior.

**Prioridade atual — 2026-10-12, estudo de aceleração/0051.** Os três
repositórios pedidos estão clonados/pinados em `private/references/`.
Nosso CORE tem o mesmo hash publicado pelo gt4-decomp: aproveitar nomes e
estruturas com verificação; 30 mil versus15 mil é splat versus chamadas.
Piloto completo ps2-web-recomp compilado intacto no Windows/Clang, CPU:
discoUSA2.00/ELF validados, apresentação Sony, criação de dados e entrada
de nome reais. Probe120 encerra normal; probe3600 termina no limite host45s
após pelo menos1625. Menu principal/corrida/estabilidade ainda abertos.
Produção preservada; próximos esforços priorizam piloto de integração do
runtime MIT e navegação inicial, não a antiga próxima fatia de observação.
PS2Recomp IOP4/4 passa; GPLv3 e retorno de prioridade divergente impedem
importação cega. Relatório externo/manifest/0051 e STATUS são atuais.
Vulkan direto, áudio, XInput e saves reais permanecem metas; GPU doador
wgpu/Win32 ainda não foi validado. Blocos abaixo são históricos.

**Atualização operacional — 2026-10-12, fatia109 encerrada.** O quadro extra
anterior ao receiver nasce na chargeN1267 da principal; handler4AB4AC escreve
1→2 antes do receiver2/2. Na referência, registro/enable0/0 já têm INTC2
pendente e o primeiro writer1/0 ocorre ainda na fase render anterior ao
próximo VBlank. Escritor comprovado por alias fastmem calibrada, endereço/
valor/largura host e bloco EE/ELF; rawPC está atrasado. Origem da pendência
e contexto original continuam abertos; reconstituir fila do STAT cegamente
arriscaria duplicar uma ocorrência. Próxima110 atribui esse lifetime/gate/
acknowledgement antes da menor correção. Modelo534/534, referência294/294
completos comuns e126/126 reduzidos exatos; fresh10k estado/work iguais,
produção/runtime/clock/IRQ preservados. Owner/BIOS/cópias18 intactos,
três referências fechadas. Gates54/97 herdados107. Boot ainda não funcional.
Evidência109, decisão0050 e STATUS são atuais; blocos abaixo históricos.

**Atualização operacional — 2026-10-12, fatia 108 encerrada.** A origem
local do quadro extra foi comprovada: a cobrança de tempo da primeira
notificação completa o período de vídeo do modelo, dispara INTC2 e chama
o manipulador do jogo. Sua instrução 4AB4AC escreve quadro 3 -> 4 antes
de a fila liberar a principal. Na referência, o mesmo trecho segue em
3/3 numa fase diferente do vídeo; o próximo evento GS não é novo VBlank.
47/47 registros do modelo e 120/120 estados comuns da referência se repetem
exatamente. Estado completo em 10 mil serviços coincide com produção;
runtime/clock/IRQ e executável preservados. Gates 54/97 herdados da 107.
Próxima 109 qualifica a diferença anterior de fase e trabalho entre serviços
antes de escolher uma política de tempo. Boot ainda não funcional.
Evidência108, decisão0049 e STATUS são a retomada atual; blocos abaixo históricos.

**Atualização operacional — 2026-10-12, slice107 encerrada.** Localizamos
um trecho menor da diferença: atualização e notify entram3/3 nos dois;
o mesmo nó da principal passa receiver->pending. Referência segue3/3,
modelo chega4/3 durante notify->release, antes da sync. Ainda faltam a
escrita exata do contador e a causa/posse da interrupção. Referência40janelas/
280estados, repetição120/120 exatos; diagnóstico10k estado completo igual
à produção. Build final reconstruído/verificado54CTest+Python97(91/6),
estado10k igual e sóproveniência do commit diferente. VS+UTF8 necessário
antes de configurar/build; artefatos antigos preservados. Runtime/clock/IRQ
preservados, boot ainda não funcional.
Próximo108 observa escrita/interrupt/time nesse intervalo; não ajusta quantum.
Evidência107, decisão0048 e STATUS são a retomada atual; blocos abaixo históricos.

**Atualização operacional — 2026-10-12, slice106 encerrada.** Resultado
da primeira sync pós-mensagem comprovado: referência espera/devolve0,
modelo jáatrasado/devolve1; valor chega à próxima iteração. Entradas já
diferiam3/3 versus4/3; origem causal anterior ainda desconhecida. Referência
31janelas/217estados, repetição93/93 iguais. Diagnóstico somenteleitura com
estado10k byteigualprodução/proveniência própria; nenhuma mudança runtime.
Próximo107 rastreia prefixo anterior, semajustarclock/IRQ. Evidência106/STATUS.

**Atualização operacional — 2026-10-12, slice105 encerrada.** CDVD_INIT
modo0 agora responde os16bytes comprovados em duas conclusões reais;
repetição12/12 e módulos da imagem conferem. RPC2 recusa checkpointRPC1.
Falha nas dependências de headers do build corrigida e verificada no objeto
real;54/54CTest ePython97(91/6), diferenciais90k/10k verdes. Strict10k/100k
sem RPC desconhecido, mas principal READY64/5ADBC8/updateRUN24 e zeroGIF.
Boot ainda não funcional. Próximo106 observa primeiro resultado de sync de
quadro após mensagem, sem mudarclock/IRQ. Evidência105 eSTATUS atuais.

**Atualização operacional — 2026-10-12, slice104 encerrada.** Runtime
corrigido: retorno da prioridade anterior preservado antes de preempção.
Kernel3 recusa checkpointkernel2; clock/IRQ iguais. CTest53/53 ePython91
(85/6) verdes, diferenciais90k/10k idênticos. Snapshot mostra restauração24,
mas principal aindaREADY64/receive5ADBC8 e zeroGIF; boot não funcional.
StrictRPC recusa80000592/fn0. Próximo105 qualifica respostaCDVDInit com
fonte/caller pinados e referência antes de implementar. Evidência104 em
`docs/reverse-engineering/slice104-previous-priority-return.md` eSTATUS.

**Atualização operacional — 2026-10-12, slice103 encerrada.** Divergência
causal no retorno de ChangeThreadPriority: referência devolve24, modelo0,
e o jogo restaura esse retorno como prioridade. Nove snapshots/duas comparações;
referência20janelas/140estados e repetição reduzida60/60 estados iguais.
Referência retoma receive principal e bloqueia em RPC_BIND; RUN dentro do
handler nativo não é trabalho comum da principal. Produção inalterada,
owner files/BIOS preservados, duas referências fechadas. Próximo104 corrige
contrato com testes/identidadekernel/bootfresh, sem mudar relógio ou IRQ.
Evidência em `docs/reverse-engineering/slice103-message-wake-prefix.md`.

**Atualização operacional — 2026-10-12, slice102 encerrada.** Observação
qualificada do boot real na referência:429 janelas completas, dois retries
de2000us e89 esperas de quadro; pedido1000us não observado. A mensagem
acorda a0=1 e a espera de quadro restaura o contexto receive anterior.
Repetição reduzida confirma54/54 estados registrados. Produção permanece
idêntica, gates completos anteriores continuam válidos; não é progresso
novo do boot do nosso runtime. Próximo103 segue esse contexto até o próximo
bloqueio e compara fase/pilha/fila com o modelo. Evidência completa em
`docs/reverse-engineering/slice102-matched-update-interval.md` e STATUS.
Capturas/helpers privados; ownerINI/saves/BIOS preservados, referências fechadas.
As atualizações inferiores registram suas próprias etapas históricas.

**Atualização operacional — 2026-10-11, slice101 encerrada.** A investigação
capturou o primeiro trap/retorno BIOS no interpretador por símbolos oficiais
conferidos com a release pinada. Treze controles +duas repetições passaram;
BEQ/BGEZ/BEQL tomados voltam ao slot e iniciam a cadeia de falha documentada,
plain/false comuns retornam à continuação e BEQL false anula. Runtime e
relógio permanecem iguais; gap likely5/1 vs1/0 ainda aberto. Gates atuais:
CTest53/53; Python91 (85 executados/6skip). Owner INI/BIOS/slot9/backup
preservados, referência fechada normalmente. Artefatos da etapa em
`private/pcsx2/sstates/slice101-interpreter-trap-observation/`,17 helpers
arquivados em `helpers/`; hashes em `docs/inputs/slice101-interpreter-traps.json`.
Leia [evidência101](reverse-engineering/slice101-interpreter-trap-observation.md)
e [STATUS](STATUS.md). **Próxima slice102:** medir intervalo real de
atualização1000us/principal com fase/motor qualificados; ainda não iniciada.
O checkpoint original de2026-10-09 abaixo permanece histórico; suas frases
“slice101 não iniciada” e desconhecimento do primeiro retorno foram superados
por esta atualização. O ZIP histórico de fontes não foi atualizado nesta etapa.


Checkpoint solicitado pelo dono em **2026-10-09**, depois da slice 100.
Este documento permite continuar sem depender do histórico da conversa.
Leia primeiro `docs/STATUS.md`; `AGENTS.md` contém o acordo de trabalho.

## 1. Onde paramos

- **Objetivo do dono:** recompilar o GT4 para Windows, chegar primeiro ao menu
  original e, depois, tornar o jogo utilizável. Não substituir o projeto pelo
  motor do PCSX2, nem fabricar uma tela que pareça o menu.
- **Confirmado:** o projeto ainda **não chegou ao menu**, não demonstrou
  renderização do jogo e não é jogável. O menu observado em sessões anteriores
  era da referência PCSX2, não da aplicação recompilada.
- Código da slice 100: `2e0de29`; fechamento/publicação: `434dcc8`.
  Ambos em `main`, publicados em `origin`. Este checkpoint acrescenta apenas
  documentação depois deles; confira o novo HEAD com `git log -1`.
- Repositório limpo antes deste checkpoint; nenhuma alteração de código pendente.
  Branch da etapa encerrada: `slice100-live-branch-controls`, já integrada.
- Nenhum experimento PCSX2, build ou suíte de testes em execução. Nenhuma meta
  autônoma OpenCode ativa. Este pedido foi **documentar antes de continuar**;
  não iniciamos a slice 101 nem criamos uma nova referência de desenvolvimento.

## 2. O que está construído e o bloqueio real

**Confirmado no registro de testes/evidências:** recompilador M0–M29, decoder
com 349 operações, tradução do jogo inteiro em um módulo, 15.068 funções e
924.991 instruções estáticas. Há runtime próprio de threads, semáforos,
interrupções, timers, DMA, transporte/RPC e comparação de estados. O panorama
completo e as limitações ficam em `docs/STATUS.md` e `docs/requirements.md`.

**Confirmado na fronteira de produção das slices 95–96:** a thread principal
está pronta (prioridade 64), mas a thread de atualização (prioridade 0) volta
a executar antes de a principal completar qualquer instrução. Permanecem
threads 1–3, dois pares RPC e zero payload GIF. Os 270 pedidos de espera de
1000 microssegundos realmente bloqueiam; o débito de 1 ms do próprio WaitSema
expira o prazo e interrompe a principal no PC restaurado, antes de executar.

Esse bloqueio não foi corrigido na slice 100. Resultados antigos de boot mais
avançado usavam semânticas anteriores e **não são a fronteira atual**.

**Confirmado na slice 98:** observação opcional `--count-work`, sem alterar o
relógio. Comparação tradução/interpretação em 90.000 serviços aceitos:
**22.566.319 instruções concluídas**, contagem e estado final idênticos. A
comparação interna não prova, sozinha, fidelidade ao PS2.

**Ainda desconhecido:** intervalo correspondente na referência em que o loop
de atualização pede 1000 us, a thread principal pode trabalhar e os eventos
estão na mesma fase. A slice 97 mediu outras esperas (retry de 2000 us e espera
de quadro); ambas selecionaram idle, não a principal. Não reutilizar essas
amostras como se fossem a espera de atualização que falta medir.

## 3. A descoberta da slice 100 e seus limites

Evidência principal: `docs/reverse-engineering/slice100-live-branch-controls.md`.

1. **Confirmado ao vivo, modo dinâmico:** programa original com GetThreadId
   positivo (`v1=0x2F`) fora de desvio entrou no vetor `0x80000180`, parou antes
   de ERET em `0x80000328` e chegou à continuação `0x00100068`, com `v0=1`.
2. **Confirmado ao vivo, modo dinâmico:** seis caminhos BEQ/BGEZ/BEQL com essa
   chamada na instrução após o desvio. Cinco chegaram ao destino/continuação
   com EXL=1, EPC=endereço do desvio, BD=0, `v0=-1` e nenhum hit no vetor.
   BEQL falso anulou a instrução. Seis controles de efeito também distinguiram
   execução da instrução de sua anulação.
3. **Fonte confirmado / explicação de alta confiança:** o emissor de BEQ do
   PCSX2 emite a chamada do slot e depois sobrescreve o PC selecionado pela
   exceção com a continuação do desvio. A atualização de exceção ocorre, mas
   o atendimento pelo BIOS é desviado. Repetições com parada apenas no ponto
   final e BEQL sem paradas no trecho testado mantiveram esse resultado.
4. **Limite confirmado:** o interpretador ignorou as paradas por endereço
   configuradas. Sua sondagem por instrução é condicionada a opções de build
   de desenvolvimento. Step Into não é prova de execução de uma palavra.
5. **Observado, causa ainda desconhecida:** BEQL tomado no interpretador, com
   lista de paradas vazia, registrou `branch delay!!` após ativar o ELF. Pausa
   posterior: PC=`0x80012BC0`, Cause=`8`, EPC=`0x800148E0`; não chegou ao ponto
   final. Não capturamos o primeiro vetor nem o retorno escolhido pelo BIOS.
   O aviso é emitido pelo **PCSX2**, não pelo BIOS; não atribuir aquele estado
   tardio à entrada da chamada `0x2F` sem nova evidência.

Não copiar a falha da referência para a produção. EPC igual ao endereço do
desvio não prova BD=1. Parar duas vezes no mesmo PC e ciclo não prova avanço.
O resultado desses programas pequenos não resolve a temporização do jogo.

Existe ainda a divergência conhecida do modelo em chamada atendida num slot
de desvio likely: nativo trabalho/serviços **5/1**, interpretador **1/0**.
Está caracterizada, não corrigida. Não declarar que a slice 100 a resolveu.

## 4. Decisões que o próximo modelo deve preservar

- Preempção correta no retorno final de interrupção e PC exato preservado.
  Não forçar turnos da principal para recuperar contadores de boot antigos.
- Produção continua com **1 ms por serviço aceito**. Não adotar conversão
  instruções→ciclos, quantum menor ou exceção especial para WaitSema por palpite.
- Identidades: time=3, interrupt=4, kernel=2, RPC=1, translation=2.
  `GT4CPT3`/`GT4KERN2` não mudaram; checkpoints interrupt-model-3 são recusados.
- Decisões 0039–0041: IDs de semáforo livres mais baixos, observador de trabalho
  externo ao estado/checkpoints, evidência qualificada por motor/ramo/exceção/
  retorno. Rascunhos 0037/0038 foram superados; não os reativar como política.
- Serviço `0x100` em `0x1604` é retorno adiado de patch/interrupção, **não idle**.
- Unsupported para com contexto útil. Nenhum fallback silencioso de sucesso.
- Código humano legível; evidências independentes, hashes e limites explícitos.
- Payloads só em diretórios ignorados. Push apenas para `origin`; `upstream`
  é leitura. Checar `git status --short` antes de cada commit.

## 5. Arquivos que devem ser lidos na retomada

| Arquivo | Papel |
|---|---|
| `AGENTS.md` | Acordo, autonomia, padrões e proteção dos inputs |
| `docs/STATUS.md` | Estado atual; blocos históricos estão identificados |
| `docs/reverse-engineering/slice100-live-branch-controls.md` | Procedimento, 31 capturas, hashes, falhas e limites |
| `docs/decisions/0041-qualify-reference-branch-events.md` | Regra de qualificação da referência |
| `docs/reverse-engineering/slice99-branch-slot-event-audit.md` | Diferenças entre motores, controles do modelo e gap likely |
| `docs/reverse-engineering/slice98-completed-guest-work.md` | Contagem concluída sem política de tempo |
| `docs/reverse-engineering/slice97-reference-delay-timing.md` | Layout auditado, amostras reais e preservação dos saves |
| `docs/journal/2026-10-09.md` | Histórico append-only das slices 88–100 e deste checkpoint |
| `docs/lessons/slice100-reference-is-not-hardware.md` | Explicação em português do que a referência prova e não prova |
| `scripts/reference_branch_fixture.py` | Gerador original de ELF; recusa sobrescrita |
| `tests/python/test_reference_branch_fixture.py` | Sete controles do gerador |
| `scripts/pcsx2_savestate.py`, `scripts/pcsx2_pine.py` | Leitura auditada de saves e observação PINE |
| `PLAN.md` | Direção estratégica até o menu, não o estágio operacional atual |

`HANDOFF.md` na raiz é **histórico de migração de máquina em M29**. Não usar
seus números, próximo M30, versão antiga do PCSX2 ou comandos de remoção de
build como instruções para esta troca de modelo na mesma máquina.

## 6. Artefatos locais e ferramentas

Workspace: `C:\Antigravity\gt4-staticrecomp`.

- Jogo: ISO na raiz, CORE em `private/fingerprint-check/CORE.GT4`, ELF de
  análise em `private/reconstructed/SCUS_973.28.elf`. Verificar contra manifests
  em `docs/inputs/`; verificação nunca altera os manifests.
- Referência atual: `private/pcsx2/pcsx2-v2.9.94/pcsx2-qt.exe`.
  **A pasta tem nome antigo; o executável é v2.9.114**, commit PCSX2
  `aa7ab4306e269075784c7ac3eb4b45e6e6c53445`, SHA256
  `0506a196f5bc76d47f618e68d40519c766bbf5959b8aa88d5b91cee1dcb48037`.
- Experimento fechado: `private/pcsx2/sstates/slice100-live-branch-controls/`.
  Contém 13 ELFs originais, 31 `.p2s`+JSON, logs, cópias dos perfis, BIOS
  isolado e `preservation.json`. Os quatro helpers foram arquivados em
  **`helpers/` nesse diretório**; a retomada não depende da pasta Temp do chat.
- Perfil realmente carregado: `<modo>/PCSX2/inis/PCSX2.ini`. Cópia inicial
  preservada em `<modo>/PCSX2-effective-launch.ini`. `-settings`, `-pause` e
  `--elf` não são opções válidas nesse executável. Receita auditada:
  `-debugger -bios -elf <caminho-absoluto> -datapath <diretório-absoluto-do-modo>`.
- PINE=28011; save version=`0x9A590000`, build=`v2.9.114`. Leitor de tempo
  recusa outras identidades. Uma nova referência diagnóstica exige qualificação
  própria; não contornar a recusa do leitor supondo o mesmo layout.
- Configuração do dono: `C:\Users\Alano\Documents\PCSX2\inis\PCSX2.ini`.
  Saves do dono em `Documents/PCSX2/sstates`; não confundir com a cópia antiga
  em `private/pcsx2/sstates`. Hashes de INI/BIOS/slot9/backup estão na evidência.
- Referência: software, sem cartões, sem injeção de controle; perfis sintéticos
  isolados, sem patches/game fixes. Não normalizar opções herdadas sem registrar
  a mudança. Avisos de FPU/MTVU também estão registrados.
- Ghidra/JDK e Python: `private/tooling/ghidra_12.1.3_PUBLIC`,
  `private/tooling/jdk-21`, `private/tooling-venv/Scripts/python.exe`.

**Atenção se trocar também de máquina:** Git/ZIP de fontes não transporta ISO,
BIOS, ferramentas privadas ou capturas. A evidência pública preserva os hashes,
não esses bytes. Não inventar observações nem recriar payloads ausentes.

## 7. Verificação já realizada

Na slice 100: build sem trabalho/avisos; **53/53 CTest** (52,44 s), Python
**91 coletados, 85 executados com sucesso e 6 pulados** (72,281 s). Há avisos
ResourceWarning de sockets já existentes na suíte Python; não confundir com
falha de teste nem dizer que Python foi totalmente sem avisos.

Depois do merge: gate de build + comparação de 90.000 serviços **2/2**
(13,58 s), gerador **7/7**. Não enfraquecemos critérios de aceitação.

Binário de produção continua byte-idêntico à slice 98, SHA256
`4c1dd903ee1bdc82d516e714fc5107ef448b34269ffbf83de61fa573d17230ba`.

Neste checkpoint documental, sem novos testes de execução: rechecados hashes
das 31 capturas e seus metadados, existência dos quatro helpers, INI/6 BIOS
originais, binário e ZIP; nenhum processo PCSX2 encontrado. Não alegar que
foram repetidas agora as suítes completas ou uma nova observação do jogo.

Na mesma máquina, usar VS2022 Build Tools em ambiente **x64**, CMake e Ninja.
Em VS Developer PowerShell, a sequência é:

```powershell
git status --short
cmake --build build
cmake --build build --target gt4boot
ctest --test-dir build --output-on-failure
private/tooling-venv/Scripts/python.exe -m unittest discover -s tests/python
```

Executar em série, não rodar build/CTest/Python simultaneamente: há testes que
compilam/linkam os mesmos binários. Shell comum não prepara SDK/MSVC. Alternativa:
um `.cmd` com `call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64`, seguido dos comandos e checagem de erro.
`docs/environment.md` registra também a armadilha de relink: após mudanças
em C++, não confiar só no build default; construir `gt4boot` explicitamente.

## 8. Proposta histórica no checkpoint — slice101 agora encerrada

**Slice 101 proposta:** observar o primeiro trap em slot no interpretador,
o retorno escolhido pelo BIOS e a cadeia de falha, incluindo o gap likely.

1. Reabrir os documentos e checar HEAD/status/artefatos, sem sobrescrever
   trabalho de outro agente. Criar uma branch curta para a nova etapa.
2. Determinar uma forma confiável de observar o interpretador. Se exigir build
   Devel/instrumentada do PCSX2, prepará-la separadamente em diretório ignorado,
   fixar fonte/build/hash, registrar observações sem mudar as decisões emuladas
   e qualificar os campos do save. Essa build **não existe ainda nesta etapa**.
3. Repetir o controle positivo fora de desvio no motor observado. Exigir prova
   de PC, serviço `0x2F`, ExcCode=8, EPC/BD/Status, atendimento e avanço real por
   ERET; não aceitar hit antigo ou nova pausa no mesmo PC/ciclo como retorno.
4. Capturar o primeiro evento nos caminhos de desvio, efeito/anulação e retorno.
   Separar execução contínua, parada no slot e efeitos da instrumentação.
   Não usar a falha do motor dinâmico como padrão do console.
5. Preservar e comparar o comportamento do modelo; só propor uma alteração
   respaldada por evidência qualificada e novos testes, sem apagar os controles.
6. Obter a espera real de atualização de **1000 us**, o intervalo de trabalho da
   principal e o alinhamento de fase antes de qualquer política trabalho→tempo.

Se a ferramenta de observação não puder ser preparada, registrar o impedimento
concreto e escolher o próximo experimento verificável; não fingir captura de
vetor/retorno. Continuar documentando descobertas e falhas conforme são produzidas.

## 9. ZIP e publicações

ZIP do dono: `C:\Users\Alano\Desktop\gt4recomp-fonte-2e0de29.zip`, 381 membros,
16.331.733 bytes, SHA256
`583ea1896e7616f2ed1afaa3009d470f30667e65e1ed08e691de238d2a8df789`.
Inclui fontes rastreadas e C++/header gerados atuais; não inclui ISO, BIOS,
build, ferramentas/capturas privadas ou binários. Arquivo anterior preservado.

O ZIP fixa o commit de código `2e0de29`, não as notas documentais de fechamento
nem este novo guia. Para retomar no mesmo workspace, usar o **repositório em
main**, onde estes registros são commitados e enviados a `origin`.
