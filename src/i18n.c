/* i18n.c - idioma da interface (nomes de comando, paineis, avisos) */
#include "util.h"
#include <string.h>

enum { L_PTBR = 0, L_PT, L_EN, L_ES, L_ZH, L_JA, L_COUNT };

static int g_lang = L_PTBR;

static const char *const CODES[L_COUNT] = { "pt-BR", "pt", "en", "es", "zh", "ja" };
static const char *const NOMES[L_COUNT] = { "Português (Brasil)", "Português", "English", "Español", "中文", "日本語" };

/* chave, pt-BR, pt, en, es, zh, ja */
static const char *const T[][7] = {
    { "cmd.help", "Mostra os comandos e atalhos", "Mostra os comandos e atalhos", "Show commands and shortcuts",
      "Muestra los comandos y atajos", "显示命令和快捷键", "コマンドとショートカットを表示" },
    { "cmd.login", "Escolhe um provedor e salva a chave de API", "Escolhe um provedor e guarda a chave de API",
      "Pick a provider and save the API key", "Elige un proveedor y guarda la clave de API",
      "选择提供方并保存 API 密钥", "プロバイダーを選び API キーを保存" },
    { "cmd.logout", "Remove a chave do provedor atual", "Remove a chave do provedor atual",
      "Remove the current provider key", "Elimina la clave del proveedor actual",
      "移除当前提供方的密钥", "現在のプロバイダーのキーを削除" },
    { "cmd.models", "Todos os modelos de todos os provedores", "Todos os modelos de todos os provedores",
      "All models from all providers", "Todos los modelos de todos los proveedores",
      "所有提供方的所有模型", "全プロバイダーの全モデル" },
    { "cmd.thinking", "Nível de raciocínio", "Nível de raciocínio", "Reasoning level",
      "Nivel de razonamiento", "推理级别", "推論レベル" },
    { "cmd.theme", "Troca o tema", "Troca o tema", "Change the theme", "Cambia el tema",
      "切换主题", "テーマを変更" },
    { "cmd.tools", "Lista as ferramentas", "Lista as ferramentas", "List tools",
      "Lista las herramientas", "列出工具", "ツール一覧" },
    { "cmd.projects", "Define a pasta onde o Delta trabalha", "Define a pasta onde o Delta trabalha",
      "Set the folder Delta works in", "Define la carpeta donde trabaja Delta",
      "设置 Delta 的工作目录", "Delta の作業フォルダを設定" },
    { "cmd.status", "Mostra a configuração e o uso", "Mostra a configuração e o uso",
      "Show config and usage", "Muestra la configuración y el uso", "显示配置和用量", "設定と使用量を表示" },
    { "cmd.session", "Informações da sessão", "Informações da sessão", "Session info",
      "Información de la sesión", "会话信息", "セッション情報" },
    { "cmd.new", "Começa uma sessão nova", "Começa uma sessão nova", "Start a new session",
      "Empieza una sesión nueva", "开始新会话", "新しいセッションを開始" },
    { "cmd.clear", "Limpa a conversa", "Limpa a conversa", "Clear the conversation",
      "Limpia la conversación", "清空对话", "会話をクリア" },
    { "cmd.stop", "Interrompe a geração em andamento", "Interrompe a geração em andamento",
      "Stop the running generation", "Detiene la generación en curso", "停止正在进行的生成", "生成中の処理を停止" },
    { "cmd.copy", "Copia a última resposta para a área de transferência", "Copia a última resposta para a área de transferência",
      "Copy the last response to the clipboard", "Copia la última respuesta al portapapeles",
      "将最后一条回复复制到剪贴板", "最後の回答をクリップボードにコピー" },
    { "cmd.terminal", "Executa um comando no shell e mostra a saída", "Executa um comando no shell e mostra a saída",
      "Run a shell command and show its output", "Ejecuta un comando en el shell y muestra la salida",
      "执行 shell 命令并显示输出", "シェルコマンドを実行して出力を表示" },
    { "cmd.skills", "Lista as skills disponíveis", "Lista as skills disponíveis", "List available skills",
      "Lista las skills disponibles", "列出可用技能", "利用可能なスキル一覧" },
    { "cmd.extensions", "Painel de extensões (ou /extensions <pedido> cria)",
      "Painel de extensões (ou /extensions <pedido> cria)",
      "Extensions panel (or /extensions <request> to create)",
      "Panel de extensiones (o /extensions <petición> crea)",
      "扩展面板（或 /extensions <请求> 创建）", "拡張パネル（/extensions <依頼> で作成）" },
    { "cmd.translator", "Troca o idioma da interface", "Troca o idioma da interface",
      "Switch the interface language", "Cambia el idioma de la interfaz",
      "切换界面语言", "インターフェース言語を切替" },
    { "cmd.reload", "Recarrega config e tema do disco", "Recarrega config e tema do disco",
      "Reload config and theme from disk", "Recarga config y tema del disco",
      "从磁盘重新加载配置和主题", "設定とテーマを再読込" },
    { "cmd.quit", "Sai do Delta Agent", "Sai do Delta Agent", "Quit Delta Agent",
      "Sale de Delta Agent", "退出 Delta Agent", "Delta Agent を終了" },
    { "ui.working", "Working...", "A trabalhar...", "Working...", "Trabajando...", "处理中...", "処理中..." },
    { "ui.running", "Running %s...", "A executar %s...", "Running %s...", "Ejecutando %s...", "正在运行 %s...", "%s を実行中..." },
    { "ext.title", "Extensões", "Extensões", "Extensions", "Extensiones", "扩展", "拡張" },
    { "ext.hint", "Enter liga/desliga, Esc fecha", "Enter liga/desliga, Esc fecha",
      "Enter toggles, Esc closes", "Enter activa/desactiva, Esc cierra", "Enter 开/关，Esc 关闭", "Enter で切替、Esc で閉じる" },
    { "ext.empty", "(nenhuma extensão — use /extensions <pedido>)", "(nenhuma extensão — use /extensions <pedido>)",
      "(no extensions — use /extensions <request>)", "(sin extensiones — usa /extensions <petición>)",
      "（没有扩展 — 使用 /extensions <请求>）", "（拡張なし — /extensions <依頼> を使ってください）" },
    { "ext.on", "ligada", "ligada", "on", "activada", "已开启", "オン" },
    { "ext.off", "desligada", "desligada", "off", "desactivada", "已关闭", "オフ" },
    { "msg.ready", "pronto (%s/%s)", "pronto (%s/%s)", "ready (%s/%s)", "listo (%s/%s)", "就绪 (%s/%s)", "準備完了 (%s/%s)" },
    { "msg.nokey", "sem API key: use /login <chave>", "sem API key: usa /login <chave>",
      "no API key: use /login <key>", "sin API key: usa /login <clave>", "没有 API key：使用 /login <密钥>",
      "API キーなし：/login <キー> を使用" },
    { "msg.interrupted", "Interrompido", "Interrompido", "Interrupted", "Interrumpido", "已中断", "中断しました" },
    { "msg.enqueued", "mensagem enfileirada (%d na fila)", "mensagem em fila (%d na fila)",
      "message queued (%d in queue)", "mensaje en cola (%d en cola)", "消息已排队（队列 %d）", "メッセージをキューに追加（%d 件）" },
    { "lang.title", "Idioma", "Idioma", "Language", "Idioma", "语言", "言語" },
    { "lang.set", "idioma: %s", "idioma: %s", "language: %s", "idioma: %s", "语言：%s", "言語：%s" },
    { "lang.unknown", "idioma desconhecido: %s (use /translator para ver)", "idioma desconhecido: %s (usa /translator)",
      "unknown language: %s (see /translator)", "idioma desconocido: %s (usa /translator)",
      "未知语言：%s（查看 /translator）", "不明な言語：%s（/translator を参照）" },
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};

void i18n_set(int lang)
{
    if (lang >= 0 && lang < L_COUNT) g_lang = lang;
}

int i18n_get(void) { return g_lang; }

int i18n_count(void) { return L_COUNT; }
const char *i18n_code(int lang) { return (lang >= 0 && lang < L_COUNT) ? CODES[lang] : "pt-BR"; }
const char *i18n_name(int lang) { return (lang >= 0 && lang < L_COUNT) ? NOMES[lang] : "?"; }

int i18n_from_code(const char *code)
{
    if (!code) return -1;
    for (int i = 0; i < L_COUNT; i++)
        if (str_ieq(code, CODES[i])) return i;
    if (str_ieq(code, "ptbr") || str_ieq(code, "pt-br") || str_ieq(code, "br")) return L_PTBR;
    if (str_ieq(code, "jp") || str_ieq(code, "jpn")) return L_JA;
    if (str_ieq(code, "cn") || str_ieq(code, "zh-cn") || str_ieq(code, "chinese")) return L_ZH;
    if (str_ieq(code, "eng") || str_ieq(code, "english")) return L_EN;
    if (str_ieq(code, "espanol") || str_ieq(code, "español")) return L_ES;
    return -1;
}

const char *tr(const char *key)
{
    for (int i = 0; T[i][0]; i++)
        if (str_eq(T[i][0], key)) return T[i][1 + g_lang];
    return key;
}
