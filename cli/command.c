/*
 * command.c - GEMDOS command interpreter
 *
 * Copyright (c) 2001, Lineo, Inc.
 * Copyright (c) 2001, Martin Doering
 *
 * Authors:
 *  JSL Jason S. Loveman
 *  LGT Lou T. Garavaglia
 *  SCC Steven C. Cavender
 *  MAD     Martin Doering
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */



extern long xoscall();
extern long xlongjmp();
extern long bios();
extern void in_term();
extern void rm_term();
extern long super();
extern long user();
extern long xsetjmp();
extern void devector();

#define NULLPTR (char *)0
#define FALSE 0
#define TRUE -1
#define MAXARGS 20

#define xrdchne() xoscall (0x08)
#define xecho(a) xoscall (0x02,a)
#define xread(a,b,c) xoscall(0x3f,a,b,c)
#define xwrite(a,b,c) xoscall(0x40,a,b,c)
#define xopen(a,b) xoscall(0x3d,a,b)
#define xclose(a) xoscall(0x3e,a)
#define xcreat(a,b) xoscall(0x3c,a,b)
#define xforce(a,b) xoscall(0x46,a,b)
#define xexec(a,b,c,d) xoscall(0x4b,a,b,c,d)
#define dup(a) xoscall(0x45,a)
#define xgetdrv() xoscall(0x19)
#define xsetdrv(a) xoscall(0x0e,a)
#define xsetdta(a) xoscall(0x1a,a)
#define xsfirst(a,b) xoscall(0x4e,a,b)
#define xsnext() xoscall(0x4f)
#define xgetdir(a,b) xoscall(0x47,a,b)
#define xmkdir(a) xoscall(0x39,a)
#define xrmdir(a) xoscall(0x3a,a)
#define xchdir(a) xoscall(0x3b,a)
#define xunlink(a) xoscall(0x41,a)
#define xrename(a,b,c) xoscall(0x56,a,b,c)
#define xgetfree(a,b) xoscall(0x36,a,b)
#define xterm(a) xoscall(0x4c,a)
#define xf_seek(a,b,c) xoscall(0x42,a,b,c)
#define xmalloc(a) xoscall(0x48,a);
#define xmfree(a) xoscall(0x49,a);
#define xattrib(a,b,c) xoscall(0x43,a,b,c)
#define getbpb(a) bios(7,a)
#define rwabs(a,b,c,d,e) bios(4,a,b,c,d,e)

#define BPB struct _bpb
BPB /* bios parameter block */
{
    int recsiz;
    int clsiz;
    int clsizb;
    int rdlen; /* root directory length in records */
    int fsiz; /* fat size in records */
    int fatrec; /* first fat record (of last fat) */
    int datrec; /* first data record */
    int numcl; /* number of data clusters available */
    int b_flags;
} ;

struct rdb		/*IO redirection info block	*/
{
    int nso;
    int nsi;
    int oldso;
    int oldsi;
};

struct rdb * rd_ptr;

char zero = { '0' } ;
char hexch[] =
{
    '0','1','2','3','4','5','6','7',
    '8','9','A','B','C','D','E','F'
} ;

int drv;
int exeflg;
int rtrnFrmBat;
int prgerr;
int cmderr;

long jb[3];
long compl_code;

#define BUFSIZ 10000
char buf[BUFSIZ];

char lin[130];
char srchb [44];
char prgTail[5] = {".PRG"};
char batTail[5] = {".BAT"};
char autoBat[13]= {"AUTOEXEC.BAT"};
char pthSymb[6] = {"PATH="};
char drvch;
char *basePage;
char *prntEnvPtr;

/* Declarations for Wild Card processing: */
char *WSrcReq;
int  WAttCode;
char wildExp[4] = {"*.*"};
char srcFlNm[67];       /*src file name*/
char dstFlNm[67];       /* destination file name */
char srcDir[67];        /*src dir path*/
char dstDir[67];        /*dst dir path */
char srcNmPat[13];      /*src file name specified in path */
char dstNmPat[13];      /*dst file name specified in path */
char path[67];	        /*lst of default path names */

/* Forward declarations */
void xCmdLn (char *parm[], int *pipeflg, long *nonStdIn, char *outsd_tl);



/*
 * chk_redirect - determines it input or output has been redirected,
 * if so restoring it to previous value.
 */

void chk_redirect (struct rdb * r)
{
    /* if a new standard in specified ...*/
    if (r->nsi == -1)
    {
	xclose (0);
	xforce(0,r->oldsi);
    }
    /* if a new standard out specified.*/
    if (r->nso == -1)
    {
	xclose (1);
	xforce(1,r->oldso);
    }
}



void errout ()
{
    chk_redirect (rd_ptr);
    xlongjmp (jb, -1);
}



int xncmps(int n, char *s, char *d)
{
    while (n--) if (*s++ != *d++) return(0);
    return(1);
}



void prthex(unsigned h)
{
    unsigned h2;
    if ((h2 = (h >> 4)))
        prthex(h2);
    else
        xwrite(1,1L,"0");
    xwrite(1,1L,&hexch[h & 0x0f]);
}



int strlen(char *s)
{
    int n;

    for (n=0; *s++; n++);
    return(n);
}



/*
 * pdl - print a decimal long value
 */

void pdl(long d)
{
    long d2;
    if ((d2 = d / 10))
        pdl(d2);
    xwrite(1, 1L, &hexch[d % 10]);
}



/*
 * prtdecl - print a decimal long value, if it exists
 */

void prtdecl(long d)
{
    if (d)
        pdl(d);
    else
	xwrite(1, 1L, "0");
}



void prtDclFmt (long d, int cnt, char *ch)
{
    int i;
    long k, j;

    k = (d ? d : 1);
    j = 1;
    for (i = 1; i < cnt; i++) j *= 10;
    while (k < j)
    {
	xwrite (1, 1L, ch);
	k *= 10;
    }
    prtdecl (d);
}



void ucase(char *s)
{
    for ( ; *s ; s++) if ((*s >= 'a') && (*s <= 'z')) *s &= ~0x20;
}



/*
 *  gtFlNm - get file name of the next file in the directory match a
 *     path\wildcat specification. The first invocation makes a call to
 *     xsfirst.  Each subsequent invocation uses xsnext().  To invoke
 *     the routine, the wildcarded path name is put into WSrcReq and the
 *     routine called.	For this and each subseqent call the descriptor block
 *     block for the found file (if one was found) is pointed to by WThisSrc.
 *
 *
 *  returns 0 if no match
 *  returns -1 if file found
 *
 */

int  gtFlNm ()
{
    /* First file request?	*/
    if (WSrcReq != NULLPTR)
    {
	if (xsfirst(WSrcReq, WAttCode)) return(FALSE);

	WSrcReq = NULLPTR;
    }

    /* Subsequent file request	*/
    else
    {
	if (xsnext()) return(FALSE);
    }
    return (TRUE);
}



/*
 * chkDir - Evaluates pathExp
 *
 * to determine if it specifies a directory, or a file name. For convenience
 * sake it stuffs the directory part in dirExp and the file name into filExp.
 * If a file name (ambiguous or not) was found, it is place in filExp.
 * If no file name was found, filExp points to "*.*".
 *
 * int chkDir (pathExp, dirExp, filExp)
 *
 * returns -3 if wild cards specified in path name
 *     -2 if pathExp does NOT specify a file, directory, or device
 *     -1 if pathExp evaluates to a subdirectory, or a volume label
 *	0 if pathExp evaluates to a normal, read-only, or system file
 *	1 if pathExp evaluates to a normal, read-only, or system file but
 *	  was specified with wild cards
 *
 * pathExp - ptr to path expression, w/wo wild cards to be evaluated
 * dirExp  - ptr to dir part of pathExp
 * filExp  - ptr to file.ext part of pathExp
 */

int chkDir (char *pathExp, char *dirExp, char *filExp)
{
    int dirLen;
    int flExsists;
    char c;

    int pathLen   = strlen (pathExp);
    int wildLen   = strlen (wildExp);
    int wildFnd   = FALSE;
    int i	  = 0;

    /*directory length = path length to start with.*/
    dirLen = pathLen;

    /* Loop thru path expresion from end looking for delimeters and the 1st char.*/
    do
    {
	if ((c = pathExp[dirLen]) == '*') wildFnd = TRUE;
    }
    while ((c != ':') && (c != '\\') && (dirLen--));
    dirLen++;

    /* IF nothing specified defalult to wild expresion.*/
    if ((pathLen == 0) ||
	(pathExp[pathLen - 1] == '\\') ||
	(pathExp[pathLen - 1] == ':'))
    {
	while ((pathExp[pathLen] = wildExp[i]))
	{
	    pathLen++;
	    i++;
	}
	wildFnd = TRUE;
    }

    /* return file not found if not found.*/
    flExsists = xsfirst (pathExp, WAttCode) ? -2 : 0;

    /* if wild cards were specified in file name and file exsists return 1 */
    flExsists = wildFnd ? (flExsists ? -2 : 1) : flExsists;

    /* If no wild cards/file name found check if directory. */
    if (!wildFnd)
    {
	/* if a file structure exsists...*/
	if (!flExsists)
	{
	    /* if it is a directory or a volume label...*/
	    if (srchb[21] & 0x18)
	    {
		/* Set up dirLen to encompas entire path specification.*/
		dirLen = pathLen;

		/*Tackon a path seperator.*/
		pathExp[dirLen++] = '\\';

		/* copy wild card expresion into spec'd path and file name.*/
		for (i = 0; i <= wildLen; i++)

		/* onto end of path expresion.*/
		pathExp[i + dirLen] = wildExp[i];

		flExsists = -1;
	    }
	}
    }
    /* copy path exp into directory expresion.*/
    for(pathLen = 0; pathLen < dirLen; pathLen++)
	if ((dirExp[pathLen] = pathExp[pathLen]) == '*')
	    return -3;

    /* chop off file nm from dirExp.*/
    dirExp [dirLen] = '\0';

    /* copy file name into return var.*/
    i = 0;
    while ((filExp[i++] = pathExp[pathLen++]))
        ;

    return (flExsists);
}


/*
 *  int chkDst ();
 *
 *  chkDst - Checks dst file name for validity. If there are any wild cards in
 *     the source file name the only valid dst names are "*", or "*.*".  Any
 *     thing else results in an error.
 *
 *  returns 0 if no error
 *	-1 if unable to make dst file name
 *
 */

int chkDst ()
{
    int i = 0;
    char c;

    /* check for proper use of wild chards.*/
    while ((c = srcNmPat[i++]))
    {
	/* Look for wild card chars.*/
	switch (c)
	{
	    case '*':
	    case '?':

		doDstChk:
		/* If dst file name longer than wild exp, must be error.*/
		if (strlen(dstNmPat) > strlen(wildExp)) return -1;

		/* Loop till end of dst fil nam to see if it matches wild exp.*/
		for (i = 0; (c = dstNmPat[i]); i++)
		    if (c != wildExp[i]) return -1;

		/* return ok.*/
		return (0);

		default : break;
	}
    }

    /* if any wild cards in dst, check for validity.*/
    for (i = 0; (c = dstNmPat[i]); i++)
	if ((c == '*') || (c == '?')) goto doDstChk;

    /* return ok.*/
    return 0;
}


/*
 *  int mkDst - Make dst file name.
 *
 *  returns 0 if dst other than src
 *  returns -1 if dst same as src
 *
 *  srcFlNm - ptr to string from search first on path name
 *  dstFlNm - ptr to string that will recieve destination file name
 *  srcDir  - ptr to src dir path
 *  dstDir  - ptr to dst dir path
 *  srcNmPat- ptr to string that contains the file name specified in path
 *  dstNmPat- ptr to string that contains the dst pattern
 *
 */

int mkDst ()
{
    int i, k, ndx;
    int srcEqDst;

    i = ndx = 0;

    /* determine If dst dir path = src dir path.*/
    while ((srcEqDst = (srcDir[i] == dstDir[i])) && srcDir[i]
	   && (dstFlNm [i] = dstDir[i])) i++;

    /* if they do...*/
    if (srcEqDst)
    {
	i = 0;

	/* if the dst is not a wild card (in which case auto match)...*/
	if (!(srcEqDst = (dstNmPat[0] == '*')))

	/* loop, chk each src=dst file.ext for match, setting srcEqDst. */
	while ((srcEqDst = (srchb[30 + i] == dstNmPat[i]))
	       && srchb[30 + i] && dstNmPat[i]) i++;
    }

    /* if the entire name matches create a dst file name with '.&&&' as ext*/
    if (srcEqDst)
    {
	i = 0;
        while ((dstFlNm[ndx] = dstDir[ndx]))
            ndx++;

	while ((dstFlNm[ndx + i] = srchb[30 + i])
	       && (dstFlNm[ndx + i] != '.')) i++;
	i++;

	for (k = 0; k <= 2; dstFlNm[i + (k++)] = '&');
    }

    /* else file names do not match.*/
    else
    {
	i = 0;
	ndx = 0;

	/* copy dst dir path into dst file name.*/
        while ((dstFlNm[ndx] = dstDir[ndx]))
            ndx++;

	/* if dst file pat is wild card, copy src file name into dst file name.*/
	if (dstNmPat[0] == '*')
	    while ((dstFlNm[ndx + i] = srchb[30 + i])) i++;

	else
    /* copy dst name pat directly into dst file name.*/
            while ((dstFlNm[ndx + i] = dstNmPat[i]))
                i++;
    }
    return (srcEqDst);
}



/*
 *  mkSrc - make source file name from directory path and file name.
 */

void mkSrc ()
{
    int i,j = 0;

    /* copy src directroy into src directory file name.*/
    for (i=0; (srcFlNm[i] = srcDir[i]); i++);

    /* copy source file name from search first/next into src file name.*/
    while ((srcFlNm[i + j] = srchb[30 + j])) j++;
}



/*
 *  wrt - write to standard output
 */

void wrt (char *msg)
{
    xwrite (1, (long)strlen(msg), msg);
}

void wrtln (char *msg)
{
    wrt ("\r\n");
    wrt (msg);
}

void wrtch (char ch)
{
    char str [2];

    str [0] = ch;
    str [1] = 0;
    wrt (str);
}



/*
 *  dspDir - display directory
 */

void dspDir (char *p, char *dir)
{
    int i, j;
    char ch, tmpbuf[100];

    if ((*p) && (p[1] == ':'))
    {
	ch = *p;
	i = p[0] - 'A';
	j = 2;
    }
    else
    {
	ch = (i = xgetdrv()) + 'A';
	j = 0;
    }
    xwrite (1, 1L, &ch);
    wrt (":");
    if (!dir[j])
    {
	xgetdir (&tmpbuf, i+1);
	if (tmpbuf[0] == 0)
	{
	    tmpbuf[0] = '\\';
	    tmpbuf[1] = 0;
	}
	wrt (tmpbuf);
    }
    else
	wrt (dir + j);
}



/*
 *  cr2cont - wait for cariage return before continuing.
 */

void cr2cont()
{
    wrt ("CR to continue...");
    lin[0] = 126;
    xoscall(10,&lin[0]);
}



/*
 *  dspMsg - dsplay message
 */

void dspMsg (int msg)
{
    switch (msg)
    {
    case 0: wrtln ("Wild cards not allowed in path name."); break;
    case 1: wrtln ("File Not Found."); break;
    case 2: wrtln ("Destination is not a valid wild card expresion."); break;
    case 3: wrtln ("******* TEST  CLI *******"); break;
    case 4: wrtln ("Command - Version 19. Oct. 2001");break;
    case 5: wrt ("Done."); break;
    case 6: wrtln ("Command is incompletely specified.");break;
    case 7: wrt (srcFlNm); break;
    case 8: wrt (dstFlNm); break;
    case 9: wrtln ("."); break;
    case 10:wrt (" to "); break;
    case 11:
        break;
    case 12:wrtln (""); break;
    case 13:wrtln ("");
    case 14:
        drvch = (drv = xgetdrv()) + 'a';
        xwrite (1,1L,&drvch);
        wrt (":");
        wrt (" ");
        break;
    case 15:wrtln ("Wild cards not allowed in destination."); break;
    case 16:drvch = (drv = xgetdrv()) + 'a';
    xwrite (1,1L,&drvch);
    wrt (":");
    break;
    case 17:
        wrtln ("# in the first non blank column is a comment.");
        wrtln ("CAT or TYPE filenm.ext");
        wrtln ("	Writes filenm.ext to standard output.");
        wrtln ("CD [pathnm]");
        wrtln ("	With pathnm it sets default for working directory.");
        wrtln ("	Without pathnm displays current working directory.");
        wrtln ("CHMOD [pathnm/]filenm mode");
        wrtln ("	Changes the mode of the file specified in filenm to the");
        wrtln ("	value of mode.	Acceptable values are < 7:");
        wrtln ("   0 - Normal File Entry");
        wrtln ("   1 - File is Read Only");
        wrtln ("   2 - File is Hidden from directory Search");
        wrtln ("   4 - File is System File");
        wrtln ("CLS");
        wrtln ("	Clears the screen.");
        wrtln ("COPY source_file [destination_file]");
        wrtln ("	Copies source to destination.");
        wrtln ("DIR or LS [filenm.ext] [-f] [-d] [-t] [-w]");
        wrtln ("	-f - anything but directoryies.");
        wrtln ("	-d - directories only.");
        wrtln ("	-t - terse: names only.");
        wrtln ("	-w - wide: names only displayed horizontally.");
        wrtln (""); cr2cont();
        wrtln ("ERR ");
        wrtln ("	Displays the value of the Completion Code for the last command.");
        wrtln ("EXIT");
        wrtln ("	Exits CLI to invoking program.");
        wrtln ("INIT [drive_spec:]");
        wrtln ("	Reinitializes FAT entries this wiping disk.");
        wrtln ("MD [subdirectory name]");
        wrtln ("	Creates a new subdirectory in current directory.");
        wrtln ("MOVE source_file [destination_file]");
        wrtln ("	Copies source to destination and deletes source.");
        wrtln ("PAUSE");
        wrtln ("	Writes 'CR to continue...' to standard output");
        wrtln ("	and waits for a carriage return from standard input.");
        wrtln ("PRGERR [ON | OFF]");
        wrtln ("	Turns command processing abort feature ON/OFF.");
        wrtln ("	If PRGERR is ON and a .PRG file returns a non zero completion");
        wrtln ("	code, all further processing will stop.  Usefull in .BAT files.");
        wrtln ("	Default is ON.");
        wrtln ("NOWRAP");
        wrtln ("	Disables line wrap.");
        wrtln ("PATH [;[pathnm]...]");
        wrtln ("	With path name sets default path for batch and commands.");
        wrtln ("	Without path name displays current path");
        wrtln (""); cr2cont();
        wrtln ("REM or ECHO [\"string\"]");
        wrtln ("	Strips quotes and writes string to standard output.");
        wrtln ("	/r is replaced by 0x13, /n by 0x10, /0 by 0x0.");
        wrtln ("	/c by 0x13 0x10, /anything is replaced by anything.");
        wrtln ("REN source_file_nm [destination_file_nm]");
        wrtln ("	Renames source to destination.");
        wrtln ("RD [pathnm]");
        wrtln ("	Removes named directory.");
        wrtln ("RM or DEL or ERA filenm [[filemn]...] [-q]");
        wrtln ("	Removes named file from directory.");
        wrtln ("	IF the -q option is used, the CLI will display the question");
        wrtln ("	Y/CR... and wait for a responce.");
        wrtln ("SHOW [drive_spec:]");
        wrtln ("	Displays disk status for default drive or drive specified.");
        wrtln ("VERSION");
        wrtln ("	Displays current version of OS.");
        wrtln ("WRAP");
        wrtln ("	Enbles line wrap.");
        wrtln (""); cr2cont();
        break;
    }
}



/*
 * getYes
 */

int getYes ()
{
    char inpStr [30];

    inpStr[0] = xrdchne();
    inpStr[1] = 0;
    ucase (&inpStr[0]);
    if (inpStr[0] == 'Y')
	return -1;
    return 0;
}



/*
 *  copyCmd - copy file.
 *
 *  returns 0 if copyied ok
 *     -1 if copy failed
 */
int copyCmd(char *src, char *dst, int move)
{
    int i, srcEqDst, fds, fdd;
    long    nr, nw;
    char    srcSpc[67];
    char    dstSpc[67];

    for (i=0; (srcSpc[i] = src[i]); i++);
    for (i=0; (dstSpc[i] = dst[i]); i++);

    WSrcReq = (char *)&srcSpc;
    WAttCode = -1;

    compl_code = 0xFFFFFFFF;

    /*If not a valid file name...*/
    switch (chkDir ((char *)&srcSpc, srcDir, srcNmPat))
    {
	case -3: goto error5;
	case -2: goto error6;
	    default:
	    {
		/* Check destination directory.*/
		if (chkDir ((char *)&dstSpc, dstDir, dstNmPat) == -3) goto error5;

		else
		{
		    if (chkDst()) dspMsg(2);

		    else
		    {
			while (gtFlNm())
			{
			    if (!(srchb[21] & 0x18))
			    {
				mkSrc();
				if (!(srcEqDst = mkDst()))
				{
				    xunlink (dstFlNm);
				    dspMsg (12); dspMsg(7); dspMsg(10); dspMsg(8);
				    if ((fds = xopen (srcFlNm, 0)) <= 0) goto error0;
				    if ((fdd = xcreat (dstFlNm, 0x20)) <= 0) goto error1;
				    compl_code = 0;
				    nr = nw = -1;
				    while ((nr) && (nw))
				    {
					if ((nr = xread (fds, (long)BUFSIZ, buf)) > 0)
					{
					    if ((nw = xwrite (fdd, nr, buf)) < nr)
						goto error4;
					}
					else if (nr < 0)
					{
					    goto error3;
					}
				    }
				    xclose (fds);
				    if (move)
				    {
					xunlink (srcFlNm);
					wrt (" DELETING "); dspMsg(7);
				    }
				    xclose (fdd);
				}
				else
				{
				    goto error2;
				}
			    }
			}
			dspMsg (12); dspMsg(5);
		    }
		}
	    }
	}
    return (0);

    error0: dspMsg (1); return (-1);
    error1: wrtln ("Error creating file."); return (-1);
    error2: wrtln ("Cannot copy "); dspMsg(7); wrt(" to itself."); return (-1);
    error3: wrtln ("Error reading source file."); goto eout;
    error4: wrtln ("Disk full -- copy failed."); goto eout;
    error5: dspMsg (0); return (-1);
    error6: dspMsg (1); return (-1);

    eout:
    xunlink (dstFlNm);
    wrt (" DELETING "); wrt (dstFlNm);
    return -1;
}



/*
 *  renmCmd - rename command
 */

long renmCmd(char *src, char *dst)
{
    int i;
    char    srcSpc[67];
    char    dstSpc[67];

    for (i=0; (srcSpc[i] = src[i]); i++);
    for (i=0; (dstSpc[i] = dst[i]); i++);

    WSrcReq = (char *)&srcSpc;
    WAttCode = -1;

    /* Set up completion code to show failure */
    compl_code = 0xFFFFFFFF;

    /* IF src not specified err out. */
    if (!(*src))
        dspMsg (6);

    /*If not a valid file name...*/
    else
        switch (chkDir ((char *)&srcSpc, srcDir, srcNmPat))
    {
	case -3: dspMsg (0); break;
	case -2: dspMsg (1); break;
	default:
	{
	    /* Check destination directory.*/
	    if (chkDir ((char *)&dstSpc, dstDir, dstNmPat) == -3)
	    {
		if (((i = chkDir ((char *)&srcSpc, srcDir, srcNmPat)) == -3) || (i == 1))
		{
		    wrt ("Rename ALL files matching ");
		    wrt (srcDir);
		    wrt (srcNmPat);
		    wrt (" (Y/CR)? ");
		    if (!getYes())
			goto skprnm;
		    wrtln ("");
		}
	    }
	    else
	    {
		if (chkDst())
		{
		    dspMsg(2);
		}
		else
		{
		    while (gtFlNm())
		    {
			if (!(srchb[21] & 0x18))
			{
			    if (!mkDst());
			    {
				mkSrc();

				dspMsg (12); dspMsg(7); dspMsg(10); dspMsg(8);
				compl_code = xrename (0, srcFlNm, dstFlNm);
				if (compl_code < 0)
				{
				    wrt ("  Rename Unsucessfull!");
				}
			    }
			}
		    }
		    dspMsg(12); dspMsg(5);
		}
	    }
	}
    }

    skprnm:
    return (compl_code);
}



long dirCmd (char * argv[])
{
    char    srcSpc[67];
    int i, j, k, n, att, *dt, filOnly, dirOnly, terse, wide;
    long    compl_code, *pl;

    wide = filOnly = dirOnly = terse = 0;
    i = 1;
    while (*argv[i])
    {
	ucase (argv[i]);
	if (*argv[i] == '-')
	{
	    switch (*(argv[i] + 1))
	    {
		case 'F' : filOnly = -1;
		    dirOnly = 0;
		    break;
		case 'D' : dirOnly = -1;
		    filOnly = 0;
		    break;
		case 'W' : wide    = -1;
		case 'T' : terse   = -1;
		    break;
		default  : break;
	    }
	    j = i;
	    while (*(argv[j] = argv[j+1])) j++;
	}
	else i++;
    }

    for (i=0; (srcSpc[i] = argv[1][i]); i++);
    chkDir ((char *)&srcSpc, srcDir, srcNmPat);

    if (!terse)
    {
	wrt("Directory of ");
	dspDir ((char *)&srcSpc, srcDir);
	dspMsg (12);
    }

    WSrcReq = (char *)&srcSpc;
    WAttCode = -1;
    if (! gtFlNm())
    {
	compl_code = 0xFFFFFFFF;
	if (!terse) dspMsg (1);
    }
    else
    {
	compl_code = 0;
	k = 0;
	do
	{
	    n = strlen(&srchb[30]);
	    if ((dirOnly) && (srchb[21] != 0x10)) goto skip;
	    if ((filOnly) && (srchb[21] == 0x10)) goto skip;
	    if ((terse) &&
		((xncmps (2, &srchb[30], ".")) || (xncmps (3, &srchb[30], ".."))))
	    goto skip;

	    if (wide)
	    {
		wrt (&srchb[30]);
		if (k == 5)
		{
		    wrtln ("");
		    k = 0;
		}
		else
		{
		    for (i=n; i<13; i++)
			wrt (" ");
		    k++;
		}
	    }
	    else wrtln (&srchb[30]);

	    if (!terse)
	    {
		for (i=n; i<15; i++)
		    xwrite(1,1L," ");

		dt = (int *)&srchb[24];
		j = *dt;
		prtDclFmt ((long)((j>>5) & 0xF ), 2, "0");
		wrt("/");
		prtDclFmt ((long)(j & 0x1F), 2, "0");
		wrt("/");
		prtDclFmt ((long)(((j>>9) & 0x7F) + 80), 2, "0");
		wrt ("	");

		dt = (int *)&srchb[22];
		j = *dt;
		prtDclFmt ((long) ((j>>11) & 0x1F), 2, "0");
		wrt (":");
		prtDclFmt ((long) ((j>>5) & 0x3F), 2, "0");
		wrt (":");
		prtDclFmt ((long) ((j & 0x1F) << 1), 2, "0");
		wrt ("	");

		att = srchb[21];
		if (att < 0x10) wrt ("0");
		prthex(att);
		xwrite(1,2L,"  ");

		pl = (long *) &srchb[26];
		prtDclFmt((long)*pl, 6, " ");
	    }
	    skip:
	}
	while (gtFlNm());
	wrtln ("");
	if (!terse) dspMsg(5);
    }
    return (compl_code);
}



int mknum (char *str)
{
    int num, hex = 0;
    char ch;

    ucase (str);
    if (*str == 'X')
    {
	hex = 1;
	str++;
    }

    num = 0;
    while ((ch = *str++))
    {
	if (hex)
	{
	    num *= 16;
	    if (ch > 9) num += (ch - 'A' + 10);
	    else num += ch - '0';
	}
	else
	{
	    num *= 10;
	    num += ch - '0';
	}
    }
    return num;
}



long chmodCmd (char * argv[])
{
    char    srcSpc[67];
    int i, att;
    long    compl_code;

    for (i=0; (srcSpc[i] = argv[1][i]); i++);

    chkDir ((char *)&srcSpc, srcDir, srcNmPat);
    WSrcReq = (char *)&srcSpc;
    WAttCode = -1;
    if (! gtFlNm())
    {
	compl_code = 0xFFFFFFFF;
	dspMsg (1);
    }
    else
    {
	mkSrc();
	att = srchb[21];
	if (att & 0x18)
	{
	    wrt ("Unable to change mode on subdirectorys or volumes.");
	    compl_code = 0xFFFFFFFF;
	}
	else
	{
	    compl_code = 0;
	    do
	    {
		if (!*argv[2])
		{
		    wrt ("Invalid mode specification.");
		    compl_code = 0xFFFFFFFF;
		}
		else
		{
		    i = mknum (argv[2]);
		    if (i & ~0x7)
		    {
			wrt ("Invalid mode specification.");
			compl_code = 0xFFFFFFFF;
		    }
		    else
			compl_code = xattrib (srcFlNm, 1, i);
		}
	    }
	    while (gtFlNm());
	}
	if (!compl_code)
	{
	    dspMsg(5);
	}
    }
    return (compl_code);
}



long typeCmd (char * argv[])
{
    char    srcSpc[67];
    int i, n, fd;
    long    compl_code;

    compl_code = 0;
    if (!(*argv[1]))
        dspMsg (6);
    else
    {
	for (i=0; (srcSpc[i] = argv[1][i]); i++);
	chkDir ((char *)&srcSpc, srcDir, srcNmPat);
	WSrcReq = (char *)&srcSpc;
	WAttCode = -1;
	if (!gtFlNm())
	{
	    dspMsg (1);
	    compl_code = 0xFFFFFFFF;
	}
	else
	{
	    compl_code = 0;
	    do
	    {
		mkSrc();
		fd = xopen(srcFlNm, 0);
		do
		{
		    n = xread(fd,1000L,buf);
                    if (n > 0)
                        xwrite(1,(long) n,buf);
		}
		while (n > 0);
	    }
	    while (gtFlNm());
	    xclose(fd);
	}
    }
    return (compl_code);
}



long delCmd (char * argv[])
{
    char    srcSpc[67];
    int i, j, k, query;
    long    compl_code;

    compl_code = 0;
    query = 0;
    i = 1;
    while (*argv[i])
    {
	ucase (argv[i]);
	if (*argv[i] == '-')
	{
	    switch (*(argv[i] + 1))
	    {
		case 'Q' : query = -1;
		    break;
		default  : break;
	    }
	    j = i;
	    while (*(argv[j] = argv[j+1])) j++;
	}
	else i++;
    }

    if (*argv[1])
    {
	k = 1;
	while (*argv[k])
	{
	    for (i=0; (srcSpc[i] = argv[k][i]); i++);
	    k++;
	    WSrcReq = (char *)&srcSpc;
	    WAttCode = -1;
	    if (((i = chkDir ((char *)&srcSpc, srcDir, srcNmPat)) == -3) || (i == 1))
	    {
		wrt ("Delete ALL files matching ");
		wrt (srcDir);
		wrt (srcNmPat);
		wrt (" (Y/CR)? ");
		if (!getYes())
		    goto noera;
		wrtln ("");
	    }
	    if (!gtFlNm())
	    {
		compl_code = 0xFFFFFFFF;
		dspMsg (1);
		wrtln("");
	    }
	    else
	    {
		do
		{
		    if (!(srchb[21] & 0x18))
		    {
			mkSrc();
			dspMsg(7);
			if (query)
			{
			    wrt ("? ");
			    i = getYes();
			    wrt ("\b\b ");
			    if (i)
				wrt (" << DELETED");
			    else
				goto skipdel;
			}
			compl_code = xunlink (srcFlNm);
			skipdel:
			dspMsg(12);
		    }
		}
		while (gtFlNm());
	    }
	}
	dspMsg(5);
    }
    else dspMsg(6);

    noera:
    return (compl_code);
}



/*
 *  dspCL - display command line
 */
void dspCL (char * argv[])
{
    int i;
    dspMsg(14);
    i = 0;
    while (*argv[i])
    {
	wrt (argv[i++]);
	wrt (" ");
    }
    dspMsg(12);
}



/*
 *  setPath - set execution path
 */
void setPath (char *p)
{
    int i = 0;

    if (!*p)
        wrt ((char *)&path);
    else
        if (xncmps (2, p, ";"))
            path[i] = 0;
        else
            while ((path[i] = p[i++]));
}


/*
 * execPrgm - execute program;
 */
long execPrgm (char *s, char *cmdtl)
{
    char cmd[100], ch, * cmdptr;
    int k, i, j, gtpath, envLen;
    int tlNtFnd = -1;
    long err;
    char * envPtr;

    /* Add len of path definition + 2 for 00 terminator */
    envLen = ((i = strlen (path)) + (i ? 5 : 0) + 2);

    /*Loop thru enviorment strings looking for '00'*/
    i = 0;
    while ((prntEnvPtr[i] + prntEnvPtr[i + 1]) != 0)
    {
	/* if a path has been defined, don't count it.*/
	if (xncmps (5, &prntEnvPtr[i], "PATH="))
	{
	    envLen--;
	    i += strlen (&prntEnvPtr[i]);
	}
	else
	{
	    envLen++;
	    i++;
	}
    }

    /* Allocate envLen number of bytes for environment strings.*/
    envPtr = (char *)xmalloc((long)envLen);

    /* copy path string into env.*/
    i = 0;
    if (path[0])
    {
        for (i = 0; pthSymb[i]; i++) envPtr[i] = pthSymb[i];
        j = 0;
        while ( (envPtr [i] = path[j++]) ) i++;
        envPtr [i++] = 0;
    }

    /* Copy parents environment string into childs.*/
    envLen = 0;
#if 0
    while ((envPtr [i] = prntEnvPtr [envLen]) | prntEnvPtr [envLen + 1])
    {
        /* if a path has been defined, don't copy it.*/
	if (xncmps (5, &prntEnvPtr [envLen], "PATH="))
	    envLen += (1 + strlen (&prntEnvPtr [envLen]));
	else
	{
	    i++;
	    envLen++;
	}
    }
#endif
    /* inc index past 0.*/
    i++;

    /* Null termintate.*/
    envPtr [i] = 0;

    for (i = 0; (cmd[i] = *s); s++, i++)
	if (*s == '.') tlNtFnd = 0;

    if (tlNtFnd)
	for (j = 0; (cmd[i] = prgTail[j]); i++, j++);

    i = 0;
    gtpath = -1;
    while ((ch = cmd[i++])) if ((ch == ':') || (ch == '\\')) gtpath = 0;

    exeflg = 1;
#if 0
    super();
    rm_term();
    user();
#endif
    cmdptr = (char *)&cmd;
    j = 0;
    while ((((err = xexec(0, cmdptr, cmdtl, envPtr)) & 0xFFFFFFFF) == -33) && (gtpath))
    {
	k = j;
	if (path [j])
	{
	    while ((path[j]) && (path[j] != ';')) j++;
	    for (i = 0; k < j; k++, i++) buf[i] = path[k];
	    if (buf[i - 1] != '\\') buf[i++] = '\\';
	    k = 0;
	    while (cmd[k]) buf[i++] = cmd[k++];
	    buf[i] = 0;
	    cmdptr = &buf[0];
	    if (!(path[j]))
		gtpath = 0;
	    else
		j++;
	}
	else gtpath = 0;
    }
#if 0
    super();
    in_term();
    user();
#endif
    exeflg = 0;
    xmfree(envPtr);

    return (err);
}



/*
 *  execBat - execute batch file
 */
int execBat (char *s, char *parms[])
{
    long flHnd;
    int i, j, k, gtpath;
    int tlNtFnd = -1;
    char ch, cmd[100], * cmdptr;

    for (i = 0; (cmd[i] = *s); s++, i++)
	if (*s == '.') tlNtFnd = 0;

    if (tlNtFnd)
	for (j = 0; (cmd[i] = batTail[j]); i++, j++);

    if (xncmps (3, &cmd[ i - 3], "BAT"))
    {
	i = 0;
	gtpath = -1;
	while ((ch = cmd[i++])) if ((ch == ':') || (ch == '\\')) gtpath = 0;

	cmdptr = (char *)&cmd;
	j = 0;
	while (((flHnd = xopen(cmdptr, 0)) <= 0) && (gtpath))
	{
	    k = j;
	    if (path [j])
	    {
		while ((path[j]) && (path[j] != ';')) j++;
		for (i = 0; k < j; k++, i++) buf[i] = path[k];
		if (buf[i - 1] != '\\') buf[i++] = '\\';
		k = 0;
		while (cmd[k]) buf[i++] = cmd[k++];
		buf[i] = 0;
		cmdptr = &buf[0];
		if (!(path[j]))
		    gtpath = 0;
		else
		    j++;
	    }
	    else gtpath = 0;
	}

	if (flHnd >= 0)
	{
	    i = 0;
	    xCmdLn (parms, &i, &flHnd, (char *)0L);
	    xclose ((int)flHnd);
	    compl_code = 0;
	    return -1;
	}
    }

    compl_code = 0XFFFFFFFF;
    return 0;
}



void chk_sub (char *tl, char **parm)
{
    char ch, tmptl [167], * tmptl_ptr, * tmp_front, * tl_front, * parm_ptr;

    tmptl_ptr = (char *)&tmptl;
    tmp_front = tmptl_ptr;
    tl_front = tl;

    while ((ch = *tl++))
    {
	switch (ch)
	{

	    case '/':
		if (*tl == '%')
		{
		    *tmptl_ptr++ = ch;
		    *tmptl_ptr++ = *tl++;
		}
		else
		{
		    *tmptl_ptr = ch;
		    tmptl_ptr++;
		}
		break;

	    case '%':
		if (*(parm_ptr = parm [*tl++ - '0']))
		{
                    while ( (*tmptl_ptr = *parm_ptr++) )
                        tmptl_ptr++;
		}
		break;

	    default:
		*tmptl_ptr++ = ch;
		break;
	}
    }
    *tmptl_ptr = 0;

    while ( (*tl_front = *tmp_front++) ) tl_front++;
}



void chk_str (char * parm[])
{
    int i = 0;
    char * parm_ptr, * tmp_ptr, ch;

    while (*parm[i])
    {
	if (*parm[i] == '"')
	{
	    parm[i]++;
	    if (*(parm[i] + strlen (parm[i]) - 1) == '"')
		*(parm[i] + strlen (parm[i]) - 1) = 0;

	    parm_ptr = parm[i];
	    while ( (ch = *parm_ptr) )
	    {
		if (ch == '/')
		{
		    switch (ch = *(parm_ptr + 1))
		    {
			case 'c' : *parm_ptr++ = 13;
			    *parm_ptr = 10;
			    goto skip;
			    case 'r' : *parm_ptr = 13;
			    break;
			case 'n' : *parm_ptr = 10;
			    break;
			case '0' : *parm_ptr = 0;
			    break;
			default  : *parm_ptr = ch;
			    break;
		    }
		    parm_ptr++;
		    tmp_ptr = parm_ptr;
                    while ( (*tmp_ptr = *(tmp_ptr + 1)) )
                        tmp_ptr++;
		    skip:
		}

		else
		{
		    parm_ptr++;
		}
	    }
	}
	i++;
    }
}



/*
 *  readSi - read standard input
 */

int readSi (char *lin)
{
    int i, j;

    dspMsg(13);
    for (i = 1; i <= 125; lin[i++] = 0);
    i = j = 0;

    lin[0] = 126;
    xoscall(10, &lin[0]);

    lin[lin[1] + 2] = 0;

    i = 2;
    while (lin[i])
    {
	if ((lin[i] >= ' ') && (lin[i] <= '~'))
	    lin[j++] = lin[i];
	i++;
    }

    lin[j] = 0;
    lin[j+1] = 0;
    return (j);
}



/*
 *  readDsk - read from disk file
 */

int readDsk (char *lin, long * flHnd)
{
    int i, j;
    int chrFnd;
    char ch;

    for (i = 1; i <= 125; lin[i++] = 0);
    i = j = 0;

    while ((chrFnd = xread ((int)*flHnd, 1L, &ch) > 0) && (ch != '\r'))
	lin[j++] = ch;

    j = 0;
    i = 0;

    while (lin[i])
    {
	if ((lin[i] >= ' ') && (lin[i] <= '~'))
	    lin[j++] = lin[i];
	i++;
    }

    lin[j] = 0;

    return (j ? j : (chrFnd ? -1 : 0));
}



/*
 *  xCmdLn - execute command line.
 */

void xCmdLn (char *parm[], int *pipeflg, long *nonStdIn, char *outsd_tl)
{
    int pipe, bdChrs;
    int fs,fd;
    int i,j,k,argc,f1,f2,nd,rec;
    int concat;
    long newso, newsi;
    long sbuf[4];
    long dskFlHnd;
    char ch, *cmdtl, *tl0, *tl1, *tl, *d, *s, *argv[MAXARGS];
    char *p, ltail[130];

    struct rdb rd;

    BPB *b;

    rd.nso = 0;
    rd.nsi = 0;
    rd_ptr = &rd;

    bdChrs = 0;

    /* while there is input from the disk or standard input */
    while ((long)outsd_tl ?1 : (*nonStdIn ? (bdChrs = readDsk ((char*)&lin, nonStdIn)) : readSi((char*)&lin)))
    {
	/*Garbage chars in disk file.*/
	if ((bdChrs == -1) && *nonStdIn) goto again;

	exeflg = 0; /* not in an exec */
	wrtln ("");
	d = &ltail[0];
	argv[0] = d;
	argc = 0;
	concat = 0;
	pipe = 0;
	dskFlHnd = 0;

	/*Set up for input redirection.*/
	if (*pipeflg)
	{
	    argv[0] = parm[0];
	    argv[1] = d;
	    argc = 1;
	}

	/* find command tail */
	if ((long)outsd_tl)
	{
	    tl = outsd_tl + 1;
	    tl[outsd_tl[0]] = 0;
	}
	else tl = &lin[0];

	while (*tl == 0x20) tl++;
	chk_sub (tl, &parm[0]);

	/* allow remarks in batch files. */
        if (*tl == '#')
            goto again;
	while ( (ch = (*tl++)) )
            switch(ch)
            {
            case ' ':
                *d++ = 0;
                while (*tl == 0x20) tl++;
                argv[++argc] = d;
                break;

            case '"':
                *d++ = ch;
                while ((*tl) && ((*d++ = *tl++) != '"'));
                break;

            case '@':
                for (tl0 = tl; (ch = *tl); tl++)
                {
                    if (ch == 0x20) break;
                }
                *tl++ = 0;

                if ((dskFlHnd = xopen(tl0, 0)) >= 0)
                {
                    pipe = -1;
                }
                else
                {
                    wrtln (tl0); wrt (" not found.");
                }
                break;

            case '<':
                for (tl0 = tl; (ch = *tl); tl++)
                {
                    if (ch == 0x20) break;
                }
                *tl++ = 0;

                if ((newsi = xopen(tl0, 0)) >= 0)
                {
                    rd.oldsi = dup(0);
                    xforce(0,(int)newsi);
                    xclose((int)newsi);
                    rd.nsi = -1;
                }
                else
                {
                    wrtln (tl0); wrt (" not found.");
                }
                break;

            case '>':
                for (tl1 = tl; (ch = *tl); tl++)
                {
                    if (ch == '>')
                    {
                        concat = -1;
                        tl1++;
                    }
                    if (ch == 0x20) break;
                }
                *tl++ = 0;
                if (concat)
                {
                    if ((newso = xopen(tl1, 1)) < 0)
                    {
                        newso = xcreat(tl1, 0);
                    }
                }
                else
                {
                    xunlink (tl1);
                    newso = xcreat(tl1,0);
                    rd.nso = -1;
                }
                rd.oldso = dup(1);
                xforce(1, (int)newso);
                xclose((int)newso);
                if (concat)
                    xf_seek (0L, 1, 2);
                rd.nso = -1;
                break;
            default: *d++ = ch;
            }

        /* If pipe tack on remaining parms if any.*/
        if (*pipeflg)
        {
            i = 1;
            while (*parm[i])
            {
                argv[++argc] = parm [i++];
            }
        }

        *d++ = 0;
        *d = 0;
        i = argc;
        argv[++i] = d;

        s = argv[0];
        p = argv[1];
        ucase(s);

        /*Build command tail.*/
        cmdtl = lin;
        j = 1;
        i = 1;
        while (*argv[i])
        {
            k = 0;
            while ( (cmdtl[++j] = *(argv[i] + k++)) );
            cmdtl[j] = ' ';
            i++;
        }
        cmdtl[j] = 0xd;
        cmdtl[j + 1] = 0;
        cmdtl[0] = --j;
        cmdtl[1] = ' ';

        if (pipe)
        {
            xCmdLn ((char**)&argv, &pipe, &dskFlHnd, (char *)0L);
            xclose ((int)dskFlHnd);
        }
        else
        {
            if ((strlen(s) == 2) && (s[1] == ':'))
            {
                xsetdrv(drv = (*s -'A'));
            }
            else if (xncmps(3,s,"LS") || xncmps(4,s,"DIR"))
            {
                if (*nonStdIn)
                    dspCL (&argv[0]);
                compl_code = dirCmd (&argv[0]);
            }
            else if (xncmps (6,s,"CHMOD"))
            {
                if (*nonStdIn)
                    dspCL (&argv[0]);
                compl_code = chmodCmd (&argv[0]);
            }
            else if (xncmps (4,s,"ERR"))
            {
                wrt ("Completion code for previous command = ");
                prthex ((int)compl_code);
            }
            else if (xncmps (5,s,"PATH"))
            {
                if (*nonStdIn)
                    dspCL (&argv[0]);
                ucase (p);
                setPath (p);
            }
            else if (xncmps (4,s,"ENV"))
            {
                i = 0;
                while ((prntEnvPtr[i] + prntEnvPtr[i + 1]) != 0)
		{
		    /* if a path has been defined, don't count it.*/
		    if (!(xncmps (5, &prntEnvPtr[i], "PATH=")))
		    {
			wrtln (&prntEnvPtr[i]);
		    }
		    i += strlen (&prntEnvPtr[i]);
		    if (prntEnvPtr[i] + prntEnvPtr[i + 1] == 0) break;
		    i += 1;
		}
		if (path[0])
		{
		    wrtln ("PATH="); wrt (path);
		}
	    }

	    else if (xncmps(4,s,"CAT") || xncmps(4,s,"TYPE"))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		compl_code = typeCmd (&argv[0]);
	    }
	    else if ((xncmps(4,s,"REM")) || (xncmps(4, s, "ECHO")))
	    {
		chk_str (&argv[1]);
		i = 1;
		while (*argv[i])
		{
		    wrt (argv[i++]);
		    wrt (" ");
		}
		dspMsg(12);
	    }

	    else if (xncmps(3,s,"CD"))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		if (argc == 0)
		{
		    xgetdir(buf,drv+1);
		    if (!buf[0])
		    {
			buf[0] = '\\';
			buf[1] = 0;
		    }
		    xwrite(1,(long) strlen(buf),buf);
		}
		else
		{
		    if ((compl_code = xchdir(p)) != 0)
			wrt ("Directory not found.");
		}
	    }

	    else if (xncmps(7,s,"CMDERR"))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		if (argc == 0)
		{
		    if (cmderr) wrt ("ON");
		    else wrt ("OFF");
		}
		else
		{
		    ucase (p);
		    if (xncmps(3,p,"ON")) cmderr = -1;
		    else if (xncmps(4,p,"OFF")) cmderr = 0;
		    else wrt ("Arg must be ON or OFF.");
		}
	    }

	    else if (xncmps(7,s,"PRGERR"))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		if (argc == 0)
		{
		    if (prgerr) wrt ("ON");
		    else wrt ("OFF");
		}
		else
		{
		    ucase (p);
		    if (xncmps(3,p,"ON")) prgerr = -1;
		    else if (xncmps(4,p,"OFF")) prgerr = 0;
		    else wrt ("Arg must be ON or OFF.");
		}
	    }

	    else if (xncmps(3,s,"MD"))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		if ((compl_code = xmkdir(p)) != 0)
		    wrt ("Unable to make directory");
	    }
	    else if (xncmps(3,s,"RD"))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		if ((compl_code = xrmdir(p)) != 0)
		    wrt ("Unable to remove directory");
	    }
	    else if (xncmps(3,s,"RM") || xncmps(4,s,"DEL") || xncmps(4,s,"ERA"))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		compl_code = delCmd (&argv[0]);
	    }

	    else if (xncmps(4,s,"REN"))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		compl_code = renmCmd(argv[1], argv[2]);
	    }
	    else if (xncmps(5,s,"SHOW"))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		ucase (p);
		xgetfree(sbuf, (*p ? *p-64 : 0));
		wrt ("Allocation Information: Drive ");
		if (!*p) dspMsg (16);
		else wrt (p);
		dspMsg(12);
		wrtln ("Drive size in BYTES    ");
		prtDclFmt ((long)(sbuf[1] * sbuf[3] * sbuf[2]), 8, " ");
		wrtln ("BYTES used on drive    ");
		prtDclFmt ((long)((sbuf[1] - sbuf[0]) * sbuf[3] * sbuf[2]), 8, " ");
		wrtln ("BYTES left on drive    ");
		prtDclFmt ((long)(sbuf[0] * sbuf[3] * sbuf[2]), 8, " ");
		wrtln ("Total Units on Drive   ");
		prtDclFmt ((long)sbuf[1], 8, " ");
		wrtln ("Free Units on Drive    ");
		prtDclFmt ((long)sbuf[0], 8, " ");
		wrtln ("Sectors per Unit   ");
		prtDclFmt ((long)sbuf[3], 8, " ");
		wrtln ("Bytes per Sector   ");
		prtDclFmt ((long)sbuf[2], 8, " ");
	    }

	    else if (xncmps(5,s,"INIT"))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		for (i=0; i < BUFSIZ; i++) buf[i] = 0;
		ucase (p);
		drv = *p - 'A';
		buf[0] = 0xf7;
		buf[1] = 0xff;
		buf[2] = 0xff;
		super();
		b = (BPB*)getbpb(drv);
		if (b->b_flags & 1) buf[3] = 0xFF;
		f1 = b->fatrec - b->fsiz;
		f2 = b->fatrec;
		fs = b->fsiz;
		rwabs(1,buf,fs,f1,drv);
		rwabs(1,buf,fs,f2,drv);
		nd = b->recsiz / 32;
		d = buf;
		for (i = 0; i < nd; i++)
		{
		    *d++ = 0;
		    for (j = 0; j < 31; j++) *d++ = 0; /*formerly f6*/
		}
		rec = f2 + fs;
		for (i = 0; i < b->rdlen; i++, rec++)
		    rwabs(1,buf,1,rec,drv);
		user();
		dspMsg(5);
	    }

	    else if (xncmps(8,s,"PUTBOOT"))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		ucase (p);
		drv = *p - 'A';
		fd = xopen(argv[2], 0);
		xread(fd,540L,buf);
		xclose(fd);
		super();
		rwabs(1,&buf[28],1,0,drv);
		user();
		dspMsg(5);
	    }

	    else if ((xncmps(5,s,"COPY")) || (xncmps(5,s,"MOVE")))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		if (argc >= 1)
		{
		    compl_code = copyCmd(p, argv[2], xncmps(5,s,"MOVE") ? 1 : 0);
		    {
			compl_code = 0xFFFFFFFF;
		    }
		}
		else dspMsg(6);
	    }
	    else if (xncmps(6,s,"PAUSE"))
	    {
		cr2cont();
	    }
            else if (xncmps(5,s,"HELP")) dspMsg(17);
#if IMPLEMENTED
            else if (xncmps(6,s,"BREAK")) xbrkpt();
#endif
	    else if (xncmps(5,s,"EXIT"))
	    {
		exit:
		if (*nonStdIn) dspCL (&argv[0]);
		xclose(rd.oldsi);
		xclose(rd.oldso);
		devector();   /* remove vectors */
		xterm(0);
	    }
	    else if (xncmps(8,s,"VERSION"))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		i = xoscall(0x30);
		prtdecl((long)(i&0xFF));
		xwrite(1,1L,".");
		prtdecl((long)((i>>8)&0xFF));
	    }
	    else if (xncmps(5,s,"WRAP"))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		xwrite(1,2L,"\033v");
		dspMsg(5);
	    }
	    else if (xncmps(7,s,"NOWRAP"))
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		xwrite(1,2L,"\033w");
		dspMsg(5);
	    }
	    else if (xncmps(4,s,"CLS"))
		xwrite(1,4L,"\033H\033J");
	    else
	    {
		if (*nonStdIn) dspCL (&argv[0]);
		if (!(execBat (s, (char**)&argv)))
		{
		    if ((compl_code = execPrgm (s, cmdtl)) == -32) errout();
		    else if ((compl_code > 0) && prgerr) errout();
		    else
			if ((compl_code & 0xFFFFFFFF) < 0)
		    {
			wrt ("Command not found.");
			if (prgerr) errout();
		    }
		}
	    }
	}
	chk_redirect (&rd);

	again:
	/*if command coming from outside the command int exit*/
	if ((long)outsd_tl) goto exit;
    }
}

/*
 * cmain
 *
 * bp - Base page address
 */

void cmain(char *bp)
{
    char * parm[MAXARGS];
    char * tl;
    int    i, k, cmd;
    long   j;

    basePage = bp;
    prntEnvPtr = *((char **) (basePage + 0x2C));
    tl = basePage + 0x80;
    if (tl[0]) cmd = -1;
    else cmd = 0;

    xsetdta(srchb);
    path[0] = 0;
    compl_code = 0;
    prgerr = -1;
    cmderr = 0;
    if (!cmd) dspMsg(4);

    i = 0;
    while ((prntEnvPtr[i] + prntEnvPtr[i + 1]) != 0)
    {
	/* if a path has been defined, don't count it.*/
	if (xncmps (5, &prntEnvPtr[i], "PATH="))
	{
	    setPath (&prntEnvPtr[i + 5]);
	    break;
	}
	i++;
    }

    if (!cmd) execBat ((char*)&autoBat, &parm[0]);

    if (xsetjmp(jb))
    {
	for (i = 6; i <= 20; i++) xclose (i);
	if (cmd)
	{
	    tl[0] = 4;
	    tl[1] = 'e';
	    tl[2] = 'x';
	    tl[3] = 'i';
	    tl[4] = 't';
	    tl[5] = 0xd;
	    tl[6] = 0;
	}
    }

    do
    {
	k = 0;
	j = 0;
	xCmdLn (&parm[0], &k, &j, cmd ? tl : (char *)0L);
    }
    while (1);
}
