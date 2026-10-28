---
name: planilhas-e-pdf
description: Gerar planilhas e PDFs estilizados de verdade — colunas dimensionadas, paleta de 3 cores, uma aba por tópico, números conferidos.
---

# Planilhas e PDF

Use para tabela, relatório, orçamento, extrato, catálogo, currículo, contrato — qualquer
entrega "para abrir no Excel" ou "em PDF".

**Regra:** planilha crua (só `write` de CSV) **não é entrega**. Estilo, organização e
conferência fazem parte do pedido, mesmo quando o usuário não detalha.

## 0. Descubra o terreno (nunca assuma biblioteca)

```bash
python -c "import openpyxl, sys; print('openpyxl', openpyxl.__version__)"
python -c "import reportlab, sys; print('reportlab', reportlab.Version)"
```

- `openpyxl` presente → **XLSX estilizado** (padrão desta skill).
- ausente → entregue CSV **e** HTML estilizado (imprimível), diga o que falta e ofereça instalar
  (`pip install openpyxl`) — **só instale se o usuário autorizar**.
- Nunca invente número, total ou gráfico que você não calculou.

## 1. Tamanho (layout) — o que faz parecer profissional

| item | regra |
|---|---|
| largura das colunas | calculada pelo maior conteúdo: `min 10`, `max 45`; texto longo → `wrap_text=True` e altura 30–45 |
| cabeçalho | primeira linha congelada (`ws.freeze_panes = "A2"`) e autofiltro na tabela |
| altura | linhas de dados uniformes (18–20); sem linha vazia no meio da tabela |
| impressão | paisagem, ajustar à largura (`fitToWidth=1`), cabeçalho repetido (`print_title_rows = "1:1"`), margens 1,5 cm |
| nada de | coluna `####`, célula mesclada no meio dos dados, tabela começando na coluna B |

## 2. Cor — paleta fechada de 3 cores

Escolha **uma** cor de marca e use sempre igual; neutro para o resto; vermelho/verde só com
significado. Contraste mínimo 4,5:1 (texto sobre fundo).

```
tinta/texto      #111827   (nunca cinza claro sobre branco)
cabeçalho        fundo #1F2937 + texto #FFFFFF negrito
zebra            linhas pares #F9FAFB (sutil)
borda            #E5E7EB, 1 px, só entre linhas da tabela
negativo         #B91C1C     positivo  #15803D     atenção  #B45309
total            negrito + borda superior dupla
```

- **cor nunca é o único sinal**: além do vermelho, o número vem com `-` e o rótulo diz o motivo.
- máximo de 3 cores + neutros; sem degradê, sem fonte colorida aleatória, sem emoji na célula.
- cabeçalho de seção: fundo claro da cor de marca (#EEF2FF) com texto na cor escura.

## 3. Tópico (organização) — uma aba por assunto

1. **Resumo** primeiro: período, fonte dos dados, totais, contagens, 3–5 indicadores. É a aba que
   alguém abre primeiro; tem que responder "o que é isso e quanto dá".
2. uma aba por tópico, nome curto e sem acento (`Resumo`, `Vendas`, `Custos`, `Por mes`).
3. dentro da aba: título da seção em negrito, depois a tabela; agrupe por categoria com
   **subtotal** em negrito e uma linha em branco entre grupos.
4. ordem das colunas: `id/data → categoria → descrição → valor → observação`.
5. ordenar por importância (data desc ou valor desc), nunca "ordem em que veio".

## 4. Números (o que mais gera desconfiança)

- moeda: `R$ #,##0.00` · percentual: `0.0%` · data: `dd/mm/aaaa` (ou ISO, escolha uma e mantenha).
- valor é **número**, não texto; alinhado à direita.
- total em negrito com borda superior; média/contagem em itálico e rótulo explícito.
- casas decimais iguais na coluna inteira.

## 5. PDF

- A4, margem 2 cm, corpo 10–11 pt, título 16 pt, entrelinha 1,35.
- cabeçalho de página: título do documento + data; rodapé: `página n/total` + fonte dos dados.
- tabela: cabeçalho repetido em toda página, zebra igual à planilha, mesma paleta.
- **sem dependência**: HTML + CSS `@media print` (o usuário imprime em PDF pelo navegador) e
  diga o passo. Com `reportlab`: platypus `Table` + `SimpleDocTemplate`.
- nunca gere PDF byte a byte "na mão".

## 6. Conferência obrigatória (antes de dizer que está pronto)

```python
# depois de salvar, reabra e prove:
from openpyxl import load_workbook
wb = load_workbook("saida.xlsx")
print(wb.sheetnames)
ws = wb["Resumo"]
print(ws.max_row, ws.max_column)
# soma das linhas == total escrito na célula; nenhuma célula obrigatória vazia
```

Mostre na resposta: caminho absoluto, nº de abas, nº de linhas, **a soma conferida** e o que
ficou de fora (ex.: "sem gráfico", "sem aba por mês"). Se não conferiu, não afirme.

## Exemplo mínimo (openpyxl, estilo completo)

```python
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill, Border, Side, Alignment
from openpyxl.utils import get_column_letter

TINTA, CAB, ZEBRA = "FF111827", "FF1F2937", "FFF9FAFB"
BORDA = Border(bottom=Side(style="thin", color="FFE5E7EB"))

def montar(ws, titulo, linhas):
    ws.freeze_panes = "A2"
    ws.print_title_rows = "1:1"
    ws.page_setup.orientation = "landscape"
    ws.sheet_properties.pageSetUpPr.fitToPage = True
    ws.page_setup.fitToWidth = 1
    ws["A1"] = titulo
    ws["A1"].font = Font(bold=True, size=14, color=TINTA)
    for c, nome in enumerate(linhas[0], start=1):
        cel = ws.cell(row=2, column=c, value=nome)
        cel.font = Font(bold=True, color="FFFFFFFF")
        cel.fill = PatternFill("solid", fgColor=CAB)
        cel.alignment = Alignment(horizontal="center", vertical="center", wrap_text=True)
    for r, linha in enumerate(linhas[1:], start=3):
        for c, val in enumerate(linha, start=1):
            cel = ws.cell(row=r, column=c, value=val)
            cel.border = BORDA
            if r % 2 == 1:
                cel.fill = PatternFill("solid", fgColor=ZEBRA)
    for c in range(1, len(linhas[0]) + 1):
        largura = max(len(str(linhas[i][c-1])) for i in range(len(linhas)))
        ws.column_dimensions[get_column_letter(c)].width = max(10, min(45, largura + 2))
```
