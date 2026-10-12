# 0052 — Qualificar entrada de perfil e menu no piloto externo

Data: 2026-10-12. Status: aceita para instrumento e piloto isolado;
menu navegável observado no piloto, adoção de runtime não decidida.

## Contexto

A decisão0051 comprovou primeiras telas guest no Windows, com o runtime MIT
externo completo e nossos inputs. O proprietário autorizou continuidade.
A entrada anterior sem nome não permitia medir progresso até o menu.

## Decisão

Usar o mecanismo de entrada existente do doador, em coldboots controlados
com cartões novos privados. Registrar telas, efeitos e motivos de parada.
Adicionar apenas um launcher reprodutível ao fork, reutilizando fingerprint
e a suíte Python existente; nenhum código do runtime doador é transplantado.

O launcher valida hashes, inclusive ISO inteiro em cada lançamento e cópia
do ELF, recusa sobrescrever evidência e distingue frame-limit de exit0
após parada guest. A aceitação de execução é separada da aceitação visual
de menu e da futura comparação de serviços com referência independente.

## Alternativas e consequência

Modificar instruções guest para pular criação de perfil perde a prova de
entrada/cartão; um replay perde a prova de execução. Mantemos a sequência
real, mesmo que isso exija qualificar as confirmações do cartão novo.
O software GS basta para esse teste; Win32/GPU/Vulkan seguem outra fatia.

O runtime continua com simplificações auditadas em0051. Capturas reais e
menus responsivos não qualificam automaticamente relógio, VU, serviços
com resposta padrão, persistência relida, corrida ou estabilidade ampla.
Produção MSVC e seus contratos/checkpoints permanecem preservados.

## Evidência

`docs/reverse-engineering/slice110-external-menu-pilot.md` e manifest
`docs/inputs/slice110-external-menu-pilot.json` registram controles, pins
e resultados. Menu só será marcado confirmado após telas e input observado.
