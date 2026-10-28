---
name: documentacao
description: Escrever README, guia, ADR e glossário que continuam verdadeiros — e evitar doc que mente com o tempo.
---

# Documentação

Use ao criar projeto, mudar comportamento visível, decidir arquitetura ou quando o usuário
pedir explicação "que dê para outra pessoa ler depois".

## Regra de ouro

Documento que repete o código envelhece mal. Documento que explica **decisão**, **contrato** e
**como verificar** envelhece bem.

| tipo | responde | onde |
|---|---|---|
| README | o que é, como instalar, como usar em 30 s | raiz |
| Guia/tutorial | primeira tarefa completa, passo a passo | `docs/` |
| Referência | cada comando/opção/erro | `docs/` ou `--help` |
| ADR | por que escolhemos X e não Y | `docs/adr/NNNN-titulo.md` |
| GLOSSARY | o que cada termo do domínio significa | `GLOSSARY.md` |
| CHANGELOG | o que mudou para quem usa | `CHANGELOG.md` |

## Como escrever cada um

- **README**: uma frase dizendo o que é; instalação copiável; o caminho feliz em bloco de
  comando; tabela de comandos; onde ficam os dados; como testar. Sem adjetivo de marketing.
- **ADR**: contexto → decisão → alternativas descartadas → consequências (inclusive as ruins).
  Uma decisão por arquivo, data e status (`aceito`, `substituído por 0007`). Nunca edite um ADR
  aceito: escreva outro que o substitui.
- **Guia**: comece pelo resultado ("no fim você terá X rodando"), cada passo com o comando e a
  saída esperada; no fim, como saber que deu certo e o que fazer se não deu.
- **Referência**: uma entrada por item, ordenada, com exemplo mínimo e as mensagens de erro
  reais (copiadas da execução, não inventadas).

## Higiene (o que faz a doc não mentir)

1. Todo comando citado tem que ter sido **executado** nesta sessão ou existir no `--help`.
2. Toda saída mostrada é **copiada** da execução — nada de exemplo "aproximado".
3. Ao mudar flag/comportamento, procure o texto antigo: `grep -rn "delta --antiga" .` e atualize
   README, `--help`, guia e site no mesmo commit.
4. Números (versão, tamanho, contagem de testes) vêm de execução, não de memória.
5. Doc em inglês quando o público é internacional; mantenha o mesmo idioma em todo o arquivo.

## Ao terminar

Liste os arquivos tocados e diga qual comando o leitor usa para validar o que você documentou.
