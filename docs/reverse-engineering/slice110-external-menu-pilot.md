# Slice110 — piloto nativo externo: perfil e menu

Data: 2026-10-12. Baseline do fork: `95dd453`.
Ramo: `codex/runtime-menu-pilot-110`. Experimento autorizado pelo proprietário
após o estudo externo/decisão0051; não altera o runtime de produção.

## Pergunta e critério

O runtime MIT completo de ps2-web-recomp já desenha telas iniciais com nossos
inputs. Esta fatia verifica se comandos controlados permitem criar um perfil
e alcançar um menu que responde, antes de decidir uma integração maior.
Encerrar milhares de frames sem falha não basta para aceitar boot funcional.

**Confirmado:** o mesmo executável nativo Windows x64/Clang23.1.2 do estudo
é utilizado, sem patch de serviços, tradutor ou código guest. SHA256:
`4a4f36899738032f44f879de422984951bd3a2e6824552ca344a06d1d1dd0ade`.
Sua fonte está fixada em ps2-web-recomp
`1b4808793a12c45125262fd2d5f50a9ec9f7c020`. Identidade completa de fontes e
build: `docs/inputs/external-recomp-references.json` e relatório externo.

O launcher dos controles2–4 verifica novamente executável, CORE, ELF e ISO
inteiro contra os manifests existentes. O helper anterior do controle1
verifica executável, ELF e ISO; não verifica CORE separadamente. A verificação
nunca atualiza os manifests.

| Input | SHA256 |
| --- | --- |
| ISO USA v2.00 | `67b6c0075837f3ae1132d608acf2858bf13b2dd62d6eae83dff76df02e4e824f` |
| ELF reconstruído | `10f82e2231a51404b95682ed3ea81171100a1af2fefdeed3391943016c7c935c` |
| CORE | `85d26aa8430154967b2633eede929286694ac39e99762527edcec365fd642ff9` |

## Instrumento e limites

`scripts/external_runtime_probe.py` utiliza o fingerprint existente, argumentos
literais e subprocesso limitado no tempo. Saídas e cartão ficam em um novo
diretório dentro de `private/`; diretórios existentes são recusados. Copia o
ELF com nome `SCUS_973.28` exigido pelo argv0 do jogo e verifica também a cópia.
Captura PPM a cada100 VBlanks do modelo externo, trace de syscalls e motivo
de parada. Os botões são injetados pelo mecanismo original `--press` do doador.
PNG é conversão sem perda de PPM para inspeção, não uma tela fabricada.

O doador retorna exit0 inclusive em certas paradas guest. O launcher só
aceita conclusão de execução se o log diz `frame limit reached`, no número
solicitado, exit0 e sem timeout. Esse resultado **não certifica menu ou boot**:
as capturas e seus efeitos precisam ser examinados separadamente.

Para reler dados, `--card-seed` aceita apenas uma imagem privada fixada no
manifest desta fatia, verifica hash/tamanho antes da cópia e verifica a cópia.
O processo recebe exclusivamente o novo cartão e o resultado identifica
a origem. Isso mantém o cartão original e sua evidência congelados.

Sete controles sintéticos na suíte Python existente cobrem inputs alterados,
destinos privados novos, argv literal, cópia corrompida, timeout com log e
exit0 após parada precoce, seed alterada e preservação do cartão original.
Não contêm bytes do jogo nem executam o doador.

## Experimentos preservados

Todos partem do ELF/disco e de cartão privado inexistente, NTSC, CPU/software
GS, sem GPU/SDL, sem checkpoints de produção. Resultados e helpers congelados
estão em `private/slice110-menu-pilot/`; metadados distributáveis serão fixados
em `docs/inputs/slice110-external-menu-pilot.json`.

1. `name-control1`:2000frames, limite110s, encerra normal em52.842666s.
   Cross300 antecede a tela interativa; Left600/Cross630 cancela a criação.
   Frame700 mostra que GT mode não é jogável sem criar dados. Controle
   negativo preservado, não atribuído a falha do runtime.
2. `name-control2`:2000frames, limite110s, encerra normal em65.379512s.
   Start300/Cross600 aceita início; Left950/Cross980 escreve **L**;
   Right1010/Cross1040 seleciona Enter. Frame1000 mostra L,1100 pede confirmar
   o nome; Cross1300 aceita. Frame1600 pede criar os dados. Start1500 não
   aceita esse diálogo. Não é menu principal.
3. `name-control3`:3000frames, limite180s, encerra normal em125.166590s.
   Mesmo prefixo sem Start1500, Cross1700 e2000:frame1800 informa espaço de
   1488KB, frame2200 pede formatar cartão novo, com Cancel selecionado.
   Sem nova confirmação, não cria cartão persistido nem chega ao menu.
4. `name-control4`:4500frames, limite300s, normal160.715291s.
   Right2300/Cross2330 aceita formatação;frame2400 mostra progresso real.
   Cross2700/3000/3300 avança instruções de autosave, verificação e proteção
   do cartão. Frame4500 ainda aguarda a última confirmação. Card8650752bytes
   persistido, SHA256`bb10a96fd78e45d648023f9962739c09f152ce01ae45c3fea1d998a71d5e3072`.
5. `card-reread5`: nova inicialização do ELF com cópia hash-verificada do
   cartão4. Frame600 mostra menu principal Arcade Mode/Gran Turismo Mode;
   frame1000 mostra categorias de circuitos,1400 seleção Nürburgring.
   Cross600/900/1200 avança telas. Não repete criação/formatação/nome.
   Frame1900 mostra fabricantes de carros,2500 mostra Ford Ka3D com dados
   e modelo renderizado. A execução pede3000frames mas atinge limite host180s
   após última captura2700 (180.923369s). Launcher retorna1 corretamente,
   preservando log/capturas; não é parada guest diagnosticada nem prova de
   estabilidade. Sem corrida observada.
6. `menu-repeat6`: repetição reduzida1500frames do prefixo5, mesmo seed4 e
   novo destino; normal56.565667s, frame-limit/exit0. As14 PPM de100 até1400
   são byte a byte idênticas às do controle5 (SHA256), incluindo menu e seleção
   de circuitos. Não é comparação de todo o estado ou referência PS2.
7. `gt-mode7`: inicialização com o mesmo cartão, Down600/Cross630 para GT Mode;
   frame1000 mostra mapa de Home/License Center/eventos, Cr10000 e dia1.
   Cross1100 entra em Home;frame1200 diz **L's Home**, com0.0%/0points/day1
   e menus Garage/Status/Options/Photo Lab/Trade/Diary/Save. Confirma releitura
   do nome L criado no controle4.1500frames normal113.342559s, frame-limit/exit0;
   não substitui o controle repetido Arcade.

Tempos são medidas host do piloto com trace e software rendering, sob cargas
variáveis; não são benchmark nem conversão de instrução para ciclo PS2.

## Fronteiras de evidência

**Confirmado:** os controles1–3 respondem aos botões e desenham as telas reais
descritas. O launcher não modifica o runtime externo ou o estado de produção.
O antigo `build/gt4boot.exe` permanece com SHA256
`839adaa22f48ba674fc8f5f6c498299a9329598dedf23ca3ee507f1f1c76dfab`.

**Confirmado no controle5:** menu principal e navegação Arcade até seleção de
circuitos, partindo de nova inicialização e relendo cartão privado criado pelo
jogo. Controle7 confirma mapa GT Mode e identidade **L's Home**: não apenas
existência de um arquivo de cartão. Não foram feitos saves de corrida/progresso.

**Desconhecido nesta etapa:** corrida, cobertura ampla de saves, fidelidade
temporal/hardware e adoção de runtime. Os fallbacks e limites
do doador continuam os do estudo0051; não se tornam contratos corretos porque
aparece uma imagem. CFC2(28) com retorno0 já observado exige tratamento próprio
antes de produção. Vulkan direto, áudio e XInput permanecem metas posteriores.

## Verificação

Python final:104 testes,98 executados/seis dispensados,73.546s, todos passam.
Após tornar a recusa de seed não fixada descritiva, os sete controles do
launcher passam novamente em0.183s, incluindo seed externa/não fixada.
ResourceWarnings de sockets da suíte foram preservados; não são falhas novas
do launcher. CTest nativo:54/54 passam,60.11s. Hash do executável de produção
novamente conferido após as suítes; idêntico ao107. Controle4 encerrado;
controles de releitura/repetição encerrados. Produção e cartão seed mantêm
seus hashes. Os fallbacks do doador continuam dívida de qualificação.

## Reprodução e próximo passo

Exemplo de repetição curta (usar sempre novo diretório de saída):

```powershell
private/tooling-venv/Scripts/python.exe scripts/external_runtime_probe.py private/slice110-menu-pilot/new-menu-repeat --frames 1500 --timeout 120 --card-seed private/slice110-menu-pilot/name-control4/card.ps2 --press 300:start --press 600:cross --press 900:cross --press 1200:cross
```

O seed precisa existir e ter exatamente o hash fixado; se ausente, refazer
controle4 pela lista de argumentos no manifest, nunca fabricar o cartão.
Para GT Mode: Down600/Cross630/Cross1100 após Start300, mesmo seed.
Todos os comandos originais, motivos, tempos, versões de helpers e hashes
de logs/cards/capturas estão no manifest110. Nenhum payload é publicado.

O manifest registra sete execuções e239artefatos privados/controles. Census
dos sete logs:zero `unhandled syscall`, import ausente **realmente chamado**,
instrução IOP desconhecida, servidor RPC desconhecido e erro de escrita do
cartão. Cada execução registra uma advertência `cfc2 28`. Isso não cobre
contratos de serviços implementados com respostas simplificadas. O trace
deste launcher não ativa `--trace-iop-modules`: não usar ausência de advertência
de resolução como prova de que toda importação tem implementação própria.

Helpers foram congelados por execução:controle2 precede verificação da cópia
ELF;3/4 acrescentam essa verificação;5/6 acrescentam seed fixada;7 usa a recusa
descritiva de seed não fixada. O código final é fixado separadamente no manifest.

**Alta confiança:** o reaproveitamento do runtime completo reduziu a fronteira
de boot para integração prática no Windows: o piloto abre e navega menu,
Arcade e GT Mode. Esse resultado justifica continuar0051/0052, preservando
contratos antigos para avaliar diferenças. Não estabelece equivalência PS2.

Próxima fatia: tornar a construção/host MIT reproduzíveis em um alvo separado
do `gt4boot` MSVC, abrir saída guest viva em Win32 e qualificar caminho GPU
rumo ao renderizador Vulkan nativo. Auditar serviços realmente executados e
comparar contratos antes de qualquer adoção. Sem corrida ou desempenho
qualificados, nenhuma alteração automática de clock/IRQ/produção.
