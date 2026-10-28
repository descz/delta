/* setup.h - instalador (tela HTML local + instalacao sem interface) */
#ifndef DELTA_SETUP_H
#define DELTA_SETUP_H

/* Tela nativa do instalador (terminal): escolher pasta de dados e/ou
   atualizar o Delta. Sem servidor HTTP e sem navegador. */
int setup_wizard(void);
/* Instala sem interface (delta --install <pasta>); imprime JSON. */
int setup_install_cli(const char *dir);

#endif /* DELTA_SETUP_H */
