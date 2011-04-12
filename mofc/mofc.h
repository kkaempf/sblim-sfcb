
/* A Bison parser, made by GNU Bison 2.4.1.  */

/* Skeleton interface for Bison's Yacc-like parsers in C
   
      Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

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
     Identifier = 258,
     BaseTypeUINT8 = 259,
     BaseTypeSINT8 = 260,
     BaseTypeUINT16 = 261,
     BaseTypeSINT16 = 262,
     BaseTypeUINT32 = 263,
     BaseTypeSINT32 = 264,
     BaseTypeUINT64 = 265,
     BaseTypeSINT64 = 266,
     BaseTypeREAL32 = 267,
     BaseTypeREAL64 = 268,
     BaseTypeSTRING = 269,
     BaseTypeCHAR16 = 270,
     BaseTypeDATETIME = 271,
     BaseTypeBOOLEAN = 272,
     IntLiteral = 273,
     FloatLiteral = 274,
     StringLiteral = 275,
     CharLiteral = 276,
     BoolLiteral = 277,
     NullLiteral = 278,
     AS = 279,
     CLASS = 280,
     INSTANCE = 281,
     OF = 282,
     QUALIFIER = 283,
     REF = 284,
     FLAVOR = 285,
     SCOPE = 286
   };
#endif
/* Tokens.  */
#define Identifier 258
#define BaseTypeUINT8 259
#define BaseTypeSINT8 260
#define BaseTypeUINT16 261
#define BaseTypeSINT16 262
#define BaseTypeUINT32 263
#define BaseTypeSINT32 264
#define BaseTypeUINT64 265
#define BaseTypeSINT64 266
#define BaseTypeREAL32 267
#define BaseTypeREAL64 268
#define BaseTypeSTRING 269
#define BaseTypeCHAR16 270
#define BaseTypeDATETIME 271
#define BaseTypeBOOLEAN 272
#define IntLiteral 273
#define FloatLiteral 274
#define StringLiteral 275
#define CharLiteral 276
#define BoolLiteral 277
#define NullLiteral 278
#define AS 279
#define CLASS 280
#define INSTANCE 281
#define OF 282
#define QUALIFIER 283
#define REF 284
#define FLAVOR 285
#define SCOPE 286




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
{

/* Line 1676 of yacc.c  */
#line 42 "mofc.y"

        char *                lval_id;
        type_type             lval_type;
        char *                lval_literal;
        class_entry         * lval_class;
        prop_or_method_list * lval_props;
        qual_chain          * lval_quals;
        qual_entry          * lval_qual;
        value_chain         * lval_vals;
        param_chain         * lval_params;
        int                   lval_int;
        qual_quals			  lval_qual_quals;
        


/* Line 1676 of yacc.c  */
#line 130 "mofc.h"
} YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
#endif

extern YYSTYPE yylval;


