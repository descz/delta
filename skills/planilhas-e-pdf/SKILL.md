---
name: planilhas-e-pdf
description: Gerar planilhas (CSV/XLSX) e PDFs conferindo números e layout, com fallback sem dependências quando não há biblioteca.
---

# Planilhas e PDF

Use quando o pedido envolver tabela, relatório, orçamento, extrato, currículo, contrato ou
qualquer entrega "para abrir no Excel" ou "em PDF".

## Primeiro: descubra o terreno (nunca assuma biblioteca)

```bash
# Windows: py -3 --version  |  Linux/macOS: python3 --version
python -c "import openpyxl, sys; print('openpyxl', openpyxl.__version__)"
python -c "import reportlab, sys; print('reportlab', reportlab.Version)"
```

Se faltar, o pedido **não morre**: escolha o formato que dá para entregar com o que existe.
Nunca instale pacote sem o usuário autorizar.

## Planilha

1. **Dados primeiro, planilha depois.** Escreva o CSV (UTF-8, uma linha de cabeçalho, sem
   linhas vazias no meio) — é o que qualquer ferramenta lê e o que dá para conferir com `diff`.
2. **Confira os números antes de formatar**: soma das linhas = total, contagem de linhas,
   mínimo/máximo. Rode a conferência e mostre a conta na resposta:
   ```bash
   python - <<'EOF'
   import csv
   linhas = list(csv.DictReader(open("saida.csv", encoding="utf-8")))
   print(len(linhas), "linhas", sum(float(l["valor"]) for l in linhas))
   EOF
   ```
3. **XLSX só se pedirem** e só com `openpyxl`: cabeçalho em negrito, larguras, número como
   número (não texto), uma aba por assunto. Nada de fórmula que dependa de macro.
4. **CSV no Excel em português**: separador `;` e decimal com vírgula costuma ser o que abre
   certo; se o arquivo for para sistema, use `,` e ponto. Diga qual escolheu.

## PDF

1. **Caminho sem dependência (preferido):** HTML + CSS de impressão, aberto no navegador do
   usuário (`start` / `xdg-open` / `open`) — ele imprime em PDF com layout de verdade. Entregue
   o HTML e diga o passo.
2. **Com `reportlab`:** A4, margem 2 cm, fonte 10–11 pt, título 16 pt; tabela com cabeçalho
   repetido a cada página; rodapé com página `n/total`.
3. **Nunca** gere PDF "na mão" byte a byte. Se não houver reportlab nem navegador, entregue
   `.html` + `.csv` e explique.

## Ao terminar (obrigatório)

- informe **caminho absoluto** de cada arquivo gerado e o tamanho em bytes;
- mostre a conferência numérica (linhas, soma, total) — não afirme "está correto" sem ela;
- se gerou HTML para PDF, diga que a paginação só se confirma na impressão.
