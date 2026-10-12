# Aceleração do GT4Recomp com três projetos externos

Data: 2026-10-12. Base local: `59c89f69d0c4a8ae305603df6ade2fd7a957b12c`.
Pedido do proprietário: estudar e clonar os três projetos antes de retomar
as fatias de diagnóstico; priorizar jogo funcionando, depois decompilação
manual para legibilidade e modificações. Este estudo não fecha o objetivo
de boot funcional nem altera o runtime de produção.

## Recomendação

**Alta confiança:** o maior ganho provável é testar o conjunto completo de
`ps2-web-recomp` como uma base alternativa de execução, inicialmente isolada,
em vez de continuar construindo sozinho cada serviço PS2. O projeto combina
tradutor EE, kernel, IOP, transporte SIF, disco, unidades vetoriais, gráficos,
áudio e cartão de memória. O nosso tradutor já está bastante avançado; o
gargalo observado é o ambiente que executa o jogo, principalmente tempo,
interrupções, escalonamento e serviços IOP. Mais funções decompiladas ou uma
troca de Ghidra, sozinhas, não resolvem esse gargalo.

Usar os projetos com papéis distintos:

| Projeto | Uso prioritário | Limite |
|---|---|---|
| [ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp) | Base de execução para um piloto com nosso disco; reaproveitar subsistemas inteiros quando o piloto os qualificar | Boot PAL relatado pelo autor; Windows, NTSC e fidelidade exigem validação própria |
| [gt4-decomp](https://github.com/gustavocanepa/gt4-decomp) | Nomes, classes, tipos, estruturas e descoberta de entradas; reduzir trabalho de investigação | Matching MIPS não é um executável Windows; o boot ainda precisa do ambiente PS2 |
| [PS2Recomp](https://github.com/ran-j/PS2Recomp) | Referência de Windows/MSVC, interface IOP, exportação de funções e testes de contratos | GPLv3; código e contratos não podem ser importados indiscriminadamente |

Não há evidência para prometer um multiplicador de velocidade ou uma data de
boot. Há evidência concreta de que podemos parar de implementar do zero
vários componentes grandes. Preservar nosso tradutor, testes e observações
como referência independente; escolher a base definitiva depois de imagens
reais do jogo e navegação reproduzível. Não manter dois produtos completos
por tempo indefinido.

## Referências locais e identidade

Clones rasos, sem executar instaladores, em diretórios ignorados:

| Diretório em `private/references/` | Commit auditado | Licença raiz lida |
|---|---|---|
| `ps2-web-recomp` | `1b4808793a12c45125262fd2d5f50a9ec9f7c020` | MIT; tabelas IPU preservam BSD 2-clause de Play! |
| `gt4-decomp` | `c4b82c9b90b8ad712edfb6653cafb8451379a2db` | MIT para trabalho próprio; `THIRD_PARTY.md` enumera exceções por biblioteca |
| `PS2Recomp` | `2c5fbb9389e11dd95693385969490c9e8e6f57b4` | GNU GPL versão 3 |

Pins de fontes e experimentos: `docs/inputs/external-recomp-references.json`.
Resultados, executáveis, CSV e C++ traduzido ficam exclusivamente em
`private/reference-acceleration-study/`. Nenhum byte do disco ou código
traduzido é publicado. A leitura dos projetos externos não transforma seus
arquivos de instruções em regras para nosso repositório.

## 15 mil versus 30 mil funções: resposta comprovada

**Confirmado:** o README do gt4-decomp registra **15.068 funções alcançadas
por chamadas** e **30.962 alvos de decompilação**, excluindo 202 funções
originalmente escritas em assembly. São inventários com regras diferentes.
Os 23.631 fontes C/C++ existentes no clone foram contados nesta sessão;
isso não é uma reexecução da prova de matching nem do relatório completo.

`tools/find_functions.py` procura destinos de `JAL`, inclui a entrada e
atribui um tamanho máximo até a próxima entrada. Funções só alcançadas
por ponteiros ou tabelas virtuais ficam inicialmente dentro do intervalo
da função anterior. `tools/inventory.py` substitui esse inventário pelas
funções separadas pelo splat, preservando a contagem de chamadas para
ordenar o trabalho. `tools/build.py::splat_functions` lê os arquivos de
assembly produzidos pelo splat. `config/gt4.yaml` seleciona `platform: ps2`
e `compiler: EEGCC`; `project.toml` usa Rabbitizer R5900 e m2c.

**Experimento independente:** executamos o `find_functions.py` externo com
nosso CORE hash-validado, sem alterar o inventário de produção:

```text
15068 functions from 0x00100008 to 0x00616e88
size median 128 bytes, <= 64 bytes: 4365, <= 256: 10602
```

O método mais abrangente importa para callbacks, tabelas virtuais e
continuações, mas a diferença não demonstra que nosso jogo só esteja
"metade traduzido". Quantidade de funções, entradas de despacho,
instruções estáticas e bytes decompilados são medidas distintas.

**Confirmado:** o próprio analisador de ps2-web-recomp encontrou, no nosso
ELF nativo, **14.660 funções**, **135.093 entradas**, **1.240.374 instruções**
e escreveu **326 unidades C++**. As entradas incluem continuações e
destinos que não são contados como funções. Esse resultado também não
prova precisão das fronteiras nem execução de todas as instruções.

## A compatibilidade com o decomp é melhor do que o rótulo sugere

O gt4-decomp descreve seu disco como USA v1.01. O nosso disco continua
validado como USA v2.00; não atualizamos nem substituímos os manifests.
Porém `orig/README.md` externo publica este SHA-1 para `CORE.GT4`:

```text
30031137ce494323816bb707b90d3ec0d07937d2
```

**Confirmado:** esse é exatamente o SHA-1 calculado sobre nosso CORE nesta
sessão. Seu SHA-256 continua igual ao manifest local:

```text
85d26aa8430154967b2633eede929286694ac39e99762527edcec365fd642ff9
```

Isso qualifica o mesmo conteúdo CORE em relação ao hash publicado pelo
decomp; não qualifica igualdade entre os discos completos, bootstrap,
arquivos IRX, dados do jogo ou ambiente de carga. Não tivemos o segundo
disco original disponível para uma comparação direta byte a byte.

**Alta confiança:** os endereços dos símbolos CORE podem ser reaproveitados
com verificação por região/função, sem inventar um deslocamento entre
versões. `config/symbol_addrs.txt` contém 8.360 aliases de função para 5.596
endereços únicos. Há ainda metadados RTTI, classes, campos, unidades,
métodos Adhoc e bibliotecas. O número de aliases não é o total de funções.
Nomes e tipos inferidos continuam inferidos mesmo quando os bytes são
iguais. Não usar nomes como substituto de prova do comportamento.

O matching do decomp recompila fontes com o compilador MIPS original e
completa o executável com assembly do próprio jogo. Isso verifica
reconstrução, não fornece automaticamente um port nativo x86-64. Ponteiros,
layout e chamadas de sistema ainda são PS2. Seu documento de arquitetura
declara desconhecida a localização do loop nativo, escalonamento e
handlers de sincronismo. Não esperar que a grande porcentagem de matching
resolva imediatamente nossa divergência de VBlank.

## Ghidra Emotion Engine Reloaded

**Confirmado:** o pipeline de matching acima usa splat/Rabbitizer/m2c e
compilação comparada. Não encontramos fundamento para atribuir seus
30.962 alvos ao Ghidra EE Reloaded. O ps2-web-recomp descobre código em
`recomp/analysis.cpp` sem exigir Ghidra: fluxo de controle, constantes de
endereço, destinos calculados, palavras nos segmentos e entradas extras.
O PS2Recomp aceita mapas exportados de Ghidra e recomenda isso para ELFs
retail sem símbolos; seu exportador usa a análise já presente no programa.

O [EE Reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded)
oferece MMI/VU0, referências R5900, STABS e importação de savestates.
**Alta confiança:** é uma ferramenta auxiliar útil para nomes, referências
e análise R5900. **Desconhecido:** compatibilidade exata da extensão com
nossa instalação Ghidra 12.1.3 e qualidade em cada função deste jogo; não
instalamos nem medimos a extensão nesta sessão. Não é pré-requisito do
piloto de execução e não justifica recomeçar nossa análise validada.

## ps2-web-recomp: onde está o ganho real

### Execução e recompilação

`recomp/main.cpp` lê ELF32, descobre código e gera C++ dividido por unidades
de 16 KiB. `analysis.cpp` segue chamadas/branches, considera constantes e
ponteiros em segmentos, e aceita entradas extras. `dispatch.cpp` interrompe
uma entrada não traduzida e registra o endereço. `scripts/port.sh` coleta
esses endereços, gera novamente e recompila; também grava microprogramas
VU1 para recompilação posterior. Podemos reutilizar esse ciclo de descoberta
guiada pela execução, em vez de analisar manualmente milhares de funções
que talvez nem sejam alcançadas no boot.

**Confirmado:** o EE principal é recompilado antecipadamente para C++ e
Wasm ou executável nativo. Não é um núcleo EE de interpretação geral.
Há, contudo, interpretação R3000 do IOP e execução/interpretação de VU
quando não há programa traduzido adequado. Existem modelos de dispositivos
PS2 e serviços de alto nível. Isso mantém a estratégia de recompilação
estática do jogo, mas não elimina a necessidade de reproduzir o hardware.

### Tempo, interrupções e threads

`codegen.cpp` debita trabalho comum do orçamento de execução e contabiliza
pares branch/slot. `system.cpp` deriva o tempo desse orçamento, agenda
VBlank e outros eventos e avança até o próximo evento no idle. O kernel
escolhe threads READY por prioridade e preserva continuação ao desenrolar
a pilha nativa. Diferentemente da nossa política 1 ms por serviço, também
contabiliza trabalho fora de serviços.

**Limite confirmado:** as unidades chamadas de ciclos nesse projeto não
são uma tabela completa de latências R5900 qualificada contra hardware.
Não copiar esse orçamento para nosso modelo de produção como se fosse
uma conversão comprovada. Um piloto de compatibilidade pode usar o
modelo externo identificado separadamente; checkpoints e comparadores
do nosso runtime não podem tratá-lo como a mesma semântica.

`hw.cpp` mantém INTC STAT independentemente da máscara e calcula pending
como `STAT & MASK`. Habilitar muda a máscara; o kernel consulta pending e
reconhece a causa antes de chamar o handler. **Hipótese útil para a antiga
slice110:** isso preserva uma ocorrência latched enquanto mascarada,
coerente com nossa observação de INTC2 anterior à registration. Não prova
origem, lifetime ou reconhecimento corretos do nosso caso; não reconstruir
cegamente uma fila a partir de STAT nem duplicar uma ocorrência entregue.

`ChangeThreadPriority` devolve a prioridade anterior no código externo,
coerente com nossa correção 104. Isso reforça sua utilidade comparativa,
sem tornar todos os outros contratos automaticamente corretos.

### IOP, disco e transporte

`runtime/src/iop/cpu.cpp` executa instruções R3000; `kernel.cpp` carrega,
reloca e inicia módulos IRX, resolve imports e mantém threads. `sif.cpp`
transporta pacotes e completions; `cdvd.cpp`, `cdvdfsv.cpp` e `io.cpp`
fornecem dispositivos e serviços básicos. Parte dos módulos é substituída
por serviços nativos. O código específico dos módulos restantes vem do
disco. O ganho é evitar reescrever cada driver particular do jogo em C++.

Isso oferece um caminho mais completo do que nosso pequeno conjunto de
respostas RPC qualificadas. Também adiciona uma CPU secundária e contratos
de imports/dispositivos que precisam de testes; não é uma integração de
somente uma função.

### Gráficos, áudio, controle e saves

GS possui memória nativa swizzled de 4 MiB, GIF, rasterização CPU e shaders
compute WGSL (`runtime/src/gs/gpu/`). VIF e VU alimentam a mesma cadeia de
desenho do jogo. Essa base pode entregar a primeira imagem real antes de
escrevermos outro renderizador completo. Existe caminho nativo com
wgpu-native, além da página WebGPU.

**Limite importante:** WGSL/WebGPU executado por wgpu-native não é nosso
renderizador Vulkan direto. No commit auditado o backend usa a seleção
Primary, não força Vulkan. O pacote baixado e a integração nativa de GPU
são Linux; a superfície aceita X11/Wayland/Canvas, sem Win32. O piloto
Windows de CPU não valida Vulkan. Preservar a meta de Vulkan nativo,
usando essa base para aprender/validar GS e decidir posteriormente quanto
do frontend e dos algoritmos se reaproveita.

`iop/spu2.cpp` modela vozes, envelopes e samples; `sio2.cpp` modela protocolo
de pad, analógicos, pressão e cartão. O host SDL integra saída de áudio e
controle. Há persistência em arquivo/browser e layout de cartão PS2.
Isso cobre componentes úteis das metas posteriores. Não verificamos nesta
sessão XInput no Windows, compatibilidade de saves com o jogo, qualidade
de áudio ou replay determinístico. Usar cartão novo privado, nunca os
saves do proprietário, em experimentos.

### Atalhos que impedem adoção cega

Fontes concretas lidas:

- `kernel.cpp::handle_syscall`: syscall desconhecido é informado e retorna
  zero; não para. Isso difere da disciplina de produção do nosso projeto.
- `iop/kernel.cpp::hle_id_for`: import desconhecido registra aviso e devolve
  zero. Relocações IOP desconhecidas também podem ser ignoradas com log.
- `iop/cdvdfsv.cpp`: serviços HLE de CDVD têm respostas simplificadas;
  comando S desconhecido pode responder 1. Não substituir nossa resposta
  CDVD_INIT 105 validada por essa resposta genérica.
- `cop.cpp`: `cpcond0` é sempre verdadeiro, VU0 busy sempre falso; CMSAR1
  de início de VU1 é explicitamente não implementado. BC0F reconhecido no
  decoder não significa condição DMA correta.
- `codegen.cpp::delay`: syscall no delay slot para como não suportado.
  Nossas caracterizações 99–101 continuam úteis; não assumir cobertura
  integral das exceções apenas porque o GT4 PAL foi demonstrado.
- A documentação admite aproximações de ponto flutuante VU; os controles
  internos de tradução/interpretação compartilham partes do modelo.

**Alta confiança:** a base é promissora para compatibilidade prática.
**Desconhecido:** quanta dessa simplificação o boot/race USA exercita.
O primeiro piloto deve conservar logs e distinguir execução funcional de
fidelidade. Antes de usar componentes em produção, substituir os defaults
desconhecidos por falhas com contexto ou contratos explicitamente validados.

## PS2Recomp: bom conjunto auxiliar, não substituição automática

O README o descreve como experimental. O projeto contém tradutor, analyzer,
runtime Windows/MSVC, exportador Ghidra e uma interface separada para IOP.
O IOP executa IRX R3000 com kernel virtual, memória própria, transporte
SIF e serviços HLE ativados por carregamento. Sua interface
`IopSubsystem` e snapshots de diagnóstico são referências úteis para
introduzir execução de módulos originais no nosso projeto.

Seu renderer auditado usa backend GS CPU e apresentação via raylib; não
encontramos prova de GT4 funcional no material auditado nem um renderer
Vulkan pronto equivalente à nossa meta. Há testes dedicados a decoder,
memória, kernel, interrupções, SIF/DMA/RPC, VU e IOP, que dão ideias para
controles independentes. O recompiler oferece patches, skips e stubs
`ret0`/`ret1`/`reta0`: recursos de triagem, não aceitação de boot.

**Divergência concreta:** `EeScheduler::changePriority` preenche
`oldPriority`, mas retorna `KE_OK`; `Thread.cpp::changePriorityImpl` publica
esse `result` no retorno guest. Isso conserva o retorno zero que nossa
evidência de referência 103/104 mostrou errado para o caminho do GT4.
Ter mais testes não prova esse contrato. Não substituir nosso kernel por
esse kernel sem confrontar os casos já qualificados.

**Licença confirmada:** a raiz é GPLv3. Não copiar código para nosso runtime
como se fosse MIT. Neste estudo só há clones privados e observação. Um
eventual reaproveitamento de código exige preservar a licença aplicável e
decidir a política de distribuição antes da incorporação. Para o piloto
priorizar ps2-web-recomp MIT; consultar algoritmos e fazer testes próprios
sem misturar fontes inadvertidamente.

## Próximos marcos de execução

1. **Piloto completo isolado (primeiro realizado):** compilar e executar nosso CORE/disco NTSC
   com o runtime externo, registrar entradas ausentes, syscalls/imports,
   módulos realmente executados, filas RPC e frames produzidos. O piloto
   tem identidade própria e não consome checkpoints da produção.
2. **Primeira imagem real (observada) e menu (aberto):** repetir cold boot com captura de GIF/GS
   originada no guest; exigir navegação por input. Contagem de VBlank,
   término sem crash e janela aberta não bastam. Sem success stubs para
   serviços desconhecidos usados no caminho aceito.
3. **Escolha da base:** se o piloto progride muito além do nosso ponto
   atual, incorporar/reter o runtime doador em uma branch de integração
   em vez de transplantar dezenas de handlers isoladamente. Se falhar,
   corrigir o primeiro bloqueio concreto aproveitando nossos contratos,
   nomes do decomp e referência PCSX2; não regressar automaticamente a
   uma longa sequência de observações sem experimento de execução.
4. **Windows e estabilidade:** adaptar biblioteca GPU/superfície Win32,
   explicitar backend Vulkan, controles e saves próprios; validar menu,
   corrida e cold boots repetidos. Depois perseguir áudio, XInput e save
   conforme os marcos do proprietário. Não declarar corrida estável só
   por um frame nem save funcional só por criar um arquivo.
5. **Decomp posterior e pontual agora:** usar nomes/metadados imediatamente
   quando ajudam o boot; reconstruir manualmente código modificável após
   funcionamento estável. Não copiar 23 mil funções MIPS para tentar
   resolver hardware, pointers e ABI automaticamente.

O diagnóstico histórico de INTC2 permanece como evidência para o piloto.
A política 1 ms/serviço e o IRQ corrigido permanecem na produção enquanto
ela existe; não enfraquecer testes para encaixar o modelo externo. O
próximo documento deve começar com o resultado concreto do piloto e o
primeiro bloqueio, não apenas uma nova porcentagem de cobertura.

## Experimentos desta sessão

**Confirmado — primeiro piloto já executado nesta sessão:** não ficamos
apenas na leitura dos READMEs. Compilamos o analisador e o conjunto
runtime/jogo e executamos nosso disco USA v2.00 no Windows x86-64.
Nenhuma dessas execuções altera a aceitação histórica do runtime local.

### Ferramentas e compilação

- MSVC 14.44.35207, C++20, x64, para o analisador externo de seis fontes.
  Sem edição do código externo. Saída `usa-v2-eerecomp-stats.txt`.
- Clang instalado 23.1.2, target `x86_64-pc-windows-msvc`, para todo o
  runtime CPU e 326 unidades traduzidas. VS amd64, UTF8, CMake/Ninja,
  Release, builds exclusivamente privados. Sem instalação global nova.
- CMake original, GPU ausente e SDL não localizado: **headless CPU**.
  Os caminhos Linux de perfil não entram no Windows; a fila GS usa
  `std::atomic::wait` no caminho não Linux. GNU vector types/builtins e
  musttail compilam com Clang. Isso corrige uma suspeita inicial de que
  todo o runtime exigiria Linux: o núcleo CPU já compila intacto no Windows.
- Avisos do build externo foram conservados nos logs. `-Wall` com
  clang-cl habilita muitos diagnósticos de compatibilidade C++98 etc;
  não apresentamos este build como warning-free ou como produção aprovada.
- Configuração de janela/GPU Windows ainda precisa de adaptação:
  wgpu package/link, backend e superfície Win32. Não medimos Vulkan.

Antes do run, os hashes do CORE, ELF nativo e ISO foram conferidos contra
os manifests existentes. ISO SHA-256:
`67b6c0075837f3ae1132d608acf2858bf13b2dd62d6eae83dff76df02e4e824f`;
ELF nativo:
`10f82e2231a51404b95682ed3ea81171100a1af2fefdeed3391943016c7c935c`.
O ELF foi copiado byte a byte para nome privado `SCUS_973.28`, preservando
o hash e permitindo argv0 `cdrom0:\SCUS_973.28;1`. Não usamos BIOS,
PCSX2, checkpoint de produção ou imagem sintética nesse piloto.

### Probe 120: primeira imagem guest

`--ntsc --frames 120 --trace-iop --trace-io --frame-every 30`, disco real
somente leitura e cartão novo privado. Quatro PPM de 640x448 foram emitidos
pelo display GS. Término normal pelo limite de frames, exit 0, sem timeout,
0,913 s de parede. O próprio runtime informa 589.961.381 unidades de seu
orçamento, 1.215 primitivas GS, DMA ch0=1/ch1=117/ch2=334 e GIF paths 2/3
com tags reais. **Esse tempo é duração do probe, não benchmark de corrida.**

Os logs mostram 23 módulos IOP carregados, incluindo SIO2MAN, MCMAN,
MCSERV, LIBSD, LIBPDI, PDICDVD, PDISPU2 e PDISTR, e 17 threads EE no dump
final. Main está WAIT/Sleep, não eternamente READY como a produção atual.
As capturas mostram a apresentação Sony Computer Entertainment America.
Há avisos DEV9 e imports DEV9 desconhecidos na resolução; não afirmamos
que todo hardware foi implementado ou que todo import foi exercitado.

### Probe 3600 solicitado: configuração inicial interativa

Novo cartão privado e diretório separado. Sequência automatizada já usada
pelo workflow do autor: Start no frame300, Cross a cada300 frames a partir
de600. Prazo host de45 s impediu execução ilimitada. **Não completou3600:**
foi terminado por timeout após45,017 s; último registro periódico é
frame1625,445.920primitivas. Não contar esse limite como travamento guest,
como término normal ou como medição confiável de desempenho.

Cinco capturas em300/600/900/1200/1500 foram preservadas. Inspeção visual:
frame300 pede criar dados GT4 no cartão; frame900 apresenta "Welcome to
Gran Turismo 4" e entrada de nome; frame1500 continua nessa tela. Os
frames900 e1500 têm exatamente o mesmo hash de pixels. As teclas genéricas
não fornecem um nome; repetir Cross não prova menu principal navegável.
Há desenho e avanço causal por input entre telas da configuração inicial,
mas **menu principal, corrida, áudio ouvido e save/load concluído ainda
não são aceitos**. Este é um executável alternativo de estudo, não uma
mudança do `gt4boot.exe` de produção.

### PS2Recomp: execução dos testes IOP

Compilamos o subprojeto IOP original com MSVC Debug, sem buscar raylib,
ImGui ou dependências do runtime completo. **4/4 CTest passam**:
emulator, imports, compatibility e import-version; relatório total0,28 s.
Isso qualifica que a interface/suites do IOP são executáveis no nosso
ambiente. Não valida GT4 nesse projeto nem desfaz a divergência de retorno
de prioridade encontrada no runtime EE.

### Repetição e arquivos privados

Os scripts privados de build/probe são hash-pinados no manifest. Com os
clones nos commits e inputs idênticos:

```powershell
# Os .cmd inicializam VS amd64 e UTF8 antes de compilar/configurar.
Push-Location private/reference-acceleration-study
cmd /c build-eerecomp.cmd
cmd /c build-runtime.cmd
Pop-Location
private/reference-acceleration-study/eerecomp.exe private/reconstructed/SCUS_973.28.elf --stats
private/reference-acceleration-study/eerecomp.exe private/reconstructed/SCUS_973.28.elf --out private/reference-acceleration-study/generated
Push-Location private/reference-acceleration-study
cmd /c build-game.cmd
cmd /c test-ps2recomp-iop.cmd
Pop-Location
private/tooling-venv/Scripts/python.exe private/reference-acceleration-study/probe.py
private/tooling-venv/Scripts/python.exe private/reference-acceleration-study/probe.py 3600
```

Não reexecutar sobre os diretórios congelados e cartões já usados: preparar
novos diretórios para preservar os primeiros resultados e controlar o
estado inicial. `probe-result.json` registra argv completo, limite host e
término; o script confere ELF e recibo da validação do disco. Para outra
cópia do disco, recomputar seu SHA-256; recibo não substitui essa conferência.
PPM→PNG foi apenas conversão de formato sem mudança dos pixels, com Pillow
do runtime de dependências local. Um primeiro intento com o Python do
projeto falhou por não possuir Pillow; nenhum pacote foi instalado.

Produção e suas bibliotecas não foram recompiladas nesta sessão. Gates
54 CTest/Python97 continuam herdados da107, como109. O manifest registra
hash do executável de produção e resultados privados. Todos os processos
de teste foram encerrados; o probe longo foi terminado explicitamente
pelo timeout, sem tocar saves/INI/BIOS do proprietário.

## Limites do estudo

Não reexecutamos o matching completo do decomp nem adquirimos seu disco
v1.01 ou o disco PAL do autor do ps2-web-recomp. O relato de 50 FPS, áudio,
menu, corrida e save PAL é evidência do autor, não benchmark nesta máquina.
Não auditamos cada uma das dezenas de milhares de funções ou cada shader;
seguimos os caminhos de descoberta, execução, tempo, kernel, IOP, dispositivos
e apresentação que determinam a decisão de arquitetura. As verificações
realizadas e seus pins estão no manifest do estudo.
