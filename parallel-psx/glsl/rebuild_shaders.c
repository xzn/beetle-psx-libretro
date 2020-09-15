#include <time.h>

#include "khash.h"
#include "kstring.h"
#include "kvec.h"
#include "wyhash.h"

void print_usage_and_exit()
{
    fputs(
        "rebuild_shaders [-v[v]] "
        "<in_conf> <out_make> <out_hpp> <in_dir_to_make>\n",
        stdout);
    exit(1);
}

kstring_t kstring_init = { 0, 0, NULL };
const char *in_dir_to_make;

typedef struct hashed_string_s {
    uint64_t hash;
    char *str;
} hashed_string_t;
hashed_string_t empty_hashed_string;

typedef kvec_t(hashed_string_t) vals_t;
typedef struct val_s {
    hashed_string_t name;
    vals_t vals;
} val_t;

struct define_s;
typedef kvec_t(struct define_s) defines_t;

typedef struct one_s {
    defines_t defines;
} one_t;

typedef struct all_s {
    defines_t defines;
} all_t;

typedef enum define_type_e {
    DEFINE_TYPE_LIT,
    DEFINE_TYPE_VAL,
    DEFINE_TYPE_ONE,
    DEFINE_TYPE_ALL,
} define_type_t;

typedef struct define_s {
    define_type_t type;
    union {
        hashed_string_t lit;
        val_t val;
        one_t one;
        all_t all;
    } d;
} define_t;

typedef enum program_type_e {
    PROGRAM_TYPE_VERTEX,
    PROGRAM_TYPE_FRAGMENT,
    PROGRAM_TYPE_COMPUTE,
} program_type_t;

typedef struct program_s {
    program_type_t type;
    uint64_t define_hash;
    define_t define;
} program_t;

uint64_t seed;
uint64_t string_hash(kstring_t s)
{
    return wyhash(s.s, s.l, seed, _wyp);
}

void *define_p(define_t define)
{
    void *p;
    switch (define.type)
    {
        case DEFINE_TYPE_LIT:
            p = define.d.lit.str;
            break;

        case DEFINE_TYPE_VAL:
            p = define.d.val.name.str;
            break;

        case DEFINE_TYPE_ONE:
            p = define.d.one.defines.a;
            break;

        case DEFINE_TYPE_ALL:
            p = define.d.all.defines.a;
            break;

        default:
            p = NULL;
            break;
    }
    return p;
}

uint64_t pointer_hash(void *p)
{
    return wyhash(&p, sizeof(p), seed, _wyp);
}

uint64_t define_hash(define_t define)
{
    void *p = define_p(define);
    return pointer_hash(p);
}

khint_t program_define_hash(program_t p)
{
    return p.define_hash;
}

uint64_t program_define_equal(program_t p1, program_t p2)
{
    return define_p(p1.define) == define_p(p2.define);
}

KHASH_INIT(programs_defines, program_t, size_t, 1,
    program_define_hash, program_define_equal)
typedef khash_t(programs_defines) *programs_defines_t;

hashed_string_t make_hashed_string(kstring_t s)
{
    hashed_string_t h = {
        string_hash(s),
        s.s
    };
    return h;
}

khint_t hashed_string_hash(hashed_string_t s)
{
    return s.hash;
}

int hashed_string_equal(hashed_string_t s1, hashed_string_t s2)
{
    return strcmp(s1.str, s2.str) == 0;
}

KHASH_INIT(programs, hashed_string_t, program_t, 1,
    hashed_string_hash, hashed_string_equal)
typedef khash_t(programs) *programs_t;

typedef struct named_program_s {
    hashed_string_t name;
    program_t program;
} named_program_t;
typedef kvec_t(named_program_t) named_programs_t;

typedef struct parsed_define_s {
    hashed_string_t name, val;
} parsed_define_t;
typedef kvec_t(parsed_define_t) parsed_defines_t;
typedef kvec_t(parsed_defines_t) parsed_defines_list_t;

typedef struct parsed_program_s {
    program_type_t type;
    hashed_string_t name;
    size_t defines_list_i;
} parsed_program_t;

typedef kvec_t(parsed_defines_list_t) parsed_defines_lists_t;
typedef struct parsed_programs_s {
    parsed_defines_lists_t defines_lists;
    kvec_t(parsed_program_t) programs;
} parsed_programs_t;

typedef enum parse_stack_type_e {
    PARSE_TYPE_ONE,
    PARSE_TYPE_ALL,
} parse_stack_type_t;

typedef struct parse_e {
    parse_stack_type_t type;
    union {
        one_t one;
        all_t all;
    } p;
} parse_t;

typedef enum parse_type_e {
    PARSE_TYPE_TOP,
    PARSE_TYPE_PROGRAM,
    PARSE_TYPE_DEFINE,
    PARSE_TYPE_VAL,
} parse_type_t;

typedef enum parse_tok_type_e {
    PARSE_TYPE_COMMAND_NAME,
    PARSE_TYPE_FILE_NAME,
    PARSE_TYPE_DEFINE_NAME,
    PARSE_TYPE_DEFINE_VAL,
} parse_tok_type_t;

typedef kvec_t(parse_t) parse_stack_t;

typedef struct parse_state_s {
    parse_type_t type;
    parse_stack_t stack;
    unsigned line, col;
    int c;
    kstring_t tok;
    parse_tok_type_t tok_type;
    int cmd;
    programs_t programs;
    kvec_t(hashed_string_t) program_file_names;
    program_t program;
    val_t val;
} parse_state_t;

void print_line_col(unsigned line, unsigned col)
{
    fprintf(stderr, "%u, %u: ", line, col);
}

int is_ascii_printable(int c)
{
    return ' ' <= c && c <= '~';
}

void print_parse_error_and_exit(const parse_state_t *state,
    int tok, const char *expected)
{
    print_line_col(
        state->line,
        state->col - (tok ? strlen(state->tok.s) : 0));

    if (tok)
    {
        fputs("Unexpected \"", stderr);
        fputs(state->tok.s, stderr);
        fputc('"', stderr);
    }
    else
    {
        int c = state->c;
        if (c == EOF)
        {
            fputs("Unexpected end of file", stderr);
            return;
        }

        fputs("Unexpected '", stderr);
        if (c == '\'')
            fputs("\\'", stderr);
        if (is_ascii_printable(c))
            fputc(c, stderr);
        else
            fprintf(stderr, "\\x%02x", c);
        fputc('\'', stderr);
    }
    fputs(", expected ", stderr);
    fputs(expected, stderr);
    fputc('\n', stderr);

    exit(1);
}

parse_t *parse_get(parse_state_t *state, int i)
{
    return &kv_A(state->stack, state->stack.n - i);
}

void parse_push(parse_state_t *state, parse_t parse)
{
    kv_push(parse_t, state->stack, parse);
}

parse_t *parse_pop(parse_state_t *state)
{
    return &kv_pop(state->stack);
}

void parse_program_push(parse_state_t *state,
    hashed_string_t name, program_t program)
{
    int r;
    khiter_t i = kh_put(programs, state->programs, name, &r);
    kh_value(state->programs, i) = program;
}

void parse_define_push(parse_state_t *state, define_t define)
{
    if (state->stack.n > 0)
    {
        parse_t *parse = parse_get(state, 1);
        switch (parse->type)
        {
            case PARSE_TYPE_ONE:
                kv_push(define_t, parse->p.one.defines, define);
                break;

            case PARSE_TYPE_ALL:
                kv_push(define_t, parse->p.all.defines, define);
                break;
        }
        state->type = PARSE_TYPE_DEFINE;
    }
    else
    {
        state->program.define = define;
        state->program.define_hash = define_hash(define);
        state->type = PARSE_TYPE_PROGRAM;
    }
}

void parse_file_name_push(parse_state_t *state, hashed_string_t name)
{
    kv_push(hashed_string_t, state->program_file_names, name);
}

void parse_val_push(parse_state_t *state, hashed_string_t val)
{
    kv_push(hashed_string_t, state->val.vals, val);
}

void parse_program_command_tok(parse_state_t *state)
{
    program_type_t *type = &state->program.type;
    if (strcmp(state->tok.s, "vertex") == 0)
        *type = PROGRAM_TYPE_VERTEX;
    else if (strcmp(state->tok.s, "fragment") == 0)
        *type = PROGRAM_TYPE_FRAGMENT;
    else if (strcmp(state->tok.s, "compute") == 0)
        *type = PROGRAM_TYPE_COMPUTE;
    else
        print_parse_error_and_exit(state, 1,
            "\"vertex\", \"fragment\", or \"compute\"");

    state->program_file_names.n = 0;
    state->program.define.type = DEFINE_TYPE_LIT;
    state->program.define.d.lit = empty_hashed_string;
    state->type = PARSE_TYPE_PROGRAM;
}

void parse_define_command_tok(parse_state_t *state)
{
    if (strcmp(state->tok.s, "one") == 0)
    {
        if (
            state->stack.n > 0 &&
            parse_get(state, 1)->type == PARSE_TYPE_ONE
        )
            print_parse_error_and_exit(state, 1,
                "\"val\" or \"all\" command inside \"one\"");
        parse_t parse;
        parse.type = PARSE_TYPE_ONE;
        kv_init(parse.p.one.defines);
        parse_push(state, parse);
        state->type = PARSE_TYPE_DEFINE;
    }
    else if (strcmp(state->tok.s, "all") == 0)
    {
        if (
            state->stack.n > 0 &&
            parse_get(state, 1)->type == PARSE_TYPE_ALL
        )
            print_parse_error_and_exit(state, 1,
                "\"val\" or \"one\" command inside \"all\"");
        parse_t parse;
        parse.type = PARSE_TYPE_ALL;
        kv_init(parse.p.all.defines);
        parse_push(state, parse);
        state->type = PARSE_TYPE_DEFINE;
    }
    else if (strcmp(state->tok.s, "val") == 0)
    {
        state->val.name.str = NULL;
        state->type = PARSE_TYPE_VAL;
    }
    else
        print_parse_error_and_exit(state, 1,
            "\"one\", \"all\", or \"val\"");
}

void parse_end_command_name_tok(parse_state_t *state)
{
    state->cmd = 0;

    switch (state->type)
    {
        case PARSE_TYPE_TOP:
            parse_program_command_tok(state);
            break;

        case PARSE_TYPE_PROGRAM:
        case PARSE_TYPE_DEFINE:
            parse_define_command_tok(state);
            break;

        default:
            break;
    }

    free(state->tok.s);
}

void parse_end_define_name_tok(parse_state_t *state)
{
    hashed_string_t name = make_hashed_string(state->tok);

    switch (state->type)
    {
        case PARSE_TYPE_DEFINE:
        {
            define_t define = { DEFINE_TYPE_LIT, .d.lit = name };
            parse_t *parse = parse_get(state, 1);
            defines_t *defines;
            switch (parse->type)
            {
                case PARSE_TYPE_ONE:
                    defines = &parse->p.one.defines;
                    break;

                case PARSE_TYPE_ALL:
                    defines = &parse->p.all.defines;
                    break;
            }
            if (
                defines->n &&
                defines->a[defines->n - 1].type != DEFINE_TYPE_LIT
            )
                print_parse_error_and_exit(state, 1,
                    "define names before sub commands");
            kv_push(define_t, *defines, define);
            break;
        }

        case PARSE_TYPE_VAL:
            state->val.name = name;
            break;

        default:
            break;
    }
}

void parse_end_define_val_tok(parse_state_t *state)
{
    hashed_string_t val = make_hashed_string(state->tok);
    parse_val_push(state, val);
}

void parse_end_file_name_tok(parse_state_t *state)
{
    hashed_string_t name = make_hashed_string(state->tok);
    khiter_t i = kh_get(programs, state->programs, name);
    if (i != kh_end(state->programs))
        print_parse_error_and_exit(state, 1,
            "unique file name for shader source");
    parse_file_name_push(state, name);
}

void parse_end_tok(parse_state_t *state)
{
    if (state->tok.l == 0)
        return;

    switch (state->tok_type)
    {
        case PARSE_TYPE_COMMAND_NAME:
            parse_end_command_name_tok(state);
            break;

        case PARSE_TYPE_DEFINE_NAME:
            parse_end_define_name_tok(state);
            break;

        case PARSE_TYPE_DEFINE_VAL:
            parse_end_define_val_tok(state);
            break;

        case PARSE_TYPE_FILE_NAME:
            parse_end_file_name_tok(state);
            break;
    }

    state->tok = kstring_init;
}

void parse_begin_val_tok(parse_state_t *state)
{
    if (state->val.name.str)
        state->tok_type = PARSE_TYPE_DEFINE_VAL;
    else
        state->tok_type = PARSE_TYPE_DEFINE_NAME;
}

void parse_begin_one_tok(parse_state_t *state)
{
    state->tok_type = PARSE_TYPE_DEFINE_NAME;
}

void parse_begin_all_tok(parse_state_t *state)
{
    state->tok_type = PARSE_TYPE_DEFINE_NAME;
}

void parse_begin_program_tok(parse_state_t *state)
{
    if (state->program.define.type == DEFINE_TYPE_LIT)
        state->tok_type = PARSE_TYPE_FILE_NAME;
    else
        print_parse_error_and_exit(state, 0,
            "')'");
}

void parse_begin_command_tok(parse_state_t *state)
{
    state->tok_type = PARSE_TYPE_COMMAND_NAME;
}

void parse_begin_tok(parse_state_t *state)
{
    if (state->cmd)
    {
        parse_begin_command_tok(state);
        return;
    }

    switch (state->type)
    {
        case PARSE_TYPE_TOP:
            print_parse_error_and_exit(state, 0, "'('");
            break;

        case PARSE_TYPE_PROGRAM:
            parse_begin_program_tok(state);
            break;

        case PARSE_TYPE_DEFINE:
            switch (parse_get(state, 1)->type)
            {
                case PARSE_TYPE_ONE:
                    parse_begin_one_tok(state);
                    break;

                case PARSE_TYPE_ALL:
                    parse_begin_all_tok(state);
                    break;
            }
            break;

        case PARSE_TYPE_VAL:
            parse_begin_val_tok(state);
            break;
    }
}

void parse_c_valid_tok(parse_state_t *state)
{
    kputc(state->c, &state->tok);
}

void parse_c_command_name_tok(parse_state_t *state)
{
    if('a' <= state->c && state->c <= 'z')
        parse_c_valid_tok(state);
    else
        print_parse_error_and_exit(state, 0, "command name");
}

void parse_c_define_name_tok(parse_state_t *state)
{
    if(
        '_' == state->c ||
        ('a' <= state->c && state->c <= 'z') ||
        ('A' <= state->c && state->c <= 'Z') ||
        ('0' <= state->c && state->c <= '9' && state->tok.l > 0)
    )
        parse_c_valid_tok(state);
    else
        print_parse_error_and_exit(state, 0,
            "identifier");
}

void parse_c_define_val_tok(parse_state_t *state)
{
    if ('0' <= state->c && state->c <= '9')
        parse_c_valid_tok(state);
    else
        print_parse_error_and_exit(state, 0,
            "integer");
}

void parse_c_file_name_tok(parse_state_t *state)
{
    if(
        '_' == state->c ||
        '.' == state->c ||
        ('a' <= state->c && state->c <= 'z') ||
        ('A' <= state->c && state->c <= 'Z') ||
        ('0' <= state->c && state->c <= '9')
    )
        parse_c_valid_tok(state);
    else
        print_parse_error_and_exit(state, 0,
            "alphanumerics, dots, and underscores");
}

void parse_c_tok(parse_state_t *state)
{
    if (state->tok.l == 0)
        parse_begin_tok(state);

    switch (state->tok_type)
    {
        case PARSE_TYPE_COMMAND_NAME:
            parse_c_command_name_tok(state);
            break;

        case PARSE_TYPE_DEFINE_NAME:
            parse_c_define_name_tok(state);
            break;

        case PARSE_TYPE_DEFINE_VAL:
            parse_c_define_val_tok(state);
            break;

        case PARSE_TYPE_FILE_NAME:
            parse_c_file_name_tok(state);
            break;
    }
}

void parse_begin_command(parse_state_t *state)
{
    if (state->cmd)
        print_parse_error_and_exit(state, 0, "command name");

    if (state->type == PARSE_TYPE_VAL)
        print_parse_error_and_exit(state, 0, "integer");

    if (state->type == PARSE_TYPE_PROGRAM)
    {
        if (state->program_file_names.n == 0)
            print_parse_error_and_exit(state, 0, "file name");
        else if (state->program.define.type != DEFINE_TYPE_LIT)
            print_parse_error_and_exit(state, 0, "')'");
    }

    state->cmd = 1;
}

void parse_end_command(parse_state_t *state)
{
    if (state->cmd)
    {
        parse_t *parse = parse_get(state, 1);
        if (parse->type != PARSE_TYPE_ONE || parse->p.one.defines.n > 0)
            print_parse_error_and_exit(state, 0 , "command name");

        define_t define;
        define.type = DEFINE_TYPE_LIT;
        define.d.lit = empty_hashed_string;
        kv_push(define_t, parse->p.one.defines, define);

        state->cmd = 0;

        return;
    }

    switch (state->type)
    {
        case PARSE_TYPE_TOP:
            print_parse_error_and_exit(state, 0, "'('");
            break;

        case PARSE_TYPE_PROGRAM:
            if (state->program_file_names.n < 1)
                print_parse_error_and_exit(state, 0,
                    "file name for program");
            for (size_t i = 0; i < state->program_file_names.n; ++i)
                parse_program_push(state,
                    state->program_file_names.a[i], state->program);
            state->program_file_names.n = 0;
            state->type = PARSE_TYPE_TOP;
            break;

        case PARSE_TYPE_DEFINE:
        {
            parse_t *parse = parse_pop(state);
            define_t define;
            switch (parse->type)
            {
                case PARSE_TYPE_ONE:
                    if (parse->p.one.defines.n < 2)
                        print_parse_error_and_exit(state, 0,
                            "more than one option for \"one\"");
                    define.type = DEFINE_TYPE_ONE;
                    define.d.one = parse->p.one;
                    break;

                case PARSE_TYPE_ALL:
                    if (parse->p.all.defines.n < 2)
                        print_parse_error_and_exit(state, 0,
                            "more than one option for \"all\"");
                    define.type = DEFINE_TYPE_ALL;
                    define.d.all = parse->p.all;
                    break;
            }

            parse_define_push(state, define);
        }
            break;

        case PARSE_TYPE_VAL:
        {
            define_t define;
            if (state->val.name.str == NULL)
                print_parse_error_and_exit(state, 0,
                    "define name for \"val\"");
            if (state->val.vals.n < 1)
                print_parse_error_and_exit(state, 0,
                    "one or more values for \"val\"");
            define.type = DEFINE_TYPE_VAL;
            define.d.val = state->val;
            kv_init(state->val.vals);

            parse_define_push(state, define);
        }
            break;
    }
}

void parse_eof(parse_state_t *state)
{
    if (state->type != PARSE_TYPE_TOP)
        print_parse_error_and_exit(state, 0, "valid parameters");
}

int parse_c(parse_state_t *state, FILE *file)
{
    switch (state->c = fgetc(file))
    {
        case ' ':
        case '\t':
        case '\n':
            parse_end_tok(state);
            break;

        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
        case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
        case 'o': case 'p': case 'q': case 'r': case 's': case 't':
        case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
        case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
        case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T':
        case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
        case '_':
        case '.':
            parse_c_tok(state);
            break;

        case '(':
            parse_end_tok(state);
            parse_begin_command(state);
            break;

        case ')':
            parse_end_tok(state);
            parse_end_command(state);
            break;

        case EOF:
            parse_eof(state);
            return 1;
            break;

        default:
            print_parse_error_and_exit(state, 0,
                "round brackets, alphanumerics, dots, and underscores");
            break;
    }

    if (state->c == '\n')
    {
        ++state->line;
        state->col = 1;
    }
    else
        ++state->col;

    return 0;
}

void parse_state_init(parse_state_t *state)
{
    kv_init(state->stack);

    state->type = PARSE_TYPE_TOP;
    state->programs = kh_init(programs);
    kv_init(state->program_file_names);
    kv_init(state->val.vals);

    state->line = 1;
    state->col = 1;
    state->tok = kstring_init;
    state->cmd = 0;
}

programs_t parse_state_destroy(parse_state_t *state)
{
    free(state->tok.s);
    kv_destroy(state->val.vals);
    kv_destroy(state->program_file_names);
    kv_destroy(state->stack);
    return state->programs;
}

programs_t parse_input_conf_file(FILE *file)
{
    parse_state_t state;
    parse_state_init(&state);
    while (parse_c(&state, file) == 0) {}
    return parse_state_destroy(&state);
}

programs_t parse_input_conf(const char *file_name)
{
    FILE *file = fopen(file_name, "r");
    if (!file)
    {
        fputs("Error opening conf file \"", stderr);
        fputs(file_name, stderr);
        fputs("\"\n", stderr);
        exit(1);
    }

    fputs("Reading conf file \"", stdout);
    fputs(file_name, stdout);
    fputs("\"\n", stdout);
    programs_t programs = parse_input_conf_file(file);

    fclose(file);
    return programs;
}

void print_define(define_t define)
{
    switch (define.type)
    {
        case DEFINE_TYPE_LIT:
            if (*define.d.lit.str == 0)
                fputs("()", stdout);
            else
                fputs(define.d.lit.str, stdout);
            break;

        case DEFINE_TYPE_VAL:
            fputs("(val ", stdout);
            fputs(define.d.val.name.str, stdout);
            for (size_t i = 0; i < define.d.val.vals.n; ++i)
            {
                fputc(' ', stdout);
                fputs(define.d.val.vals.a[i].str, stdout);
            }
            fputc(')', stdout);
            break;

        case DEFINE_TYPE_ONE:
            fputs("(one", stdout);
            for (size_t i = 0; i < define.d.one.defines.n; ++i)
            {
                fputc(' ', stdout);
                print_define(define.d.one.defines.a[i]);
            }
            fputs(")", stdout);
            break;

        case DEFINE_TYPE_ALL:
            fputs("(all", stdout);
            for (size_t i = 0; i < define.d.all.defines.n; ++i)
            {
                fputc(' ', stdout);
                print_define(define.d.all.defines.a[i]);
            }
            fputs(")", stdout);
            break;
    }
}

void print_program(hashed_string_t name, program_t program)
{
    fputc('(', stdout);
    switch (program.type)
    {
        case PROGRAM_TYPE_VERTEX:
            fputs("vertex", stdout);
            break;

        case PROGRAM_TYPE_FRAGMENT:
            fputs("fragment", stdout);
            break;

        case PROGRAM_TYPE_COMPUTE:
            fputs("compute", stdout);
            break;
    }
    fputc(' ', stdout);
    fputs(name.str, stdout);
    if (program.define.type != DEFINE_TYPE_LIT)
    {
        fputc(' ', stdout);
        print_define(program.define);
    }
    fputs(")\n", stdout);
}

void print_named_program(named_program_t program)
{
    print_program(program.name, program.program);
}

void print_named_programs(named_programs_t programs)
{
    for (size_t i = 0; i != programs.n; ++i)
        print_named_program(programs.a[i]);
}

int hashed_string_compare(hashed_string_t s1, hashed_string_t s2)
{
    return strcmp(s1.str, s2.str);
}

int named_program_compare(const void *p1, const void *p2)
{
    return hashed_string_compare(
        ((const named_program_t *)p1)->name,
        ((const named_program_t *)p2)->name);
}

named_programs_t sort_programs(programs_t programs)
{
    named_programs_t named;
    kv_init(named);
    for (
        khiter_t i = kh_begin(programs);
        i != kh_end(programs);
        ++i
    )
        if (kh_exist(programs, i))
        {
            named_program_t program;
            program.name = kh_key(programs, i);
            program.program = kh_value(programs, i);
            kv_push(named_program_t, named, program);
        }

    qsort(named.a, named.n, sizeof(named_program_t),
        named_program_compare);
    return named;
}

void parsed_defines_list_append_and_destroy(
    parsed_defines_list_t *l1, parsed_defines_list_t *l2)
{
    size_t n1 = l1->n;
    size_t n2 = l2->n;
    size_t n = n1 + n2;
    kv_resize(parsed_defines_t, *l1, n);
    memcpy(l1->a + n1, l2->a, n2 * sizeof(parsed_defines_t));
    l1->n = n;

    kv_destroy(*l2);
}

void parsed_defines_append(
    parsed_defines_t *l1, const parsed_defines_t *l2)
{
    size_t n1 = l1->n;
    size_t n2 = l2->n;
    size_t n = n1 + n2;
    kv_resize(parsed_define_t, *l1, n);
    memcpy(l1->a + n1, l2->a, n2 * sizeof(parsed_define_t));
    l1->n = n;
}

void parsed_defines_list_combine_and_destroy(
    parsed_defines_list_t *l1, parsed_defines_list_t *l2)
{
    if (l1->n == 0)
    {
        *l1 = *l2;
        return;
    }

    parsed_defines_list_t l;
    kv_init(l);

    for (size_t i = 0; i < l1->n; ++i)
    {
        for (size_t j = 0; j < l2->n; ++j)
        {
            parsed_defines_t d;
            kv_init(d);
            kv_copy(parsed_define_t, d, l1->a[i]);
            parsed_defines_append(&d, &l2->a[j]);
            kv_push(parsed_defines_t, l, d);
        }
        kv_destroy(l1->a[i]);
    }
    kv_destroy(*l1);

    for (size_t j = 0; j < l2->n; ++j)
        kv_destroy(l2->a[j]);
    kv_destroy(*l2);

    *l1 = l;
}

parsed_defines_list_t parse_defines_list(define_t define)
{
    switch (define.type)
    {
        case DEFINE_TYPE_LIT:
        {
            parsed_defines_list_t parsed_defines_list;
            kv_init(parsed_defines_list);
            parsed_defines_t parsed_defines;
            kv_init(parsed_defines);
            parsed_define_t parsed_define;
            parsed_define.name = define.d.lit;
            parsed_define.val = empty_hashed_string;
            kv_push(parsed_define_t,
                parsed_defines, parsed_define);
            kv_push(parsed_defines_t,
                parsed_defines_list, parsed_defines);
            return parsed_defines_list;
        }

        case DEFINE_TYPE_VAL:
        {
            parsed_defines_list_t parsed_defines_list;
            kv_init(parsed_defines_list);
            for (size_t i = 0; i < define.d.val.vals.n; ++i)
            {
                parsed_defines_t parsed_defines;
                kv_init(parsed_defines);

                parsed_define_t parsed_define;
                parsed_define.name = define.d.val.name;
                parsed_define.val = define.d.val.vals.a[i];

                kv_push(parsed_define_t,
                    parsed_defines, parsed_define);
                kv_push(parsed_defines_t,
                    parsed_defines_list, parsed_defines);
            }
            return parsed_defines_list;
        }

        case DEFINE_TYPE_ONE:
        {
            parsed_defines_list_t l1;
            kv_init(l1);
            for (size_t i = 0; i < define.d.one.defines.n; ++i)
            {
                parsed_defines_list_t l2 = parse_defines_list(
                    define.d.one.defines.a[i]
                );
                parsed_defines_list_append_and_destroy(&l1, &l2);
            }
            return l1;
        }

        case DEFINE_TYPE_ALL:
        {
            parsed_defines_list_t l1;
            kv_init(l1);
            for (size_t i = 0; i < define.d.one.defines.n; ++i)
            {
                parsed_defines_list_t l2 = parse_defines_list(
                    define.d.one.defines.a[i]
                );
                parsed_defines_list_combine_and_destroy(&l1, &l2);
            }
            return l1;
        }

        default:
        {
            parsed_defines_list_t l;
            kv_init(l);
            return l;
        }
    }
}

KHASH_INIT(defines_once, hashed_string_t, hashed_string_t, 1,
    hashed_string_hash, hashed_string_equal)
typedef khash_t(defines_once) *defines_once_t;

int parsed_define_compare(const void *p1, const void *p2)
{
    const parsed_define_t *d1 = (const parsed_define_t *)p1;
    const parsed_define_t *d2 = (const parsed_define_t *)p2;
    int r = strcmp(d1->name.str, d2->name.str);
    if (r)
        return r;

    int v1 = atoi(d1->val.str);
    int v2 = atoi(d2->val.str);
    return v1 < v2 ? -1 : v1 > v2 ? 1 : 0;
}

void sort_defines(parsed_defines_t *defines)
{
    defines_once_t defines_once = kh_init(defines_once);
    for (size_t i = 0; i < defines->n; ++i)
    {
        hashed_string_t name = defines->a[i].name;
        if (*name.str == 0)
            continue;
        hashed_string_t val = defines->a[i].val;
        int r;
        khiter_t j = kh_put(defines_once,
            defines_once, name, &r);
        hashed_string_t *value = &kh_value(defines_once, j);
        if (r == 0)
        {
            if (*name.str)
                free(name.str);
            if (*value->str)
                free(value->str);
        }
        *value = val;
    }

    kv_destroy(*defines);
    kv_init(*defines);
    for (
        khiter_t i = kh_begin(defines_once);
        i != kh_end(defines_once);
        ++i
    )
        if (kh_exist(defines_once, i))
        {
            parsed_define_t define = {
                kh_key(defines_once, i),
                kh_value(defines_once, i),
            };
            kv_push(parsed_define_t, *defines, define);
        }
    kh_destroy(defines_once, defines_once);

    qsort(defines->a, defines->n, sizeof(parsed_define_t),
        parsed_define_compare);
}

int parsed_defines_compare(const void *p1, const void *p2)
{
    const parsed_defines_t *l1 = (const parsed_defines_t *)p1;
    const parsed_defines_t *l2 = (const parsed_defines_t *)p2;
    size_t n = l1->n < l2->n ? l1->n : l2->n;
    for (size_t i = 0; i < n; ++i)
    {
        int r = parsed_define_compare(l1->a + i, l2->a + i);
        if (r)
            return r;
    }
    return l1->n < l2->n ? -1 : l1->n > l2->n ? 1 : 0;
}

void sort_defines_list(parsed_defines_list_t *defines_list)
{
    for (size_t i = 0; i < defines_list->n; ++i)
        sort_defines(&defines_list->a[i]);

    qsort(defines_list->a, defines_list->n, sizeof(parsed_defines_t),
        parsed_defines_compare);
}

parsed_programs_t parse_programs(named_programs_t programs)
{
    parsed_programs_t parsed;
    kv_init(parsed.defines_lists);
    kv_init(parsed.programs);

    programs_defines_t programs_defines =
        kh_init(programs_defines);

    for (size_t i = 0; i < programs.n; ++i)
    {
        named_program_t named = programs.a[i];
        parsed_program_t program;

        khiter_t j = kh_get(programs_defines,
            programs_defines, named.program);
        if (j != kh_end(programs_defines))
        {
            program.type = named.program.type;
            program.name = named.name;
            program.defines_list_i = kh_value(programs_defines, j);
        }
        else
        {
            parsed_defines_list_t defines_list =
                parse_defines_list(named.program.define);
            sort_defines_list(&defines_list);
            parsed_program_t program;
            program.type = named.program.type;
            program.name = named.name;
            program.defines_list_i = parsed.defines_lists.n;
            kv_push(parsed_defines_list_t,
                parsed.defines_lists, defines_list);

            int r;
            j = kh_put(programs_defines,
                programs_defines, named.program, &r);
            kh_value(programs_defines, j) = program.defines_list_i;
        }

        kv_push(parsed_program_t,
            parsed.programs, program);
    }

    kh_destroy(programs_defines, programs_defines);
    return parsed;
}

void print_parsed_program_i(hashed_string_t name,
    parsed_defines_list_t defines_list)
{
    for (size_t i = 0; i < defines_list.n; ++i)
    {
        fputc('(', stdout);
        fputs(name.str, stdout);
        parsed_defines_t defines = defines_list.a[i];
        for (size_t j = 0; j < defines.n; ++j)
        {
            fputc(' ', stdout);
            const char *name = defines.a[j].name.str;
            if (*name)
            {
                fputs(name, stdout);
                const char *val = defines.a[j].val.str;
                if (*val)
                {
                    fputc('=', stdout);
                    fputs(val, stdout);
                }
            }
            else
                fputs("()", stdout);
        }
        fputs(")\n", stdout);
    }
}

void print_parsed_program(parsed_program_t program,
    parsed_defines_lists_t defines_lists)
{
    print_parsed_program_i(
        program.name,
        defines_lists.a[program.defines_list_i]);
}

void print_parsed_programs(parsed_programs_t programs)
{
    for (size_t i = 0; i < programs.programs.n; ++i)
        print_parsed_program(programs.programs.a[i],
            programs.defines_lists);
}

void output_program_defines_file(FILE *file,
    parsed_defines_t defines, hashed_string_t name)
{
    fputs(name.str, file);
    for (size_t i = 0; i < defines.n; ++i)
    {
        parsed_define_t define = defines.a[i];
        fputc('.', file);
        fputs(define.name.str, file);
        if (*define.val.str)
        {
            fputc('.', file);
            fputs(define.val.str, file);
        }
    }
    fputs(".inc", file);
}

void output_program_prereqs_file(FILE *file,
    parsed_defines_lists_t lists, parsed_program_t program)
{
    parsed_defines_list_t list = lists.a[program.defines_list_i];
    for (size_t i = 0; i < list.n; ++i)
    {
        fputs(" \\\n\t", file);
        output_program_defines_file(file, list.a[i], program.name);
    }
}

void output_defines_recipe_file(FILE *file,
    parsed_defines_t defines)
{
    for (size_t i = 0; i < defines.n; ++i)
    {
        parsed_define_t define = defines.a[i];
        fputs(" -D", file);
        fputs(define.name.str, file);
        if (*define.val.str)
        {
            fputc('=', file);
            fputs(define.val.str, file);
        }
    }
}

void output_program_recipe_file(FILE *file,
    parsed_defines_lists_t lists, parsed_program_t program)
{
    parsed_defines_list_t list = lists.a[program.defines_list_i];
    for (size_t i = 0; i < list.n; ++i)
    {
        fputs("\n\n", file);
        output_program_defines_file(file, list.a[i], program.name);
        fputs(": ", file);
        fputs("$(IN_DIR)/", file);
        fputs(program.name.str, file);
        fputs("\n\t$(GLSLC) -o $@ $< -mfmt=c -MD", file);
        output_defines_recipe_file(file, list.a[i]);
    }
}

void output_program_dep_file(FILE *file,
    parsed_defines_lists_t lists, parsed_program_t program)
{
    parsed_defines_list_t list = lists.a[program.defines_list_i];
    for (size_t i = 0; i < list.n; ++i)
    {
        fputs(" \\\n\t", file);
        output_program_defines_file(file, list.a[i], program.name);
        fputs(".d", file);
    }
}

void output_make_file(FILE *file, parsed_programs_t programs)
{
    fputs(
        "# Automatically generated by rebuild_shaders\n\n"
        "IN_DIR = ", file);
    fputs(in_dir_to_make, file);
    fputs(
        "\n\nGLSLC = glslc\n\n.PHONY: all\n\n"
        "all:", file);
    for (size_t i = 0; i < programs.programs.n; ++i)
        output_program_prereqs_file(file,
            programs.defines_lists, programs.programs.a[i]);
    for (size_t i = 0; i < programs.programs.n; ++i)
        output_program_recipe_file(file,
            programs.defines_lists, programs.programs.a[i]);
    fputs("\n\n-include", file);
    for (size_t i = 0; i < programs.programs.n; ++i)
        output_program_dep_file(file,
            programs.defines_lists, programs.programs.a[i]);
    fputs("\n", file);
}

void output_make(const char *file_name, parsed_programs_t programs)
{
    FILE *file = fopen(file_name, "w");
    if (!file)
    {
        fputs("Error opening make file \"", stderr);
        fputs(file_name, stderr);
        fputs("\"\n", stderr);
        exit(1);
    }

    fputs("Writing make file \"", stdout);
    fputs(file_name, stdout);
    fputs("\"\n", stdout);

    output_make_file(file, programs);

    fclose(file);
}

void output_hpp(const char *file_name, parsed_programs_t programs)
{
    (void)file_name;
    (void)programs;
}

void parse_programs_destroy(parsed_programs_t parsed)
{
    for (size_t i = 0; i < parsed.defines_lists.n; ++i)
    {
        parsed_defines_list_t l = parsed.defines_lists.a[i];
        for (size_t j = 0; j < l.n; ++j)
            kv_destroy(l.a[j]);
        kv_destroy(l);
    }
    kv_destroy(parsed.defines_lists);
    kv_destroy(parsed.programs);
}

void sort_programs_destroy(named_programs_t named)
{
    kv_destroy(named);
}

void define_name_destroy(hashed_string_t name)
{
    if (*name.str)
        free(name.str);
}

void define_val_destroy(hashed_string_t val)
{
    free(val.str);
}

void define_destroy(define_t define)
{
    switch (define.type)
    {
        case DEFINE_TYPE_LIT:
            define_name_destroy(define.d.lit);
            break;

        case DEFINE_TYPE_VAL:
            define_name_destroy(define.d.val.name);
            for (size_t i = 0; i < define.d.val.vals.n; ++i)
                define_val_destroy(define.d.val.vals.a[i]);
            kv_destroy(define.d.val.vals);
            break;

        case DEFINE_TYPE_ONE:
            for (size_t i = 0; i < define.d.one.defines.n; ++i)
                define_destroy(define.d.one.defines.a[i]);
            kv_destroy(define.d.one.defines);
            break;

        case DEFINE_TYPE_ALL:
            for (size_t i = 0; i < define.d.all.defines.n; ++i)
                define_destroy(define.d.all.defines.a[i]);
            kv_destroy(define.d.all.defines);
            break;
    }
}

void program_destroy(program_t program)
{
    define_destroy(program.define);
}

void parse_destroy(programs_t programs)
{
    programs_defines_t programs_defines =
        kh_init(programs_defines);

    for (
        khiter_t i = kh_begin(programs);
        i != kh_end(programs);
        ++i
    )
        if (kh_exist(programs, i))
        {
            free(kh_key(programs, i).str);
            int r;
            kh_put(programs_defines, programs_defines,
                kh_value(programs, i), &r);
        }

    for (
        khiter_t i = kh_begin(programs_defines);
        i != kh_end(programs_defines);
        ++i
    )
        if (kh_exist(programs_defines, i))
            program_destroy(kh_key(programs_defines, i));

    kh_destroy(programs_defines, programs_defines);
    kh_destroy(programs, programs);
}

uint64_t llrand() {
    uint64_t r = 0;
    for (int i = 0; i < 5; ++i)
        r = (r << 15) | (rand() & 0x7FFF);
    return r;
}

int main(int argc, char **argv)
{
    int verbose = 0;
    if (argc == 6)
    {
        if (strcmp(argv[1], "-v") == 0)
            verbose = 1;
        else if (strcmp(argv[1], "-vv") == 0)
            verbose = 2;
        else
            print_usage_and_exit();

        --argc;
        ++argv;
    }

    if (argc != 5)
        print_usage_and_exit();
    char *conf_file_name = argv[1];
    char *make_file_name = argv[2];
    char *hpp_file_name = argv[3];
    in_dir_to_make = argv[4];

    srand(time(0));
    seed = llrand();
    kstring_t empty_kstring = { 0, .s = "" };
    empty_hashed_string = make_hashed_string(empty_kstring);

    programs_t programs = parse_input_conf(conf_file_name);
    named_programs_t sorted = sort_programs(programs);
    if (verbose >= 1)
        print_named_programs(sorted);

    parsed_programs_t parsed = parse_programs(sorted);
    if (verbose >= 2)
        print_parsed_programs(parsed);

    output_make(make_file_name, parsed);
    output_hpp(hpp_file_name, parsed);

    // Cleaning up
    parse_programs_destroy(parsed);
    sort_programs_destroy(sorted);
    parse_destroy(programs);

    return 0;
}
