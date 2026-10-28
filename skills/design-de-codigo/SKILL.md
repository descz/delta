---
name: design-de-codigo
description: Projetar módulos fundos (interface pequena, comportamento grande), escolher onde fica o seam e manter o código testável e navegável.
---

# Design de código

Use antes de criar módulo, mover responsabilidade ou "só refatorar rapidinho".

## Vocabulário (use estes termos, não sinônimos)

- **Módulo**: qualquer coisa com interface e implementação — função, arquivo, pacote, camada.
- **Interface**: tudo que o chamador precisa saber: assinatura, invariantes, ordem de chamada,
  erros possíveis, configuração, custo.
- **Profundidade**: quanto comportamento o chamador consegue acionar por unidade de interface.
  Fundo = muita coisa atrás de pouco contrato. Raso = interface do tamanho da implementação.
- **Seam**: o lugar onde a interface vive; onde dá para trocar comportamento sem editar ali.
- **Adapter**: quem satisfaz a interface num seam (Postgres, fake em memória, HTTP).

## Regras de decisão

1. **Teste da deleção**: se apagar o módulo e a complexidade sumir, ele era pass-through — apague.
   Se a complexidade reaparecer em N chamadores, ele está ganhando o pão.
2. **Dois adapters = seam real.** Um adapter só = seam hipotético; não invente abstração.
3. **A interface é a superfície de teste.** Se para testar você precisa furar a interface,
   o módulo está com o formato errado.
4. **Injete dependências, não as crie** dentro da função que você quer testar.
5. **Retorne resultados, não produza efeito colateral** (quem grava é o chamador).
6. **Menos métodos, menos parâmetros.** Se a interface tem 8 funções que só repassam, funda.

## Sinais de que o código está raso (corrija)

- nome de função descreve o passo em vez do resultado (`dados_para_json` vs `catalogo_salvar`);
- arquivo de 3.000 linhas com seis responsabilidades (é o caso clássico de `main.c`/`tools_ext.c`);
- string mágica compartilhada por `if` em cinco arquivos (vira enum/struct num módulo);
- `#ifdef` repetido em toda função (mova para um adapter por plataforma).

## Ao terminar

Rode o build e os testes que cobrem o módulo. Se o design mudou a interface, atualize o
GLOSSARY/ADR (skill `documentacao`) e o README se o comportamento visível mudou.
