//  crm_main.c  - Controllable Regex Mutilator,  version v1.0
//  Copyright 2001-2009  William S. Yerazunis, all rights reserved.
//
//  This software is licensed to the public under the Free Software
//  Foundation's GNU GPL, version 2.  You may obtain a copy of the
//  GPL by visiting the Free Software Foundations web site at
//  www.fsf.org, and a copy is included in this distribution.
//
//  Other licenses may be negotiated; contact the
//  author for details.
//
//  include some standard files
#include "crm114_sysincludes.h"

//  include any local crm114 configuration file
#include "crm114_config.h"

//  include the crm114 data structures file
#include "crm114_structs.h"

//  include the routine declarations file
#include "crm114.h"

//  and include OSBF declarations
#include "crm114_osbf.h"

//
//    Global variables



/* [i_a] no variable instantiation in a common header file */
int vht_size = 0;

int cstk_limit = 0;

int max_pgmlines = 0;

int max_pgmsize = 0;

int user_trace = 0;

int internal_trace = 0;

int debug_countdown = 0;

int cmdline_break = 0;

int cycle_counter = 0;

int ignore_environment_vars = 0;

int data_window_size = 0;

int sparse_spectrum_file_length = 0;

int microgroom_chain_length = 0;

int microgroom_stop_after = 0;

double min_pmax_pmin_ratio = 0.0;

int profile_execution = 0;

int prettyprint_listing = 0;  //  0= none, 1 = basic, 2 = expanded, 3 = parsecode

int engine_exit_base = 0;  //  All internal errors will use this number or higher;
//  the user programs can use lower numbers freely.


//        how should math be handled?
//        = 0 no extended (non-EVAL) math, use algebraic notation
//        = 1 no extended (non-EVAL) math, use RPN
//        = 2 extended (everywhere) math, use algebraic notation
//        = 3 extended (everywhere) math, use RPN
int q_expansion_mode = 0;

int selected_hashfunction = 0;  //  0 = default

int act_like_Bill = 0;





//   The VHT (Variable Hash Table)
VHT_CELL **vht = NULL;

//   The pointer to the global Current Stack Level (CSL) frame
CSL_CELL *csl = NULL;

//    the data window
CSL_CELL *cdw = NULL;

//    the temporarys data window (where argv, environ, newline etc. live)
CSL_CELL *tdw = NULL;

//    the pointer to a CSL that we use during matching.  This is flipped
//    to point to the right data window during matching.  It doesn't have
//    it's own data, unlike cdw and tdw.
CSL_CELL *mdw = NULL;

////    a pointer to the current statement argparse block.  This gets whacked
////    on every new statement.
//ARGPARSE_BLOCK *apb = NULL;




//    the app path/name
char *prog_argv0 = NULL;

//    the auxilliary input buffer (for WINDOW input)
char *newinputbuf = NULL;

//    the globals used when we need a big buffer  - allocated once, used
//    wherever needed.  These are sized to the same size as the data window.
char *inbuf = NULL;
char *outbuf = NULL;
char *tempbuf = NULL;


#if !defined(CRM_WITHOUT_BMP_ASSISTED_ANALYSIS)
CRM_ANALYSIS_PROFILE_CONFIG analysis_cfg = { 0 };
#endif /* CRM_WITHOUT_BMP_ASSISTED_ANALYSIS */




void free_arg_parseblock(ARGPARSE_BLOCK *apb)
{
    if (!apb)
        return;

    free(apb);
}

void free_stack_item(CSL_CELL *csl)
{
    if (!csl)
        return;

    mark_vars_as_out_of_scope(csl);

    if (csl->vht_var_collection)
    {
        free(csl->vht_var_collection);
    }
    csl->vht_var_collection = NULL;

    if (csl->mct && csl->mct_allocated)
    {
        int i;

        for (i = 0; i < csl->mct_size; i++)
        {
            MCT_CELL *cp = csl->mct[i];

            if (cp != NULL)
            {
#if !FULL_PARSE_AT_COMPILE_TIME
                free(cp->apb);
                cp->apb = NULL;
#endif
                // free(cp->hosttxt);
                free(cp);
                csl->mct[i] = NULL;
            }
        }
        free(csl->mct);
        csl->mct = NULL;
    }

    if (csl->filename_allocated)
    {
        free(csl->filename);
    }
    csl->filename = NULL;
    if (csl->filetext_allocated)
    {
        free(csl->filetext);
    }
    csl->filetext = NULL;

    free(csl);
}



void free_stack(CSL_CELL *csl)
{
    CSL_CELL *caller;

    for (; csl != NULL; csl = caller)
    {
        caller = csl->caller;

        free_stack_item(csl);
    }
}



/*
 * memory cleanup routine which is called at the end of the crm114 run.
 *
 * Note: this routine *also* called when an error occurred (e.g. out of memory)
 *    so tread carefully here: do not assume all these pointers are filled.
 */
static void crm_final_cleanup(void)
{
    // GROT GROT GROT
    //
    // move every malloc/free to use xmalloc/xcalloc/xrealloc/xfree, so we can be sure
    // [x]free() will be able to cope with NULL pointers as it is.

    crm_munmap_all();

    if (internal_trace)
    {
        int index;
        int b;

        fprintf(stderr, "Variable Hash Table Dump @ scope depth %d\n", csl->calldepth);
        for (b = INT_MAX, index = 0; index < vht_size; index++)
        {
            if (vht[index] == NULL)
            {
                if (b > index)
                {
                    b = index;
                }
                continue;
            }
            if (index > 0 && vht[index - 1] == NULL)
            {
                fprintf(stderr, "empty slots in range %d .. %d\n", b, index);
                b = INT_MAX;
            }
            fprintf(stderr, "  var '");
            fwrite_ASCII_Cfied(stderr, vht[index]->nametxt + vht[index]->nstart, vht[index]->nlen);
            fprintf(stderr, "'[%d] found at %d (",
                    vht[index]->nlen,  index);
            if (vht[index]->valtxt == cdw->filetext)
            {
                fprintf(stderr, "(main)");
            }
            else
            {
                fprintf(stderr, "(isol)");
            }
            fprintf(stderr, " s: %d, l:%d,%s scope: %d)\n",
                    vht[index]->vstart, vht[index]->vlen,
                    (vht[index]->out_of_scope ? " (out-of-scope)" : ""),
                    vht[index]->scope_depth);
        }
    }

    free_stack(csl); // expects a live vht...
    csl = NULL;
    free_hash_table(vht, vht_size);
    vht = NULL;
    free_stack(cdw);
    cdw = NULL;
    free_stack(tdw);
    tdw = NULL;
    //free_stack(mdw);
    //mdw = NULL;
    //free_arg_parseblock(apb);
    //apb = NULL;

    free_regex_cache();
    cleanup_expandvar_allocations();

    free(newinputbuf);
    newinputbuf = NULL;
    free(inbuf);
    inbuf = NULL;
    free(outbuf);
    outbuf = NULL;
    free(tempbuf);
    tempbuf = NULL;

    free_debugger_data();

    crm_terminate_analysis(&analysis_cfg);

    cleanup_stdin_out_err_as_os_handles();
}

#if 0
char stdout_buf[65536];
char stderr_buf[65536];
#endif


struct write_spec_prop
{
    FILE *o;
    int pos;
};

static int write_spec_bit(const char *str, int len, void *propagator)
{
    struct write_spec_prop *p = (struct write_spec_prop *)propagator;
    int ret = 0;

    if (len == -1)
        len = strlen(str);

    if (len > 0)
    {
        // check if it still fits on this line or is end-of-line:
        if (str[0] == '\n')
        {
            // rude hackish code: 'knows' we send "\n" strings as opcode 'line terminators'.
            ret = fwrite(str, len, 1, p->o);
            if (ret < 0)
                return -1;

            p->pos = 0;
        }
        else if (p->pos + len >= 79)
        {
            // split or wrap?
            const char *ws2 = str - 1;
            const char *ws;

            do
            {
                ws = ws2;
                ws2 = memchr(ws2 + 1, ' ', len - (ws2 + 1 - str));
            }  while (ws2 && ws2 - str + p->pos < 79);

            if (ws)
            {
                ret = fwrite(str, ws + 1 - str, 1, p->o);
                if (ret < 0)
                    return -1;
            }
            else
            {
                ws = str - 1;
            }

            fwrite("\n        ", 9, 1, p->o);
            p->pos = 8;
            // indent!

            if (len - (ws + 1 - str) > 0)
            {
                ret = fwrite(ws + 1, len - (ws + 1 - str), 1, p->o);
                if (ret < 0)
                    return -1;

                p->pos += len - (ws + 1 - str);
            }
        }
        else
        {
            ret = fwrite(str, len, 1, p->o);
            if (ret < 0)
                return -1;

            p->pos += len;
        }
    }

    return 0;
}



int main(int argc, char **argv)
{
    int i;  //  some random counters, when we need a loop
    int status;
    int openbracket;            //  if there's a command-line program...
    int openparen = -1;         //  if there's a list of acceptable arguments
    //int user_cmd_line_vars = 0; // did the user specify --vars on cmdline?

    char *stdin_filename = "stdin (default)";
    char *stdout_filename = "stdout (default)";
    char *stderr_filename = "stderr (default)";

    char *profile_argset = NULL;

    int posc;
    char **posv;

    init_stdin_out_err_as_os_handles();
#if 0
    setvbuf(stdout, stdout_buf, _IOFBF, sizeof(stdout_buf));
    setvbuf(stderr, stderr_buf, _IOFBF, sizeof(stderr_buf));
#endif

#if (defined(WIN32) || defined(_WIN32) || defined(_WIN64) || defined(WIN64)) && defined(_DEBUG)
    /*
     * Hook in our client-defined reporting function.
     * Every time a _CrtDbgReport is called to generate
     * a debug report, our function will get called first.
     */
    _CrtSetReportHook(crm_dbg_report_function);

    /*
     * Define the report destination(s) for each type of report
     * we are going to generate.  In this case, we are going to
     * generate a report for every report type: _CRT_WARN,
     * _CRT_ERROR, and _CRT_ASSERT.
     * The destination(s) is defined by specifying the report mode(s)
     * and report file for each report type.
     */
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

    // Store a memory checkpoint in the s1 memory-state structure
    _CrtMemCheckpoint(&crm_memdbg_state_snapshot1);

    atexit(crm_report_mem_analysis);

    // Get the current bits
    i = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);

    // Set the debug-heap flag so that freed blocks are kept on the
    // linked list, to catch any inadvertent use of freed memory
#if 0
    i |= _CRTDBG_DELAY_FREE_MEM_DF;
#endif

    // Set the debug-heap flag so that memory leaks are reported when
    // the process terminates. Then, exit.
    //i |= _CRTDBG_LEAK_CHECK_DF;

    // Clear the upper 16 bits and OR in the desired freqency
    //i = (i & 0x0000FFFF) | _CRTDBG_CHECK_EVERY_16_DF;

    i |= _CRTDBG_CHECK_ALWAYS_DF;

    // Set the new bits
    _CrtSetDbgFlag(i);

//    // set a malloc marker we can use it in the leak dump at the end of the program:
//    (void)_calloc_dbg(1, 1, _CLIENT_BLOCK, __FILE__, __LINE__);
#endif

#if 0
    fprintf(stderr, " args: %d \n", argc);
    for (i = 0; i < argc; i++)
        fprintf(stderr, " argi: %d, argv: %s \n", i, argv[i]);
#endif

    atexit(crm_final_cleanup);

#if defined(HAVE__SET_OUTPUT_FORMAT)
    _set_output_format(_TWO_DIGIT_EXPONENT);     // force MSVC (& others?) to produce floating point %f with 2 digits for power component instead of 3 for easier comparison with 'knowngood'.
#endif

    // force MSwin/Win32 console I/O into binary mode: treat \r\n and \n as completely different - like it is on *NIX boxes!
#if defined(HAVE__SETMODE) && defined(HAVE__FILENO) && defined(O_BINARY)
    (void)_setmode(_fileno(crm_stdin), O_BINARY);
    (void)_setmode(_fileno(crm_stdout), O_BINARY);
    (void)_setmode(_fileno(crm_stderr), O_BINARY);
#endif

    //   copy program path/name into global static...
    prog_argv0 = argv[0];

    vht_size = DEFAULT_VHT_SIZE;
    cstk_limit = DEFAULT_CSTK_LIMIT;
    max_pgmlines = DEFAULT_MAX_PGMLINES;
    max_pgmsize = DEFAULT_MAX_PGMLINES * 128;
    data_window_size = DEFAULT_DATA_WINDOW;
    user_trace = DEFAULT_USER_TRACE_LEVEL;
    internal_trace = DEFAULT_INTERNAL_TRACE_LEVEL;
    sparse_spectrum_file_length = 0;
    microgroom_chain_length = 0;
    microgroom_stop_after = 0;
    min_pmax_pmin_ratio = OSBF_MIN_PMAX_PMIN_RATIO;
    ignore_environment_vars = 0;
    debug_countdown = DEBUGGER_DISABLED_FOREVER; /* [i_a] special signal: debugger disabled, unless 'debug' command is found. */
    cycle_counter = 0;
    cmdline_break = -1;
    profile_execution = 0;
    prettyprint_listing = 0;
    engine_exit_base = 0;
    q_expansion_mode = 0;
    selected_hashfunction = 0;
    act_like_Bill = 0;

    posv = (char **)calloc(argc, sizeof(posv[0]));
    if (!posv)
    {
        untrappableerror("Couldn't alloc the pos[].  Big problem!\n", "");
    }
    posv[0] = argv[0];
    posc = 1;

    //    allocate and initialize the initial root csl (control stack
    //    level) cell.  We do this first, before command-line parsing,
    //    because the command line parse fills in a lot of the first level csl.

    csl = (CSL_CELL *)calloc(1, sizeof(csl[0]));
    if (!csl)
    {
        untrappableerror("Couldn't alloc the csl.  Big problem!\n", "");
    }
    csl->filename = NULL;
    csl->filedes = -1;
    csl->rdwr = 0; //  0 means readonly, 1 means read/write
    csl->nchars = 0;
    csl->mct = 0;
    csl->cstmt = 0;
    csl->nstmts = 0;
    csl->preload_window = 1;
    csl->caller = NULL;
    csl->calldepth = 0;
    csl->aliusstk[0]  = 0; // this gets initted later.
#if defined(TOLERATE_FAIL_AND_OTHER_CASCADES)
    csl->cstmt_recall = 0;
    csl->next_stmt_due_to_fail = -1;
    csl->next_stmt_due_to_trap = -1;
    csl->next_stmt_due_to_jump = -1;
#endif
    csl->running = 0;

    openbracket = -1;
    openparen = -1;

//  //   and allocate the argparse block
//  apb = (ARGPARSE_BLOCK *) calloc (1, sizeof (apb[0]));
//  if (!apb)
//    untrappableerror ("Couldn't alloc apb.  This is very bad.\n","");

    //   Parse the input command arguments

    //  user_trace = 1;
    //internal_trace = 1;

#if 01
    profile_argset = getenv("CRM114_PROFILING");
    if (profile_argset && profile_argset[0])
    {
        i = crm_init_analysis(&analysis_cfg, profile_argset, -1);
        if (i)
        {
            untrappableerror_ex(SRC_LOC(), "Failed to init analysis code using env.var. CRM114_PROFILING settings: '%s': error code %d. Urck!",
                                profile_argset, i);
        }
    }
#endif


    for (i = 1; i < argc; i++)
    {
        // fprintf(stderr, "Arg %d = '%s' \n", i, argv[i]);
        //   is this a plea for help?
        if (
            (strncmp(argv[i], "-?", 2) == 0)
           || (strncmp(argv[i], "-h", 2) == 0)
           || (argc == 1))
        {
            fprintf(stderr, " CRM114 version %s, rev %s (regex engine: %s) (OS: %s)\n",
                    VERSION,
                    REVISION,
                    crm_regversion(),
                    HOSTTYPE);
            fprintf(stderr, " Copyright 2001-2009 William S. Yerazunis\n");
            fprintf(stderr, " This software is licensed under the GPL "
                            "with ABSOLUTELY NO WARRANTY\n");
            fprintf(stderr, "     For language help, RTFRM.\n");
            fprintf(stderr, "     Command Line Options:\n");
            fprintf(stderr, " -{statements}   executes statements\n");
            fprintf(stderr, " -A 'file markerset'\n");
            fprintf(stderr, "         turn on EXR-assisted analysis profiling. See the docs for more info\n");
            fprintf(stderr, " -b nn   sets a breakpoint on stmt nn\n");
            fprintf(stderr, " -d nn   run nn statements, then drop to debug\n");
            fprintf(stderr, " -e      ignore environment variables\n");
            fprintf(stderr, " -E nn   set base nn for engine exit values\n");
            fprintf(stderr, " -h      this help\n");
            fprintf(stderr, " -H n    select hash function N - handle this with the utmost care! (default=0)\n");
            fprintf(stderr, " -l n    listing (detail level 1 through 5)\n");
            fprintf(stderr, " -m nn   max number of microgroomed buckets in a chain\n");
            fprintf(stderr, " -M nn   max chain length - triggers microgrooming if enabled\n");
            fprintf(stderr, " -p      profile statement times\n");
            fprintf(stderr, " -P nn   max program lines @ 128 chars/line\n");
            fprintf(stderr, " -q m    mathmode (0,1 alg/RPN in EVAL,2,3 alg/RPN everywhere)\n");
            fprintf(stderr, " -r nn   set OSBF min pmax/pmin ratio (default=9)\n");
            fprintf(stderr, " -s nn   sparse spectra (.css) featureslots\n");
            fprintf(stderr, " -S nn   round up to 2^N+1 .css featureslots\n");
            fprintf(stderr, " -C      use env. locale (default POSIX)\n");
            fprintf(stderr, " -t      user trace mode on\n");
            fprintf(stderr, " -T      implementors trace mode on\n");
            fprintf(stderr, " -u dir  chdir to directory before starting\n");
            fprintf(stderr, " -v      print version ID and exit\n");
            fprintf(stderr, " -w nn   max data window size ( bytes )\n");
            fprintf(stderr, " --      end of CRM114 flags; start of user args\n");
            fprintf(stderr, " --foo   creates var :foo: with value 'SET'\n");
            fprintf(stderr, " --x=y   creates var :x: with value 'y'\n");
            fprintf(stderr, " -in file\n"
                            "         use file instead of stdin for input. Note that '-in' may\n"
                            "         specify the standard handle value '0' for stdin.\n");
            fprintf(stderr, " -out file\n"
                            "         use file instead of stdout for output\n");
            fprintf(stderr, " -err file\n"
                            "         use file instead of stderr for output. Note that '-out' may\n"
                            "         use the same file as '-err'. Note also that '-out' and '-err'\n"
                            "         may specify the standard handle values '1' for stdout and '2'\n"
                            "         for stderr. This implies that '-err 1' is essentially\n"
                            "         identical to the UNIX shell '2>&1' redirection.\n");
            fprintf(stderr, " -Bill   makes us act like BillY vanilla crm114\n");
            fprintf(stderr, " -RT     ensure tests are as reproducible across platforms as possible.\n"
                            "         One of the implications is that the random generator will be\n"
                            "         seeded with a fixed value so random values are deterministic.\n");
#ifndef CRM_DONT_ASSERT
            fprintf(stderr, " -Cdbg   direct developer support: trigger the C/IDE debugger when an\n"
                            "         internal error is hit.\n");
#endif
#if (defined(WIN32) || defined(_WIN32) || defined(_WIN64) || defined(WIN64)) && defined(_DEBUG)
            fprintf(stderr, " -memdump\n"
                            "         direct developer support: dump all detected memory leaks\n");
#endif

            if (openparen > 0)
            {
                fprintf(stderr, "\n This program also claims to accept these command line args:");
                fprintf(stderr, "\n  %s\n", &argv[openparen][1]);
            }
            if (engine_exit_base != 0)
            {
                exit(engine_exit_base + 14);
            }
            else
            {
                exit(EXIT_SUCCESS);
            }
        }

        //  -- means "end of crm114 flags" - remainder of args goes to
        //  the program alone.
        if (strcmp(argv[i], "--") == 0)
        {
            if (user_trace)
                fprintf(stderr, "system flag processing ended at arg %d .\n", i);
            break;
        }
        if (strncmp(argv[i], "--", 2) == 0 && strlen(argv[i]) > 2)
        {
            if (user_trace)
                fprintf(stderr, "Commandline set of user variable at %d '%s'.\n",
                        i, argv[i]);
            //if (user_cmd_line_vars == 0)
            //    user_cmd_line_vars = i;
            goto end_command_line_parse_loop;
        }
        //   set debug levels
        if (strcmp(argv[i], "-t") == 0)
        {
            if (user_trace == 0)
            {
                user_trace = 1;
                fprintf(stderr, "User tracing on\n");
            }
            else
            {
                user_trace = 0;
                fprintf(stderr, "User tracing off\n");
            }
            goto end_command_line_parse_loop;
        }

        // did user specify a hash function to use instead of the default one?
        if (strcmp(argv[i], "-H") == 0)
        {
            i++;  // move to the next arg
            if (i < argc)
            {
                if (1 != sscanf(argv[i], "%d", &selected_hashfunction))
                {
                    untrappableerror("Failed to decode the numeric -H argument [hashfunction ID]: ", argv[i]);
                }
            }
            else
            {
                untrappableerror("You must specify a (numeric) -H argument [hashfunction ID]! ", "Uh-oh...");
            }
            if (user_trace)
            {
                fprintf(stderr, "Configuring CRM114 to use hash function %d\n",
                        selected_hashfunction);
            }
            goto end_command_line_parse_loop;
        }
        // did user specify his/her desire to have an analysis profile run?
        if (strcmp(argv[i], "-A") == 0)
        {
            i++;  // move to the next arg
            if (i < argc)
            {
                if (crm_init_analysis(&analysis_cfg, argv[i], -1))
                {
                    untrappableerror("Failed to decode the -A argument: ", argv[i]);
                }
            }
            else
            {
                untrappableerror("The commandline analysis profiling option '-A' requires an extra argument: ", "Uh-oh...");
            }

            if (user_trace)
            {
                fprintf(stderr, "Configuring CRM114 to use BMP-assisted analysis profiling. We're GO! from NOW!\n");
            }
            goto end_command_line_parse_loop;
        }

        if (strcmp(argv[i], "-T") == 0)
        {
            if (internal_trace == 0)
            {
                internal_trace = 1;
                fprintf(stderr, "Internal tracing on\n");
            }
            else
            {
                internal_trace = 0;
                fprintf(stderr, "Internal tracing off\n");
            }
            goto end_command_line_parse_loop;
        }

        if (strcmp(argv[i], "-p") == 0)
        {
            profile_execution = 1;
            if (user_trace)
                fprintf(stderr, "Setting profile_execution to 1\n");
            goto end_command_line_parse_loop;
        }

        //   is this a change to the maximum number of program lines?
        if (strcmp(argv[i], "-P") == 0)
        {
            i++;  // move to the next arg
            if (i < argc)
            {
                if (1 != sscanf(argv[i], "%d", &max_pgmlines))
                {
                    untrappableerror("Failed to decode the numeric -P argument [number of program lines]: ", argv[i]);
                }
                max_pgmsize = 128 * max_pgmlines;
            }
            else
            {
                untrappableerror("You must specify a (numeric) -P argument [number of program lines]! ", "Uh-oh...");
            }
            if (user_trace)
            {
                fprintf(stderr, "Setting max prog lines to %d (%d bytes)\n",
                        max_pgmlines, (int)(sizeof(char) * max_pgmsize));
            }
            goto end_command_line_parse_loop;
        }

        //   is this a "gimme a listing" flag?
        if (strcmp(argv[i], "-l") == 0)
        {
            i++;  // move to the next arg
            if (i < argc)
            {
                if (1 != sscanf(argv[i], "%d", &prettyprint_listing))
                {
                    untrappableerror("Failed to decode the numeric -l argument [listing level]: ", argv[i]);
                }
            }
            else
            {
                untrappableerror("You must specify a (numeric) -l argument [listing level]! ", "Uh-oh...");
            }
            if (user_trace)
            {
                fprintf(stderr, "Setting listing level to %d\n",
                        prettyprint_listing);
            }
            goto end_command_line_parse_loop;
        }

        //   is this a "Use Local Country Code" flag?
        if (strcmp(argv[i], "-C") == 0)
        {
            if (user_trace)
                fprintf(stderr, "Setting locale to local\n");
            setlocale(LC_ALL, "");
            goto end_command_line_parse_loop;
        }

        //   is this a change to the math mode (0,1 for alg/RPN but only in EVAL,
        //   2,3 for alg/RPN everywhere.
        if (strcmp(argv[i], "-q") == 0)
        {
            i++;  // move to the next arg
            if (i < argc)
            {
                if (1 != sscanf(argv[i], "%d", &q_expansion_mode))
                {
                    untrappableerror("Failed to decode the numeric -q argument [expansion mode]: ", argv[i]);
                }
                if (q_expansion_mode < 0 || q_expansion_mode > 3)
                {
                    untrappableerror("You've specified an invalid -q argument.\n"
                                     "     (accepted: 0=algebra-eval, 1=RPN-eval, 2=algebra-all, 3=RPN-all)\n"
                                     "     You specified -q [expansion mode]: ", argv[i]);
                }
            }
            else
            {
                untrappableerror("You must specify the (numeric) -q argument [expansion mode]! ", "Uh-oh...");
            }
            if (user_trace)
            {
                fprintf(stderr, "Setting math mode to %d ", q_expansion_mode);
                if (q_expansion_mode == 0)
                    fprintf(stderr, "(algebraic, only in EVAL\n");
                else if (q_expansion_mode == 1)
                    fprintf(stderr, "(RPN, only in EVAL\n");
                else if (q_expansion_mode == 2)
                    fprintf(stderr, "(algebraic, in all expressions)\n");
                else if (q_expansion_mode == 3)
                    fprintf(stderr, "(RPN, in all expressions)\n");
            }
            goto end_command_line_parse_loop;
        }

        //   change the size of the maximum data window we'll allow
        if (strcmp(argv[i], "-w") == 0)
        {
            i++; // move to the next arg
            if (i < argc)
            {
                if (1 != sscanf(argv[i], "%u", &data_window_size))
                {
                    untrappableerror("Failed to decode the numeric -w argument [data windows size]: ", argv[i]);
                }
            }
            else
            {
                untrappableerror("You must specify the (numeric) -w argument [data windows size]! ", "Uh-oh...");
            }
            if (data_window_size < 8192)
            {
                fprintf(stderr, "Sorry, but the min data window is 8192 bytes\n");
                data_window_size = 8192;
            }
            if (user_trace)
            {
                fprintf(stderr, "Setting max data window to %d chars\n",
                        data_window_size);
            }
            goto end_command_line_parse_loop;
        }

        //   change the size of the sparse spectrum file default.
        if (strcasecmp(argv[i], "-s") == 0)
        {
            i++;  // move to the next arg
            if (i < argc)
            {
                if (1 == sscanf(argv[i], "%d", &sparse_spectrum_file_length))
                {
                    if (strcmp(argv[i - 1], "-S") == 0)
                    {
                        int k;

                        k = (int)floor(log2(sparse_spectrum_file_length - 1));
                        while ((2 << k) + 1 < sparse_spectrum_file_length)
                        {
                            k++;
                        }
                        sparse_spectrum_file_length = (2 << k) + 1;
                    }
                }
                else
                {
                    untrappableerror("On -s flag: incomprehensible .CSS file length: ", argv[i]);
                }
            }
            else
            {
                untrappableerror("On -s flag: missing .CSS file length! ", "Uh-oh...");
            }

            if (user_trace)
            {
                fprintf(stderr, "Setting sparse spectrum length to %d bins\n",
                        sparse_spectrum_file_length);
            }
            goto end_command_line_parse_loop;
        }

        //   set a break from the command line
        if (strcmp(argv[i], "-b") == 0)
        {
            i++;  // move to the next arg
            if (i < argc)
            {
                if (1 != sscanf(argv[i], "%d", &cmdline_break))
                {
                    untrappableerror("Failed to decode the numeric -b argument [breakpoint line #]: ", argv[i]);
                }
            }
            else
            {
                untrappableerror("You must specify the (numeric) -b argument [breakpoint line #]! ", "Uh-oh...");
            }
            if (user_trace)
            {
                fprintf(stderr, "Setting the command-line break to line %d\n",
                        cmdline_break);
            }
            goto end_command_line_parse_loop;
        }

        //   set base value for detailed engine exit values
        if (strcmp(argv[i], "-E") == 0)
        {
            i++;  // move to the next arg
            if (i < argc)
            {
                if (1 != sscanf(argv[i], "%d", &engine_exit_base))
                {
                    untrappableerror("Failed to decode the numeric -E argument [engine exit base value]: ", argv[i]);
                }
            }
            else
            {
                untrappableerror("You must specify the numeric -E argument [engine exit base value]! ", "Uh-oh...");
            }
            if (user_trace)
            {
                fprintf(stderr, "Setting the engine exit base value to %d\n",
                        engine_exit_base);
            }
            goto end_command_line_parse_loop;
        }

        //   set countdown cycles before dropping to debugger
        if (strcmp(argv[i], "-d") == 0)
        {
            i++;  // move to the next arg
            debug_countdown = 0;
            if (i < argc)
            {
                if (1 != sscanf(argv[i], "%d", &debug_countdown))
                {
                    // optional argument!
                    //  if next arg wasn't numeric, back up
                    i--;
                    // untrappableerror("Failed to decode the numeric -d argument [debug statement countdown]: ", argv[i]);
                }
            }

            if (user_trace)
            {
                fprintf(stderr, "Setting debug countdown to %d statements\n",
                        debug_countdown);
            }
            goto end_command_line_parse_loop;
        }

        //   ignore environment variables?
        if (strcmp(argv[i], "-e") == 0)
        {
            ignore_environment_vars++;
            if (user_trace)
                fprintf(stderr, "Ignoring environment variables\n");
            goto end_command_line_parse_loop;
        }

        // is this to set the cwd?
        if (strcmp(argv[i], "-u") == 0)
        {
            i++;  // move to the next arg
            if (i >= argc)
            {
                untrappableerror("The -u working-directory change needs an arg\n", "Uh-oh...");
            }
            else
            {
                if (user_trace)
                {
                    fprintf(stderr, "Setting WD to '%s'\n", argv[i]);
                }
                if (chdir(argv[i]))
                {
                    untrappableerror_ex(SRC_LOC(), "Sorry, couldn't chdir to '%s'; errno=%d(%s)\n",
                                        argv[i], errno, errno_descr(errno));
                }
            }
            goto end_command_line_parse_loop;
        }

        if (strcmp(argv[i], "-v") == 0)
        {
            int all_included = 1;
            char cs[80 * 30];
            int len = WIDTHOF(cs);
            char *dst = cs;
            int partlen;
            struct write_spec_prop prop = { 0 };

            //   NOTE - version info goes to stdout, not stderr, just like GCC does
            fprintf(stdout, " This is CRM114, version %s, rev %s (%s) (OS: %s)\n",
                    VERSION,
                    REVISION,
                    crm_regversion(),
                    HOSTTYPE);
            fprintf(stdout, " Copyright 2001-2009 William S. Yerazunis\n");
            fprintf(stdout, " This software is licensed under the GPL with ABSOLUTELY NO WARRANTY\n");
            fprintf(stdout, "\n"
                            "Classifiers included in this build:\n");
#if !CRM_WITHOUT_BIT_ENTROPY
            snprintf(dst, len, "  Bit-Entropy\n");
            dst[len - 1] = 0;
            partlen = (int)strlen(dst);
            dst += partlen;
            len -= partlen;
#else
            all_included = 0;
#endif

#if !CRM_WITHOUT_CORRELATE
            snprintf(dst, len, "  Correlate\n");
            dst[len - 1] = 0;
            partlen = (int)strlen(dst);
            dst += partlen;
            len -= partlen;
#else
            all_included = 0;
#endif

#if !CRM_WITHOUT_FSCM
            snprintf(dst, len, "  FSCM\n");
            dst[len - 1] = 0;
            partlen = (int)strlen(dst);
            dst += partlen;
            len -= partlen;
#else
            all_included = 0;
#endif

#if !CRM_WITHOUT_MARKOV
            snprintf(dst, len, "  Markov\n");
            dst[len - 1] = 0;
            partlen = (int)strlen(dst);
            dst += partlen;
            len -= partlen;
#else
            all_included = 0;
#endif

#if !CRM_WITHOUT_NEURAL_NET
            snprintf(dst, len, "  Neural-Net\n");
            dst[len - 1] = 0;
            partlen = (int)strlen(dst);
            dst += partlen;
            len -= partlen;
#else
            all_included = 0;
#endif

#if !CRM_WITHOUT_OSBF
            snprintf(dst, len, "  OSBF\n");
            dst[len - 1] = 0;
            partlen = (int)strlen(dst);
            dst += partlen;
            len -= partlen;
#else
            all_included = 0;
#endif

#if !CRM_WITHOUT_OSB_BAYES
            snprintf(dst, len, "  OSB-Bayes\n");
            dst[len - 1] = 0;
            partlen = (int)strlen(dst);
            dst += partlen;
            len -= partlen;
#else
            all_included = 0;
#endif

#if !CRM_WITHOUT_OSB_HYPERSPACE
            snprintf(dst, len, "  OSB-Hyperspace\n");
            dst[len - 1] = 0;
            partlen = (int)strlen(dst);
            dst += partlen;
            len -= partlen;
#else
            all_included = 0;
#endif

#if !CRM_WITHOUT_OSB_WINNOW
            snprintf(dst, len, "  OSB-Winnow\n");
            dst[len - 1] = 0;
            partlen = (int)strlen(dst);
            dst += partlen;
            len -= partlen;
#else
            all_included = 0;
#endif

#if !CRM_WITHOUT_SKS
            snprintf(dst, len, "  SKS\n");
            dst[len - 1] = 0;
            partlen = (int)strlen(dst);
            dst += partlen;
            len -= partlen;
#else
            all_included = 0;
#endif

#if !CRM_WITHOUT_SVM
            snprintf(dst, len, "  SVM\n");
            dst[len - 1] = 0;
            partlen = (int)strlen(dst);
            dst += partlen;
            len -= partlen;
#else
            all_included = 0;
#endif

#if !CRM_WITHOUT_CLUMP
            snprintf(dst, len, "  CLUMP\n");
            dst[len - 1] = 0;
            partlen = (int)strlen(dst);
            dst += partlen;
            len -= partlen;
#else
            all_included = 0;
#endif

            if (all_included)
            {
                fprintf(stdout, "  all of 'em\n");
            }
            else
            {
                fprintf(stdout, "%s", cs);
            }

            // extra features:
            all_included = 0;
            fprintf(stdout, "\nExtra features:\n");
#ifndef CRM_ASSERT_IS_UNTRAPPABLE
#error "config is corrupted; check config.h"
#endif

#ifndef CRM_DONT_ASSERT
            fprintf(stdout, "  Developer Assertions are:\n"
                            "    - included\n");
            all_included = 1;
#if CRM_ASSERT_IS_UNTRAPPABLE
            fprintf(stdout, "    - UNTRAPPABLE\n");
#else
            fprintf(stdout, "    - trappable (i.e. cause 'fault' in script when triggered)\n");
#endif
#if defined HAVE__CRTDBGBREAK
            fprintf(stdout, "    - '-Cdbg' will trigger the debugger on your system\n");
#elif defined HAVE___DEBUGBREAK
            fprintf(stdout, "    - '-Cdbg' will trigger the debugger on your system\n");
#else
            fprintf(stdout, "    - '-Cdbg' will cause a memory access violation in a last ditch\n"
                            "      effort to trigger your debugger\n");
#endif
#endif      // CRM_DONT_ASSERT

            if (!all_included)
            {
                fprintf(stdout, "  none\n");
            }

            fprintf(stdout, "\nScript language:\n"
                            "  Reserved words (instructions) + attributes / format:\n");

            prop.o = stdout;
            show_instruction_spec(-1, write_spec_bit, &prop);

            if (engine_exit_base != 0)
            {
                exit(engine_exit_base + 16);
            }
            else
            {
                exit(EXIT_SUCCESS);
            }
        }

        if (strncmp(argv[i], "-{", 2) == 0) //  don't care about the "}"
        {
            if (user_trace)
                fprintf(stderr, "Command line program at arg %d\n", i);
            openbracket = i;

            posv[1] = argv[i];
            posc = 2;

            goto end_command_line_parse_loop;
        }

        //
        //      What about -( var var var ) cmdline var restrictions?
        if (strncmp(argv[i], "-(", 2) == 0)
        {
            if (user_trace)
                fprintf(stderr, "Allowed command line arg list at arg %d\n", i);
            openparen = i;
            //
            //      If there's a -- at the end of the arg, lock out system
            //      flags as though we hit a '--' flag.
            //      (i.e. no debugger.  Minimal security. No doubt this is
            //      circumventable by a sufficiently skilled user, but
            //      at least it's a start.)
            if (strncmp("--", &argv[i][strlen(argv[i]) - 2], 2) == 0)
            {
                if (user_trace)
                    fprintf(stderr, "cmdline arglist also locks out sysflags.\n");
                for (; ++i < argc;)
                {
                    if (argv[i][0] != '-')
                    {
                        posv[posc++] = argv[i];
                    }
                }
                break;
            }
            goto end_command_line_parse_loop;
        }

        //   set microgroom_stop_after
        if (strcmp(argv[i], "-m") == 0)
        {
            i++;  // move to the next arg
            if (i < argc)
            {
                if (1 != sscanf(argv[i], "%d", &microgroom_stop_after))
                {
                    untrappableerror("Failed to decode the numeric -m argument [microgroom stop after #]: ", argv[i]);
                }
            }
            else
            {
                untrappableerror("You must specify the numeric -m argument [microgroom stop after #]! ", "Uh-oh...");
            }
            if (user_trace)
            {
                fprintf(stderr, "Setting microgroom_stop_after to %d\n",
                        microgroom_stop_after);
            }
            if (microgroom_stop_after <= 0)  //  if value <= 0 set it to default
                microgroom_stop_after = MICROGROOM_STOP_AFTER;
            goto end_command_line_parse_loop;
        }

        //   set microgroom_chain_length length
        if (strcmp(argv[i], "-M") == 0)
        {
            i++;  // move to the next arg
            if (i < argc)
            {
                if (1 != sscanf(argv[i], "%d", &microgroom_chain_length))
                {
                    untrappableerror("Failed to decode the numeric -M argument [microgroom chain length]: ", argv[i]);
                }
            }
            else
            {
                untrappableerror("You must specify the numeric -M argument [microgroom chain length]! ", "Uh-oh...");
            }
            if (user_trace)
            {
                fprintf(stderr, "Setting microgroom_chain_length to %d\n",
                        microgroom_chain_length);
            }
            if (microgroom_chain_length < 5)  //  if value <= 5 set it to default
                microgroom_chain_length = MICROGROOM_CHAIN_LENGTH;
            goto end_command_line_parse_loop;
        }

        //   set min_pmax_pmin_ratio
        if (strcmp(argv[i], "-r") == 0)
        {
            i++;  // move to the next arg
            if (i < argc)
            {
                if (1 != sscanf(argv[i], "%lf", &min_pmax_pmin_ratio))
                {
                    untrappableerror("Failed to decode the numeric -r argument [Pmin/Pmax ratio]: ", argv[i]);
                }
            }
            else
            {
                untrappableerror("You must specify the numeric -r argument [Pmin/Pmax ratio]! ", "Uh-oh...");
            }
            if (user_trace)
            {
                fprintf(stderr, "Setting min pmax/pmin of a feature to %f\n",
                        min_pmax_pmin_ratio);
            }
            if (min_pmax_pmin_ratio < 0)  //  if value < 0 set it to 0
                min_pmax_pmin_ratio = OSBF_MIN_PMAX_PMIN_RATIO;
            goto end_command_line_parse_loop;
        }

        if (strcmp(argv[i], "-in") == 0)
        {
            i++;  // move to the next arg
            if (i < argc)
            {
                // support '0' as stdin handle:
                if (strcmp(argv[i], "0") == 0
                   || strcmp(argv[i], "-") == 0
                   || strcmp(argv[i], "stdin") == 0
                   || strcmp(argv[i], "/dev/stdin") == 0
                   || strcmp(argv[i], "CON:") == 0
                   || strcmp(argv[i], "/dev/tty") == 0)
                {
                    stdin = os_stdin();
                    stdin_filename = "stdin (default)";
                }
                else if (strcmp(argv[i], "1") == 0
                        || strcmp(argv[i], "2") == 0)
                {
                    untrappableerror("'-in' cannot use the stdout/stderr handles 1/2! This argument is therefor illegal: ",
                                     argv[i]);
                }
                else
                {
                    stdin_filename = argv[i];
                    stdin = fopen(stdin_filename, "rb"); // open in BINARY mode!
                    if (stdin == NULL)
                    {
                        char dirbuf[DIRBUFSIZE_MAX];

                        stdin = os_stdin();                                                     // reset stdin before the error report is sent out!
                        untrappableerror_ex(SRC_LOC(), "Failed to open stdin input replacement file '%s' (full path: '%s')",
                                            stdin_filename, mk_absolute_path(dirbuf, WIDTHOF(dirbuf), stdin_filename));
                    }
                }
            }
            else
            {
                untrappableerror("The -in option requires a (file/device) argument! ", "Uh-oh...");
            }
            if (user_trace)
            {
                fprintf(stderr, "Setting stdin replacement file '%s'\n",
                        stdin_filename);
            }
            goto end_command_line_parse_loop;
        }
        if (strcmp(argv[i], "-out") == 0)
        {
            i++;  // move to the next arg
            if (i < argc)
            {
                // support '1' as stdout handle:
                if (strcmp(argv[i], "1") == 0
                   || strcmp(argv[i], "-") == 0
                   || strcmp(argv[i], "stdout") == 0
                   || strcmp(argv[i], "/dev/stdout") == 0
                   || strcmp(argv[i], "CON:") == 0
                   || strcmp(argv[i], "/dev/tty") == 0)
                {
                    stdout = os_stdout();
                    stdout_filename = "stdout (default)";
                }
                else if (strcmp(argv[i], "2") == 0
                        || strcmp(argv[i], "stderr") == 0
                        || strcmp(argv[i], "/dev/stderr") == 0)
                {
                    // support '2' as stderr handle:
                    stdout = os_stderr();
                    stdout_filename = "stderr";
                }
                else if (strcmp(argv[i], "0") == 0)
                {
                    untrappableerror("'-out' cannot use the stdin handle 0! This argument is therefor illegal: ", argv[i]);
                }
                else
                {
                    int appending = 0;

                    stdout_filename = argv[i];
                    if (stdout_filename[0] == '@')
                    {
                        stdout_filename++;
                        appending = 1;
                    }
                    if (strcmp(stdout_filename, stderr_filename) != 0)
                    {
                        //
                        // GROT GROT GROT: Win32 is case insensitive; besides, you could screw up by using different
                        // relative and/or absolute paths for both.
                        // For now, the user won't be protected against his own 'smartness' here.
                        //
                        stdout = fopen(stdout_filename, (appending ? "ab" : "wb"));                       // open in BINARY mode!
                        if (stdout == NULL)
                        {
                            char dirbuf[DIRBUFSIZE_MAX];

                            stdout = os_stdout();                             // reset stdout before the error report is sent out!
                            untrappableerror_ex(SRC_LOC(), "Failed to open stdout input replacement file '%s' (full path: '%s')",
                                                stdout_filename, mk_absolute_path(dirbuf, WIDTHOF(dirbuf), stdout_filename));
                        }
                    }
                    else
                    {
                        // same file for both.
                        if (stderr != NULL)
                            stdout = stderr;
                        if (appending)
                            (void)fseek(stdout, 0, SEEK_END);
                    }
                }
            }
            else
            {
                untrappableerror("The -out option requires a (file/device) argument! ", "Uh-oh...");
            }
            if (user_trace)
            {
                fprintf(stderr, "Setting stdout replacement file '%s'\n",
                        stdout_filename);
            }
            goto end_command_line_parse_loop;
        }
        if (strcmp(argv[i], "-err") == 0)
        {
            i++;  // move to the next arg
            if (i < argc)
            {
                // support '2' as stderr handle:
                if (strcmp(argv[i], "2") == 0
                   || strcmp(argv[i], "-") == 0
                   || strcmp(argv[i], "stderr") == 0
                   || strcmp(argv[i], "/dev/stderr") == 0
                   || strcmp(argv[i], "CON:") == 0
                   || strcmp(argv[i], "/dev/tty") == 0)
                {
                    stderr = os_stderr();
                    stderr_filename = "stderr (default)";
                }
                else if (strcmp(argv[i], "1") == 0
                        || strcmp(argv[i], "stdout") == 0
                        || strcmp(argv[i], "/dev/stdout") == 0)
                {
                    // support '1' as stdout handle:
                    stderr = os_stdout();
                    stderr_filename = "stdout";
                }
                else if (strcmp(argv[i], "0") == 0)
                {
                    untrappableerror("'-err' cannot use the stdin handle 0! This argument is therefor illegal: ", argv[i]);
                }
                else
                {
                    int appending = 0;

                    stderr_filename = argv[i];
                    if (stderr_filename[0] == '@')
                    {
                        stderr_filename++;
                        appending = 1;
                    }
                    if (strcmp(stdout_filename, stderr_filename) != 0)
                    {
                        //
                        // GROT GROT GROT: Win32 is case insensitive; besides, you could screw up by using different
                        // relative and/or absolute paths for both.
                        // For now, the user won't be protected against his own 'smartness' here.
                        //
                        stderr = fopen(stderr_filename, (appending ? "ab" : "wb"));                       // open in BINARY mode!
                        if (stderr == NULL)
                        {
                            char dirbuf[DIRBUFSIZE_MAX];

                            stderr = os_stderr();                             // reset stderr before the error report is sent out!
                            untrappableerror_ex(SRC_LOC(), "Failed to open stderr input replacement file '%s' (full path: '%s')",
                                                stderr_filename, mk_absolute_path(dirbuf, WIDTHOF(dirbuf), stderr_filename));
                        }
                    }
                    else
                    {
                        // same file for both.
                        if (stdout != NULL)
                            stderr = stdout;
                        if (appending)
                            (void)fseek(stderr, 0, SEEK_END);
                    }
                }
            }
            else
            {
                untrappableerror("The -err option requires a (file/device) argument! ", "Uh-oh...");
            }
            if (user_trace)
            {
                fprintf(stderr, "Setting stderr replacement file '%s'\n",
                        stderr_filename);
            }
            goto end_command_line_parse_loop;
        }
        if (strcmp(argv[i], "-Bill") == 0)
        {
            act_like_Bill = 1;
            if (user_trace)
                fprintf(stderr, "Enabling BillY/vanilla mode.\n");
            goto end_command_line_parse_loop;
        }
        if (strcmp(argv[i], "-RT") == 0)
        {
            crm_rand_init(666);
            if (user_trace)
                fprintf(stderr, "Enabling 'test reproducibility' mode.\n");
            goto end_command_line_parse_loop;
        }

#ifndef CRM_DONT_ASSERT
        if (strcmp(argv[i], "-Cdbg") == 0)
        {
            trigger_debugger = 1;
            if (user_trace)
                fprintf(stderr, "Debugger trigger turned ON.\n");
            goto end_command_line_parse_loop;
        }
#endif
#if (defined(WIN32) || defined(_WIN32) || defined(_WIN64) || defined(WIN64)) && defined(_DEBUG)
        if (strcmp(argv[i], "-memdump") == 0)
        {
            trigger_memdump = 1;
            if (user_trace)
                fprintf(stderr, "memory leak dump turned ON.\n");
            goto end_command_line_parse_loop;
        }
#endif

        //  that's all of the flags.  Anything left must be
        //  the name of the file we want to use as a program
        //  BOGOSITUDE - only the FIRST such thing is the name of the
        //  file we want to use as a program.  The rest of the args
        //  should just be passed along
        if (csl->filename == NULL && openbracket < 1)
        {
            if (strlen(argv[i]) > MAX_FILE_NAME_LEN)
            {
                untrappableerror_ex(SRC_LOC(), "Couldn't open the file, "
                                               "filename too long: (path length = %d, max = %d) '%s'",
                                    (int)strlen(argv[i]), MAX_FILE_NAME_LEN, argv[i]);
            }

            posv[1] = argv[i];
            posc = 2;

            csl->filename = argv[i];
            csl->filename_allocated = 0;
            if (user_trace)
                fprintf(stderr, "Using program file %s\n", csl->filename);
        }
        else
        {
            // further non-option arguments: feed 'em to the script as :_posX: variables:
            if (argv[i][0] != '-')
            {
                posv[posc++] = argv[i];
            }
        }

end_command_line_parse_loop:
        if (internal_trace)
        {
            fprintf(stderr, "End of pass %d through cmdline parse loop\n",
                    i);
        }
    }

    for (; ++i < argc;)
    {
        if (argv[i][0] != '-')
        {
            posv[posc++] = argv[i];
        }
    }

#if 0
    //
    //     Did we get a program filename?  If not, look for one.
    //     At this point, accept any arg that doesn't start with a - sign
    //
    if (csl->filename == NULL && openbracket < 1)
    {
        if (internal_trace)
            fprintf(stderr, "Looking for _some_ program to run...\n");
        for (i = 1; i < argc; i++)
        {
            if (argv[i][0] != '-')
            {
                if (strlen(argv[i]) > MAX_FILE_NAME_LEN)
                {
                    untrappableerror_ex(SRC_LOC(), "Couldn't open the file, "
                                                   "filename too long: (path length = %d, max = %d) '%s'",
                                        (int)strlen(argv[i]), MAX_FILE_NAME_LEN, argv[i]);
                }
                posv[1] = argv[i];
                posc = CRM_MAX(posc, 2);

                csl->filename = argv[i];
                csl->filename_allocated = 0;
                i = argc;
            }
        }
        if (user_trace)
            fprintf(stderr, "Using program file %s\n", csl->filename);
    }
#endif

    //      If we still don't have a program, we're done.  Squalk an
    //      error.
    if (csl->filename == NULL && openbracket < 0)
    {
        fprintf(stderr, "\nCan't find a file to run,"
                        "or a command-line to execute.\n"
                        "I give up... (exiting)\n");
        if (engine_exit_base != 0)
        {
            exit(engine_exit_base + 17);
        }
        else
        {
            exit(EXIT_SUCCESS);
        }
    }

    //     open, stat and load the program file
    if (openbracket < 0)
    {
        if (user_trace)
            fprintf(stderr, "Using program file %s\n", csl->filename);

        if (argc <= 1)
        {
            fprintf(stderr, "CRM114 version %s, rev %s (regex engine: %s) (OS: %s)\n",
                    VERSION,
                    REVISION,
                    crm_regversion(),
                    HOSTTYPE);
            fprintf(stderr, "Try 'crm <progname>', or 'crm -h' for help\n");
            if (engine_exit_base != 0)
            {
                exit(engine_exit_base + 18);
            }
            else
            {
                exit(EXIT_SUCCESS);
            }
        }
        else
        {
            if (user_trace)
            {
                fprintf(stderr, "Loading program from file %s\n",
                        csl->filename);
            }
            crm_load_csl(csl);
        }
    }
    else
    {
        //   if we got here, then it's a command-line program, and
        //   we should just assemble the proggie from the argv [openbracket]
        if (strlen(&(argv[openbracket][1])) + 2048 > max_pgmsize)
        {
            untrappableerror("The command line program is too big.\n",
                             "Try increasing the max program size with -P.\n");
        }
        csl->filename = "(from command line)";
        csl->filename_allocated = 0;
        csl->filetext = (char *)calloc(max_pgmsize, sizeof(csl->filetext[0]));
        csl->filetext_allocated = 1;
        if (!csl->filetext)
        {
            untrappableerror(
                "Couldn't alloc csl->filetext space (where I was going to put your program.\nWithout program space, we can't run.  Sorry.",
                "");
        }
        if (user_trace)
            fprintf(stderr, "Using program file %s\n", csl->filename);

        /* [i_a] make sure we never overflow the buffer: */

        // [i_a] WARNING: ALWAYS make sure the program ends with TWO newlines so that we have a program
        //       which comes with an 'empty' statement at the very end. This is mandatory to ensure
        //       a valid 'alius' fail forward target is available at the end of the program at all times.

        //     the [1] below gets rid of the leading '-' char
        snprintf(csl->filetext, max_pgmsize, "\n%s\n\n", &(argv[openbracket][1]));
        csl->filetext[max_pgmsize - 1] = 0;   /* make sure the string is terminated; some environments don't do this when the boundary was hit */
        csl->nchars = (int)strlen(csl->filetext);
        csl->hash = strnhash(csl->filetext, csl->nchars);
        if (user_trace)
        {
            fprintf(stderr, "Hash of program: 0x%08lX, length is %d bytes: %s\n-->\n%s",
                    (unsigned long int)csl->hash, csl->nchars, csl->filename, csl->filetext);
        }
    }

    //  We get another csl-like data structure,
    //  which we'll call the cdw, which has all the fields we need, and
    //  simply allocate the data window of "adequate size" and read
    //  stuff in on stdin.

    cdw = calloc(1, sizeof(cdw[0]));
    if (!cdw)
    {
        untrappableerror("Couldn't alloc cdw.\nThis is very bad.", "");
    }
    cdw->filename = NULL;
    cdw->rdwr = 1;
    cdw->filedes = -1;
    cdw->filetext = calloc(data_window_size, sizeof(cdw->filetext[0]));
    cdw->filetext_allocated = 1;
    if (!cdw->filetext)
    {
        untrappableerror(
            "Couldn't alloc cdw->filetext.\nWithout this space, you have no place for data.  Thus, we cannot run.",
            "");
    }

    //      also allocate storage for the windowed data input
    newinputbuf = calloc(data_window_size, sizeof(newinputbuf[0]));

    //      and our three big work buffers - these are used ONLY inside
    //      of a single statement's execution and do NOT ever contain state
    //      that has to exist across statements.
    inbuf = calloc(data_window_size, sizeof(inbuf[0]));
    outbuf = calloc(data_window_size, sizeof(outbuf[0]));
    tempbuf = calloc(data_window_size, sizeof(tempbuf[0]));
    if (!tempbuf || !outbuf || !inbuf || !newinputbuf)
    {
        untrappableerror(
            "Couldn't alloc one or more of"
            "newinputbuf,inbuf,outbuf,tempbuf.\n"
            "These are all necessary for operation."
            "We can't run.", "");
    }

    //     Initialize the VHT, add in a few predefined variables
    //
    crm_vht_init(argc, argv, posc, posv);

    //    Call the pre-processor on the program
    //
    status = crm_preprocessor(csl, 0);

    //    Now, call the microcompiler on the program file.
    status = crm_microcompiler(csl, vht);
    //    Great - program file is now mapped via csl->mct

    //    Put a copy of the preprocessor-result text into
    //    the isolated variable ":_pgm_text:"
    crm_set_temp_var(":_pgm_text:", csl->filetext, -1, 0);

    //  If the windowflag == 0, we should preload the data window.  Now,
    //  let's get some data in.

    //    and preload the data window with stdin until we hit EOF
    i = 0;
    if (csl->preload_window)
    {
        //     GROT GROT GROT  This is slow
        //
        //while (!feof (stdin) && i < data_window_size - 1)
        //{
        //  cdw->filetext[i] = fgetc(stdin);
        //  i++;
        //}
        //i--;  //    get rid of the extra ++ on i from the loop; this is the
        //            EOF "character" which prints like an umlauted-Y.
        //
        //
        //         This is the much faster way.
        //
        //      i = fread (cdw->filetext, 1, data_window_size - 1, stdin);
        //
        //         JesusFreke suggests this instead- retry with successively
        //         smaller readsizes on systems that can't handle full
        //         POSIX-style massive block transfers.
        size_t readsize = data_window_size - 1;
        size_t targetsize = data_window_size - 1;
        size_t offset;

#if (defined(WIN32) || defined(_WIN32) || defined(_WIN64) || defined(WIN64))
        readsize = CRM_MIN(16384, readsize);   // WIN32 doesn't like those big sizes AT ALL! (core dump of executable!) :-(
#endif
        for (offset = 0; !feof(stdin) && offset < targetsize;)
        {
            //i += fread (cdw->filetext + i, 1, readsize-1, stdin);
            size_t rs;
            size_t rr;

            rs = offset + readsize < targetsize
                 ? readsize
                 : targetsize - offset;
            rr = fread(cdw->filetext + offset, 1, rs, stdin);
            if (ferror(stdin))
            {
                if (errno == ENOMEM && readsize > 1) //  insufficient memory?
                {
                    readsize = readsize / 2; //  try a smaller block
                    clearerr(stdin);
                }
                else
                {
                    fprintf(stderr, "Error while trying to get startup input.  "
                                    "This is usually pretty much hopeless, but "
                                    "I'll try to keep running anyway.  ");
                    break;
                }
            }
            offset += rr;
        }
        i = offset;
    }

    //   data window is now preloaded (we hope), set the cdwo up.

    cdw->filetext[i] = 0;
    cdw->nchars = i;
    cdw->hash = strnhash(cdw->filetext, cdw->nchars);
    cdw->mct = NULL;
    cdw->nstmts = -1;
    cdw->cstmt = -1;
    cdw->caller = NULL;

    // and put the initial data window suck-in contents into the vht
    //  with the special name :_dw:
    //
    //   GROT GROT GROT  will have to change this when we get rid of separate
    //   areas for the data window and the temporary area.  In particular, the
    //   "start" will no longer be zero.  Note to self: get rid of this comment
    //   when it gets fixed.  Second note to self - since most of the insert
    //   and delete action happens in :_dw:, for efficiency reasons perhaps
    //   we don't want to merge these areas.
    //
    {
        int dwname;
        int dwlen;
        tdw->filetext[tdw->nchars] = '\n';
        tdw->nchars++;
        dwlen = (int)strlen(":_dw:");
        dwname = tdw->nchars;
        //strcat (tdw->filetext, ":_dw:");
        crm_memmove(&tdw->filetext[dwname], ":_dw:", dwlen);
        tdw->nchars = tdw->nchars + dwlen;
        //    strcat (tdw->filetext, "\n");
        crm_memmove(&tdw->filetext[tdw->nchars], "\n", strlen("\n"));
        tdw->nchars++;
        crm_setvar(NULL,
                   NULL,
                   0,
                   tdw->filetext,
                   dwname,
                   dwlen,
                   cdw->filetext,
                   0,
                   cdw->nchars,
                   -1,
                   -1);
    }
    //
    //    We also set up the :_iso: to hold the isolated variables.
    //    Note that we must specifically NOT use this var during reclamation
    //    or GCing the isolated var storage area.
    //
    //    HACK ALERT HACK ALERT - note that :_iso: starts out with a zero
    //    length and must be updated
    //
#define USE_COLON_ISO_COLON
#ifdef USE_COLON_ISO_COLON
    {
        int isoname;
        int isolen;
        isolen = (int)strlen(":_iso:");
        isoname = tdw->nchars;
        //strcat (tdw->filetext, ":_dw:");
        crm_memmove(&tdw->filetext[isoname], ":_iso:", isolen);
        tdw->nchars = tdw->nchars + isolen;
        //    strcat (tdw->filetext, "\n");
        crm_memmove(&tdw->filetext[tdw->nchars], "\n", strlen("\n"));
        tdw->nchars++;
        crm_setvar(NULL,
                   NULL,
                   0,
                   tdw->filetext,
                   isoname,
                   isolen,
                   tdw->filetext,
                   0,
                   0,
                   -1,
                   -1);
    }
#endif
    //    Now we're here, we can actually run!
    //    set up to start at the 0'th statement (the start)
    csl->cstmt = 0;

    status = crm_invoke(&csl);

    //     This is the *real* exit from the engine, so we do not override
    // the engine's exit status with an engine_exit_base value.
    return status;
}


