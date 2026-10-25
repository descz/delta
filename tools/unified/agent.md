# agent (unificada)

**ops:** `plan` · `subagent` · `skill`

**Cobre (3):** `update_plan`, `spawn_subagent`, `load_skill`

```c
zt_invoke(h, "agent", "{\"op\":\"plan\"}", &out, &err);
```

Motor: `zt_agent_op()` em `harness/src/zt_family_agent.c`.
