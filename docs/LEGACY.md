# Arquivo do motor original

Antes da limpeza, a branch
[`codex/archive-original-recomp-20261013`](https://github.com/moesuito/gt4-staticrecomp/tree/codex/archive-original-recomp-20261013)
foi publicada em origin no commit `95f615519b992c65505d77da35af825fc6a13a53`.
Ela guarda o estado completo versionado: motor original, ferramentas, testes,
planos, handoffs e todas as evidências anteriores, inclusive a base nativa113.
O histórico Git anterior continua intacto; nenhum commit foi reescrito.

## Retirado da main

411 arquivos legados: motor `src/`/`include/`, ferramentas `tools/gt4*` e
helpers exclusivos, testes C++/Python específicos desse motor, scripts de
observação Ghidra/PCSX2 e documentação/planos/handoffs supersedidos.
Seu trabalho continua consultável na branch, mas não é um segundo produto
ativo nem aparece nos comandos de build/teste da main.

O código Vulkan/Win32/áudio/XInput foi movido de `experimental/vulkan/` para
`native/`. Permaneceram scripts, testes e manifests que o caminho atual usa,
mais relatórios/decisões/lessons110–113 que explicam a migração.

Links/paths e gates antigos nesses relatórios/manifests são evidências do
produtor daquela data; fontes removidas agora estão na branch de arquivo,
ou nas cápsulas privadas. Não editar pins históricos para refletir uma limpeza.

## Material ignorado

A branch preserva somente conteúdo versionado. ISO, BIOS, CORE/ELF, bibliotecas,
código gerado, ferramentas locais, captures e cards nunca estiveram no Git.
Todos foram preservados em seus caminhos locais, incluindo outputs do motor
original necessários às evidências. `build/` e `generated/` antigos são locais
inativos, não targets atuais; não foram apagados nem religados à main.
Saves e a janela manual do dono não foram alterados por esta limpeza.

## Consultar/restaurar

Consulte a branch no GitHub sem trocar o checkout ativo. Para recuperar um
arquivo específico, escolha seu path na branch e faça uma alteração documentada;
não retorne automaticamente o projeto inteiro ao estado antigo. Um checkout
separado pode reconstruir o motor arquivado quando uma investigação o exigir.
Inputs privados continuam exigindo suas verificações e compatibilidade originais.
