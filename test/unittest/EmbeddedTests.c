/*
 * This file provides the launching point for all unit tests
 * that are embedded in the parent .c files. In order to embed
 * a unit test you should write routines in the parent source file
 * you wish to test and define their prototypes in the corresponding
 * header file. The write calls to those routines in this file with 
 * the appropriate return or result checks to validate the tests
 *
 * Be sure and wrap all embedded test routines with 
 * "#ifdef UNITTEST" and "#endif" to prevent them from being included
 * in production builds. 
 *
 */

#define CMPI_PLATFORM_LINUX_GENERIC_GNU

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Include the header file for each embedded test routine.
#include "trace.h"
#include "queryOperation.h"
#include "objectImpl.h"
int trimws;

int
main(void)
{
  // Overall success, set this to 1 if any test fails
  int             fail = 0;
  int             rc;
  printf("  Performing embedded unit tests ...\n");

  printf("  Testing trace.c ...\n");
  rc = trace_test();
  if (rc != 0)
    fail = 1;

  printf("  Testing queryOperation.c ...\n");
  rc = queryOperation_test();
  if (rc != 0)
    fail = 1;

  printf("  Testing objectImpl.c ...\n");
  rc = oi_test();
  if (rc != 0)
    fail = 1;

  // Return the overall results.
  return fail;
}
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
