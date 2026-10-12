# 0051 — Priorizar um piloto de execução com runtime externo

Data: 2026-10-12. Status: aceita para estudo e piloto isolado;
substituição do runtime de produção ainda não decidida.

## Contexto

O proprietário pediu estudar gt4-decomp, ps2-web-recomp e PS2Recomp antes
da próxima fatia, para acelerar muito o caminho até jogo funcional. A
produção segue na fronteira de tempo/interrupções das slices 96–109.
O objetivo é funcionamento estável antes de decompilação manual ampla.

## Decisão

Priorizar o runtime completo MIT de ps2-web-recomp em um experimento com
nosso CORE/disco hash-pinado, com identidade e arquivos privados separados.
Usar gt4-decomp como mapa semântico: seu hash publicado do CORE coincide
com nosso input, e seu inventário ampliado explica os mais de 30 mil alvos.
Usar PS2Recomp como referência de interfaces/tests/Windows, observando
GPLv3 e divergências de contrato já encontradas. EE Reloaded é auxiliar de
análise; não é pré-requisito nem substituto do ambiente de execução.

Não alterar política de tempo/IRQ/checkpoints do runtime atual para imitar
o externo. Não adotar respostas genéricas de sucesso como correção de
serviços desconhecidos. O piloto deve revelar e registrar esses casos.

## Alternativas e consequência

Continuar todas as fatias pequenas na base atual mantém máxima continuidade
dos contratos, mas exige implementar muitos subsistemas que já existem no
outro projeto. Migrar imediatamente perde a referência independente e
presume boot NTSC/Windows ainda não observado. O piloto limitado preserva
a evidência existente e testa o ganho mais amplo com pouco código novo.

Uma eventual integração deve reaproveitar subsistemas coerentes, incluindo
tempo/IOP/transporte, em vez de combinar modelos incompatíveis por handlers.
Vulkan direto, XInput, áudio e saves reais continuam metas do produto; o
caminho wgpu-native é uma base de teste/aproveitamento, não certificação de
que essas metas já estejam cumpridas no Windows.

## Aceitação e revisão

O estudo fecha com clones/pins, explicação de contagens, fontes auditadas e
experimentos reproduzíveis. A escolha da base final exige cold boot,
primeira imagem guest, menu navegável e identificação de serviços realmente
usados. Corrida, saves e fidelidade têm gates separados. Rever esta decisão
quando o primeiro piloto publicar seu bloqueio ou progresso concreto.

Evidência: `docs/reverse-engineering/external-recomp-acceleration-2026-10-12.md`
e `docs/inputs/external-recomp-references.json`.
