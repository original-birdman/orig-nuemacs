/*      buffer.c
 *
 *      Buffer management.
 *      Some of the functions are internal,
 *      and some are actually attached to user
 *      keys. Like everyone else, they set hints
 *      for the display system
 *
 *      modified by Petri Kutvonen
 */

#include <stdio.h>
#include <unistd.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"

/*
 * Attach a buffer to a window. The
 * values of dot and mark come from the buffer
 * if the use count is 0. Otherwise, they come
 * from some other window.
 */
int usebuffer(int f, int n)
{
        struct buffer *bp;
        int s;
        char bufn[NBUFN];

/* GGR - handle saved buffer name in minibuffer */
        if ((s = mlreply("Use buffer: ", bufn, NBUFN)) != TRUE) {
                if (!inmb)
                        strcpy(bufn, savnam);
                else
                        return(s);
        }

        if ((bp = bfind(bufn, TRUE, 0)) == NULL)
                return FALSE;
        return swbuffer(bp);
}

/*
 * switch to the next buffer in the buffer list
 *
 * int f, n;            default flag, numeric argument
 */
int nextbuffer(int f, int n)
{
        struct buffer *bp = NULL;  /* eligable buffer to switch to */
        struct buffer *bbp;        /* eligable buffer to switch to */

        /* make sure the arg is legit */
        if (f == FALSE)
                n = 1;
        if (n < 1)
                return FALSE;

        bbp = curbp;
        while (n-- > 0) {
                /* advance to the next buffer */
                bp = bbp->b_bufp;

                /* cycle through the buffers to find an eligable one */
                while (bp == NULL || bp->b_flag & BFINVS) {
                        if (bp == NULL)
                                bp = bheadp;
                        else
                                bp = bp->b_bufp;

                        /* don't get caught in an infinite loop! */
                        if (bp == bbp)
                                return FALSE;

                }

                bbp = bp;
        }

        return swbuffer(bp);
}

/*
 * make buffer BP current
 */
int swbuffer(struct buffer *bp) {
    struct window *wp;

/* Save last name so we can switch back to it on empty MB reply */
    if (!inmb) strcpy(savnam, curbp->b_bname);

    if (--curbp->b_nwnd == 0) {     /* Last use. */
        curbp->b_dotp = curwp->w_dotp;
        curbp->b_doto = curwp->w_doto;
        curbp->b_markp = curwp->w_markp;
        curbp->b_marko = curwp->w_marko;
        curbp->b_fcol = curwp->w_fcol;
    }
    curbp = bp;                     /* Switch. */
    if (curbp->b_active != TRUE) {  /* buffer not active yet */
        curbp->b_mode |= gmode;     /* readin() runs file-hooks, so set now */
/* Read it in and activate it */
        readin(curbp->b_fname, TRUE);
        curbp->b_dotp = lforw(curbp->b_linep);
        curbp->b_doto = 0;
        curbp->b_active = TRUE;
    }
    curwp->w_bufp = bp;
    curwp->w_linep = bp->b_linep;   /* For macros, ignored. */
    curwp->w_flag |= WFMODE | WFFORCE | WFHARD;     /* Quite nasty. */
    if (bp->b_nwnd++ == 0) {        /* First use.           */
        curwp->w_dotp = bp->b_dotp;
        curwp->w_doto = bp->b_doto;
        curwp->w_markp = bp->b_markp;
        curwp->w_marko = bp->b_marko;
        curwp->w_fcol = bp->b_fcol;
        cknewwindow();
        return TRUE;
    }
    wp = wheadp;                    /* Look for old.        */
    while (wp != NULL) {
        if (wp != curwp && wp->w_bufp == bp) {
            curwp->w_dotp = wp->w_dotp;
            curwp->w_doto = wp->w_doto;
            curwp->w_markp = wp->w_markp;
            curwp->w_marko = wp->w_marko;
            curwp->w_fcol = wp->w_fcol;
            break;
        }
        wp = wp->w_wndp;
    }
    cknewwindow();
    return TRUE;
}

/*
 * Dispose of a buffer, by name.
 * Ask for the name. Look it up (don't get too
 * upset if it isn't there at all!). Get quite upset
 * if the buffer is being displayed. Clear the buffer (ask
 * if the buffer has been changed). Then free the header
 * line and the buffer header. Bound to "C-X K".
 */
int killbuffer(int f, int n)
{
        struct buffer *bp;
        int s;
        char bufn[NBUFN];

        if ((s = mlreply("Kill buffer: ", bufn, NBUFN)) != TRUE)
                return s;
        if ((bp = bfind(bufn, FALSE, 0)) == NULL)   /* Easy if unknown. */
                return TRUE;
        if (bp->b_flag & BFINVS)    /* Deal with special buffers */
                return TRUE;        /* by doing nothing.    */
        return zotbuf(bp);
}

/*
 * kill the buffer pointed to by bp
 */
int zotbuf(struct buffer *bp)
{
        struct buffer *bp1;
        struct buffer *bp2;
        int s;

        if (bp->b_nwnd != 0) {          /* Error if on screen.  */
                mlwrite("Buffer is being displayed");
                return FALSE;
        }
        if ((s = bclear(bp)) != TRUE)   /* Blow text away.      */
                return s;
        free((char *) bp->b_linep);     /* Release header line. */
        bp1 = NULL;                     /* Find the header.     */
        bp2 = bheadp;
        while (bp2 != bp) {
                bp1 = bp2;
                bp2 = bp2->b_bufp;
        }
        bp2 = bp2->b_bufp;              /* Next one in chain.   */
        if (bp1 == NULL)                /* Unlink it.           */
                bheadp = bp2;
        else
                bp1->b_bufp = bp2;
        free((char *) bp);              /* Release buffer block */
        return TRUE;
}

/*
 * Rename the current buffer
 *
 * int f, n;            default Flag & Numeric arg
 */
int namebuffer(int f, int n)
{
        struct buffer *bp;      /* pointer to scan through all buffers */
        char bufn[NBUFN];       /* buffer to hold buffer name */

        /* prompt for and get the new buffer name */
      ask:if (mlreply("Change buffer name to: ", bufn, NBUFN) !=
            TRUE)
                return FALSE;

        /* and check for duplicates */
        bp = bheadp;
        while (bp != NULL) {
                if (bp != curbp) {
                        /* if the names the same */
                        if (strcmp(bufn, bp->b_bname) == 0)
                                goto ask;       /* try again */
                }
                bp = bp->b_bufp;        /* onward */
        }

        strcpy(curbp->b_bname, bufn);   /* copy buffer name to structure */
        curwp->w_flag |= WFMODE;        /* make mode line replot */
        mlerase();
        return TRUE;
}

/*
 * List all of the active buffers.  First update the special
 * buffer that holds the list.  Next make sure at least 1
 * window is displaying the buffer list, splitting the screen
 * if this is what it takes.  Lastly, repaint all of the
 * windows that are displaying the list.  Bound to "C-X C-B".
 *
 * A numeric argument forces it to list invisible buffers as
 * well.
 */
int listbuffers(int f, int n)
{
    struct window *wp;
    struct buffer *bp;
    int s;

/* GGR - disallow in minibuffer */
    if (mbstop()) return(FALSE);

    if ((s = makelist(f)) != TRUE) return s;

    if (blistp->b_nwnd == 0) {      /* Not on screen yet.   */
        if ((wp = wpopup()) == NULL) return FALSE;
        bp = wp->w_bufp;
        if (--bp->b_nwnd == 0) {
            bp->b_dotp = wp->w_dotp;
            bp->b_doto = wp->w_doto;
            bp->b_markp = wp->w_markp;
            bp->b_marko = wp->w_marko;
            bp->b_fcol = wp->w_fcol;
        }
        wp->w_bufp = blistp;
        ++blistp->b_nwnd;
    }
    wp = wheadp;
    while (wp != NULL) {
        if (wp->w_bufp == blistp) {
            wp->w_linep = lforw(blistp->b_linep);
            wp->w_dotp = lforw(blistp->b_linep);
            wp->w_doto = 0;
            wp->w_markp = NULL;
            wp->w_marko = 0;
            wp->w_flag |= WFMODE | WFHARD;
        }
        wp = wp->w_wndp;
    }
    return TRUE;
}

/*
 * This routine rebuilds the text in the special secret buffer
 * that holds the buffer list.
 * It is called by the list buffers command.
 * Return TRUE if everything works.
 * Return FALSE if there is an error (if there is no memory).
 * Iflag indicates wether to list hidden buffers.
 *
 * int iflag;           list hidden buffer flag
 */
#define MAXLINE MAXCOL
int makelist(int iflag)
{
    char *cp1;
    char *cp2;
    int c;
    struct buffer *bp;
    struct line *lp;
    int s;
    int i;
    long nbytes;                        /* # of bytes in current buffer */
    char b[9+1];                        /* field width for nbytes + NL */
    char line[MAXLINE];

    blistp->b_flag &= ~BFCHG;           /* Don't complain!      */
    if ((s = bclear(blistp)) != TRUE)   /* Blow old text away   */
        return s;
    strcpy(blistp->b_fname, "");
    if (addline("ACT MODES          Size Buffer        File") == FALSE
     || addline("--- -----          ---- ------        ----") == FALSE)
        return FALSE;
    bp = bheadp;                        /* For all buffers      */

/* Build line to report global mode settings */

    cp1 = &line[0];
    for (i = 0; i < 4; i++) *cp1++ = ' ';

/* Output the mode codes */

    for (i = 0; i < NUMMODES; i++)
        *cp1++ = (gmode & (1 << i))? modecode[i]: '.';
    strcpy(cp1, "           Global Modes");
    if (addline(line) == FALSE) return FALSE;

/* Output the list of buffers */

    while (bp != NULL) {
/* Skip invisable buffers if iflag is false */
        if (((bp->b_flag & BFINVS) != 0) && (iflag != TRUE)) {
            bp = bp->b_bufp;
            continue;
        }
        cp1 = &line[0];                 /* Start at left edge   */

/* Output status of ACTIVE flag (has the file been read in? */

        *cp1++ = (bp->b_active == TRUE)? '@': ' ';

/* Output status of changed flag */

        *cp1++ = ((bp->b_flag & BFCHG) != 0)? '*': ' ';

/* Report if the file is truncated */

        *cp1++ = ((bp->b_flag & BFTRUNC) != 0)? '#': ' ';
        *cp1++ = ' ';                   /* space */

/* Output the mode codes */

        for (i = 0; i < NUMMODES; i++)
            *cp1++ = (bp->b_mode & (1 << i))?  modecode[i]: '.';

        *cp1++ = ' ';                   /* Gap.                 */
        nbytes = 0L;                    /* Count bytes in buf.  */
        lp = lforw(bp->b_linep);
        while (lp != bp->b_linep) {
            nbytes += (long) llength(lp) + 1L;
            lp = lforw(lp);
        }
        ltoa(b, 9, nbytes);             /* 9 digit buffer size. */
        cp2 = b;
        while ((c = *cp2++) != 0) *cp1++ = c;
        *cp1++ = ' ';                   /* Gap.                 */
        cp2 = &bp->b_bname[0];          /* Buffer name          */
        while ((c = *cp2++) != 0)
            *cp1++ = c;
        cp2 = &bp->b_fname[0];          /* File name            */
        if (*cp2 != 0) {
/*
 * We'll assume an 80-column width for determining whether the
 * filename will fit.
 * The column data so far is 3+1+9+1+9+1+14 == 38.
 * If the buffername has reached >37 or the filename is > 40 we'll print
 * the filename on the next line...
 */

            if (((cp1 - line) > 37) || (strlen(cp2) > 40)) {
                *cp1++ = ' ';
                *cp1++ = 0xe2;      /* Carriage return symbol */
                *cp1++ = 0x86;      /* U+2185                 */
                *cp1++ = 0xb5;      /* as utf-8               */
                *cp1 = 0;           /* Add to the buffer.   */
                if (addline(line) == FALSE) return FALSE;
                cp1 = line;
                for (i = 0; i < 5; i++) *cp1++ = ' ';
            }
            else {
                while (cp1 < line+38) *cp1++ = ' ';
            }
            while ((c = *cp2++) != 0) {
                if (cp1 < &line[MAXLINE - 1]) *cp1++ = c;
            }
        }
        *cp1 = 0;       /* Add to the buffer.   */
        if (addline(line) == FALSE) return FALSE;
        bp = bp->b_bufp;
    }
    return TRUE;            /* All done             */
}

/* This is being put into a 9-char (+NL) field, but could actually
 * have 10 chars...
 */
void ltoa(char *buf, int width, long num)
{
        buf[width] = 0;                 /* End of string.       */
        while (num >= 10 && width) {    /* Conditional digits.  */
                buf[--width] = (int) (num % 10L) + '0';
                num /= 10L;
        }
        if (width) buf[--width] = (int) num + '0';  /* If room */
        else buf[0] = '+';                          /* if not */
        while (width != 0)      /* Pad with blanks.     */
                buf[--width] = ' ';
}

/*
 * The argument "text" points to
 * a string. Append this line to the
 * buffer list buffer. Handcraft the EOL
 * on the end. Return TRUE if it worked and
 * FALSE if you ran out of room.
 */
int addline(char *text)
{
        struct line *lp;
        int ntext;

        ntext = strlen(text);
        if ((lp = lalloc(ntext)) == NULL)
                return FALSE;
        lfillchars(lp, ntext, text);
        blistp->b_linep->l_bp->l_fp = lp;       /* Hook onto the end    */
        lp->l_bp = blistp->b_linep->l_bp;
        blistp->b_linep->l_bp = lp;
        lp->l_fp = blistp->b_linep;
        if (blistp->b_dotp == blistp->b_linep)  /* If "." is at the end */
                blistp->b_dotp = lp;    /* move it to new line  */
        return TRUE;
}

/*
 * Look through the list of
 * buffers. Return TRUE if there
 * are any changed buffers. Buffers
 * that hold magic internal stuff are
 * not considered; who cares if the
 * list of buffer names is hacked.
 * Return FALSE if no buffers
 * have been changed.
 */
int anycb(void)
{
        struct buffer *bp;

        bp = bheadp;
        while (bp != NULL) {
                if ((bp->b_flag & BFINVS) == 0
                    && (bp->b_flag & BFCHG) != 0)
                        return TRUE;
                bp = bp->b_bufp;
        }
        return FALSE;
}

/*
 * Find a buffer, by name. Return a pointer
 * to the buffer structure associated with it.
 * If the buffer is not found
 * and the "cflag" is TRUE, create it. The "bflag" is
 * the settings for the flags in in buffer.
 */
struct buffer *bfind(char *bname, int cflag, int bflag) {
    struct buffer *bp;
    struct buffer *sb;      /* buffer to insert after */
    struct line *lp;

/* GGR Add a check on the sent namelength, given that we are going
 * to copy it into a structure if we create a new buffer.
 */
    if (strlen(bname) >= NBUFN) {   /* We need a NUL too */
        mlforce("Buffer name too long: %s. Ignored.", bname);
        sleep(1);
        return NULL;
    }
    bp = bheadp;
    while (bp != NULL) {
        if (strcmp(bname, bp->b_bname) == 0) return bp;
        bp = bp->b_bufp;
    }
    if (cflag != FALSE) {
        if ((bp = (struct buffer *)malloc(sizeof(struct buffer))) == NULL)
            return NULL;
        if ((lp = lalloc(0)) == NULL) {
            free((char *) bp);
            return NULL;
        }
/* Find the place in the list to insert this buffer */
        if (bheadp == NULL || strcmp(bheadp->b_bname, bname) > 0) {
            bp->b_bufp = bheadp;        /* insert at the beginning */
            bheadp = bp;
        } else {
            sb = bheadp;
            while (sb->b_bufp != NULL) {
                if (strcmp(sb->b_bufp->b_bname, bname) > 0) break;
                sb = sb->b_bufp;
            }

/* And insert it */
            bp->b_bufp = sb->b_bufp;
            sb->b_bufp = bp;
        }

/* And set up the other buffer fields */
        bp->b_topline = NULL;   /* GGR - for widen and  */
        bp->b_botline = NULL;   /* GGR - shrink windows */
        bp->b_active = TRUE;
        bp->b_dotp = lp;
        bp->b_doto = 0;
        bp->b_markp = NULL;
        bp->b_marko = 0;
        bp->b_fcol = 0;
        bp->b_flag = bflag;
        bp->b_mode = gmode;
        bp->b_nwnd = 0;
        bp->b_linep = lp;
        strcpy(bp->b_fname, "");
        strcpy(bp->b_bname, bname);
#if     CRYPT
        bp->b_key[0] = 0;
        bp->b_keylen = 0;
        bp->b_EOLmissing = 0;
#endif
#if PROC
        bp->ptt_headp = NULL;
        bp->b_type = BTNORM;
#endif
        bp->b_exec_level = 0;
        lp->l_fp = lp;
        lp->l_bp = lp;
    }
    return bp;
}

/*
 * This routine blows away all of the text
 * in a buffer. If the buffer is marked as changed
 * then we ask if it is ok to blow it away; this is
 * to save the user the grief of losing text. The
 * window chain is nearly always wrong if this gets
 * called; the caller must arrange for the updates
 * that are required. Return TRUE if everything
 * looks good.
 */
int bclear(struct buffer *bp) {
    struct line *lp;
    int s;

    if ((bp->b_flag & BFINVS) == 0      /* Not scratch buffer.  */
        && (bp->b_flag & BFCHG) != 0    /* Something changed    */
        && (s = mlyesno("Discard changes")) != TRUE)
            return s;
    bp->b_flag &= ~BFCHG;               /* Not changed          */
    while ((lp = lforw(bp->b_linep)) != bp->b_linep) lfree(lp);
    bp->b_dotp = bp->b_linep;           /* Fix "."              */
    bp->b_doto = 0;
    bp->b_markp = NULL;                 /* Invalidate "mark"    */
    bp->b_marko = 0;
    bp->b_fcol = 0;
    return TRUE;
}

/*
 * unmark the current buffers change flag
 *
 * int f, n;            unused command arguments
 */
int unmark(int f, int n) {
    curbp->b_flag &= ~BFCHG;
    curwp->w_flag |= WFMODE;
    return TRUE;
}
