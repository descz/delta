/* setup.c - instalador do Delta com tela HTML (sem dependencias)
 *
 * `delta --setup` sobe um servidor local em 127.0.0.1:<porta livre>, abre o
 * navegador na pagina embutida (src/installer_html.h), e expoe:
 *
 *   GET  /                 pagina do instalador
 *   GET  /logo.png         a logo (deltalogo.png ao lado do exe)
 *   GET  /api/state        {os, version, defaultDir, exe, logo}
 *   POST /api/pick         abre o seletor de pasta do sistema -> {dir}
 *   POST /api/install      {dir} -> cria a arvore, instala o binario, poe
 *                          `delta` no PATH, cria atalho e tenta fixar
 *   POST /api/quit         encerra o servidor
 *
 * `delta --install <dir>` faz o mesmo sem interface (usado nos testes).
 */
#include "setup.h"
#include "util.h"
#include "json.h"
#include "tools.h"
#include "installer_html.h"
#include "asset_logo.h"

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
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#endif

#ifndef DELTA_VERSION
#  define DELTA_VERSION "0.1.1"
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

static char *exe_dir(void)
{
    char *exe = self_exe();
    char *dir = path_dirname(exe);
    free(exe);
    return dir;
}

/* Procura a logo: ao lado do exe, no cwd e um nivel acima. */
static char *find_logo(void)
{
    char *dir = exe_dir();
    char *p = path_join(dir, "deltalogo.png");
    free(dir);
    if (file_exists(p)) return p;
    free(p);
    if (file_exists("deltalogo.png")) return str_dup("deltalogo.png");
    if (file_exists("../deltalogo.png")) return str_dup("../deltalogo.png");
    return NULL;   /* sem arquivo: usa a logo embutida */
}

/* Roda um comando escondido e descarta a saida. */
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
static int g_stop = 0;
static int g_srv = -1;          /* socket de escuta (fechado no /api/quit) */

static void http_send(int fd, const char *status, const char *ctype,
                      const char *body, size_t len)
{
    Str h; str_init(&h);
    str_adds(&h, "HTTP/1.1 ");
    str_adds(&h, status);
    str_adds(&h, "\r\nContent-Type: ");
    str_adds(&h, ctype);
    str_adds(&h, "\r\nContent-Length: ");
    str_addf(&h, "%zu", len);
    str_adds(&h, "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n");
#ifdef _WIN32
    send(fd, h.data, (int)h.len, 0);
    if (len) send(fd, body, (int)len, 0);
#else
    (void)!write(fd, h.data, h.len);
    if (len) (void)!write(fd, body, len);
#endif
    str_free(&h);
}

static void json_reply(int fd, Str *json)
{
    http_send(fd, "200 OK", "application/json; charset=utf-8", json->data, json->len);
}

static void state_json(Str *o)
{
    char *exe = self_exe();
    char *home = home_get();
    char *core = path_join(home, ".deltaCore");
#ifdef _WIN32
    for (char *q = core; *q; q++) if (*q == '/') *q = '\\';
#endif
    str_adds(o, "{\"ok\":true,\"version\":\"");
    str_adds(o, DELTA_VERSION);
    str_adds(o, "\",\"os\":");
#ifdef _WIN32
    str_adds(o, "\"Windows\"");
#elif defined(__APPLE__)
    str_adds(o, "\"macOS\"");
#else
    str_adds(o, "\"Linux\"");
#endif
    str_adds(o, ",\"defaultDir\":");
    json_write_string(o, core, strlen(core));
    str_adds(o, ",\"exe\":");
    json_write_string(o, exe, strlen(exe));
    str_adds(o, ",\"logo\":");
    str_adds(o, "true");       /* embutida no exe */
    str_addc(o, '}');
    free(core);
    free(home);
    free(exe);
}

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

static void handle_request(int fd, const char *method, const char *path,
                           const char *body)
{
    if (str_eq(path, "/") || str_eq(path, "/index.html") ||
        str_eq(path, "/wiki") || str_eq(path, "/docs")) {
        http_send(fd, "200 OK", "text/html; charset=utf-8",
                  INSTALLER_HTML, sizeof INSTALLER_HTML - 1);
        return;
    }
    if (str_eq(path, "/logo.png")) {
        char *logo = find_logo();
        if (logo) {
            size_t n = 0;
            char *data = file_read(logo, &n);
            free(logo);
            if (data) {
                http_send(fd, "200 OK", "image/png", data, n);
                free(data);
                return;
            }
        }
        /* embutida no executavel: o exe e' autossuficiente */
        http_send(fd, "200 OK", "image/png", (const char *)LOGO_PNG, LOGO_PNG_LEN);
        return;
    }
    if (str_eq(path, "/api/state")) {
        Str o; str_init(&o);
        state_json(&o);
        json_reply(fd, &o);
        str_free(&o);
        return;
    }
    if (str_eq(path, "/api/pick") && str_eq(method, "POST")) {
        char *dir = pick_folder();
        Str o; str_init(&o);
        str_adds(&o, "{\"ok\":true,\"dir\":");
        json_write_string(&o, dir ? dir : "", dir ? strlen(dir) : 0);
        str_addc(&o, '}');
        json_reply(fd, &o);
        str_free(&o);
        free(dir);
        return;
    }
    if (str_eq(path, "/api/install") && str_eq(method, "POST")) {
        const char *dir = "";
        JValue *v = json_parse_cstr(body ? body : "{}");
        if (v) {
            dir = json_str(json_get(v, "dir"));
            if (!*dir) dir = "";
        }
        Str steps; str_init(&steps);
        Install in;
        int ok = *dir ? do_install(dir, &in, &steps) : 0;
        Str o; str_init(&o);
        if (!ok) {
            str_adds(&o, "{\"ok\":false,\"error\":");
            const char *msg = *dir ? "falhou ao instalar" : "escolha uma pasta";
            json_write_string(&o, msg, strlen(msg));
            str_adds(&o, ",\"steps\":[");
            str_adds(&o, steps.data ? steps.data : "");
            str_adds(&o, "]}");
        } else {
            int roda = verify_command(&in, &steps);
            str_adds(&o, "{\"ok\":");
            str_adds(&o, roda ? "true" : "false");
            str_adds(&o, ",\"coreDir\":");
            json_write_string(&o, in.core, strlen(in.core));
            str_adds(&o, ",\"exePath\":");
            json_write_string(&o, in.exe, strlen(in.exe));
            str_adds(&o, ",\"resolved\":");
            json_write_string(&o, in.resolved, strlen(in.resolved));
            str_adds(&o, ",\"pinned\":");
            json_write_string(&o, in.pinned, strlen(in.pinned));
            str_adds(&o, ",\"steps\":[");
            str_adds(&o, steps.data ? steps.data : "");
            str_adds(&o, "]}");
        }
        json_reply(fd, &o);
        str_free(&o);
        str_free(&steps);
        if (v) json_free(v);
        return;
    }
    if (str_eq(path, "/api/quit") && str_eq(method, "POST")) {
        http_send(fd, "200 OK", "application/json", "{\"ok\":true}", 11);
        g_stop = 1;
        /* fecha a escuta: o accept() volta com erro e o loop termina na hora */
        if (g_srv >= 0) {
#ifdef _WIN32
            closesocket((SOCKET)g_srv);
#else
            close(g_srv);
#endif
            g_srv = -1;
        }
        return;
    }
    http_send(fd, "404 Not Found", "text/plain", "404", 3);
}

static void open_browser(const char *url)
{
#ifdef _WIN32
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    Str c; str_init(&c); str_adds(&c, "open '"); str_adds(&c, url); str_adds(&c, "'");
    run_cmd(c.data); str_free(&c);
#else
    Str c; str_init(&c); str_adds(&c, "xdg-open '"); str_adds(&c, url); str_adds(&c, "' &");
    int rc = system(c.data);
    (void)rc;
    str_free(&c);
#endif
}

int setup_run(int wiki)
{
    delta_bootstrap();
#ifdef _WIN32
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);   /* SHBrowseForFolder */
#endif

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "delta: falha no winsock\n");
        return 1;
    }
    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) return 1;
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;                       /* porta livre */
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) != 0) return 1;
    if (listen(srv, 8) != 0) return 1;
    int alen = sizeof addr;
    getsockname(srv, (struct sockaddr *)&addr, &alen);
    int port = ntohs(addr.sin_port);
#else
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return 1;
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) != 0) return 1;
    if (listen(srv, 8) != 0) return 1;
    socklen_t alen = sizeof addr;
    getsockname(srv, (struct sockaddr *)&addr, &alen);
    int port = ntohs(addr.sin_port);
#endif

    g_srv = srv;

    char url[160];
    snprintf(url, sizeof url, "http://127.0.0.1:%d%s", port, wiki ? "/wiki" : "/");
    printf("Delta instalador: %s\n", url);
    printf("(feche esta janela ou use 'Fechar instalador' na pagina para sair)\n");
    fflush(stdout);
    if (!getenv("DELTA_SETUP_NOBROWSER")) open_browser(url);

    while (!g_stop) {
#ifdef _WIN32
        SOCKET cli = accept(srv, NULL, NULL);
        if (cli == INVALID_SOCKET) break;
        int cfd = (int)cli;
#else
        int cfd = accept(srv, NULL, NULL);
        if (cfd < 0) break;
#endif
        char req[65536];
        int got = 0;
#ifdef _WIN32
        got = recv(cfd, req, (int)sizeof req - 1, 0);
#else
        got = (int)read(cfd, req, sizeof req - 1);
#endif
#ifdef _WIN32
        if (got <= 0) { closesocket(cfd); continue; }
#else
        if (got <= 0) { close(cfd); continue; }
#endif
        req[got] = 0;

        char method[8] = "", path[512] = "";
        sscanf(req, "%7s %511s", method, path);
        const char *body = "";
        char *hdr = strstr(req, "\r\n\r\n");
        if (hdr) {
            body = hdr + 4;
            /* corpo pode ter vindo no mesmo pacote; se nao, le o resto */
            int want = 0;
            const char *cl = strstr(req, "Content-Length:");
            if (cl) want = atoi(cl + 15);
            int have = (int)strlen(body);
            while (have < want && got < (int)sizeof req - 1) {
#ifdef _WIN32
                int n = recv(cfd, req + got, (int)sizeof req - 1 - got, 0);
#else
                int n = (int)read(cfd, req + got, sizeof req - 1 - (size_t)got);
#endif
                if (n <= 0) break;
                got += n;
                req[got] = 0;
                hdr = strstr(req, "\r\n\r\n");
                body = hdr ? hdr + 4 : "";
                have = (int)strlen(body);
            }
        }
        handle_request(cfd, method, path, body);
#ifdef _WIN32
        closesocket(cfd);
#else
        close(cfd);
#endif
    }
#ifdef _WIN32
    closesocket(srv);
    WSACleanup();
#else
    close(srv);
#endif
    return 0;
}

/* Modo sem interface: delta --install <pasta> (usado nos testes) */
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
