---
name: debug-sistematico
description: Achar a causa raiz antes de corrigir — reproduzir, isolar, provar a hipótese, uma correção por vez.
---

# Debug sistemático

Use em qualquer bug, teste vermelho, resultado estranho ou "funcionava ontem".

## Fases (não pule)

1. **Reproduza** — o menor comando que mostra o problema, de forma determinística. Se não
   consegue reproduzir, ainda não entendeu; colete mais dados.
2. **Meça, não presuma** — instrumente antes de teorizar:
   `DELTA_EVENT_LOG=...`, `set -x`, log temporário, sonda isolada, contador.
   A hipótese que você "sabe" que é a causa é a que mais custa tempo.
3. **Isole** — reduza o caso: mesmo código com uma variável a menos, um `#ifdef` de cada vez,
   um arquivo de entrada mínimo. Bisseção em commit também vale (`git bisect`).
4. **Prove a hipótese** — um teste que falha *por causa dela* e passa quando você a corrige.
   Se o "conserto" não muda o resultado do teste, a hipótese estava errada.
5. **Corrija a causa, não o sintoma** — uma mudança por vez; roda a suíte inteira.
6. **Feche o buraco** — deixe um teste que impediria a volta do mesmo bug.

## Vieses que já custaram caro

- **A corrida pelo conserto**: mexer em cinco lugares "para ver se resolve" embaralha a
  evidência. Um por vez.
- **Confiar no comentário/documento** em vez de medir: "(medido: 1 s configurado, zero timeouts)"
  só vale porque foi medido.
- **Culpar a biblioteca** antes de checar a própria chamada.
- **Ignorar o ambiente**: console, codificação (UTF-8 vs OEM), locale, permissão, versão do SO.
- **Achar que o print/log é a ordem real**: stdout bufferizado mente; use `stderr` sem buffer.

## Ao terminar

Responda em três linhas: **causa**, **evidência** (comando/saída) e **teste que impede a
recaída**. Se a causa ainda não foi provada, diga "hipótese" — não "corrigido".
