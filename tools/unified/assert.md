# assert (unificada)

**ops:** `equals` · `contains`

**Cobre (2):** `assert_true`, `assert_contains`

```c
zt_invoke(h, "assert", "{\"op\":\"equals\"}", &out, &err);
```

Motor: `zt_verify_op()` em `harness/src/zt_family_verify.c`.
