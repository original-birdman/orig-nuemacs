/*      file.c
 *
 *      The routines in this file handle the reading, writing
 *      and lookup of disk files.  All of details about the
 *      reading and writing of the disk are in "fileio.c".
 *
 *      modified by Petri Kutvonen
 */

#include <stdio.h>
#include <unistd.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"

/* Max number of lines from one file. */
#define MAXNLINE 10000000

/*
 * Read a file into the current
 * buffer. This is really easy; all you do is
 * find the name of the file, and call the standard
 * "read a file into the current buffer" code.
 * Bound to "C-X C-R".
 */
int fileread(int f, int n)
{
        int s;
        char fname[NFILEN];

        if (restflag)           /* don't allow this command if restricted */
                return resterr();
/* GGR - return any current filename for the buffer on <CR>.
 *      Useful for ^X^R <CR> to re-read current file on erroneous change
 */
        s = mlreply("Read file: ", fname, NFILEN);
        if (s == ABORT)
                return(s);
        else if (s == FALSE) {
                if (strlen(curbp->b_fname) == 0)
                        return(s);
                else
                        strcpy(fname, curbp->b_fname);
        }
        return readin(fname, TRUE);
}

/*
 * Insert a file into the current
 * buffer. This is really easy; all you do it
 * find the name of the file, and call the standard
 * "insert a file into the current buffer" code.
 * Bound to "C-X C-I".
 */
int insfile(int f, int n)
{
        int s;
        char fname[NFILEN];

        if (restflag)           /* don't allow this command if restricted */
                return resterr();
        if (curbp->b_mode & MDVIEW)     /* don't allow this command if      */
                return rdonly();        /* we are in read only mode     */
        if ((s = mlreply("Insert file: ", fname, NFILEN)) != TRUE)
                return s;
        if ((s = ifile(fname)) != TRUE)
                return s;
        return reposition(TRUE, -1);
}

/*
 * Select a file for editing.
 * Look around to see if you can find the
 * fine in another buffer; if you can find it
 * just switch to the buffer. If you cannot find
 * the file, create a new buffer, read in the
 * text, and switch to the new buffer.
 * Bound to C-X C-F.
 */
int filefind(int f, int n)
{
        char fname[NFILEN];     /* file user wishes to find */
        int s;          /* status return */
        if (restflag)           /* don't allow this command if restricted */
                return resterr();
        if ((s = mlreply("Find file: ", fname, NFILEN)) != TRUE) {
                return s;
        }
        return getfile(fname, TRUE);
}

int viewfile(int f, int n)
{                               /* visit a file in VIEW mode */
        char fname[NFILEN];     /* file user wishes to find */
        int s;          /* status return */
        struct window *wp;      /* scan for windows that need updating */

        if (restflag)           /* don't allow this command if restricted */
                return resterr();
        if ((s = mlreply("View file: ", fname, NFILEN)) != TRUE)
                return s;
        s = getfile(fname, FALSE);
        if (s) {                /* if we succeed, put it in view mode */
                curwp->w_bufp->b_mode |= MDVIEW;

                /* scan through and update mode lines of all windows */
                wp = wheadp;
                while (wp != NULL) {
                        wp->w_flag |= WFMODE;
                        wp = wp->w_wndp;
                }
        }
        return s;
}

#if     CRYPT
static int resetkey(void) { /* Reset the encryption key if needed */
    int s;              /* return status */

    cryptflag = FALSE;  /* Turn off the encryption flag */

    if (curbp->b_mode & MDCRYPT) {  /* If we are in crypt mode */
        if (curbp->b_keylen == 0) {
            s = set_encryption_key(FALSE, 0);
            if (s != TRUE) return s;
        }
        cryptflag = TRUE;           /* let others know... */

/* Set up the key to be used! */
        myencrypt(NULL, 0);
        myencrypt(curbp->b_key, curbp->b_keylen);

/* curbp->b_key is encrypted when you set it, and since this is done in
 * place we've just decrypted it. So we re-encrypt it.
 * Since this uses a symmetric cipher the running key ends up the same
 * Taking a copy would mean we could do only one set of encrypting, but
 * would then leave us with the decrypted key in memory, and we're trying
 *  to avoid that...
 */
        myencrypt(NULL, 0);
        myencrypt(curbp->b_key, curbp->b_keylen);
    }
    return TRUE;
}
#endif

/*
 * getfile()
 *
 * char fname[];        file name to find
 * int lockfl;          check the file for locks?
 */
int getfile(char *fname, int lockfl) {
    struct buffer *bp;
    struct line *lp;
    int i;
    int s;
    char bname[NBUFN];      /* buffer name to put file */

#if     MSDOS
    mklower(fname);         /* msdos isn't case sensitive */
#endif
    for (bp = bheadp; bp != NULL; bp = bp->b_bufp) {
        if ((bp->b_flag & BFINVS) == 0 && strcmp(bp->b_fname, fname) == 0) {
            swbuffer(bp);
            lp = curwp->w_dotp;
            i = curwp->w_ntrows / 2;
            while (i-- && lback(lp) != curbp->b_linep) lp = lback(lp);
            curwp->w_linep = lp;
            curwp->w_flag |= WFMODE | WFHARD;
            cknewwindow();
            mlwrite(MLpre "Old buffer" MLpost);
            return TRUE;
        }
    }
    makename(bname, fname); /* New buffer name.     */
    while ((bp = bfind(bname, FALSE, 0)) != NULL) {
/* Old buffer name conflict code */
        s = mlreply("Buffer name: ", bname, NBUFN);
        if (s == ABORT) return s;   /* ^G to just quit      */
        if (s == FALSE) {           /* CR to clobber it     */
            makename(bname, fname);
            break;
        }
    }
    if (bp == NULL && (bp = bfind(bname, TRUE, 0)) == NULL) {
        mlwrite("Cannot create buffer");
        return FALSE;
    }
    if (--curbp->b_nwnd == 0) {     /* Undisplay.           */
        curbp->b_dotp = curwp->w_dotp;
        curbp->b_doto = curwp->w_doto;
        curbp->b_markp = curwp->w_markp;
        curbp->b_marko = curwp->w_marko;
        curbp->b_fcol = curwp->w_fcol;
    }
/* GGR - remember last buffer */
    if (!inmb) strcpy(savnam, curbp->b_bname);

    curbp = bp;                     /* Switch to it.        */
    curwp->w_bufp = bp;
    curbp->b_nwnd++;
    s = readin(fname, lockfl);      /* Read it in.          */
    cknewwindow();
    return s;
}

/* GGR routine to handle file-hooks in one place.
 * We now look for /file-hooks-<<sfx>> after /file-hooks.
 * And now done in readin() only
 */
static void handle_filehooks(char *fname) {
    struct buffer *sb;
    if ((sb = bfind("/file-hooks", FALSE, 0)) != NULL) dobuf(sb);
    char *sfx = strrchr(fname, '.');
    if (sfx && strlen(sfx) <= 19) {     /* Max bufname is 32, incl NUL */
        sfx++;                          /* Skip over '.' */
        char sfx_bname[NBUFN];
        strcpy(sfx_bname, "/file-hooks-");
        strcat(sfx_bname, sfx);
        if ((sb = bfind(sfx_bname, FALSE, 0)) != NULL) dobuf(sb);
    }
    return;
}

/*
 * Read file "fname" into the current buffer, blowing away any text
 * found there - called by both the read and find commands.
 * Return the final status of the read.
 * Also called by the main routine to read in a file specified on the
 * command line as an argument.
 * The command bound to M-FNR is called after the buffer is set up
 * and before it is read.
 *
 * char fname[];        name of file to read
 * int lockfl;          check for file locks?
 */
int readin(char *fname, int lockfl) {
    struct line *lp1;
    struct line *lp2;
    struct window *wp;
    struct buffer *bp;
    int s;
    int nline;
    char mesg[NSTRING];

#if FILOCK && (BSD || SVR4)
    if (lockfl && lockchk(fname) == ABORT) {
        s = FIOFNF;
        bp = curbp;
        strcpy(bp->b_fname, "");
        goto out;
    }
#endif
#if CRYPT
    s = resetkey();
    if (s != TRUE) return s;
#endif
    bp = curbp;                             /* Cheap.        */
    if ((s = bclear(bp)) != TRUE) return s; /* Might be old. */
    bp->b_flag &= ~(BFINVS | BFCHG);

/* If this is a translation table, remove any compiled data */

    if ((bp->b_type == BTPHON) && bp->ptt_headp) ptt_free(bp);

/* If activating an inactive buffer, these may be the same and the
 * action of strcpy() is undefined for overlapping strings.
 * On a Mac it will crash...
 */
    if (bp->b_fname != fname) strcpy(bp->b_fname, fname);

/* GGR - lockfl is TRUE for the cases where we want file-hooks to run.
 * Accidental, but convenient.
 */
    if (lockfl) handle_filehooks(fname);

/* let a user macro get hold of things...if he wants */
    execute(META | SPEC | 'R', FALSE, 1);

    if ((s = ffropen(fname)) == FIOERR) goto out;   /* Hard file open. */
    if (s == FIOFNF) {                              /* File not found. */
        mlwrite(MLpre "New file" MLpost);
        goto out;
    }

/* Read the file in */
    if (!silent) mlwrite(MLpre "Reading file" MLpost);  /* GGR */
    nline = 0;
    while ((s = ffgetline()) == FIOSUC) {
        if (nline > MAXNLINE) {
            s = FIOMEM;
            break;
        }
        lp1 = fline;                    /* Allocated by ffgetline() */
        lp2 = lback(curbp->b_linep);
        lp2->l_fp = lp1;
        lp1->l_fp = curbp->b_linep;
        lp1->l_bp = lp2;
        curbp->b_linep->l_bp = lp1;
        ++nline;
        if (!(nline % 300) && !silent)  /* GGR */
            mlwrite(MLpre "Reading file" MLpost " : %d lines", nline);
    }
    ffclose();                          /* Ignore errors. */
    strcpy(mesg, MLpre);
    if (s == FIOERR) {
        strcat(mesg, "I/O ERROR, ");
        curbp->b_flag |= BFTRUNC;
    }
    if (s == FIOMEM) {
        strcat(mesg, "OUT OF MEMORY, ");
        curbp->b_flag |= BFTRUNC;
    }
    sprintf(&mesg[strlen(mesg)], "Read %d line", nline);
    if (nline != 1) strcat(mesg, "s");
    strcat(mesg, MLpost);
    if (!silent) {                      /* GGR */
        mlwrite(mesg);
        if (s == FIOMEM) sleep(1);      /* Let it be seen */
    }

out:
    for (wp = wheadp; wp != NULL; wp = wp->w_wndp) {
        if (wp->w_bufp == curbp) {
            wp->w_linep = lforw(curbp->b_linep);
            wp->w_dotp = lforw(curbp->b_linep);
            wp->w_doto = 0;
            wp->w_markp = NULL;
            wp->w_marko = 0;
            wp->w_flag |= WFMODE | WFHARD;
        }
    }
    if (s == FIOERR || s == FIOFNF) return FALSE;   /* False if error. */
    return TRUE;
}

/*
 * Take a file name, and from it
 * fabricate a buffer name. This routine knows
 * about the syntax of file names on the target system.
 * I suppose that this information could be put in
 * a better place than a line of code.
 */
void makename(char *bname, char *fname)
{
        char *cp1;
        char *cp2;

        cp1 = &fname[0];
        while (*cp1 != 0)
                ++cp1;

#if     MSDOS
        while (cp1 != &fname[0] && cp1[-1] != ':' && cp1[-1] != '\\'
               && cp1[-1] != '/')
                --cp1;
#endif
#if     V7 | USG | BSD
        while (cp1 != &fname[0] && cp1[-1] != '/')
                --cp1;
#endif
        cp2 = &bname[0];
        while (cp2 != &bname[NBUFN - 1] && *cp1 != 0 && *cp1 != ';')
                *cp2++ = *cp1++;
        *cp2 = 0;
}

/*
 * make sure a buffer name is unique
 *
 * char *name;          name to check on
 */
void unqname(char *name)
{
        char *sp;

        /* check to see if it is in the buffer list */
        while (bfind(name, 0, FALSE) != NULL) {

                /* go to the end of the name */
                sp = name;
                while (*sp)
                        ++sp;

/* GGR - Use an algorithm that doesn't easily add characters outside of
 * the defined buffer size.
 * First append a number (if there is free space) and let that go up
 * to 9. Iff we run out of numbers and length, increment the last
 * non-numeric character...
 */
                if (sp == name) {   /* Empty name!! */
                        *sp++ = '0';
                        *sp = 0;
                        continue;
                }
                if ((sp - name) < NBUFN-1) {  /* Space left */
                        *sp++ = '0';
                        *sp = 0;
                        continue;
                }
                char *last_non_numeric = NULL;
                while (--sp > name) {
                    if ((*sp >= '0') && (*sp <= '8')) {
                        break;
                    }
                    if (!last_non_numeric) last_non_numeric = sp;
                }
                if (sp == name) {
                    sp = last_non_numeric;
                    *sp = '0' - 1;
                }
                *sp += 1;       /* Increment where we are now */
        }
}

/*
 * Ask for a file name, and write the
 * contents of the current buffer to that file.
 * Update the remembered file name and clear the
 * buffer changed flag. This handling of file names
 * is different from the earlier versions, and
 * is more compatable with Gosling EMACS than
 * with ITS EMACS. Bound to "C-X C-W".
 */
int filewrite(int f, int n)
{
        struct window *wp;
        int s;
        char fname[NFILEN];

        if (restflag)           /* don't allow this command if restricted */
                return resterr();
        if ((s = mlreply("Write file: ", fname, NFILEN)) != TRUE)
                return s;
        if ((s = writeout(fname)) == TRUE) {
                strcpy(curbp->b_fname, fname);
                curbp->b_flag &= ~BFCHG;
                wp = wheadp;    /* Update mode lines.   */
                while (wp != NULL) {
                        if (wp->w_bufp == curbp)
                                wp->w_flag |= WFMODE;
                        wp = wp->w_wndp;
                }
        }
        return s;
}

/*
 * Save the contents of the current
 * buffer in its associatd file. No nothing
 * if nothing has changed (this may be a bug, not a
 * feature). Error if there is no remembered file
 * name for the buffer. Bound to "C-X C-S". May
 * get called by "C-Z".
 */
int filesave(int f, int n)
{
        struct window *wp;
        int s;

        if (curbp->b_mode & MDVIEW)     /* don't allow this command if  */
                return rdonly();        /* we are in read only mode     */
        if ((curbp->b_flag & BFCHG) == 0)       /* Return, no changes.  */
                return TRUE;
        if (curbp->b_fname[0] == 0) {   /* Must have a name. */
                mlwrite("No file name");
                return FALSE;
        }

        /* Complain about truncated files */
        if ((curbp->b_flag & BFTRUNC) != 0) {
                if (mlyesno("Truncated file ... write it out") == FALSE) {
                        mlwrite(MLpre "Aborted" MLpost);
                        return FALSE;
                }
        }

        /* Complain about narrowed buffers */
        if ((curbp->b_flag&BFNAROW) != 0) {
                if (mlyesno("Narrowed buffer: write it out anyway") != TRUE) {
                        mlwrite(MLpre "Aborted" MLpost);
                        return(FALSE);
                }
        }

        if ((s = writeout(curbp->b_fname)) == TRUE) {
                curbp->b_flag &= ~BFCHG;
                wp = wheadp;    /* Update mode lines. */
                while (wp != NULL) {
                        if (wp->w_bufp == curbp)
                                wp->w_flag |= WFMODE;
                        wp = wp->w_wndp;
                }
        }
        return s;
}

/*
 * This function performs the details of file writing.
 * Uses the file management routines in the "fileio.c" package.
 * The number of lines written is displayed.
 * Most of the grief is error checking of some sort.
 */
int writeout(char *fn) {
    int s;
    struct line *lp;
    int nline;

#if CRYPT
    s = resetkey();
    if (s != TRUE) return s;
#endif

    if ((s = ffwopen(fn)) != FIOSUC) return FALSE;  /* Open writes message */

    mlwrite(MLpre "Writing..." MLpost);     /* tell us were writing */
    lp = lforw(curbp->b_linep);             /* First line.          */
    nline = 0;                              /* Number of lines.     */
    while (lp != curbp->b_linep) {
        if ((s = ffputline(lp->l_text, llength(lp))) != FIOSUC) break;
        ++nline;
        if (!(nline % 300) && !silent)      /* GGR */
            mlwrite(MLpre "Writing..." MLpost " : %d lines",nline);
        lp = lforw(lp);
    }
    if (s == FIOSUC) {                      /* No write error.      */
        ffputline(NULL, 0);                 /* Must flush write cache!! */
        s = ffclose();
        if (s == FIOSUC) {                  /* No close error.      */
            if (nline == 1)
                mlwrite(MLpre "Wrote 1 line" MLpost);
            else
                mlwrite(MLpre "Wrote %d lines" MLpost, nline);
            }
    } else                                  /* Ignore close error   */
        ffclose();                          /* if a write error.    */
    if (s != FIOSUC) return FALSE;          /* Some sort of error.  */
    return TRUE;
}

/*
 * The command allows the user
 * to modify the file name associated with
 * the current buffer. It is like the "f" command
 * in UNIX "ed". The operation is simple; just zap
 * the name in the buffer structure, and mark the windows
 * as needing an update. You can type a blank line at the
 * prompt if you wish.
 */
int filename(int f, int n)
{
        struct window *wp;
        int s;
        char fname[NFILEN];

        if (restflag)           /* don't allow this command if restricted */
                return resterr();
        if ((s = mlreply("Name: ", fname, NFILEN)) == ABORT)
                return s;
        if (s == FALSE)
                strcpy(curbp->b_fname, "");
        else
                strcpy(curbp->b_fname, fname);
        wp = wheadp;            /* Update mode lines.   */
        while (wp != NULL) {
                if (wp->w_bufp == curbp)
                        wp->w_flag |= WFMODE;
                wp = wp->w_wndp;
        }
        curbp->b_mode &= ~MDVIEW;       /* no longer read only mode */
        return TRUE;
}

/*
 * Insert file "fname" into the current
 * buffer, Called by insert file command. Return the final
 * status of the read.
 */
int ifile(char *fname) {
    struct line *lp0;
    struct line *lp1;
    struct line *lp2;
    struct buffer *bp;
    int s;
    int nline;
    char mesg[NSTRING];

    bp = curbp;             /* Cheap.               */
    bp->b_flag |= BFCHG;    /* we have changed      */
    bp->b_flag &= ~BFINVS;  /* and are not temporary */

/* If this is a translation table, remove any compiled data */

    if ((bp->b_type == BTPHON) && bp->ptt_headp) ptt_free(bp);

    pathexpand = FALSE;     /* GGR */

    if ((s = ffropen(fname)) == FIOERR) goto out;   /* Hard file open */
    if (s == FIOFNF) {                              /* File not found */
        mlwrite(MLpre "No such file" MLpost);
        return FALSE;
    }
    mlwrite(MLpre "Inserting file" MLpost);

#if     CRYPT
    s = resetkey();
    if (s != TRUE) return s;
#endif
/* Back up a line and save the mark here */
    curwp->w_dotp = lback(curwp->w_dotp);
    curwp->w_doto = 0;
    curwp->w_markp = curwp->w_dotp;
    curwp->w_marko = 0;

    nline = 0;
    while ((s = ffgetline()) == FIOSUC) {
        lp1 = fline;            /* Allocate by ffgetline..*/
        lp0 = curwp->w_dotp;    /* line previous to insert */
        lp2 = lp0->l_fp;        /* line after insert */

/* Re-link new line between lp0 and lp2 */
        lp2->l_bp = lp1;
        lp0->l_fp = lp1;
        lp1->l_bp = lp0;
        lp1->l_fp = lp2;

/* And advance and write out the current line */
        curwp->w_dotp = lp1;
        ++nline;
        if (!(nline % 300) && !silent)      /* GGR */
             mlwrite(MLpre "Inserting file" MLpost " : %d lines", nline);

    }
    ffclose();              /* Ignore errors. */
    curwp->w_markp = lforw(curwp->w_markp);
    strcpy(mesg, MLpre);
    if (s == FIOERR) {
        strcat(mesg, "I/O ERROR, ");
        curbp->b_flag |= BFTRUNC;
    }
    if (s == FIOMEM) {
        strcat(mesg, "OUT OF MEMORY, ");
        curbp->b_flag |= BFTRUNC;
    }
    sprintf(&mesg[strlen(mesg)], "Inserted %d line", nline);
    if (nline > 1) strcat(mesg, "s");
    strcat(mesg, MLpost);
    mlwrite(mesg);

out:
/* Advance to the next line and mark the window for changes */
    curwp->w_dotp = lforw(curwp->w_dotp);
    curwp->w_flag |= WFHARD | WFMODE;

/* Copy window parameters back to the buffer structure */
    curbp->b_dotp = curwp->w_dotp;
    curbp->b_doto = curwp->w_doto;
    curbp->b_markp = curwp->w_markp;
    curbp->b_marko = curwp->w_marko;
    curbp->b_fcol = curwp->w_fcol;

    if (s == FIOERR) return FALSE;      /* False if error. */
    return TRUE;
}
