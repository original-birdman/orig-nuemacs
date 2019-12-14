/*      input.c
 *
 *      Various input routines
 *
 *      written by Daniel Lawrence 5/9/86
 *      modified by Petri Kutvonen
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"

#include "utf8proc.h"

/* GGR - some needed things for minibuffer handling */
#if UNIX
#include <signal.h>
#ifdef SIGWINCH
static int remap_c_on_intr = 0;
#endif
#endif
#include "line.h"

/* A set of functions to do with filename/buffer/name completions.
 * Part of the GGR additions.
 */

#include <string.h>

static char picture[NFILEN], directory[NFILEN];

static struct buffer *expandbp;

#define COMPFILE 1
#define COMPBUFF 2
#define COMPPROC 3
#define COMPNAME 4
#define COMPVAR  5

/* Static variable shared by comp_file(), comp_buff() and matcher() */

static char so_far[NFILEN];     /* Maximal match so far */

static short ljust(char *str) {
    char *p, *q;
    short justified;

    justified = FALSE;
    p = str;
    while (*p++ == ' ') justified = TRUE;
    p--;
    if (justified) {
        q = str;
        while (*p!=0) *q++ = *p++;
        *q = 0;
    }
    return(justified);
}

/* getnfile() and getffile()
 * Handle file name completion
 *
 * Here is what getffile() should do.
 * Parse out any directory part and and file part.
 * Open that directory, if necessary.
 * Build a wildcard from the file part.
 * Call getnfile() to return the first match.
 * If the system is not case-sensitive, force lower case.
 */
#include <sys/types.h>

#if USG
#include <dirent.h>
#else
#include <sys/dir.h>
#endif

#include <sys/stat.h>

static DIR *dirptr;
static int piclen;
static char *nameptr;
static char suggestion[NFILEN];
static short allfiles;
static char fullname[NFILEN];

static void close_dir(void) {
    if (dirptr != NULL) closedir(dirptr);
}

static char *getnfile(void) {
    unsigned short type;        /* file type */
#if USG
    struct dirent *dp;
#else
    struct direct *dp;
#endif
    struct stat statbuf;
    int namelen;

/* Get the next directory entry, if no more, we finish */
    if ((dp = readdir(dirptr)) == NULL) return(NULL);

    if ((allfiles ||
         (((namelen = strlen(dp->d_name)) >= piclen) &&
         (!strncmp(dp->d_name, picture, piclen)))) &&
         (strcmp(dp->d_name, ".")) &&
         (strcmp(dp->d_name, ".."))) {

        strcpy(nameptr, dp->d_name);
        stat(fullname, &statbuf);
        type = (statbuf.st_mode & S_IFMT);
        if ((type == S_IFREG)
#ifdef S_IFLNK
             || (type == S_IFLNK)
#endif
             || (type == S_IFDIR)) {
            strcpy(suggestion, dp->d_name);
            if (type == S_IFDIR) strcat(suggestion, "/");
            return(suggestion);
        }
    }
    return(getnfile());
}

static char *getffile(char *fspec) {
    char *p;                        /* handy pointers */

    dirptr = NULL;                  /* Initialise things */

    fixup_fname(fspec);
    strcpy(directory, fspec);

    if ((p = strrchr(directory, '/'))) {
        p++;
        strcpy(picture, p);
        *p = 0;
        strcpy(fullname, directory);
    }
    else {
        strcpy(picture, directory);
        directory[0] = 0;
        fullname[0] = 0;
    }

    nameptr = &fullname[strlen(fullname)];

    if (!directory[0]) dirptr = opendir(".");
    else               dirptr = opendir(directory);

    if (dirptr == NULL) return(NULL);

    piclen = strlen(picture);
    allfiles = (piclen == 0);

/* And return the first match (we return ONLY matches, for speed) */
    return(getnfile());
}

/* getnbuffer() and getfbuffer()
 * Handle buffer name completion
 * Also handles userproc name completion, as those are just buffers with
 * with names starting '/' and a type of BTPROC.
 */

static char *getnbuffer(char *bpic, int bpiclen, int mtype) {
    char *retptr;

    if (expandbp) {
/* We NEVER return minibuffer buffers (CC$00nnn), and we return internal
 * [xx] buffers only if the user asked for them by specifying a picture
 * starting with [.
 * For a type of COMPPROC we only consider buffer-names starting with '/'
 * with a b_type of BTPROC. We return the name *without* the leading '/'.
 */

        int offset;
            if (mtype == COMPPROC)  offset = 1;
            else                    offset = 0;

/* We don't return a buffer if it doesn't match, or if it's a minibuffer
 * buffername (CC$) or if it's an internal buffer [xx], unless the user
 * *asked* for these.
 */
        if ((mtype == COMPPROC &&
              (expandbp->b_type != BTPROC || expandbp->b_bname[0] != '/')) ||
            (strncmp(bpic, expandbp->b_bname + offset, bpiclen)) ||
            (!strncmp(expandbp->b_bname, "CC$", 3)) ||
            ((expandbp->b_bname[0] == '[') && bpiclen == 0)) {

            expandbp = expandbp->b_bufp;
            return(getnbuffer(bpic, bpiclen, mtype));
        }
        else {
            retptr = expandbp->b_bname + offset;
            expandbp = expandbp->b_bufp;
            return(retptr);
        }
    }
    else
        return(NULL);
}

static char *getfbuffer(char *bpic, int bpiclen, int mtype) {
    expandbp = bheadp;
    return(getnbuffer(bpic, bpiclen, mtype));
}

/* getnname() and getfname()
 * Handle internal command name completions.
 *
 * Just uses the sorted index to step through the names in order.
 */

static int n_nidx;
static char *getfname(char *name, int namelen) {
    n_nidx = -1;
    while ((n_nidx = nxti_name_info(n_nidx)) >= 0) {    /* -1 at end of list */
        if (strncmp(name, names[n_nidx].n_name, namelen) == 0)
            return names[n_nidx].n_name;
    }
    return NULL;
}

static char *getnname(char *name, int namelen) {
    while ((n_nidx = nxti_name_info(n_nidx)) >= 0) {
        if (strncmp(name, names[n_nidx].n_name, namelen) == 0)
            return names[n_nidx].n_name;
        else
            return NULL;
    }
    return NULL;
}

/* getnvar() and getfvar()
 * Handle internal env/user variable completions.
 *
 * We build the env variable sort index in getfvar once
 * We build the use variable sort index in getfvar each time.
 * However iff the incoming text is blank we just want to show
 * $... %..., and if it doesn't start with $ or % we want show no match.
 */
extern int *envvar_index;
extern int *usrvar_index;
extern char *uvnames[];
extern struct evlist evl[];

/* Since we know the string will be copied immediatey after return we
 * can just put it into a static buffer here
 */

static char *varcat(char pfx, char *name) {
    static char pfxd_name[NVSIZE + 1];
    pfxd_name[0] = pfx;
    strcpy(pfxd_name+1, name);
    return pfxd_name;
}

static int nul_state;   /* 0 = doing $, 1 = doing %, 2 = done */
static int n_evidx, n_uvidx;
static char *getnvar(char *name, int namelen) {
    if (namelen == 0) {
        if (nul_state == 1) {
            nul_state = 2;
            return "%...";
        }
        else {
            return NULL;
        }
    }
    namelen--;      /* We don't compare the leading $ or % */
    char *mp = name + 1;
    if (name[0] == '$') {
/* -1 at end of list */
        while ((n_evidx = nxti_envvar(n_evidx)) >= 0) {
            if (strncmp(mp, evl[n_evidx].var, namelen) == 0)
                return varcat('$', evl[n_evidx].var);
        }
    }
    if (name[0] == '%') {
        while ((n_uvidx = nxti_usrvar(n_uvidx)) >= 0) {
            if (strncmp(mp, uvnames[n_uvidx], namelen) == 0)
                return varcat('%', uvnames[n_uvidx]);
        }
    }
    return NULL;
}

static char *getfvar(char *name, int namelen) {
    nul_state = 0;
    if (namelen == 0) {
        nul_state = 1;
        return "$...";
    }
/* Set up sort index and array */
    if (name[0] == '$') {
        if (envvar_index == NULL) init_envvar_index();
        n_evidx = -1;
    }
    if (name[0] == '%') {
        sort_user_var();
        n_uvidx = -1;
    }
    return getnvar(name, namelen);
}

/* matcher()
 * A routine to do the matching for completion code.
 * The routine to get the next item to check is determined by mtype.
 */
static int matcher(char *name, int namelen, char *choices, int mtype) {
    char *next;                 /* Next filename to look at */
    int match_length;           /* Maximal match length     */
    char *p, *q;                /* Handy pointers           */
    int max, l, unique;

    match_length = strlen(so_far);

/* Restrict length of returned string to number of columns, so that we
 * don't end up wrapping in the minibuffer line.
 */
    max = term.t_ncol;
#if BSD
    max--;
#endif
    l = (match_length < max) ? match_length : max;
/* We also need to check we are not going to overflow the
 * destination buffer, and we have to allow for the final NUL
 */
    if (l >= NFILEN) l = NFILEN - 1;
    so_far[l] = '\0';           /* Ensure this will fit into choices */
    strcpy(choices, so_far);
    max -= l;
    unique = TRUE;
    while (1) {             /* Rather than try to put the switch in there. */
        switch(mtype) {
        case COMPFILE:
            next = getnfile();
            break;
        case COMPBUFF:
        case COMPPROC:
            next = getnbuffer(name, namelen, mtype);
            break;
        case COMPNAME:
            next = getnname(name, namelen);
            break;
        case COMPVAR:
            next = getnvar(name, namelen);
            break;
        default:            /* If mtype arrives oddly? */
            next = NULL;
        }
        if (next == NULL) break;

        unique = FALSE;
        for (p = so_far, q = next, match_length = 0;
            (*p && *q && (*p == *q)); p++, q++)
            match_length++;
        so_far[match_length] = 0;
        l = strlen(next);
        if (max == 0) {
            if (match_length == 0)
                break;
        }
        else if (l < max) {
            strcat(choices, " ");
            strcat(choices, next);
            max -= (l + 1);
        }
        else {
            l = (max - 1);
            strcat(choices, " ");
            strncat(choices, next, l);
            max = 0;
        }
    }
    return unique;
}

/* Entry point for filename completion
 * Looks for things starting with name, and if there are multiple choices
 * builds a catenated string of them in choices.
 */
static int comp_file(char *name, char *choices) {
    char supplied[NFILEN];  /* Maximal match so far */
    char *p;                /* Handy pointer */
    int unique;

    *choices = 0;

/* Left-justify the user's input */
    ljust(name);
    if ((p = getffile(name)) == NULL) {
        close_dir();
        return(FALSE);
    }
    else
        strcpy(so_far, p);

    strcpy(supplied, name);
    unique = matcher(name, 0, choices, COMPFILE);
    close_dir();
    if (directory[0]) {
        strcpy(name, directory);
        strcat(name, so_far);
    }
    else {
        strcpy(name, so_far);
    }

    if (unique && strcmp(name, supplied))
        *choices = 0;
    return(TRUE);
}

/* Generic entry point for completions where the getf* function selects
 * names from an internal list.
 * Use by mtype COMPBUFF/COMPPROC and COMPNAME.
 * The getf* function to use is determined by mtype.
 * Looks for things starting with name, and if there are multiple choices
 * builds a catenated string of them in choices.
 */
static int comp_gen(char *name, char *choices, int mtype) {
    char supplied[NFILEN];  /* Maximal match so far */
    char *p;                /* Handy pointer */
    int unique;
    int namelen;

    *choices = 0;
    ljust(name);
    namelen = strlen(name);
    switch(mtype) {
    case COMPBUFF:
    case COMPPROC:
        p = getfbuffer(name, namelen, mtype);
        break;
    case COMPNAME:
        p = getfname(name, namelen);
        break;
    case COMPVAR:
        p = getfvar(name, namelen);
        break;
    default:            /* If mtype arrives oddly? */
        p = NULL;
    }
    if (p == NULL) return(FALSE);
    strcpy(so_far, p);
    strcpy(supplied, name);
    unique = matcher(name, namelen, choices, mtype);
    strcpy(name, so_far);

    if (unique && strcmp(name, supplied)) *choices = 0;

    return(TRUE);
}

/* Entry point for buffer name completion. And userprocs.
 * Front-end to comp_gen().
 */
static int comp_buffer(char *name, char *choices, int mtype) {
    return comp_gen(name, choices, mtype);
}

/* Entry point for internal command name completion
 * Front-end to comp_gen().
 */
static int comp_name(char *name, char *choices) {
    return comp_gen(name, choices, COMPNAME);
}

/* Entry point for internal environment/user var completion
 * Front-end to comp_gen().
 */
static int comp_var(char *name, char *choices) {
    return comp_gen(name, choices, COMPVAR);
}


/* Ask a yes or no question in the message line. Return either TRUE, FALSE, or
 * ABORT. The ABORT status is returned if the user bumps out of the question
 * with a ^G. Used any time a confirmation is required.
 */
int mlyesno(char *prompt) {
    int c;                  /* input character - tgetc() returns unicode */
    char buf[NPAT];         /* prompt to user */

    int res = -1;           /* NOT ABORT, TRUE or FALSE */
    while(res == -1) {
        strcpy(buf, prompt);    /* build and prompt the user */
        strcat(buf, " " MLbkt("y/n") "? ");
        mlwrite(buf);

        c = tgetc();        /* get the response */

        if (c == ectoc(abortc)) res = ABORT;
        else if (c == 'y' || c == 'Y') res = TRUE;
        else if (c == 'n' || c == 'N') res = FALSE;
    }
    mlerase();
    return res;
}

/* Write a prompt into the message line, then read back a response. Keep
 * track of the physical position of the cursor. If we are in a keyboard
 * macro throw the prompt away, and return the remembered response. This
 * lets macros run at full speed. The reply is always terminated by a carriage
 * return. Handle erase, kill, and abort keys.
 * NOTE that when the function is processing macros from a (start-up) file
 * it will get one arg at a time.
 * BUT when run interactively it will return the entire response.
 * So macro-file args need to be quoted...
 * We pass on any expansion-type requested (for, eventually, getstring()).
 */

int mlreply(char *prompt, char *buf, int nbuf, int exp_type) {
    return nextarg(prompt, buf, nbuf, exp_type);
}

/*
 * ectoc:
 *      expanded character to character
 *      collapse the CONTROL and SPEC flags back into an ascii code
 */
int ectoc(int c) {
    if (c & CONTROL) c = c & ~(CONTROL | 0x40);
    if (c & SPEC)    c = c & 255;
    return c;
}

/*
 * get a command name from the command line. Command completion means
 * that pressing a <SPACE> will attempt to complete an unfinished command
 * name if it is unique.
 */
struct name_bind *getname(char *prompt) {
    char buf[NSTRING];      /* buffer to hold tentative command name */

/* First get the name... */
    if (clexec == FALSE) {
        if (mlreply(prompt, buf, NSTRING, EXPNAME) != TRUE) return NULL;
    }
    else {      /* macro line argument - grab token and skip it */
        execstr = token(execstr, buf, NSTRING - 1);
   }

/* Now check it exists */

    struct name_bind *nbp = name_info(buf);
    if (nbp) {
        if (kbdmode == RECORD) addto_kbdmacro(buf, 1, 0);
        return nbp;
    }
    mlwrite("No such function: %s", buf);
    return NULL;
}

/*      tgetc:  Get a key from the terminal driver, resolve any keyboard
                macro action                                    */

int tgetc(void) {
    int c;                      /* fetched character */

/* If we are playing a keyboard macro back, */
    if (kbdmode == PLAY) {
        if (kbdptr < kbdend)    /* if there is some left... */
            return (int) *kbdptr++;
        if (--kbdrep < 1) {     /* at the end of last repetition? */
            kbdmode = STOP;     /* Leave ctlxe_togo alone */
#if VISMAC == 0
            update(FALSE);      /* force a screen update after all is done */
#endif
        }
        else {
            kbdptr = kbdm;      /* reset macro to beginning for the next rep */
            f_arg = p_arg;      /* Restore original f_arg */
            return (int) *kbdptr++;
        }
    }

/* Fetch a character from the terminal driver */
#ifdef SIGWINCH
    struct sigaction sigact;
    if (remap_c_on_intr) {
        sigaction(SIGWINCH, NULL, &sigact);
        sigact.sa_flags = 0;
        sigaction(SIGWINCH, &sigact, NULL);
        errno = 0;
    }
#endif
    c = TTgetc();
#ifdef SIGWINCH
    if (remap_c_on_intr) {
        sigaction(SIGWINCH, NULL, &sigact);
        sigact.sa_flags = SA_RESTART;
        sigaction(SIGWINCH, &sigact, NULL);
    }
#endif
/* Record it for $lastkey */
    lastkey = c;

#ifdef SIGWINCH
    if (c == 0 && errno == EINTR && remap_c_on_intr)
        c = UEM_NOCHAR;         /* Note illegal char */
    else
#endif
/* Save it if we need to */
        if (kbdmode == RECORD) {
            *kbdptr++ = c;
            kbdend = kbdptr;
            if (kbdptr == &kbdm[NKBDM - 1]) {   /* don't overrun buffer */
                kbdmode = STOP; /* Must be collecting - leave ctlxe_togo */
                TTbeep();
            }
        }

/* And finally give the char back */
    return c;
}

/* get1key: Get one keystroke.
 *          The only prefixes legal here are SPEC and CONTROL.
 */
int get1key(void) {
    int c;

    c = tgetc();                    /* get a keystroke */
    if (c >= 0x00 && c <= 0x1F)     /* C0 control -> C-     */
        c = CONTROL | (c + '@');
    return c;
}

/* Force a char to uppercase
 * We expect c to mostly be ASCII, so shortcut that case
 * Used several times in getcmd
 */
static int ensure_uppercase(int c) {
    if (c <= 0x7f) {
        if (c >= 'a' && c <= 'z') c ^= DIFCASE;
    }
    else {
        c = utf8proc_toupper(c);    /* Unchanged if not lower/title case */
    }
    return c;
}

/* getcmd: Get a command from the keyboard.
 *         Process all applicable prefix keys
 */

int getcmd(void) {
    int c;                  /* fetched keystroke */
#if VT220
    int d;                  /* second character P.K. */
    int cmask = 0;
#endif
    c = get1key();          /* get initial character */

#if VT220
proc_metac:
    if (c == 128+27)        /* CSI - ~equiv to Esc[ */
        goto handle_CSI;
#endif

    if (c == (CONTROL | '[')) {     /* Process META prefix */
        c = get1key();
#if VT220
        if (c == '[' || c == 'O') { /* CSI P.K. */
handle_CSI:
            c = get1key();
            if (c >= 'A' && c <= 'D') return SPEC | c | cmask;
            if (c >= 'E' && c <= 'z' && c != 'i' && c != 'c')
                return SPEC | c | cmask;
            d = get1key();
            if (d == '~')   /* ESC [ n ~   P.K. */
                return SPEC | c | cmask;
            switch (c) {    /* ESC [ n n ~ P.K. */
            case '1':
                c = d + 32;
                break;
            case '2':
                c = d + 48;
                break;
            case '3':
                c = d + 64;
                break;
            default:
                c = '?';
                break;
            }
            if (d != '~')   /* eat tilde P.K. */
                get1key();
            if (c == 'i') { /* DO key    P.K. */
                c = ctlxc;
                goto proc_ctlxc;
            }
            else if (c == 'c')    /* ESC key   P.K. */
                    c = get1key();
                else
                    return SPEC | c | cmask;
        }
        if (c == (CONTROL | '[')) {
            cmask = META;
            goto proc_metac;
        }
#endif
        c = ensure_uppercase(c);
        if (c >= 0x00 && c <= 0x1F)     /* control key */
            c = CONTROL | (c + '@');
        return META | c;
    }
    else if (c == metac) {
        c = get1key();
#if VT220
        if (c == (CONTROL | '[')) {
            cmask = META;
            goto proc_metac;
        }
#endif
        c = ensure_uppercase(c);
        if (c >= 0x00 && c <= 0x1F)     /* control key */
            c = CONTROL | (c + '@');
        return META | c;
    }

#if VT220
proc_ctlxc:
#endif
    if (c == ctlxc) {                   /* process CTLX prefix */
        c = get1key();
#if VT220
        if (c == (CONTROL | '[')) {
            cmask = CTLX;
            goto proc_metac;
        }
#endif
        c = ensure_uppercase(c);
        if (c >= 0x00 && c <= 0x1F)     /* control key */
            c = CONTROL | (c + '@');
        return CTLX | c;
    }

/* Otherwise, just return it */
    return c;
}

/* GGR
 * A version of getstring in which the minibuffer is a true buffer!
 * Note that this loops for each character, so you can manipulate the
 * prompt etc. by providing updated info and using it after the
 * loop: label.
 */

/* If the window size changes whilst this is running the display will end
 * up incorrect (as the code reckons we are in this buffer, not the one we
 * arrived from).
 * So set up a SIGWINCH handler to get us out the minibuffer before
 * display::newscreensize() runs.
 */

static struct window *mb_winp = NULL;

#ifdef SIGWINCH

typedef void (*sighandler_t)(int);

void sigwinch_handler(int signr) {

    UNUSED(signr);

/* We need to get back to how things were before we arrived in teh
 * minibuffer.
 * So we save the current settings, restore the originals, let the
 * resize code run, re-fetch teh original (in case they ahve changed)
 * the restore the ones we arrived with.
 */
    struct buffer *mb_bp = curbp;
    struct window *mb_wp = curwp;
    struct window *mb_hp = wheadp;

    curbp = mb_info.main_bp;
    curwp = mb_info.main_wp;
    wheadp = mb_info.wheadp;
    inmb = FALSE;

/* Do the bits from sizesignal() in display.c that do the work */
    int w, h;

    getscreensize(&w, &h);
    if (h && w && (h - 1 != term.t_nrow || w != term.t_ncol))
        newscreensize(h, w, 0);

/* Need to reget the mb_info data now */

    mb_info.main_bp = curbp;
    mb_info.main_wp = curwp;
    mb_info.wheadp = wheadp;

/* Now get back to how we arrived */

    curbp = mb_bp;
    curwp = mb_wp;
    wheadp = mb_hp;
    curwp->w_toprow = term.t_nrow;  /* Set new value */
    inmb = TRUE;
/* Ensure the minibuffer is redrawn */
    mbupdate();

    return;
}
#endif

int getstring(char *prompt, char *buf, int nbuf, int exp_type) {
    struct buffer *bp;
    struct buffer *cb;
    char mbname[NBUFN];
    int    c;               /* command character */
    struct line *lp;        /* line to copy */
    int size;
    char *sp;               /* string pointer into line */
    char tstring[NSTRING];
    char choices[1000];     /* MUST be > max likely window width */
    int status;
    func_arg sav;

    short savdoto;
    int prolen;
    char procopy[NSTRING];

/* gcc 4.4.7 is happy to report "may be used initialized" for wsave
 * (which is wrong) even though it doesn't have a -Wmaybe-uninitialized
 * option.
 * Since we aren't using -Winit-self we can initialize it to itself
 * and the optimizer will optimize it away (!?!).
 * From Flexo's answer at:
 *   https://stackoverflow.com/questions/5080848/disable-gcc-may-be-used-uninitialized-on-a-particular-variable
 */
    struct window wsave = wsave;

#ifdef SIGWINCH
/* We need to block SIGWINCH until we have set-up all of the variables
 * we need after the longjmp.
 */
    sigset_t sigwinch_set, incoming_set;
    sigemptyset(&sigwinch_set);
    sigaddset(&sigwinch_set, SIGWINCH);
    sigprocmask(SIG_BLOCK, &sigwinch_set, &incoming_set);
#endif

/* Create a minibuffer window for use by all minibuffers */
    if (!mb_winp) {
        mb_winp = (struct window *)Xmalloc(sizeof(struct window));
        mb_winp->w_wndp = NULL;               /* Initialize window    */
#if COLOR
/* initalize colors to global defaults */
        mb_winp->w_fcolor = gfcolor;
        mb_winp->w_bcolor = gbcolor;
#endif
        mb_winp->w_toprow = term.t_nrow;
        mb_winp->w_ntrows = 1;
        mb_winp->w_fcol = 0;
        mb_winp->w_force = 0;
        mb_winp->w_flag = WFMODE | WFHARD;    /* Full.                */
    }

/* GGR NOTE!!
 * We want to ensure that we don't return with garbage in buf, but we
 * can't initialize it to empty here, as some callers use it as a
 * temporary buffer for the prompt!
 */

/* Expansion commands leave junk in mb */

    mlerase();
    sprintf(mbname, "//minib%04d", mb_info.mbdepth+1);
    cb = curbp;

/* Update main screen before entering minibuffer */

    update(FALSE);

    if ((bp = bfind(mbname, TRUE, BFINVS)) == NULL) {
        buf = "";               /* Ensure we never return garbage */
#ifdef SIGWINCH
        sigprocmask(SIG_SETMASK, &incoming_set, NULL);
#endif
        return(FALSE);
    }

/* Save real reexecution history */

    sav = f_arg;

/* Remember the original buffer name if at level 1 */

/* Get the PHON state from the current buffer, so we can carry it to
 * the next minibuffer.
 * We *don't* carry back any change on return!
 */
    int new_bmode;
    if (curbp->b_mode & MDPHON) new_bmode = MDEXACT | MDPHON;
    else                        new_bmode = MDEXACT;

    if (++mb_info.mbdepth == 1) {   /* Arrival into minibuffer */
        mb_info.main_bp = curbp;    /* For main buffer info in modeline */
        mb_info.main_wp = curwp;    /* Used to position modeline */
        mb_info.wheadp = wheadp;    /* ??? */
        inmb = TRUE;
    }
    else {                          /* Save this minibuffer for recursion */
        wsave = *curwp;             /* Structure copy... */
    }
/* Switch to the minibuffer window */
    curwp = mb_winp;
    wheadp = curwp;

/* Set-up the (incoming) prompt string and clear any prompt update flag,
 * as it should only get set during loop:
 */
    strcpy(procopy, prompt);
    prolen = strlen(procopy);
    prmpt_buf.update = 0;

    if (mpresf) mlerase();
    mberase();

    if (!swbuffer(bp, 0)) return FALSE;

    curwp->w_toprow = term.t_nrow;
    curwp->w_ntrows = 1;
    curbp->b_mode = new_bmode;

#ifdef SIGWINCH
/* The oldact is restored on exit. */
    struct sigaction sigact, oldact;
    sigact.sa_handler = sigwinch_handler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sigact, &oldact);
/* Now we can enable the signal */
    sigprocmask(SIG_SETMASK, &incoming_set, NULL);
#endif

/* A copy of the main.c command loop from 3.9e, but things are a
 *  *little* different here..
 */
loop:

/* Execute the "command" macro...normally null */
    saveflag = lastflag;        /* preserve lastflag through this */
    execute(META|SPEC|'C', FALSE, 1);
    lastflag = saveflag;

/* Have we been asked to update the prompt? */

    if (prmpt_buf.update) {
        strcpy(procopy, prmpt_buf.prompt);
        prolen = strlen(procopy);
        prmpt_buf.update = 0;
    }

/* Have we been asked to load (a search string)? */
    if (prmpt_buf.preload) {
        linstr(prmpt_buf.preload);
        prmpt_buf.preload = NULL;    /* One-time usage */
    }

/* Prepend the prompt to the beginning of the visible line */
    savdoto = curwp->w_doto;
    curwp->w_doto = 0;
    linstr(procopy);
    curwp->w_doto = savdoto + prolen;

/* Fix up the screen - we do NOT want horizontal scrolling in the mb */

    int real_hscroll;
    real_hscroll = hscroll;
    hscroll = FALSE;
    curwp->w_flag |= WFMODE;    /* Need to update the modeline... */
    mbupdate();                 /* Will set modeline to prompt... */
    hscroll = real_hscroll;

/* Remove the prompt from the beginning of the visible line */
    curwp->w_doto = 0;
    ldelete((long)prolen, FALSE);
    curwp->w_doto = savdoto;

/* Get the next command (character) from the keyboard */
#ifdef SIGWINCH
    remap_c_on_intr = 1;
#endif
    c = getcmd();
/* We get UEM_NOCHAR back on a sigwin signal. */
#ifdef SIGWINCH
    remap_c_on_intr = 0;
    if (c == UEM_NOCHAR) goto loop;
#endif

/* Filename expansion code:
 * a list of matches is temporarily displayed in the minibuffer.
 * THIS ONLY USES THE TEXT ON THE CURRENT LINE, and uses ALL OF IT.
 */
    if (c == (CONTROL|'I')) {
        lp = curwp->w_dotp;
        sp = lp->l_text;
/* NSTRING-1, as we need to add a trailing NUL */
        if (lp->l_used < NSTRING-1) {
            int expanded;
            memcpy(tstring, sp, lp->l_used);
            tstring[lp->l_used] = '\0';
            switch(exp_type) {
            case EXPFILE:
                expanded = comp_file(tstring, choices);
                break;
            case EXPBUF:
                expanded = comp_buffer(tstring, choices, COMPBUFF);
                break;
            case EXPPROC:
                expanded = comp_buffer(tstring, choices, COMPPROC);
                break;
            case EXPNAME:
                expanded = comp_name(tstring, choices);
                break;
            case EXPVAR:
                expanded = comp_var(tstring, choices);
                break;
            default:
                expanded = 0;
            }
            if (expanded) {
                savdoto = curwp->w_doto;
                curwp->w_doto = 0;
                ldelete((long) lp->l_used, FALSE);
                linstr(tstring);
                if (choices[0]) {
                    mlwrite_one(choices);
                    size = (strlen(choices) < 42) ? 1 : 2;
                    sleep(size);
                    mlerase();
                    mberase();
                }
            }
            else
                TTbeep();
            goto loop;
        }
        else {
            TTbeep();
            goto loop;
        }
    }
/* End of filename expansion code */

/* Check for any numeric prefix */

    com_arg *carg = multiplier_check(c);

/* Intercept minibuffer specials.. <NL> and ^G */

    if ((carg->c == (CONTROL|'M')) || (carg->c == (CTLX|CONTROL|'C')))
        goto submit;
    else if (carg->c == (CONTROL|'G')) {
        status = ABORT;
        buf = "";               /* Don't return garbage */
        goto abort;
    }

/* ...and execute the command. DON'T MACRO_CAPTURE THIS!!
 * No need to mlerase any numeric arg as either:
 *  o the command will edit the minibuffer, which will be redrawn, or
 *  o the command will execute, so the minibuffer will be erased anyway.
 */

    execute(carg->c, carg->f, carg->n);
    if (mpresf) {
        sleep(1);
        mlerase();
        mberase();
    }

/* Whatever dumb modes, they've put on, allow only the sensible... */

    curbp->b_mode &= (MDEXACT|MDOVER|MDMAGIC|MDPHON|MDEQUIV);
    goto loop;

submit:     /* Tidy up */
    status = TRUE;

/* Find the contents of the current buffer, and its length, including
 * newlines to join lines, but excluding the final newline.
 * Only do this upto the requested return length.
 */

    struct line *mblp = lforw(bp->b_linep);
    int sofar = 0;
    int maxadd = nbuf - 1;
    while (mblp != bp->b_linep && sofar < maxadd) {
        if (sofar != 0) buf[sofar++] = '\n';    /* Add NL if not first */
        int add = llength(mblp);
        if ((sofar + add) > maxadd) add = maxadd - sofar;
        memcpy(buf+sofar, mblp->l_text, add);
        sofar += add;
        mblp = lforw(mblp);
    }
    buf[sofar] = '\0';          /* Add the NUL */

/* Need to copy to return buffer and, if not empty,
 * to save as last minibuffer.
 */
    int retlen = sofar;         /* Without terminating NUL */
    if (retlen) {
        if (retlen >= NSTRING) retlen = NSTRING - 1;
        addto_lastmb_ring(buf); /* Terminating NULL is actually there */
    }
    else status = FALSE;        /* Empty input... */

/* Record the result if we are recording a keyboard macro, but only
 * at first level of the minibuffer (i.e. the "true" result),
 * and only if we have some text.
 */
    if ((*buf != '\0') && (kbdmode == RECORD) && (mb_info.mbdepth == 1))
         addto_kbdmacro(buf, 0, 1);

abort:

#ifdef SIGWINCH
/* If we get here "normally" SIGWINCH will still be enabled, so we need
 * to block it while we tidy up, otherwise we might run through this end
 * code twice.
 */
    sigprocmask(SIG_BLOCK, &sigwinch_set, NULL);
#endif

    if (!swbuffer(bp, 0))   /* Make sure we're still in our minibuffer */
        return FALSE;
    unmark(TRUE, 1);

    if (--mb_info.mbdepth) {        /* Back to previous mini-buffer */
        if (!swbuffer(cb, 0))       /* Switch to its buffer... */
            return FALSE;
        *curwp = wsave;             /* ...and its window info */
    }
    else {                              /* Leaving minibuffer */
        if (!swbuffer(mb_info.main_bp, 0))  /* Switch back to main buffer */
            return FALSE;
        curwp = mb_info.main_wp;        /* Reset window info ptr */
        wheadp = mb_info.wheadp;        /* Reset window list */
        inmb = FALSE;                   /* Note we have left mb */
    }
    curwp->w_flag |= WFMODE;        /* Forces modeline redraw */
    zotbuf(bp);                     /* Remove this minibuffer */

/* Restore real reexecution history */

    f_arg = sav;

    mberase();
    if (status == ABORT) {
        ctrlg(FALSE, 0);
        TTflush();
    }

#ifdef SIGWINCH
/* We need to re-instate the original handler now... */
    sigaction(SIGWINCH, &oldact, NULL);
    sigprocmask(SIG_SETMASK, &incoming_set, NULL);
#endif

    return(status);
}
