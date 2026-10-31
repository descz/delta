---
name: webdesign
description: Criar ou reformular sites e interfaces com identidade própria (nada de template genérico) — direção visual, tipografia, cor, layout, responsividade e acabamento. Regra dura: TODO site nasce com favicon (SVG ou .ico).
---

# Webdesign

Use ao criar um site novo, uma landing page, um painel ou ao reformular uma interface
que parece "template de IA": genérica, sem voz, igual a todas as outras.

## Regra dura: favicon sempre

**Nenhum site sai sem favicon.** Em todo projeto de site, gere e referencie:

1. **SVG** (preferido): um `favicon.svg` simples — um glifo/marca da paleta do site,
   legível em 16 px. `<link rel="icon" type="image/svg+xml" href="/favicon.svg">`.
2. **.ico** (fallback/legado): se o pedido exigir compatibilidade antiga, gere um
   `.ico` de 16/32 px (ex.: `magick convert` ou um PNG 32×32 renomeado não vale —
   use ferramenta de verdade) e referencie com `<link rel="icon" href="/favicon.ico" sizes="any">`.
3. Coloque também `apple-touch-icon` (180×180 PNG) quando houver build que gere assets.
4. O favicon se deriva da marca do site (inicial, símbolo, cor de fundo da paleta) —
   nunca é esquecido no `<head>`, nunca é o padrão do framework.

Sem favicon, o trabalho não está acabado — trate como esquecer o `<title>`.

## Direção antes de CSS

1. **Uma ideia visual por site** (o que faz este site ser ESTE site e não outro).
   Escolha antes de escrever CSS e siga até o fim.
2. **Tipografia com voz**: uma família de display com personalidade + uma de leitura
   (ou só uma bem usada). Nada de Inter/Roboto por padrão. Tamanho de corpo ≥ 16 px,
   escala tipográfica deliberada, `line-height` ≥ 1.5 em texto corrido.
3. **Cor com intenção**: paleta pequena (1 dominante + 1 acento + neutros), contraste
   AA no texto. Cor de acento se repete em < 5% da tela.
4. **Espaço como recurso**: respiro generoso entre seções; grid consistente (8 px);
   alinhar à grid, não "no olho".
5. **Sem contorno-caixa em tudo**: hierarquia por peso/espaço/cor antes de borda e
   sombra. Sombra sutil, raramente.

## Acabamento (o que separa site de "site de IA")

- Estados: hover/focus/active/disabled em todo interativo; foco visível (acessibilidade).
- Responsivo de verdade: testa em 360 px, 768 px e 1280 px; nada estoura, nada "esmagado".
- Imagens com `alt`; `prefers-reduced-motion` respeitado em animações.
- Números e datas tabulares (`font-variant-numeric: tabular-nums`) em tabelas/valores.
- Vazio bonito: tela sem dados tem estado desenhado, não um branco.

## Antipadrões

- Gradiente roxo/azul padrão + 3 cards iguais + emoji no hero ("site de IA genérico").
- `border-radius` e sombra em tudo; cada seção com estilo diferente.
- Fonte de display em texto corrido; caixa alta em parágrafos.
- Esquecer favicon, `<title>`, meta description ou estados de foco.
