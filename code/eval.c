/*      eval.c
 *
 *      Expression evaluation functions
 *
 *      written 1986 by Daniel Lawrence
 *      modified by Petri Kutvonen
 */

#include <stdio.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "evar.h"
#include "line.h"
#include "util.h"
#include "version.h"

#define MAXVARS 255

/*
 * return some of the contents of the kill buffer
 */
static char *getkill(void) {
    int size;                       /* max number of chars to return */
    static char value[NSTRING];     /* fixed buffer for value */

    if (kbufh[0] == NULL)
                /* no kill buffer....just a null string */
        value[0] = 0;
    else {      /* copy in the contents...allow for a trailing NUL */
        if (kused[0] < NSTRING) size = kused[0];
        else                    size = NSTRING - 1;
        memcpy(value, kbufh[0]->d_chunk, size);
        *(value+size) = '\0';
    }
    return value;       /* Return the constructed value */
}


/* User variables */
static struct user_variable uv[MAXVARS + 1];

/* Initialize the user variable list. */
void varinit(void) {
    int i;
    for (i = 0; i < MAXVARS; i++) uv[i].u_name[0] = 0;
}

/*
 * convert a string to a numeric logical
 * Used by exec.c
 *
 * char *val;           value to check for stol
 */
int stol(char *val) {
/* check for logical values */
    if (val[0] == 'F') return FALSE;
    if (val[0] == 'T') return TRUE;

/* check for numeric truth (!= 0) */
    return (atoi(val) != 0);
}

/*
 * numeric logical to string logical
 *
 * int val;             value to translate
 */
static char *ltos(int val) {
    if (val) return truem;
    else     return falsem;
}

/*
 * make a string upper case
 * Internal to this routine - only needs to handle ASCII
 *
 * char *str;           string to upper case
 */
static char *mkupper(char *str)
{
        char *sp;

        sp = str;
        while (*sp) {
                if ('a' <= *sp && *sp <= 'z')
                        *sp += 'A' - 'a';
                ++sp;
        }
        return str;
}

/*
 * make a string lower case
 * Internal to this routine - only needs to handle ASCII
 *
 * char *str;           string to lower case
 */
static char *mklower(char *str) {
    char *sp;

    sp = str;
    while (*sp) {
        if ('A' <= *sp && *sp <= 'Z') *sp += 'a' - 'A';
        ++sp;
    }
    return str;
}

/*
 * take the absolute value of an integer
 * also used by idxsorter.c
 */
int abs(int x) {
    return x < 0 ? -x : x;
}

/*
 * returns a random integer
 */
static int ernd(void) {
    seed = abs(seed * 1721 + 10007);
    return seed;
}

/*
 * find pattern within source
 *
 * char *source;        source string to search
 * char *pattern;       string to look for
 */
static int sindex(char *source, char *pattern) {
    char *sp;               /* ptr to current position to scan */
    char *csp;              /* ptr to source string during comparison */
    char *cp;               /* ptr to place to check for equality */

/* Scanning through the source string */
    sp = source;
    while (*sp) {           /* Scan through the pattern */
        cp = pattern;
        csp = sp;
        while (*cp) {
            if (!eq(*cp, *csp)) break;
            ++cp;
            ++csp;
        }

/* Was it a match? */
        if (*cp == 0) return (int) (sp - source) + 1;
        ++sp;
    }

/* No match at all.. */
    return 0;
}

/*
 * Filter a string through a translation table
 *
 * char *source;        string to filter
 * char *lookup;        characters to translate
 * char *trans;         resulting translated characters
 */
static char *xlat(char *source, char *lookup, char *trans) {
    char *sp;       /* pointer into source table */
    char *lp;       /* pointer into lookup table */
    char *rp;       /* pointer into result */
    static char result[NSTRING];    /* temporary result */

/* Scan source string */
    sp = source;
    rp = result;
    while (*sp) {   /* scan lookup table for a match */
        lp = lookup;
        while (*lp) {
            if (*sp == *lp) {
                *rp++ = trans[lp - lookup];
                goto xnext;
            }
            ++lp;
        }
/* No match, copy in the source char untranslated */
        *rp++ = *sp;
        xnext:++sp;
    }

/* Terminate and return the result */
    *rp = 0;
    return result;
}

/*
 * Evaluate a function.
 *
 * @fname: name of function to evaluate.
 */
static char *gtfun(char *fname) {
    unsigned int fnum;      /* index to function to eval */
    int status;             /* return status */
    char *tsp;              /* temporary string pointer */
    char arg1[NSTRING];     /* value of first argument */
    char arg2[NSTRING];     /* value of second argument */
    char arg3[NSTRING];     /* value of third argument */
    static char result[2 * NSTRING];        /* string result */

/* Look the function up in the function table */
    fname[3] = 0;           /* only first 3 chars significant */
    mklower(fname);         /* and let it be upper or lower case */
    for (fnum = 0; fnum < ARRAY_SIZE(funcs); fnum++)
        if (strcmp(fname, funcs[fnum].f_name) == 0) break;

/* Return errorm on a bad reference */
    if (fnum == ARRAY_SIZE(funcs)) return errorm;

/* If needed, retrieve the first argument */
    if (funcs[fnum].f_type >= MONAMIC) {
        if ((status = macarg(arg1)) != TRUE) return errorm;

/* If needed, retrieve the second argument */
        if (funcs[fnum].f_type >= DYNAMIC) {
            if ((status = macarg(arg2)) != TRUE) return errorm;

/* If needed, retrieve the third argument */
            if (funcs[fnum].f_type >= TRINAMIC)
                if ((status = macarg(arg3)) != TRUE) return errorm;
        }
    }


/* And now evaluate it! */
    switch (funcs[fnum].tag) {
    case UFADD:         return ue_itoa(atoi(arg1) + atoi(arg2));
    case UFSUB:         return ue_itoa(atoi(arg1) - atoi(arg2));
    case UFTIMES:       return ue_itoa(atoi(arg1) * atoi(arg2));
    case UFDIV:         return ue_itoa(atoi(arg1) / atoi(arg2));
    case UFMOD:         return ue_itoa(atoi(arg1) % atoi(arg2));
    case UFNEG:         return ue_itoa(-atoi(arg1));
    case UFCAT:
        strcpy(result, arg1);
        return strcat(result, arg2);
    case UFLEFT:        return strncpy(result, arg1, atoi(arg2));
    case UFRIGHT:
        return (strcpy(result, &arg1[(strlen(arg1) - atoi(arg2))]));
    case UFMID:
        return (strncpy(result, &arg1[atoi(arg2) - 1], atoi(arg3)));
    case UFNOT:         return ltos(stol(arg1) == FALSE);
    case UFEQUAL:       return ltos(atoi(arg1) == atoi(arg2));
    case UFLESS:        return ltos(atoi(arg1) < atoi(arg2));
    case UFGREATER:     return ltos(atoi(arg1) > atoi(arg2));
    case UFSEQUAL:      return ltos(strcmp(arg1, arg2) == 0);
    case UFSLESS:       return ltos(strcmp(arg1, arg2) < 0);
    case UFSGREAT:      return ltos(strcmp(arg1, arg2) > 0);
    case UFIND:         return strcpy(result, getval(arg1));
    case UFAND:         return ltos(stol(arg1) && stol(arg2));
    case UFOR:          return ltos(stol(arg1) || stol(arg2));
    case UFLENGTH:      return ue_itoa(strlen(arg1));
    case UFUPPER:
        strcpy(result, arg1);
        return mkupper(result);
    case UFLOWER:
        strcpy(result, arg1);
        return mklower(result);
    case UFTRUTH:       return ltos(atoi(arg1) == 42);
    case UFASCII:       return ue_itoa((int) arg1[0]);
    case UFCHR:
        result[0] = atoi(arg1);
        result[1] = 0;
        return result;
    case UFGTKEY:
        result[0] = tgetc();
        result[1] = 0;
        return result;
    case UFRND:         return ue_itoa((ernd() % abs(atoi(arg1))) + 1);
    case UFABS:         return ue_itoa(abs(atoi(arg1)));
    case UFSINDEX:      return ue_itoa(sindex(arg1, arg2));
    case UFENV:
#if     ENVFUNC
        tsp = getenv(arg1);
        return tsp == NULL ? "" : tsp;
#else
        return "";
#endif
    case UFBIND:        return transbind(arg1);
    case UFEXIST:       return ltos(fexist(arg1));
    case UFFIND:
        tsp = flook(arg1, TRUE, ONPATH);
        return tsp == NULL ? "" : tsp;
    case UFBAND:        return ue_itoa(atoi(arg1) & atoi(arg2));
    case UFBOR:         return ue_itoa(atoi(arg1) | atoi(arg2));
    case UFBXOR:        return ue_itoa(atoi(arg1) ^ atoi(arg2));
    case UFBNOT:        return ue_itoa(~atoi(arg1));
    case UFXLATE:       return xlat(arg1, arg2, arg3);
    }

    exit(-11);              /* never should get here */
}

/*
 * look up a user var's value
 *
 * char *vname;                 name of user variable to fetch
 */
static char *gtusr(char *vname) {
    int vnum;       /* Ordinal number of user var */

/* Scan the list looking for the user var name */
    for (vnum = 0; vnum < MAXVARS; vnum++) {
        if (uv[vnum].u_name[0] == 0) return errorm;
        if (strcmp(vname, uv[vnum].u_name) == 0) return uv[vnum].u_value;
    }

/* Return errorm if we run off the end */
    return errorm;
}

/*
 * gtenv()
 *
 * char *vname;                 name of environment variable to retrieve
 */
static char *gtenv(char *vname) {
    unsigned int vnum;  /* ordinal number of var referenced */

/* Scan the list, looking for the referenced name */
    for (vnum = 0; vnum < ARRAY_SIZE(evl); vnum++)
        if (strcmp(vname, evl[vnum].var) == 0) break;

/* Return errorm on a bad reference */
    if (vnum == ARRAY_SIZE(evl))
#if     ENVFUNC
    {
        char *ename = getenv(vname);
        if (ename != NULL)
            return ename;
        else
            return errorm;
    }
#else
    return errorm;
#endif

/* Otherwise, fetch the appropriate value */
    switch (evl[vnum].tag) {
    case EVFILLCOL:         return ue_itoa(fillcol);
    case EVPAGELEN:         return ue_itoa(term.t_nrow + 1);
    case EVCURCOL:          return ue_itoa(getccol(FALSE));
    case EVCURLINE:         return ue_itoa(getcline());
    case EVRAM:             return ue_itoa((int) (envram / 1024l));
    case EVFLICKER:         return ltos(flickcode);
    case EVCURWIDTH:        return ue_itoa(term.t_ncol);
    case EVCBUFNAME:        return curbp->b_bname;
    case EVCFNAME:          return curbp->b_fname;
    case EVSRES:            return sres;
    case EVDEBUG:           return ltos(macbug);
    case EVSTATUS:          return ltos(cmdstatus);
    case EVPALETTE:         return palstr;
    case EVASAVE:           return ue_itoa(gasave);
    case EVACOUNT:          return ue_itoa(gacount);
    case EVLASTKEY:         return ue_itoa(lastkey);
    case EVCURCHAR:
        return (curwp->w_dotp->l_used == curwp->w_doto ?
            ue_itoa('\n') :
            ue_itoa(lgetc(curwp->w_dotp, curwp->w_doto)));
    case EVDISCMD:          return ltos(discmd);
    case EVVERSION:         return VERSION;
    case EVPROGNAME:        return PROGRAM_NAME_LONG;
    case EVSEED:            return ue_itoa(seed);
    case EVDISINP:          return ltos(disinp);
    case EVWLINE:           return ue_itoa(curwp->w_ntrows);
    case EVCWLINE:          return ue_itoa(getwpos());
    case EVTARGET:
        saveflag = lastflag;
        return ue_itoa(curgoal);
    case EVSEARCH:          return pat;
    case EVREPLACE:         return rpat;
    case EVMATCH:           return (patmatch == NULL) ? "" : patmatch;
    case EVKILL:            return getkill();
    case EVCMODE:           return ue_itoa(curbp->b_mode);
    case EVGMODE:           return ue_itoa(gmode);
    case EVTPAUSE:          return ue_itoa(term.t_pause);
    case EVPENDING:         return ltos(typahead());
    case EVLWIDTH:          return ue_itoa(llength(curwp->w_dotp));
    case EVLINE:            return getctext();
    case EVGFLAGS:          return ue_itoa(gflags);
    case EVRVAL:            return ue_itoa(rval);
    case EVTAB:             return ue_itoa(tabmask + 1);
    case EVOVERLAP:         return ue_itoa(overlap);
    case EVSCROLLJUMP:      return ue_itoa(scrolljump);
    case EVSCROLL:          return ltos(term.t_scroll != NULL);
    case EVINMB:            return ue_itoa(inmb);
    case EVFCOL:            return(ue_itoa(curwp->w_fcol));
    case EVHSCROLL:         return(ltos(hscroll));
    case EVHJUMP:           return(ue_itoa(hjump));
    case EVYANKMODE:        switch (yank_mode) {
                            case Old:
                                return("old");
                                break;
                            case GNU:
                                return("gnu");
                                break;
                            }
                            return("");
                            break;
    case EVAUTOCLEAN:       return(ue_itoa(autoclean));
    case EVREGLTEXT:        return regionlist_text;
    case EVREGLNUM:         return regionlist_number;
    case EVAUTODOS:         return(ltos(autodos));
    }
    exit(-12);              /* again, we should never get here */
}

/*
 * Find a variables type and name.
 *
 * @var: name of variable to get.
 * @vd: structure to hold type and pointer.
 * @size: size of variable array.
 */
static void findvar(char *var, struct variable_description *vd, int size) {
    unsigned int vnum;  /* subscript in variable arrays */
    int vtype;          /* type to return */

    vnum = -1;
fvar:
    vtype = -1;
    switch (var[0]) {

    case '$':               /* Check for legal enviromnent var */
        for (vnum = 0; vnum < ARRAY_SIZE(evl); vnum++)
            if (strcmp(&var[1], evl[vnum].var) == 0) {
                vtype = TKENV;
                break;
            }
        break;

    case '%':               /* Check for existing legal user variable */
        for (vnum = 0; vnum < MAXVARS; vnum++)
            if (strcmp(&var[1], uv[vnum].u_name) == 0) {
                vtype = TKVAR;
                break;
            }
        if (vnum < MAXVARS) break;
        for (vnum = 0; vnum < MAXVARS; vnum++)  /* Create a new one??? */
            if (uv[vnum].u_name[0] == 0) {
                vtype = TKVAR;
                strcpy(uv[vnum].u_name, &var[1]);
                break;
            }
        break;

    case '&':               /* Indirect operator? */
        var[4] = 0;
        if (strcmp(&var[1], "ind") == 0) {  /* Grab token, and eval it */
            execstr = token(execstr, var, size);
            strcpy(var, getval(var));
            goto fvar;
        }
    }

/* Return the results */
    vd->v_num = vnum;
    vd->v_type = vtype;
    return;
}

/*
 * Set a variable.
 *
 * @var: variable to set.
 * @value: value to set to.
 */
static int svar(struct variable_description *var, char *value) {
    int vnum;       /* ordinal number of var referenced */
    int vtype;      /* type of variable to set */
    int status;     /* status return */
    int c;          /* translated character */
    char *sp;       /* scratch string pointer */

/* simplify the vd structure (we are gonna look at it a lot) */
    vnum = var->v_num;
    vtype = var->v_type;

/* and set the appropriate value */
    status = TRUE;
    switch (vtype) {
    case TKVAR:             /* set a user variable */
        if (uv[vnum].u_value != NULL) free(uv[vnum].u_value);
        sp = malloc(strlen(value) + 1);
        if (sp == NULL) {
            uv[vnum].u_value = NULL;    /* GGR */
            return FALSE;
        }
        strcpy(sp, value);
        uv[vnum].u_value = sp;
        break;

    case TKENV:             /* set an environment variable */
        status = TRUE;  /* by default */
        switch (vnum) {
        case EVFILLCOL:
            fillcol = atoi(value);
            break;
        case EVPAGELEN:
            status = newsize(TRUE, atoi(value));
            break;
        case EVCURCOL:
            status = setccol(atoi(value));
            break;
        case EVCURLINE:
            status = gotoline(TRUE, atoi(value));
            break;
        case EVRAM:
            break;
        case EVFLICKER:
            flickcode = stol(value);
            break;
        case EVCURWIDTH:
            status = newwidth(TRUE, atoi(value));
            break;
        case EVCBUFNAME:
            strcpy(curbp->b_bname, value);
            curwp->w_flag |= WFMODE;
            break;
        case EVCFNAME:
            strcpy(curbp->b_fname, value);
            curwp->w_flag |= WFMODE;
            break;
        case EVSRES:
            status = TTrez(value);
            break;
        case EVDEBUG:
            macbug = stol(value);
            break;
        case EVSTATUS:
            cmdstatus = stol(value);
            break;
        case EVASAVE:
            gasave = atoi(value);
            break;
        case EVACOUNT:
            gacount = atoi(value);
            break;
        case EVLASTKEY:
            lastkey = atoi(value);
            break;
        case EVCURCHAR:
            ldelgrapheme(1, FALSE);     /* delete 1 char-place */
            c = atoi(value);
            if (c == '\n') lnewline();
            else           linsert_uc(1, c);
            back_grapheme(1);
            break;
        case EVDISCMD:
            discmd = stol(value);
            break;
        case EVVERSION:
            break;
        case EVPROGNAME:
            break;
        case EVSEED:
            seed = atoi(value);
            break;
        case EVDISINP:
            disinp = stol(value);
            break;
        case EVWLINE:
            status = resize(TRUE, atoi(value));
            break;
        case EVCWLINE:
            status = forwline(TRUE, atoi(value) - getwpos());
            break;
        case EVTARGET:
            curgoal = atoi(value);
            thisflag = saveflag;
            break;
        case EVSEARCH:
            strcpy(pat, value);
            rvstrcpy(tap, pat);
#if     MAGIC
            mcclear();
#endif
            new_prompt(value);  /* Let gestring() know, via the search code */
            break;
        case EVREPLACE:
            strcpy(rpat, value);
            new_prompt(value);  /* Let gestring() know, via the search code */
            break;
        case EVMATCH:
            break;
        case EVKILL:
            break;
        case EVCMODE:
            curbp->b_mode = atoi(value);
            curwp->w_flag |= WFMODE;
            break;
        case EVGMODE:
            gmode = atoi(value);
            break;
        case EVTPAUSE:
            term.t_pause = atoi(value);
            break;
        case EVPENDING:
            break;
        case EVLWIDTH:
            break;
        case EVLINE:
            putctext(value);
            break;
        case EVGFLAGS:
            gflags = atoi(value);
            break;
        case EVRVAL:
            break;
        case EVTAB:
            tabmask = atoi(value) - 1;
            if (tabmask != 0x07 && tabmask != 0x03)
            tabmask = 0x07;
            curwp->w_flag |= WFHARD;
            break;
        case EVOVERLAP:
            overlap = atoi(value);
            break;
        case EVSCROLLJUMP:
            scrolljump = atoi(value);
            break;
        case EVSCROLL:
            if (!stol(value)) term.t_scroll = NULL;
            break;
        case EVINMB:
            break;
        case EVFCOL:
            curwp->w_fcol = atoi(value);
            if (curwp->w_fcol < 0) curwp->w_fcol = 0;
            curwp->w_flag |= WFHARD | WFMODE;
            break;
        case EVHSCROLL:
            hscroll = stol(value);
            lbound = 0;
            break;
        case EVHJUMP:
            hjump = atoi(value);
            if (hjump < 1) hjump = 1;
            if (hjump > term.t_ncol - 1) hjump = term.t_ncol - 1;
            break;
        case EVYANKMODE:
            if (strcmp("old", value) == 0)
                yank_mode = Old;
            else if (strcmp("gnu", value) == 0)
                yank_mode = GNU;
            break;      /* For anything else, leave unset */
        case EVAUTOCLEAN:
           {int old_autoclean = autoclean;
            autoclean = atoi(value);
            if (autoclean >= 0 && autoclean < old_autoclean) dumpdir_tidy();
           }
            break;
        case EVREGLTEXT:    /* These two are... */
        case EVREGLNUM:     /* ...very similar */
            if (strlen(value) >= MAX_REGL_LEN) {
                mlforce("String too long - max %d", MAX_REGL_LEN - 1);
                status = FALSE;
                break;
            }
            strcpy((vnum == EVREGLTEXT)? regionlist_text: regionlist_number,
                  value);
            break;
        case EVAUTODOS:
            autodos = stol(value);
            break;
        }
        break;
    }
    return status;
}

/*
 * set a variable
 * The command front-end to svar
 * Also used by names.c
 *
 * int f;               default flag
 * int n;               numeric arg (can overide prompted value)
 */
int setvar(int f, int n) {
    int status;                     /* status return */
#if DEBUGM
    char *sp;                       /* temp string pointer */
    char *ep;                       /* ptr to end of outline */
#endif
    struct variable_description vd; /* variable num/type */
    char var[NVSIZE + 1];           /* name of variable to fetch */
    char value[NSTRING];            /* value to set variable to */

/* First get the variable to set.. */
    if (clexec == FALSE) {
        status = mlreply("Variable to set: ", &var[0], NVSIZE);
        if (status != TRUE) return status;
    }
    else {      /* macro line argument - grab token and skip it */
        execstr = token(execstr, var, NVSIZE + 1);
   }

/* Check the legality and find the var */
    findvar(var, &vd, NVSIZE + 1);

/* If its not legal....bitch */
    if (vd.v_type == -1) {
        mlwrite("%%No such variable as '%s'", var);
        return FALSE;
    }

/* Get the value for that variable */
    if (f == TRUE) strcpy(value, ue_itoa(n));
    else {
        status = mlreply("Value: ", &value[0], NSTRING);
        if (status != TRUE) return status;
    }

/* And set the appropriate value */
    status = svar(&vd, value);

#if DEBUGM
/* If $debug == TRUE, every assignment will echo a statment to
 * that effect here.
 */
    if (macbug) {
        strcpy(outline, "(((");

/* Assignment status */
        strcat(outline, ltos(status));
        strcat(outline, ":");

/* Variable name */
        strcat(outline, var);
        strcat(outline, ":");

/* And lastly the value we tried to assign */
        strcat(outline, value);
        strcat(outline, ")))");

/* Expand '%' to "%%" so mlwrite wont bitch */
        sp = outline;
        while (*sp) {
            if (*sp++ == '%') {
                ep = --sp;      /* start at that % */
                while (*ep++);  /* advance to the end */
                *(ep + 1) = 0;  /* null terminate the string one out */
                while (ep-- > sp) *(ep + 1) = *ep;  /* copy backwards */
                sp += 2;        /* and advance sp past the new % */
            }
        }

/* Write out the debug line */
        mlforce(outline);
        update(TRUE);

/* And get the keystroke to hold the output */
        if (get1key() == abortc) {
            mlforce(MLpre "Macro aborted" MLpost);
            status = FALSE;
        }
    }
#endif

/* And return it */
    return status;
}

/*
 * ue_itoa:
 *      integer to ascii string.......... This is too
 *      inconsistant to use the system's
 *
 * int i;               integer to translate to a string
 */
char *ue_itoa(int i) {
    int digit;      /* current digit being used */
    char *sp;       /* pointer into result */
    int sign;       /* sign of resulting number */
    static char result[INTWIDTH + 1];       /* resulting string */

/* Record the sign... */
    sign = 1;
    if (i < 0) {
        sign = -1;
        i = -i;
    }

/* And build the string (backwards!) */
    sp = result + INTWIDTH;
    *sp = 0;
    do {
        digit = i % 10;
        *(--sp) = '0' + digit;  /* And install the new digit */
        i = i / 10;
    } while (i);

/* And fix the sign */
    if (sign == -1) {
        *(--sp) = '-';          /* And install the minus sign */
    }

    return sp;
}

/*
 * find the type of a passed token
 *
 * char *token;         token to analyze
 */
int gettyp(char *token) {
    char c;         /* first char in token */

    c = *token;     /* grab the first char (all we check) */

    if (c == 0) return TKNUL;      /* no blanks!!! */

/* A numeric literal? *GGR* allow for -ve ones too */

    if (c >= '0' && c <= '9') return TKLIT;
    if (c == '-') {
        char c2 = token[1];
        if (c2 >= '0' && c2 <= '9') return TKLIT;
    }

    switch (c) {
    case '"':   return TKSTR;
    case '!':   return TKDIR;
    case '@':   return TKARG;
    case '#':   return TKBUF;
    case '$':   return TKENV;
    case '%':   return TKVAR;
    case '&':   return TKFUN;
    case '*':   return TKLBL;
    default:    return TKCMD;
    }
}

/*
 * find the value of a token
 *
 * char *token;         token to evaluate
 */
char *getval(char *token) {
    int status;                 /* error return */
    struct buffer *bp;          /* temp buffer pointer */
    int blen;                   /* length of buffer argument */
    int distmp;                 /* temporary discmd flag */
    static char buf[NSTRING];   /* string buffer for some returns */
    char tbuf[NSTRING];         /* string buffer for some workings */

    switch (gettyp(token)) {
    case TKNUL:
        return "";

    case TKARG:                 /* interactive argument */
/* GGR - There is the possibility of an illegal overlap of args here.
 *       So it must be done via a temporary buffer.
 *              strcpy(token, getval(&token[1]));
 */
        strcpy(tbuf, getval(&token[1]));
        strcpy(token, tbuf);
        distmp = discmd;    /* echo it always! */
        discmd = TRUE;
        status = getstring(token, buf, NSTRING, ctoec('\n'));
        discmd = distmp;
        if (status == ABORT) return errorm;
        return buf;

    case TKBUF:             /* buffer contents fetch */
/* Grab the right buffer
 * GGR - There is the possibility of an illegal overlap of args here.
 *       So it must be done via a temporary buffer.
 *              strcpy(token, getval(&token[1]));
 */
        strcpy(tbuf, getval(&token[1]));
        strcpy(token, tbuf);
        bp = bfind(token, FALSE, 0);
        if (bp == NULL) return errorm;

/* If the buffer is displayed, get the window vars instead of the buffer vars */
        if (bp->b_nwnd > 0) {
            curbp->b_dotp = curwp->w_dotp;
            curbp->b_doto = curwp->w_doto;
        }

/* Make sure we are not at the end */
        if (bp->b_linep == bp->b_dotp) return errorm;

/* Grab the line as an argument */
        blen = bp->b_dotp->l_used - bp->b_doto;
        if (blen >= NSTRING)        /* GGR >= to allow for NUL */
            blen = NSTRING - 1;
        memcpy(buf, bp->b_dotp->l_text + bp->b_doto, blen);
        buf[blen] = 0;

/* And step the buffer's line ptr ahead a line */
        bp->b_dotp = bp->b_dotp->l_fp;
        bp->b_doto = 0;

/* If displayed buffer, reset window ptr vars */
        if (bp->b_nwnd > 0) {
            curwp->w_dotp = curbp->b_dotp;
            curwp->w_doto = 0;
            curwp->w_flag |= WFMOVE;
        }

/* And return the spoils */
        return buf;

    case TKVAR:
        return gtusr(token + 1);
    case TKENV:
        return gtenv(token + 1);
    case TKFUN:
        return gtfun(token + 1);
    case TKDIR:
        return errorm;
    case TKLBL:
        return errorm;
    case TKLIT:
        return token;
    case TKSTR:
        return token + 1;
    case TKCMD:
        return token;
    }
    return errorm;
}
