/*
  $Id: engine.c,v 1.13 1994-08-31 19:42:44 pazsan Exp $
  Copyright 1992 by the ANSI figForth Development Group
*/

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "forth.h"
#include "io.h"

typedef union {
  struct {
#ifdef BIG_ENDIAN
    Cell high;
    Cell low;
#else
    Cell low;
    Cell high;
#endif;
  } cells;
  DCell dcell;
} Double_Store;

typedef struct F83Name {
  struct F83Name	*next;  /* the link field for old hands */
  char			countetc;
  Char			name[0];
} F83Name;

/* are macros for setting necessary? */
#define F83NAME_COUNT(np)	((np)->countetc & 0x1f)
#define F83NAME_SMUDGE(np)	(((np)->countetc & 0x40) != 0)
#define F83NAME_IMMEDIATE(np)	(((np)->countetc & 0x20) != 0)

/* NEXT and NEXT1 are split into several parts to help scheduling */
#ifdef DIRECT_THREADED
#	define NEXT1_P1
#	ifdef i386
#		define NEXT1_P2 ({cfa=*ip++; goto *cfa;})
#	else
#		define NEXT1_P2 ({goto *cfa;})
#	endif
#	define DEF_CA
#else
#	define NEXT1_P1 ({ca = *cfa;})
#	define NEXT1_P2 ({goto *ca;})
#	define DEF_CA	Label ca;
#endif
#if defined(i386) && defined(DIRECT_THREADED)
#	define NEXT_P1
#	define NEXT1 ({goto *cfa;})
#else
#	define NEXT_P1 ({cfa = *ip++; NEXT1_P1;})
#	define NEXT1 ({DEF_CA NEXT1_P1; NEXT1_P2;})
#endif

#define NEXT ({DEF_CA NEXT_P1; NEXT1_P2;})

#ifdef USE_TOS
#define IF_TOS(x) x
#else
#define IF_TOS(x)
#define TOS (sp[0])
#endif

#ifdef USE_FTOS
#define IF_FTOS(x) x
#else
#define IF_FTOS(x)
#define FTOS (fp[0])
#endif

int emitcounter;
#define NULLC '\0'

#ifdef copycstr
#   define cstr(to,from,size)\
	{	memcpy(to,from,size);\
		to[size]=NULLC;}
#else
char scratch[1024];
int soffset;
#   define cstr(from,size) \
           ({ char * to = scratch; \
	      memcpy(to,from,size); \
	      to[size] = NULLC; \
	      soffset = size+1; \
	      to; \
	   })
#   define cstr1(from,size) \
           ({ char * to = scratch+soffset; \
	      memcpy(to,from,size); \
	      to[size] = NULLC; \
	      soffset += size+1; \
	      to; \
	   })
#endif

#define NEWLINE	'\n'


static char* fileattr[6]={"r","rb","r+","r+b","w+","w+b"};

static Address up0=NULL;

#if defined(i386) && defined(FORCE_REG)
#  define REG(reg) __asm__(reg)

Label *engine(Xt *ip0, Cell *sp0, Cell *rp, Float *fp, Address lp)
{
   register Xt *ip REG("%esi")=ip0;
   register Cell *sp REG("%edi")=sp0;

#else
#  define REG(reg)

Label *engine(Xt *ip, Cell *sp, Cell *rp, Float *fp, Address lp)
{
#endif
/* executes code at ip, if ip!=NULL
   returns array of machine code labels (for use in a loader), if ip==NULL
*/
  register Xt cfa
#ifdef i386
#  ifdef USE_TOS
   REG("%ecx")
#  else
   REG("%edx")
#  endif
#endif
   ;
  Address up=up0;
  static Label symbols[]= {
    &&docol,
    &&docon,
    &&dovar,
    &&douser,
    &&dodefer,
    &&dodoes,
    &&dodoes,  /* dummy for does handler address */
#include "prim_labels.i"
  };
  IF_TOS(register Cell TOS;)
  IF_FTOS(Float FTOS;)
#ifdef CPU_DEP
  CPU_DEP;
#endif

  if (ip == NULL)
    return symbols;

  IF_TOS(TOS = sp[0]);
  IF_FTOS(FTOS = fp[0]);
  prep_terminal();
  NEXT;
  
 docol:
#ifdef DEBUG
  printf("%08x: col: %08x\n",(Cell)ip,(Cell)PFA1(cfa));
#endif
#ifdef i386
  /* this is the simple version */
  *--rp = (Cell)ip;
  ip = (Xt *)PFA1(cfa);
  NEXT;
#endif
  /* this one is important, so we help the compiler optimizing
     The following version may be better (for scheduling), but probably has
     problems with code fields employing calls and delay slots
  */
  {
    DEF_CA
    Xt *current_ip = (Xt *)PFA1(cfa);
    cfa = *current_ip;
    NEXT1_P1;
    *--rp = (Cell)ip;
    ip = current_ip+1;
    NEXT1_P2;
  }
  
 docon:
#ifdef DEBUG
  printf("%08x: con: %08x\n",(Cell)ip,*(Cell*)PFA1(cfa));
#endif
#ifdef USE_TOS
  *sp-- = TOS;
  TOS = *(Cell *)PFA1(cfa);
#else
  *--sp = *(Cell *)PFA1(cfa);
#endif
  NEXT;
  
 dovar:
#ifdef DEBUG
  printf("%08x: var: %08x\n",(Cell)ip,(Cell)PFA1(cfa));
#endif
#ifdef USE_TOS
  *sp-- = TOS;
  TOS = (Cell)PFA1(cfa);
#else
  *--sp = (Cell)PFA1(cfa);
#endif
  NEXT;
  
  /* !! user? */
  
 douser:
#ifdef DEBUG
  printf("%08x: user: %08x\n",(Cell)ip,(Cell)PFA1(cfa));
#endif
#ifdef USE_TOS
  *sp-- = TOS;
  TOS = (Cell)(up+*(Cell*)PFA1(cfa));
#else
  *--sp = (Cell)(up+*(Cell*)PFA1(cfa));
#endif
  NEXT;
  
 dodefer:
#ifdef DEBUG
  printf("%08x: defer: %08x\n",(Cell)ip,(Cell)PFA1(cfa));
#endif
  cfa = *(Xt *)PFA1(cfa);
  NEXT1;

 dodoes:
  /* this assumes the following structure:
     defining-word:
     
     ...
     DOES>
     (possible padding)
     possibly handler: jmp dodoes
     (possible branch delay slot(s))
     Forth code after DOES>
     
     defined word:
     
     cfa: address of or jump to handler OR
          address of or jump to dodoes, address of DOES-code
     pfa:
     
     */
#ifdef DEBUG
  printf("%08x/%08x: does: %08x\n",(Cell)ip,(Cell)cfa,*(Cell)PFA(cfa));
  fflush(stdout);
#endif
  *--rp = (Cell)ip;
  /* PFA1 might collide with DOES_CODE1 here, so we use PFA */
  ip = DOES_CODE1(cfa);
#ifdef USE_TOS
  *sp-- = TOS;
  TOS = (Cell)PFA(cfa);
#else
  *--sp = (Cell)PFA(cfa);
#endif
  NEXT;
  
#include "primitives.i"
}
