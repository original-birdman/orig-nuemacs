/*      edef.h
 *
 *      Global variable definitions
 *
 *      written by Dave G. Conroy
 *      modified by Steve Wilhite, George Jones
 *      greatly modified by Daniel Lawrence
 *      modified by Petri Kutvonen
 */
#ifndef EDEF_H_
#define EDEF_H_

#include <stdlib.h>
#include <string.h>
#include "utf8.h"

/* Initialized global external declarations. */

extern int fillcol;             /* Fill column                  */
extern int kbdm[];              /* Holds kayboard macro data    */
extern char pat[];              /* Search pattern               */
extern char rpat[];             /* Replacement pattern          */
extern char *execstr;           /* pointer to string to execute */
extern char golabel[];          /* current line to go to        */
extern int execlevel;           /* execution IF level           */
extern int eolexist;            /* does clear to EOL exist?     */
extern int revexist;            /* does reverse video exist?    */
extern int flickcode;           /* do flicker supression?       */
extern char *modename[];        /* text names of modes          */
extern char *mode2name[];       /* text names of modes          */
extern char modecode[];         /* letters to represent modes   */
extern struct key_tab *keytab;  /* key bind to functions table  */
extern struct name_bind names[];/* name to function table */
extern int gmode;               /* global editor mode           */
extern int gflags;              /* global control flag          */
extern int gfcolor;             /* global forgrnd color (white) */
extern int gbcolor;             /* global backgrnd color (black) */
extern int gasave;              /* global ASAVE size            */
extern int gacount;             /* count until next ASAVE       */
extern int sgarbf;              /* State of screen unknown      */
extern int mpresf;              /* Stuff in message line        */
extern int clexec;              /* command line execution flag  */
extern int mstore;              /* storing text to macro flag   */
extern int discmd;              /* display command flag         */
extern int disinp;              /* display input characters     */
extern struct buffer *bstore;   /* buffer to store macro text to */
extern int ttrow;               /* Row location of HW cursor */
extern int ttcol;               /* Column location of HW cursor */
extern int lbound;              /* leftmost column of current line
                                   being displayed */
extern int metac;               /* current meta character */
extern int ctlxc;               /* current control X prefix char */
extern int reptc;               /* current universal repeat char */
extern int abortc;              /* current abort command char   */

extern int quotec;              /* quote char during mlreply() */
extern int tabmask;
extern char *cname[];           /* names of colors              */
extern struct kill *kbufp;      /* current kill buffer chunk pointer */
extern struct kill *kbufh;      /* kill buffer header pointer   */
extern int kused;               /* # of bytes used in KB        */
extern struct window *swindow;  /* saved window pointer         */
extern int cryptflag;           /* currently encrypting?        */
extern int *kbdptr;             /* current position in keyboard buf */
extern int *kbdend;             /* ptr to end of the keyboard */
extern int kbdmode;             /* current keyboard macro mode  */
extern int kbdrep;              /* number of repetitions        */
extern int restflag;            /* restricted use?              */
extern int lastkey;             /* last keystoke                */
extern int seed;                /* random number seed           */
extern long envram;             /* # of bytes current in use by malloc */
extern int macbug;              /* macro debuging flag          */
extern char errorm[];           /* error literal                */
extern char truem[];            /* true literal                 */
extern char falsem[];           /* false litereal               */
extern int cmdstatus;           /* last command status          */
extern char palstr[];           /* palette string               */
extern int saveflag;            /* Flags, saved with the $target var */
extern struct line *fline;      /* dynamic return line */
extern int rval;                /* return value of a subprocess */
extern int nullflag;
extern int overlap;             /* line overlap in forw/back page */
extern int scrolljump;          /* number of lines to jump on scroll */

/* Uninitialized global external declarations. */

extern int currow;              /* Cursor row                   */
extern int curcol;              /* Cursor column                */
extern int thisflag;            /* Flags, this command          */
extern int lastflag;            /* Flags, last command          */
extern int curgoal;             /* Goal for C-P, C-N            */
extern struct window *curwp;    /* Current window               */
extern struct buffer *curbp;    /* Current buffer               */
extern struct window *wheadp;   /* Head of list of windows      */
extern struct buffer *bheadp;   /* Head of list of buffers      */
extern struct buffer *blistp;   /* Buffer for C-X C-B           */

extern char sres[NBUFN];        /* Current screen resolution.   */
extern char pat[];              /* Search pattern.              */
extern char tap[];              /* Reversed pattern array.      */
extern char rpat[];             /* Replacement pattern.         */

extern unsigned int matchlen;
extern unsigned int mlenold;
extern char *patmatch;
extern struct line *matchline;
extern int matchoff;

extern char *dname[];           /* Directive name table.        */

#if     DEBUGM
/* Vars needed for macro debugging output. */
extern char outline[];          /* Global string to hold debug line text. */
#endif

/* Terminal table defined only in term.c */
extern struct terminal term;

#endif  /* EDEF_H_ */

/* GGR - Additional declarations */
extern int clast;               /* Last command (for re-execute) */
extern int flast;               /* "f" option to last command */
extern int nlast;               /* "n" option to last command */
extern int inreex;              /* Set when re-executing */

extern unicode_t *eos_list;     /* List of end-of-sentence characters */
extern int inmb;                /* Set when in minibuffer */
extern char lastmb[];           /* Contents of last minibuffer */
extern int pathexpand;          /* Whether to expand paths */
extern int silent;              /* Set for "no message line info" */
extern char savnam[];           /* Saved buffer name */
extern int using_ggr_style;     /* Macro-settable options flag */
extern int allow_current;       /* Look in current dir for startup? */

extern int chg_width;           /* Changed width on SIGWINCH */
extern int chg_height;          /* Changed height on SIGWINCH */

extern char *input_waiting;     /* Input ready (for execproc) */

extern int keytab_alloc_ents;   /* Allocated number of keytab entries */

extern struct buffer *ptt;      /* Global pt table */

extern int hscroll;             /* TRUE when we are scrolling horizontally */
extern int hjump;               /* How much to jump on horizontal scroll */

extern char kbdmacro_buffer[];  /* The name of the keyboard macro buffer */
extern struct buffer *kbdmac_bp;    /* keyboard macro buffer */
