# 0056 — Adotar base nativa e arquivar motor original

2026-10-13; pedido explícito do proprietário, baseline95f6155.
Status: aceita; build e gates atuais verdes, fatia114.

## Decisão

Preservar o estado versionado anterior em origin/codex/archive-original-recomp-20261013
antes de remover legado da main. Usar ps2-web-recomp/MIT + nosso port nativo
como único caminho ativo. Mover adapters para native/, substituir o CMake raiz
pelos targets atuais e conservar apenas testes que cobrem o código mantido.
Preservar inputs/saves/evidências privadas e produtores históricos em seus paths.

O proprietário pede projeto compreensível e um caminho de desenvolvimento.
Manter dois motores ativos e um README sobre o motor que já não roda o piloto
confunde build, estado e evidência. A branch mantém o trabalho independente
original para referência sem exigir sua manutenção em cada nova entrega.

## Limites e trade-offs

Adotamos as dependências de execução do donor: IOP/VU interpretados e hardware
PS2 em software. Executável/EE/Vulkan nativos não significam zero emulação.
Os contratos/testes do motor antigo ficam no arquivo; não validam automaticamente
este ambiente diferente. Fidelidade/estabilidade/performance permanecem abertas.
Build atual reutiliza ps2game.lib privada hash-pinada; geração integral desde
ISO ainda não é uma receita pública automatizada.

Nenhum ajuste de tempo/física/input/renderização foi autorizado nesta limpeza.
O pedido anterior de só registrar desempenho e teclas duplicadas continua vigente.
Trocas de caminho/manifest precisam preservar validações de inputs, shaders,
seed/backups/travas e critérios de encerramento; novos outputs não sobrescrevem113.

## Verificação exigida

Branch de arquivo publicada e apontando ao baseline; build novo sem warnings;
CTest/Python atuais verdes;9SPIR-V iguais ao produtor113; runtime/game-library/
seeds/inputs congelados preservados; imports/build sem dependências no motor
retirado; README/AGENTS/STATUS e origem das claims atualizados. O relatório114
registra as contagens, hashes, limites e encerramento desta decisão.
