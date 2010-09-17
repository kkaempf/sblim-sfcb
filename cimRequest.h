
/*
 * cimRequest.h
 *
 * (C) Copyright IBM Corp. 2005
 *
 * THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
 * ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
 * CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
 *
 * You can obtain a current copy of the Eclipse Public License from
 * http://www.opensource.org/licenses/eclipse-1.0.php
 *
 * Author:       Adrian Schuur <schuur@de.ibm.com>
 *
 * Description:
 * CMPI broker encapsulated functionality.
 *
 * CIM operations request handler .
 *
 */

#ifndef handleCimRequest_h
#define handleCimRequest_h

#include "msgqueue.h"
#include "providerMgr.h"

struct commHndl;
struct chunkFunctions;

typedef struct respSegment {
  int             mode;
  char           *txt;
} RespSegment;

typedef struct respSegments {
  void           *buffer;
  int             chunkedMode,
                  rc;
  char           *errMsg;
  RespSegment     segments[7];
} RespSegments;

typedef struct expSegments {
  RespSegment     segments[7];
} ExpSegments;

typedef struct cimRequestContext {
  char           *cimDoc;
  char           *principal;
  char           *host;
  char           *contentType;
  int             teTrailers;
  unsigned int    sessionId;
  unsigned long   cimDocLength;
  struct commHndl *commHndl;
  struct chunkFunctions *chunkFncs;
  char           *className;
  int             operation;
  char           *path;
} CimRequestContext;

extern RespSegments handleCimRequest(CimRequestContext * ctx);
extern int      cleanupCimXmlRequest(RespSegments * rs);

#endif
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
