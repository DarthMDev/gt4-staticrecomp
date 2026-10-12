# Slice111 — Vulkan nativo com saída real do GT4

2026-10-12, base `bd9ae77`, branch `codex/vulkan-renderer-pilot-111`.
Decisão0053; prioridade do proprietário após o piloto de menus110.

## Resultado e alcance

**Confirmed:** o novo backend executa rasterização, texturas, transferências
e extração do framebuffer por compute Vulkan. Não apresenta uma imagem
desenhada pela CPU. O host liga `PS2_GS=gpu`; o log confirma CPU raster
desabilitado e contadores reais dos nove kernels. Win32 copia o buffer RGBA
produzido na GPU para uma imagem e faz blit para o swapchain, sem download
da imagem para a CPU no caminho de apresentação. Capturas PPM separadas
usam leitura explícita do framebuffer da GPU.

O coldboot com cópia do cartão L alcança menu, seleção de circuito e FordKa01
em3D. Capturas da própria janela mostram Nürburgring e o carro. A execução
de3000quadros termina pelo limite pedido, sem timeout. Não qualificamos
corrida, estabilidade prolongada, todo o GS ou fidelidade do modelo PS2.

## Construção e limites

Donor MIT `snuri00/ps2-web-recomp` congelado em
`1b4808793a12c45125262fd2d5f50a9ec9f7c020`. O gerador verifica cinco fontes
antes de derivar GPU/GS/host em `private/slice111-vulkan/port`. Checkout original
preservado. `ps2game.lib` AOT existente é reutilizada; nenhum interpretador
EE ou JIT EE novo foi acrescentado. As limitações herdadas do runtime e VU
continuam separadas da produção.

Clang23.1.2, C++20/Release, VS2022x64/WindowsSDK, Ninja. Naga30.0.0 com
Cargo.lock de32pacotes transforma SCALE1 e bindings explícitos nos kernels
upload/copy_read/copy_write/load_clut/display/raster/resolve/snapshot/decode.
SPIR-V permanece privado. Headers Khronos1.4.362 têm licença própria
MIT/Apache2; nenhum código de implementação PCSX2/PS2Recomp GPL é ligado.

GPU observada: NVIDIA GeForce RTX3060Laptop, API1.4.351; loader1.4.341.
O backend exige Vulkan1.2, limites auditados e KHRswapchain_maintenance1.
Buffers mapeados coerentes preferem memória também local à GPU. Uma fila,
fences síncronos, barreiras compute→compute/host/transfer; alocações zeradas.
Destrutor libera buffers, pipelines, descriptors, imagem, swapchain, semáforos,
fences, surface/device/instance/loader. A revisão independente encontrou o
uso indevido de queue-idle como prova de consumo do semáforo de apresentação:
corrigimos com fence maintenance1 aguardado por apresentação. A fence prova
segurança dos recursos, não o instante da exibição física no monitor.

Win32 tem fila de mensagens na mesma thread, teclado básico e entrada
programada preservada. Resize/tamanhozero/restore são tratados. Escala1,
blit nearest com proporção4:3, sem composição temporal dos campos. Não há
medição de GPU busy, fullscreen qualificado, áudio ou XInput. Não há camada
Khronos instalada; não declarar execução sem mensagens de validação como
prova de validação Vulkan. OBS/RTSS antigos aparecem no vulkaninfo, sem
diagnóstico causal contra o piloto.

## Controles

Fixture integrada ao unittest existente quando o build privado existe:
sprites sobrepostos em vários tiles, máscara de canal, pixels de display
conhecidos, snapshot/restore e serial de readback consumido uma só vez.
Compara os4MiB completos da VRAM com o raster CPU separado. Transferências
fragmentadas byte a byte passam nos formatos PSMCT32/24/16/16S,
PSMT8/4/8H/4HL/4HH, incluindo preservação de bits vizinhos. O oráculo CPU
compartilha conceitos de endereço do donor; acordo não prova hardware PS2.
Treze apresentações repetidas, tamanhozero recusado e restore/redimensionamento
passam. Builds finais sem warnings.

Launcher existente estendido para runtime separado com executable+shaders
hash-pinned. Continua verificando ISO completo, CORE, ELF e seed; cópias e
saídas privadas novas, argv literal, motivo/quadros/timeout no gate. Controles
negativos recusam runtime/shader/donor alterados e saída pública/checkout
congelado. Nenhum manifesto de jogo é atualizado durante verificação.

## Execuções preservadas

- headless1:1500quadros normais,27.320833s, kernelsGPU ativos. Menu600,
  Arcade1000, Nürburgring1400. Producer inicial congelado na pasta do run.
- window2: primeiro controle de janela; teclado neutralizava a entrada
  programada porque o donor aplica presses antes do callback.1500normais
  mas trajetória diferente; rejeitado como equivalência. Producer exato
  não foi congelado antes de rebuild; log/capturas/result mantidos, sem
  tratá-lo como evidência final reproduzível do renderer.
- window3: correção da precedência;1500normais32.221853s.15/15PPM
  byte-idênticos a headless1, orçamento7372994805 em ambos.
- window4:3000normais121.951303s, navegação estendida até FordKa3D,
  screenshots reais da janela e PPM privados. Não entrou em corrida.
- headless5: confirmação final após guardas de offset64→32 e buffer nulo;
  1500normais24.919064s,15/15PPM idênticos ao inicial.
- window6: build final reproduzido comW4 em todos os targets;1500normais
  30.679208s,15/15PPM idênticos ao inicial. Producer final congelado e
  runtime+shaders fixados para o launcher. Compilador SPIR-V final também
  reproduz9/9binários exatamente em uma pasta separada.

Seisruns/248artefatos e17fontes próprias fixados no manifesto; window2 é
explicitamente rejeitado. Cinco probes aceitos têm0serviços desconhecidos
nos padrões auditados e1warning CFC2(28) cada, herdado, ainda não qualificado.
Nenhuma política temporal foi modificada.

Full CTest54/54 em52.21s e Python108(102run/6skip) em73.505s verdes.
Após as guardas finais, os5controles Vulkan e7do launcher passam novamente.
Produção SHA839adaa22f48ba674fc8f5f6c498299a9329598dedf23ca3ee507f1f1c76dfab
e seed SHA bb10a96fd78e45d648023f9962739c09f152ce01ae45c3fea1d998a71d5e3072
permanecem iguais. Copy_read/copy_write/resolve foram compilados em pipeline
mas não tiveram dispatch nos probes reais; não lhes atribuir cobertura de
execução. Snapshot/decode/CLUT são exercidos pelo jogo, sem oráculo separado
para cada contrato gráfico.

Comparação inicial CPU110 versusGPU111:100/200/300 idênticos; frame400 tem
142bytes RGB diferentes, depois até81972/860160bytes nos quadros comuns.
Não atribuimos isso automaticamente a arredondamento ou tolerância. Causa
primeira ainda **Unknown**. O arquivo de comparação conserva números por
quadro. A aparência do menu e navegação estão confirmadas, precisão pixel a
pixel do renderer completo não. O tempo27.32s versus56.57s do controle CPU
é duração desses probes com logs/capturas, não FPS sustentado de corrida.

Falhas durante desenvolvimento: resolução Cargo offline rejeitada antes
do build locked online; recorte inicial do adapter removia helpers de
endereçamento e foi corrigido antes de execução; fixture de máscara inicialmente
omitira setup-generation, corrigida para reproduzir writes reais do GS.
LoadCursorW corrigido para resource Unicode; macro de proc list refeita
uma função por linha após um recorte mecânico incompleto. Nenhum desses
builds falhos foi aceito como produtor. Não enfraquecemos controles.

## Reprodução e continuidade

`experimental/vulkan/build.cmd` constrói o piloto no ambiente local já
qualificado. Verificar manifesto111 antes de executar:

```powershell
private/tooling-venv/Scripts/python.exe scripts/external_runtime_probe.py `
  private/slice111-vulkan/new-run --frames 1500 --timeout 180 `
  --runtime-manifest docs/inputs/slice111-native-vulkan.json --window `
  --card-seed private/slice110-menu-pilot/name-control4/card.ps2 `
  --press 300:start --press 600:cross --press 900:cross --press 1200:cross
```

Build não altera manifestos; binários distintos exigem nova qualificação.
Próxima fatia: primeira divergência de pixels/validação Vulkan e extensão
controlada até corrida. Optimizar filas ou adotar runtime em produção só com
evidência; áudio/XInput/saves continuam objetivos posteriores.

Referências primárias: [semaphore reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html),
[present fences](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html),
[erros de apresentação](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html).
Hashes/versões/gates/capturas em `docs/inputs/slice111-native-vulkan.json`;
payloads privados, produção e seed originais preservados.
