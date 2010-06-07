
/*
 * cimXmlRequest.h
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

#ifndef handleCimXmlRequest_h
#define handleCimXmlRequest_h

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

typedef struct cimXmlRequestContext {
#ifdef CIM_RS
  char           *method;
  char           *path;
  char           *content;
  unsigned int    length;
#endif
  char           *cimXmlDoc;
  char           *principal;
  char           *host;
  int             teTrailers;
  unsigned int    sessionId;
  unsigned long   cimXmlDocLength;
  struct commHndl *commHndl;
  struct chunkFunctions *chunkFncs;
  char           *className;
  int             operation;
} CimXmlRequestContext;

extern RespSegments handleCimXmlRequest(CimXmlRequestContext * ctx);
extern int      cleanupCimXmlRequest(RespSegments * rs);
#ifdef CIM_RS
extern RespSegments handleCimRsRequest(CimXmlRequestContext * ctx);
extern int      cleanupCimRsRequest(RespSegments * rs);
#endif

#endif
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
