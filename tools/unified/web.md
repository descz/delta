# web (unificada)

**ops:** `http` · `scrape` · `browser`

**Cobre (3):** `http_request`, `scrape_url`, `browser_navigate`

```c
zt_invoke(h, "web", "{\"op\":\"http\"}", &out, &err);
```

Motor: `zt_web_op()` em `harness/src/zt_family_web.c`.
