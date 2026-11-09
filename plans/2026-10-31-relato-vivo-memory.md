# Relato vivo no memory + encurtamento seguro de contexto — Plano

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

## Decisões de desenho (aprovadas pelo usuário)

- **Sem compaction "cega":** o memory é a rede de segurança. O contexto antigo só encurta
  porque o fio da conversa vive no relato.
- **Relato determinístico** (sem chamada ao modelo): nota única `kind:"report"` +
  `scope:"session"` no `memory.json`, atualizada em 3 momentos:
  1. **antes de responder** — registra o pedido do usuário;
  2. **a cada ferramenta concluída** — acumula ferramenta/arquivos/status em 1 linha;
  3. **no fim do turno** — fecha com estado ("feito/pendente").
- **Injeção automática:** o relato da sessão entra no system prompt a cada passo.
- **Detalhe sob demanda:** o modelo usa a tool `memory` op=`search` (palavras-chave) —
  já existe; o prompt passa a ensinar isso. Nada de ler a sessão inteira.
- **Encurtamento:** saídas de `ROLE_TOOL` antigas (além das últimas 12) viram
  marcador curto quando o total de saídas passa de ~60.000 caracteres.
  Preferências (heurísticas atuais) continuam como estão.

## Arquivos tocados

| Arquivo | Responsabilidade |
|---|---|
| `src/tools_ext.c` | relato: `tools_report_*` (novo), reaproveita `memory_load/save` |
| `src/tools.h` | declarações novas |
| `src/agent.c` | injeta relato no system prompt; encurta saídas antigas |
| `src/main.c` | hooks: envio do usuário, `cb_on_tool_end`, fim do turno; comando `/report`; ajuda |
| `tests/run_all.sh` | testes novos + contador de comandos (1/22) |

## Tarefas

### T1 — Relato vivo (gravar)
- [x] Falha: teste que roda `--tool memory` + replay com tool e espera
  `{"kind":"report"` em `memory.json` com o pedido do usuário.
- [x] `tools_ext.c`: `tools_report_user(const char *pedido)`,
  `tools_report_tool(const char *nome, const char *alvo, int is_error)`,
  `tools_report_turn_end(const char *resumo)`. Nota única por sessão
  (`session_id` no campo `tags` p/ busca). Linha de ferramenta: `nome: alvo (ok|erro)`,
  teto de ~1200 caracteres no relato (mais antigo sai).
- [x] `main.c`: chama `tools_report_user` no envio (junto de `tools_auto_memorize`),
  `tools_report_tool` em `cb_on_tool_end`, `tools_report_turn_end` no fim do loop de turnos.
- [x] Suíte seca + commit.

### T2 — Injeção no system prompt
- [x] Falha: teste espera o texto do relato dentro do prompt capturado pelo
  mock (`build/mock-last-request.json`).
- [x] `agent.c` `build_system_prompt`: se existir nota `report` da sessão,
  inclui bloco "Relato desta sessão (fio da conversa): ...". Função
  `tools_session_report(void)` (retorna malloc ou NULL) em `tools_ext.c`.
- [x] System prompt ensina: detalhes antigos → `memory` op=`search` por palavras-chave.
- [x] Suíte seca + commit.

### T3 — Encurtamento de saídas antigas
- [x] Falha: replay com mock devolvendo tool calls com saídas grandes; espera
  marcador `[saida de <tool> omitida` no request capturado, com as 12 últimas intactas.
- [x] `agent.c` na serialização do request: soma tamanho das saídas; acima do teto,
  saídas fora das últimas 12 viram o marcador (uma linha, com nome da tool).
- [x] Suíte seca + commit.

### T4 — `/report` + contagem + docs
- [x] Comando `/report`: mostra o relato atual da sessão (e "sem relato ainda").
- [x] `COMMANDS` + contador do menu no teste `(1/22)` + linha no `/help`.
- [x] Suíte completa verde + commit final.

### T5 — Plano visível para requests complexos (decisão do usuário)
- [x] System prompt (agent.c): instrui — request complexo/multi-passo → criar plano
  com `agent` op=`plan` (checkboxes `- [ ]`); marcar `- [x]` ao concluir cada item;
  ao fechar todos, o plano sai da tela sozinho.
- [x] `agent` op=`plan`: se o markdown recebido tem itens e TODOS estão `[x]`,
  remove `plan.md` (o painel desaparece ao resolver, pedido do usuário).
- [x] `cb_on_tool_end` chama `plan_refresh()`: o painel atualiza ao vivo quando o
  agente marca um item no meio do turno.

### T6 — Acentos nos textos da interface
- [x] Varredura dos textos visíveis ao usuário (comandos, notificações, blocos de
  info/erro) em main.c/i18n: grafia correta ("sessão", "não", "ferramenta"...).
- [x] Entra na release que o usuário pega via `delta update`. Suíte completa + commit.
