#include <stdbool.h>
#include <yed/plugin.h>

/* COMMANDS */
void
drill_take_key(int n_args, char** args);
;
void
drill_bind(int n_args, char** args);
void
drill_unbind(int n_args, char** args);
void
drill_exit_insert(int n_args, char** args);
void
drill_write(int n_args, char** args);
void
drill_quit(int n_args, char** args);
void
drill_write_quit(int n_args, char** args);
/* END COMMANDS */

/* pp :) */
#define MODE_NORMAL (0x0)
#define MODE_INSERT (0x1)
#define N_MODES     (0x2)

/* Variables */
static int restore_cursor_line;

static char* mode_strs[]           = {"NORMAL", "INSERT"};

static char* mode_strs_lowercase[] = {"normal", "insert"};
/* End variables */

static int
drill_mode_completion(char* string, yed_completion_results* results)
{
    int status;
    // if for some reason you reduce the number of modes even more, lower this
    // number to match N_MODES
    FN_BODY_FOR_COMPLETE_FROM_ARRAY(string, 2, mode_strs_lowercase, results,
                                    status);
    return status;
}

typedef struct
{
    int    len;
    int    keys[MAX_SEQ_LEN];
    char*  cmd;
    int    key;
    int    n_args;
    char** args;
} drill_key_binding;

static yed_plugin* Self;
static int         mode;
static array_t     mode_bindings[N_MODES];
static array_t     repeat_keys;
static int         repeating;
static int till_pending; /* 0 = not pending, 1 = pending forward, 2 = pending
                            backward, 3 = pending backward; stop before */
static int  last_till_key;
static char last_till_op;
static int  num_undo_records_before_insert;

void
drill_unload(yed_plugin* self);
void
drill_normal(int key, char* key_str);
void
drill_insert(int key, char* key_str);
void
bind_keys(void);
void
drill_change_mode(int new_mode, int by_line, int cancel);
void
enter_insert(void);
void
exit_insert(void);
void
drill_make_binding(int b_mode, int n_keys, int* keys, char* cmd, int n_args,
                   char** args);
void
drill_remove_binding(int b_mode, int n_keys, int* keys);

// up for change
void
drill_enter_insert();

/* entry point */
int
yed_plugin_boot(yed_plugin* self)
{
    int i;
    YED_PLUG_VERSION_CHECK();
    Self = self;
    for (i = 0; i < N_MODES; i += 1)
    {
        mode_bindings[i] = array_make(drill_key_binding);
    }
    repeat_keys = array_make(int);
    yed_plugin_set_unload_fn(Self, drill_unload);
    yed_plugin_set_command(Self, "drill-take-key", drill_take_key);
    yed_plugin_set_command(Self, "drill-bind", drill_bind);
    yed_plugin_set_command(Self, "drill-unbind", drill_unbind);
    yed_plugin_set_command(Self, "drill-exit-insert", drill_exit_insert);
    yed_plugin_set_command(Self, "drill-enter-insert", drill_enter_insert);
    yed_plugin_set_command(Self, "w", drill_write);
    yed_plugin_set_command(Self, "W", drill_write);
    yed_plugin_set_command(Self, "q", drill_quit);
    yed_plugin_set_command(Self, "Q", drill_quit);
    yed_plugin_set_command(Self, "wq", drill_write_quit);
    yed_plugin_set_command(Self, "Wq", drill_write_quit);
    yed_plugin_set_completion(Self, "drill-mode", drill_mode_completion);
    yed_plugin_set_completion(Self, "drill-bind-compl-arg-0",
                              drill_mode_completion);
    yed_plugin_set_completion(Self, "drill-bind-compl-arg-2",
                              yed_get_completion("command"));
    yed_plugin_set_completion(Self, "drill-unbind-compl-arg-0",
                              drill_mode_completion);
    bind_keys();
    drill_change_mode(MODE_NORMAL, 0, 0);
    yed_set_var("drill-mode", mode_strs[mode]);
    YEXE("set", "status-line-var", "drill-mode");
    return 0;
}

void
drill_unload(yed_plugin* self)
{
    int                i, j;
    drill_key_binding* b;
    for (i = 0; i < N_MODES; i += 1)
    {
        array_traverse(mode_bindings[i], b)
        {
            if (b->args)
            {
                for (j = 0; j < b->n_args; j += 1)
                {
                    free(b->args[j]);
                }
                free(b->args);
            }
            free(b->cmd);
        }
        array_free(mode_bindings[i]);
    }
    array_free(repeat_keys);
}

void
bind_keys(void)
{
    int   meta_keys[2];
    int   meta_key;
    char* ctrl_h_is_bs;
    char* ctrl_h_is_bs_cpy;
    int   key;
    char  key_str[32];
    meta_keys[0] = ESC;
    meta_keys[1] = ARROW_UP;
    meta_key     = yed_get_key_sequence(2, meta_keys);
    yed_unbind_key(meta_key);
    yed_delete_key_sequence(meta_key);
    meta_keys[1] = ARROW_DOWN;
    meta_key     = yed_get_key_sequence(2, meta_keys);
    yed_unbind_key(meta_key);
    yed_delete_key_sequence(meta_key);
    meta_keys[1] = ARROW_RIGHT;
    meta_key     = yed_get_key_sequence(2, meta_keys);
    yed_unbind_key(meta_key);
    yed_delete_key_sequence(meta_key);
    meta_keys[1] = ARROW_LEFT;
    meta_key     = yed_get_key_sequence(2, meta_keys);
    yed_unbind_key(meta_key);
    yed_delete_key_sequence(meta_key);
    if ((ctrl_h_is_bs = yed_get_var("ctrl-h-is-backspace")))
    {
        ctrl_h_is_bs_cpy = strdup(ctrl_h_is_bs);
        yed_unset_var("ctrl-h-is-backspace");
    }
    for (key = 1; key < REAL_KEY_MAX; key += 1)
    {
        sprintf(key_str, "%d", key);
        YPBIND(Self, key, "drill-take-key", key_str);
    }
    if (ctrl_h_is_bs)
    {
        yed_set_var("ctrl-h-is-backspace", ctrl_h_is_bs_cpy);
        free(ctrl_h_is_bs_cpy);
    }
}

void
drill_change_mode(int new_mode, int by_line, int cancel)
{
    char               key_str[32];
    drill_key_binding* b;
    array_traverse(mode_bindings[mode], b)
    {
        yed_unbind_key(b->key);
        if (b->len > 1)
        {
            yed_delete_key_sequence(b->key);
        }
        else if (b->key < REAL_KEY_MAX)
        {
            sprintf(key_str, "%d", b->key);
            YPBIND(Self, b->key, "drill-take-key", key_str);
        }
    }
    array_traverse(mode_bindings[new_mode], b)
    {
        if (b->len > 1)
        {
            b->key = yed_plugin_add_key_sequence(Self, b->len, b->keys);
        }
        else
        {
            b->key = b->keys[0];
        }
        yed_plugin_bind_key(Self, b->key, b->cmd, b->n_args, b->args);
    }
    switch (mode)
    {
        case MODE_NORMAL:
            break;
        case MODE_INSERT:
            exit_insert();
            break;
    }
    mode = new_mode;
    switch (new_mode)
    {
        case MODE_NORMAL:
            {
                yed_set_var("enable-search-cursor-move", "no");
                break;
            }
        case MODE_INSERT:
            enter_insert();
            break;
    }
    yed_set_var("drill-mode", mode_strs[new_mode]);
}

static void
_drill_take_key(int key, char* maybe_key_str)
{
    char* key_str, buff[32];
    if (maybe_key_str)
    {
        key_str = maybe_key_str;
    }
    else
    {
        sprintf(buff, "%d", key);
        key_str = buff;
    }
    switch (mode)
    {
        case MODE_NORMAL:
            drill_normal(key, key_str);
            break;
        case MODE_INSERT:
            drill_insert(key, key_str);
            break;
        default:
            LOG_FN_ENTER();
            yed_log("[!] invalid mode (?)");
            LOG_EXIT();
    }
}

void
drill_take_key(int n_args, char** args)
{
    int key;
    if (n_args != 1)
    {
        yed_cerr("expected 1 argument, but got %d", n_args);
        return;
    }
    sscanf(args[0], "%d", &key);
    _drill_take_key(key, args[0]);
}

void
drill_bind(int n_args, char** args)
{
    char* mode_str, *cmd;
    int   b_mode, n_keys, keys[MAX_SEQ_LEN], n_cmd_args;
    if (n_args < 1)
    {
        yed_cerr("missing 'mode' as first argument");
        return;
    }
    mode_str = args[0];
    if (strcmp(mode_str, "normal") == 0)
    {
        b_mode = MODE_NORMAL;
    }
    else if (strcmp(mode_str, "insert") == 0)
    {
        b_mode = MODE_INSERT;
    }
    else
    {
        yed_cerr("no mode named '%s'", mode_str);
        return;
    }
    if (n_args < 2)
    {
        yed_cerr("missing 'keys' as second argument");
        return;
    }
    if (n_args < 3)
    {
        yed_cerr(
                    "missing 'command', 'command_args'... as third and up arguments");
        return;
    }
    n_keys = yed_string_to_keys(args[1], keys);
    if (n_keys == -1)
    {
        yed_cerr("invalid string of keys '%s'", args[1]);
        return;
    }
    if (n_keys == -2)
    {
        yed_cerr("too many keys to be a sequence in '%s'", args[1]);
        return;
    }
    cmd        = args[2];
    n_cmd_args = n_args - 3;
    drill_make_binding(b_mode, n_keys, keys, cmd, n_cmd_args, args + 3);
}

void
drill_unbind(int n_args, char** args)
{
    char* mode_str;
    int   b_mode, n_keys, keys[MAX_SEQ_LEN];
    if (n_args < 1)
    {
        yed_cerr("missing 'mode' as first argument");
        return;
    }
    mode_str = args[0];
    if (strcmp(mode_str, "normal") == 0)
    {
        b_mode = MODE_NORMAL;
    }
    else if (strcmp(mode_str, "insert") == 0)
    {
        b_mode = MODE_INSERT;
    }
    else
    {
        yed_cerr("no mode named '%s'", mode_str);
        return;
    }
    if (n_args != 2)
    {
        yed_cerr("missing 'keys' as second argument");
        return;
    }
    n_keys = yed_string_to_keys(args[1], keys);
    if (n_keys == -1)
    {
        yed_cerr("invalid string of keys '%s'", args[1]);
        return;
    }
    if (n_keys == -2)
    {
        yed_cerr("too many keys to be a sequence in '%s'", args[1]);
        return;
    }
    drill_remove_binding(b_mode, n_keys, keys);
}

void
drill_make_binding(int b_mode, int n_keys, int* keys, char* cmd, int n_args,
                   char** args)
{
    int               i;
    drill_key_binding binding, *b;
    if (n_keys <= 0)
    {
        return;
    }
    binding.len = n_keys;
    for (i = 0; i < n_keys; i += 1)
    {
        binding.keys[i] = keys[i];
    }
    binding.cmd    = strdup(cmd);
    binding.key    = KEY_NULL;
    binding.n_args = n_args;
    if (n_args)
    {
        binding.args = malloc(sizeof(char*) * n_args);
        for (i = 0; i < n_args; i += 1)
        {
            binding.args[i] = strdup(args[i]);
        }
    }
    else
    {
        binding.args = NULL;
    }
    array_push(mode_bindings[b_mode], binding);
    if (b_mode == mode)
    {
        array_traverse(mode_bindings[mode], b)
        {
            yed_unbind_key(b->key);
            if (b->len > 1)
            {
                yed_delete_key_sequence(b->key);
            }
        }
        array_traverse(mode_bindings[mode], b)
        {
            if (b->len > 1)
            {
                b->key = yed_plugin_add_key_sequence(Self, b->len, b->keys);
            }
            else
            {
                b->key = b->keys[0];
            }
            yed_plugin_bind_key(Self, b->key, b->cmd, b->n_args, b->args);
        }
    }
}

void
drill_remove_binding(int b_mode, int n_keys, int* keys)
{
    int                i;
    drill_key_binding* b;
    if (n_keys <= 0)
    {
        return;
    }
    i = 0;
    array_traverse(mode_bindings[b_mode], b)
    {
        if (b->len == n_keys &&
                memcmp(b->keys, keys, n_keys * sizeof(int)) == 0)
        {
            break;
        }
        i += 1;
    }
    if (i == array_len(mode_bindings[b_mode]))
    {
        return;
    }
    if (b_mode == mode)
    {
        array_traverse(mode_bindings[mode], b)
        {
            yed_unbind_key(b->key);
            if (b->len > 1)
            {
                yed_delete_key_sequence(b->key);
            }
        }
        array_delete(mode_bindings[b_mode], i);
        ;
        array_traverse(mode_bindings[mode], b)
        {
            if (b->len > 1)
            {
                b->key = yed_plugin_add_key_sequence(Self, b->len, b->keys);
            }
            else
            {
                b->key = b->keys[0];
            }
            yed_plugin_bind_key(Self, b->key, b->cmd, b->n_args, b->args);
        }
    }
    else
    {
        array_delete(mode_bindings[b_mode], i);
    }
}

static void
drill_push_repeat_key(int key)
{
    if (repeating)
    {
        return;
    }
    array_push(repeat_keys, key);
}

static void
drill_pop_repeat_key(void)
{
    if (repeating)
    {
        return;
    }
    array_pop(repeat_keys);
}

static void
drill_repeat(void)
{
    int* key_it;
    repeating = 1;
    array_traverse(repeat_keys, key_it)
    {
        _drill_take_key(*key_it, NULL);
    }
    repeating = 0;
}

static void
drill_start_repeat(int key)
{
    if (repeating)
    {
        return;
    }
    array_clear(repeat_keys);
    array_push(repeat_keys, key);
}

void
drill_exit_insert(int n_args, char** args)
{
    drill_push_repeat_key(CTRL_C);
    drill_change_mode(MODE_NORMAL, 0, 0);
}

static void
drill_do_till_fw(int key)
{
    yed_frame* f;
    yed_line*  line;
    int        col;
    yed_glyph* g;
    if (!ys->active_frame || !ys->active_frame->buffer)
    {
        goto out;
    }
    f    = ys->active_frame;
    line = yed_buff_get_line(f->buffer, f->cursor_line);
    if (!line)
    {
        goto out;
    }
    for (col = f->cursor_col + 1; col <= line->visual_width;)
    {
        g = yed_line_col_to_glyph(line, col);
        if (g->c == key)
        {
            yed_set_cursor_within_frame(f, f->cursor_line, col);
            break;
        }
        col += yed_get_glyph_width(*g);
    }
    last_till_key = key;
out:
    till_pending = 0;
    return;
}

static void
drill_do_till_bw(int key, int stop_before)
{
    yed_frame* f;
    yed_line*  line;
    int        col;
    yed_glyph* g;
    if (!ys->active_frame || !ys->active_frame->buffer)
    {
        goto out;
    }
    f    = ys->active_frame;
    line = yed_buff_get_line(f->buffer, f->cursor_line);
    if (!line)
    {
        goto out;
    }
    for (col = f->cursor_col - 1; col >= 1;)
    {
        g = yed_line_col_to_glyph(line, col);
        if (g->c == key)
        {
            yed_set_cursor_within_frame(
                        f, f->cursor_line,
                        col + (stop_before * yed_get_glyph_width(*g)));
            break;
        }
        if (col == 1)
        {
            break;
        } /* Didn't find it. Prevent endless loop. */
        col = yed_line_idx_to_col(line, yed_line_col_to_idx(line, col - 1));
    }
    last_till_key = key;
out:
    till_pending = 0;
    return;
}

static void
drill_repeat_till(void)
{
    if (last_till_op == 'f' || last_till_op == 't')
    {
        drill_do_till_fw(last_till_key);
    }
    else if (last_till_op == 'F' || last_till_op == 'T')
    {
        drill_do_till_bw(last_till_key, last_till_op == 'T');
    }
}

enum select_type
{
    SELECT = 1,
    VISUAL = 2,
    WORD   = 4,
    XEL    = 8,
    GLOB   = 16,
};
unsigned char drill_bool = 0;
int
drill_nav_common(int key, char* key_str)
{
    if (till_pending == 1)
    {
        drill_do_till_fw(key);
        if (mode != MODE_NORMAL)
        {
            drill_push_repeat_key(key);
        }
        goto out;
    }
    else if (till_pending > 1)
    {
        drill_do_till_bw(key, till_pending == 3);
        if (mode != MODE_NORMAL)
        {
            drill_push_repeat_key(key);
        }
        goto out;
    }
    switch (key)
    {
        case 'h':
        case ARROW_LEFT:
            /*      check if visual mode in on    */
            if (!(drill_bool & VISUAL)) YEXE("select-off");
            YEXE("cursor-left");
            /*          turn off WORD and x selection    */
            drill_bool &= ~(WORD | XEL | GLOB);
            break;
        case 'j':
        case ARROW_DOWN:
            if (!(drill_bool & VISUAL)) YEXE("select-off");
            YEXE("cursor-down");
            drill_bool &= ~(WORD | XEL | GLOB);
            break;
        case 'k':
        case ARROW_UP:
            if (!(drill_bool & VISUAL)) YEXE("select-off");
            YEXE("cursor-up");
            drill_bool &= ~(WORD | XEL | GLOB);
            break;
        case 'l':
        case ARROW_RIGHT:
            if (!(drill_bool & VISUAL)) YEXE("select-off");
            YEXE("cursor-right");
            drill_bool &= ~(WORD | XEL | GLOB);
            break;
        case PAGE_UP:
            if (!(drill_bool & VISUAL)) YEXE("select-off");
            YEXE("cursor-page-up");
            drill_bool &= ~(WORD | XEL | GLOB);
            break;
        case PAGE_DOWN:
            if (!(drill_bool & VISUAL)) YEXE("select-off");
            YEXE("cursor-page-down");
            drill_bool &= ~(WORD | XEL | GLOB);
            break;
        case 'w':
            if (!(drill_bool & VISUAL))
            {
                YEXE("select-off");
                YEXE("select");
            }
            YEXE("cursor-next-word");
            /*          turn on word select    */
            drill_bool |= WORD;
            break;
        case 'q':
            if (!(drill_bool & VISUAL))
            {
                YEXE("select-off");
                YEXE("select");
            }
            YEXE("cursor-prev-word");
            drill_bool |= WORD;
            break;
        case '^':
        case HOME_KEY:
            YEXE("cursor-line-begin");
            break;
        case '$':
        case END_KEY:
            YEXE("cursor-line-end");
            break;
        case '{':
            YEXE("cursor-prev-paragraph");
            break;
        case '}':
            YEXE("cursor-next-paragraph");
            break;
        case '/':
            YEXE("find-in-buffer");
            break;
        case '?':
            YEXE("replace-current-search");
            break;
        case 'n':
            YEXE("find-next-in-buffer");
            break;
        case 'f':
        case 't':
            till_pending = 1;
            last_till_op = key;
            break;
        case 'F':
        case 'T':
            till_pending = 2 + (key == 'T');
            last_till_op = key;
            break;
        case ';':
            drill_repeat_till();
            break;
        default:
            return 0;
    }
out:
    return 1;
}

void
drill_normal(int key, char* key_str)
{
    if (drill_bool & VISUAL)
    {
        yed_buffer* buff = ys->active_frame->buffer;
        if (!buff->has_selection)
        {
            /*          turn off visual mode    */
            drill_bool &= ~(VISUAL);
        }
    }
    if (drill_nav_common(key, key_str))
    {
        return;
    }
    switch (key)
    {
        case 'd':
            if (!(drill_bool & (WORD | XEL | VISUAL | GLOB)))
            {
                YEXE("select");
                YEXE("cursor-right");
            }
            YEXE("yank-selection", "1");
            YEXE("delete-forward");
            drill_bool &= ~(XEL | GLOB);
            break;
        case 'y':
            YEXE("yank-selection", "1");
            drill_bool &= ~(XEL | GLOB);
            break;
        case 'v':
            if (drill_bool & VISUAL)
            {
                YEXE("select-off");
            }
            if (!(drill_bool & (VISUAL | WORD | XEL)))
            {
                YEXE("select");
            }
            drill_bool |= VISUAL;
            drill_bool &= ~(WORD | XEL | GLOB);
            break;
        case 'x':
            if (drill_bool & XEL)
            {
                YEXE("select-lines");
                YEXE("cursor-down");
                YEXE("select-lines");
            }
            else
            {
                YEXE("select-lines");
            }
            drill_bool |= XEL;
            YEXE("cursor-line-end");
            break;
        case 'X':
            if (!(drill_bool & (XEL | VISUAL)))
            {
                YEXE("select-lines");
            }
            drill_bool |= XEL;
            YEXE("cursor-down");
            YEXE("cursor-line-end");
            break;
        case 's':
            YEXE("find-in-buffer");
            drill_bool &= ~(XEL | GLOB);
            break;
        case '%':
            YEXE("cursor-buffer-begin");
            if (!(drill_bool & (XEL | VISUAL)))
            {
                YEXE("select-lines");
            }
            YEXE("cursor-buffer-end");
            drill_bool |= GLOB;
            break;
        case 'c':
            YEXE("replace-current-search");
            break;
        case 'p':
            drill_start_repeat(key);
            YEXE("paste-yank-buffer");
            break;
        case 'a':
            if (!(drill_bool & (VISUAL | WORD | XEL)))
            {
                YEXE("select");
            }
            drill_start_repeat(key);
            YEXE("cursor-right");
            drill_bool |= VISUAL;
            goto enter_insert;
        case 'A':
            YEXE("select-off");
            drill_start_repeat(key);
            YEXE("cursor-line-end");
            goto enter_insert;
        case 'o':
            YEXE("select-off");
            YEXE("cursor-line-end");
            YEXE("insert", "13");
            goto enter_insert;
            break;
        case 'O':
            YEXE("select-off");
            YEXE("cursor-up");
            YEXE("cursor-line-end");
            YEXE("insert", "13");
            goto enter_insert;
            break;
        case 'i':
            YEXE("select-off");
            drill_start_repeat(key);
enter_insert:
            drill_change_mode(MODE_INSERT, 0, 0);
            break;
        case DEL_KEY:
            YEXE("select-off");
            drill_start_repeat(key);
            YEXE("delete-forward");
            break;
        case 'u':
            YEXE("undo");
            break;
        case 'U':
            YEXE("redo");
            break;
        case '.':
            YEXE("select-off");
            drill_repeat();
            break;
        case ':':
            YEXE("command-prompt");
            break;
            ;
        case ESC:
        case CTRL_C:
            YEXE("select-off");
            break;
        case CTRL_Z:
            YEXE("suspend");
            break;
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            {
                char buff[32];
                sprintf(buff, "cursor-line %d", key - '0');
                YEXE("command-prompt", buff);
                break;
            }
        default:
            yed_cerr("[NORMAL] unhandled key %d", key);
    }
}
int last_insert;
void
drill_insert(int key, char* key_str)
{
    drill_push_repeat_key(key);
    switch (key)
    {
        case ARROW_LEFT:
            YEXE("cursor-left");
            break;
        case ARROW_DOWN:
            YEXE("cursor-down");
            break;
        case ARROW_UP:
            YEXE("cursor-up");
            break;
        case ARROW_RIGHT:
            YEXE("cursor-right");
            break;
        case PAGE_UP:
            YEXE("cursor-page-up");
            break;
        case PAGE_DOWN:
            YEXE("cursor-page-down");
            break;
        case HOME_KEY:
            YEXE("cursor-line-begin");
            break;
        case END_KEY:
            YEXE("cursor-line-end");
            break;
        case BACKSPACE:
            YEXE("delete-back");
            break;
        case DEL_KEY:
            YEXE("delete-forward");
            break;
        case ESC:
        case CTRL_C:
            drill_change_mode(MODE_NORMAL, 0, 1);
            break;
        default:
            if (key == ENTER || key == TAB || key == MBYTE || !iscntrl(key))
            {
                YEXE("insert", key_str);
                last_insert = key;
            }
            else
            {
                drill_pop_repeat_key();
                yed_cerr("[INSERT] unhandled key %d", key);
            }
    }
}

void
drill_write(int n_args, char** args)
{
    yed_execute_command("write-buffer", n_args, args);
}

void
drill_quit(int n_args, char** args)
{
    yed_frame* frame;
    if (array_len(ys->frames) > 1)
    {
        YEXE("frame-delete");
    }
    else
    {
        if (array_len(ys->frames) == 1)
        {
            frame = *(yed_frame**)array_item(ys->frames, 0);
            if (frame->top == 1 && frame->left == 1 &&
                    frame->height == ys->term_rows - 2 &&
                    frame->width == ys->term_cols)
            {
                YEXE("quit");
            }
            else
            {
                YEXE("frame-delete");
            }
        }
        else
        {
            YEXE("quit");
        }
    }
}

void
drill_write_quit(int n_args, char** args)
{
    YEXE("w");
    YEXE("q");
}

void
enter_insert(void)
{
    yed_frame*  frame;
    yed_buffer* buff;
    frame = ys->active_frame;
    if (frame)
    {
        buff = frame->buffer;
        if (buff)
        {
            num_undo_records_before_insert = yed_get_undo_num_records(buff);
        }
    }
    if (yed_get_var("drill-insert-no-cursor-line") &&
            yed_get_var("cursor-line"))
    {
        restore_cursor_line = 1;
        yed_set_var("cursor-line", "no");
    }
}

void
exit_insert(void)
{
    yed_frame*  frame;
    yed_buffer* buff;
    frame = ys->active_frame;
    if (frame)
    {
        buff = frame->buffer;
        if (buff)
        {
            while (yed_get_undo_num_records(buff) >
                    num_undo_records_before_insert + 1)
            {
                yed_merge_undo_records(buff);
            }
        }
    }
    if (restore_cursor_line && yed_get_var("drill-insert-no-cursor-line"))
    {
        yed_set_var("cursor-line", "yes");
        restore_cursor_line = 0;
    }
}

void
drill_enter_insert(void)
{
    YEXE("select-off");
    drill_start_repeat(last_insert);
    ;
    drill_change_mode(MODE_INSERT, 0, 0);
}
