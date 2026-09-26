# Arquitetura

## Visão geral

```
┌──────────────────────── OrbisLink ────────────────────────┐
│                                                            │
│  UI (Qt 6 / QML)                    ← Fase 3+, por fazer   │
│   StreamView (chiaki-ng) · DropOverlay · TransferPanel     │
│   FtpBrowser · SettingsView                                │
│                                                            │
│  ─────────────── orbislink_core (C++17) ───────────────    │
│   ConsoleManager   verificação de serviços, perfil          │
│   PkgInspector     cabeçalho PKG + PARAM.SFO + ICON0        │
│   LocalHttpServer  serve os pkg à consola (Range/206)       │
│   IInstallerBackend ← RpiClient (API 12800)                 │
│   FtpClient        libcurl, FTP do GoldHEN (2121)           │
│   InstallQueue     fila sequencial, persistência            │
│   SettingsStore    JSON na pasta de dados                   │
│   common/          json, log rotativo, utilidades           │
│   net/             sockets, HTTP cliente, descoberta de IP  │
└────────────────────────────────────────────────────────────┘
```

## Porque é que o núcleo não depende de Qt

A especificação previa `QTcpServer`/`QNetworkAccessManager`. O núcleo foi
escrito em C++17 puro (mais libcurl) por três razões práticas:

1. **Testabilidade e CI.** Os testes do núcleo e o teste de integração
   correm sem Qt, sem servidor gráfico e sem consola — 9 suites em ~3
   segundos. Qt no CI implicaria centenas de MB de dependências para testar
   lógica que não é de interface.
2. **A fila e o servidor HTTP não podem depender do ciclo de eventos da
   UI.** O requisito "o stream não pode engasgar" (§4) é mais fácil de
   garantir com threads próprias do que com slots no thread da UI.
3. **A camada Qt fica fina.** A UI liga-se a `InstallQueue::setListener()`,
   `ConsoleManager::setListener()` e aos getters — basta reemitir como
   sinais Qt num adaptador (`QObject` que guarda um ponteiro para o núcleo).

O que a especificação pedia continua garantido: nenhuma operação de rede ou
de disco corre no thread da UI, e cada módulo tem uma interface clara com
mocks nos testes (`IInstallerBackend` é a prova: os testes da fila usam um
instalador falso).

## Threads

| Thread | Dono | O que faz |
|---|---|---|
| UI | Qt (Fase 3) | só desenha e reencaminha eventos |
| `LocalHttpServer::acceptThread_` | servidor HTTP | aceita ligações |
| uma por ligação HTTP | servidor HTTP | serve um Range; destacada, contada em `activeWorkers_` |
| `InstallQueue::worker_` | fila | executa **uma** tarefa de cada vez |
| `ConsoleManager::thread_` | consola | verifica os serviços de 10 em 10 s |
| chamadas FTP | thread do chamador | limitadas a 1 (até 2) por `FtpClient::Slot` |

Regras: toda a partilha de estado passa por `std::mutex`; os *listeners* são
invocados **fora** do lock (a UI pode reentrar no núcleo sem deadlock);
`stop()` de cada módulo é idempotente e espera pelas threads.

## Fluxo da instalação direta (Modo A)

```
ficheiro.pkg
   │ PkgInspector.inspect()            valida magic, lê PARAM.SFO/ICON0
   ▼
InstallQueue.enqueue()                 ordena gd → gp → ac por TITLE_ID
   │
   ├─ (opcional) RpiClient.isExists()  "Reinstalar / Saltar"
   │
   ├─ LocalHttpServer.registerFile()   token aleatório → /f/<token>/<nome>.pkg
   │
   ├─ RpiClient.installDirect([url])   POST /api/install  → task_id
   │
   ├─ a consola faz HEAD + GET com Range ao servidor local
   │
   ├─ RpiClient.taskProgress(task_id)  POST de 1 em 1 s → bytes, ETA, erro
   │     · 0 bytes servidos após 20 s → "a consola não alcança o PC"
   │     · instalador em baixo        → fila em pausa, tarefa volta a Pendente
   │
   └─ LocalHttpServer.unregisterFile() o token expira quando a tarefa acaba
```

## Fluxo do envio por FTP (Modo B)

```
ficheiro → FtpClient.upload() → /data/pkg/<nome>.pkg (progresso e cancelamento)
```

A opção "Instalar após upload" **não pode** disparar uma instalação: a API
do instalador só aceita URLs HTTP (ver `docs/validacao.md` §4.4). O
utilizador é informado de que tem de instalar a partir da consola, ou usar a
Instalação direta.

## Segurança

* O servidor HTTP faz bind a **uma interface concreta** (a que alcança a
  consola), nunca a `0.0.0.0` por omissão.
* Nenhuma pasta é exposta: cada ficheiro tem um token aleatório de 16 bytes
  e qualquer outro caminho devolve 404.
* `allowedClient` restringe os pedidos ao IP da consola (403 para os
  restantes); `allowLoopback` existe só para desenvolvimento e testes.
* O token é removido quando a tarefa termina, é cancelada ou falha.
* Caminhos de sistema no FTP (`/system`, `/system_ex`, `/preinst`, …) são
  só de leitura salvo "Modo avançado".
* `redactSensitive()` remove Account ID, chaves de registo, tokens e
  palavras-passe de tudo o que entra no log.

## Estado

O que está feito e o que falta, item a item, está em [`estado.md`](estado.md).

## Remote Play (`src/orbislink/stream/`)

O `chiaki-lib` entra como biblioteca, sem uma linha alterada no submódulo.
O `cmake/ChiakiLib.cmake` explica porque não se usa o CMakeLists.txt de topo
do chiaki-ng: esse exige um libcurl com WebSockets, que só serve para jogar
através da PSN. Conduzem-se directamente os subdiretórios `third-party/`
(nanopb e jerasure) e `lib/`.

| Ficheiro | O que faz |
|---|---|
| `chiaki_log_bridge` | manda o registo do chiaki para o mesmo ficheiro que o resto |
| `discovery` | pergunta à consola (987/UDP) e devolve estado, nome, versão e alvo; varre a rede; acorda |
| `credentials` | guarda e lê as consolas registadas, num ficheiro à parte das definições |
| `registration` | PIN de 8 dígitos + Account ID da PSN → chave de registo e rp_key |
| `session` | liga, recebe vídeo e áudio, envia o estado do comando, traduz as razões de fim de sessão |

Do lado do Qt:

| Ficheiro | O que faz |
|---|---|
| `qt/stream_controller` | o que o QML vê; marshalling das callbacks do chiaki para a thread da UI |
| `qt/video_bridge` | AVFrame (YUV420P) → QVideoFrame, plano a plano; a conversão para RGB fica para a placa gráfica |
| `qt/audio_output` | PCM → QAudioSink, com tecto de meio segundo na fila para a latência não crescer |
| `qt/input_map` | teclado → botões e eixos do comando, com o mapa por omissão do chiaki-ng |

O `qml/orbislink/StreamVideo.qml` é o único ficheiro com `import
QtMultimedia`, e é carregado por um `Loader`: numa máquina sem esse módulo
falha só o vídeo, não a janela toda.

**Threads.** O chiaki tem as suas. As callbacks de estado, vídeo e áudio
chegam de lá; tudo o que toca em propriedades do QML passa por
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
