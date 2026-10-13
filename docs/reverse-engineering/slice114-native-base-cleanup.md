# Slice114 — adoção da base atual e limpeza do legado

2026-10-13, baseline95f615519b992c65505d77da35af825fc6a13a53,
decisão0056. Pedido do proprietário: preservar estado numa branch, retornar
ao fluxo main, limpar o motor original e atualizar visão do projeto.

## Confirmado: preservação e dependências

Branch origin/codex/archive-original-recomp-20261013 publicada antes de qualquer
remoção, apontando ao baseline. Origin/main foi sincronizada por fast-forward
(já atualizada); trabalho isolado em codex/cleanup-native-base-114.
Plano privado lista411 arquivos removidos entre466 versionados: docs294,
include17,src20,tests48,scripts12,tools16 e4 feedbacks/planos/handoff raiz.
Cada target foi conferido como arquivo dentro do workspace. Nenhum private,
generated,ISO,build antigo,card ou captura foi removido. Histórico preservado.

Auditoria read-only separada conferiu dependências transitivas. Launcher ainda
usa gt4disc/fingerprint, pycdlib e manifests de inputs/seeds; estes permaneceram.
Nenhuma dependência ativa importa/compila src/include/tools-gt4/scripts-Ghidra/
PCSX2/testes do motor original. Native14 sources movidos de experimental/vulkan
para native; CMake raiz agora só contém runtime/host/fixtures dessa base.

## Confirmado: o significado de nativo

EE AOT326 unidades C++ registradas, compiladas para x86-64; dispatcher nativo
recusa código ausente. IOP interpreta R3000 e usa HLE. VU runtime tem suporte
a programas compilados registrados, mas o conjunto gerado atual não registra
VU AOT; VU JIT é desabilitado fora de Emscripten, logo VU Windows interpretado.
Hardware e dispositivos continuam modelos de software. Vulkan/Win32/WinMM/
XInput são integração nativa. Não há browser/WASM/PCSX2 embutido.
ARCHITECTURE.md guarda paths/linhas da leitura; não é medição de performance.

## Build e controles

Novo diretório private/slice114-native-base mantém todos os produtores antigos
intactos. Build Clang23.1.2/C++20/VS2022SDK/Ninja e Naga30.0.0 reproduz os
9SPIR-V byte-identicamente. Biblioteca AOTc837d13d...6de1f é pré-requisito
verificado agora pelo CMake. Geração da biblioteca continua privada/histórica,
sem promessa de reprodução desde checkout vazio.
Novo executávelc634a3d32c9b3c32d2b0ab9d769d0682572a2f35761e397669e044117e2dc033.
Manifest114 seleciona a nova base; script de probe sem argumento de manifest
também usa114, eliminando o default obsoleto de ps2run/CPU.
Seus checks de tamper/path/seed/proteção/card/timeout/encerramento permanecem.
Build sem warnings;3/3CTest11.13s inclui32Python (6.439s); repetição Python
final32/32 sem skips5.812s após simplificar o default do probe. Nenhuma expressão
de aceitação mantida foi afrouxada. Testes antigos acompanham o motor arquivado.
Smoke120headless termina normalmente/589983844budgetcycles em2.081s com cópia
de seed, sem input automático, sem janela e sem manipular cartão manual. Isso
é um smoke de ligação/execução; não prova nova corrida nem100%realtime.

Captureframe100 é byte-idêntica à capture preservada113/options-nav1, mesmo
input/seed. Manifest114 congela60 artifacts privados e27 sources de publicação,
incluindo produtor imutável e portas geradas. Revisão read-only final não achou
dependência ativa em arquivo removido nem bloqueador na arquitetura/documentação.

## Unknown e exclusões pedidas

Não investigar/corrigir desempenho ou input duplicado. Não manipular sessão
manual/cartão do proprietário. Não testar novas corridas/modos automaticamente.
Sem novo clock,physics144FPS,escalaHD/4K,deinterlace,serviços ou tradução.
Os resultados humanos480i/480p/1080i continuam separados de fidelidade de campos.
Fixtures da base antiga são recuperáveis na branch; sua remoção não é afrouxar
uma expressão ativa, é retirar uma implementação inteira por escolha do dono.
