/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton interface for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     DEFAULT_DEPTH = 258,
     SHUFFLE = 259,
     SUBST = 260,
     TAGS = 261,
     KFEAT = 262,
     KITEM = 263,
     KMONS = 264,
     KMASK = 265,
     KPROP = 266,
     NAME = 267,
     DEPTH = 268,
     ORIENT = 269,
     PLACE = 270,
     CHANCE = 271,
     WEIGHT = 272,
     MONS = 273,
     ITEM = 274,
     MARKER = 275,
     COLOUR = 276,
     PRELUDE = 277,
     MAIN = 278,
     VALIDATE = 279,
     VETO = 280,
     NSUBST = 281,
     WELCOME = 282,
     LFLAGS = 283,
     BFLAGS = 284,
     LFLOORCOL = 285,
     LROCKCOL = 286,
     LFLOORTILE = 287,
     LROCKTILE = 288,
     FTILE = 289,
     RTILE = 290,
     TILE = 291,
     SUBVAULT = 292,
     COMMA = 293,
     COLON = 294,
     PERC = 295,
     INTEGER = 296,
     CHARACTER = 297,
     STRING = 298,
     MAP_LINE = 299,
     MONSTER_NAME = 300,
     ITEM_INFO = 301,
     LUA_LINE = 302
   };
#endif
/* Tokens.  */
#define DEFAULT_DEPTH 258
#define SHUFFLE 259
#define SUBST 260
#define TAGS 261
#define KFEAT 262
#define KITEM 263
#define KMONS 264
#define KMASK 265
#define KPROP 266
#define NAME 267
#define DEPTH 268
#define ORIENT 269
#define PLACE 270
#define CHANCE 271
#define WEIGHT 272
#define MONS 273
#define ITEM 274
#define MARKER 275
#define COLOUR 276
#define PRELUDE 277
#define MAIN 278
#define VALIDATE 279
#define VETO 280
#define NSUBST 281
#define WELCOME 282
#define LFLAGS 283
#define BFLAGS 284
#define LFLOORCOL 285
#define LROCKCOL 286
#define LFLOORTILE 287
#define LROCKTILE 288
#define FTILE 289
#define RTILE 290
#define TILE 291
#define SUBVAULT 292
#define COMMA 293
#define COLON 294
#define PERC 295
#define INTEGER 296
#define CHARACTER 297
#define STRING 298
#define MAP_LINE 299
#define MONSTER_NAME 300
#define ITEM_INFO 301
#define LUA_LINE 302




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 45 "levcomp.ypp"
{
    int i;
    const char *text;
    raw_range range;
}
/* Line 1489 of yacc.c.  */
#line 149 "levcomp.tab.h"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

extern YYSTYPE yylval;

