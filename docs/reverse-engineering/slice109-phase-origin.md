# Fatia 109 — fase anterior, interrupção pendente e trabalho observado

2026-10-12. Base `b525fc4`, branch `slice109-frame-phase-origin`.
Decisão [0050](../decisions/0050-earlier-phase-and-pending-intc2.md).
Metadados [slice109-phase-origin.json](../inputs/slice109-phase-origin.json),
SHA256 `fb9c07f9fca00ad85fc299f563bcc6146a46037e726c818cc5cabb646958db0a`.

**Confirmado:** o incremento extra anterior à espera da mensagem, no modelo,
nasce da cobrança N1267, enquanto a principal trabalha na fila `617D04`.
O registro do tratador e a habilitação da causa 2 ocorrem com contador zero
nos dois ambientes. Na referência, o primeiro incremento consome uma fase
anterior ao próximo VBlank programado: a causa 2 já estava latched e masked
antes da habilitação. **Alta confiança:** é entrega de pendência anterior.
Origem dessa pendência e contexto original interrompido continuam desconhecidos.

Isso qualifica uma diferença anterior à notificação estudada em 108. Ainda
não justifica uma nova política de tempo nem uma alteração isolada em EnableIntc.
O jogo não tem boot funcional; executável, relógio e IRQ de produção permanecem iguais.

## Identidades e método

Entradas imutáveis, verificadas antes da referência e na publicação:

| Entrada | SHA256 |
|---|---|
| ISO USA v2.00 | `67b6c0075837f3ae1132d608acf2858bf13b2dd62d6eae83dff76df02e4e824f` |
| CORE.GT4 | `85d26aa8430154967b2633eede929286694ac39e99762527edcec365fd642ff9` |
| ELF reconstruído | `10f82e2231a51404b95682ed3ea81171100a1af2fefdeed3391943016c7c935c` |
| Produção gt4boot.exe | `839adaa22f48ba674fc8f5f6c498299a9329598dedf23ca3ee507f1f1c76dfab` |
| PCSX2 v2.9.114 | `0506a196f5bc76d47f618e68d40519c766bbf5959b8aa88d5b91cee1dcb48037` |
| PDB oficial correspondente | `99b483451a79da4237585aa9da2ffc9a844dcd128e8628921a0f12832336de6f` |

Referência dynarec EE fixada pelo ponteiro do motor, sem mudar fastmem ou
configuração de execução. PDB GUID `7833e0ae-b1fc-c437-4c4c-44205044422e`,
age1; fonte oficial aa7ab43. Leitor privado de 108 reutilizado com dois novos
endpoints e observação da escrita. Modelos time3/interrupt4/kernel3/RPC2/translation2.
O observador do modelo tem identidade explícita `slice109-read-only-observer`.
Sua janela é N1225..1303; não é produtor autorizado para retomar produção.

Todos os scripts, fontes, binários, mapas e capturas ficam em `private/`.
33 helpers arquivados e 53 artefatos têm hashes no manifesto; os 16 insumos
do produtor e os símbolos/fontes consultados também estão fixados ali.
Nenhum payload, bloco recompilado ou captura foi acrescentado ao Git.

## Inicialização e primeiro quadro no modelo

Âncoras confirmadas por words do ELF e efeitos do módulo:

| Endereço | Efeito |
|---|---|
| `4A041C`, RA `4A0424` | memset `5A48D8`, destino `70002000`, zero, tamanho `1840` |
| `4A0518`, RA `4A0520` | AddIntcHandler: causa2, handler `4AB430`, argumento inicial0 |
| `5AE818`, RA `5AE820` | EnableIntc2; wrapper salva RA externo `4A055C` em stack+20 |
| `4AB4AC` | `FE230000`, SD do contador, slot comum da BEQ `4AB4A8` |
| `4A23B8` | `FE020008`, copia current para synced |

O zero registrado em N1226 tem **state.pc=107730 atrasado**, início do caller
AOT, cuja word é ADDIU SP. O caminho `107738→4A0398→4A03C0→memset5A48D8`,
RA4A0424 e aritmética do loop qualificam a metade inferior64 do SQ128
`5A4924/7CE80000`: segundo SQ da terceira iteração, destino inicial2000,
offset50, a2 observado17E0. Não é SD em107730. O watch da referência foi
armado depois desse reset; não captura nem demonstra sua execução anterior.

| Serviço | Antes | Charge BUSCLK → resíduo | Escrita e proprietário salvo |
|---|---|---|---|
| N1239 registro | 0/0 | 688128→835584 | Nenhuma escrita de incremento |
| N1242 enable2 | 0/0 | 1130496→1277952 | Nenhuma entrega de quadro nesta janela |
| N1250 WaitSema, RA4A1398 | 0/0 | 2310144+147456→0 | 0→1; update3, PC5ADCE8/RA4A1398 |
| N1260 primeiro frameSleep | 1/0 | 1327104→1474560 | Principal selecionada para continuar |
| N1267 GetThread, fila617D04 | 1/0 | 2359296+147456→49152 | 1→2; main1, PC5ADB98/RA57644C |
| N1284 receiverSleep | 2/2 | 2408448+147456→98304 | 2→3; idle0, PC5ADBC4/RA57689C |

Os três crossings enfileiram INTC2 antes de sua entrega. Os stores exatos
da bridge são `4AB4AC`; o contexto deferido qualifica a posse, não CP0EPC.
Em N1267 o receiver da mensagem ainda não existe; N1272 já chega2/2 e
a principal só instala o receiver `01FFFE30={0,1}` antes de N1284.

**N1284 tem dois advances:** a charge do serviço já enfileira INTC2;
o advance idle posterior de2457600 mantém o resíduo98304 e a mesma entrada
coalescida, acrescentando INTC11. Não atribuir o terceiro incremento somente
ao idle. Serviço100/PC1604 continua sendo retorno deferido, não idle.

## Qualificação do escritor na referência

Primeira tentativa `qualified1`: 42 janelas/294 estados, encerramento normal,
registro/enable0/0, watch armado mas zero hits de escrita embora o contador
chegue3. Foi preservada como controle de endpoints e rejeitada como evidência
do escritor. Também houve zero hits do breakpoint standalone hwIntcIrq2.

A fonte fixa RFASTMEMBASE=RBP. O fastmem escreve pelo endereço virtual
`fastmem_base+70002050`, distinto do backing `eeMem+08002050`. Um watch x64
do backing não acompanha automaticamente a outra alias da mesma memória.
O PDB qualifica `vtlb_private::vtlbdata` RVA321A940, MapData tamanho1058840,
fastmem_base offset1058832. Na repetição `qualified2`, Dr1 observa backing,
Dr2 a alias fastmem; leituras iguais são exigidas antes e depois. Dr0 continua
qualificando as materializações do relógio. Os watches persistem entre os
eventos e são removidos antes do detach/encerramento normal.

**Quatro escritas calibradas:** 0→1→2→3→4. Em todas, RBP+RCX é exatamente
Dr2, RCX/R13=`70002050`, RSI é o novo valor. Store host `48 89 74 0D 00`:
MOV64 [RBP+RCX],RSI. Sequência única lê o mesmo endereço, incrementa1 e grava.

Dois globals `recBlocks` homônimos foram desambiguados pelo recompiler:
igual ao JITCompile EE e diferente do IOP. PDB: BaseBlocks40/recompiler+16/
array+24; BaseBlockArray16/reserved0/count4/pointer8; BASEBLOCKEX24/fnptr0/
startpc8/size12/x86size16. O matching live é único, limitado e read-only.
RecBlocks EE RVA35F66F0 identifica, nas quatro ocorrências, bloco guest
`4AB430`, 32 words, 541 bytes host, RIP pós-store no offset415.
Todos os128 bytes guest coincidem com o ELF; último quarteto LD/DADDIU/BEQ/SD
termina precisamente em `4AB4AC`. Assim o PC do escritor é qualificado
independentemente de **cpuRegs.pc observado4AB460**, que está atrasado.

84 stores de ciclo foram decodificados independentemente como add2 à CPUcycle
e RIP+18; isso qualifica endpoints específicos, não uma conversão universal.
Branch displacements dos blocos JIT podem mudar com links; não são exigidos
iguais entre ocorrências para qualificar o mesmo store e seu endereço.

## Pendência anterior e separação das fases

| Referência qualified2 | current/synced | Cycle raw | Fase / ocorrência |
|---|---|---|---|
| Q42 registro | 0/0 | 1801427955 | STAT=C, MASK1802; INTC2 latched e masked |
| Q49 enable2 | 0/0 | 1801428625 | Mesmo STAT/MASK no endpoint anterior ao syscall; EIE0 |
| Q56 primeiro store | 1/0 | 1801431983 | STAT8/MASK1806; renderMode0/start1801186298 |
| Q71 primeiro frameSleep | 1/0 | 1801445063 | Update bloqueia, main trabalha |
| Q106 receiverSleep | 1/0 | 1801447616 | Principal instala receiver e bloqueia |
| Q113 segundo store | 2/0 | 1805684935 | Receiver01FFFB90={0,1} já existe; GSblankMode2 |
| Q121 terceiro store | 3/2 | 1810606018 | Receiver permanece, depois primeiro update3/3 |

Registro, enable e primeiro store têm a **mesma fase renderMode0**,
start1801186298/delta4498396. O próximo render deadline1805684694 ainda
está no futuro no primeiro store. Não houve novo VBlank start entre esses
endpoints. **Alta confiança:** a entrega decorre da INTC2 já pendente ao
habilitar, em vez de um novo vencimento dessa fase.

Não é entrega imediata dentro de EnableIntc: seu retorno materializado ainda
tem EIE0 e PC5AD9E8; o writer aparece depois. Origem da latch, restauração
EIE, percurso BIOS e proprietário antes da vetorização não foram capturados.
No writer, Cause400/EPC81FE0 são registros observados; não substituem uma
captura do contexto original. hwIntcIrq standalone não atingido pode ter sido
inlined neste caminho; zero hits não prova ausência da sinalização.

Modelo N1239 tem STAT804/MASK800, INTC2 latched mas queue vazia. N1242
apenas habilita a máscara; a primeira entrada/entrega desta janela aparece
na charge1250. **Confirmado:** representação/entrega diferente da referência.
**Desconhecido:** de onde vem o bit2 e se corresponde a ocorrência já entregue
sem acknowledgement, nova pendência ou outro percurso anterior. Não fazer
um patch que reconstitua cegamente a fila do STAT: poderia duplicar entrega.

A fonte atual preserva entries masked já enfileiradas em start_interrupt;
enable_internal altera apenas MASK. advance_busclk gera VBlank apenas com
handler registrado. Portanto a origem/lifetime anterior dos dois componentes
precisa ser qualificada antes de alterar esse contrato.

Delta raw entre registro e receiver=19661. É diferença de endpoints, com
materializações qualificadas; não é custo completo de um serviço nem prova
de alinhamento absoluto com o modelo. Frames1/0 ou3/3 iguais também não o são.

## Trabalho, controles e preservação

N1225-before→N1303-continuation:405460 instruções concluídas,79 serviços
aceitos,405381 instruções fora desses serviços. 80 advances somam14106624
ticks:79×147456+um idle2457600. Entre primeiro frameSleep e getterN1267:
600 instruções, das quais593 fora dos7 serviços. Nenhuma conversão em ciclos
ou alteração do acumulador pelo trabalho fora dos serviços foi adotada.

Controles automáticos e revisão independente:

- Modelo534/534 registros repetidos exatamente; whole observer/repeat CPT
  SHA453f7a8dc7ea4fc3451107b30be6a5bbe2ef170cd7ff5f9d03d48983b7647b69.
- Produção10k wholeCPT igual108. Observador tem todos os mesmos modelos,
  serviços, registradores, RAM, kernel e bancos; só binary provenance difere.
- Três controles fresh10k strict/count-work versus interpreter: estado completo
  e work8946341 idênticos. Não demonstra fidelidade do ambiente compartilhado.
- Map final atribui driver/kernel/state/compare a objetos privados, sem variantes
  dessas unidades do archive. Compare recompilado sem mudança com mesma declaração
  privada de Kernel, sem novos fields/virtuals; não há violação de identidade ODR.
- Referência294/294 estados completos comuns entre tentativa e repetição;
  reduced126/126 estados de42 janelas idênticos. Só sequence/thread/host_rva
  excluídos; nenhum estado guest, fase, banco ou custo foi excluído.
- Três referências encerradas normalmente; owner3, BIOS originais6 e cópias18
  preservados com hashes. Nenhum processo de referência permanece aberto.
- CTest54/54 e Python97(91run/6skip) herdados de107, não repetidos109.
  Produção/exe/module/libs/source não mudaram. Novos controles diagnósticos passam.

O publisher inicialmente parou por caminho errado de recVTLB; archive parcial
foi preservado e finish-publication verificou cada hash existente antes de
completar com o caminho ix86-32. Nenhum manifesto de entrada foi atualizado.
Consultas de PDB com nome de tipo incorreto foram retidas como tentativas,
sem uso como identidade aceita.

Reprodução dos checks, a partir da raiz, sem sobrescrever saídas existentes:
`private/tooling-venv/Scripts/python.exe private/tooling/slice109-qualify-model.py`
e `... slice109-qualify-reference.py`. Asserts verificam dados antes de recusar
um qualification.json já existente. Builds privados usam VS amd64/chcp65001.

## Próxima experiência — fatia110

Rastrear **origem e lifetime da pendência INTC2 antes do registro/reset** nos
dois ambientes: ocorrências, queue, STAT/W1C, delivery, máscaras e gateCPU.
Na referência, incluir fase anterior e retorno/gate antes do primeiro writer;
no modelo, localizar quem deixou STAT2 set com queue vazia. Verificar se a
diferença está em occurrence, acknowledgement ou delivery antes de escolher
a menor correção respaldada por referência e testes de snapshot/dispatch.
Preservar preempção correta,1ms/service e contagem integral de trabalho.
Não ressuscitar boot antigo nem substituir as pendências por wakes forçados.
