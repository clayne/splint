/*
** LCLint - annotation-assisted static program checker
** Copyright (C) 1994-2000 University of Virginia,
**         Massachusetts Institute of Technology
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of the GNU General Public License as published by the
** Free Software Foundation; either version 2 of the License, or (at your
** option) any later version.
** 
** This program is distributed in the hope that it will be useful, but
** WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** The GNU General Public License is available from http://www.gnu.org/ or
** the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
** MA 02111-1307, USA.
**
** For information on lclint: lclint-request@cs.virginia.edu
** To report a bug: lclint-bug@cs.virginia.edu
** For more information: http://lclint.cs.virginia.edu
*/
/*
** llerror.c
**
** error reporting procedures
*/

# include "lclintMacros.nf"
# include <string.h>
# include <errno.h>
# include "llbasic.h"
# include "llmain.h"
# include "version.h"

static void printIndentMessage (FILE *p_stream, /*@only@*/ cstring p_sc, int p_indent)
   /*@modifies *p_stream@*/ ;

# ifndef NOLCL
static int lclerrors = 0;
# endif

static int lastfileloclen = 10;
static /*@only@*/ cstring lastmsg = cstring_undefined;
static int mcount = 0;
static /*@only@*/ cstring saveOneMessage = cstring_undefined;
static /*@only@*/ fileloc lastparseerror = fileloc_undefined;
static /*@only@*/ fileloc lastbug = fileloc_undefined;
static bool llgenerrorreal (/*@only@*/ cstring p_s, fileloc p_fl, bool p_iserror, bool p_indent)
                 /*@modifies g_msgstream@*/ ;
static bool llgenerroraux (/*@only@*/ cstring p_s, fileloc p_fl, bool p_iserror, bool p_indent)
                 /*@modifies g_msgstream@*/ ;
static void printError (FILE *p_stream, /*@only@*/ cstring p_sc)
   /*@globals lastfileloclen @*/
   /*@modifies *p_stream@*/ ;
static void printMessage (FILE *p_stream, /*@only@*/ cstring p_s)
   /*@modifies *p_stream@*/ ;

static /*@null@*/ char *
maxcp (/*@null@*/ /*@returned@*/ char *a, /*@null@*/ /*@returned@*/ char *b)
{
  if (a > b) return a;
  else return b;
}

static void
printBugReport (void)
{
  fprintf (stderr, "     *** Please report bug to %s ***\n",
	   LCLINT_MAINTAINER);
  llflush ();
  /* don't exit (EXIT_FAILURE); */
}

static bool s_needsPrepare = TRUE;

void prepareMessage (void)
{
  if (context_isPreprocessing ()
      && s_needsPrepare
      && context_getDebug (FLG_SHOWSCAN))
    {
      llflush ();
      fprintf (stderr, " >\n");
      s_needsPrepare = FALSE;
    }

  llflush ();
}

void closeMessage (void)
{
  if (context_isPreprocessing ()
      && context_getDebug (FLG_SHOWSCAN))
    {
      llflush ();
      fprintf (stderr, "< more preprocessing .");

      llassert (!s_needsPrepare);
      s_needsPrepare = TRUE;
    }
  else
    {
      llflush ();
    }
}

void
llmsg (/*@only@*/ cstring s)
{
  context_setNeednl ();
  prepareMessage ();
  printMessage (g_msgstream, s);
  closeMessage ();
}

void
lldiagmsg (/*@only@*/ cstring s)
{
  context_setNeednl ();
  prepareMessage ();
  printMessage (stderr, s);
  closeMessage ();
}

void
llmsgplain (/*@only@*/ cstring s)
{
  context_setNeednl ();
  prepareMessage ();
  printMessage (g_msgstream, s);
  closeMessage ();
}

void flagWarning (cstring s)
{
  if (context_getFlag (FLG_WARNFLAGS))
    {
      showHerald ();

      if (fileloc_isBuiltin (g_currentloc))
	{
	  llmsg (message ("Warning: %s", s));
	}
      else
	{
	  llgenmsg (message ("Warning: %s", s), g_currentloc);
	}
    }
}

static void
llgenhint (/*@only@*/ cstring s) /*@modifies g_msgstream@*/
{
  context_setNeednl ();
  printIndentMessage (g_msgstream, s, 2);
}

void
llhint (cstring s)
{
  if (context_getFlag (FLG_HINTS) &&
      !(context_inSuppressRegion () || context_inSuppressZone (g_currentloc)))
    {
      llgenhint (s);
    }
  else
    {
      cstring_free (s);
    }
}

static void
llshowhint (flagcode f)
{
  if (context_getFlag (FLG_HINTS))
    {
      if ((flagcode_numReported (f) == 0) || context_getFlag (FLG_FORCEHINTS))
	{
	  cstring desc = flagcodeHint (f);

	  if (cstring_isDefined (desc))
	    {
	      llgenhint (cstring_copy (desc));
	    }
	}
    }
}


static void
llsuppresshint (char c, flagcode f)
{

  if (context_getFlag (FLG_HINTS))
    {
      if ((flagcode_numReported (f) == 0) || context_getFlag (FLG_FORCEHINTS))
	{
	  cstring desc = flagcodeHint (f);
	  context_setNeednl ();
	  lastfileloclen = 8;

	  if (flagcode_isNamePrefixFlag (f))
	    {
	      f = FLG_NAMECHECKS;
	    }

	  if (cstring_isDefined (desc))
	    {
	      llgenhint (message ("%s (%h%s will suppress message)", desc, c,
				  flagcode_name (f)));
	    }
	  else
	    {
	      llgenhint (message ("(%h%s will suppress message)", c,
				  flagcode_name (f)));
	    }
	}
    }
}

static void
llnosuppresshint (flagcode f)
{

  if (context_getFlag (FLG_FORCEHINTS))
    {
      cstring desc = flagcodeHint (f);
      context_setNeednl ();
      lastfileloclen = 8;

      if (cstring_isDefined (desc))
	{
	  printError (g_msgstream, message ("    %s", desc));
	}
    }
}

/*@constant int MAXSEARCH; @*/
# define MAXSEARCH 20

/*@constant int MINLINE; @*/
# define MINLINE 35

typedef /*@null@*/ /*@dependent@*/ char *nd_charp;

static void
mstring_split (/*@returned@*/ char **sp,
	       /*@out@*/ nd_charp *tp,
	       int maxline, /*@in@*/ int *indentchars)
{
  char *nl;
  char *t;
  char *s = *sp;

  *tp = NULL;

  if (maxline < MINLINELEN)
    {
      maxline = MINLINELEN;
    }

  if (*indentchars > 0)
    {
      s = *sp = mstring_concatFree1 (mstring_spaces (*indentchars), s);
    }

  nl = strchr (s, '\n');

  /*
  ** splitting:
  **
  **    if there is a newline in first maxline characters, split there
  **    if line len is <= maxline, return no split
  **    if there is a ':' or ';' followed by ' ' in first maxline characters,
  **       split there unless the ' ' is followed by a '}', then
  **       split after '}'
  **       of the ';' is inside quotation marks
  **    if there is a space or tab in last maxsearch characters, split there
  **    else, split at maxline
  **
  **    special code: slash [1-9] after a newline means indent the rest <n> chars
  **
  */

  if ((nl != NULL) && ((nl - s) < maxline))
    {
      *nl = '\0';
      t = nl + 1;

      if (*t == '\0')
	{
	  return;
	}

      if (*t >= '\1' && *t <= '\7')
	{
	  *indentchars += (int) (*t - '\1') + 1;
	  t++;
	  	}

      *tp = t;
      return;
    }
  else if (size_toInt (strlen (s)) < maxline)
    {
      return;
    }
  else
    {
      int i = 0;
      char savechar;
      char *lcolon, *lsemi, *splitat;

      splitat = NULL;

      t = s + maxline - 1;
      savechar = *t;

      *t = '\0';
      lcolon = strrchr (s, ':');
      lsemi = strrchr (s, ';');
      *t = savechar;

      splitat = maxcp (lcolon, lsemi);

      if (splitat != NULL && ((int)(splitat - s) > MINLINE)
	  && *(splitat + 1) == ' ' && *(splitat + 2) != '}')
	{
	  *(splitat + 1) = '\0';
	  t = splitat + 2;
	  *tp = t;
	  return;
	}

      while (*t != ' ' && *t != '\t' && i < MAXSEARCH)
	{
	  t--;
	  i++;
	}

      if (*t != ' ' && *t != '\t')
	{
	  t = mstring_copy (s + maxline);
	  *(s + maxline) = '\0';

	  if (*t == '\0')
	    {
	      sfree (t);
	      return;
	    }

	  mstring_markFree (t);
	  *tp = t;
	  return;
	}
      else
	{
	  *t = '\0';
	  t++;

	  if (*t == '\0') return;

	  /*
	  ** returns unqualified as only
	  */

	  *tp = t;
	  return;
	}
    }
}

static
void limitmessage (/*@only@*/ cstring s, fileloc loc)
{
  if (mcount > context_getLimit () + 1)
    {
      cstring_free (s);
    }
  else
    {
      cstring flstring = fileloc_unparse (loc);

      lastfileloclen = cstring_length (flstring);
      cstring_free (saveOneMessage);
      saveOneMessage = message ("%q: %q", flstring, s);
    }
}

static int parseerrorcount = 0;

void cleanupMessages ()
{
  parseerrorcount = 0;

  if (context_unlimitedMessages ())
    {
     ;
    }
  else
    {
      int unprinted = mcount - context_getLimit ();

      if (unprinted > 0)
	{
	  if (unprinted == 1 && cstring_isDefined (saveOneMessage))
	    {
	      prepareMessage ();
	      printError (g_msgstream, saveOneMessage);
	      closeMessage ();
	      saveOneMessage = cstring_undefined;
	    }
	  else
	    {
	      if (cstring_isDefined (saveOneMessage))
		{
		  /* cstring_free (saveOneMessage); */
		  saveOneMessage = cstring_undefined;
		}

	      fprintf (g_msgstream, "%s: (%d more similar errors unprinted)\n",
		       cstring_toCharsSafe (fileloc_filename (g_currentloc)),
		       mcount - context_getLimit ());
	    }
	}
    }

  mcount = 0;
}

void
llgenmsg (/*@only@*/ cstring s, fileloc fl)
{
  cstring flstring = fileloc_unparse (fl);
  lastfileloclen = cstring_length (flstring);

  prepareMessage ();
  (void) printError (g_msgstream, message ("%q: %q", flstring, s));
  closeMessage ();
}

void
llgenindentmsg (/*@only@*/ cstring s, fileloc fl)
{
  cstring flstring = fileloc_unparse (fl);

  prepareMessage ();
  (void) printIndentMessage (g_msgstream, message ("%q: %q", flstring, s), 3);
  closeMessage ();
}

void
llgenindentmsgnoloc (/*@only@*/ cstring s)
{
  prepareMessage ();
  (void) printIndentMessage (g_msgstream, s, 3);
  closeMessage ();
}

static bool
  llgentypeerroraux (flagcode ocode, ctype t1, exprNode e1, ctype t2, exprNode e2,
		     /*@only@*/ cstring s, fileloc fl)
{
  cstring hint = cstring_undefined;
  flagcode code = ocode;
  flagcode hcode = INVALID_FLAG;
  ctype ut1 = t1;
  ctype ut2 = t2;

  DPRINTF (("Type error: %s / %s : %s / %s",
	    exprNode_unparse (e1), exprNode_unparse (e2),
	    ctype_unparse (t1), ctype_unparse (t2)));
  
  DPRINTF (("Bool: %s / %s",
	    bool_unparse (ctype_isBool (t1)),
	    bool_unparse (ctype_isBool (t2))));

  /* 
  ** Set the flag using the underlying types that didn't match.
  */

  while (ctype_isPointer (ut1) && ctype_isPointer (ut2)) {
    ut1 = ctype_baseArrayPtr (ut1);
    ut2 = ctype_baseArrayPtr (ut2);
  }

  if ((ctype_isFloat (ut1) && ctype_isDouble (ut2))
      || (ctype_isFloat (ut1) && ctype_isDouble (ut2)))
    {
      hcode = FLG_FLOATDOUBLE;
    }
  else if ((exprNode_isCharLit (e1) && ctype_isInt (ut2))
	   || (exprNode_isCharLit (e2) && ctype_isInt (ut1)))
    {
      hcode = FLG_CHARINTLITERAL;
    }
  else if ((exprNode_isNumLit (e1) && ctype_isReal (ut2))
	   || (exprNode_isNumLit (e2) && ctype_isReal (ut1)))
    {
      hcode = FLG_NUMLITERAL;
    }
  else if ((ctype_isManifestBool (ut1) && ctype_isInt (ut2))
	   || (ctype_isInt (ut1) && ctype_isManifestBool (ut2)))
    /* evs 2000-07-24: was ctype_isDirectBool */
    {
      hcode = FLG_BOOLINT;
    }
  else if (((ctype_isChar (ut1) && !ctype_isInt (ut1)) && ctype_isInt (ut2))
	   || ((ctype_isInt (ut1) && (ctype_isChar (ut2) && !ctype_isInt (ut2)))))
    {
      hcode = FLG_CHARINT;
    }
  else if ((ctype_isInt (ut1) && ctype_isInt (ut2))
	   || (ctype_isChar (ut1) && ctype_isChar (ut2))
	   || (ctype_isDouble (ut1) && ctype_isDouble (ut2)))
    {
      if (!bool_equal (ctype_isSigned (ut1), ctype_isSigned (ut2)))
	{
	  hcode = FLG_IGNORESIGNS;
	}
      else
	{
	  hcode = FLG_IGNOREQUALS;
	}
    }
  else if (ctype_isArbitraryIntegral (ctype_realType (ut1)))
    {
      if (ctype_isArbitraryIntegral (ctype_realType (ut2)))
	{
	  hcode = FLG_MATCHANYINTEGRAL;
	}
      else if (ctype_match (ut2, ctype_ulint))
	{
	  hcode = FLG_LONGUNSIGNEDINTEGRAL;
	}
      else if (ctype_match (ut2, ctype_lint))
	{
	  hcode = FLG_LONGINTEGRAL;
	}
      else if (ctype_isInt (ut2))
	{
	  hcode = FLG_MATCHANYINTEGRAL;
	}
      else
	{
	  ;
	}
    }
  else if (ctype_isArbitraryIntegral (ctype_realType (ut2)))
    {
      ctype tr = ctype_realType (ut1);

      if (ctype_isArbitraryIntegral (tr))
	{
	  hcode = FLG_MATCHANYINTEGRAL;
	}
      else if (ctype_match (ut1, ctype_ulint))
	{
	  if (ctype_isUnsignedIntegral (tr))
	    {
	      hcode = FLG_LONGUNSIGNEDUNSIGNEDINTEGRAL;
	    }
	  else if (ctype_isSignedIntegral (tr))
	    {
	      ;
	    }
	  else
	    {
	      hcode = FLG_LONGUNSIGNEDINTEGRAL;
	    }
	}
      else if (ctype_match (ut1, ctype_lint))
	{
	  if (ctype_isSignedIntegral (tr))
	    {
	      hcode = FLG_LONGSIGNEDINTEGRAL;
	    }
	  else if (ctype_isSignedIntegral (tr))
	    {
	      ;
	    }
	  else
	    {
	      hcode = FLG_LONGINTEGRAL;
	    }
	}
      else if (ctype_isInt (ut1))
	{
	  hcode = FLG_MATCHANYINTEGRAL;
	}
      else
	{
	  ;
	}
    }
  else if (ctype_isAbstract (ut1) && !ctype_isAbstract (ut2))
    {
      uentry ue1 = usymtab_getTypeEntry (ctype_typeId (ut1));
      ctype ct = uentry_getType (ue1);

      if (ctype_match (ct, ut2))
	{
	  code = FLG_ABSTRACT;
	  hint = message ("Underlying types match, but %s is an "
			  "abstract type that is not accessible here.",
			  ctype_unparse (t1));
	}
    }
  else if (ctype_isAbstract (ut2) && !ctype_isAbstract (ut1))
    {
      uentry ue = usymtab_getTypeEntry (ctype_typeId (ut2));
      ctype ct = uentry_getType (ue);

      if (ctype_match (ct, ut1))
	{
	  code = FLG_ABSTRACT;
	  hint = message ("Underlying types match, but %s is an "
			  "abstract type that is not accessible here.",
			  ctype_unparse (t2));
	}
    }
  else if ((ctype_isEnum (ut1) && ctype_isArbitraryIntegral (ut2))
	   || (ctype_isEnum (ut2) && ctype_isArbitraryIntegral (ut1))) 
    {
      code = FLG_ENUMINT;
    }
  else if ((ctype_isEnum (ut1) && ctype_isArbitraryIntegral (ut2))
	   || (ctype_isEnum (ut2) && ctype_isArbitraryIntegral (ut1))) 
    {
      code = FLG_ENUMINT;
    }
  else if ((ctype_isSignedChar (ut1) && ctype_isUnsignedChar (ut2))
	   || (ctype_isUnsignedChar (ut1) && ctype_isSignedChar (ut2)))
    {
      code = FLG_CHARUNSIGNEDCHAR;
    }
  else if (ctype_isNumeric (ut1) && ctype_isNumeric (ut2)) 
    {
      code = FLG_RELAXTYPES;
    }
  else
    {
      DPRINTF (("No special type rule: %s / %s", ctype_unparse (ut1),
		ctype_unparse (ut2)));
    }

  if (cstring_isDefined (hint))
    {
      if (!context_suppressFlagMsg (ocode, fl))
	{
	  return llgenhinterror (code, s, hint, fl);
	}
      else
	{
	  cstring_free (s);
	  cstring_free (hint);
	  return FALSE;
	}
    }
  else
    {
      if (llgenerroraux (s, fl, TRUE, FALSE))
	{
	  if (hcode != INVALID_FLAG && hcode != ocode)
	    {
	      code = hcode;
	      llshowhint (code);

	    }
	  else
	    {
	      llsuppresshint ('-', code);
	    }

	  flagcode_recordError (code);
	  return TRUE;
	}

      return FALSE;
    }
}

bool
llgentypeerror (ctype t1, exprNode e1, ctype t2, exprNode e2,
		/*@only@*/ cstring s, fileloc fl)
{
  return llgentypeerroraux (FLG_TYPE, t1, e1, t2, e2, s, fl);
}

bool
llgenformattypeerror (ctype t1, exprNode e1, ctype t2, exprNode e2,
		      /*@only@*/ cstring s, fileloc fl)
{
  return llgentypeerroraux (FLG_FORMATTYPE, t1, e1, t2, e2, s, fl);
}

bool
llgenerror (flagcode o, /*@only@*/ cstring s, fileloc fl)
{
  if (llgenerroraux (s, fl, TRUE, FALSE))
    {
      llnosuppresshint (o);
      flagcode_recordError (o);
      closeMessage ();
      return TRUE;
    }
  else
    {
      flagcode_recordSuppressed (o);
      return FALSE;
    }
}

bool
llgenhinterror (flagcode o, /*@only@*/ cstring s, /*@only@*/ cstring hint,
		fileloc fl)
{
  if (!context_suppressFlagMsg (o, fl))
    {
      if (llgenerroraux (s, fl, TRUE, FALSE))
	{
	  flagcode_recordError (o);

	  if (context_getFlag (FLG_HINTS))
	    {
	      llgenhint (hint);
	    }
	  else
	    {
	      cstring_free (hint);
	    }

	  closeMessage ();
	  return TRUE;
	}

      cstring_free (hint);
    }
  else
    {
      cstring_free (hint);
      cstring_free (s);
    }

  flagcode_recordSuppressed (o);
  return FALSE;
}

static bool
llrealerror (/*@only@*/ cstring s, fileloc fl)
{
  return (llgenerrorreal (s, fl, TRUE, FALSE));
}

static bool
llgenerroraux (/*@only@*/ cstring s, fileloc fl, bool iserror, bool indent)
{

  if (context_inSuppressZone (fl))
    {
      cstring_free (s);
      return FALSE;
    }
  else
    {
      ;
    }

  return (llgenerrorreal (s, fl, iserror, indent));
}

void
llforceerror (flagcode code, /*@only@*/ cstring s, fileloc fl)
{
  flagcode_recordError (code);
  (void) llgenerrorreal (s, fl, TRUE, FALSE);
  closeMessage ();
}

static bool
llgenerrorreal (/*@only@*/ cstring s, fileloc fl, bool iserror, bool indent)
{
  cstring flstring;

  /* duplicate message (rescanning a header file */

  if (!messageLog_add (context_messageLog (), fl, s))
    {
      cstring_free (s);
      return FALSE;
    }

  if (iserror) context_hasError ();

  if (context_unlimitedMessages ())
    {
      ;
    }
  else
    {
      /*
      ** suppress excessive messages:
      **    check up to ':'
      **
      */

      char *sc = cstring_toCharsSafe (s);
      char *tmpmsg = strchr (sc, ':');

      if (tmpmsg == NULL)
	{
	  tmpmsg = sc;
	}
      else
	{
	  char *savechar = tmpmsg;
	  *tmpmsg = '\0';
	  tmpmsg = sc;
	  *savechar = ':';
	}

      if (cstring_equal (lastmsg, cstring_fromChars (tmpmsg)))
	{
	  mcount++;
	  if (mcount == (context_getLimit () + 1))
	    {
	      limitmessage (s, fl);
	      return FALSE;
	    }

	  if (mcount > (context_getLimit ()))
	    {
	      cstring_free (s);
	      return FALSE;
	    }
	}
      else
	{
	  cleanupMessages ();
	  mcount = 0;
	  cstring_free (lastmsg);
	  lastmsg = cstring_fromCharsNew (tmpmsg);
	}
    }

  if (context_hasAliasAnnote ())
    {
      char *sc = cstring_toCharsSafe (s);
      char *fcolon = strchr (sc, ':');
      cstring a = context_getAliasAnnote ();


      if (fcolon == NULL)
	{
	  s = message ("%q (%q)", s, a);
	}
      else
	{
	  cstring afterColon;

	  *fcolon = '\0';
	  afterColon = cstring_fromCharsNew (fcolon + 1);

	  s = message ("%q (%q):%q", s, a, afterColon);
	}
          }

  if (context_hasMessageAnnote ())
    {
      char *fcolon = strchr (cstring_toCharsSafe (s), ':');


      if (fcolon == NULL)
	{
	  /*@-dependenttrans@*/ /* s becomes dependent for fcolon */
	  s = message ("%q (%q)", s, context_getMessageAnnote ());
	  /*@=dependenttrans@*/
	}
      else
	{
	  cstring afterColon;

	  *fcolon = '\0';
	  afterColon = cstring_fromCharsNew (fcolon + 1);

	  /*@-dependenttrans@*/ /* s becomes dependent for fcolon */
	  s = message ("%q (%q):%q", s,
		       context_getMessageAnnote (), afterColon);
	  /*@=dependenttrans@*/
	}
     }

  context_setNeednl ();
  prepareMessage ();

  if (context_showFunction ())
    {
      cstring fname = fileloc_unparseFilename (g_currentloc);

      if (context_inIterDef ())
	{
	  fprintf (g_msgstream, "%s: (in iter %s)\n",
		   cstring_toCharsSafe (fname),
		   cstring_toCharsSafe (context_inFunctionName ()));
	}
      else if (context_inIterEnd ())
	{
	  fprintf (g_msgstream, "%s: (in iter finalizer %s)\n",
		   cstring_toCharsSafe (fname),
		   cstring_toCharsSafe (context_inFunctionName ()));
	}
      else if (context_inMacro ())
	{
	  fprintf (g_msgstream, "%s: (in macro %s)\n", cstring_toCharsSafe (fname),
		   cstring_toCharsSafe (context_inFunctionName ()));
	}
      else
	{
	  fprintf (g_msgstream, "%s: (in function %s)\n",
		   cstring_toCharsSafe (fname),
		   cstring_toCharsSafe (context_inFunctionName ()));
	}

      cstring_free (fname);
      context_setShownFunction ();
    }

  flstring = fileloc_unparse (fl);

  lastfileloclen = cstring_length (flstring);

  if (indent)
    {
      printError (g_msgstream, message ("   %q: %q", flstring, s));
    }
  else
    {
      printError (g_msgstream, message ("%q: %q", flstring, s));
    }

  return TRUE;
}

/*
** printMessage
**
** message contains no '\n'
**    message fits in one line: print it
**    message fits in two lines with 3-space indent after fileloc: print it
**    split line with 5-space indent from left margin: print it
**
*/

static
void printMessage (FILE *stream, /*@only@*/ cstring s)
{
  printIndentMessage (stream, s, 0);
}

static
void printIndentMessage (FILE *stream, /*@only@*/ cstring sc, int indent)
{
  int maxlen = context_getLineLen ();
  char *s = cstring_toCharsSafe (sc);


  do
    {
      char *t = NULL;
      char *st = s;

      mstring_split (&st, &t, maxlen, &indent);
      fprintf (stream, "%s\n", st);
      llassert (t != s);
      s = t;
    } while (s != NULL) ;

  cstring_free (sc);
}

static
void printError (FILE *stream, /*@only@*/ cstring sc)
{
  int maxlen = context_getLineLen ();
  int nspaces = lastfileloclen + 5;
  int nextlen = maxlen - nspaces;
  int len = cstring_length (sc);
  int indent = 0;
  char *s = cstring_toCharsSafe (sc);
  char *os = s;
  char *t = NULL;

  if (len < (maxlen + nextlen) && (strchr (s, '\n') == NULL))
    {
      mstring_split (&s, &t, maxlen, &indent);

      fprintf (stream, "%s\n", s);

      if (t != NULL)
	{
	  len = mstring_length (t);

	  if (len < (maxlen - 3) && (strchr (t, '\n') == NULL)
	      && len > (nextlen - 1))
	    {
	      fprintf (stream, "    %s\n", t);
	    }
	  else
	    {
	      char *spaces = (char *) dmalloc ((nspaces + 1) * sizeof (*spaces));
	      int i;

	      for (i = 0; i < nspaces; i++)
		{
		  spaces[i] = ' ';
		}

	      spaces[nspaces] = '\0';

	      while (t != NULL)
		{
		  char *st = t;
		  mstring_split (&st, &t, nextlen, &indent);
		  fprintf (stream, "%s%s\n", spaces, st);
		}

	      sfree (spaces);
	    }
	}
    }
  else
    {
      if (len < (maxlen + maxlen - 1) && (strchr (s, '\n') != NULL))
	{
	  nspaces = ((maxlen + maxlen - 1) - len) / 2;

	  if (nspaces < 1) nspaces = 1;

	  nextlen = maxlen - nspaces;

	  mstring_split (&s, &t, maxlen, &indent);

	  fprintf (stream, "%s\n", s);

	  if (t != NULL)
	    {
	      char *spaces = (char *) dmalloc ((nspaces + 1) * sizeof (*spaces));
	      int i;

	      for (i = 0; i < nspaces; i++)
		{
		  spaces[i] = ' ';
		}

	      spaces[nspaces] = '\0';

	      while (t != NULL)
		{
		  char *st = t;

		  mstring_split (&st, &t, nextlen, &indent);
		  fprintf (stream, "%s%s\n", spaces, st);
		}

	      sfree (spaces);
	    }
	}
      else
	{
	  nspaces = 4;
	  nextlen = maxlen - nspaces;

	  mstring_split (&s, &t, maxlen, &indent);

	  fprintf (stream, "%s\n", s);

	  if (t != NULL)
	    {
  	      char *spaces = (char *) dmalloc ((nspaces + 1) * sizeof (*spaces));
	      int i;

	      for (i = 0; i < nspaces; i++)
		{
		  spaces[i] = ' ';
		}

	      spaces[nspaces] = '\0';

	      while (t != NULL)
		{
		  char *st = t;
		  mstring_split (&st, &t, nextlen, &indent);
		  fprintf (stream, "%s%s\n", spaces, st);
		}

	      sfree (spaces);
	    }
	}
    }

  sfree (os);
}

void
llfatalbug (/*@only@*/ cstring s)
{
  prepareMessage ();
  printError (stderr, message ("%q: *** Fatal bug: %q",
			       fileloc_unparse (g_currentloc), s));
  printCodePoint ();
  printBugReport ();
  llexit (LLFAILURE);
}

# ifndef NOLCL
void
lclfatalbug (char *msg)
{
  prepareMessage ();
  printError (stderr,
	      message ("*** Fatal Bug: %s", cstring_fromChars (msg)));
  printCodePoint ();
  printBugReport ();
  llexit (LLFAILURE);
}
# endif

void
checkParseError (void)
{
  if (fileloc_withinLines (lastparseerror, g_currentloc, 10))
    {
      llfatalerror (message ("%q: Cannot recover from parse error.",
			     fileloc_unparse (g_currentloc)));
    }
}

void llbugaux (cstring file, int line, /*@only@*/ cstring s)
{
  /*@unchecked@*/
  static int numbugs = 0;
  static bool inbug = FALSE;

  if (inbug)
    {
      cstring temps = fileloc_unparseRaw (file, line);

      fprintf (stderr, "%s: Recursive bug detected: %s\n",
	       cstring_toCharsSafe (temps),
	       cstring_toCharsSafe (s));
      cstring_free (temps);

      llexit (LLFAILURE);
    }

  inbug = TRUE;

  prepareMessage ();

  if (fileloc_isRealLib (g_currentloc))
    {
      llfatalerror (message ("%q: Library file appears to be corrupted.  Error: %q:%s",
			     fileloc_unparse (g_currentloc), 
			     fileloc_unparseRaw (file, line), s));
    }

  if (fileloc_withinLines (lastparseerror, g_currentloc, 7))
    {
      llfatalerror (message ("%q: Cannot recover from parse error.",
			     fileloc_unparse (g_currentloc)));
    }

  (void) fflush (g_msgstream);
  printError (stderr, message ("%q: *** Internal Bug at %q: %q [errno: %d]",
			       fileloc_unparse (g_currentloc),
			       fileloc_unparseRaw (file, line),
			       s, errno));
  printCodePoint ();

  (void) fflush (stderr);
  perror ("Possible system error diagnostic: ");
  (void) fflush (stderr);

  printBugReport ();

  numbugs++;

  if (numbugs > 5 && fileloc_withinLines (lastbug, g_currentloc, 2))
    {
      llfatalerror (message ("%q: Cannot recover from last bug.",
			     fileloc_unparse (g_currentloc)));
    }

  fprintf (stderr, "       (attempting to continue, results may be incorrect)\n");
  fileloc_free (lastbug);
  lastbug = fileloc_copy (g_currentloc);
  closeMessage ();

  (void) fflush (stderr);
  inbug = FALSE;
}

# ifndef NOLCL
void
lclbug (/*@only@*/ cstring s)
{
  prepareMessage ();
  printError (stderr, message ("*** Internal Bug: %q", s));
  printCodePoint ();
  printBugReport ();
  fprintf (stderr, "       (attempting to continue, results may be incorrect)\n");
  closeMessage ();
}
# endif

void
llfatalerror (cstring s)
{
  prepareMessage ();
  printError (stderr, s);
  printError (stderr, cstring_makeLiteral ("*** Cannot continue."));
  llexit (LLFAILURE);
}

void
llfatalerrorLoc (/*@only@*/ cstring s)
{
  prepareMessage ();
  (void) fflush (g_msgstream);
  printError (stderr, message ("%q: %q", fileloc_unparse (g_currentloc), s));
  printError (stderr, cstring_makeLiteral ("*** Cannot continue."));
  llexit (LLFAILURE);
}

/*
** free's s!
*/

void
llgloberror (/*@only@*/ cstring s)
{
  if (context_inSuppressRegion ())
    {
      cstring_free (s);
    }
  else
    {
      context_setNeednl ();
      prepareMessage ();
      context_hasError ();
      flagcode_recordError (FLG_SPECIAL);
      printError (g_msgstream, s);
      closeMessage ();
    }
}

# ifndef NOLCL
bool
lclHadError (void)
{
  return (lclerrors > 0);
}

bool
lclHadNewError (void)
{
  static int lastcall = 0;

  if (lclerrors > lastcall)
    {
      lastcall = lclerrors;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

int
lclNumberErrors (void)
{
  return (lclerrors);
}

void
lclerror (ltoken t, /*@only@*/ cstring msg)
{
  lclerrors++;

  if (ltoken_getCode (t) != NOTTOKEN)
    {
      cstring loc = ltoken_unparseLoc (t);

      lastfileloclen = cstring_length (loc);

      printError (g_msgstream, message ("%q: %q", loc, msg));
    }
  else
    {
      printError (g_msgstream, msg);
    }
}

void
lclplainerror (/*@only@*/ cstring msg)
{
  lclerrors++;

  printError (g_msgstream, msg);
}

void
lclfatalerror (ltoken t, /*@only@*/ cstring msg)
{
  lclerror (t, msg);
  (void) fflush (g_msgstream);
  printError (stderr, cstring_makeLiteral ("*** Cannot continue"));
  llexit (LLFAILURE);
}

void
lclplainfatalerror (/*@only@*/ cstring msg)
{
  (void) fflush (g_msgstream);
  printError (stderr, message ("*** Cannot continue: %q", msg));
  llexit (LLFAILURE);
}

void
lclRedeclarationError (ltoken id)
{
  cstring s = ltoken_getRawString (id);


  if (usymtab_existsEither (s))
    {
      uentry le = usymtab_lookupEither (s);

      lclerror (id, message ("Respecification of %s", s));
      llgenindentmsg (message ("Previous specification of %q",
			       uentry_getName (le)),
		uentry_whereSpecified (le));
    }
  else
    {
      lclerror (id, message ("Identifier redeclared: %s", s));
    }
}
# endif

void genppllerror (flagcode code, /*@only@*/ cstring s)
{
  if (context_inSuppressZone (g_currentloc))
    {
      cstring_free (s);
    }
  else
    {
      if (context_getFlag (code))
	{
	  if (context_getFlag (FLG_SHOWSCAN) && !context_isInCommandLine ())
	    {
	      fprintf (g_msgstream, " >\n");
	    }

	  llerror (code, s);

	  if (code != FLG_PREPROC)
	    {
	      llsuppresshint ('-', code);
	    }

	  if (context_getFlag (FLG_SHOWSCAN) && !context_isInCommandLine ())
	    {
	      fprintf (stderr, "< more preprocessing .");
	    }
	}
      else
	{
	  cstring_free (s);
	}
    }
}

void genppllerrorhint (flagcode code, /*@only@*/ cstring s,
		       /*@only@*/ cstring hint)
{
  if (context_inSuppressZone (g_currentloc))
    {
      cstring_free (s);
      cstring_free (hint);
    }
  else
    {
      if (context_getFlag (code))
	{
	  prepareMessage ();
	  context_clearPreprocessing ();
	  llerror (code, s);
	  llgenhint (hint);
	  context_setPreprocessing ();
	  closeMessage ();
	}
      else
	{
	  cstring_free (s);
	  cstring_free (hint);
	}
    }
}

void ppllerror (/*@only@*/ cstring s)
{
  genppllerror (FLG_PREPROC, s);
}

void pplldiagmsg (cstring s)
{
  if (context_getDebug (FLG_SHOWSCAN) && !context_isInCommandLine ())
    {
      fprintf (stderr, " >\n");
      lldiagmsg (s);
      fprintf (stderr, "< more preprocessing .");
    }
  else
    {
      lldiagmsg (s);
    }
}

void loadllmsg (cstring s)
{
  if (context_getDebug (FLG_SHOWSCAN))
    {
      fprintf (stderr, " >\n");
      lldiagmsg (s);
      fprintf (stderr, "< .");
    }
  else
    {
      lldiagmsg (s);
    }
}

static void llreportparseerror (/*@only@*/ cstring s)
{
  if (fileloc_withinLines (lastparseerror, g_currentloc, 5))
    {
      cstring_free (s);
    }
  else
    {
      llerror (FLG_SYNTAX, s);

      fileloc_free (lastparseerror);
      lastparseerror = fileloc_copy (g_currentloc);
    }
}

bool lloptgenerror (flagcode o, /*@only@*/ cstring s, fileloc loc)
{
  if (llrealerror (s, loc))
    {
      llsuppresshint ('-', o);
      closeMessage ();
      flagcode_recordError (o);
      return TRUE;
    }

  flagcode_recordSuppressed (o);
  return FALSE;
}

bool optgenerror2 (flagcode f1, flagcode f2, /*@only@*/ cstring s, fileloc loc)
{
  if (context_suppressFlagMsg (f1, loc))
    {
      flagcode_recordSuppressed (f1);
      cstring_free (s);
    }
  else
    {
      if (context_suppressFlagMsg (f2, loc))
	{
	  flagcode_recordSuppressed (f2);
	  cstring_free (s);
	}
      else
	{
	  if (llrealerror (s, loc))
	    {
	      llsuppresshint ('-', f2);
	      flagcode_recordError (f2);
	      closeMessage ();
	      return TRUE;
	    }

	  flagcode_recordSuppressed (f2);
	}
    }
  return FALSE;
}

bool optgenerror2n (flagcode f1, flagcode f2, /*@only@*/ cstring s, fileloc loc)
{

  if (context_suppressFlagMsg (f1, loc))
    {
      flagcode_recordSuppressed (f1);
      cstring_free (s);
    }
  else
    {
      if (context_suppressNotFlagMsg (f2, loc))
	{
	  flagcode_recordSuppressed (f2);
	  cstring_free (s);
	}
      else
	{
	  if (llrealerror (s, loc))
	    {
	      llsuppresshint ('+', f2);
	      flagcode_recordError (f2);
	      closeMessage ();
	      return TRUE;
	    }

	  flagcode_recordSuppressed (f2);
	}
    }
  return FALSE;
}

bool llnoptgenerror (flagcode o, /*@only@*/ cstring s, fileloc loc)
{
  if (llrealerror (s, loc))
    {
      llsuppresshint ('+', o);
      flagcode_recordError (o);
      closeMessage ();
      return TRUE;
    }

  flagcode_recordSuppressed (o);
  return FALSE;
}

void llparseerror (cstring s)
{
  if (context_getFlag (FLG_TRYTORECOVER))
    {
      parseerrorcount++;

      if (parseerrorcount > GIVEUPPARSE)
	{
	  if (cstring_isDefined (s))
	    {
	      llfatalerror (message ("%q: Parse Error: %q.  "
				     "Too many errors, giving up.",
				     fileloc_unparse (g_currentloc), s));
	    }
	  else
	    {
	      llfatalerror (message ("%q: Parse Error.  Too many errors, giving up.",
				     fileloc_unparse (g_currentloc)));
	    }
	}
      else
	{
	  if (cstring_isDefined (s))
	    {
	      llreportparseerror (message ("Parse Error: %q. Attempting to continue.",
					   s));
	    }
	  else
	    {
	      llreportparseerror (message ("Parse Error. Attempting to continue."));
	    }
	}
    }
  else
    {
      cstring msg;

      if (cstring_isDefined (s))
	{
	  msg = message ("Parse Error: %q.", s);
	}
      else
	{
	  msg = message ("Parse Error.");
	}

      llfatalerror
	(message ("%q: %s (For help on parse errors, "
		  "see lclint -help parseerrors.)",
		  fileloc_unparse (g_currentloc), msg));
    }
}

bool doCheck (bool x, cstring pred, cstring file, int line)
{
  if (!x) {
    llbug (message ("%q: Check Failed: %s",
		    fileloc_unparseRaw (file, line),
		    pred));
  }

  return x;
}

/*@observer@*/ cstring lldecodeerror (/*@unused@*/ int errnum)
{
  char *result;

#ifndef VMS
#ifndef HAVE_STRERROR
  result = NULL;
#else
  result = strerror (errnum);
#endif
#else	/* VMS */
  /* VAXCRTL's strerror() takes an optional second argument, which only
     matters when the first argument is EVMSERR.  However, it's simplest
     just to pass it unconditionally.  `vaxc$errno' is declared in
     <errno.h>, and maintained by the library in parallel with `errno'.
     We assume that caller's `errnum' either matches the last setting of
     `errno' by the library or else does not have the value `EVMSERR'.  */

  result = strerror (errnum, vaxc$errno);
#endif

  if (result == NULL)
    {
      result = cstring_toCharsSafe (message ("undocumented I/O error: %d", errnum));
    }

  return cstring_fromChars (result);
}

void llquietbugaux (cstring s, /*@unused@*/ cstring file, /*@unused@*/ int line)
{
# if 0
# ifdef HOMEVERSION
  llflush ();
  printError (stderr, message ("%q: *** Internal Bug at %q: %q [errno: %d]",
			       fileloc_unparse (g_currentloc),
			       fileloc_unparseRaw (file, line),
			       s, errno));
  printCodePoint ();
  llflush ();
# endif
# else
  cstring_free (s);
# endif
}

void llflush (void)
{
  (void) fflush (g_msgstream);
  (void) fflush (stderr);
}