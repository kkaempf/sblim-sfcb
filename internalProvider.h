
/*
 * $Id: internalProvider.h,v 1.7 2009/05/08 00:04:33 mchasal Exp $
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
 * Author:        Viktor Mihajlovski <mihajlov@de.ibm.com>
 * Contributions: 
 *
 * Description:
 *
 * Header file for the internal provider external functions
 *
 */

#ifndef INTERNAL_PROVIDER_H
#define INTERNAL_PROVIDER_H

#include <sfcCommon/utilft.h>
#include "cmpi/cmpidt.h"

CMPIStatus      InternalProviderEnumInstanceNames(CMPIInstanceMI * mi,
                                                  const CMPIContext *ctx,
                                                  const CMPIResult *rslt,
                                                  const CMPIObjectPath *
                                                  ref);
CMPIStatus      InternalProviderEnumInstances(CMPIInstanceMI * mi,
                                              const CMPIContext *ctx,
                                              const CMPIResult *rslt,
                                              const CMPIObjectPath * ref,
                                              const char **properties);
CMPIInstance   *internalProviderGetInstance(const CMPIObjectPath * cop,
                                            CMPIStatus *rc);
CMPIStatus      InternalProviderCreateInstance(CMPIInstanceMI * mi,
                                               const CMPIContext *ctx,
                                               const CMPIResult *rslt,
                                               const CMPIObjectPath * cop,
                                               const CMPIInstance *ci);
CMPIStatus      InternalProviderModifyInstance(CMPIInstanceMI * mi,
                                               const CMPIContext *ctx,
                                               const CMPIResult *rslt,
                                               const CMPIObjectPath * cop,
                                               const CMPIInstance *ci,
                                               const char **properties);
CMPIStatus      InternalProviderGetInstance(CMPIInstanceMI * mi,
                                            const CMPIContext *ctx,
                                            const CMPIResult *rslt,
                                            const CMPIObjectPath * cop,
                                            const char **properties);
CMPIStatus      InternalProviderDeleteInstance(CMPIInstanceMI * mi,
                                               const CMPIContext *ctx,
                                               const CMPIResult *rslt,
                                               const CMPIObjectPath * cop);
CMPIStatus      InternalProviderAssociatorNames(CMPIAssociationMI * mi,
                                                const CMPIContext *ctx,
                                                const CMPIResult *rslt,
                                                const CMPIObjectPath * cop,
                                                const char *assocClass,
                                                const char *resultClass,
                                                const char *role,
                                                const char *resultRole);
CMPIStatus      InternalProviderAssociators(CMPIAssociationMI * mi,
                                            const CMPIContext *ctx,
                                            const CMPIResult *rslt,
                                            const CMPIObjectPath * cop,
                                            const char *assocClass,
                                            const char *resultClass,
                                            const char *role,
                                            const char *resultRole,
                                            const char **propertyList);
CMPIStatus      InternalProviderReferenceNames(CMPIAssociationMI * mi,
                                               const CMPIContext *ctx,
                                               const CMPIResult *rslt,
                                               const CMPIObjectPath * cop,
                                               const char *assocClass,
                                               const char *role);
CMPIStatus      InternalProviderReferences(CMPIAssociationMI * mi,
                                           const CMPIContext *ctx,
                                           const CMPIResult *rslt,
                                           const CMPIObjectPath * cop,
                                           const char *assocClass,
                                           const char *role,
                                           const char **propertyList);
extern char    *internalProviderNormalizeObjectPath(const CMPIObjectPath *
                                                    cop);

#endif
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
