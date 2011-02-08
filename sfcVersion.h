
/*
 * sfcVersion.h
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
 * Author:        Frank Scheffler
 * Contributions: Adrian Schuur <schuur@de.ibm.com>
 *
 * Description:
 *
 * Version definition.
 *
 */

#ifndef SFCVERSION_H
#define SFCVERSION_H

#ifdef HAVE_CONFIG_H
#include "config.h"

#define sfcBrokerVersion PACKAGE_VERSION
#define sfcHttpDaemonVersion PACKAGE_VERSION

#else
/*
 * this should never be used - but who knows 
 */
#define sfcBrokerVersion "0.8.1"
#define sfcHttpDaemonVersion "0.8.1"

#endif

#endif
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
