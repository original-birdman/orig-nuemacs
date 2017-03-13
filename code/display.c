/*      display.c
 *
 *      The functions in this file handle redisplay. There are two halves, the
 *      ones that update the virtual display screen, and the ones that make the
 *      physical display screen the same as the virtual display screen. These
 *      functions use hints that are left in the windows by the commands.
 *
 *      Modified by Petri Kutvonen
 */

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "version.h"
#include "wrapper.h"
#include "utf8.h"

static int mbonly = FALSE;      /* GGR - minibuffer only */

/* The display information is buffered.
 * We fill in vscreen as we go (with vtrow and vtcol) then this
 * is copied (in update()) to pscreen, which is sent to the
 * display with TTputc calls, with ttcol tracked by other TTput* calls.
 * Only this source file needs to know about the data.
 */
static int vtrow = 0;                  /* Row location of SW cursor */
static int vtcol = 0;                  /* Column location of SW cursor */

struct video {
        int v_flag;             /* Flags */
#if     COLOR
        int v_fcolor;           /* current forground color */
        int v_bcolor;           /* current background color */
        int v_rfcolor;          /* requested forground color */
        int v_rbcolor;          /* requested background color */
#endif
        struct glyph v_text[0]; /* Screen data - dynamic */
};

#define VFCHG   0x0001          /* Changed flag                 */
#define VFEXT   0x0002          /* extended (beyond column 80)  */
#define VFREV   0x0004          /* reverse video status         */
#define VFREQ   0x0008          /* reverse video request        */
#define VFCOL   0x0010          /* color change requested       */

static struct video **vscreen;          /* Virtual screen. */
#if     MEMMAP == 0 || SCROLLCODE
static struct video **pscreen;          /* Physical screen. */
#endif

static int displaying = TRUE;
#if UNIX
#include <signal.h>
#endif
#ifdef SIGWINCH
#include <sys/ioctl.h>
#endif

static int reframe(struct window *wp);
static void updone(struct window *wp);
static void updall(struct window *wp);
#if SCROLLCODE
static int scrolls(int inserts);
static void scrscroll(int from, int to, int count);
static int texttest(int vrow, int prow);
static int endofline(struct glyph *s, int n);
#endif
static void updext(void);
static int updateline(int row, struct video *vp1, struct video *vp2);
static void modeline(struct window *wp);
static void mlputi(int i, int r);
static void mlputli(long l, int r);
static void mlputf(int s);
static int newscreensize(int h, int w);

#if RAINBOW
static void putline(int row, int col, char *buf);
#endif

/* GGR Some functions to handle struct glyph usage */

/* Set the entry to an ASCII character.
 * Checks for previous extended cdm usage and frees any such found.
 * POSSIBLY INLINABLE??
 */
static void set_glyph(struct glyph *gp, unicode_t uc) {
    gp->uc = uc;
    gp->cdm = 0;
    if (gp->ex != NULL) {
        free(gp->ex);
        gp->ex = NULL;
    }
    return;
}

/* Add a unicode character as a cdm or dynamic ex entry
 */
static void extend_glyph(struct glyph *gp, unicode_t uc) {

    if (gp->cdm == 0) {         /* None there yet - simple */
        gp->cdm = (unicode_t)uc;
        return;
    }
/* Need to create or extend an ex section */
    int xc;
    if (gp->ex == NULL) {
        xc = 0;
    }
    else {
        xc = 1;
        while(gp->ex[xc] != END_UCLIST) xc++;
    }
    gp->ex = realloc(gp->ex, (xc+2)*sizeof(unicode_t));
    gp->ex[xc] = uc;
    gp->ex[xc+1] = END_UCLIST;
    return;
}

/* Copy glyph data between structures.
 * This needs to allocate new space for any ex section and copy the data.
 */
static void clone_glyph(struct glyph *gtarget, struct glyph *gsource) {

    gtarget->uc = gsource->uc;
    gtarget->cdm = gsource->cdm;
/* Free any existing ex data in the target */
    if (gtarget->ex != NULL) free(gtarget->ex);
    if (gsource->ex == NULL) {  /* No incoming ex section */
        gtarget->ex = NULL;
        return;
    }

/* Need to allocate and copy the ex section */
    int xc = 1;
    while(gsource->ex[xc] != END_UCLIST) xc++;
    gtarget->ex = malloc((xc+2)*sizeof(unicode_t));
    memcpy(gtarget->ex, gsource->ex, (xc+2)*sizeof(unicode_t));
    return;
}

/* Only interested in same/not same here
 */
static int glyph_same(struct glyph *gp1, struct glyph *gp2) {
    if (gp1->uc != gp2->uc) return FALSE;
    if (gp1->cdm != gp2->cdm) return FALSE;
    if ((gp1->ex == NULL) && (gp2->ex == NULL)) return TRUE;
    if ((gp1->ex == NULL) && (gp2->ex != NULL)) return FALSE;
    if ((gp1->ex != NULL) && (gp2->ex == NULL)) return FALSE;
/* Have to compare the ex lists!... */
    unicode_t *ex1 = gp1->ex;
    unicode_t *ex2 = gp2->ex;
    while(1) {
        if (*ex1 != *ex2) return FALSE;
        if ((*ex1 == END_UCLIST) || (*ex2 == END_UCLIST)) break;
        ex1++; ex2++;
    }
    return ((*ex1 == END_UCLIST) && (*ex2 == END_UCLIST));
}

static int glyph_same_array(struct glyph *gp1, struct glyph *gp2, int nelem) {
    for (int i = 0; i < nelem; i++) {
        if (!glyph_same(gp1+i, gp2+i)) return FALSE;
    }
    return TRUE;
}

static int is_space(struct glyph *gp) {
    return ((gp->uc == ' ') && (gp->cdm == 0));
}

/* Output a glyph - which is in one column.
 * Handle remapping on the main character.
 */
static inline int TTputglyph(struct glyph *gp) {
    int status = TTputc(display_for(gp->uc));
    if (gp->cdm) TTputc(gp->cdm);   /* Might add display_for here too */
    if (gp->ex != NULL) {
        for(unicode_t *zw = gp->ex; *zw != END_UCLIST; zw++) {
            TTputc(*zw);            /* Might add display_for here too */
        }
    }
    ttcol++;
    return status;
}
/* Output a single character. mlwrite, mlput* may send zerowidth ones */
static inline int TTput1char(unicode_t uc) {
    int status = TTputc(display_for(uc));
    if (!zerowidth_type(uc)) ttcol++;
    return status;
}

/* Routine callable from other modules to access the TTput* handlers */
int ttput1c(char c) {
    return TTput1char((unicode_t)c);
}

/*
 * Initialize the data structures used by the display code. The edge vectors
 * used to access the screens are set up. The operating system's terminal I/O
 * channel is set up. All the other things get initialized at compile time.
 * The original window has "WFCHG" set, so that it will get completely
 * redrawn on the first call to "update".
 */
void vtinit(void)
{
        int i;
        struct video *vp;

        TTopen();               /* open the screen */
        TTkopen();              /* open the keyboard */
        TTrev(FALSE);
        vscreen = xmalloc(term.t_mrow * sizeof(struct video *));

#if     MEMMAP == 0 || SCROLLCODE
        pscreen = xmalloc(term.t_mrow * sizeof(struct video *));
#endif
        for (i = 0; i < term.t_mrow; ++i) {
                vp = xmalloc(sizeof(struct video) +
                        term.t_mcol*sizeof(struct glyph));
                vp->v_flag = 0;
#if     COLOR
/* GGR - use defined colors */
                vp->v_rfcolor = gfcolor;
                vp->v_rbcolor = gbcolor;
#endif
                vscreen[i] = vp;

/* GGR - clear things out at the start.
 * Can't use set_glyph() until after we have initialized
 * We only need to initialize vscreen, as we do all assigning
 * (including malloc()/free()) there.
 */
        int j = 0;
        while (j < term.t_mcol) {   /* Need to clear out *all* now */
                vp->v_text[j++].uc = ' ';
                vp->v_text[j++].cdm = 0;
                vp->v_text[j++].ex = NULL;
        }

#if     MEMMAP == 0 || SCROLLCODE
                vp = xmalloc(sizeof(struct video) +
                        term.t_mcol*sizeof(struct glyph));
                vp->v_flag = 0;
                pscreen[i] = vp;
#endif
        }
        mberase();              /* GGR */
}

#if     CLEAN
/* free up all the dynamically allocated video structures */

void vtfree(void)
{
        int i;
        for (i = 0; i < term.t_mrow; ++i) {
                free(vscreen[i]);
#if     MEMMAP == 0 || SCROLLCODE
                free(pscreen[i]);
#endif
        }
        free(vscreen);
#if     MEMMAP == 0 || SCROLLCODE
        free(pscreen);
#endif
}
#endif

/*
 * Clean up the virtual terminal system, in anticipation for a return to the
 * operating system. Move down to the last line and clear it out (the next
 * system prompt will be written in the line). Shut down the channel to the
 * terminal.
 */
void vttidy(void)
{
        mlerase();
        movecursor(term.t_nrow, 0);
        TTflush();
        TTclose();
        TTkclose();
#ifdef PKCODE
        int dnc __attribute__ ((unused)) = write(1, "\r", 1);
#endif
}

/*
 * Set the virtual cursor to the specified row and column on the virtual
 * screen. There is no checking for nonsense values; this might be a good
 * idea during the early stages.
 */
void vtmove(int row, int col)
{
        vtrow = row;
        vtcol = col;
}

/*
 * Write a character to the virtual screen. The virtual row and
 * column are updated. If we are not yet on left edge, don't print
 * it yet. If the line is too long put a "$" in the last column.
 *
 * This routine only puts printing characters into the virtual
 * terminal buffers. Only column overflow is checked.
 */

static void vtputc(unsigned int c)
{
        struct video *vp;       /* ptr to line being updated */

        if (c > MAX_UTF8_CHAR) c = display_for(c);

        vp = vscreen[vtrow];

        if (zerowidth_type((unicode_t)c)) {
/* Only extend a glyph if we have a prev-char within screen width */
            if (vtcol > 0 && (vtcol <= term.t_ncol)) {
                extend_glyph(&(vp->v_text[vtcol-1]), c);
                return;     /* Nothing else do do... */
            }
        }

        if (vtcol >= term.t_ncol) {
                ++vtcol;
                set_glyph(&(vp->v_text[term.t_ncol - 1]), '$');
                return;
        }

        if (c == '\t') {
                do {
                        vtputc(' ');
                } while (((vtcol + taboff) & tabmask) != 0);
                return;
        }

/* NOTE: Unicode has characters for displaying Control Charcaters
 *  U+2400 to U+241F (so 2400+c)
 */
        if (c < 0x20) {
                vtputc('^');
                vtputc(c ^ 0x40);
                return;
        }

/* NOTE: Unicode has a characters for displaying Delete.
 *  U+2421
 */
        if (c == 0x7f) {
                vtputc('^');
                vtputc('?');
                return;
        }

        if (c >= 0x80 && c <= 0xA0) {
                static const char hex[] = "0123456789abcdef";
                vtputc('\\');
                vtputc(hex[c >> 4]);
                vtputc(hex[c & 15]);
                return;
        }

        if (vtcol >= 0)
                set_glyph(&(vp->v_text[vtcol]), c);
        ++vtcol;
}

/*
 * Erase from the end of the software cursor to the end of the line on which
 * the software cursor is located.
 */
static void vteeol(void)
{
        struct glyph *vcp = vscreen[vtrow]->v_text;

        while (vtcol < term.t_ncol)
                set_glyph(&(vcp[vtcol++]), ' ');
}

/*
 * upscreen:
 *      user routine to force a screen update
 *      always finishes complete update
 */
int upscreen(int f, int n)
{
        update(TRUE);
        return TRUE;
}

#if SCROLLCODE
static int scrflags = 0;
#endif

/*
 * Make sure that the display is right. This is a three part process. First,
 * scan through all of the windows looking for dirty ones. Check the framing,
 * and refresh the screen. Second, make sure that "currow" and "curcol" are
 * correct for the current window. Third, make the virtual and physical
 * screens the same.
 *
 * int force;           force update past type ahead?
 */
int update(int force)
{
        struct window *wp;

#if     TYPEAH && ! PKCODE
        if (force == FALSE && typahead())
                return TRUE;
#endif
#if     VISMAC == 0
        if (force == FALSE && kbdmode == PLAY)
                return TRUE;
#endif

        displaying = TRUE;

#if SCROLLCODE

        /* first, propagate mode line changes to all instances of
           a buffer displayed in more than one window */
        wp = wheadp;
        while (wp != NULL) {
                if (wp->w_flag & WFMODE) {
                        if (wp->w_bufp->b_nwnd > 1) {
                                /* make sure all previous windows have this */
                                struct window *owp;
                                owp = wheadp;
                                while (owp != NULL) {
                                        if (owp->w_bufp == wp->w_bufp)
                                                owp->w_flag |= WFMODE;
                                        owp = owp->w_wndp;
                                }
                        }
                }
                wp = wp->w_wndp;
        }

#endif

        /* update any windows that need refreshing */
        if (mbonly)         /* GGR - get the correct window */
                wp = curwp;
        else
                wp = wheadp;
        while (wp != NULL) {
                if (wp->w_flag) {
                        /* if the window has changed, service it */
                        reframe(wp);    /* check the framing */
#if SCROLLCODE
                        if (wp->w_flag & (WFKILLS | WFINS)) {
                                scrflags |=
                                    (wp->w_flag & (WFINS | WFKILLS));
                                wp->w_flag &= ~(WFKILLS | WFINS);
                        }
#endif
                        if ((wp->w_flag & ~WFMODE) == WFEDIT)
                                updone(wp);     /* update EDITed line */
                        else if (wp->w_flag & ~WFMOVE)
                                updall(wp);     /* update all lines */
#if SCROLLCODE
                        if (scrflags || (wp->w_flag & WFMODE))
#else
                        if (wp->w_flag & WFMODE)
#endif
                                modeline(wp);   /* update modeline */
                        wp->w_flag = 0;
                        wp->w_force = 0;
                }
                /* on to the next window */
                if (mbonly)     /* GGR - stop if in minibuffer */
                        wp = NULL;
                else
                        wp = wp->w_wndp;
        }
        /* recalc the current hardware cursor location */
        updpos();

#if     MEMMAP && ! SCROLLCODE
        /* update the cursor and flush the buffers */
        movecursor(currow, curcol - lbound);
#endif

        /* check for lines to de-extend */
        upddex();

        /* if screen is garbage, re-plot it */
        if (sgarbf != FALSE)
                updgar();

        /* update the virtual screen to the physical screen */
        updupd(force);

        /* update the cursor and flush the buffers */
        movecursor(currow, curcol - lbound);

        TTflush();
        displaying = FALSE;
#if SIGWINCH
        while (chg_width || chg_height)
                newscreensize(chg_height, chg_width);
#endif
        return TRUE;
}

/*
 * reframe:
 *      check to see if the cursor is on in the window
 *      and re-frame it if needed or wanted
 */
static int reframe(struct window *wp)
{
        struct line *lp;
#if SCROLLCODE
        struct line *lp0;
#endif
        int i = 0;

        /* if not a requested reframe, check for a needed one */
        if ((wp->w_flag & WFFORCE) == 0) {
#if SCROLLCODE
                /* loop from one line above the window to one line after */
                lp = wp->w_linep;
                lp0 = lback(lp);
                if (lp0 == wp->w_bufp->b_linep)
                        i = 0;
                else {
                        i = -1;
                        lp = lp0;
                }
                for (; i <= (int) (wp->w_ntrows); i++)
#else
                lp = wp->w_linep;
                for (i = 0; i < wp->w_ntrows; i++)
#endif
                {
                        /* if the line is in the window, no reframe */
                        if (lp == wp->w_dotp) {
#if SCROLLCODE
                                /* if not _quite_ in, we'll reframe gently */
                                if (i < 0 || i == wp->w_ntrows) {
                                        /* if the terminal can't help, then
                                           we're simply outside */
                                        if (term.t_scroll == NULL)
                                                i = wp->w_force;
                                        break;
                                }
#endif
                                return TRUE;
                        }

                        /* if we are at the end of the file, reframe */
                        if (lp == wp->w_bufp->b_linep)
                                break;

                        /* on to the next line */
                        lp = lforw(lp);
                }
        }
#if SCROLLCODE
        if (i == -1) {                  /* we're just above the window */
                i = scrollcount;        /* put dot at first line */
                scrflags |= WFINS;
        } else if (i == wp->w_ntrows) { /* we're just below the window */
                i = -scrollcount;       /* put dot at last line */
                scrflags |= WFKILLS;
        } else                          /* put dot where requested */
#endif
                i = wp->w_force;        /* (is 0, unless reposition() was called) */

        wp->w_flag |= WFMODE;

        /* how far back to reframe? */
        if (i > 0) {            /* only one screen worth of lines max */
                if (--i >= wp->w_ntrows)
                        i = wp->w_ntrows - 1;
        } else if (i < 0) {     /* negative update???? */
                i += wp->w_ntrows;
                if (i < 0)
                        i = 0;
        } else
                i = wp->w_ntrows / 2;

        /* backup to new line at top of window */
        lp = wp->w_dotp;
        while (i != 0 && lback(lp) != wp->w_bufp->b_linep) {
                --i;
                lp = lback(lp);
        }

        /* and reset the current line at top of window */
        wp->w_linep = lp;
        wp->w_flag |= WFHARD;
        wp->w_flag &= ~WFFORCE;
        return TRUE;
}

static void show_line(struct line *lp)
{
        int i = 0, len = llength(lp);
        while (i < len) {
                unicode_t c;
                i += utf8_to_unicode(lp->l_text, i, len, &c);
                vtputc(c);
        }
}

/*
 * updone:
 *      update the current line to the virtual screen
 *
 * struct window *wp;           window to update current line in
 */
static void updone(struct window *wp)
{
        struct line *lp;        /* line to update */
        int sline;              /* physical screen line to update */

        /* search down the line we want */
        lp = wp->w_linep;
        sline = wp->w_toprow;
        while (lp != wp->w_dotp) {
                ++sline;
                lp = lforw(lp);
        }

        /* and update the virtual line */
        vscreen[sline]->v_flag |= VFCHG;
        vscreen[sline]->v_flag &= ~VFREQ;
        vtmove(sline, 0);
        show_line(lp);
#if     COLOR
        vscreen[sline]->v_rfcolor = wp->w_fcolor;
        vscreen[sline]->v_rbcolor = wp->w_bcolor;
#endif
        vteeol();
}

/*
 * updall:
 *      update all the lines in a window on the virtual screen
 *
 * struct window *wp;           window to update lines in
 */
static void updall(struct window *wp)
{
        struct line *lp;        /* line to update */
        int sline;              /* physical screen line to update */

        /* search down the lines, updating them */
        lp = wp->w_linep;
        sline = wp->w_toprow;
        while (sline < wp->w_toprow + wp->w_ntrows) {

                /* and update the virtual line */
                vscreen[sline]->v_flag |= VFCHG;
                vscreen[sline]->v_flag &= ~VFREQ;
                vtmove(sline, 0);
                if (lp != wp->w_bufp->b_linep) {
                        /* if we are not at the end */
                        show_line(lp);
                        lp = lforw(lp);
                }

                /* on to the next one */
#if     COLOR
                vscreen[sline]->v_rfcolor = wp->w_fcolor;
                vscreen[sline]->v_rbcolor = wp->w_bcolor;
#endif
                vteeol();
                ++sline;
        }
}

/*
 * updpos:
 *      update the position of the hardware cursor and handle extended
 *      lines. This is the only update for simple moves.
 */
void updpos(void)
{
        struct line *lp;
        int i;

        /* find the current row */
        lp = curwp->w_linep;
        currow = curwp->w_toprow;
        while (lp != curwp->w_dotp) {
                ++currow;
                lp = lforw(lp);
        }

        /* find the current column */
        curcol = 0;
        i = 0;
        while (i < curwp->w_doto) {
                unicode_t c;
                int bytes;

                bytes = utf8_to_unicode(lp->l_text, i, curwp->w_doto, &c);
                i += bytes;
                if (c == '\t')
                        curcol |= tabmask;

/* GGR - when counting columns we need to allow for *all* displays of
 *   non-printing characters as multiple chars in vtputc()!
 *   And those with no width at all!!
 */
                else if (c < 0x20 || c == 0x7f)     /* Displayed as ^X */
                        ++curcol;
                else if (c >= 0x80 && c <= 0xA0)    /* Displayed as \nn */
                        curcol += 2;
                else if (zerowidth_type(c)) curcol--;
                ++curcol;
        }

        /* if extended, flag so and update the virtual line image */
        if (curcol >= term.t_ncol - 1) {
                vscreen[currow]->v_flag |= (VFEXT | VFCHG);
                updext();
        } else
                lbound = 0;
}

/*
 * upddex:
 *      de-extend any line that deserves it
 */
void upddex(void)
{
        struct window *wp;
        struct line *lp;
        int i;

        wp = wheadp;

        while (wp != NULL) {
                lp = wp->w_linep;
                i = wp->w_toprow;

/* GGR - FIX1 (version 2)
 * Add check for reaching end-of-buffer (== loop back to start) too
 * otherwise we process lines at start of file as if they are
 * beyond the end of it. (the "lp != " part).
 */
                while ((i < wp->w_toprow + wp->w_ntrows) &&
                       (lp != wp->w_bufp->b_linep)) {
                        if (vscreen[i]->v_flag & VFEXT) {
                                if ((wp != curwp) || (lp != wp->w_dotp) ||
                                    (curcol < term.t_ncol - 1)) {
                                        vtmove(i, 0);
                                        show_line(lp);
                                        vteeol();

                                        /* this line no longer is extended */
                                        vscreen[i]->v_flag &= ~VFEXT;
                                        vscreen[i]->v_flag |= VFCHG;
                                }
                        }
                        lp = lforw(lp);
                        ++i;
                }
                /* and onward to the next window */
                wp = wp->w_wndp;
        }
}

/*
 * updgar:
 *      if the screen is garbage, clear the physical screen and
 *      the virtual screen and force a full update
 */
void updgar(void)
{
        struct glyph *txt;
        int i, j;

/* GGR - include the last row, so <=, (for mini-buffer) */
        for (i = 0; i <= term.t_nrow; ++i) {
                vscreen[i]->v_flag |= VFCHG;
#if     REVSTA
                vscreen[i]->v_flag &= ~VFREV;
#endif
#if     COLOR
                vscreen[i]->v_fcolor = gfcolor;
                vscreen[i]->v_bcolor = gbcolor;
#endif
#if     MEMMAP == 0 || SCROLLCODE
                txt = pscreen[i]->v_text;
                for (j = 0; j < term.t_ncol; ++j)
                        set_glyph(txt+j, ' ');
#endif
        }

        movecursor(0, 0);       /* Erase the screen. */
        (*term.t_eeop) ();
        sgarbf = FALSE;         /* Erase-page clears */
        mpresf = FALSE;         /* the message area. */
#if     COLOR
        mlerase();              /* needs to be cleared if colored */
#endif
}

/*
 * updupd:
 *      update the physical screen from the virtual screen
 *
 * int force;           forced update flag
 */
int updupd(int force)
{
        struct video *vp1;
        int i;
#if SCROLLCODE
        if (scrflags & WFKILLS)
                scrolls(FALSE);
        if (scrflags & WFINS)
                scrolls(TRUE);
        scrflags = 0;
#endif

/* GGR - include the last row, so <=, (for mini-buffer) */
        for (i = 0; i <= term.t_nrow; ++i) {
                vp1 = vscreen[i];

                /* for each line that needs to be updated */
                if ((vp1->v_flag & VFCHG) != 0) {
#if     TYPEAH && ! PKCODE
                        if (force == FALSE && typahead())
                                return TRUE;
#endif
#if     MEMMAP && ! SCROLLCODE
                        updateline(i, vp1, NULL);
#else
                        updateline(i, vp1, pscreen[i]);
#endif
                }
        }
        return TRUE;
}

#if SCROLLCODE

/*
 * optimize out scrolls (line breaks, and newlines)
 * arg. chooses between looking for inserts or deletes
 */
static int scrolls(int inserts)
{                               /* returns true if it does something */
        struct video *vpv;      /* virtual screen image */
        struct video *vpp;      /* physical screen image */
        int i, j, k;
        int rows, cols;
        int first, match, count, target, end;
        int longmatch, longcount;
        int from, to;

        if (!term.t_scroll)     /* no way to scroll */
                return FALSE;

        rows = term.t_nrow;
        cols = term.t_ncol;

        first = -1;
        for (i = 0; i < rows; i++) {    /* find first wrong line */
                if (!texttest(i, i)) {
                        first = i;
                        break;
                }
        }

        if (first < 0)
                return FALSE;   /* no text changes */

        vpv = vscreen[first];
        vpp = pscreen[first];

        if (inserts) {
                /* determine types of potential scrolls */
                end = endofline(vpv->v_text, cols);
                if (end == 0)
                        target = first; /* newlines */
                else if (glyph_same_array(vpp->v_text, vpv->v_text, end))
                        target = first + 1;     /* broken line newlines */
                else
                        target = first;
        } else {
                target = first + 1;
        }

        /* find the matching shifted area */
        match = -1;
        longmatch = -1;
        longcount = 0;
        from = target;
        for (i = from + 1; i < rows - longcount /* P.K. */ ; i++) {
                if (inserts ? texttest(i, from) : texttest(from, i)) {
                        match = i;
                        count = 1;
                        for (j = match + 1, k = from + 1;
                             j < rows && k < rows; j++, k++) {
                                if (inserts ? texttest(j, k) :
                                    texttest(k, j))
                                        count++;
                                else
                                        break;
                        }
                        if (longcount < count) {
                                longcount = count;
                                longmatch = match;
                        }
                }
        }
        match = longmatch;
        count = longcount;

        if (!inserts) {
                /* full kill case? */
                if (match > 0 && texttest(first, match - 1)) {
                        target--;
                        match--;
                        count++;
                }
        }

        /* do the scroll */
        if (match > 0 && count > 2) {   /* got a scroll */
                /* move the count lines starting at target to match */
                if (inserts) {
                        from = target;
                        to = match;
                } else {
                        from = match;
                        to = target;
                }
                if (2 * count < abs(from - to))
                        return FALSE;
                scrscroll(from, to, count);
                for (i = 0; i < count; i++) {
                        vpp = pscreen[to + i];
                        vpv = vscreen[to + i];
                        memcpy(vpp->v_text, vpv->v_text,
                                sizeof(struct glyph)*cols);
                        vpp->v_flag = vpv->v_flag;      /* XXX */
                        if (vpp->v_flag & VFREV) {
                                vpp->v_flag &= ~VFREV;
                                vpp->v_flag |= ~VFREQ;
                        }
#if     MEMMAP
                        vscreen[to + i]->v_flag &= ~VFCHG;
#endif
                }
                if (inserts) {
                        from = target;
                        to = match;
                } else {
                        from = target + count;
                        to = match + count;
                }
#if     MEMMAP == 0
                for (i = from; i < to; i++) {
                        struct glyph *txt;
                        txt = pscreen[i]->v_text;
                        for (j = 0; j < term.t_ncol; ++j)
                                set_glyph(txt+j, ' ');
                        vscreen[i]->v_flag |= VFCHG;
                }
#endif
                return TRUE;
        }
        return FALSE;
}

/* move the "count" lines starting at "from" to "to" */
static void scrscroll(int from, int to, int count)
{
        ttrow = ttcol = -1;
        (*term.t_scroll) (from, to, count);
}

/*
 * return TRUE on text match
 *
 * int vrow, prow;              virtual, physical rows
 */
static int texttest(int vrow, int prow)
{
        struct video *vpv = vscreen[vrow];      /* virtual screen image */
        struct video *vpp = pscreen[prow];      /* physical screen image */

        return glyph_same_array(vpv->v_text, vpp->v_text, term.t_ncol);
}

/*
 * return the index of the first blank of trailing whitespace
 */
static int endofline(struct glyph *s, int n)
{
        int i;
        for (i = n - 1; i >= 0; i--)
                if (!is_space(s+i))
                        return i + 1;
        return 0;
}

#endif                          /* SCROLLCODE */

/*
 * updext:
 *      update the extended line which the cursor is currently
 *      on at a column greater than the terminal width. The line
 *      will be scrolled right or left to let the user see where
 *      the cursor is
 */
static void updext(void)
{
        int rcursor;                /* real cursor location */
        struct line *lp;            /* pointer to current line */

        /* calculate what column the real cursor will end up in */
        rcursor = ((curcol - term.t_ncol) % term.t_scrsiz) + term.t_margin;
        taboff = lbound = curcol - rcursor + 1;

        /* scan through the line outputing characters to the virtual screen */
        /* once we reach the left edge                                  */
        vtmove(currow, -lbound);    /* start scanning offscreen */
        lp = curwp->w_dotp;         /* line to output */
        show_line(lp);

        /* truncate the virtual line, restore tab offset */
        vteeol();
        taboff = 0;

        /* and put a '$' in column 1 */
        set_glyph(&(vscreen[currow]->v_text[0]), '$');
}

/*
 * Update a single line. This does not know how to use insert or delete
 * character sequences; we are using VT52 functionality. Update the physical
 * row and column variables. It does try an exploit erase to end of line. The
 * RAINBOW version of this routine uses fast video.
 */
#if     MEMMAP
/*      UPDATELINE specific code for the IBM-PC and other compatables */

static int updateline(int row, struct video *vp1, struct video *vp2)
{
#if     SCROLLCODE
        struct glyph *cp1;
        struct glyph *cp2;
        int nch;

        cp1 = &vp1->v_text[0];
        cp2 = &vp2->v_text[0];
        nch = term.t_ncol;
        do {
                clone_glyph(cp2, cp1);
                ++cp2;
                ++cp1;
        }
        while (--nch);
#endif
#if     COLOR
        scwrite(row, vp1->v_text, vp1->v_rfcolor, vp1->v_rbcolor);
        vp1->v_fcolor = vp1->v_rfcolor;
        vp1->v_bcolor = vp1->v_rbcolor;
#else
        if (vp1->v_flag & VFREQ)
                scwrite(row, vp1->v_text, 0, 7);
        else
                scwrite(row, vp1->v_text, 7, 0);
#endif
        vp1->v_flag &= ~(VFCHG | VFCOL);        /* flag this line as changed */

}

#else

/*
 * updateline()
 *
 * int row;             row of screen to update
 * struct video *vp1;   virtual screen image
 * struct video *vp2;   physical screen image
 */
static int updateline(int row, struct video *vp1, struct video *vp2)
{
#if RAINBOW
/*      UPDATELINE specific code for the DEC rainbow 100 micro  */

        struct glyph *cp1;
        struct glyph *cp2;
        int nch;

        /* since we don't know how to make the rainbow do this, turn it off */
        flags &= (~VFREV & ~VFREQ);

        cp1 = &vp1->v_text[0];  /* Use fast video. */
        cp2 = &vp2->v_text[0];
        putline(row + 1, 1, cp1);
        nch = term.t_ncol;

        do {
                *cp2 = *cp1;
                ++cp2;
                ++cp1;
        }
        while (--nch);
        *flags &= ~VFCHG;
#else
/*      UPDATELINE code for all other versions          */

        struct glyph *cp1;
        struct glyph *cp2;
        struct glyph *cp3;
        struct glyph *cp4;
        struct glyph *cp5;
        int nbflag;             /* non-blanks to the right flag? */
        int rev;                /* reverse video flag */
        int req;                /* reverse video request flag */


        /* set up pointers to virtual and physical lines */
        cp1 = &vp1->v_text[0];
        cp2 = &vp2->v_text[0];

#if     COLOR
        TTforg(vp1->v_rfcolor);
        TTbacg(vp1->v_rbcolor);
#endif

#if     REVSTA | COLOR
        /* if we need to change the reverse video status of the
           current line, we need to re-write the entire line     */
        rev = (vp1->v_flag & VFREV) == VFREV;
        req = (vp1->v_flag & VFREQ) == VFREQ;
        if ((rev != req)
#if     COLOR
            || (vp1->v_fcolor != vp1->v_rfcolor)
            || (vp1->v_bcolor != vp1->v_rbcolor)
#endif
            ) {
                movecursor(row, 0);     /* Go to start of line. */
                /* set rev video if needed */
                if (rev != req)
                        (*term.t_rev) (req);

                /* scan through the line and dump it to the screen and
                   the virtual screen array                             */
                cp3 = &vp1->v_text[term.t_ncol];
                while (cp1 < cp3) {
                        TTputglyph(cp1);
                        clone_glyph(cp2++, cp1++);
                }
                /* turn rev video off */
                if (rev != req)
                        (*term.t_rev) (FALSE);

                /* update the needed flags */
                vp1->v_flag &= ~VFCHG;
                if (req)
                        vp1->v_flag |= VFREV;
                else
                        vp1->v_flag &= ~VFREV;
#if     COLOR
                vp1->v_fcolor = vp1->v_rfcolor;
                vp1->v_bcolor = vp1->v_rbcolor;
#endif
                return TRUE;
        }
#endif

        /* advance past any common chars at the left */
        while (cp1 != &vp1->v_text[term.t_ncol] &&
             glyph_same(cp1, cp2)) {
                ++cp1;
                ++cp2;
        }

/* This can still happen, even though we only call this routine on changed
 * lines. A hard update is always done when a line splits, a massive
 * change is done, or a buffer is displayed twice. This optimizes out most
 * of the excess updating. A lot of computes are used, but these tend to
 * be hard operations that do a lot of update, so I don't really care.
 */
        /* if both lines are the same, no update needs to be done */
        if (cp1 == &vp1->v_text[term.t_ncol]) {
                vp1->v_flag &= ~VFCHG;  /* flag this line is changed */
                return TRUE;
        }

        /* find out if there is a match on the right */
        nbflag = FALSE;
        cp3 = &vp1->v_text[term.t_ncol];
        cp4 = &vp2->v_text[term.t_ncol];

        while (glyph_same(&(cp3[-1]), &(cp4[-1]))) {
                --cp3;
                --cp4;
                if (!is_space(&(cp3[0])))   /* Note if any nonblank */
                        nbflag = TRUE;      /* in right match. */
        }

        cp5 = cp3;

        /* Erase to EOL ? */
        if (nbflag == FALSE && eolexist == TRUE && (req != TRUE)) {
                while (cp5 != cp1 && is_space(&(cp5[-1])))
                        --cp5;

                if (cp3 - cp5 <= 3)     /* Use only if erase is */
                        cp5 = cp3;      /* fewer characters. */
        }

        movecursor(row, cp1 - &vp1->v_text[0]); /* Go to start of line. */
#if     REVSTA
        TTrev(rev);
#endif

        while (cp1 != cp5) {    /* Ordinary. */
                TTputglyph(cp1);
                clone_glyph(cp2++, cp1++);
        }

        if (cp5 != cp3) {       /* Erase. */
                TTeeol();
                while (cp1 != cp3)
                        clone_glyph(cp2++, cp1++);
}
#if     REVSTA
        TTrev(FALSE);
#endif
        vp1->v_flag &= ~VFCHG;  /* flag this line as updated */
        return TRUE;
#endif
}
#endif

/*
 * Redisplay the mode line for the window pointed to by the "wp". This is the
 * only routine that has any idea of how the modeline is formatted. You can
 * change the modeline format by hacking at this routine. Called by "update"
 * any time there is a dirty window.
 */
static void modeline(struct window *wp)
{
        char *cp;
        int c;
        struct buffer *bp;
        int i;                  /* loop index */
        int lchar;              /* character to draw line in buffer with */
        int firstm;             /* is this the first mode? */
        char tline[NLINE];      /* buffer for part of mode line */

        int n = wp->w_toprow + wp->w_ntrows;            /* Location. */
        vscreen[n]->v_flag |= VFCHG | VFREQ | VFCOL;    /* Redraw next time. */
#if     COLOR
/* GGR - use configured colors, not 0 and 7 */
        vscreen[n]->v_rfcolor = gbcolor;    /* chosen for on */
        vscreen[n]->v_rbcolor = gfcolor;    /* chosen..... */
#endif
        vtmove(n, 0);           /* Seek to right line. */
        if (wp == curwp)        /* mark the current buffer */
#if     PKCODE && !GGR_MODE
                lchar = '-';
#else
                lchar = '=';
#endif
        else
#if     REVSTA
        if (revexist)
                lchar = ' ';
        else
#endif
                lchar = '-';

        bp = wp->w_bufp;
#if     PKCODE == 0
        if ((bp->b_flag & BFTRUNC) != 0)
                vtputc('#');
        else
#endif
                vtputc(lchar);

        if ((bp->b_flag & BFCHG) != 0)  /* "*" if changed. */
                vtputc('*');
        else
                vtputc(lchar);

        strcpy(tline, " ");
        strcat(tline, PROGRAM_NAME_LONG);
        strcat(tline, " ");

/* GGR - only of no filename (space issue) */
        if (bp->b_fname[0] == 0) {
                strcat(tline, " ");
                strcat(tline, VERSION);
        }
        strcat(tline, ": ");
        cp = &tline[0];
        while ((c = *cp++) != 0) vtputc(c);

        cp = &bp->b_bname[0];
        while ((c = *cp++) != 0) vtputc(c);

        strcpy(tline, " " MLpre);

        /* display the modes */

        firstm = TRUE;
        if ((bp->b_flag & BFTRUNC) != 0) {
                firstm = FALSE;
                strcat(tline, "Truncated");
        }
        for (i = 0; i < NUMMODES; i++)  /* add in the mode flags */
                if (wp->w_bufp->b_mode & (1 << i)) {
                        if (firstm != TRUE)
                                strcat(tline, " ");
                        firstm = FALSE;
                        strcat(tline, mode2name[i]);
                }
        strcat(tline, MLpost " ");

        cp = &tline[0];
        while ((c = *cp++) != 0) vtputc(c);

#if     PKCODE
        if (bp->b_fname[0] != 0 && strcmp(bp->b_bname, bp->b_fname) != 0)
#else
        if (bp->b_fname[0] != 0)        /* File name. */
#endif
        {
#if     PKCODE == 0
                cp = "File: ";
                while ((c = *cp++) != 0) vtputc(c);
#endif

                cp = &bp->b_fname[0];
                while ((c = *cp++) != 0) vtputc(c);
                vtputc(' ');
        }
/* Pad to full width. */
        while (vtcol < term.t_ncol) vtputc(lchar);

/* determine if top line, bottom line, or both are visible */

        struct line *lp = wp->w_linep;
        int rows = wp->w_ntrows;
        char *msg = NULL;

        vtcol -= 7;  /* strlen(" top ") plus a couple */
        while (rows--) {
                lp = lforw(lp);
                if (lp == wp->w_bufp->b_linep) {
                        msg = " Bot ";
                        break;
                }
        }
        if (lback(wp->w_linep) == wp->w_bufp->b_linep) {
                if (msg) {
                        if (wp->w_linep == wp->w_bufp->b_linep)
                                msg = " Emp ";
                        else
                                msg = " All ";
                } else {
                        msg = " Top ";
                }
        }
        if (!msg) {
                struct line *lp;
                int numlines, predlines, ratio;

                lp = lforw(bp->b_linep);
                numlines = 0;
                predlines = 0;
                while (lp != bp->b_linep) {
                        if (lp == wp->w_linep) {
                                predlines = numlines;
                        }
                        ++numlines;
                        lp = lforw(lp);
                }
                if (wp->w_dotp == bp->b_linep) {
                        msg = " Bot ";
                } else {
                        ratio = 0;
                        if (numlines != 0)
                                ratio = (100L * predlines) / numlines;
                        if (ratio > 99)
                                ratio = 99;
                        sprintf(tline, " %2d%% ", ratio);
                        msg = tline;
                }
        }

        cp = msg;
        while ((c = *cp++) != 0) {
                vtputc(c);
                ++n;
        }
}

void upmode(void)
{                               /* update all the mode lines */
        struct window *wp;

        wp = wheadp;
        while (wp != NULL) {
                wp->w_flag |= WFMODE;
                wp = wp->w_wndp;
        }
}

/*
 * Send a command to the terminal to move the hardware cursor to row "row"
 * and column "col". The row and column arguments are origin 0. Optimize out
 * random calls. Update "ttrow" and "ttcol".
 */
void movecursor(int row, int col)
{
        if (row != ttrow || col != ttcol) {
                ttrow = row;
                ttcol = col;
                TTmove(row, col);
        }
}

/*
 * Erase the message line. This is a special routine because the message line
 * is not considered to be part of the virtual screen. It always works
 * immediately; the terminal buffer is flushed via a call to the flusher.
 */
void mlerase(void)
{
        int i;

        movecursor(term.t_nrow, 0);
        if (discmd == FALSE)
                return;

#if     COLOR
/* GGR - use configured colors, not 7 and 0 */
        TTforg(gfcolor);
        TTbacg(gbcolor);
#endif
        if (eolexist == TRUE)
                TTeeol();
        else {
                for (i = 0; i < term.t_ncol - 1; i++)
                        TTputc(' ');            /* No need to update ttcol */
                movecursor(term.t_nrow, 1);     /* force the move! */
                movecursor(term.t_nrow, 0);
        }
        TTflush();
        mpresf = FALSE;
}

/*
 * Write a message into the message line. Keep track of the physical cursor
 * position. A small class of printf like format items is handled. Assumes the
 * stack grows down; this assumption is made by the "++" in the argument scan
 * loop. Set the "message line" flag TRUE.
 *
 * GGR modified to handle utf8 strings.
 *
 * char *fmt;           format string for output
 * char *arg;           pointer to first argument to print
 */
void mlwrite(const char *fmt, ...)
{
        unicode_t c;            /* current char in format string */
        va_list ap;

        /* if we are not currently echoing on the command line, abort this */
        if (discmd == FALSE) {
                movecursor(term.t_nrow, 0);
                return;
        }
#if     COLOR
        /* set up the proper colors for the command line */
/* GGR - use configured colors, not 7 and 0 */
        TTforg(gfcolor);
        TTbacg(gbcolor);
#endif

        /* if we can not erase to end-of-line, do it manually */
        if (eolexist == FALSE) {
                mlerase();
                TTflush();
        }
        movecursor(term.t_nrow, 0);
        va_start(ap, fmt);

/* GGR - loop through the bytes getting any utf8 sequence as unicode */
        int bytes_togo = strlen(fmt);
        while (bytes_togo > 0) {
            int used = utf8_to_unicode((char *)fmt, 0, bytes_togo, &c);
            bytes_togo -= used;
            fmt += used;
                if (c != '%') {
                        TTput1char(c);
                } else {
                        int used = utf8_to_unicode((char *)fmt, 0, bytes_togo, &c);
                        bytes_togo -= used;
                        fmt += used;

                        switch (c) {
                        case 'd':
                                mlputi(va_arg(ap, int), 10);
                                break;

                        case 'o':
                                mlputi(va_arg(ap, int), 8);
                                break;

                        case 'x':
                                mlputi(va_arg(ap, int), 16);
                                break;

                        case 'D':
                                mlputli(va_arg(ap, long), 10);
                                break;

                        case 's':
                                mlputs(va_arg(ap, char *));
                                break;

                        case 'f':
                                mlputf(va_arg(ap, int));
                                break;

                        default:
                                TTput1char(c);
                        }
                }
        }
        va_end(ap);

        /* if we can, erase to the end of screen */
        if (eolexist == TRUE)
                TTeeol();
        TTflush();
        mpresf = TRUE;
}

/*
 * Force a string out to the message line regardless of the
 * current $discmd setting. This is needed when $debug is TRUE
 * and for the write-message and clear-message-line commands
 *
 * char *s;             string to force out
 */
void mlforce(char *s)
{
        int oldcmd;     /* original command display flag */

        oldcmd = discmd;        /* save the discmd value */
        discmd = TRUE;          /* and turn display on */
        mlwrite(s);             /* write the string out */
        discmd = oldcmd;        /* and restore the original setting */
}

/*
 * Write out a string. Update the physical cursor position. This assumes that
 * the characters in the string all have width "1"; if this is not the case
 * things will get screwed up a little.
 *
 * GGR - modified to handle utf8 strings.
 */
void mlputs(char *s)
{
        unicode_t c;

        int nbytes = strlen(s);
        int idx = 0;
        while (idx <= nbytes) {
                idx += utf8_to_unicode(s, idx, nbytes, &c);
                TTput1char(c);
        }
}

/*
 * Write out an integer, in the specified radix. Update the physical cursor
 * position.
 */
static void mlputi(int i, int r)
{
        int q;
        static char hexdigits[] = "0123456789ABCDEF";

        if (i < 0) {
                i = -i;
                TTput1char('-');
        }

        q = i / r;

        if (q != 0)
                mlputi(q, r);

        TTput1char(hexdigits[i % r]);
}

/*
 * do the same except as a long integer.
 */
static void mlputli(long l, int r)
{
        long q;

        if (l < 0) {
                l = -l;
                TTput1char('-');
        }

        q = l / r;

        if (q != 0)
                mlputli(q, r);

        TTput1char((int) (l % r) + '0');
}

/*
 * write out a scaled integer with two decimal places
 *
 * int s;               scaled integer to output
 */
static void mlputf(int s)
{
        int i;                  /* integer portion of number */
        int f;                  /* fractional portion of number */

        /* break it up */
        i = s / 100;
        f = s % 100;

        /* send out the integer portion */
        mlputi(i, 10);
        TTput1char('.');
        TTput1char((f / 10) + '0');
        TTput1char((f % 10) + '0');
}

#if RAINBOW

static void putline(int row, int col, char *buf)
{
        int n;

        n = strlen(buf);
        if (col + n - 1 > term.t_ncol)
                n = term.t_ncol - col + 1;
        Put_Data(row, col, n, buf);
}
#endif

/* Get terminal size from system.
   Store number of lines into *heightp and width into *widthp.
   If zero or a negative number is stored, the value is not valid.  */

void getscreensize(int *widthp, int *heightp)
{
#ifdef TIOCGWINSZ
        struct winsize size;
        *widthp = 0;
        *heightp = 0;
        if (ioctl(0, TIOCGWINSZ, &size) < 0)
                return;
        *widthp = size.ws_col;
        *heightp = size.ws_row;
#else
        *widthp = 0;
        *heightp = 0;
#endif
}

#ifdef SIGWINCH
void sizesignal(int signr)
{
        int w, h;
        int old_errno = errno;

        getscreensize(&w, &h);

        if (h && w && (h - 1 != term.t_nrow || w != term.t_ncol))
                newscreensize(h, w);

        struct sigaction sigact, oldact;
        sigact.sa_handler = sizesignal;
        sigact.sa_flags = SA_RESTART;
        sigaction(SIGWINCH, &sigact, &oldact);
        errno = old_errno;
}

static int newscreensize(int h, int w)
{
        /* do the change later */
        if (displaying) {
                chg_width = w;
                chg_height = h;
                return FALSE;
        }
        chg_width = chg_height = 0;
        if (h - 1 < term.t_mrow)
                newsize(TRUE, h);
        if (w < term.t_mcol)
                newwidth(TRUE, w);

/* GGR - We have to clear the last line, as we display it and it
 *       probably won't be empty if we are shrinking the window.
 *       Needs to run before the update.
 */
        mberase();
        update(TRUE);

        return TRUE;
}

#endif


/* GGR
 *    function to erase the mapped minibuffer line
 *    (so different from mlerase()
 */
void mberase(void)
{
        struct video *vp1;

                vtmove(term.t_nrow, 0);
                vteeol();
                vp1 = vscreen[term.t_nrow];
#if COLOR
                vp1->v_fcolor = gfcolor;
                vp1->v_bcolor = gbcolor;
#endif

#if     MEMMAP
                        updateline(term.t_nrow, vp1, NULL);
#else
                        updateline(term.t_nrow, vp1, pscreen[term.t_nrow]);
#endif
                return;
}

/* GGR
 *    function to update *just* the minibuffer window
 */
void mbupdate(void)
{
    mbonly = TRUE;
    update(TRUE);
    mbonly = FALSE;
}
