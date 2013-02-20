/*
 * cimslpCMPI.h
 *
 * (C) Copyright IBM Corp. 2006
 *
 * THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
 * ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
 * CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
 *
 * You can obtain a current copy of the Eclipse Public License from
 * http://www.opensource.org/licenses/eclipse-1.0.php
 *
 * Author:        Sven Schuetz <sven@de.ibm.com>
 * Contributions:
 *
 * Description:
 *
 * Functions getting slp relevant information from the CIMOM utilizing sfcc
 *
 */

#ifndef _cimslpCMPI_h
#define _cimslpCMPI_h

#include <unistd.h>
#include <stdio.h>
#include "cmpi/cmpidt.h"
#include "cmpi/cmpimacs.h"

typedef struct {
  char           *commScheme;   // http or https
  char           *cimhost;
  char           *port;
  char           *cimuser;
  char           *cimpassword;
  char           *trustStore;
  char           *certFile;
  char           *keyFile;
} cimomConfig;

char           *getSLPData(cimomConfig cfg, const CMPIBroker *_broker,
                           const CMPIContext *ctx, char** url_syntax);

#endif
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
