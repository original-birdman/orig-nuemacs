/*      bind.c
 *
 *      This file is for functions having to do with key bindings,
 *      descriptions, help commands and startup file.
 *
 *      Written 11-feb-86 by Daniel Lawrence
 *      Modified by Petri Kutvonen
 */

#include <stdio.h>
#include <unistd.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "epath.h"
#include "line.h"
#include "util.h"

int help(int f, int n)
{                               /* give me some help!!!!
                                   bring up a fake buffer and read the help file
                                   into it with view mode                 */
        struct window *wp;      /* scaning pointer to windows */
        struct buffer *bp;      /* buffer pointer to help */
        char *fname = NULL;     /* ptr to file returned by flook() */

        if (mbstop())           /* GGR - disallow in minibuffer */
                return(FALSE);

        /* first check if we are already here */
        bp = bfind(init_files.help, FALSE, BFINVS); /* GGR - epath.h setting */

        if (bp == NULL) {
                fname = flook(init_files.help, FALSE, INTABLE);
                if (fname == NULL) {
                        mlwrite(MLpre "Help file is not online" MLpost);
                        return FALSE;
                }
        }

        /* split the current window to make room for the help stuff */
        if (splitwind(FALSE, 1) == FALSE)
                return FALSE;

        if (bp == NULL) {
                /* and read the stuff in */
                /* GGR - unset pathexpand around call */
                pathexpand = FALSE;
                int res = getfile(fname, FALSE);
                pathexpand = TRUE;
                if (res == FALSE) return(FALSE);
        } else
                swbuffer(bp);

        /* make this window in VIEW mode, update all mode lines */
        curwp->w_bufp->b_mode |= MDVIEW;
        curwp->w_bufp->b_flag |= BFINVS;
        wp = wheadp;
        while (wp != NULL) {
                wp->w_flag |= WFMODE;
                wp = wp->w_wndp;
        }
        return TRUE;
}

int deskey(int f, int n)
{                               /* describe the command for a certain key */
        int c;          /* key to describe */
        char *ptr;      /* string pointer to scan output strings */
        char outseq[NSTRING];   /* output buffer for command sequence */
        char *pbp;

        /* prompt the user to type us a key to describe */
        mlwrite(": describe-key ");

        /* get the command sequence to describe
           change it to something we can print as well */
        cmdstr(c = getckey(FALSE), outseq);
        /* and dump it out */
        mlputs(outseq);
        mlputs(" ");

        /* find the right ->function */
        if ((ptr = getfname(getbind(c, &pbp))) == NULL)
                ptr = "Not Bound";

        /* output the command sequence */
        mlputs(ptr);

        /* Add buffer-name, if set */
        if (pbp) {
            mlputs(" ");
            mlputs(pbp);
        }
        mpresf = TRUE;      /* GGR */
        return TRUE;
}

/*
 * bindtokey:
 *      add a new key to the key binding table
 *
 * int f, n;            command arguments [IGNORED]
 */
int bindtokey(int f, int n)
{
        unsigned int c;         /* command key to bind */
        fn_t kfunc;             /* ptr to the requested function to bind to */
        struct key_tab *ktp;    /* pointer into the command table */
        int found;              /* matched command flag */
        char outseq[80];        /* output buffer for keystroke sequence */
        struct key_tab *destp;  /* Where to copy the name and type */

        /* prompt the user to type in a key to bind */
        if (!clexec) {
            mlwrite(": bind-to-key ");
            mpresf = TRUE;          /* GGR */
        }

        /* get the function name to bind it to */
        kfunc = getname();
        if (kfunc == NULL) {
                if (!clexec) mlwrite(MLpre "No such function" MLpost);
                return FALSE;
        }
        if (!clexec) mlputs(" ");

        /* get the command sequence to bind */
        c = getckey((kfunc == metafn) || (kfunc == cex) ||
                    (kfunc == unarg) || (kfunc == ctrlg));

        /* change it to something we can print as well */
        cmdstr(c, outseq);

        /* and dump it out */
        if (!clexec) mlputs(outseq);

        /* if the function is a prefix key */
        if (kfunc == metafn || kfunc == cex ||
            kfunc == unarg || kfunc == ctrlg) {

/* Search for an existing binding for the prefix key
 */
                ktp = keytab;
                found = FALSE;
                while (ktp->k_type != ENDL_KMAP) {
                        if ((ktp->k_type == FUNC_KMAP) &&
                            (ktp->hndlr.k_fp == kfunc))
                                unbindchar(ktp->k_code);
                        ++ktp;
                }

                /* reset the appropriate global prefix variable */
                if (kfunc == metafn)
                        metac = c;
                if (kfunc == cex)
                        ctlxc = c;
                if (kfunc == unarg)
                        reptc = c;
                if (kfunc == ctrlg)
                        abortc = c;
        }

        /* search the table to see if it exists */
        ktp = keytab;
        found = FALSE;
        while (ktp->k_type != ENDL_KMAP) {
                if (ktp->k_code == c) {
                        found = TRUE;
                        break;
                }
                ++ktp;
        }

        if (found) {            /* it exists, just change it then */
                if (ktp->k_type == PROC_KMAP)
                    free(ktp->hndlr.pbp);       /* Free the name */
                destp = ktp;
        } else {                /* otherwise we need to add it to the end */
                ktp->k_code = c;        /* set keycode */
                destp = ktp;

/* If the list is not exhausted the next one will also be an End-of-List.
 * If it is an End-of-Structure we need to extend, and if we do that we
 * need to handle destp, in case the realloc() moves things.
 * extend_keytab() fills in ENDL_KMAP and ENDS_KMAP entries.
 */
                ++ktp;
                if (ktp->k_type == ENDS_KMAP) {
                    int destp_offs = destp - keytab;
                    int ktp_offs = ktp - keytab;
                    extend_keytab(0);
                    destp = keytab + destp_offs;
                    ktp = keytab + ktp_offs;
                }
        }
        destp->hndlr.k_fp = kfunc;      /* and the function pointer */
        destp->k_type = FUNC_KMAP;      /* Set the type */

        mpresf = TRUE;                  /* GGR */
        TTflush();

        return TRUE;
}

/*
 * unbindkey:
 *      delete a key from the key binding table
 *
 * int f, n;            command arguments [IGNORED]
 */
int unbindkey(int f, int n)
{
        int c;                  /* command key to unbind */
        char outseq[80];        /* output buffer for keystroke sequence */

        /* prompt the user to type in a key to unbind */
        if (!clexec) {
            mlwrite(": unbind-key ");
            mpresf = TRUE;      /* GGR */
        }

        /* get the command sequence to unbind */
        c = getckey(FALSE);     /* get a command sequence */

        /* change it to something we can print as well */
        cmdstr(c, outseq);

        /* and dump it out */
        if (!clexec) mlputs(outseq);

        /* if it isn't bound, bitch */
        if (unbindchar(c) == FALSE) {
                mlwrite(MLpre "Key not bound" MLpost);
                mpresf = TRUE;  /* GGR */
                return FALSE;
        }
        TTflush();              /* GGR */

        return TRUE;
}


/*
 * unbindchar()
 *
 * int c;               command key to unbind
 */
int unbindchar(int c)
{
        struct key_tab *ktp;   /* pointer into the command table */
        struct key_tab *sktp;  /* saved pointer into the command table */
        int found;             /* matched command flag */

        /* search the table to see if the key exists */
        ktp = keytab;
        found = FALSE;
        while (ktp->k_type != ENDL_KMAP) {
                if (ktp->k_code == c) {
                        found = TRUE;
                        break;
                }
                ++ktp;
        }

        /* if it isn't bound, bitch */
        if (!found)
                return FALSE;

        /* save the pointer and scan to the end of the table */
        sktp = ktp;
        while (ktp->k_type != ENDL_KMAP)
                ++ktp;
        --ktp;                  /* backup to the last legit entry */

        /* copy the last entry to the current one */
        *sktp = *ktp;           /* Copy the whole structure */

        /* null out the last one */
        ktp->k_code = 0;
        ktp->hndlr.k_fp = NULL;
        ktp->k_type = ENDL_KMAP;
        return TRUE;
}

/* describe bindings
 * bring up a fake buffer and list the key bindings
 * into it with view mode
 */
int desbind(int f, int n)
#if     APROP
{
        buildlist(TRUE, "");
        return TRUE;
}

int apro(int f, int n)
{                               /* Apropos (List functions that match a substring) */
        char mstring[NSTRING];  /* string to match cmd names to */
        int status;             /* status return */

        if (mbstop())           /* GGR - disallow in minibuffer */
                return(FALSE);

        status = mlreply("Apropos string: ", mstring, NSTRING - 1);
        if (status != TRUE)
                return status;

        return buildlist(FALSE, mstring);
}

/*
 * build a binding list (limited or full)
 *
 * int type;            true = full list,   false = partial list
 * char *mstring;       match string if a partial list
 */
int buildlist(int type, char *mstring)
#endif
{
        struct window *wp;         /* scanning pointer to windows */
        struct key_tab *ktp;       /* pointer into the command table */
        struct name_bind *nptr;    /* pointer into the name binding table */
        struct buffer *bp;         /* buffer to put binding list into */
        int cpos;                  /* current position to use in outseq */
        char outseq[80];           /* output buffer for keystroke sequence */

        if (mbstop())              /* GGR - disallow in minibuffer */
                return(FALSE);

        /* split the current window to make room for the binding list */
        if (splitwind(FALSE, 1) == FALSE)
                return FALSE;

        /* and get a buffer for it */
        bp = bfind("/Binding list", TRUE, 0);
        if (bp == NULL || bclear(bp) == FALSE) {
                mlwrite("Can not display binding list");
                return FALSE;
        }

        /* let us know this is in progress */
        mlwrite(MLpre "Building binding list" MLpost);

        /* disconect the current buffer */
        if (--curbp->b_nwnd == 0) {     /* Last use.            */
                curbp->b_dotp = curwp->w_dotp;
                curbp->b_doto = curwp->w_doto;
                curbp->b_markp = curwp->w_markp;
                curbp->b_marko = curwp->w_marko;
        }

        /* connect the current window to this buffer */
        curbp = bp;             /* make this buffer current in current window */
        bp->b_mode = 0;         /* no modes active in binding list */
        bp->b_nwnd++;           /* mark us as more in use */
        wp = curwp;
        wp->w_bufp = bp;
        wp->w_linep = bp->b_linep;
        wp->w_flag = WFHARD | WFFORCE;
        wp->w_dotp = bp->b_dotp;
        wp->w_doto = bp->b_doto;
        wp->w_markp = NULL;
        wp->w_marko = 0;

        /* build the contents of this window, inserting it line by line */
        nptr = &names[0];
        while (nptr->n_func != NULL) {

                /* add in the command name */
                strcpy(outseq, nptr->n_name);
                cpos = strlen(outseq);

#if     APROP
                /* if we are executing an apropos command..... */
                if (type == FALSE &&
                    /* and current string doesn't include the search string */
                    strinc(outseq, mstring) == FALSE)
                        goto fail;
#endif
                /* search down any keys bound to this */
                ktp = keytab;
                while (ktp->k_type != ENDL_KMAP) {
                        if ((ktp->k_type == FUNC_KMAP) &&
                            (ktp->hndlr.k_fp == nptr->n_func)) {
                                /* pad out some spaces */
                                while (cpos < 28)
                                        outseq[cpos++] = ' ';

                                /* add in the command sequence */
                                cmdstr(ktp->k_code, outseq+cpos);
                                strcat(outseq, "\n");

                                /* and add it as a line into the buffer */
                                if (linstr(outseq) != TRUE)
                                        return FALSE;

                                cpos = 0;       /* and clear the line */
                        }
                        ++ktp;
                }

                /* if no key was bound, we need to dump it anyway */
                if (cpos > 0) {
                        outseq[cpos++] = '\n';
                        outseq[cpos] = 0;
                        if (linstr(outseq) != TRUE)
                                return FALSE;
                }

fail:           /* and on to the next name */
                ++nptr;
        }

/* Now we go through all the key_table looking for proc buf bindings */

    cpos = 0;
    int found = 0;
    for(ktp = keytab; ktp->k_type != ENDL_KMAP; ++ktp) {
        if (ktp->k_type == PROC_KMAP) {
            if (!found) {
                if (linstr("\nProcedure bindings\n") != TRUE) return FALSE;
                found = 1;
            }
        }
        else continue;
        strcpy(outseq, ktp->hndlr.pbp);
        cpos = strlen(outseq);
        while (cpos < 28) outseq[cpos++] = ' ';
/* Add in the command sequence */
        cmdstr(ktp->k_code, outseq+cpos);
        strcat(outseq, "\n");

/* Add the line into the buffer */
        if (linstr(outseq) != TRUE) return FALSE;
        cpos = 0;       /* and clear the line */
    }


        curwp->w_bufp->b_mode |= MDVIEW;    /* put this buffer view mode */
        curbp->b_flag &= ~BFCHG;            /* don't flag this as a change */
        wp->w_dotp = lforw(bp->b_linep);    /* back to the beginning */
        wp->w_doto = 0;
        wp = wheadp;                        /* and update ALL mode lines */
        while (wp != NULL) {
                wp->w_flag |= WFMODE;
                wp = wp->w_wndp;
        }
        mlwrite("");                        /* clear the mode line */
        return TRUE;
}

#if     APROP

/*
 * does source include sub?
 *
 * char *source;        string to search in
 * char *sub;           substring to look for
 */
int strinc(char *source, char *sub)
{
        char *sp;               /* ptr into source */
        char *nxtsp;            /* next ptr into source */
        char *tp;               /* ptr into substring */

        /* for each character in the source string */
        sp = source;
        while (*sp) {
                tp = sub;
                nxtsp = sp;

                /* is the substring here? */
                while (*tp) {
                        if (*nxtsp++ != *tp)
                                break;
                        else
                                tp++;
                }

                /* yes, return a success */
                if (*tp == 0)
                        return TRUE;

                /* no, onward */
                sp++;
        }
        return FALSE;
}
#endif

/*
 * get a command key sequence from the keyboard
 *
 * int mflag;           going for a meta sequence?
 */
unsigned int getckey(int mflag)
{
        unsigned int c;         /* character fetched */
        char tok[NSTRING];      /* command incoming */

        /* check to see if we are executing a command line */
        if (clexec) {
                macarg(tok);    /* get the next token */
                return stock(tok);
        }

        /* or the normal way */
        if (mflag)
                c = get1key();
        else
                c = getcmd();
        return c;
}

/*
 * execute the startup file
 *
 * char *sfname;        name of startup file (null if default)
 */
int startup(char *sfname)
{
        char *fname;            /* resulting file name to execute */

        /* look up the startup file */
        if (*sfname != 0)
                fname = flook(sfname, TRUE, INTABLE);
        else
                fname = flook(init_files.startup, TRUE, INTABLE);

        /* if it isn't around, don't sweat it */
        if (fname == NULL)
                return TRUE;

        /* otherwise, execute the sucker */
        return dofile(fname);
}

/* GGR - Define a path-separator
 */
static char path_sep =
#if MSDOS
    '\\'
#else
#if V7 | BSD | USG
    '/'
#else
    '\0'
#endif
#endif
;


#if     ENVFUNC
static int along_path(char *fname, char *fspec) {
        char *path;     /* environmental PATH variable */
        char *sp;       /* pointer into path spec */

        /* get the PATH variable */
        path = getenv("PATH");
        if (path != NULL)
                while (*path) {

                        /* build next possible file spec */
                        sp = fspec;
                        while (*path && (*path != PATHCHR))
                                *sp++ = *path++;

                        /* add a terminating dir separator if we need it */
                        if (sp != fspec)
                                *sp++ = path_sep;
                        *sp = 0;
                        strcat(fspec, fname);

                        /* and try it out */
                        if (ffropen(fspec) == FIOSUC) {
                                ffclose();
                                return TRUE;
                        }

                        if (*path == PATHCHR)
                                ++path;
                }
        return FALSE;
}
#endif

/* GGR - function to set pathname from the command-line
 * This overides the compiled-in defaults
 */
void set_pathname(char *cl_string) {
    int slen;
    int add_sep = 0;
    if (path_sep != '\0') {     /* Ensure it ends with it... */
        slen = strlen(cl_string);
        if ((slen > 0) && (cl_string[slen-1] != path_sep)) {
            add_sep = 1;
        }
    }
    pathname[0] = strdup(cl_string);
    if (add_sep) {
        pathname[0] = realloc(pathname[0], slen+2); /* incl. NULL! */
        pathname[0][slen] = path_sep;
        pathname[0][slen+1] = '\0';
    }
    pathname[1] = NULL;
    return;
}

/*
 * Look up the existence of a file along the normal or PATH
 * environment variable. Look first in the HOME directory if
 * asked and possible.
 * GGR - added mode flag detemines whether to look along PATH
 * or in the set list of directories.
 *
 * char *fname;         base file name to search for
 * int hflag;           Look in the HOME environment variable first?
 */
char *flook(char *fname, int hflag, int mode)
{
        char *home;                     /* path to home directory */
        int i;                          /* index */
        static char fspec[NSTRING];     /* full path spec to search */

        pathexpand = FALSE;             /* GGR */

/* If we've been given a pathname, rather than a filename, just use that */

        if (strchr(fname, path_sep)) {
            char *res = NULL;   /* Assume the worst */
            if (ffropen(fname) == FIOSUC) {
                ffclose();
                res = fname;
            }
            pathexpand = TRUE;  /* GGR */
            return res;
        }

#if     ENVFUNC

        if (hflag) {
                home = getenv("HOME");
                if (home != NULL) {
                        /* build home dir file spec */
                        strcpy(fspec, home);
                        char psbuf[2];
                        psbuf[0] = path_sep;
                        psbuf[1] = '\0';
                        strcat(fspec, psbuf);
                        strcat(fspec, fname);

                        /* and try it out */
                        if (ffropen(fspec) == FIOSUC) {
                                ffclose();
                                pathexpand = TRUE;  /* GGR */
                                return fspec;
                        }
                }
        }
#endif

        /* always try the current directory first - if allowed */
        if (allow_current && ffropen(fname) == FIOSUC) {
                ffclose();
                pathexpand = TRUE;                  /* GGR */
                return fname;
        }

/* GGR - use PATH or TABLE according to mode.
 * You want to use ONPATH when looking for a command, but INTABLE
 * when looking for a config files.
 * The caller knows which...
 */
        if (mode == ONPATH) {
            if (along_path(fname, fspec) == TRUE) {
                pathexpand = TRUE;                  /* GGR */
                return fspec;
            }
        }

        if (mode == INTABLE) {
        /* look it up via the old table method */
            for (i = 0;; i++) {
                if (pathname[i] == NULL) break;
                strcpy(fspec, pathname[i]);
                strcat(fspec, fname);

                /* and try it out */
                if (ffropen(fspec) == FIOSUC) {
                    ffclose();
                    pathexpand = TRUE;          /* GGR */
                    return fspec;
                }
            }
        }

        pathexpand = TRUE;                          /* GGR */
        return NULL;            /* no such luck */
}

/*
 * change a key command to a string we can print out
 *
 * int c;               sequence to translate
 * char *seq;           destination string for sequence
 */
void cmdstr(int c, char *seq)
{
        char *ptr;      /* pointer into current position in sequence */

        ptr = seq;

        /* apply meta sequence if needed */
        if (c & META) {
                *ptr++ = 'M';
                *ptr++ = '-';
        }

        /* apply ^X sequence if needed */
        if (c & CTLX) {
                *ptr++ = '^';
                *ptr++ = 'X';
        }

        /* apply SPEC sequence if needed */
        if (c & SPEC) {
                *ptr++ = 'F';
                *ptr++ = 'N';
        }

        /* apply control sequence if needed */
        if (c & CONTROL) {
                *ptr++ = '^';
        }

        /* and output the final sequence */

        /* GGR - handle the SP for space which we allow */
        if ((c & 255) == ' ') {
                *ptr++ = 'S';
                *ptr++ = 'P';
        }
        else
                *ptr++ = c & 255;   /* strip the prefixes */

        *ptr = 0;                   /* terminate the string */
}

/*
 * This function looks a key binding up in the binding table
 * GGR - it now sets the buffer name for PROC_KMAP entries
 *
 * int c;               key to find what is bound to it
 */
fn_t getbind(int c, char **pbp)
{
        struct key_tab *ktp;

/* Look through the key table. */

        for (ktp = keytab; ktp->k_type != ENDL_KMAP; ++ktp) {
                if (ktp->k_code == c) {
                    if (ktp->k_type == FUNC_KMAP) {
                        *pbp = NULL;
                        return ktp->hndlr.k_fp;
                    }
                    else if (ktp->k_type == PROC_KMAP) {
                        *pbp = ktp->hndlr.pbp;
                        return execproc;
                    }
                }
        }
        *pbp = NULL;
        return NULL;        /* No such binding */
}

/*
 * getfname:
 *      This function takes a ptr to function and gets the name
 *      associated with it.
 */
char *getfname(fn_t func)
{
        struct name_bind *nptr; /* pointer into the name binding table */

        /* skim through the table, looking for a match */
        nptr = &names[0];
        while (nptr->n_func != NULL) {
                if (nptr->n_func == func)
                        return nptr->n_name;
                ++nptr;
        }
        return NULL;
}

/*
 * match fname to a function in the names table
 * and return any match or NULL if none
 *
 * char *fname;         name to attempt to match
 */
int (*fncmatch(char *fname)) (int, int)
{
        struct name_bind *ffp;  /* pointer to entry in name binding table */

        /* scan through the table, returning any match */
        ffp = &names[0];
        while (ffp->n_func != NULL) {
                if (strcmp(fname, ffp->n_name) == 0)
                        return ffp->n_func;
                ++ffp;
        }
        return NULL;
}

/*
 * stock:
 *      String key name TO Command Key
 *
 * char *keyname;       name of key to translate to Command key form
 */
unsigned int stock(char *keyname)
{
        unsigned int c; /* key sequence to return */

        /* GGR - allow different bindings for 1char UPPER and lower */
        int noupper = (strlen(keyname) == 1);

        /* parse it up */
        c = 0;

        /* first, the META prefix */
        if (*keyname == 'M' && *(keyname + 1) == '-') {
                c = META;
                keyname += 2;
        }

        /* next the function prefix */
        if (*keyname == 'F' && *(keyname + 1) == 'N') {
                c |= SPEC;
                keyname += 2;
        }

        /* control-x as well... (but not with FN) */
        if (*keyname == '^' && *(keyname + 1) == 'X' && !(c & SPEC)) {
                c |= CTLX;
                keyname += 2;
        }

        /* a control char? */
        if (*keyname == '^' && *(keyname + 1) != 0) {
                c |= CONTROL;
                ++keyname;
        }
        if (*keyname < 32) {
                c |= CONTROL;
                *keyname += 'A';
        }
        /* GGR - allow SP for space by putting it there... */
        if (*keyname == 'S' && *(keyname + 1) == 'P') {
                ++keyname;
                *keyname = ' ';
        }


        /* make sure we are not lower case (not with function keys) */
        if (*keyname >= 'a' && *keyname <= 'z' && !(c & SPEC)
              && !(noupper))            /* GGR */
                *keyname -= 32;

        /* the final sequence... */
        c |= *keyname;
        return c;
}

/*
 * string key name to binding name....
 *
 * char *skey;          name of key to get binding for
 */
char *transbind(char *skey)
{
        char *bindname, *pbp;

        bindname = getfname(getbind(stock(skey), &pbp));
        if (bindname == NULL)
                bindname = "ERROR";

        return bindname;
}

/* GGR added
 * buffertokey:
 *      Add a new key to the key binding table to invoke a buffer.
 *      Much copied from bindtokey B()and execproc()
 *
 * int f, n;            command arguments [IGNORED]
 */
int buffertokey(int f, int n)
{
    unsigned int c;         /* command key to bind */
    struct key_tab *ktp;    /* pointer into the command table */
    int found;              /* matched command flag */
    char bname[NBUFN+1];    /* buffer name */
    char outseq[80];        /* output buffer for keystroke sequence */
    struct buffer *bp;      /* ptr to buffer to execute */
    int status;             /* status return */
    struct key_tab *destp;  /* Where to copy the name and type */

/* Prompt the user to type in a key to bind */

    if (!clexec) {
        mlwrite(": buffer-to-key ");
        mpresf = TRUE;          /* GGR */
    }

/* Fudge in the tag, then get the name of the buffer to invoke.
 * The maximum length is 16 chars *including* the NUL.
 * mlreply (via either token or getstring) includes the NUL in the size
 * and we are actually writing this in from offset 1, so we allow NBUFN
 * chars for that and complain if the reply fills the buffer (as that will
 * make the name too long, and we don't want unexpected truncation meaning
 * we have two different names ending up the same.
 */
    bname[0] = '/';
    if ((status = mlreply("macro buffer: ", bname+1, NBUFN)) != TRUE)
        return status;
    if (strlen(bname) >= NBUFN) {
         mlforce("Procedure name too long: %s. Ignored.", bname);
         sleep(1);
         return TRUE;       /* Don't abort start-up file */
    }

/* Check that this buffer exists */
    if ((bp = bfind(bname, FALSE, 0)) == NULL) {
        mlwrite("No such exec procedure %s", bname);
        return FALSE;
    }

    if (!clexec) mlputs("key sequence: ");

/* get the command sequence to bind */

    c = getckey(0);

/* Change it to something we can print as well and dump it out */

    cmdstr(c, outseq);
    if (!clexec) mlputs(outseq);

/* Search the table to see if it exists */

    ktp = keytab;
    found = FALSE;
    while (ktp->k_type != ENDL_KMAP) {
        if (ktp->k_code == c) {
            found = TRUE;
            break;
        }
        ++ktp;
    }

    if (found) {                    /* If it exists, change it... */
        if (ktp->k_type == FUNC_KMAP) /* Need to allocate space for name */
            ktp->hndlr.pbp = malloc(NBUFN);
        destp = ktp;
    }
    else {                          /* ...else add it at the end */
        ktp->k_code = c;            /* set keycode */
        ktp->hndlr.pbp = malloc(NBUFN);
        destp = ktp;

/* If the list is not exhausted the next one will also be an End-of-List.
 * If it is an End-of-Structure we need to extend, and if we do that we
 * need to handle destp, in case the realloc() moves things.
 * extend_keytab() fills in ENDL_KMAP and ENDS_KMAP entries.
 */
        ++ktp;
        if (ktp->k_type == ENDS_KMAP) {
            int destp_offs = destp - keytab;
            int ktp_offs = ktp - keytab;
            extend_keytab(0);
            destp = keytab + destp_offs;
            ktp = keytab + ktp_offs;
        }
        ktp->k_code = 0;
        ktp->hndlr.pbp = NULL;
        ktp->k_type = ENDL_KMAP;
    }
    destp->k_type = PROC_KMAP;
    strcpy(destp->hndlr.pbp, bname+1); /* ...and copy in name */

    mpresf = TRUE;                  /* GGR */
    TTflush();

    return TRUE;
}
