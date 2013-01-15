/*
 * $Id: control.h,v 1.4 2009/03/24 19:02:19 smswehla Exp $
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
 * Author:        Adrian Schuur <schuur@de.ibm.com>
 *
 * Description:
 *
 * sfcb.cfg config parser
 *
 */

#ifndef _CONTROL_
#define _CONTROL_

int             setupControl(char *fn);
void            sunsetControl();
int             getControlChars(char *id, char **val);
int             getControlUNum(char *id, unsigned int *val);
int             getControlULong(char *id, unsigned long *val);
int             getControlNum(char *id, long *val);
int             getControlBool(char *id, int *val);
const char      * sfcBrokerStart;

#endif
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
