<p align="center">
  <img src="assets/hero.svg" alt="Delta Agent" width="100%">
</p>

<p align="center">
  <a href="https://github.com/descz/delta/releases/latest"><img alt="release" src="https://img.shields.io/github/v/release/descz/delta?style=flat-square&color=white&labelColor=black"></a>
  <img alt="C11" src="https://img.shields.io/badge/C-11-white?style=flat-square&labelColor=black">
  <img alt="deps" src="https://img.shields.io/badge/dependencies-0-white?style=flat-square&labelColor=black">
  <img alt="source-available" src="https://img.shields.io/badge/source-available-white?style=flat-square&labelColor=black">
  <img alt="windows" src="https://img.shields.io/badge/windows-10%2F11-white?style=flat-square&labelColor=black">
  <img alt="linux" src="https://img.shields.io/badge/linux-x86__64-white?style=flat-square&labelColor=black">
  <img alt="macos" src="https://img.shields.io/badge/macos-11%2B-white?style=flat-square&labelColor=black">
  <a href="LICENSE"><img alt="license" src="https://img.shields.io/badge/license-noncommercial-white?style=flat-square&labelColor=black"></a>
</p>

> **Licença:** código disponível para estudo e uso pessoal/interno — **proibido vender ou clonar**,
> e toda divulgação deve ser **sem fins lucrativos** e **com créditos** ao projeto. Veja [LICENSE](LICENSE).

**Delta Agent** é um agente de codificação que vive no seu terminal.
Site: **[descz.github.io/delta](https://descz.github.io/delta/)** · downloads: **[Releases](https://github.com/descz/delta/releases/latest)** Você conversa em linguagem
natural e ele executa o trabalho: lê e escreve arquivos, roda comandos, resolve git, consulta a web,
guarda memória e explica o que encontrou — com o modelo que você escolher.

É **um único executável escrito em C11, sem nenhuma dependência**: nada de Node, Python, npm, pip
ou instalador de framework. No Windows são **544 KB** e ~1,7 MB de memória privada.

---

## Índice

- [O que ele faz](#o-que-ele-faz)
- [Instalar](#instalar)
- [Primeiros passos](#primeiros-passos)
- [Comandos e atalhos](#comandos-e-atalhos)
- [Como ele funciona](#como-ele-funciona)
- [Onde ficam os seus dados](#onde-ficam-os-seus-dados)
- [Compilar do código](#compilar-do-código)
- [Testes](#testes)
- [Estrutura do projeto](#estrutura-do-projeto)
- [Licença](#licença)

---

## O que ele faz

| | |
|---|---|
| **Arquivos** | lê, cria, edita com precisão (`edit` por trechos exatos), busca no projeto, mostra diff |
| **Shell** | roda comandos, mantém jobs em background, `/terminal` mostra a saída num bloco no chat |
| **Git** | status, diff, log, commit e *checkpoint* antes de mexer |
| **Web** | baixa páginas, extrai dados, lê documentação — e fala **MCP** (JSON-RPC) |
| **Skills** | fluxos salvos em `SKILL.md`, indexados no prompt de sistema e reaproveitados |
| **Memória** | fatos duráveis entre sessões, buscáveis por texto |
| **Modelos** | catálogo [models.dev](https://models.dev) com centenas de modelos e provedores |
| **Sessões** | histórico por pasta em JSONL; `delta` retoma o último chat daquela pasta |
| **Streaming** | resposta token a token, com corte imediato quando você manda uma mensagem no meio |

Tudo dentro do terminal, com TUI própria em tela alternativa: cópia com o mouse, seleção por
arrasto, markdown renderizado (títulos, listas, código, tabelas), cores OkLCH, temas, i18n
(pt, en, es, zh, ja) e instalador com tela HTML servida **pelo próprio executável**.

## Instalar

Baixe em [**Releases**](https://github.com/descz/delta/releases/latest). Cada sistema tem um arquivo
que já é o instalador:

| sistema | arquivo | como |
|---|---|---|
| **Windows 10/11 x64** | `Delta-windows-x64.zip` (ou `Delta.exe`) | duplo clique → tela de instalação no navegador |
| **Linux x86_64** | `delta-linux-x86_64.run` | `chmod +x delta-linux-x86_64.run && ./delta-linux-x86_64.run` |
| **macOS 11+** | `delta-macos-source.zip` | descompacte e dê duplo clique em `INSTALAR-MAC.command` |

O instalador pergunta **onde hospedar os dados**, cria a pasta, copia o binário, coloca `delta` no
`PATH` e cria o atalho (barra de tarefas no Windows, `.desktop` + favorito no GNOME, `Delta.app` +
Dock no macOS). O Windows pode avisar no SmartScreen na primeira execução (*Mais informações →
Executar assim mesmo*) porque o binário não é assinado.

No macOS não há binário pronto: o pacote leva o código e um `.command` que confere as Xcode Command
Line Tools (gratuitas), compila e abre o instalador — não existe cross-compile confiável para Mach-O.

## Primeiros passos

```text
$ delta
sem chave de API · use /login para colar a chave do provedor

/login                          # escolhe o provedor, pede a chave, sugere um modelo
/projects D:\dev\meu-app        # onde ele trabalha (fica salvo para as próximas vezes)
leia src/main.c e explique o fluxo
```

Na segunda vez, `delta` já abre **no último chat daquela pasta**. `delta --new` começa do zero.

## Comandos e atalhos

| comando | o que faz |
|---|---|
| `/login` `/logout` | conecta/desconecta provedores (chave guardada por provedor) |
| `/models` | modelos dos provedores conectados |
| `/thinking` | esforço de raciocínio (off → max, conforme o modelo) |
| `/projects <pasta>` | define a pasta de trabalho (salva na config) |
| `/terminal <cmd>` | roda no shell e mostra a saída no chat |
| `/copy` | copia a última resposta |
| `/status` `/session` | configuração, uso e sessão |
| `/new` `/clear` `/reload` `/quit` | sessão nova, limpa, recarrega, sai |
| `/help` | tudo isso na tela |

`delta wiki` abre a documentação no navegador; `delta --setup` reabre o instalador.

| tecla | ação |
|---|---|
| `Enter` / `Shift+Enter` | envia / quebra linha |
| `Ctrl+C` | interrompe a geração (nunca fecha o app) |
| `Ctrl+X` | copia a última resposta |
| `Ctrl+Q` | devolve a última mensagem da fila ao editor |
| mouse | arrastar seleciona e copia; roda rola a conversa |
| `Esc` | limpa a seleção e volta ao fim |

**Digitar durante o trabalho funciona**: a mensagem entra no chat marcada *na fila* e o agente
**corta a resposta no meio** para lê-la — sem esperar a mensagem final.

## Como ele funciona

Um turno é: *prompt → modelo (streaming) → tool calls → resultado → próximo passo*, até o modelo
parar. O Delta é dono do loop e das ferramentas; o modelo só pede.

- **Renderização própria** (`ui.c`): grade de células com cores OkLCH (port fiel de OkLch/OkHsl),
  markdown → `Doc` de linhas estilizadas, caixas de ferramenta, editor multi-linha com cursor.
  O transcript é reconstruído **incrementalmente**, bloco a bloco (`tr_y`/`tr_fast_from`), o que
  mantém o frame em ~0,5 ms mesmo com conversas longas.
- **Terminal** (`term.c`): modo cru com `ReadConsoleInput` no Windows e `termios` no POSIX; mouse
  (clique, roda, arrasto), colagem, redimensionamento, tela alternativa e atalhos com modificadores.
- **Rede** (`provider.c`): WinHTTP no Windows, `curl` no POSIX — SSE lido linha a linha, com
  *abort* no meio do stream quando você interfere.
- **Agente** (`agent.c`): histórico tipado (`user`/`assistant`/`tool`), tool calls com id,
  sessões em JSONL v3 por pasta, prompt de sistema montado a cada turno (com as skills indexadas).
- **Ferramentas** (`tools.c`, `tools_ext.c`): 40+ operações agrupadas em famílias (`fs`, `shell`,
  `git`, `web`, `skills`, `mcp`, `memory`, `code`, `assert`, `agent`, `extend`), com timeout,
  limites de saída e conversão de encoding do console.
- **Instalador** (`setup.c`): servidor HTTP local (Winsock/Berkeley) que serve uma página embutida
  (~13 KB, fontes em base64), abre o navegador e executa a instalação — seletor de pasta nativo,
  cópia do binário, `PATH`, atalhos e *pin*.

O visual (temas, tokens de cor, tamanhos) segue o mesmo vocabulário de um editor moderno: 56 tokens
de tema, arquivos `.json` simples em `themes/`.

## Onde ficam os seus dados

Uma pasta só, escolhida na instalação (padrão `C:\Users\<você>\.deltaCore`):

```
.deltaCore/
  config.json        provedor, modelo, idioma, thinking, projectDir
  auth.json          uma chave de API por provedor
  models.v2.json     catálogo models.dev em cache
  memory.json        memória durável
  sessions/          --<slug-da-pasta>--/<data>_<id>.jsonl
  skills/            <nome>/SKILL.md
  mcps/              um .json por servidor MCP
  extensions/        extensões plugáveis
  system/prompt.md   o que ele deve obedecer sempre
  paste/ jobs/ themes/
```

`system/prompt.md` é relido a cada turno: é ali que você escreve as regras do seu time.

## Compilar do código

Só precisa de um compilador C e da libc do sistema.

**Windows** (MinGW-w64 / Git Bash):

```bat
build.bat            :: bin\delta.exe
package.bat          :: dist\Delta.exe + zip
```

**Linux / macOS**:

```bash
./build.sh           # bin/delta
./build.sh --test    # + testes de núcleo
./package.sh         # tarball + .run (Linux) | .app + zip + dmg (macOS)
```

No macOS, as Xcode Command Line Tools (`xcode-select --install`) bastam. Em runtime, o Linux usa
`curl` para falar com o provedor.

## Testes

Duas suítes, sem framework:

```bash
bash tests/run_all.sh     # Windows: 60+ verificações (replay da TUI, mock de provedor, instalador)
bash tests/run_posix.sh   # Linux/macOS: build, núcleo, replay, streaming real, instalador, docs
```

O modo **replay** roda a aplicação de verdade sem terminal (`--replay roteiro.txt 110 36`) e despeja
a tela como texto — é assim que a TUI é testada em CI. O servidor falso (`tests/mock_openai.py`)
cobre streaming, tool calls, erros HTTP, retry e o *steer* no meio do stream.

## Estrutura do projeto

```
src/
  main.c        TUI, comandos, editor, transcript
  agent.c       loop do agente, sessões, prompt de sistema, config/auth
  provider.c    streaming (WinHTTP / curl), tool calls, uso de tokens
  tools.c       fs, shell, git, diff, web
  tools_ext.c   skills, mcp, memory, code, assert, agent, extend
  setup.c       instalador (servidor local + tela HTML embutida)
  ui.c          grade de células, markdown, componentes
  term.c        terminal (Win32 / POSIX)
  theme.c okcolor.c  temas e cores OkLCH
  clipboard.c   colagem (texto, imagem, arquivos)
  json.c util.c i18n.c extensions.c modelsdev.c
installer/      a página do instalador (embutida no exe)
docs/           landing page (GitHub Pages)
assets/         logo, hero, screenshots
tests/          replay + mocks + suítes
```

## Licença

**Source-available, uso não comercial.** O código é público para estudo e uso pessoal/interno, mas a
licença **proíbe venda, aluguel, cobrança de acesso e clone** — e exige crédito ao produto em
qualquer divulgação. Veja o texto completo em [LICENSE](LICENSE).

| permitido | proibido |
|---|---|
| usar, estudar e modificar para uso pessoal, educacional ou interno | vender, alugar, cobrar acesso, incluir em produto/serviço pago |
| compartilhar a cópia **não modificada**, sem fins lucrativos | publicar fork público, espelho, pacote, binário ou **clone/rebranding** |
| citar, escrever, gravar vídeo — **com crédito** ao Delta | remover créditos, nome, símbolo Δ ou este aviso |

Crédito obrigatório em qualquer divulgação: **Delta Agent — https://github.com/descz/delta**.
Permissão comercial, clone ou redistribuição especial: abra uma issue no repositório.
