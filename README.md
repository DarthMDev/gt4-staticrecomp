# GT4Recomp

**Gran Turismo 4 no PC. Preservar a experiência original e abrir caminho para o que vem depois.**

GT4Recomp é um projeto de recompilação de **Gran Turismo 4 USA v2.00 para
Windows x86-64**. Queremos transformar o clássico do PlayStation 2 em uma
experiência para PC com desempenho estável, gráficos configuráveis e uma base
de código que permita estudar, modificar e expandir o jogo.

**O jogo já inicia, navega pelos menus e entra em corrida real.** A base atual
tem Vulkan, áudio no Windows, teclado, implementação de controles XInput e
cartão de memória persistente. Estamos na fase de desenvolvimento experimental:
o próximo desafio é transformar esse progresso em uma experiência confiável
para jogar.

[Estado detalhado](docs/STATUS.md) · [Plano de desenvolvimento](docs/ROADMAP.md) · [Guia de build](docs/BUILD.md)

## Onde estamos hoje

| Recurso | Estado atual |
|---|---|
| **Boot e menus** | Inicialização real, menus navegáveis e seleção de carros e circuitos |
| **Gameplay** | Corrida em Nürburgring já acessada e controles exercitados; estabilidade de corridas completas ainda em validação |
| **Vulkan** | Backend nativo para Windows renderizando a saída real do jogo na GPU |
| **Tela 16:9** | Padrão do projeto, usando a opção original do GT4 para câmera e HUD; 4:3 também disponível |
| **Modos de vídeo** | 480i, 480p e 1080i testados manualmente em gameplay, todos em 16:9 |
| **Áudio** | Saída estéreo de 48 kHz no Windows; continuidade ainda precisa melhorar |
| **Controles** | Teclado funcional e XInput implementado com analógicos e vibração; controle físico ainda não testado |
| **Persistência** | Perfil e configurações salvos pelo jogo, cartão persistente com backup por sessão e proteção contra uso simultâneo |

**Ainda há trabalho importante antes de uma experiência estável:** desempenho
baixo em cenas 3D, tempo entre quadros inconsistente, interrupções de áudio e
relatos de acionamento duplo no teclado. Compatibilidade gráfica, saves além
dos cenários já testados e estabilidade prolongada continuam em validação.

O teste manual mostrou 480p como o modo mais estável até agora. Os modos
entrelaçados funcionaram, mas sua fidelidade visual e temporal ainda não está
completamente qualificada.

## A experiência que queremos construir

O primeiro objetivo é **jogar GT4 com estabilidade e velocidade correta no PC**.
A partir dessa base, queremos desenvolver:

- **FPS independente da velocidade do jogo:** renderização em 144 FPS ou na
  frequência do monitor, preservando física, cronômetros e velocidade original.
- **Resolução interna configurável:** 1920×1080, 2560×1440, 3840×2160 e outras
  resoluções, com saída progressiva.
- **Ultrawide na gameplay:** câmera adaptada e HUD reposicionado, preservando
  proporções e legibilidade.
- **Melhorias gráficas:** espaço para aprimorar efeitos, texturas e opções do
  renderizador Vulkan.
- **Configuração simples:** launcher para ajustar vídeo e controles antes de
  iniciar o jogo.
- **Código legível e modificável:** avançar na decompilação para facilitar
  pesquisa, correções e futuras modificações.

Esses são **objetivos de desenvolvimento**, não recursos já disponíveis.
Atualmente a escala interna continua original; aumentar a janela não aumenta
a resolução interna. Da mesma forma, ainda não temos renderização a 144 FPS
independente da simulação.

## Como funciona

O código principal do jogo é traduzido antecipadamente para C++ e compilado
para executar em **x86-64**. A integração com o PC usa janela Windows,
**Vulkan**, áudio e controles nativos.

A arquitetura atual é uma **recompilação híbrida**: IOP e unidades vetoriais
ainda são interpretados, e serviços e hardware do PS2 são reproduzidos em
software. O Vulkan atual também preserva regras do sistema gráfico original.
Isso não impede evoluir a apresentação, o controle de tempo e o renderizador;
a migração de subsistemas será feita conforme a necessidade demonstrada.

Detalhes para quem quer conhecer a implementação: [arquitetura](docs/ARCHITECTURE.md).

## Próximos passos

1. Tornar a geração do código recompilado e o build completos e reproduzíveis.
2. Qualificar controles, áudio, saves e estabilidade em mais situações reais.
3. Medir os custos de execução e melhorar desempenho e regularidade dos quadros.
4. Separar tempo do jogo e apresentação; depois, gerar novas cenas entre os
   passos da simulação para oferecer taxas de renderização maiores.
5. Evoluir saída progressiva, resolução interna e opções gráficas.

Trabalhamos em etapas pequenas, com resultados registrados e verificações antes
de adotar mudanças. O [plano técnico](docs/ROADMAP.md) explica a sequência,
as dependências e os critérios de validação. Não há prazo anunciado para
concluir essas funcionalidades.

## Experimentar e desenvolver

O projeto ainda exige um **ambiente de desenvolvimento com arquivos privados
preparados**. Este repositório não distribui o jogo, ISO, BIOS, código traduzido,
executáveis ou saves. O build atual também depende de uma biblioteca recompilada
privada já gerada; a receita completa desde a ISO ainda é trabalho pendente.

Com esse ambiente preparado, `Jogar-GT4.cmd` abre o jogo, verifica os arquivos
necessários e preserva o cartão existente. Requisitos e instruções estão no
[guia de build](docs/BUILD.md).

| Tecla | Ação |
|---|---|
| Enter | Start / pausa |
| Z | Confirmar / acelerar |
| A | Frear |
| S | Triângulo / voltar |
| X | Círculo |
| Setas | Navegar / dirigir |
| F6 | Alternar enquadramento 16:9 / 4:3 |

F6 altera o enquadramento da janela. Use a mesma proporção em **Options →
Screen → Aspect Ratio** no jogo para acertar câmera e HUD.
`Jogar-GT4.cmd --aspect 4:3` inicia com enquadramento 4:3. Cartões existentes
mantêm as configurações salvas. Os modos 480p e 1080i devem ser conferidos em
corrida; os menus podem usar o modo normal.

## Bases, histórico e documentação

A base de execução atual vem do projeto MIT
[ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp), adaptado para
Windows e Vulkan. O nosso executável roda diretamente no Windows, sem navegador
ou WebAssembly e sem PCSX2 embutido. Créditos, versões e limites de uso estão em
[THIRD_PARTY.md](native/THIRD_PARTY.md).

O desenvolvimento já passou por boot e menus, integração Vulkan, áudio e
controles, corrida real e modos de vídeo. O motor original de recompilação
foi preservado na branch
[`codex/archive-original-recomp-20261013`](https://github.com/moesuito/gt4-staticrecomp/tree/codex/archive-original-recomp-20261013).
A `main` concentra a base atual.

- [STATUS](docs/STATUS.md): progresso confirmado e pendências.
- [ROADMAP](docs/ROADMAP.md): plano para velocidade, renderização e evolução nativa.
- [ARCHITECTURE](docs/ARCHITECTURE.md): funcionamento da base atual.
- [BUILD](docs/BUILD.md): ambiente e verificações.
- [LEGACY](docs/LEGACY.md): preservação e recuperação do trabalho anterior.

O código ativo está em `native/`, com ferramentas em `scripts/`, testes em
`tests/` e registros técnicos em `docs/`. O fork de desenvolvimento é
[`moesuito/gt4-staticrecomp`](https://github.com/moesuito/gt4-staticrecomp).
