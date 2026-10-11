# Ghidra Emotion Engine Reloaded — avaliação documental

Data: 2026-10-11. Baseline: `d284c79`. Pedido do dono: avaliar se vale pausar
a continuação para trocar o Ghidra genérico pelo suporte EE Reloaded.
Escopo: leitura de fontes locais e documentação oficial; nenhuma instalação,
importação, build, medição de jogo ou slice 101 iniciada.

## Uso existente e limites

**Confirmed nos registros e scripts:** a comparação M6/M15/M16 usa Ghidra
12.1.3 e linguagem `MIPS:LE:64:64-32addr`, não a linguagem R5900 da extensão.
Ver `m6-disassembly.md`, `m15-cop1-mmi.md`,
`m16-unaligned-and-multiply.md` e `scripts/ghidra/CompareDisassembly.java`.
O resultado histórico M16 foi 594 correspondências, zero divergências,
34 linhas classificadas separadamente como `r5900-only`. Estas 34 não foram
validadas pelo Ghidra genérico; o projeto registra uso de tabelas da referência
e fixtures para essas famílias. Não confundir ausência de comparação com
confirmação independente por esse disassembler.

**Confirmed no build:** `CMakeLists.txt` gera o módulo completo executando
`gt4translate` sobre CORE, não exportando a descompilação do Ghidra. Mudar a
ferramenta de análise não substitui automaticamente o decoder, interpretador,
tradutor ou runtime, nem exige descartar o C++ existente.

**Confirmed no histórico:** `docs/environment.md` registra Ghidra já disponível
na instalação fornecida pelo dono na auditoria inicial; isso não prova a
motivação para preferi-lo. `docs/references/used-tools.md` registra EE Reloaded
avaliado, mas não instalado; `GPT_FEEDBACK.md` seção 17.6 fixa a avaliação em
`ae013ee1475dc970db4fdeba3ec88def6b933d43` e distingue importação de replay.
`PLAN.md` o lista para análise EE/símbolos/importação, com validação de suporte
e encodings pendente. **Unknown:** a motivação original completa para não
instalar; não foi localizado um registro comparativo que demonstre rejeição
por qualidade, incompatibilidade ou superioridade do Ghidra genérico.
**High confidence / interpretação:** usar MIPS base foi suficiente para o
escopo inicial de importação/bytes e comparação de instruções comuns, mas
deixou a auditoria independente das extensões EE menos abrangente.

## O que a extensão oferece

**Confirmed na documentação do mantenedor consultada hoje:** é uma extensão
do Ghidra para PS2; anuncia disassembly/decompilation de MMI e VU0 macro,
importação de saves PCSX2, análise de referências globais e recuperação STABS
quando o ELF contém `.mdebug`. Não foi demonstrada presença de `.mdebug` no
nosso ELF reconstruído nem recuperação de símbolos do GT4 nesta avaliação.

- [README oficial](https://github.com/chaoticgd/ghidra-emotionengine-reloaded)
- [Release v2.1.37](https://github.com/chaoticgd/ghidra-emotionengine-reloaded/releases/tag/v2.1.37)
- [Changelog](https://github.com/chaoticgd/ghidra-emotionengine-reloaded/blob/main/CHANGELOG.md)
- [Importador no pin já registrado](https://github.com/chaoticgd/ghidra-emotionengine-reloaded/blob/ae013ee1475dc970db4fdeba3ec88def6b933d43/ghidra_scripts/PCSX2SaveStateImporter.java)

O changelog associa v2.1.37 (`ae013ee`) a suporte para Ghidra 12.1.3; v2.1.38
acrescenta 12.1.4. Isso confirma uma opção declarada para nossa versão, não
uma instalação testada. Nenhum ZIP foi baixado ou hash de pacote verificado.
Antes de uso efetivo, fixar o pacote correspondente e seus hashes.

O mesmo changelog registra, desde v2.0.0, substituição das implementações
MMI/VU macro por stubs pcodeop para melhorar descompilação e remoção de
disassembly VU micro. Portanto, suporte de análise não estabelece execução
aritmética/ciclos corretos, nem cobre por si o futuro pipeline VU1.
A importação de um save ajuda a estudar um estado; não constitui captura
da sequência executada até ele ou medição do intervalo entre eventos.

## Avaliação e recomendação — ainda não adotadas como mudança de ferramenta

**High confidence:** EE Reloaded é um complemento pertinente à análise do
Emotion Engine e à auditoria das famílias que o Ghidra genérico não cobre.
Uma adoção qualificada não exige refazer o projeto.

**High confidence:** não é o gargalo operacional atual. STATUS/RETOMADA
registram a espera de 1000 us consumida pelo relógio de 1 ms/serviço, exclusão
da thread principal e observação do primeiro trap/retorno do interpretador
ainda incompleta. Trocar a linguagem estática não fornece essas medições.

Recomendação: acrescentar a extensão em ambiente/projeto de análise separado,
preservando a comparação genérica existente. Fazer uma auditoria pequena e
comparável das regiões já fixadas e das 34 linhas EE, incluindo opcode,
operandos, endereços e controle de fluxo; usar fixtures independentes para
divergências. Não reaplicar automaticamente o filtro `r5900-only` como passe
na nova comparação. Somente depois avaliar benefício e adoção nos scripts.
Esta auditoria é proposta, não executada nem pré-requisito imposto à slice 101.

Nenhuma correção do runtime, política de tempo ou adoção da extensão decorre
somente desta avaliação. Baseline executável e evidências anteriores preservados.
