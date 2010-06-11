
/*
 * cimJsonGen.h
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
 * CIM Xml generators .
 *
 */

#ifndef array_h
#define array_h

#include "msgqueue.h"
#include "constClass.h"

#include "native.h"
#include "trace.h"
#include "utilft.h"
#include "string.h"

#include "queryOperation.h"

extern int      value2json(CMPIData d, UtilStringBuffer * sb, int wv);
extern int      instanceName2json(CMPIObjectPath * cop,
                                 UtilStringBuffer * sb);
extern int      cls2json(CMPIConstClass * cls, UtilStringBuffer * sb,
                        unsigned int flags);
extern int      instance2json(CMPIInstance *ci, UtilStringBuffer * sb,
                             unsigned int flags);
extern int      args2json(CMPIArgs * args, UtilStringBuffer * sb);
extern int      enum2json(CMPIEnumeration *enm, UtilStringBuffer * sb,
                         CMPIType type, const char *name, unsigned int flags);
extern int      qualiEnum2json(CMPIEnumeration *enm, UtilStringBuffer * sb);
extern int      qualifierDeclaration2json(CMPIQualifierDecl * q,
                                         UtilStringBuffer * sb);
extern char    *XMLEscape(char *in, int *outlen);
extern void     data2json(CMPIData *data, void *obj, CMPIString *name,
                         CMPIString *refName, char *bTag, int bTagLen,
                         char *eTag, int eTagLen, UtilStringBuffer * sb,
                         UtilStringBuffer * qsb, int inst, int param);
CMPIType        guessType(char *val);

#endif
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
