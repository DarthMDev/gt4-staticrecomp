# Build do caminho atual

## Ambiente qualificado

Windows x86-64, VS2022 Build Tools/SDK10.0.26100.0, clang-cl23.1.2,
CMake3.24+, Ninja, Rust/Cargo com Cargo.lock e Naga30.0.0.
Python local em `private/tooling-venv`, com `scripts/requirements.txt`
(pycdlib1.20.0). A GPU qualificada é RTX3060Laptop; o backend exige Vulkan1.2
e swapchain maintenance1. Não há evidência de validação Khronos completa.

`native/build.cmd` entra no ambiente VSx64 e seleciona UTF8 antes do CMake.
Este build Clang substitui o antigo build MSVC do motor original.

## Pré-requisitos privados

| Local | Papel |
|---|---|
| `private/references/ps2-web-recomp` | Donor MIT no commit1b4808793a12c45125262fd2d5f50a9ec9f7c020 |
| `private/reference-acceleration-study/runtime-build/ps2game.lib` | Biblioteca AOT do jogo, SHA256c837d13dae027f377aecdb0c39834ede2e9463eabf7f767fda6a3c5a0762de1f |
| `private/reference-acceleration-study/generated` |326 unidades C++ EE e registro que produziram essa biblioteca |
| `private/tooling/pcsx2-slice101-source/3rdparty/vulkan/include` | Headers Khronos; nenhuma implementação PCSX2 ligada |
| `private/tooling-venv/Scripts/python.exe` | Ambiente Python |
| ISO raiz, `private/fingerprint-check/CORE.GT4`, `private/reconstructed/SCUS_973.28.elf` | Inputs hash-pinados em docs/inputs |
| Seeds privadas110/113 | Cartões derivados da interface original; nunca distribuídos |

O build verifica fontes do adapter donor e recusa uma biblioteca AOT com hash
diferente. Launcher verifica ISO/CORE/ELF/runtime/shaders/card seed antes de usar.
Pins não podem ser atualizados para disfarçar uma divergência.

**Limite de reprodução:** não há ainda um comando público completo que extraia
e gere todos os inputs a partir de uma ISO. A biblioteca foi gerada anteriormente
com o tradutor donor e Clang; scripts privados históricos e evidence do estudo
preservam essa origem. O build atual recompila runtime/host/adapters e reutiliza
a biblioteca qualificada. Não confundir isso com um build independente de
checkout vazio. O código gerador externo continua disponível no donor; o motor
original arquivado não é usado para executar o jogo.

## Compilar e verificar

Na raiz:

```powershell
cmd /c native\build.cmd
ctest --test-dir private/slice114-native-base/build --output-on-failure
```

CTest contém os controles GPU/Vulkan, áudio/input Windows e suite Python ativa.
Os54 testes C++ do motor original foram arquivados junto com o código removido,
sem alterar suas expressões. Isso muda a população de testes; não torna seus
resultados históricos prova do runtime donor.

Teste Python separado, quando necessário:

```powershell
private/tooling-venv/Scripts/python.exe -m unittest discover -s tests/python
```

Execute builds/testes serialmente. Linkers podem bloquear outputs, e uma sessão
manual mantém trava no cartão. Não feche o jogo do dono para testar sem necessidade.

## Produtores e launcher

Esta fatia gera adapter/shaders/build em `private/slice114-native-base/`,
sem sobrescrever os produtores111–113. O launcher usa
`docs/inputs/slice114-native-base.json`. Sua publicação requer build/testes e
hashes qualificados; recompilar fontes diferentes não autoriza repinar o exe.
Futuras fatias devem escolher diretórios novos antes de compilar e preservar os
produtores aceitos. Binaries/SPIR-V/generated code ficam fora do Git.

`Jogar-GT4.cmd` mantém `private/manual-play/card.ps2`, backup por sessão e trava
de exclusividade. Ele nunca ressemeia um cartão existente. Para iniciar um novo
cartão, usa a seed16:9 ou4:3 correspondente ao argumento `--aspect`.
