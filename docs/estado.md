# Estado da aplicação

O que está feito e o que falta, verificado contra o código — não contra a
memória de ninguém. Quando mudar alguma coisa, muda-se aqui também.

Última revisão: v1.0.0.

## Legenda

- ✅ feito e testado (automaticamente, contra a consola falsa, ou ambos)
- 🟡 feito mas por confirmar numa PS4 real
- ⬜ por fazer
- ➖ decidido não fazer, com a razão à frente

---

## 1. Instalação de .pkg

| | |
|---|---|
| ✅ | Ler o cabeçalho do pkg e o PARAM.SFO (título, content-id, categoria, versão, tamanho) |
| ✅ | Ficheiros acima de 4 GB (tudo em 64 bits) |
| ✅ | Recusar ficheiros que não são pkg de PS4, com a razão |
| ✅ | Servidor HTTP local com pedidos `Range`/206 — a consola descarrega aos bocados |
| ✅ | URLs com token imprevisível e restrição ao IP da consola |
| ✅ | Escolha automática do IP local na sub-rede da consola |
| ✅ | Porta ocupada → escolhe outra |
| ✅ | API do Remote Package Installer (instalar, progresso, já-instalado, pausar, retomar, cancelar) |
| ✅ | Respostas malformadas do instalador (números em hexadecimal sem aspas, `exists` como texto) |
| ✅ | Fila sequencial com ordem jogo → patch → DLC automática |
| ✅ | Deteção de paragem (20 s sem bytes) e pausa quando o serviço cai |
| ✅ | Fila persistida: fechar a aplicação a meio não perde a lista |
| ✅ | Saltar títulos já instalados (opcional) |
| ✅ | Arrastar ficheiros e pastas para a janela (procura .pkg lá dentro) |
| ✅ | Escolher entre instalar e enviar por FTP ao largar |
| 🟡 | Instalação de 10 GB+ numa consola real |
| ✅ | Instalar automaticamente depois do envio por FTP (fica na consola e instala a partir do PC, porque o instalador só sabe descarregar por HTTP) |
| ✅ | Apagar da consola a cópia enviada, depois de instalar |

## 2. FTP

| | |
|---|---|
| ✅ | Ligação ao servidor do GoldHEN (2121, anónimo, passivo) |
| ✅ | Listar, navegar, criar pasta, apagar, mudar o nome |
| ✅ | Enviar ficheiros com progresso e cancelamento |
| ✅ | Trazer ficheiros para o PC (menu, duplo clique, ou pasta à escolha) |
| ✅ | Arrastar da janela para o ambiente de trabalho (traz para uma cache local primeiro) |
| ✅ | Zonas protegidas só de leitura, salvo "modo avançado" |
| ✅ | Limite de ligações simultâneas (1 por omissão, até 2) |
| ✅ | Reconexão automática com recuo exponencial |
| ✅ | Menu por ficheiro e por pasta |
| 🟡 | Retoma de envio interrompido (`REST`/`APPE` implementado; falta confirmar que o servidor do GoldHEN o aceita) |

## 3. Remote Play

| | |
|---|---|
| ✅ | `chiaki-lib` a compilar sem alterar uma linha do submódulo |
| ✅ | Descoberta da consola (987/UDP): estado, nome, versão, jogo a correr |
| ✅ | Varrer a rede à procura de consolas (existe no núcleo, ainda não na interface) |
| ✅ | Acordar consola em repouso |
| ✅ | Registo do PC (PIN de 8 dígitos + Account ID da PSN) |
| ✅ | O Account ID aceita-se em hexadecimal, em decimal ou em base64, e a app converte. As três formas aparecem por baixo do campo, para se confirmar de relance que é o mesmo número, com um botão para inverter a ordem dos bytes |
| ✅ | Guardar e esquecer consolas registadas |
| ✅ | Sessão com vídeo (placa gráfica quando dá, processador quando não dá) |
| ✅ | Som (Opus → PCM → placa de som), com conversão de formato quando a placa não aceita os 48 kHz da consola. Escreve-se directamente na placa, porque o modo em que ela vem buscar as amostras não funciona em todas as máquinas. Confirmado a funcionar numa PS4 real |
| ✅ | Teclado como comando, com o mapa por omissão do chiaki-ng |
| ✅ | Comando físico por SDL (DualShock, DualSense e outros) |
| ✅ | PIN de início de sessão da conta, quando a consola o pede |
| ✅ | Razões de fim de sessão em português, em vez de um código |
| ✅ | Descoberta, registo, vídeo e som confirmados numa PS4 real |
| ✅ | Diário passo a passo de cada tentativa (descoberta → registo → preparar → ligar → primeiro fotograma), com tempos e o motivo exacto de cada falha |
| ✅ | Descodificação por hardware (d3d11va no Windows, vaapi no Linux, videotoolbox no macOS) com recuo automático para software |
| 🟡 | Microfone: captura a 48 kHz estéreo, o chiaki codifica em Opus e envia. Desligado por omissão e visível enquanto capta. Falta ouvir-se do outro lado numa consola real |
| 🟡 | Vibração no comando (por SDL; os gatilhos adaptativos são do DualSense e ficam para depois) |
| ✅ | Touchpad por rato |
| ✅ | Ecrã inteiro (F11, Esc para sair) |
| ✅ | Escolher resolução, fps e bitrate nas definições |
| ⬜ | Remote Play através da PSN (precisa de um libcurl com WebSockets) |

## 4. Interface

| | |
|---|---|
| ✅ | Janela única: stream ao centro, fila e FTP no painel lateral |
| ✅ | Botões, caixas e separadores com o estilo da aplicação em vez do cinzento do Qt |
| ✅ | Indicadores de serviço (Remote Play, FTP, instalador) com a razão quando estão em baixo |
| ✅ | Verificação automática do endereço IP enquanto se escreve |
| ✅ | Sobreposição de arrastar e largar com as duas opções |
| ✅ | Ícones da aplicação, do instalador e dos atalhos |
| ✅ | Avisos dentro da janela |
| ✅ | Nenhuma operação falha em silêncio: um clique que não pode seguir diz porquê, e cada falha de FTP avisa em vez de ir só para a barra de estado |
| ✅ | Três temas (escuro, vidro, claro), trocados nas definições sem reiniciar |
| ✅ | Os diálogos são quase opacos e escurecem o que está por trás: vidro serve para painéis, não para uma caixa que pede uma decisão |
| ✅ | Um botão nunca fica mais estreito do que o seu texto — as larguras pedidas são mínimos, não máximos |
| ✅ | Linguagem "liquid glass": superfícies translúcidas sobrepostas, aresta de luz, sombras difusas, cantos de 28px, barra e painéis flutuantes, movimento com física |
| ✅ | Tema "vidro": painéis translúcidos por cima do fundo |
| 🟡 | Desfoque verdadeiro do que está por trás (precisa do QtQuick.Effects, Qt 6.5+; aqui compila-se com 6.4 e o efeito é feito por camadas) |
| ✅ | A barra de título deixa de ser uma faixa branca por cima do tema: pede-se ao Windows o seu próprio material translúcido (acrílico no tema "vidro", mica nos outros) e, onde ele não exista, a cor do tema ou a barra escura. O diagnóstico diz qual das quatro ficou |
| 🟡 | Janela inteira translúcida com o material do sistema por trás do conteúdo (e não só na barra de título) — implica pintar a janela transparente, o que numa máquina em desenho por software pode sair preto |
| ✅ | Assistente de primeira utilização (3 passos, com verificação da consola ao vivo); volta a abrir pelas definições |
| ✅ | Notificações do sistema no Windows (`Shell_NotifyIconW`); no resto, aviso na janela e a janela pisca |
| ✅ | Janela de registo ao vivo (Ctrl+L), com filtro e detalhe do Remote Play |
| ✅ | Exportar diagnóstico para um ficheiro, com versões, rede, definições (sem segredos), o diário da última tentativa de Remote Play e o fim do registo |
| ✅ | Português e inglês (283 mensagens), escolhido nas definições ou pelo idioma do sistema |
| ⬜ | Atalho para a fila em ecrã inteiro |

## 5. Empacotamento e CI

| | |
|---|---|
| ✅ | Instalador para Windows (NSIS) com componentes, regra de firewall e atalhos |
| ✅ | Zip portátil para Windows |
| ✅ | .tar.gz para Linux |
| ✅ | Compilação cruzada mingw para a linha de comandos |
| ✅ | Verificação de que o pacote leva todas as DLLs (Qt, curl, runtime do MSVC, FFmpeg, SDL2) |
| ✅ | Deteção de falta de aceleração gráfica, com modo compatível automático |
| ✅ | Diagnóstico (`diagnostico.bat`) que recolhe registos e o registo de eventos do Windows |
| ⬜ | AppImage para Linux |
| ➖ | Assinatura do executável: decidido não fazer. Um certificado custa €200–400/ano e isto não se distribui — o aviso do SmartScreen clica-se |
| ✅ | Verificação de actualizações: lê os lançamentos do GitHub, compara por semver, descarrega, confirma o SHA-256 e corre o instalador |
| ✅ | Canal estável ou de testes (pré-lançamentos), e o repositório é uma definição — o projecto pode mudar de casa sem recompilar |
| ✅ | Cada push para o ramo `beta` publica uma compilação de testes `vX.Y.Z-dev.N`, que o canal de testes recebe; ficam as cinco mais recentes |
| ✅ | Definições gravadas por uma versão antiga seguem o repositório da compilação nova, a não ser que o repositório tenha sido escrito à mão |
| ✅ | Repositório público: a verificação funciona sem credenciais |

## 6. Testes

16 conjuntos automáticos, todos a correr no CI:

| | |
|---|---|
| ✅ | JSON tolerante, PARAM.SFO, cabeçalho do pkg |
| ✅ | Servidor HTTP local (incluindo `Range` e restrição por IP) |
| ✅ | Fila de instalação (ordem, paragem, pausa, persistência) |
| ✅ | Listagem de FTP |
| ✅ | Protocolo do instalador remoto contra um servidor falso |
| ✅ | Definições (incluindo a flag do assistente de primeira utilização) |
| ✅ | Actualizações: SHA-256 contra os vectores do NIST, ordem semver das versões, leitura da resposta do GitHub e escolha do canal |
| ✅ | Verificação de serviços |
| ✅ | Descoberta do Remote Play contra uma PS4 falsa |
| ✅ | Credenciais do Remote Play |
| ✅ | Integração ponta a ponta contra a consola falsa (instalar, FTP, trazer de volta, enviar-e-instalar) |
| ✅ | Interface: a janela abre e desenha (smoke test) |
| ✅ | Interface: arrastar e largar acompanha o cursor e aceita o ficheiro |
| ⬜ | Sessão de Remote Play com vídeo sintético — falta um teste que não precise de consola |

---

## O que é preciso para dizer que está pronto

Por ordem de importância:

1. **Uma sessão de Remote Play numa PS4 real.** Tudo o que está marcado 🟡
   nesta lista depende disto. É o passo que nenhuma máquina de CI dá.
2. **Um teste de sessão com vídeo sintético.** Sem ele, cada mudança no
   descodificador só se percebe com uma consola à frente.
3. **AppImage — só se o Linux interessar.** Hoje o pacote de Linux leva
   apenas a linha de comandos: a interface gráfica não é distribuída lá.
   Por decidir se vale a pena.
