# Estado atual

2026-10-13 — fatia114, adoção da base nativa e limpeza verificadas.
Base95f6155; branch de trabalho `codex/cleanup-native-base-114`.

## Caminho ativo

`native/` + runtime MIT ps2-web-recomp congelado em
1b4808793a12c45125262fd2d5f50a9ec9f7c020. EE traduzido AOT para x86-64,
IOP/VU interpretados e serviços/hardware PS2 em software. Vulkan compute,
Win32, WinMM e XInput. Não usa navegador/WASM/PCSX2 embutido.

Motor original, ferramentas e testes específicos saíram da main; preservados
em `codex/archive-original-recomp-20261013`, commit95f6155, publicado em origin
antes da limpeza. Dados privados/saves/produtores congelados continuam locais.

## Resultados confirmados e limites

- Boot/menu, seleção e corrida real Nürburgring; controles limitados de9000quadros.
- 16:9 do jogo/câmera/HUD e enquadramento16:9 padrão;4:3 opcional.
- Proprietário testou480i/480p/1080i em gameplay, todos16:9;480p subjetivamente
  mais estável. Confirmação funcional humana, sem prova independente de campos.
- PCM estéreo48k entregue ao Windows; fila vazia/continuidade ainda aberta.
- Código XInput/analógicos/vibração com fixtures; controle físico não testado.
- Cartão persistente, perfil/configurações salvos, backup e trava de sessão.

Desempenho/frametime ruins registrados:19FPS corrida,11FPS carro3D,
18FPS/72.5ms no print1080i; métricas do overlay não calibradas independentemente.
Setas de teclado dão dois movimentos por toque, segundo dono; outros botões
suspeitos. Causas desconhecidas. Pedido vigente: apenas registrar, sem auditoria
ou correção de desempenho/input. Janela manual e cartão não são manipulados
por esta limpeza. Fidelity CPU/GPU, CFC2(28), interlace temporal,100% realtime,
estabilidade prolongada, corrida concluída e saves amplos continuam abertos.

## Build e verificação

Build raiz agora só contém o caminho atual Clang/C++20, gerado por
`native/build.cmd` em `private/slice114-native-base/`. Runtime113 e noveSPIR-V
originais permanecem preservados. Biblioteca AOT privada é pré-requisito,
validada por hash; geração completa desde ISO ainda não é automatizada.
Build novo sem warnings;3/3CTest11.13s,32Python sem skips (repetição final5.812s)
verdes. Smoke headless120quadros termina normalmente com cópia de cartão,
sem navegar/abrir nova corrida.9SPIR-V iguais ao113; pins congelados preservados.
Os54CTest/117Python originais são históricos: testes do motor removido ficam
na branch de arquivo; a suite ativa cobre o código que permaneceu.

## Próximo trabalho, quando autorizado

Planejamento solicitado após114: [ROADMAP.md](ROADMAP.md). Não é necessário
eliminar toda a emulação para separar velocidade/FPS. Primeira recomendação:
geração EE/build reproduzível; depois referência medida e apresentação/tempo
independentes. VU/IOP nativos por custo comprovado;144 imagens novas exigem
hooks de atualização/renderização do GT4 e estados visuais interpolados.
Somente leitura de código e documentação nesta revisão, sem perfil/execução.

Qualificar input/controle físico, áudio, saves e estabilidade; mapear perdas
de desempenho sem presumir o gargalo. Depois, separar renderização/física
para144FPS/refresh mantendo100% velocidade; saída progressiva, resolução
interna1080p/1440p/4K e launcher. Efeitos/texturas/ultrawide câmera/HUD depois.
Nenhuma dessas melhorias foi implementada na limpeza.

Entrada técnica: [arquitetura](ARCHITECTURE.md), [build](BUILD.md),
[arquivo legado](LEGACY.md), [relatório114](reverse-engineering/slice114-native-base-cleanup.md).
Evidência funcional anterior: relatórios110–113, manifests correspondentes e
`inputs/slice113-owner-results.json`. A fatia114 não reclassifica esses resultados
como fidelidade integral ou benchmark.

README atualizado para apresentação pública: visão, recursos confirmados,
limites experimentais e objetivos futuros. Mudança editorial, sem alterar
runtime, requisitos, prioridades ou resultados de validação.
