/* setup.c - instalador do Delta.
 *
 * Sem servidor HTTP e sem navegador: a instalacao acontece numa tela nativa
 * do proprio terminal (setup_wizard) — escolher a pasta de dados no seletor
 * do sistema, copiar o binario, PATH, atalho e/ou atualizar pelo GitHub.
 * `delta --install <pasta>` instala sem interface (imprime JSON).
 */
#include "setup.h"
#include "util.h"
#include "installer_html.h"
#include "json.h"
#include "tools.h"
#include "asset_logo.h"
#include "ui.h"
#include "term.h"
#include "theme.h"
#include "update.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <windows.h>
#  include <shlobj.h>
#  include <shellapi.h>
#else
#  include <unistd.h>
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#endif

#ifndef DELTA_VERSION
#  define DELTA_VERSION "0.1.7"
#endif

/* ================================================================== */
/* Utilitarios                                                         */
/* ================================================================== */
static void trim_end(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
}

/* Caminho do proprio executavel. */
static char *self_exe(void)
{
#ifdef _WIN32
    wchar_t wpath[2048];
    DWORD n = GetModuleFileNameW(NULL, wpath, (DWORD)(sizeof wpath / sizeof wpath[0]));
    if (n == 0 || n >= sizeof wpath) return str_dup("delta.exe");
    int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return str_dup("delta.exe");
    char *out = (char *)malloc((size_t)len);
    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, out, len, NULL, NULL);
    return out;
#else
    char buf[2048];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return str_dup("delta");
    buf[n] = 0;
    return str_dup(buf);
#endif
}

static void run_cmd(const char *cmd)
{
    int code = 1;
    Str out; str_init(&out);
    tools_exec(NULL, cmd, 120, &out, &code);
    str_free(&out);
}

/* Copia um arquivo (binario) preservando os bytes. */
static int copy_file(const char *from, const char *to)
{
    size_t n = 0;
    char *data = file_read(from, &n);
    if (!data) return 0;
    int ok = file_write(to, data, n);
    free(data);
    return ok;
}

/* ================================================================== */
/* Estrutura de dados                                                  */
/* ================================================================== */
typedef struct {
    char core[1024];      /* pasta de dados escolhida */
    char bindir[1024];    /* onde o binario foi instalado */
    char exe[1200];       /* caminho do binario instalado */
    char resolved[512];   /* `delta` -> ... (como o shell resolve) */
    char pinned[32];      /* taskbar | dock-apps | shortcut | none */
    int  path_ok;
} Install;

static int add_step(Str *steps, const char *fmt, int ok, ...)
{
    char buf[1200];
    va_list ap;
    va_start(ap, ok);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (steps->len) str_adds(steps, ",");
    str_adds(steps, "{\"ok\":");
    str_adds(steps, ok ? "true" : "false");
    str_adds(steps, ",\"text\":");
    json_write_string(steps, buf, strlen(buf));
    str_addc(steps, '}');
    return ok;
}

/* ================================================================== */
/* Instalacao                                                          */
/* ================================================================== */
#ifndef _WIN32
static char *home_join(const char *a, const char *b)
{
    char *home = home_get();
    char *p1 = path_join(home, a);
    char *p2 = path_join(p1, b);
    free(home);
    free(p1);
    return p2;
}
#endif

static int install_core_tree(const char *dir, Str *steps)
{
    static const char *subs[] = { "skills", "mcps", "extensions", "system",
                                  "sessions", "paste", "jobs", "themes" };
    dir_make(dir);
    if (!path_is_dir(dir)) {
        add_step(steps, "nao consegui criar <b>%s</b>", 0, dir);
        return 0;
    }
    for (size_t i = 0; i < sizeof subs / sizeof subs[0]; i++) {
        char *d = path_join(dir, subs[i]);
        dir_make(d);
        free(d);
    }
    char *mem = path_join(dir, "memory.json");
    if (!file_exists(mem)) file_write(mem, "[]\n", 3);
    free(mem);

    char *sys = path_join(dir, "system");
    dir_make(sys);
    char *sp = path_join(sys, "prompt.md");
    if (!file_exists(sp)) {
        static const char seed[] =
            "<!-- Instrucoes extras do Delta. Tudo aqui e anexado ao prompt de\n"
            "     sistema em toda conversa. Edite a vontade. -->\n";
        file_write(sp, seed, sizeof seed - 1);
    }
    free(sp);
    free(sys);
    add_step(steps, "dados do Delta em <b>%s</b>", 1, dir);
    return 1;
}

#ifdef _WIN32
/* ------------------------------- Windows ------------------------- */
static void win_add_to_path(const char *dir, Str *steps, Install *in)
{
    /* PowerShell evita o limite de 1024 chars do setx */
    Str cmd; str_init(&cmd);
    str_adds(&cmd, "powershell -NoProfile -ExecutionPolicy Bypass -Command \"");
    str_adds(&cmd, "$d='");
    str_adds(&cmd, dir);
    str_adds(&cmd, "'; $p=[Environment]::GetEnvironmentVariable('Path','User'); ");
    str_adds(&cmd, "if($p -notlike ('*'+$d+'*')){[Environment]::SetEnvironmentVariable('Path',");
    str_adds(&cmd, "(($p.TrimEnd(';'))+';'+$d),'User')}\"");
    run_cmd(cmd.data);
    str_free(&cmd);
    in->path_ok = 1;
    add_step(steps, "<b>delta</b> no PATH do usuario (abra um terminal novo)", 1);
}

static void win_make_shortcut(const char *exe, const char *dir, Str *steps, Install *in)
{
    char *appdata = env_get("APPDATA");
    char *progs = path_join(appdata, "Microsoft/Windows/Start Menu/Programs");
    dir_make(progs);
    char *lnk = path_join(progs, "Delta.lnk");
    char *home = home_get();          /* abre no perfil do usuario, nao na pasta do app */

    Str cmd; str_init(&cmd);
    str_adds(&cmd, "powershell -NoProfile -ExecutionPolicy Bypass -Command \"");
    str_adds(&cmd, "$w=New-Object -ComObject WScript.Shell; $s=$w.CreateShortcut('");
    str_adds(&cmd, lnk);
    str_adds(&cmd, "'); $s.TargetPath='");
    str_adds(&cmd, exe);
    str_adds(&cmd, "'; $s.WorkingDirectory='");
    str_adds(&cmd, home);
    str_adds(&cmd, "'; $s.IconLocation='");
    str_adds(&cmd, exe);
    str_adds(&cmd, "'; $s.Save()\"");
    run_cmd(cmd.data);
    str_free(&cmd);

    if (file_exists(lnk)) {
        add_step(steps, "atalho no menu Iniciar", 1);
        /* fixa na barra de tarefas (verbo do shell, melhor esforco) */
        Str pin; str_init(&pin);
        str_adds(&pin, "powershell -NoProfile -ExecutionPolicy Bypass -Command \"");
        str_adds(&pin, "$p='");
        str_adds(&pin, progs);
        str_adds(&pin, "'; (New-Object -ComObject Shell.Application).NameSpace($p)");
        str_adds(&pin, ".ParseName('Delta.lnk').InvokeVerb('taskbarpin')\"");
        run_cmd(pin.data);
        str_free(&pin);
        snprintf(in->pinned, sizeof in->pinned, "%s", "taskbar");
        add_step(steps, "fixado na barra de tarefas", 1);
    } else {
        add_step(steps, "nao consegui criar o atalho", 0);
        snprintf(in->pinned, sizeof in->pinned, "%s", "none");
    }
    free(lnk);
    free(progs);
    free(appdata);
    free(home);
}
#endif

static int do_install(const char *raw_dir, Install *in, Str *steps)
{
    memset(in, 0, sizeof *in);
    snprintf(in->pinned, sizeof in->pinned, "%s", "none");
    /* DELTA_INSTALL_NOLINK=1: instala so' a arvore + o binario dentro dela,
       sem mexer em PATH, atalho ou dock (usado pelos testes). */
    int nolink = getenv("DELTA_INSTALL_NOLINK") != NULL;

    char buf[1024];
    snprintf(buf, sizeof buf, "%s", raw_dir ? raw_dir : "");
    trim_end(buf);
    /* tira aspas que o usuario possa ter colado */
    size_t l = strlen(buf);
    if (l >= 2 && buf[0] == '"' && buf[l - 1] == '"') {
        memmove(buf, buf + 1, l - 2);
        buf[l - 2] = 0;
    }
    char *expanded = path_expand(buf);
    snprintf(buf, sizeof buf, "%s", expanded);
    free(expanded);
    trim_end(buf);
    while (strlen(buf) > 3 && (buf[strlen(buf) - 1] == '/' || buf[strlen(buf) - 1] == '\\'))
        buf[strlen(buf) - 1] = 0;

    if (!*buf || !install_core_tree(buf, steps))
        return 0;
    snprintf(in->core, sizeof in->core, "%s", buf);

    /* ---- onde fica o binario ---- */
    char *exe = self_exe();
    char *name;
    char *base;
    if (nolink) {
#ifdef _WIN32
        name = str_dup("delta.exe");
#else
        name = str_dup("delta");
#endif
        base = path_join(in->core, "bin");
    } else {
#ifdef _WIN32
        name = str_dup("delta.exe");
        char *local = env_get("LOCALAPPDATA");
        base = path_join(local, "Delta");
        free(local);
#else
        name = str_dup("delta");
        base = home_join(".local/share", "delta");
#endif
    }
    dir_make(base);
    char *dest = path_join(base, name);
    int same = str_eq(exe, dest);
    int okcopy = same ? 1 : copy_file(exe, dest);
#ifndef _WIN32
    chmod(dest, 0755);
#endif
    if (!okcopy) {
        add_step(steps, "nao consegui copiar o executavel para <b>%s</b>", 0, base);
        free(dest); free(base); free(name); free(exe);
        return 0;
    }
    snprintf(in->bindir, sizeof in->bindir, "%s", base);
    snprintf(in->exe, sizeof in->exe, "%s", dest);
    add_step(steps, "binario em <b>%s</b>", 1, dest);

    if (nolink) {
        snprintf(in->resolved, sizeof in->resolved, "%s", dest);
        add_step(steps, "instalacao sem atalhos (modo teste)", 1);
        free(dest); free(base); free(name); free(exe);
        return 1;
    }
#ifdef _WIN32
    win_add_to_path(base, steps, in);
    win_make_shortcut(dest, base, steps, in);
    snprintf(in->resolved, sizeof in->resolved, "%s", dest);
#else
    /* symlink em ~/.local/bin e no /usr/local/bin (se der) */
    char *bin = home_join(".local", "bin");
    dir_make(bin);
    char *link1 = path_join(bin, "delta");
    unlink(link1);
    if (symlink(dest, link1) == 0) {
        add_step(steps, "comando <b>delta</b> em %s", 1, link1);
        snprintf(in->resolved, sizeof in->resolved, "%s", link1);
    } else {
        add_step(steps, "nao consegui criar o link em %s", 0, link1);
    }
    if (symlink(dest, "/usr/local/bin/delta") == 0) {
        add_step(steps, "comando <b>delta</b> em /usr/local/bin", 1);
        snprintf(in->resolved, sizeof in->resolved, "%s", "/usr/local/bin/delta");
    }

    /* PATH do shell */
    {
        char *home = home_get();
        const char *rcs[] = { ".zshrc", ".bashrc", ".profile" };
        int ja = 0;
        char *cur = env_get("PATH");
        if (cur && strstr(cur, bin)) ja = 1;
        free(cur);
        if (!ja) {
            for (size_t i = 0; i < 3; i++) {
                char *rc = path_join(home, rcs[i]);
                if (file_exists(rc)) {
                    size_t n = 0;
                    char *txt = file_read(rc, &n);
                    int tem = txt && strstr(txt, ".local/bin");
                    free(txt);
                    if (!tem) {
                        Str line; str_init(&line);
                        str_adds(&line, "\n# Delta\nexport PATH=\"$HOME/.local/bin:$PATH\"\n");
                        file_append(rc, line.data, line.len);
                        str_free(&line);
                    }
                }
                free(rc);
            }
            add_step(steps, "~/.local/bin no PATH (abra um terminal novo)", 1);
        }
        in->path_ok = 1;
        free(home);
    }

    /* launcher + fixar */
#ifdef __APPLE__
    {
        char *apps = home_join("Applications", "");
        dir_make(apps);
        char *bundle = path_join(apps, "Delta.app");
        char *macos = path_join(bundle, "Contents/MacOS");
        dir_make(macos);
        char *plist = path_join(bundle, "Contents/Info.plist");
        Str pl; str_init(&pl);
        str_adds(&pl,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\"><dict>"
            "<key>CFBundleName</key><string>Delta</string>"
            "<key>CFBundleIdentifier</key><string>dev.delta.agent</string>"
            "<key>CFBundleExecutable</key><string>Delta</string>"
            "<key>CFBundlePackageType</key><string>APPL</string>"
            "</dict></plist>\n");
        file_write(plist, pl.data, pl.len);
        str_free(&pl);
        /* o app e' de console: abrir pelo Finder precisa de um Terminal */
        char *bin = path_join(macos, "delta-bin");
        unlink(bin);
        symlink(dest, bin);
        char *lnk = path_join(macos, "Delta");
        {
            Str sh; str_init(&sh);
            str_adds(&sh, "#!/bin/sh
HERE=$(cd \"$(dirname \"$0\")\" && pwd)
");
            str_adds(&sh, "osascript -e \"tell application \\\"Terminal\\\" to do script "
                          "\\\"'$HERE/delta-bin'\\\"\" -e 'tell application \"Terminal\" to activate'
");
            file_write(lnk, sh.data, sh.len);
            chmod(lnk, 0755);
            str_free(&sh);
        }
        free(bin);
        add_step(steps, "Delta.app em ~/Applications", 1);

        Str dock; str_init(&dock);
        str_adds(&dock, "defaults write com.apple.dock persistent-apps -array-add "
                        "'<dict><key>tile-data</key><dict><key>file-data</key><dict>"
                        "<key>_CFURLString</key><string>");
        str_adds(&dock, bundle);
        str_adds(&dock, "</string><key>_CFURLStringType</key><integer>0</integer>"
                        "</dict></dict></dict>' && killall Dock");
        run_cmd(dock.data);
        str_free(&dock);
        snprintf(in->pinned, sizeof in->pinned, "%s", "dock-apps");
        add_step(steps, "adicionado ao Dock", 1);
        free(lnk); free(plist); free(macos); free(bundle); free(apps);
    }
#elif defined(_WIN32)
    /* ja feito em win_make_shortcut */
#else
    {
        char *apps = home_join(".local/share", "applications");
        dir_make(apps);
        char *desk = path_join(apps, "delta.desktop");
        Str d; str_init(&d);
        str_adds(&d,
            "[Desktop Entry]\nType=Application\nName=Delta Agent\n"
            "Comment=Coding agent de terminal\nTerminal=true\nCategories=Development;\n");
        str_adds(&d, "Exec=");
        str_adds(&d, dest);
        str_adds(&d, "\nIcon=utilities-terminal\n");
        file_write(desk, d.data, d.len);
        str_free(&d);
        chmod(desk, 0755);
        add_step(steps, "atalho em ~/.local/share/applications", 1);
        Str pin; str_init(&pin);
        str_adds(&pin, "gsettings set org.gnome.shell favorite-apps \"$(gsettings get "
                       "org.gnome.shell favorite-apps | sed \"s/]$/, 'delta.desktop']/\")\" 2>/dev/null");
        run_cmd(pin.data);
        str_free(&pin);
        snprintf(in->pinned, sizeof in->pinned, "%s", "dock-apps");
        add_step(steps, "favorito no dock (GNOME)", 1);
        free(desk); free(apps);
    }
#endif
#endif /* !_WIN32 */
    (void)name;
    free(dest);
    free(base);
    free(name);
    free(exe);
    return 1;
}

/* Verifica que o comando resolve e roda. */
static int verify_command(Install *in, Str *steps)
{
#ifdef _WIN32
    Str cmd; str_init(&cmd);
    str_adds(&cmd, "\"");
    str_adds(&cmd, in->exe);
    str_adds(&cmd, "\" --version");
#else
    Str cmd; str_init(&cmd);
    str_adds(&cmd, "\"");
    str_adds(&cmd, in->resolved);
    str_adds(&cmd, "\" --version");
#endif
    Str out; str_init(&out);
    int code = 1;
    tools_exec(NULL, cmd.data, 60, &out, &code);
    str_free(&cmd);
    int ok = (code == 0 && out.len > 0);
    char first[120] = "";
    if (out.data) {
        size_t k = 0;
        while (out.data[k] && out.data[k] != '\n' && k < sizeof first - 1) { first[k] = out.data[k]; k++; }
        first[k] = 0;
    }
    str_free(&out);
    add_step(steps, "%s roda: <b>%s</b>", ok, ok ? "delta --version" : "o binario nao executou", first);
    return ok;
}

/* ================================================================== */
/* Servidor HTTP                                                       */
/* ================================================================== */
static char *pick_folder(void)
{
#ifdef _WIN32
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof bi);
    wchar_t title[] = L"Escolha a pasta de dados do Delta";
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return NULL;
    wchar_t wpath[MAX_PATH];
    char *out = NULL;
    if (SHGetPathFromIDListW(pidl, wpath)) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, NULL, 0, NULL, NULL);
        if (len > 0) {
            out = (char *)malloc((size_t)len);
            WideCharToMultiByte(CP_UTF8, 0, wpath, -1, out, len, NULL, NULL);
        }
    }
    CoTaskMemFree(pidl);
    return out;
#elif defined(__APPLE__)
    FILE *f = popen("osascript -e 'POSIX path of (choose folder with prompt "
                    "\"Escolha a pasta de dados do Delta\")' 2>/dev/null", "r");
    if (!f) return NULL;
    char buf[1024] = "";
    if (!fgets(buf, sizeof buf, f)) { pclose(f); return NULL; }
    pclose(f);
    trim_end(buf);
    if (buf[0] == '/' && buf[strlen(buf) - 1] == '/') buf[strlen(buf) - 1] = 0;
    return *buf ? str_dup(buf) : NULL;
#else
    FILE *f = popen("zenity --file-selection --directory --title='Pasta de dados do Delta' "
                    "2>/dev/null", "r");
    if (!f) f = popen("kdialog --getexistingdirectory \"$HOME\" 2>/dev/null", "r");
    if (!f) return NULL;
    char buf[1024] = "";
    if (!fgets(buf, sizeof buf, f)) { pclose(f); return NULL; }
    pclose(f);
    trim_end(buf);
    return *buf ? str_dup(buf) : NULL;
#endif
}

/* ------------------------------------------------------------------ */
/* Instalador: pagina HTML numa janela (popup) + servidor local        */
/* ------------------------------------------------------------------ */
/* A pagina e' servida em 127.0.0.1/<porta>/i/<token>/ — o token aleatorio
   impede que outro processo local (ou uma aba qualquer) chame /api/install.
   A janela e' aberta em modo "app" (Edge/Chrome), o que da' cara de popup:
   sem abas, sem barra de endereco. Sem navegador Chromium, cai no navegador
   padrao; sem navegador nenhum, cai na tela nativa do terminal. */

static int g_stop = 0;
static int g_srv = -1;
static char g_token[40];
static uint64_t g_ultimo_ping = 0;
static int g_teve_ping = 0;

#ifdef _WIN32
typedef SOCKET sock_t;
typedef int sock_len_t;
#  define SOCK_INVALID INVALID_SOCKET
#  define sock_close closesocket
#  define pid_local() ((uint64_t)GetCurrentProcessId())
#else
typedef int sock_t;
typedef socklen_t sock_len_t;
#  define SOCK_INVALID (-1)
#  define sock_close close
#  define pid_local() ((uint64_t)getpid())
#endif

static void token_gera(void)
{
    static const char hex[] = "0123456789abcdef";
    uint64_t s = now_ms() ^ (pid_local() << 17) ^ 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < 24; i++) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        g_token[i] = hex[(int)(s & 0xF)];
    }
    g_token[24] = 0;
}

/* Abre a URL numa janela sem abas (Edge/Chrome em modo app). 0 = nao achou. */
static int url_open_popup(const char *url)
{
#ifdef _WIN32
    static const char *const exes[] = {
        "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
        "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
        "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
        NULL
    };
    for (int i = 0; exes[i]; i++) {
        if (!file_exists(exes[i])) continue;
        Str args; str_init(&args);
        str_addf(&args, "--app=%s --window-size=1000,760", url);
        HINSTANCE r = ShellExecuteA(NULL, "open", exes[i], args.data, NULL, SW_SHOWNORMAL);
        str_free(&args);
        if ((INT_PTR)r > 32) return 1;
    }
    return 0;
#else
    static const char *const bins[] = {
        "microsoft-edge", "google-chrome", "chromium", "chromium-browser", "brave-browser", NULL
    };
    for (int i = 0; bins[i]; i++) {
        Str cmd; str_init(&cmd);
        str_addf(&cmd, "command -v %s >/dev/null 2>&1", bins[i]);
        int tem = system(cmd.data) == 0;
        str_free(&cmd);
        if (!tem) continue;
        Str run; str_init(&run);
        str_addf(&run, "%s --app='%s' --window-size=1000,760 >/dev/null 2>&1 &", bins[i], url);
        system(run.data);
        str_free(&run);
        return 1;
    }
    return 0;
#endif
}

static void http_send(sock_t fd, const char *status, const char *ctype,
                      const char *body, size_t len)
{
    Str o; str_init(&o);
    str_addf(&o, "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n",
             status, ctype, len);
    str_adds(&o, "Cache-Control: no-store\r\nConnection: close\r\n\r\n");
    str_add(&o, body, len);
#ifdef _WIN32
    send(fd, o.data, (int)o.len, 0);
#else
    size_t enviado = 0;
    while (enviado < o.len) {
        ssize_t n = write(fd, o.data + enviado, o.len - enviado);
        if (n <= 0) break;
        enviado += (size_t)n;
    }
#endif
    str_free(&o);
}

static void json_reply(sock_t fd, Str *json)
{
    http_send(fd, "200 OK", "application/json; charset=utf-8",
              json->data ? json->data : "{}", json->len);
}

static void state_json(Str *o)
{
    str_adds(o, "{\"os\":");
#ifdef _WIN32
    str_adds(o, "\"windows\"");
#else
    str_adds(o, "\"posix\"");
#endif
    str_adds(o, ",\"version\":");
    json_write_string(o, DELTA_VERSION, strlen(DELTA_VERSION));
    str_adds(o, ",\"core\":");
    {
        char *h = delta_home();
        json_write_string(o, h, strlen(h));
        free(h);
    }
    str_adds(o, ",\"exe\":");
    {
        char *e = self_exe();
        json_write_string(o, e ? e : "", e ? strlen(e) : 0);
        free(e);
    }
    str_adds(o, ",\"home\":");
    {
        char *h = home_get();
        json_write_string(o, h, strlen(h));
        free(h);
    }
    str_addc(o, '}');
}

/* Le o corpo de um POST (ate' 8 KB). */
static void le_corpo(int fd, const char *req, const char *body, Str *out)
{
    (void)fd;
    const char *cl = str_istarts(req, "") ? NULL : NULL;
    (void)cl;
    size_t n = body ? strlen(body) : 0;
    /* o corpo veio junto na primeira leitura; se faltou, pede o resto */
    const char *p = NULL;
    for (const char *q = req; *q; q++) {
        if ((q[0] == 'c' || q[0] == 'C') && str_istarts(q, "content-length:")) {
            p = q + 15;
            break;
        }
    }
    long quer = p ? atol(p) : 0;
    if ((long)n < quer) {
        char buf[8192];
        long falta = quer - (long)n;
        if (falta > (long)sizeof buf) falta = (long)sizeof buf;
#ifdef _WIN32
        int got = recv(fd, buf, (int)falta, 0);
#else
        ssize_t got = read(fd, buf, (size_t)falta);
#endif
        if (got > 0) { str_add(out, body, n); str_add(out, buf, (size_t)got); return; }
    }
    if (body) str_add(out, body, n);
}

static void handle_request(sock_t fd, const char *method, const char *path,
                           const char *body)
{
    /* todo caminho valido comeca pelo token: /i/<token>/... */
    char alvo[512];
    snprintf(alvo, sizeof alvo, "/i/%s/", g_token);
    size_t tlen = strlen(alvo);
    if (strncmp(path, alvo, tlen) != 0) {
        http_send(fd, "403 Forbidden", "text/plain; charset=utf-8",
                  "token invalido", 14);
        return;
    }
    const char *resto = path + tlen;          /* "" | api/... | logo.png */

    if (str_eq(resto, "") || str_eq(resto, "index.html")) {
        http_send(fd, "200 OK", "text/html; charset=utf-8",
                  INSTALLER_HTML, sizeof INSTALLER_HTML - 1);
        return;
    }
    if (str_eq(resto, "logo.png")) {
        http_send(fd, "200 OK", "image/png", (const char *)LOGO_PNG, sizeof LOGO_PNG);
        return;
    }
    if (str_eq(resto, "api/ping")) {
        g_ultimo_ping = now_ms();
        g_teve_ping = 1;
        Str j; str_init(&j);
        str_adds(&j, "{\"ok\":true}");
        json_reply(fd, &j);
        str_free(&j);
        return;
    }
    if (str_eq(resto, "api/state")) {
        Str j; str_init(&j);
        state_json(&j);
        json_reply(fd, &j);
        str_free(&j);
        return;
    }
    if (str_eq(resto, "api/quit") && str_eq(method, "POST")) {
        Str j; str_init(&j);
        str_adds(&j, "{\"ok\":true}");
        json_reply(fd, &j);
        str_free(&j);
        g_stop = 1;
        return;
    }
    if (str_eq(resto, "api/pick") && str_eq(method, "POST")) {
        Str j; str_init(&j);
        char *pasta = pick_folder();
        if (pasta) {
            str_adds(&j, "{\"ok\":true,\"dir\":");
            json_write_string(&j, pasta, strlen(pasta));
            str_addc(&j, '}');
            free(pasta);
        } else {
            str_adds(&j, "{\"ok\":false,\"dir\":\"\"}");
        }
        json_reply(fd, &j);
        str_free(&j);
        return;
    }
    if (str_eq(resto, "api/install") && str_eq(method, "POST")) {
        Str corpo; str_init(&corpo);
        le_corpo((int)fd, path, body, &corpo);
        char alvo_dir[1024] = "";
        JValue *v = json_parse_cstr(corpo.data ? corpo.data : "{}");
        if (v) {
            const char *d = json_str(json_get(v, "dir"));
            if (*d) snprintf(alvo_dir, sizeof alvo_dir, "%s", d);
            json_free(v);
        }
        str_free(&corpo);
        if (!alvo_dir[0]) {
            http_send(fd, "200 OK", "application/json; charset=utf-8",
                      "{\"ok\":false,\"error\":\"sem pasta\"}", 30);
            return;
        }
        Str passos; str_init(&passos);
        Install in;
        int ok = do_install(alvo_dir, &in, &passos);
        int roda = ok ? verify_command(&in, &passos) : 0;
        Str j; str_init(&j);
        str_addf(&j, "{\"ok\":%s", (ok && roda) ? "true" : "false");
        str_adds(&j, ",\"coreDir\":");
        json_write_string(&j, in.core, strlen(in.core));
        str_adds(&j, ",\"exePath\":");
        json_write_string(&j, in.exe, strlen(in.exe));
        str_adds(&j, ",\"resolved\":");
        json_write_string(&j, in.resolved, strlen(in.resolved));
        str_adds(&j, ",\"pinned\":");
        json_write_string(&j, in.pinned, strlen(in.pinned));
        str_adds(&j, ",\"steps\":[");
        str_adds(&j, passos.data ? passos.data : "");
        str_adds(&j, "]}");
        json_reply(fd, &j);
        str_free(&j);
        str_free(&passos);
        return;
    }
    http_send(fd, "404 Not Found", "text/plain; charset=utf-8", "404", 3);
}

static int servidor_sobe(int *porta)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
#endif
    sock_t srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == SOCK_INVALID) return 0;
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;                        /* porta livre */
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) != 0) { sock_close(srv); return 0; }
    if (listen(srv, 8) != 0) { sock_close(srv); return 0; }
    sock_len_t alen = sizeof addr;
    getsockname(srv, (struct sockaddr *)&addr, &alen);
    *porta = ntohs(addr.sin_port);
    g_srv = (int)srv;
    return 1;
}

int setup_run(void)
{
    delta_bootstrap();
#ifdef _WIN32
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);   /* SHBrowseForFolder */
#endif

    int porta = 0;
    if (!servidor_sobe(&porta)) {
        fprintf(stderr, "delta: nao consegui abrir a porta do instalador\n");
        return 1;
    }
    token_gera();

    char url[200];
    snprintf(url, sizeof url, "http://127.0.0.1:%d/i/%s/", porta, g_token);
    printf("Delta instalador: %s\n", url);
    fflush(stdout);

    int abriu = 0;
    if (getenv("DELTA_SETUP_NOBROWSER")) {
        /* teste/headless: serve a pagina sem abrir janela nenhuma */
        abriu = 1;
    } else {
        abriu = url_open_popup(url);
        if (!abriu) abriu = url_open(url);    /* navegador padrao, em aba */
    }
    if (!abriu) {
        /* sem navegador: a tela nativa do terminal faz o mesmo servico */
        sock_close((sock_t)g_srv);
        g_srv = -1;
        return setup_wizard();
    }

    g_ultimo_ping = now_ms();
    while (!g_stop) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET((sock_t)g_srv, &rf);
        struct timeval tv = { 0, 250000 };
        int r = select((int)g_srv + 1, &rf, NULL, NULL, &tv);
        /* janela fechada sem avisar (X, crash): sem ping por 15 s -> sai.
           Se a pagina nunca carregou (nenhum ping), nao espera para sempre. */
        if (g_teve_ping && now_ms() - g_ultimo_ping > 15000) break;
        if (!g_teve_ping && now_ms() - g_ultimo_ping > 60000) break;
        if (r <= 0) continue;

        sock_t cli = accept((sock_t)g_srv, NULL, NULL);
        if (cli == SOCK_INVALID) continue;
        char req[8192];
        int got = 0;
#ifdef _WIN32
        got = recv(cli, req, (int)sizeof req - 1, 0);
#else
        got = (int)read(cli, req, sizeof req - 1);
#endif
        if (got <= 0) { sock_close(cli); continue; }
        req[got] = 0;
        char method[8] = "", path[512] = "";
        sscanf(req, "%7s %511s", method, path);
        const char *body = strstr(req, "\r\n\r\n");
        body = body ? body + 4 : "";
        handle_request(cli, method, path, body);
        sock_close(cli);
    }

    sock_close((sock_t)g_srv);
    g_srv = -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Fallback: tela nativa no terminal (quando nao ha' navegador)        */
/* ------------------------------------------------------------------ */
/* Deliberadamente simples: so' screen_text/screen_para, sem componentes de
   layout. Serve para instalar/atualizar sem navegador nenhum. */

static void wizard_passos(const char *json_fragmento, Str *out)
{
    if (!json_fragmento || !*json_fragmento) return;
    Str j; str_init(&j);
    str_addc(&j, '[');
    str_adds(&j, json_fragmento);
    str_addc(&j, ']');
    JValue *v = json_parse_cstr(j.data);
    str_free(&j);
    if (!v) return;
    for (size_t i = 0; i < json_count(v); i++) {
        JValue *it = json_at(v, i);
        int ok = json_bool(json_get(it, "ok"), 0);
        const char *txt = json_str(json_get(it, "text"));
        Str limpo; str_init(&limpo);
        for (const char *c = txt; *c; c++) {
            if (c[0] == '<') {
                const char *fim = strchr(c, '>');
                if (fim) { c = fim; continue; }
            }
            str_addc(&limpo, *c);
        }
        str_addf(out, "   %s %s\n", ok ? "[ok]" : "[X]", limpo.data ? limpo.data : "");
        str_free(&limpo);
    }
    json_free(v);
}

static void wizard_tela(Screen *s, const Theme *t, const char *core, const char *novidade,
                        const Str *detalhe, const char *rodape)
{
    screen_clear(s);
    Style st_t   = { tcolor_hex(theme_tok(t, TK_ACCENT)), COL_DEFAULT, ATTR_BOLD };
    Style st_dim = { tcolor_hex(theme_tok(t, TK_DIM)), COL_DEFAULT, 0 };
    Style st_txt = { tcolor_hex(theme_tok(t, TK_TEXT)), COL_DEFAULT, 0 };
    Style st_war = { tcolor_hex(theme_tok(t, TK_WARNING)), COL_DEFAULT, ATTR_BOLD };
    int w = s->cols > 4 ? s->cols - 4 : 1;
    int y = 1;

    {
        char linha[256];
        snprintf(linha, sizeof linha, "Delta Agent %s  \u00b7  instalador", DELTA_VERSION);
        y += screen_text(s, 2, y, linha, strlen(linha), st_t, w);
    }
    y += 1;
    {
        char linha[1200];
        snprintf(linha, sizeof linha, "pasta de dados: %s", core);
        y += screen_para(s, 2, y, w, linha, strlen(linha), st_dim);
    }
    if (novidade && *novidade) {
        y += screen_para(s, 2, y, w, novidade, strlen(novidade), st_war);
    }
    y += 1;
    y += screen_text(s, 2, y, "1  Instalar nesta maquina", 24, st_txt, w);
    y += screen_text(s, 2, y, "2  Atualizar o Delta", 20, st_txt, w);
    y += screen_text(s, 2, y, "3  Sair", 7, st_txt, w);
    y += 1;

    if (detalhe && detalhe->len) {
        int linhas = 0;
        const char *p = detalhe->data;
        while (*p && linhas < 14 && y < s->rows - 2) {
            const char *eol = strchr(p, '\n');
            size_t n = eol ? (size_t)(eol - p) : strlen(p);
            int usado = screen_para(s, 2, y, w, p, n, st_txt);
            y += usado > 0 ? usado : 1;
            linhas++;
            if (!eol) break;
            p = eol + 1;
        }
    }

    if (rodape && *rodape)
        screen_text(s, 2, s->rows - 1, rodape, strlen(rodape), st_dim, w);
}

static int wizard_frame(Screen *s, const Theme *t, const char *core, const char *novidade,
                        const Str *detalhe, int espera)
{
    int cols = 0, rows = 0;
    term_size(&cols, &rows);
    if (s->cols != cols || s->rows != rows) {
        if (s->cols == 0) screen_init(s, cols, rows);
        else screen_resize(s, cols, rows);
    }
    wizard_tela(s, t, core, novidade, detalhe,
                espera ? "Enter para voltar" : "1/2/3 ou Enter \u00b7 Esc sai");
    term_present(s);
    for (;;) {
        Event ev;
        if (!term_read(&ev, 250)) return -2;      /* timeout: redesenha */
        if (ev.type == EV_ESC || (ev.type == EV_CTRL && (ev.cp == 'c' || ev.cp == 'd'))) return -1;
        if (ev.type == EV_ENTER && espera) return 0;
        if (ev.type == EV_CHAR) {
            if (ev.cp == '1') return 1;
            if (ev.cp == '2') return 2;
            if (ev.cp == '3') return 3;
            if (ev.cp == 'q' || ev.cp == 'Q') return -1;
        }
    }
}

int setup_wizard(void)
{
    delta_bootstrap();
    char *core = delta_home();

    if (!term_is_tty() || !term_init()) {
        printf("Delta Agent %s\n\n", DELTA_VERSION);
        printf("Instalar : delta --install <pasta>\n");
        printf("Atualizar: delta update [--check]\n");
        printf("Dados    : %s\n", core);
        free(core);
        return 1;
    }

    Theme t;
    theme_load_builtin_dark(&t);
    term_alt_enter();
    term_set_title("Delta Agent - instalador");

    char novidade[256] = "";
    {
        UpdateInfo u;
        if (update_cached(&u) && u.available)
            snprintf(novidade, sizeof novidade, "versao nova: %s (voce tem %s)",
                     u.latest, u.current);
    }

    Screen s;
    memset(&s, 0, sizeof s);
    Str detalhe; str_init(&detalhe);
    int saiu = 0;
    while (!saiu) {
        int escolha = 0;
        while (escolha == 0 || escolha == -2)
            escolha = wizard_frame(&s, &t, core, novidade, &detalhe, 0);
        if (escolha < 0 || escolha == 3) break;

        str_clear(&detalhe);
        if (escolha == 1) {
            char *pasta = pick_folder();
            if (!pasta) {
                str_adds(&detalhe, "   nenhuma pasta escolhida; nada foi alterado\n");
            } else {
                Str passos; str_init(&passos);
                Install in;
                int ok = do_install(pasta, &in, &passos);
                int roda = ok ? verify_command(&in, &passos) : 0;
                if (in.core[0]) { free(core); core = str_dup(in.core); }
                str_addf(&detalhe, "   %s\n\n", (ok && roda) ? "instalado" : "instalacao incompleta");
                wizard_passos(passos.data ? passos.data : "", &detalhe);
                str_free(&passos);
                free(pasta);
            }
        } else if (escolha == 2) {
            str_adds(&detalhe, "   consultando os releases no GitHub...\n");
            wizard_frame(&s, &t, core, novidade, &detalhe, 1);
            str_clear(&detalhe);
            UpdateInfo u;
            int ok = update_check(&u, 0);
            if (!ok)
                str_addf(&detalhe, "   nao consegui consultar: %s\n",
                         u.err[0] ? u.err : "sem conexao");
            else if (!u.available)
                str_addf(&detalhe, "   voce ja esta na versao mais nova (%s)\n", u.current);
            else {
                Str msg; str_init(&msg);
                int trocou = update_apply(&u, &msg);
                str_addf(&detalhe, "   %s\n", msg.len ? msg.data : (trocou ? "atualizado" : "falhou"));
                str_free(&msg);
                if (trocou) novidade[0] = 0;
            }
        }
        /* le o resultado antes de voltar ao menu */
        int r = 0;
        while (r == 0 || r == -2)
            r = wizard_frame(&s, &t, core, novidade, &detalhe, 1);
        if (r < 0) break;
    }

    str_free(&detalhe);
    screen_free(&s);
    term_alt_leave();
    term_shutdown();
    theme_free(&t);
    free(core);
    return 0;
}

int setup_install_cli(const char *dir)
{
    delta_bootstrap();
    Str steps; str_init(&steps);
    Install in;
    int ok = do_install(dir, &in, &steps);
    int roda = ok ? verify_command(&in, &steps) : 0;
    printf("{\"ok\":%s", (ok && roda) ? "true" : "false");
    printf(",\"coreDir\":");
    Str q; str_init(&q);
    json_write_string(&q, in.core, strlen(in.core));
    printf("%s", q.data ? q.data : "\"\"");
    printf(",\"exePath\":");
    str_clear(&q);
    json_write_string(&q, in.exe, strlen(in.exe));
    printf("%s", q.data ? q.data : "\"\"");
    printf(",\"resolved\":");
    str_clear(&q);
    json_write_string(&q, in.resolved, strlen(in.resolved));
    printf("%s", q.data ? q.data : "\"\"");
    printf(",\"pinned\":");
    str_clear(&q);
    json_write_string(&q, in.pinned, strlen(in.pinned));
    printf("%s", q.data ? q.data : "\"none\"");
    printf(",\"steps\":[%s]}\n", steps.data ? steps.data : "");
    str_free(&q);
    str_free(&steps);
    return ok ? 0 : 1;
}
