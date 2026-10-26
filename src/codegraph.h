/* codegraph.h - grafo do projeto (símbolos/imports) para não reler tudo */
#ifndef DELTA_CODEGRAPH_H
#define DELTA_CODEGRAPH_H

#include <stddef.h>

typedef struct CodeGraph CodeGraph;

/* Abre o grafo do projeto em `root`. Se não existir (ou rebuild=1), monta. */
CodeGraph *graph_open(const char *root, int rebuild);
void       graph_free(CodeGraph *g);
/* Reindexa só os arquivos que mudaram. Retorna quantos. */
int        graph_refresh(CodeGraph *g);
/* Caminho do arquivo .graph em <core>/graphs/. */
char      *graph_path_for(const char *root);

char      *graph_summary(const CodeGraph *g, size_t max_chars);
char      *graph_find(const CodeGraph *g, const char *name, int max);
char      *graph_file_info(const CodeGraph *g, const char *path, int max);
char      *graph_refs(const CodeGraph *g, const char *name, int max);
size_t     graph_file_count(const CodeGraph *g);
size_t     graph_sym_count(const CodeGraph *g);

#endif /* DELTA_CODEGRAPH_H */
