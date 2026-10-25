# Testes manuais (NÃO rodam na suíte)

Estes scripts **abrem a TUI numa janela** e mandam teclas com `SendKeys`
(`WScript.Shell`) para o console. Eles existem para validar o caminho real do
console do Windows (conhost / Windows Terminal), mas atrapalham quem está
usando a máquina — por isso ficaram fora do `tests/run_all.sh`.

| Arquivo | O que valida |
|---|---|
| `console-while-working.ps1` | mensagem digitada durante o stream chega ao modelo |
| `console-steer.ps1` | stream cortado no meio pelo steer, mensagem antes da resposta final |
| `console-paste.ps1` | conhost colando texto/imagem (Ctrl+V real) |
| `set-dib.ps1` | coloca um CF_DIB no clipboard (usado por `console-paste`/teste de imagem) |

Uso (na raiz do projeto, com a máquina livre):

```powershell
powershell -STA -NoProfile -ExecutionPolicy Bypass -File tests/manual/console-steer.ps1 -Root (Get-Location) -Port 8481
```

O equivalente **headless** (sem janela, roda em CI e nesta suíte) é o
`steer-at` do modo replay — veja `tests/replay-steer.txt` e a seção `[17]` do
`tests/run_all.sh`.
