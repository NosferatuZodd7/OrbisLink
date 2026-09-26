# Códigos de erro da consola

O instalador remoto devolve falhas no formato
`{ "status": "fail", "error_code": 0x8XXXXXXX }` (hexadecimal **sem aspas**,
ver `docs/validacao.md`). O `RpiClient` lê esse código e passa-o por
`describeConsoleError()` (`src/orbislink/installer/error_codes.cpp`).

**Regra:** só se traduz um código cujo valor esteja confirmado em fonte
oficial. Códigos desconhecidos aparecem em hexadecimal, sem inventar
significado.

## Confirmados — libkernel (`0x8002xxxx`)

Fonte: `OpenOrbis-PS4-Toolchain/include/orbis/_types/errors.h`
(`ORBIS_KERNEL_ERROR_*`, que são os `SCE_KERNEL_ERROR_*` usados no código do
instalador).

| Código | Nome | Mensagem apresentada |
|---|---|---|
| `0x80020001` | EPERM | Operação não permitida na consola. |
| `0x80020002` | ENOENT | A consola não encontrou o ficheiro ou o caminho. |
| `0x80020005` | EIO | Erro de entrada/saída na consola. |
| `0x8002000C` | ENOMEM | A consola ficou sem memória. |
| `0x8002000D` | EACCES | Acesso negado na consola. |
| `0x80020010` | EBUSY | O recurso está ocupado na consola. |
| `0x80020011` | EEXIST | Já existe. |
| `0x80020016` | EINVAL | Pedido inválido para a consola. |
| `0x8002001B` | EFBIG | Ficheiro demasiado grande para a consola. |
| **`0x8002001C`** | **ENOSPC** | **Espaço insuficiente na consola.** |
| `0x8002001E` | EROFS | Sistema de ficheiros só de leitura. |
| `0x80020023` | EAGAIN | A consola pediu para tentar de novo. |
| `0x80020033` | ENETUNREACH | A consola não conseguiu alcançar a rede. |
| `0x80020035` | ECONNABORTED | Ligação abortada. |
| `0x80020036` | ECONNRESET | A ligação foi reposta pela outra ponta. |
| `0x8002003C` | ETIMEDOUT | A consola excedeu o tempo de espera. |
| `0x8002003D` | ECONNREFUSED | A consola não conseguiu ligar-se ao PC (verifica a firewall). |
| `0x80020041` | EHOSTUNREACH | A consola não alcança o PC. |
| `0x80020055` | ECANCELED | A operação foi cancelada. |

`isOutOfSpaceError()` devolve `true` para `0x8002001C` — é este o código que
a UI usa para a mensagem de espaço insuficiente exigida em §7.

## TODO — famílias por confirmar

Os códigos das bibliotecas do instalador propriamente dito não estão
publicados em nenhuma fonte citável:

* **BGFT** (`sceBgftService*`, gestão de tarefas de download) — família
  `0x8099xxxx` segundo relatos da comunidade, **não confirmado**.
* **AppInstUtil** (`sceAppInstUtil*`, `is_exists`, desinstalação) — família
  `0x8024xxxx`, **não confirmado**.
* **libhttp** (`SCE_HTTP_ERROR_*`, usado ao descarregar do PC) — nomes
  visíveis em `http.c` do instalador, valores não publicados.

Enquanto não houver fonte, estes códigos aparecem como
`A consola devolveu o erro 0x8XXXXXXX.` e ficam registados no log.

**Como contribuir:** ao apanhar um código novo em hardware real, registar
aqui o código, o contexto exato (endpoint, ação) e a fonte da confirmação
antes de o acrescentar à tabela em `error_codes.cpp`.

## Erros do lado do OrbisLink

Estes não vêm da consola; são produzidos localmente e já estão em português:

| Situação | Mensagem |
|---|---|
| Porta 2121 fechada | FTP indisponível. Confirma que o GoldHEN está carregado e o servidor FTP ativo. |
| Porta 12800 fechada | Instalador remoto indisponível. Abre o Remote Package Installer na consola. |
| Tarefa criada mas 0 bytes servidos após 20 s | A consola não conseguiu descarregar do PC. Verifica a firewall do Windows e se estão na mesma rede. |
| Magic do pkg inválido | Não é um pkg PS4 válido. |
| Já instalado | Já existe na consola. Reinstalar ou saltar? |
| Escrita numa zona protegida | Zona protegida do sistema: ativa o Modo avançado nas definições. |
