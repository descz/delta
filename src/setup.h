/* setup.h - instalador (tela HTML local + instalacao sem interface) */
#ifndef DELTA_SETUP_H
#define DELTA_SETUP_H

/* Sobe o servidor local (instalador ou docs), abre o navegador e atende ate
   fechar. wiki=0 -> instalador; wiki=1 -> documentacao (/wiki). */
int setup_run(int wiki);
/* Instala sem interface (delta --install <pasta>); imprime JSON. */
int setup_install_cli(const char *dir);

#endif /* DELTA_SETUP_H */
