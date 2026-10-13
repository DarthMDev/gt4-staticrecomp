# Slice114 — um caminho ativo e um arquivo recuperável

Uma branch preserva o estado versionado sem obrigar o projeto a compilar dois
motores diferentes em cada mudança. Por isso publicamos a branch primeiro,
conferimos as dependências e só então removemos as ferramentas e os testes
exclusivos do motor original. Arquivos privados não entram nessa proteção Git:
os inputs, saves e outputs continuaram nos caminhos locais, com hashes.

O termo nativo descreve onde o executável e o código traduzido executam.
Não descreve automaticamente cada subsistema. Nosso EE executa código AOT;
IOP e unidades vetoriais ainda interpretam instruções/microprogramas, e os
dispositivos PS2 são implementações em software. Essa distinção permite
planejar melhorias reais sem partir da premissa errada de zero emulação.

Os testes acompanham a implementação que verificam. Arquivar o motor original
arquiva seus testes; a suite atual continua verificando GPU, áudio/input,
verificação de inputs, launcher e persistência. Um número menor de testes
precisa ser explicado por escopo, sem mudar critérios para fazer bugs passarem.

O build desta base reutiliza uma biblioteca privada já traduzida. Compilar o
host/runtime novamente não é gerar o jogo inteiro desde uma ISO. A documentação
precisa dizer isso para o próximo engenheiro saber o que consegue reproduzir
e quais materiais pinados precisa preservar.
