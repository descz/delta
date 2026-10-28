---
name: verificacao
description: Provar que está pronto antes de dizer que está pronto — build, testes e evidência colada, nunca opinião.
---

# Verificação antes de concluir

Use sempre que for dizer "pronto", "corrigido", "funcionando" ou criar commit/release.

## Regra

**Evidência antes de afirmação.** Se você não rodou, não afirmou.

## Checklist

1. **Compilou?** Rode o build de verdade e cole o final da saída (com avisos relevantes).
2. **Passou?** Rode a suíte e cite a contagem: `N ok, 0 falhas`. Nunca "os testes passam".
3. **O caso do usuário foi exercitado?** Reproduza o cenário relatado e mostre antes/depois.
4. **Falhou algo?** Diga qual teste, cole a saída. Não marque como pronto; crie tarefa para o
   bloqueio.
5. **Nada de "deve funcionar"**: se depende de ambiente que você não tem (outro SO, GPU, conta),
   diga exatamente o que não foi verificado.

## Antipadrões (não faça)

- rodar só o teste que você mexeu e chamar de suíte verde;
- reescrever o teste para ele passar;
- pedir para o usuário testar o que você podia testar;
- "os outros testes já passavam antes" (você não sabe, rode);
- esconder aviso de compilador que você introduziu.

## Formato da resposta final

```
o que mudou   → bullets curtos
como provei   → comando + saída resumida (contagem, hash, tempo)
o que falta   → se algo não foi verificado, diga aqui
```
