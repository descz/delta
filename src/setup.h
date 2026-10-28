/* setup.h - instalador (tela HTML local + instalacao sem interface) */
#ifndef DELTA_SETUP_H
#define DELTA_SETUP_H

/* Instalador: pagina HTML servida localmente e aberta numa janela (popup)
   em modo app; sem navegador, cai na tela nativa do terminal. */
int setup_run(void);
/* Tela nativa (fallback sem navegador): escolher pasta e/ou atualizar. */
int setup_wizard(void);
/* Instala sem interface (delta --install <pasta>); imprime JSON. */
int setup_install_cli(const char *dir);

#endif /* DELTA_SETUP_H */
