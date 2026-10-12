# Slice105 — uma resposta comprovada exige um executável comprovado

O jogo pede a inicialização do CDVD por RPC e recebe quatro palavras. O
modelo anterior devolvia uma resposta vazia. Observamos duas conclusões do
pedido no jogo de referência, verificamos os módulos da imagem original e
confirmamos a resposta: sucesso, duas versões codificadas como0x0226 e
diagnóstico zero.
Somente o pedido observado, modo0 com4bytes enviados e16recebidos, ganhou
implementação. Outros modos e formatos param com contexto útil.

A resposta segue a transferência SIF, o pacote de conclusão e a interrupção
DMAC existentes. Não acordamos a thread diretamente. A versão semântica RPC
passou a2 para impedir que um checkpoint antigo esconda essa mudança.

Essa recusa revelou outro erro: embora os testes de código passassem, o
executável ainda anunciava RPC1. O Ninja não registrava as dependências de
headers porque o prefixo localizado dos includes tinha bytes diferentes dos
emitidos pelo compilador. Uma alteração no header podia deixar um objeto
antigo dentro do programa novo. Forçar inglês não resolveu nesta instalação.

Agora a ferramenta de build normaliza apenas essas linhas de metadados para
um prefixo ASCII e conserva os diagnósticos. Falhas do compilador e argumentos
não preserváveis param explicitamente. Além dos testes, a aceitação exige
verificar as dependências reais do objeto, agendar recompilação ao tocar o
header e recusar um checkpoint RPC1 pelo próprio executável.

Uma resposta correta remove um contrato desconhecido; não demonstra boot
funcional. A comparação com nosso interpretador verifica consistência porque
ambos compartilham o mesmo modelo. A continuação depende de encontrar a
primeira divergência causal de sincronização com a referência independente.
