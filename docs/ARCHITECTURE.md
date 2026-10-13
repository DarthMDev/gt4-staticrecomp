# Arquitetura ativa — Windows nativo com subsistemas PS2

2026-10-13, decisão0056/fatia114. Evidência obtida no código congelado do
donor1b4808793a12c45125262fd2d5f50a9ec9f7c020 e nos adapters locais.

## Confirmado

O programa é um executável PE Windows x86-64, compilado por clang-cl, ligado
a uma biblioteca estática privada de código EE traduzido. Não carrega um
navegador, um módulo WebAssembly ou uma implementação PCSX2. O EE não é
executado por um interpretador genérico de instruções: `dispatch.cpp:126–159`
despacha para código traduzido e para quando falta código. `generated/table.cpp`
do produtor privado registra326 unidades EE.

Isso não elimina a emulação de subsistemas:

| Subsistema | Implementação confirmada no donor |
|---|---|
| IOP | `runtime/src/iop/cpu.cpp:61–74` busca/decodifica instruções R3000; `iop/kernel.cpp:144–167` liga imports a serviços HLE |
| VU0/VU1 | `runtime/src/vu_micro.cpp:788–812` usa microprograma compilado quando registrado, senão interpreta |
| JIT VU | `runtime/src/vu_jit_web.cpp:18–30` retorna modo0 fora de Emscripten; não há JIT Windows ativo |
| Registro AOT VU | Nenhum registro gerado no conjunto atual; `vu_micro.cpp:978` oferece o registro weak vazio |
| Kernel/hardware | Modelos de memória, timers, interrupções, DMA, VIF, disco, SIF/IOP, IPU, SPU2 e cartão |
| GS | Algoritmos/memória PS2 preservados no batching e compute shaders; nosso adapter usa Vulkan diretamente |

Portanto o termo preciso é **recompilação híbrida nativa**. O fato de um
interpretador ser compilado para x86-64 não transforma o código que ele
interpreta em AOT. Chamar o produto de nativo não justifica afirmar ausência
de emulação ou fidelidade perfeita dos modelos de hardware.

## Integração que implementamos

`scripts/prepare_external_vulkan.py` verifica fontes donor antes de gerar
adapter GPU/GS/host privado. `native/native_vulkan.cpp` executa Vulkan compute
e apresenta por Win32; `window.cpp` integra teclado/XInput e eventos.
`host_io.cpp` entrega PCM48k ao WinMM e traduz o controle para botões/analógicos
do jogo. O launcher verifica executável/shaders/inputs e protege o cartão.

O pipeline gráfico atual usa escala interna1 e leitura GS por regras originais.
Viewport16:9 combina com opção original GT4 para câmera/HUD. Isso não implementa
resolução interna4K,144 imagens novas por segundo ou física independente.

## Separação das bases

- ps2-web-recomp MIT: runtime e tradutor usados no produtor privado atual.
- gt4-decomp: referência estudada para metadata/decompilação; não é nosso executável.
- PS2Recomp: referência estudada; nenhuma implementação GPL importada/ligada.
- PCSX2: ferramenta histórica de comparação e local de headers Khronos;
  nenhum motor de emulação PCSX2 ligado ao jogo.
- Recompiler/runtime EE original deste projeto: agora na branch de arquivo,
  sem target/build/test ativo na main.

## Unknown

Fidelidade integral EE/IOP/VU/GS/SPU2, estabilidade longa, causa da lentidão,
duplicação de input, continuidade acústica e validação física de XInput.
As observações antigas do modelo original não validam automaticamente este
runtime diferente. O proprietário pediu registrar desempenho/input por enquanto;
esta análise de arquitetura não é um perfil de desempenho nem uma correção.
