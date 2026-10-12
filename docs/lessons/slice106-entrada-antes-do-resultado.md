# Slice106 — comparar a entrada antes de corrigir o resultado

A referência dorme esperando o próximo quadro; o modelo chega ao mesmo
trecho quando o contador já avançou. A função devolve 0 no primeiro caso e 1 no
segundo. Observamos também que esse valor chega à próxima iteração da
atualização. No modelo, ele leva ao caminho da espera de 1000 microssegundos.

Isso explica a diferença, mas ainda não mostra onde ela começou. Na entrada,
referência tinha quadro 3/sincronizado 3; modelo tinha 4/3. Mudar a função porque
os retornos divergem esconderia a diferença que já existia nos contadores.
Precisamos voltar ao trecho anterior e alinhar fila, contexto e fase.

Para observar o modelo, acrescentamos somente leituras no callback de
serviços, antes da chamada e da cobrança de tempo. Esse diagnóstico recebeu
nome próprio. Seu estado completo em 10 mil serviços é idêntico ao executável
publicado; a proveniência difere de propósito. Assim a ferramenta também
foi verificada, dentro desse intervalo, sem ser confundida com a produção.

A fatia confirma o resultado e limita sua interpretação. Não escolhe outro
relógio e não declara boot funcional. O próximo experimento procura a
primeira separação anterior à entrada, preservando as correções já comprovadas.
