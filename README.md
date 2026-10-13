# GT4Recomp

Gran Turismo 4 USA v2.00 para Windows x86-64, com código principal recompilado
e integração nativa de Vulkan, áudio e controles. O objetivo inicial é executar
o jogo de forma funcional e estável. Depois, tornar seu código legível e
modificável e desenvolver melhorias gráficas e de jogabilidade.

A base de execução atual vem de
[ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp), congelada no commit
`1b4808793a12c45125262fd2d5f50a9ec9f7c020`. Nosso port usa Windows e Vulkan;
não executa uma página web nem WebAssembly.

## É nativo? Ainda existe emulação?

**Sim, o executável e o código principal do jogo são nativos. Ainda existe
emulação de subsistemas.** Trata-se de uma recompilação híbrida:

| Parte | Caminho atual |
|---|---|
| CPU principal EE/R5900 | Código do jogo traduzido antecipadamente para C++ e compilado para x86-64; despacho para funções traduzidas |
| IOP/R3000 | Interpretador executa os módulos originais do disco; kernel e bibliotecas são implementados no runtime |
| Unidades vetoriais VU0/VU1 | Interpretadas nesta configuração Windows; não há microprogramas AOT registrados na biblioteca atual nem JIT Windows ativo |
| Gráficos GS | Memória e regras do GS reproduzidas por compute shaders Vulkan na GPU; apresentação Win32 |
| Hardware e serviços PS2 | Modelos de software para memória, DMA, interrupções, kernel, disco, IPU, SPU2 e outros serviços |
| Integração com o PC | Janela Win32, saída PCM WinMM, teclado e XInput |

Não há PCSX2 embutido nem execução do EE por seu interpretador. Isso não
significa que todo o PS2 tenha virado código de jogo nativo ou que seus
dispositivos tenham desaparecido. Detalhes e evidência:
[arquitetura atual](docs/ARCHITECTURE.md).

## O que já funciona

- Boot real, menus, seleção de carros e corrida em Nürburgring.
- 16:9 nativo do jogo e apresentação16:9 por padrão;4:3 opcional.
- 480i,480p e1080i funcionaram no teste manual do proprietário, todos16:9.
 480p foi subjetivamente o mais estável; isso não comprova fidelidade de campos.
- Áudio estéreo48k gerado pelo jogo e entregue ao Windows. Há starvation com
 a lentidão atual; continuidade acústica completa não foi qualificada.
- Teclado e implementação XInput com botões, analógicos e vibração. O controle
 físico ainda não foi testado; setas no teclado têm relato de acionamento duplo.
- Cartão privado persistente, perfil/configurações salvos pelo jogo, backup
 por sessão e trava contra duas instâncias usando o mesmo cartão.

O jogo ainda apresenta desempenho baixo e frametime inconsistente. Há
divergência conhecida entre renderização CPU/GPU e um aviso herdado CFC2(28).
Não declaramos estabilidade geral, corrida completa,100% de velocidade ou
fidelidade integral do PS2. [Estado e pendências](docs/STATUS.md).

## Como abrir e controlar

Com os inputs privados e o runtime verificado presentes, abra `Jogar-GT4.cmd`.
O launcher verifica hashes e preserva o cartão existente.

| Tecla | Ação |
|---|---|
| Enter | Start / pausa |
| Z | Confirmar / acelerar |
| S | Triângulo / voltar |
| Setas | Navegar / dirigir |
| A | Frear |
| X | Círculo |
| F6 | Alternar enquadramento16:9/4:3 |

F6 muda a apresentação. Selecione a mesma proporção em **Options → Screen →
Aspect Ratio** no GT4 para acertar câmera/HUD. `Jogar-GT4.cmd --aspect 4:3`
abre no enquadramento4:3. Cartões novos usam a seed correspondente; cartões
existentes mantêm as configurações que o jogo salvou. Modos480p/1080i devem
ser configurados e conferidos na corrida; menus podem voltar ao modo normal.

## Build e organização

`native/build.cmd` é o build atual, Clang/C++20 para Windows. Os requisitos,
diretórios privados e testes estão em [BUILD.md](docs/BUILD.md). Ele reutiliza
uma biblioteca AOT privada previamente gerada: ainda não existe uma receita
completa pública de ISO até executável. Game data, código traduzido, bins,
shaders compilados, imagens e saves não são distribuídos neste repositório.

| Diretório | Conteúdo |
|---|---|
| `native/` | Backend Vulkan, janela, áudio/XInput e fixtures atuais |
| `scripts/` | Verificação de inputs, derivação do adapter e launcher |
| `tests/python/` | Contratos de inputs, launcher, persistência e fixtures nativas |
| `tools/wgsl_spirv/` | Conversão WGSL→SPIR-V com dependências pinadas |
| `docs/` | Estado, arquitetura, decisões e evidência da base atual |
| `private/`, `generated/`, `build/` | Material local ignorado; não é conteúdo público |

O desenvolvimento passou pelo estudo externo, boot/menu110, Vulkan111,
áudio/XInput/corrida112,16:9 e modos de vídeo113. A fatia114 adota essa base
como caminho único da `main` e arquiva o motor original. O histórico completo
anterior à limpeza está na branch
[`codex/archive-original-recomp-20261013`](https://github.com/moesuito/gt4-staticrecomp/tree/codex/archive-original-recomp-20261013).
[O que foi arquivado](docs/LEGACY.md).

## Próximas prioridades

Registrar e posteriormente investigar input duplicado e desempenho3D;
qualificar controle físico, áudio, saves e estabilidade. Depois, separar
renderização e física para144FPS/refresh do monitor mantendo100% da velocidade,
fixar saída progressiva, implementar resolução interna1080p/1440p/4K e um
launcher de configuração. Efeitos, texturas e ultrawide com câmera/HUD corretos
continuam no plano. Essas melhorias ainda não estão implementadas.

Licenças e limites das bases externas: [THIRD_PARTY.md](native/THIRD_PARTY.md).
O fork de trabalho é `moesuito/gt4-staticrecomp`; não publicamos no upstream antigo.
