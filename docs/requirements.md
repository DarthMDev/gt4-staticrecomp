# Requisitos vigentes

2026-10-13; substitui o resumo do motor original, preservado na branch de arquivo.

1. GT4 USA v2.00 hash-pinado, Windows x86-64/C++20/Clang, código principal AOT
   e runtime PS2 necessário. Primeiro jogo funcional/estável; depois decompilar
   para legibilidade/modificação. Não afirmar ausência de emulação de subsistemas.
2. Nenhum payload/código traduzido/captura/save/binary distribuído pelo Git.
   Inputs e produtores precisam de hashes; alteração de pin requer qualificação.
3. Vulkan real na GPU; não substituir saída guest por replay/mock. Preservar
   áudio, input, cartão persistente/backups/travas e validação de encerramento.
4.16:9 padrão com opção4:3, câmera/HUD coerentes; respeitar os modos originais
   até qualificação das futuras alterações progressivas/resolução interna.
5. Manter desconhecidos explícitos: performance, input duplicado, continuidade
   acústica, PS2 fidelity/scan modes e estabilidade ampla. Sem auditoria ou fix
   de performance/input enquanto vigorar o pedido do dono de apenas registrar.
6. Futuro144FPS/refresh deve manter velocidade100% e separar simulação/render;
   repetir frames ou aumentar clock guest não demonstra esse suporte.
7. Estado guest explícito, código humano legível, sem fallback silencioso.
   Novas mudanças têm controles automatizados; evidência/docs fazem parte da entrega.
8. O motor original não é um segundo produto ativo; restauração da branch
   preservada precisa de uma decisão técnica documentada.
