/*
 *      main.c

 *      uEmacs/PK 4.0
 *
 *      Based on:
 *
 *      MicroEMACS 3.9
 *      Written by Dave G. Conroy.
 *      Substantially modified by Daniel M. Lawrence
 *      Modified by Petri Kutvonen
 *
 *      MicroEMACS 3.9 (c) Copyright 1987 by Daniel M. Lawrence
 *
 *      Original statement of copying policy:
 *
 *      MicroEMACS 3.9 can be copied and distributed freely for any
 *      non-commercial purposes. MicroEMACS 3.9 can only be incorporated
 *      into commercial software with the permission of the current author.
 *
 *      No copyright claimed for modifications made by Petri Kutvonen.
 *
 *      This file contains the main driving routine, and some keyboard
 *      processing code.
 *
 * REVISION HISTORY:
 *
 * 1.0  Steve Wilhite, 30-Nov-85
 *
 * 2.0  George Jones, 12-Dec-85
 *
 * 3.0  Daniel Lawrence, 29-Dec-85
 *
 * 3.2-3.6 Daniel Lawrence, Feb...Apr-86
 *
 * 3.7  Daniel Lawrence, 14-May-86
 *
 * 3.8  Daniel Lawrence, 18-Jan-87
 *
 * 3.9  Daniel Lawrence, 16-Jul-87
 *
 * 3.9e Daniel Lawrence, 16-Nov-87
 *
 * After that versions 3.X and Daniel Lawrence went their own ways.
 * A modified 3.9e/PK was heavily used at the University of Helsinki
 * for several years on different UNIX, VMS, and MSDOS platforms.
 *
 * This modified version is now called eEmacs/PK.
 *
 * 4.0  Petri Kutvonen, 1-Sep-91
 *
 * 4.1  Ian Dunkin, Mike Arnautov and Gordon Lack ~1988->2017
 *      GGR tags...
 */

#include <stdio.h>
#include <unistd.h>

#include "estruct.h" /* Global structures and defines. */
#include "edef.h"    /* Global definitions. */
#include "efunc.h"   /* Function declarations and name table. */
#include "ebind.h"   /* Default key bindings. */
#include "version.h"

/* For MSDOS, increase the default stack space. */
#if MSDOS & TURBO
#if PKCODE
extern unsigned _stklen = 20000;
#else
extern unsigned _stklen = 32766;
#endif
#endif

#ifndef GOOD
#define GOOD    0
#endif

#if UNIX
#include <signal.h>
static void emergencyexit(int);
#endif

void usage(int status) /* GGR - list all options actually available! */
{
#define NL "\n"

printf( \
"Usage: %s [options] [filename(s)]"                       NL \
"   Options:"                                             NL \
"      +            start at the end of file"             NL \
"      +<n>         start at line <n>"                    NL \
"      -a           process error file"                   NL \
"      -c<filepath> replacement for default rc file"      NL \
"      @<filepath>  deprecated legacy synonym for -c"     NL \
"      -d<dir>      directory holding rc and hlp files"   NL \
"      -e           edit file (default)"                  NL \
"      -g<n>        go to line <n> (same as +<n>)"        NL \
"      -i           insecure mode - look in current dir"  NL \
        , PROGRAM_NAME);
#if CRYPT
fputs(
"      -k<key>      encryption key"                       NL \
, stdout);
#endif
#if PKCODE
fputs(
"      -n           accept null chars"                    NL \
, stdout);
#endif
fputs(
"      -r           restrictive use"                      NL \
"      -s<str>      initial search string"                NL \
"      -v           view only (no edit)"                  NL \
"      -x<filepath> an additional rc file"                NL \
"      -h,--help    display this help and exit"           NL \
"      -V,--version output version information and exit"  NL \
, stdout);
  exit(status);
}

int main(int argc, char **argv)
{
        int c = -1;             /* command character */
        int f;                  /* default flag */
        int n;                  /* numeric repeat count */
        int mflag;              /* negative flag on repeat */
        struct buffer *bp;      /* temp buffer pointer */
        int firstfile;          /* first file flag */
        struct buffer *firstbp = NULL;  /* ptr to first buffer in cmd line */
        int basec;              /* c stripped of meta character */
        int viewflag;           /* are we starting in view mode? */
        int gotoflag;           /* do we need to goto a line at start? */
        int gline = 0;          /* if so, what line? */
        int searchflag;         /* Do we need to search at start? */
        int saveflag;           /* temp store for lastflag */
        int errflag;            /* C error processing? */
        char bname[NBUFN];      /* buffer name of file to read */
#if     CRYPT
        int cryptflag;          /* encrypting on the way in? */
        char ekey[NPAT];        /* startup encryption key */
#endif
        int newc;
        int verflag = 0;        /* GGR Flags -v/-V presence on command line */
        char *rcfile = NULL;    /* GGR non-default rc file */
        char *rcextra[10];      /* GGR additional rc files */
        int rcnum = 0;          /* GGR number of extra files to process */

#if     PKCODE & BSD
        sleep(1); /* Time for window manager. */
#endif

#if     UNIX
#ifdef SIGWINCH
        struct sigaction sigact, oldact;
        sigact.sa_handler = sizesignal;
        sigact.sa_flags = SA_RESTART;
        sigaction(SIGWINCH, &sigact, &oldact);
#endif
#endif
/* GGR The rest of intialisation is done after processing optional args */
        varinit();              /* initialise user variables */

        viewflag = FALSE;       /* view mode defaults off in command line */
        gotoflag = FALSE;       /* set to off to begin with */
        searchflag = FALSE;     /* set to off to begin with */
        firstfile = TRUE;       /* no file to edit yet */
        errflag = FALSE;        /* not doing C error parsing */
#if     CRYPT
        cryptflag = FALSE;      /* no encryption by default */
#endif

/* Set up the initial keybindings.  Must be done early, before any
 * command line processing.
 * We need to allow for the additonal ENDL_KMAP and ENDS_KMAP entries,
 * which mark the End-of-List and End-of-Structure, and round up to the
 * next KEYTAB_INCR boundary.
 */
        int n_init_keys = sizeof(init_keytab)/sizeof(struct key_tab);
        int keytab_alloc_ents = n_init_keys + 2 + KEYTAB_INCR;
        keytab_alloc_ents /= KEYTAB_INCR;
        keytab_alloc_ents *= KEYTAB_INCR;
        extend_keytab(keytab_alloc_ents);
        memcpy(keytab, init_keytab, sizeof(init_keytab));

/* GGR Command line parsing substantially reorganised. It now consists of two
 * separate loops. The first loop processes all optional arguments (command
 * keywords and associated options if any) and stops on reaching the first
 * specification of a file to edit. Along the way it collects specs of
 * initialisation files to process -- these are processed in the correct order
 * after this first loop. The second loop then deals with files nominated
 * by the user.
 */
        while (--argc) {
                argv++;         /* Point at the next token */
                /* Process Switches */
#if     PKCODE
                if (**argv == '+') {
                        gotoflag = TRUE;
                        gline = atoi(*argv + 1); /* skip the '+' */
                } else
#endif
                if (**argv == '-') {
                        char *opt = NULL;
                        char key1;
                        char *arg = *argv + 1;
                        if (*arg == '-') arg++; /* Treat -- as - */
/* Check for specials: help and version requests */
                        if (*arg == '?' ||
                                strncmp("help", arg, strlen(arg)) == 0) {
                                        usage(EXIT_FAILURE);
                                }
                        if (strncmp("version", arg, strlen(arg)) == 0)
                                verflag = strlen(arg);
                        key1 = *arg;
/* Allow options to be given as separate tokens */
                        if (strchr("cCdDgGkKsSxX", key1)) {
                                opt = *argv + 2;
                                if (*opt == '\0' && argc > 0 &&
                                    !strchr("-@", *(*argv + 1))) {
                                        opt = *(++argv);
                                        argc--;
                                    }
                        }

                        switch (key1) {
                                        /* Process Startup macros */
                        case 'a':       /* process error file */
                        case 'A':
                                errflag = TRUE;
                                break;
                        case 'c':
                        case 'C':       /* GGR -c replacement of default rc */
                                rcfile = opt;
                                break;
                        case 'd':
                        case 'D':       /* GGR -d for config/help directory */
                                set_pathname(opt);
                                break;
                        case 'e':       /* -e for Edit file */
                        case 'E':
                                viewflag = FALSE;
                                break;
                        case 'g':       /* -g for initial goto */
                        case 'G':
                                gotoflag = TRUE;
                                gline = atoi(opt);
                                break;
                        case 'i':       /* -i for insecure mode */
                        case 'I':
                                allow_current = 1;
                                break;
#if     CRYPT
                        case 'k':       /* -k<key> for code key */
                        case 'K':
                                /* GGR only if given a key.. */
                                if (strlen(opt) > 0) {
                                    cryptflag = TRUE;
                                    strcpy(ekey, opt);
                                }
                                break;
#endif
#if     PKCODE
                        case 'n':       /* -n accept null chars */
                        case 'N':
                                nullflag = TRUE;
                                break;
#endif
                        case 'r':       /* -r restrictive use */
                        case 'R':
                                restflag = TRUE;
                                break;
                        case 's':       /* -s for initial search string */
                        case 'S':
                                searchflag = TRUE;
                                strncpy(pat, opt, NPAT);
                       /* GGR - set-up some more things */
                                rvstrcpy(tap, pat);
                                mlenold = matchlen = strlen(pat);
                                break;
                        case 'v':       /* -v for View File */
                        case 'V':
                                if (!verflag)
                                    verflag = 1;    /* could be version or */
                                viewflag = TRUE;    /* view request */
                                break;
                        case 'x':       /* GGR: -x for eXtra rc file */
                        case 'X':
                                if (rcnum < sizeof(rcextra)/sizeof(rcextra[0]))
                                        rcextra[rcnum++] = opt;
                                break;
                        default:        /* unknown switch */
                                /* ignore this for now */
                                break;
                        }

                } else if (**argv == '@') {
                        rcfile = *argv + 1;
                } else break;
        }
        if ((verflag && argc == 0) || verflag > 1)
        {
                version();
                exit(EXIT_SUCCESS);
        }

        /* Initialize the editor. */
        vtinit();               /* Display */
        edinit("main");         /* Buffers, windows */

/* GGR - Now process initialisation files before processing rest of comline */
        silent = TRUE;
        if (!rcfile || !startup(rcfile)) startup("");
        if (rcnum) {
                for (int n = 0; n < rcnum; n++)
                        startup(rcextra[n]);
        }
        silent = FALSE;

/* If we are C error parsing... run it! */
        if (errflag) startup("error.cmd");


/* Process rest of comline, which is a list of files to edit */
        while (argc--) {

/* You can use -v/-e to toggle view mode on/off as you go through the
 * files on the command line.
 */
                if (strcmp(*argv, "-e") == 0 || strcmp(*argv, "-v") == 0) {
                        viewflag = *(*(argv++) + 1) == 'v';
                        continue;
                }
                if (strlen(*argv) >= NFILEN) {  /* Sanity check */
                    fprintf(stderr, "filename too long!!\n");
                    sleep(2);
                    vttidy();
                    exit(1);
                }
                 /* set up a buffer for this file */
                makename(bname, *argv);
                unqname(bname);
                 /* set this to inactive */
                bp = bfind(bname, TRUE, 0);
                strcpy(bp->b_fname, *argv);
                bp->b_active = FALSE;
                if (firstfile) {
                        firstbp = bp;
                        firstfile = FALSE;
                }
                 /* set the modes appropriatly */

                if (viewflag)
                        bp->b_mode |= MDVIEW;
#if     CRYPT
                if (cryptflag) {
                        bp->b_mode |= MDCRYPT;
                        myencrypt((char *) NULL, 0);
                        myencrypt(ekey, strlen(ekey));
                        strncpy(bp->b_key, ekey, NPAT);
                }
#endif
                argv++;
        }

/* Done with processing command line */

#if     UNIX
        signal(SIGHUP, emergencyexit);
        signal(SIGTERM, emergencyexit);
#endif

        discmd = TRUE;          /* P.K. */

        /* if there are any files to read, read the first one! */
        bp = bfind("main", FALSE, 0);
        if (firstfile == FALSE && (gflags & GFREAD)) {
                swbuffer(firstbp);
                zotbuf(bp);
        } else
                bp->b_mode |= gmode;

        /* Deal with startup gotos and searches */
        if (gotoflag && searchflag) {
                update(FALSE);
                mlwrite(MLpre "Can not search and goto at the same time!" MLpost);
        } else if (gotoflag) {
                if (gotoline(TRUE, gline) == FALSE) {
                        update(FALSE);
                        mlwrite(MLpre "Bogus goto argument" MLpost);
                }
        } else if (searchflag) {
                if (forwhunt(FALSE, 0) == FALSE)
                        update(FALSE);
        }

        /* Setup to process commands. */
        lastflag = 0;  /* Fake last flags. */

loop:
        /* Execute the "command" macro...normally null. */
        saveflag = lastflag;  /* Preserve lastflag through this. */
        execute(META | SPEC | 'C', FALSE, 1);
        lastflag = saveflag;

#if TYPEAH && PKCODE
        if (typahead()) {
                newc = getcmd();
                update(FALSE);
                do {
                        fn_t execfunc;
                        char *pbp;

                        if (c == newc && (execfunc = getbind(c, &pbp)) != NULL
                            && execfunc != insert_newline
                            && execfunc != insert_tab)
                                newc = getcmd();
                        else
                                break;
                } while (typahead());
                c = newc;
        } else {
                update(FALSE);
                c = getcmd();
        }
#else
        /* Fix up the screen    */
        update(FALSE);

        /* get the next command from the keyboard */
        c = getcmd();
#endif
        /* if there is something on the command line, clear it */
        if (mpresf != FALSE) {
                mlerase();
                update(FALSE);
#if     CLRMSG
                if (c == ' ')   /* ITS EMACS does this  */
                        goto loop;
#endif
        }
        f = FALSE;
        n = 1;

        /* do META-# processing if needed */

        basec = c & ~META;      /* strip meta char off if there */
        if ((c & META) && ((basec >= '0' && basec <= '9') || basec == '-')) {
                f = TRUE;       /* there is a # arg */
                n = 0;          /* start with a zero default */
                mflag = 1;      /* current minus flag */
                c = basec;      /* strip the META */
                while ((c >= '0' && c <= '9') || (c == '-')) {
                        if (c == '-') {
                                /* already hit a minus or digit? */
                                if ((mflag == -1) || (n != 0))
                                        break;
                                mflag = -1;
                        } else {
                                n = n * 10 + (c - '0');
                        }
                        if ((n == 0) && (mflag == -1))  /* lonely - */
                                mlwrite("Arg:");
                        else
                                mlwrite("Arg: %d", n * mflag);

                        c = getcmd();   /* get the next key */
                }
                n = n * mflag;  /* figure in the sign */
        }

        /* do ^U repeat argument processing */

        if (c == reptc) {       /* ^U, start argument   */
                f = TRUE;
                n = 4;          /* with argument of 4 */
                mflag = 0;      /* that can be discarded. */
                mlwrite("Arg: 4");
                while (((c = getcmd()) >= '0' && c <= '9') || c == reptc
                       || c == '-') {
                        if (c == reptc)
                                if ((n > 0) == ((n * 4) > 0))
                                        n = n * 4;
                                else
                                        n = 1;
                        /*
                         * If dash, and start of argument string, set arg.
                         * to -1.  Otherwise, insert it.
                         */
                        else if (c == '-') {
                                if (mflag)
                                        break;
                                n = 0;
                                mflag = -1;
                        }
                        /*
                         * If first digit entered, replace previous argument
                         * with digit and set sign.  Otherwise, append to arg.
                         */
                        else {
                                if (!mflag) {
                                        n = 0;
                                        mflag = 1;
                                }
                                n = 10 * n + c - '0';
                        }
                        mlwrite("Arg: %d",
                                (mflag >= 0) ? n : (n ? -n : -1));
                }
                /*
                 * Make arguments preceded by a minus sign negative and change
                 * the special argument "^U -" to an effective "^U -1".
                 */
                if (mflag == -1) {
                        if (n == 0)
                                n++;
                        n = -n;
                }
        }

        /* and execute the command */
        execute(c, f, n);
        goto loop;
}

/*
 * Initialize all of the buffers and windows. The buffer name is passed down
 * as an argument, because the main routine may have been told to read in a
 * file by default, and we want the buffer name to be right.
 */
void edinit(char *bname)
{
        struct buffer *bp;
        struct window *wp;

        bp = bfind(bname, TRUE, 0);             /* First buffer         */
        blistp = bfind("/List", TRUE, BFINVS); /* Buffer list buffer   */
        wp = (struct window *)malloc(sizeof(struct window)); /* First window */
        if (bp == NULL || wp == NULL || blistp == NULL)
                exit(1);
        curbp = bp;             /* Make this current    */
        wheadp = wp;
        curwp = wp;
        wp->w_wndp = NULL;      /* Initialize window    */
        wp->w_bufp = bp;
        bp->b_nwnd = 1;         /* Displayed.           */
        wp->w_linep = bp->b_linep;
        wp->w_dotp = bp->b_linep;
        wp->w_doto = 0;
        wp->w_markp = NULL;
        wp->w_marko = 0;
        wp->w_toprow = 0;
#if     COLOR
        /* initalize colors to global defaults */
        wp->w_fcolor = gfcolor;
        wp->w_bcolor = gbcolor;
#endif
        wp->w_ntrows = term.t_nrow - 1; /* "-1" for mode line.  */
        wp->w_force = 0;
        wp->w_flag = WFMODE | WFHARD;   /* Full.                */
}

/*
 * This is the general command execution routine. It handles the fake binding
 * of all the keys to "self-insert". It also clears out the "thisflag" word,
 * and arranges to move it to the "lastflag", so that the next command can
 * look at it. Return the status of command.
 */
int execute(int c, int f, int n)
{
        int status;
        fn_t execfunc;
        char *pbp;

        /* if the keystroke is a bound function...do it */

        execfunc = getbind(c, &pbp);
        if (execfunc != NULL) {
                thisflag = 0;
/* GGR - implement re-execute */
                if (inreex) {
                        if ((execfunc == fisearch) || (execfunc == forwsearch))
                             execfunc = forwhunt;
                        if ((execfunc == risearch) || (execfunc == backsearch))
                             execfunc = backhunt;
                }
                else if ((execfunc != reexecute) && (execfunc != nullproc)
                        && (kbdmode != PLAY)) {
                        clast = c;
                        flast = f;
                        nlast = n;
                }
                if (pbp != NULL) input_waiting = pbp;
                else input_waiting = NULL;
                status = (*execfunc) (f, n);
                input_waiting = NULL;
                lastflag = thisflag;
/* GGR - abort keyboard macro at point of error */
                if ((kbdmode == PLAY) & !status) kbdmode = STOP;

                return status;
        }

        /*
         * If a space was typed, fill column is defined, the argument is non-
         * negative, wrap mode is enabled, and we are now past fill column,
         * and we are not read-only, perform word wrap.
         */
        if (c == ' ' && (curwp->w_bufp->b_mode & MDWRAP) && fillcol > 0 &&
            n >= 0 && getccol(FALSE) > fillcol &&
            (curwp->w_bufp->b_mode & MDVIEW) == FALSE)
                execute(META | SPEC | 'W', FALSE, 1);

#if     PKCODE
        if ((c >= 0x20 && c <= 0x7E)    /* Self inserting.      */
#if     IBMPC
            || (c >= 0x80 && c <= 0xFE)) {
#else
#if     BSD || USG       /* 8BIT P.K. */
            || (c >= 0xA0 && c <= 0x10FFFF)) {
#else
            ) {
#endif
#endif
#else
        if ((c >= 0x20 && c <= 0xFF)) { /* Self inserting.      */
#endif

/* GGR - Implement Phonetic Translation iff we are about to self-insert.
 * If the mode is active call the handler.
 * If this returns TRUE then the character has been handled such that
 * we do not need to insert it.
 */
                if (curbp->b_mode & MDPHON) {
                    if (ptt_handler(c)) return TRUE;
                }

                if (n <= 0) {   /* Fenceposts.          */
                        lastflag = 0;
                        return n < 0 ? FALSE : TRUE;
                }
                thisflag = 0;   /* For the future.      */

                /* if we are in overwrite mode, not at eol,
                   and next char is not a tab or we are at a tab stop,
                   delete a char forword                        */
                if (curwp->w_bufp->b_mode & MDOVER &&
                    curwp->w_doto < curwp->w_dotp->l_used &&
                    (lgetc(curwp->w_dotp, curwp->w_doto) != '\t' ||
                     (curwp->w_doto) % 8 == 7))
                        ldelchar(1, FALSE);

                /* do the appropriate insertion */
                if (c == '}' && (curbp->b_mode & MDCMOD) != 0)
                        status = insbrace(n, c);
                else if (c == '#' && (curbp->b_mode & MDCMOD) != 0)
                        status = inspound();
                else
                        status = linsert(n, c);

#if     CFENCE
                /* check for CMODE fence matching */
                if ((c == '}' || c == ')' || c == ']') &&
                    (curbp->b_mode & MDCMOD) != 0)
                        fmatch(c);
#endif

                /* check auto-save mode */
                if (curbp->b_mode & MDASAVE)
                        if (--gacount == 0) {
                                /* and save the file if needed */
                                upscreen(FALSE, 0);
                                filesave(FALSE, 0);
                                gacount = gasave;
                        }

                lastflag = thisflag;
                return status;
        }
        TTbeep();
        mlwrite(MLpre "Key not bound" MLpost);  /* complain             */
        lastflag = 0;                           /* Fake last flags.     */
        return FALSE;
}

/*
 * Fancy quit command, as implemented by Norm. If any buffer has changed
 * do a write on that buffer and exit uemacs, otherwise simply exit.
 */
int quickexit(int f, int n)
{
        struct buffer *bp;      /* scanning pointer to buffers */
        struct buffer *oldcb;   /* original current buffer */
        int status;

        if (mbstop())           /* GGR - disallow in minibuffer */
                return(FALSE);

        oldcb = curbp;          /* save in case we fail */

        bp = bheadp;
        while (bp != NULL) {
            if ((bp->b_flag & BFCHG) != 0       /* Changed.             */
                && (bp->b_flag & BFTRUNC) == 0  /* Not truncated P.K.   */
                && (bp->b_flag & BFINVS) == 0) {/* Real.                */
                    curbp = bp;                 /* make that buffer cur */
                mlwrite(MLpre "Saving %s" MLpost, bp->b_fname);
                mlwrite("\n");              /* So user can see filename */
                if ((status = filesave(f, n)) != TRUE) {
                    curbp = oldcb;          /* restore curbp */
                    sleep(1);
                    redraw(FALSE, 0);       /* Redraw - remove filenames */
                    return status;
                }
            }
            bp = bp->b_bufp;        /* on to the next buffer */
        }
        quit(f, n);                     /* conditionally quit   */
        return TRUE;
}

static void emergencyexit(int signr)
{
        quickexit(FALSE, 0);
        quit(TRUE, 0);
}

/*
 * Quit command. If an argument, always quit. Otherwise confirm if a buffer
 * has been changed and not written out. Normally bound to "C-X C-C".
 */
int quit(int f, int n)
{
        int s;

        if (f != FALSE          /* Argument forces it.  */
            || anycb() == FALSE /* All buffers clean.   */
            || (s =             /* User says it's OK.   */
                mlyesno("Modified buffers exist. Leave anyway")) == TRUE) {
#if     FILOCK && (BSD || SVR4)
                if (lockrel() != TRUE) {
                        ttput1c('\n');
                        ttput1c('\r');
                        TTclose();
                        TTkclose();
                        exit(1);
                }
#endif
                vttidy();
                if (f)
                        exit(n);
                else
                        exit(GOOD);
        }
        mlwrite("");
        return s;
}

/*
 * Begin a keyboard macro.
 * Error if not at the top level in keyboard processing. Set up variables and
 * return.
 */
int ctlxlp(int f, int n)
{
        if (kbdmode != STOP) {
                mlwrite("%%Macro already active");
                return FALSE;
        }
        mlwrite(MLpre "Start macro" MLpost);
        kbdptr = &kbdm[0];
        kbdend = kbdptr;
        kbdmode = RECORD;
        return TRUE;
}

/*
 * End keyboard macro. Check for the same limit conditions as the above
 * routine. Set up the variables and return to the caller.
 */
int ctlxrp(int f, int n)
{
        if (kbdmode == STOP) {
                mlwrite("%%Macro not active");
                return FALSE;
        }
        if (kbdmode == RECORD) {
                mlwrite(MLpre "End macro" MLpost);
                kbdmode = STOP;
        }
        return TRUE;
}

/*
 * Execute a macro.
 * The command argument is the number of times to loop. Quit as soon as a
 * command gets an error. Return TRUE if all ok, else FALSE.
 */
int ctlxe(int f, int n)
{
        if (kbdmode != STOP) {
                mlwrite("%%Macro already active");
                return FALSE;
        }
        if (n <= 0)
                return TRUE;
        kbdrep = n;             /* remember how many times to execute */
        kbdmode = PLAY;         /* start us in play mode */
        kbdptr = &kbdm[0];      /*    at the beginning */
        return TRUE;
}

/*
 * Abort.
 * Beep the beeper. Kill off any keyboard macro, etc., that is in progress.
 * Sometimes called as a routine, to do general aborting of stuff.
 */
int ctrlg(int f, int n)
{
        TTbeep();
        kbdmode = STOP;
        mlwrite(MLpre "Aborted" MLpost);
        return ABORT;
}

/*
 * tell the user that this command is illegal while we are in
 * VIEW (read-only) mode
 */
int rdonly(void)
{
        TTbeep();
        mlwrite(MLpre "Key illegal in VIEW mode" MLpost);
        return FALSE;
}

int resterr(void)
{
        TTbeep();
        mlwrite(MLpre "That command is RESTRICTED" MLpost);
        return FALSE;
}

/* user function that does NOTHING */
int nullproc(int f, int n)
{
        return TRUE;
}

/* dummy function for binding to meta prefix */
int metafn(int f, int n)
{
        return TRUE;
}

/* dummy function for binding to control-x prefix */
int cex(int f, int n)
{
        return TRUE;
}

/* dummy function for binding to universal-argument */
int unarg(int f, int n)
{
        return TRUE;
}

/*****          Compiler specific Library functions     ****/

#if     RAMSIZE
/*      These routines will allow me to track memory usage by placing
        a layer on top of the standard system malloc() and free() calls.
        with this code defined, the environment variable, $RAM, will
        report on the number of bytes allocated via malloc.

        with SHOWRAM defined, the number is also posted on the
        end of the bottom mode line and is updated whenever it is changed.
*/

#undef  malloc
#undef  free

char *allocate(unsigned nbytes)
/* allocate nbytes and track */
{
        char *mp;               /* ptr returned from malloc */
        char *malloc();

        mp = malloc(nbytes);
        if (mp) {
                envram += nbytes;
#if     RAMSHOW
                dspram();
#endif
        }

        return mp;
}

void release(char *mp)
/* release malloced memory and track */
{
        unsigned *lp;           /* ptr to the long containing the block size */

        if (mp) {               /* update amount of ram currently malloced */
                lp = ((unsigned *) mp) - 1;
                envram -= (long) *lp - 2;
                free(mp);
#if     RAMSHOW
                dspram();
#endif
        }
}

#if     RAMSHOW
void dspram(void)
/* display the amount of RAM currently malloced */
{
        char mbuf[20];
        char *sp;

        TTmove(term.t_nrow - 1, 70);
#if     COLOR
        TTforg(7);
        TTbacg(0);
#endif
        sprintf(mbuf, "[%lu]", envram);
        sp = &mbuf[0];
        while (*sp)
                ttput1c(*sp++);
        TTmove(term.t_nrow, 0);
        movecursor(term.t_nrow, 0);
}
#endif
#endif

/*      On some primitave operation systems, and when emacs is used as
        a subprogram to a larger project, emacs needs to de-alloc its
        own used memory
*/

#if     CLEAN

/*
 * cexit()
 *
 * int status;          return status of emacs
 */
int cexit(int status)
{
        struct buffer *bp;      /* buffer list pointer */
        struct window *wp;      /* window list pointer */
        struct window *tp;      /* temporary window pointer */

        /* first clean up the windows */
        wp = wheadp;
        while (wp) {
                tp = wp->w_wndp;
                free(wp);
                wp = tp;
        }
        wheadp = NULL;

        /* then the buffers */
        bp = bheadp;
        while (bp) {
                bp->b_nwnd = 0;
                bp->b_flag = 0; /* don't say anything about a changed buffer! */
                zotbuf(bp);
                bp = bheadp;
        }

        /* and the kill buffer */
        kdelete();

        /* and the video buffers */
        vtfree();

#undef  exit
        exit(status);
}
#endif

/* GGR
 * Function to implement re-execute last command
 */
int reexecute(int f, int n)
{
        int reloop;

        inreex = TRUE;
        /* We can't just multiply n's. Must loop. */
        for (reloop = 1; reloop<=n; ++reloop) {
                execute(clast, flast, nlast);
        }
        inreex = FALSE;
        return(TRUE);
}

/* GGR
 * Extend the size of the key_table list.
 * If input arg is non-zero use that, otherwise extend by the
 * defined increment and update keytab_alloc_ents.
 */
static struct key_tab endl_keytab = {ENDL_KMAP, 0, {NULL}};
static struct key_tab ends_keytab = {ENDS_KMAP, 0, {NULL}};

void extend_keytab(int n_ents) {

    int init_from;
    if (n_ents == 0) {
        init_from = keytab_alloc_ents;
        keytab_alloc_ents += KEYTAB_INCR;
    }
    else {      /* Only happens at start */
        init_from = 0;
        keytab_alloc_ents = n_ents;
    }
    keytab = realloc(keytab, keytab_alloc_ents*sizeof(struct key_tab));
    for (int i = init_from; i < keytab_alloc_ents - 1; i++)
        keytab[i] = endl_keytab;
    keytab[keytab_alloc_ents - 1] = ends_keytab;

    return;
}
