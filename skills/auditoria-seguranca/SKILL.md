---
name: auditoria-seguranca
description: Auditar código em busca de vulnerabilidades REAIS (OWASP/CWE), revisar mudanças sensíveis (auth, input, segredos), rodar pentest de codebase ou responder dúvida de segurança. Baseado na metodologia source-first da Cloudflare (security-audit-skill).
---

# Auditoria de segurança

Achar vulnerabilidade que **viola uma fronteira de confiança real** — e entregar
evidência na fonte, reprodução segura, prioridade e a **correção mínima efetiva**.
Fluxo defensivo, orientado a código. Candidato sem principal/recurso afetado
concreto **não é achado** — é suspeita.

## Lei de ferro

**Sem fronteira + resultado concreto, não existe achado.** Para cada candidato, nomeie:

- o principal de **menor confiança** e a entrada/ação que ele controla;
- o controle que deveria barrar (e onde na fonte ele deveria viver);
- a **fronteira cruzada** (dado de baixa confiança chegando onde não devia);
- o principal/recurso afetado e o **resultado concreto** (observado ou observável
  pelo dono do sistema).

Falta de boa prática, deploy adivinhado, crash genérico de parser ou auto-impacto
(próprio do mesmo principal) **não sobem para achado de segurança**.

## Modos

- **Orientação** (default): dúvida, revisão focada, triagem, investigação de um
  achado — use só a parte relevante. NÃO rode as 6 fases nem crie arquivos.
- **Auditoria completa**: só quando pedido explícito (auditoria/pentest do codebase,
  revisão full/end-to-end, relatório). Aí sim: reconhecimento → varredura por
  frentes → validação de candidatos → relatório. Se ambíguo, pergunte ANTES de
  criar arquivos.

## Segurança de execução (nos dois modos)

- Inspecionar código é **somente leitura**. Construir/testar/rodar código controlado
  pelo alvo só em sandbox real: sem rede externa, ambiente vazio com allowlist,
  alvo read-only, limites de CPU/memória/tempo.
- **Nada de sondar endpoints de produção, serviços alheios, identidades reais ou
  infra compartilhada.** Principais e segredos de teste (dummy) sempre.
- Se o controle decisivo não está na fonte (proxy, WAF, config de deploy), NÃO
  presuma presença nem ausência: registre como `needs_validation` com o fato que falta
  e um plano seguro de validação.
- Pare no **efeito mínimo** que prova o defeito (retorno errado, registro não
  autorizado num tenant dummy, sanitizer). Nada de persistência, pós-exploração
  ou material de ocultação.

## Onde caçar (checklist de superfícies)

- **Input → interpretação**: injeção (SQL/OS/SSRF/XSS), desserialização, path
  traversal, format strings. Toda fonte externa (corpo, header, query, arquivo,
  variável de ambiente, MCP/tool output) entra como não confiável até a fronteira.
- **Autorização**: todo acesso a recurso checa dono/escopo NO PONTO DE USO (IDOR é
  "checou autenticação, esqueceu autorização por objeto").
- **Auth**: sessão/cookie flags, comparadores de token em tempo constante, fluxo de
  reset, JWT (alg, aud, exp), confusão de issuer.
- **Segredos**: chaves hardcoded, logs que imprimem token/PII, segredo em URL,
  `.env` commitado, redaction bypass.
- **Fronteiras de processo/tool**: command injection em ferramenta que executa
  shell, prompt injection em output de web/MCP que volta como instrução.
- **Supply chain**: dependência sem pin, postinstall suspeito, versão com CVE conhecida.
- **Lógica de negócio**: condição de corrida (saldo/cupom/rate limit), reentrância,
  valor negativo, bypass de etapa.

## Severidade (só para achado CONFIRMADO)

- **critical** — não autenticado ganha execução de código, acesso total a dados ou
  takeover de conta arbitrária.
- **high** — derrota POR COMPLETO um controle explícito com consequência real
  (bypass de auth, leitura/escrita entre tenants, XSS armazenado afetando outros).
- **medium** — violação real com raio limitado, precondições incomuns ou recurso estreito.
- **low** — divulgação de interno não-secreto; efeito que exige esforço contínuo por ganho mínimo.
- **informational** — confirmado, porém pre-requisito dentro de achado maior.

Discriminador high/medium: o resultado **derrotou** o controle ou só o **enfraqueceu**?
Não consegue formular o dano concreto? Baixe a severidade. `needs_validation` NÃO tem
severidade — é hipótese bloqueada, não achado de baixa confiança.

## Correção mínima efetiva

Para cada confirmado: qual **invariante** o código precisa impor, e a menor mudança
na fonte que a impõe **no último ponto de decisão confiável** + teste de regressão
que falha sem a correção. Prefira a correção específica (arquivo:linha) a conselho
genérico de hardening. A auditoria DESCREVE a correção; não aplica no alvo.

## Anti-padrões (não cometa)

1. Desvio de checklist apresentado como vulnerabilidade.
2. Conselho de defense-in-depth sem violação de fronteira alcançável.
3. Teste em ambiente vivo/compartilhado quando evidência local bastaria.
4. Adivinhar comportamento de proxy/provider/deploy que não está na fonte.
5. Tratar auto-impacto (mesmo principal) como travessia de fronteira.
6. Reportar efeito de parser/runtime maior que o efeito observado.
7. Severidade para achado não confirmado.
8. Relatório escrito antes da verificação independente, ou prosa que contradiz o JSON.
