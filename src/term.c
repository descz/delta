/* term.c - terminal cru + renderizador com diff de linhas.
 * Windows: API de console do Win32. POSIX: termios + sequencias ANSI. */
#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#else
#  include <unistd.h>
#  include <termios.h>
#  include <sys/ioctl.h>
#  include <sys/select.h>
#  include <signal.h>
#  include <fcntl.h>
#endif

/* ------------------------------------------------------------------ */
/* Estado interno                                                      */
/* ------------------------------------------------------------------ */
static int   g_inited = 0;
static int   g_cols = 80, g_rows = 24;
static int   g_dirty_all = 1;
static volatile int g_resized = 0;   /* comum: resize E recuperacao de console */

/* Buffer de saida (linhas ja convertidas para ANSI) */
static char **g_prev_lines = NULL;
static int    g_prev_rows = 0;
static Str    g_out;                  /* UTF-8 da saida do frame */

#ifdef _WIN32
static HANDLE g_hin = INVALID_HANDLE_VALUE;
static HANDLE g_hout = INVALID_HANDLE_VALUE;
static DWORD  g_old_in_mode = 0;
static DWORD  g_old_out_mode = 0;
static WCHAR *g_wbuf = NULL;
static size_t g_wcap = 0;
#else
static struct termios g_old_termios;
static int g_old_flags = 0;
#endif

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static void free_prev_lines(void)
{
    if (!g_prev_lines) return;
    for (int i = 0; i < g_prev_rows; i++) free(g_prev_lines[i]);
    free(g_prev_lines);
    g_prev_lines = NULL;
    g_prev_rows = 0;
}

static void ensure_prev_lines(int rows)
{
    if (g_prev_rows == rows && g_prev_lines) return;
    free_prev_lines();
    g_prev_lines = (char **)calloc((size_t)rows, sizeof(char *));
    g_prev_rows = rows;
    g_dirty_all = 1;
}

#ifdef _WIN32
static void write_utf8(const char *s, size_t n)
{
    if (!s || !n) return;
    /* converte para UTF-16 e escreve com WriteConsoleW (mais confiavel no Windows) */
    int need = MultiByteToWideChar(CP_UTF8, 0, s, (int)n, NULL, 0);
    if (need <= 0) return;
    if ((size_t)need + 1 > g_wcap) {
        g_wcap = (size_t)need * 2 + 64;
        g_wbuf = (WCHAR *)realloc(g_wbuf, g_wcap * sizeof(WCHAR));
    }
    MultiByteToWideChar(CP_UTF8, 0, s, (int)n, g_wbuf, need);
    DWORD written = 0;
    WriteConsoleW(g_hout, g_wbuf, (DWORD)need, &written, NULL);
}
#else
static void write_utf8(const char *s, size_t n)
{
    if (!s || !n) return;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(STDOUT_FILENO, s + off, n - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
}
#endif

/* ------------------------------------------------------------------ */
/* Init / shutdown                                                     */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
int term_init(void)
{
    if (g_inited) return 1;
    g_hin = GetStdHandle(STD_INPUT_HANDLE);
    g_hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_hin == INVALID_HANDLE_VALUE || g_hout == INVALID_HANDLE_VALUE) return 0;

    DWORD im = 0, om = 0;
    if (!GetConsoleMode(g_hin, &im)) return 0;   /* nao e um console */
    GetConsoleMode(g_hout, &om);
    g_old_in_mode = im;
    g_old_out_mode = om;

    im &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
    im |= ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT;
    SetConsoleMode(g_hin, im);
    SetConsoleMode(g_hout, om | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleW(L"Delta Agent");

    term_size(&g_cols, &g_rows);
    g_inited = 1;
    return 1;
}

void term_shutdown(void)
{
    if (!g_inited) return;
    SetConsoleMode(g_hin, g_old_in_mode);
    SetConsoleMode(g_hout, g_old_out_mode);
    g_inited = 0;
    free_prev_lines();
    str_free(&g_out);
    free(g_wbuf);
    g_wbuf = NULL;
}

void term_size(int *cols, int *rows)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(g_hout, &info)) {
        g_cols = info.srWindow.Right - info.srWindow.Left + 1;
        g_rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
    if (g_cols < 1) g_cols = 80;
    if (g_rows < 1) g_rows = 24;
    if (cols) *cols = g_cols;
    if (rows) *rows = g_rows;
}

void term_alt_enter(void)
{
    static const char seq[] = "\x1b[?7l\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H";
    write_utf8(seq, sizeof seq - 1);
    g_dirty_all = 1;
}

void term_alt_leave(void)
{
    static const char seq[] = "\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l";
    write_utf8(seq, sizeof seq - 1);
    g_dirty_all = 1;
}

void term_mouse_enable(int on)
{
    static const char on_seq[] = "\x1b[?1000h\x1b[?1006h";
    static const char off_seq[] = "\x1b[?1000l\x1b[?1006l";
    if (on) write_utf8(on_seq, sizeof on_seq - 1);
    else    write_utf8(off_seq, sizeof off_seq - 1);
}

void term_set_title(const char *t)
{
    if (!t) return;
    Str s; str_init(&s);
    str_adds(&s, "\x1b]0;");
    str_adds(&s, t);
    str_addc(&s, 0x07);
    write_utf8(s.data, s.len);
    str_free(&s);
}

/* Usado pelo modo replay: fixa o tamanho sem um console real. */
void term_set_size(int cols, int rows)
{
    if (cols > 0) g_cols = cols;
    if (rows > 0) g_rows = rows;
}

/* Reabre o console depois de o host cair/PC dormir. 1 = de volta. */
static int term_recover_console(void)
{
    static uint64_t ultima = 0;
    uint64_t agora = now_ms();
    if (agora - ultima < 2000) return 0;   /* no maximo uma tentativa a cada 2 s */
    ultima = agora;

    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hin == INVALID_HANDLE_VALUE || hout == INVALID_HANDLE_VALUE) return 0;
    DWORD im = 0;
    if (!GetConsoleMode(hin, &im)) return 0;   /* nao voltou a ser console */

    /* recha o modo cru (o modo antigo se perdeu com o host) e o estado da UI */
    im &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
    im |= ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT;
    SetConsoleMode(hin, im);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    /* redesenha do zero: o buffer do host novo esta' vazio e as celulas que
       o Delta achava que estavam la' nao existem mais */
    {
        static const char seq[] = "\x1b[2J\x1b[H";
        DWORD esc = 0;
        WriteConsoleA(hout, seq, (DWORD)(sizeof seq - 1), &esc, NULL);
    }
    free_prev_lines();        /* limpa o cache de linhas do term_present */
    g_dirty_all = 1;
    g_resized = 1;            /* forca term_size e um render completo */
    return 1;
}

int term_has_input(void)
{
    DWORD n = 0;
    if (!GetNumberOfConsoleInputEvents(g_hin, &n)) {
        /* console desconectado (PC dormiu, host saiu): tenta voltar */
        if (term_recover_console()) return GetNumberOfConsoleInputEvents(g_hin, &n) && n > 0;
        return 0;
    }
    return n > 0;
}


/* Log de eventos para diagnostico (DELTA_EVENT_LOG=<arquivo>). */
static void event_log(const char *fmt, ...)
{
    const char *path = getenv("DELTA_EVENT_LOG");
    if (!path || !*path) return;
    FILE *f = fopen(path, "ab");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void translate_key(const KEY_EVENT_RECORD *k, Event *ev)
{
    ev->type = EV_NONE;
    ev->cp = 0;
    ev->ctrl = (k->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) ? 1 : 0;
    ev->alt = (k->dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) ? 1 : 0;
    ev->shift = (k->dwControlKeyState & SHIFT_PRESSED) ? 1 : 0;

    WORD vk = k->wVirtualKeyCode;
    /* a tecla Ctrl sozinha: o conhost cola texto por conta propria, entao o
       Ctrl+V nunca chega como tecla. Guardamos o Ctrl para detectar colagem
       de imagem/arquivo (que nao gera evento nenhum). */
    if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU) {
        if (vk == VK_CONTROL) ev->type = EV_CTRL_KEY;
        return;
    }
    switch (vk) {
        case VK_UP:     ev->type = EV_UP; return;
        case VK_DOWN:   ev->type = EV_DOWN; return;
        case VK_LEFT:   ev->type = EV_LEFT; return;
        case VK_RIGHT:  ev->type = EV_RIGHT; return;
        case VK_HOME:   ev->type = EV_HOME; return;
        case VK_END:    ev->type = EV_END; return;
        case VK_DELETE: ev->type = EV_DELETE; return;
        case VK_PRIOR:  ev->type = EV_PGUP; return;
        case VK_NEXT:   ev->type = EV_PGDN; return;
        case VK_PAUSE:  return;
        default: break;
    }
    if (vk == VK_BACK)     { ev->type = EV_BACKSPACE; return; }
    if (vk == VK_TAB)      { ev->type = EV_TAB; return; }
    if (vk == VK_RETURN)   { ev->type = EV_ENTER; return; }
    if (vk == VK_ESCAPE)   { ev->type = EV_ESC; return; }

    if (k->uChar.UnicodeChar) {
        ev->cp = k->uChar.UnicodeChar;
        if (ev->ctrl && ev->cp < 0x20) {
            /* ja vem como codigo de controle */
            ev->type = EV_CTRL;
            ev->cp = ev->cp + 'a' - 1;
            return;
        }
        if (ev->ctrl && ev->cp >= 'A' && ev->cp <= 'Z') {
            ev->type = EV_CTRL;
            ev->cp = ev->cp - 'A' + 'a';
            return;
        }
        ev->type = EV_CHAR;
    }
}

int term_read(Event *ev, int timeout_ms)
{
    ev->type = EV_NONE;
    DWORD start = GetTickCount();

    for (;;) {
        /* timeout 0 = poll: espera 0 ms e devolve o que ja' esta' pronto.
           (antes, com timeout 0, a checagem `used >= timeout` cortava na
           primeira volta e term_read() nunca devolvia evento nenhum, o que
           deixava o teclado morto durante o streaming.) */
        DWORD wait;
        if (timeout_ms < 0) {
            wait = INFINITE;
        } else {
            DWORD used = GetTickCount() - start;
            wait = (used >= (DWORD)timeout_ms) ? 0 : (DWORD)timeout_ms - used;
        }
        DWORD r = WaitForSingleObject(g_hin, wait);
        if (r == WAIT_TIMEOUT) return 0;
        if (r != WAIT_OBJECT_0) {
            /* console se desconectou (PC dormiu, host reiniciou): nao encerra
               o Delta — tenta reabrir o console e segue vivo */
            if (!term_recover_console()) return 0;
            continue;
        }

        INPUT_RECORD rec;
        DWORD read = 0;
        if (!ReadConsoleInputW(g_hin, &rec, 1, &read) || read == 0) {
            if (!term_recover_console()) return 0;
            continue;
        }
        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            term_size(&g_cols, &g_rows);
            ev->type = EV_RESIZE;
            return 1;
        }
        if (rec.EventType == MOUSE_EVENT) {
            static int g_left_down = 0;
            const MOUSE_EVENT_RECORD *me = &rec.Event.MouseEvent;
            ev->mx = me->dwMousePosition.X;
            ev->my = me->dwMousePosition.Y;
            if (me->dwEventFlags & MOUSE_WHEELED) {
                short delta = (short)HIWORD(me->dwButtonState);
                ev->type = EV_WHEEL;
                ev->shift = delta > 0 ? 1 : -1;
                return 1;
            }
            int left = (me->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
            if (left) {
                g_left_down = 1;
                if (me->dwEventFlags & MOUSE_MOVED) {
                    ev->type = EV_DRAG;
                    return 1;
                }
                if (me->dwEventFlags == 0) {   /* clique simples (sem double click) */
                    ev->type = EV_CLICK;
                    return 1;
                }
                continue;
            }
            if (g_left_down) {                 /* soltou: fim da selecao */
                g_left_down = 0;
                ev->type = EV_RELEASE;
                return 1;
            }
            continue;
        }
        if (rec.EventType != KEY_EVENT) continue;
        if (!rec.Event.KeyEvent.bKeyDown) continue;
        translate_key(&rec.Event.KeyEvent, ev);
        event_log("vk=0x%02x char=0x%04x ctrl=%d alt=%d shift=%d -> type=%d cp=0x%04x",
                  rec.Event.KeyEvent.wVirtualKeyCode, rec.Event.KeyEvent.uChar.UnicodeChar,
                  ev->ctrl, ev->alt, ev->shift, (int)ev->type, ev->cp);
        if (ev->type != EV_NONE) return 1;
    }
}

/* Windows Terminal consome o chord Ctrl+V inteiro (nem o Ctrl sozinho chega
   como tecla), entao nem sempre ha evento de console para detectar a colagem.
   Isto aqui e a rede de seguranca: varre o teclado de verdade via
   GetAsyncKeyState e devolve 1 quando o usuario acabou de apertar Ctrl+V. */
int term_poll_paste_hotkey(void)
{
    static int prev_v_down = 0, prev_ctrl_down = 0;
    SHORT cs = GetAsyncKeyState(VK_CONTROL);
    SHORT vs = GetAsyncKeyState('V');
    int v_down = (vs & 0x8000) != 0;
    int ctrl_down = (cs & 0x8000) != 0;
    /* apertou desde a ultima varredura (sem contar soltar: soltar o V/Ctrl
       de outra combinacao gerava colagem fantasma) */
    int v_hit = (vs & 0x0001) != 0 || (v_down && !prev_v_down);
    int ctrl_hit = (cs & 0x0001) != 0 || (ctrl_down && !prev_ctrl_down);
    prev_v_down = v_down;
    prev_ctrl_down = ctrl_down;
    /* so interessa se o Ctrl participou da mesma janela que o V */
    return v_hit && (ctrl_hit || ctrl_down);
}

#else /* ================= POSIX ================= */

static void on_winch(int sig) { (void)sig; g_resized = 1; }

int term_poll_paste_hotkey(void) { return 0; }

int term_init(void)
{
    if (g_inited) return 1;
    if (!isatty(STDIN_FILENO)) return 0;
    if (tcgetattr(STDIN_FILENO, &g_old_termios) != 0) return 0;
    struct termios raw = g_old_termios;
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= (tcflag_t)~(OPOST);
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    g_old_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    signal(SIGWINCH, on_winch);
    term_size(&g_cols, &g_rows);
    g_inited = 1;
    return 1;
}

void term_shutdown(void)
{
    if (!g_inited) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_old_termios);
    g_inited = 0;
    free_prev_lines();
    str_free(&g_out);
}

void term_size(int *cols, int *rows)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col) g_cols = ws.ws_col;
        if (ws.ws_row) g_rows = ws.ws_row;
    }
    if (cols) *cols = g_cols;
    if (rows) *rows = g_rows;
}

void term_alt_enter(void)
{
    static const char seq[] = "\x1b[?7l\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H";
    write_utf8(seq, sizeof seq - 1);
    g_dirty_all = 1;
}
void term_alt_leave(void)
{
    static const char seq[] = "\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l";
    write_utf8(seq, sizeof seq - 1);
    g_dirty_all = 1;
}
void term_mouse_enable(int on)
{
    static const char on_seq[] = "\x1b[?1000h\x1b[?1006h";
    static const char off_seq[] = "\x1b[?1000l\x1b[?1006l";
    if (on) write_utf8(on_seq, sizeof on_seq - 1);
    else    write_utf8(off_seq, sizeof off_seq - 1);
}
void term_set_title(const char *t)
{
    if (!t) return;
    Str s; str_init(&s);
    str_adds(&s, "\x1b]0;");
    str_adds(&s, t);
    str_addc(&s, 0x07);
    write_utf8(s.data, s.len);
    str_free(&s);
}
/* Usado pelo modo replay: fixa o tamanho sem um terminal real. */
void term_set_size(int cols, int rows)
{
    if (cols > 0) g_cols = cols;
    if (rows > 0) g_rows = rows;
}
int term_has_input(void)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = { 0, 0 };
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

int term_read(Event *ev, int timeout_ms)
{
    ev->type = EV_NONE;
    ev->cp = 0; ev->ctrl = ev->alt = ev->shift = 0;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv;
    tv.tv_sec = timeout_ms < 0 ? 0 : timeout_ms / 1000;
    tv.tv_usec = timeout_ms < 0 ? 100000 : (timeout_ms % 1000) * 1000;

    int r = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    if (g_resized) { g_resized = 0; term_size(&g_cols, &g_rows); ev->type = EV_RESIZE; return 1; }
    if (r <= 0) return 0;
    if (!term_has_input()) return 0;

    unsigned char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return 0;
    if (c == 0x1B) {
        /* le a sequencia (com pequeno timeout) */
        char seq[16];
        size_t n = 0;
        struct timeval tiny = { 0, 15000 };
        for (;;) {
            fd_set f2;
            FD_ZERO(&f2);
            FD_SET(STDIN_FILENO, &f2);
            if (select(STDIN_FILENO + 1, &f2, NULL, NULL, &tiny) <= 0) break;
            unsigned char d;
            if (read(STDIN_FILENO, &d, 1) != 1) break;
            if (n < sizeof seq - 1) seq[n++] = (char)d;
            if (n >= 2) break;
        }
        seq[n] = 0;
        if (n == 0) { ev->type = EV_ESC; return 1; }
        if (seq[0] == '[') {
            char k = seq[1];
            switch (k) {
                case 'A': ev->type = EV_UP; return 1;
                case 'B': ev->type = EV_DOWN; return 1;
                case 'C': ev->type = EV_RIGHT; return 1;
                case 'D': ev->type = EV_LEFT; return 1;
                case 'H': ev->type = EV_HOME; return 1;
                case 'F': ev->type = EV_END; return 1;
                case '3': ev->type = EV_DELETE; return 1;
                default: break;
            }
        }
        ev->type = EV_ESC;
        ev->alt = 1;
        return 1;
    }
    if (c == 0x7F || c == 0x08) { ev->type = EV_BACKSPACE; return 1; }
    if (c == '\r' || c == '\n') { ev->type = EV_ENTER; return 1; }
    if (c == '\t') { ev->type = EV_TAB; return 1; }
    if (c < 0x20) { ev->type = EV_CTRL; ev->cp = (uint32_t)(c + 'a' - 1); return 1; }
    /* UTF-8 */
    {
        int extra = 0;
        if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        char buf[4];
        buf[0] = (char)c;
        for (int i = 0; i < extra; i++) {
            unsigned char d;
            if (read(STDIN_FILENO, &d, 1) != 1) break;
            buf[1 + i] = (char)d;
        }
        uint32_t cp = 0;
        utf8_decode(buf, (size_t)(1 + extra), &cp);
        ev->type = EV_CHAR;
        ev->cp = cp;
        return 1;
    }
}

#endif

int term_read_batch(Event *evs, int max, int timeout_ms)
{
    if (max <= 0) return 0;
    if (!term_read(&evs[0], timeout_ms)) return 0;
    int n = 1;
    while (n < max && term_has_input()) {
        Event e;
        if (!term_read(&e, 0)) break;
        if (e.type == EV_NONE) break;
        evs[n++] = e;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Apresentacao com diff de linhas                                     */
/* ------------------------------------------------------------------ */
static uint32_t g_last_fg = 0, g_last_bg = 0;
static uint8_t  g_last_attr = 0;

static void emit_style(Str *out, uint32_t fg, uint32_t bg, uint8_t attr)
{
    int need = (fg != g_last_fg) || (bg != g_last_bg) || (attr != g_last_attr);
    if (!need) return;

    str_adds(out, "\x1b[0");
    if (attr & ATTR_BOLD)      str_adds(out, ";1");
    if (attr & ATTR_DIM)       str_adds(out, ";2");
    if (attr & ATTR_ITALIC)    str_adds(out, ";3");
    if (attr & ATTR_UNDERLINE) str_adds(out, ";4");
    if (attr & ATTR_STRIKE)    str_adds(out, ";9");
    if (attr & ATTR_INVERSE)   str_adds(out, ";7");
    char tmp[64];
    if (fg != COL_DEFAULT) {
        snprintf(tmp, sizeof tmp, ";38;2;%u;%u;%u", (fg >> 16) & 0xFF, (fg >> 8) & 0xFF, fg & 0xFF);
        str_adds(out, tmp);
    }
    if (bg != COL_DEFAULT) {
        snprintf(tmp, sizeof tmp, ";48;2;%u;%u;%u", (bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF);
        str_adds(out, tmp);
    }
    str_addc(out, 'm');
    g_last_fg = fg; g_last_bg = bg; g_last_attr = attr;
}

static void build_row(Screen *s, int y, Str *out)
{
    str_clear(out);
    /* move o cursor para a linha */
    char tmp[32];
    snprintf(tmp, sizeof tmp, "\x1b[%d;1H", y + 1);
    str_adds(out, tmp);

    g_last_fg = 0; g_last_bg = 0; g_last_attr = 0xFF;  /* forca o primeiro estilo */
    str_adds(out, "\x1b[0m");
    g_last_attr = 0;

    int link_atual = 0;                        /* OSC 8 em vigor nesta linha */
    for (int x = 0; x < s->cols; x++) {
        size_t idx = (size_t)y * s->cols + x;
        uint32_t cp = s->cp[idx];
        if (cp == 0) continue;                 /* continuacao de wide char */
        int lk = s->lk ? (int)s->lk[idx] : 0;
        if (lk != link_atual) {                /* abre/fecha o hyperlink */
            str_adds(out, "\x1b]8;;\x1b\\");
            if (lk > 0 && lk <= (int)s->nlinks && s->links[lk - 1]) {
                str_adds(out, "\x1b]8;;");
                str_adds(out, s->links[lk - 1]);
                str_adds(out, "\x1b\\");
            }
            link_atual = lk;
        }
        emit_style(out, s->fg[idx], s->bg[idx], s->at[idx]);
        if (cp == 0xFFFFFFFFu) cp = ' ';
        str_addcp(out, cp);
    }
    if (link_atual) str_adds(out, "\x1b]8;;\x1b\\");
    str_adds(out, "\x1b[0m");
    g_last_attr = 0;
}

void term_present(Screen *s)
{
    if (!g_inited) return;
    ensure_prev_lines(s->rows);
    if (g_out.cap == 0) str_init(&g_out);
    str_clear(&g_out);

    static Str row;
    static int row_init = 0;
    if (!row_init) { str_init(&row); row_init = 1; }

    int changed = 0;
    for (int y = 0; y < s->rows; y++) {
        build_row(s, y, &row);
        if (!g_dirty_all && g_prev_lines[y] && strcmp(g_prev_lines[y], row.data) == 0) continue;
        free(g_prev_lines[y]);
        g_prev_lines[y] = str_dup(row.data);
        str_add(&g_out, row.data, row.len);
        changed = 1;
    }

    if (changed) write_utf8(g_out.data, g_out.len);
    g_dirty_all = 0;
}

int term_is_tty(void)
{
#ifdef _WIN32
    DWORD m;
    return GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &m) ? 1 : 0;
#else
    return isatty(STDOUT_FILENO);
#endif
}
