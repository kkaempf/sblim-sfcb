
/*
 * cimXmlGen.h
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

#include "cimRequest.h"
#include "cimXmlParser.h"
#include "msgqueue.h"
#include "constClass.h"

#include "native.h"
#include "trace.h"
#include "string.h"

#include "config.h"
#ifdef SFCCOMMON
#include "utilft.h"
#else
#include <sfcCommon/utilft.h>
#endif

#include "queryOperation.h"

extern CMPIValue *getKeyValueTypePtr(char *type, char *value,
                                     XtokValueReference * ref,
                                     CMPIValue * val, CMPIType *typ,
                                     char *scopingNS);
extern CMPIValue str2CMPIValue(CMPIType type, XtokValue val,
                               XtokValueReference * ref, char *ns);
extern int      value2xml(CMPIData d, UtilStringBuffer * sb, int wv);
extern int      instanceName2xml(CMPIObjectPath * cop,
                                 UtilStringBuffer * sb);
extern int      cls2xml(CMPIConstClass * cls, UtilStringBuffer * sb,
                        unsigned int flags);
extern int      instance2xml(CMPIInstance *ci, UtilStringBuffer * sb,
                             unsigned int flags);
extern int      args2xml(CMPIArgs * args, UtilStringBuffer * sb);
extern int      enum2xml(CMPIEnumeration *enm, UtilStringBuffer * sb,
                         CMPIType type, int xmlAs, unsigned int flags);
extern int      qualiEnum2xml(CMPIEnumeration *enm, UtilStringBuffer * sb);
extern CMPIValue union2CMPIValue(CMPIType type, char *val,
                                 XtokValueArray * arr);
extern int      qualifierDeclaration2xml(CMPIQualifierDecl * q,
                                         UtilStringBuffer * sb);
extern char    *XMLEscape(char *in, int *outlen);
extern void     data2xml(CMPIData *data, void *obj, CMPIString *name,
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
