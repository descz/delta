---
name: automacao
description: Escrever scripts idempotentes, com dry-run, log e código de saída — e agendar no Windows/Linux sem quebrar.
---

# Automação

Use para "faz isso todo dia", renomear em lote, backup, sincronizar, gerar relatório
recorrente ou encadear comandos que alguém vai rodar sem você.

## Contrato mínimo de um script confiável

1. **Idempotente**: rodar duas vezes não duplica nem apaga o que não devia. Antes de criar,
   cheque existência; antes de mover, cheque destino.
2. **`--dry-run` de verdade**: monta a lista de ações e imprime; só executa com `--apply`
   (ou `-y`). Sempre rode o dry-run primeiro e mostre a saída ao usuário.
3. **Log**: uma linha por ação com carimbo de tempo e o resultado. Arquivo em `logs/` ou
   `stderr`, nunca silêncio.
4. **Código de saída**: `0` sucesso, `1` falha esperada (validação), `2` erro de ambiente.
   `set -euo pipefail` no bash; `$ErrorActionPreference = "Stop"` no PowerShell.
5. **Falha alto e cedo**: valide entrada (arquivo existe? pasta existe? comando instalado?)
   antes de começar a mexer em qualquer coisa.
6. **Sem segredo no script**: leia de variável de ambiente ou arquivo fora do repositório.

## Esqueleto (bash)

```bash
#!/usr/bin/env bash
set -euo pipefail
DRY=1; [ "${1:-}" = "--apply" ] && DRY=0
log() { printf '%s %s\n' "$(date -Is)" "$*" >&2; }
[ -d "$ORIGEM" ] || { log "origem inexistente: $ORIGEM"; exit 2; }
while IFS= read -r f; do
  if [ "$DRY" = 1 ]; then log "[dry] mover $f"; else mv -- "$f" "$DESTINO/"; log "movido $f"; fi
done < <(find "$ORIGEM" -maxdepth 1 -name '*.csv' -print)
```

## Windows (PowerShell / .bat)

- Caminho com espaço: sempre `"..."`; use `-LiteralPath` no PowerShell.
- Tarefa agendada: `schtasks /Create /SC DAILY /TN "Delta\\Backup" /TR "\"C:\\...\\backup.cmd\"" /ST 09:00`
  — e para testar: `schtasks /Run /TN "Delta\\Backup"`.
- Linux: `crontab -e` (`0 9 * * * /caminho/backup.sh >> /var/log/backup.log 2>&1`) ou
  `systemd --user` timer se precisar de retry e log rotativo.

## Fluxo de trabalho

1. escreva o script com dry-run;
2. rode o dry-run e **mostre** ao usuário o que mudaria;
3. rode com `--apply` em uma amostra pequena (1 pasta, 1 arquivo);
4. só então o lote inteiro — e diga como desfazer (backup em `pasta.bak-<data>`).

## Ao terminar

Informe: caminho do script, comando de dry-run, comando real, onde ficou o log, e como desfazer.
