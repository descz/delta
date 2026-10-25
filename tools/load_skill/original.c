/* =========================================================================
 * load_skill
 * -------------------------------------------------------------------------
 * arquivo   : ZenoTools/load_skill.c        (fragmento extraido, nao standalone)
 * origem    : ZenoC/src/zeno_tools.c:1381  ->  tool_load_skill
 * handler   : tool_load_skill
 * efeito    : READ_LOCAL
 * perfis    : skill, skill
 * -------------------------------------------------------------------------
 * schema    : {"type":"object","properties":{"name":{"type":"string","description":"Skill directory name"}},"required":["name"]}
 * -------------------------------------------------------------------------
 * Load the full instructions of one available skill by name (skills are listed in the system prompt).
 * =========================================================================
 */

static int tool_load_skill(void *context, const char *args, char **output, char **error) {
    ZenoRegistry *registry = (ZenoRegistry *)context;
    if (registry == NULL || registry->skills_dir == NULL) { if (error != NULL) *error = zeno_strdup("no skills directory attached"); return 0; }
    char *name = NULL;
    arg_string(args, "name", &name, "");
    int valid = name != NULL && *name != '\0' && strchr(name, '/') == NULL && strchr(name, '\\') == NULL && strstr(name, "..") == NULL;
    if (!valid) { free(name); if (error != NULL) *error = zeno_strdup("skill name required (no path separators)"); return 0; }
    char *path = zeno_format("%s/%s/SKILL.md", registry->skills_dir, name);
    int ok = 0;
    if (path != NULL) {
        FILE *file = fopen(path, "rb");
        if (file != NULL) {
            size_t capacity = 9000; size_t length = 0;
            char *buffer = (char *)malloc(capacity);
            if (buffer != NULL) {
                size_t got;
                while (length + 4096 < capacity && (got = fread(buffer + length, 1, 4096, file)) > 0) length += got;
                buffer[length] = '\0';
                *output = buffer;
                ok = 1;
            }
            (void)fclose(file);
        }
        free(path);
    }
    if (!ok) { if (error != NULL) *error = zeno_strdup("skill not found in skills directory"); }
    free(name);
    return ok;
}
