# 0050 — Aceitar writer calibrado e âncoras anteriores da INTC2

2026-10-12. Status: accepted, observation only. Predecessora0049.

Fatia109 encontra o incremento extra anterior ao receiver na chargeN1267,
sobre main1/PC5ADB98, e qualifica independentemente o store4AB4AC na referência.
O primeiro writer referencial ocorre na mesma fase render do registro/enable,
com INTC2 previamente latched e masked; entrega de pendência anterior é alta
confiança. Sua origem e proprietário antes da vetorização permanecem desconhecidos.

Aceitamos apenas a rota calibrada: PDB MapDatafastmem_base, watches separados
para backing/alias, registros host de endereço/largura/valor e bloco EE
desambiguado de IOP pelos compiladores JIT. Quatro matches de bloco/store/ELF,
294 estados completos comuns e126 reduzidos repetem exatamente. Raw PC4AB460
não é PC do store; PC AOT107730 do reset também está atrasado.

qualified1 é controle válido de endpoints, mas rejeitado como writer por zero
hits. Standalone hwIntcIrq2 não observado não implica falta de sinalização.
Não aceitamos inferir entrega imediata de EnableIntc, origem da latch, contexto
original, custo completo de serviço ou conversão work→tempo dessas capturas.

No modelo STAT2latched/queueempty antes do registro pode representar lifetime
anterior divergente. Reconstituir queue cegamente a partir do STAT arrisca
duplicar uma ocorrência já entregue. Próxima110 atribui occurrence/acknowledgement/
delivery e gateCPU antes da menor correção e seus testes.

Relógio1ms/service, IRQ correta, modelos, formato CPT e produção preservados.
534 registros/modelo, snapshots completos e work8946341 versus interpreter
passam; CTest54/Python97 herdados de107. Não há claim de boot funcional.

Evidência e hashes: [fatia109](../reverse-engineering/slice109-phase-origin.md),
[manifesto109](../inputs/slice109-phase-origin.json).
