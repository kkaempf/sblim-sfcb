
/*
 * interopServerProvider.c
 *
 * (C) Copyright IBM Corp. 2005, 2010, 2011
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
 *
 * InteropServer provider for sfcb
 *
 * Has implementation for the following classes:
 * 
 *   Namespace
 *   ObjectManager
 *   CIMXMLCommunicationMechanism
 *   IndicationService
 *   IndicationServiceCapabilities
 *   ServiceAffectsElement
 *   HostedService
 *   ElementConformsToProfile
 *
 */

#include <sfcCommon/utilft.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include "constClass.h"
#include "providerRegister.h"
#include "trace.h"
#include "native.h"
#include "control.h"
#include "config.h"
#include "objectpath.h"
#include "sfcbmacs.h"

#define NEW(x) ((x *) malloc(sizeof(x)))

#include "cmpi/cmpidt.h"
#include "cmpi/cmpift.h"
#include "cmpiftx.h"
#include "cmpi/cmpimacs.h"
#include "cmpimacsx.h"

static const CMPIBroker *_broker;
static CMPIStatus invClassSt = { CMPI_RC_ERR_INVALID_CLASS, NULL };
static CMPIInstance *ISinst; //Global instance for IndicationService

// ------------------------------------------------------------------

static char    *
getSfcbUuid()
{
  static char    *uuid = NULL;
  static char    *u = NULL;

  if (uuid == NULL) {
    FILE           *uuidFile;
    char           *fn =
        alloca(strlen(SFCB_STATEDIR) + strlen("/uuid") + 8);
    strcpy(fn, SFCB_STATEDIR);
    strcat(fn, "/uuid");
    uuidFile = fopen(fn, "r");
    if (uuidFile) {
      char            u[512];
      if (fgets(u, 512, uuidFile) != NULL) {
        int             l = strlen(u);
        if (l)
          u[l - 1] = 0;
        uuid = malloc(l + 32);
        strcpy(uuid, "sfcb:");
        strcat(uuid, u);
        fclose(uuidFile);
        return uuid;
      }
      fclose(uuidFile);
    } else if (u == NULL) {
      char            hostName[512];
      gethostname(hostName, 511);
      u = malloc(strlen(hostName) + 32);
      strcpy(u, "sfcb:NO-UUID-FILE-");
      strcat(u, hostName);
    }
    return u;
  }
  return uuid;
}

// ------------------------------------------------------------------

static int
genNameSpaceData(const char *ns, int dbl,
                 const CMPIResult *rslt, CMPIObjectPath * op,
                 CMPIInstance *ci)
{
  if (ci) {
    CMSetProperty(ci, "Name", ns + dbl + 1, CMPI_chars);
    CMReturnInstance(rslt, ci);
  } else if (op) {
    CMAddKey(op, "Name", ns + dbl + 1, CMPI_chars);
    CMReturnObjectPath(rslt, op);
  }
  return 0;
}

static void
gatherNameSpacesData(const char *dn, int dbl,
                     const CMPIResult *rslt,
                     CMPIObjectPath * op, CMPIInstance *ci)
{
  DIR            *dir,
                 *de_test;
  struct dirent  *de;
  char           *n;
  int             l;

  dir = opendir(dn);
  if (dir) {
    while ((de = readdir(dir)) != NULL) {
      if (strcmp(de->d_name, ".") == 0)
        continue;
      if (strcmp(de->d_name, "..") == 0)
        continue;
      l = strlen(dn) + strlen(de->d_name) + 4;
      n = malloc(l + 8);
      strcpy(n, dn);
      strcat(n, "/");
      strcat(n, de->d_name);
      de_test = opendir(n);
      if (de_test == NULL) {
        free(n);
        continue;
      }
      closedir(de_test);

      genNameSpaceData(n,dbl,rslt,op,ci);
      gatherNameSpacesData(n,dbl,rslt,op,ci);
      free(n);
    }
    closedir(dir);
  }
}

static CMPIStatus
NameSpaceProviderGetInstance(CMPIInstanceMI * mi,
                             const CMPIContext *ctx,
                             const CMPIResult *rslt,
                             const CMPIObjectPath * cop,
                             const char **properties)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  char           *dirn,
                 *dn,
                  hostName[512];
  DIR            *dir;
  CMPIObjectPath *op;
  CMPIInstance   *ci;
  CMPIString     *name;
  unsigned short  info = 0,
      dbl;

  _SFCB_ENTER(TRACE_PROVIDERS, "NameSpaceProviderGetInstance");

  if (getControlChars("registrationDir", &dirn)) {
    dirn = "/var/lib/sfcb/registration";
  }

  name = CMGetKey(cop, "name", NULL).value.string;

  if (name && name->hdl) {
    dn = alloca(strlen(dirn) + 32 + strlen((char *) name->hdl));
    strcpy(dn, dirn);
    if (dirn[strlen(dirn) - 1] != '/')
      strcat(dn, "/");
    strcat(dn, "repository/");
    dbl = strlen(dn);
    strcat(dn, (char *) name->hdl);

    if ((dir = opendir(dn)) != NULL) {
      op=CMNewObjectPath(_broker,"root/interop","CIM_Namespace",NULL);
      ci=CMNewInstance(_broker,op,NULL);
        
      CMSetProperty(ci,"CreationClassName","CIM_Namespace",CMPI_chars);
      CMSetProperty(ci,"ObjectManagerCreationClassName","CIM_ObjectManager",CMPI_chars);
      CMSetProperty(ci,"ObjectManagerName",getSfcbUuid(),CMPI_chars);
      CMSetProperty(ci,"SystemCreationClassName","CIM_ComputerSystem",CMPI_chars);
      hostName[0]=0;
      gethostname(hostName,511);
      CMSetProperty(ci,"SystemName",hostName,CMPI_chars);
      CMSetProperty(ci,"ClassInfo",&info,CMPI_uint16);
      CMSetProperty(ci,"Name",dn+dbl,CMPI_chars);
      CMReturnInstance(rslt,ci);
      closedir(dir);
    } else
      st.rc = CMPI_RC_ERR_NOT_FOUND;
  } else
    st.rc = CMPI_RC_ERR_INVALID_PARAMETER;

  _SFCB_RETURN(st);
}

static CMPIStatus
NameSpaceProviderEnumInstances(CMPIInstanceMI * mi,
                               const CMPIContext *ctx,
                               const CMPIResult *rslt,
                               const CMPIObjectPath * ref,
                               const char **properties)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  char           *dir,
                 *dn,
                  hostName[512];
  CMPIObjectPath *op;
  CMPIInstance   *ci;
  unsigned short  info = 0;

  _SFCB_ENTER(TRACE_PROVIDERS, "NameSpaceProviderEnumInstances");

  if (getControlChars("registrationDir", &dir)) {
    dir = "/var/lib/sfcb/registration";
  }

  dn = alloca(strlen(dir) + 32);
  strcpy(dn, dir);
  if (dir[strlen(dir) - 1] != '/')
    strcat(dn, "/");
  strcat(dn, "repository");

  op = CMNewObjectPath(_broker, "root/interop", "CIM_Namespace", NULL);
  ci = CMNewInstance(_broker, op, NULL);

  CMSetProperty(ci, "CreationClassName", "CIM_Namespace", CMPI_chars);
  CMSetProperty(ci, "ObjectManagerCreationClassName", "CIM_ObjectManager",
                CMPI_chars);
  CMSetProperty(ci, "ObjectManagerName", getSfcbUuid(), CMPI_chars);
  CMSetProperty(ci, "SystemCreationClassName", "CIM_ComputerSystem",
                CMPI_chars);
  hostName[0] = 0;
  gethostname(hostName, 511);
  CMSetProperty(ci, "SystemName", hostName, CMPI_chars);
  CMSetProperty(ci, "ClassInfo", &info, CMPI_uint16);

  gatherNameSpacesData(dn, strlen(dn), rslt, NULL, ci);

  _SFCB_RETURN(st);
}

static CMPIStatus
NameSpaceProviderEnumInstanceNames(CMPIInstanceMI * mi,
                                   const CMPIContext *ctx,
                                   const CMPIResult *rslt,
                                   const CMPIObjectPath * ref)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  char           *dir,
                 *dn,
                  hostName[512];
  CMPIObjectPath *op;

  _SFCB_ENTER(TRACE_PROVIDERS, "NameSpaceProviderEnumInstanceNames");

  if (getControlChars("registrationDir", &dir)) {
    dir = "/var/lib/sfcb/registration";
  }

  dn = alloca(strlen(dir) + 32);
  strcpy(dn, dir);
  if (dir[strlen(dir) - 1] != '/')
    strcat(dn, "/");
  strcat(dn, "repository");

  op = CMNewObjectPath(_broker, "root/interop", "CIM_Namespace", NULL);

  CMAddKey(op, "CreationClassName", "CIM_Namespace", CMPI_chars);
  CMAddKey(op, "ObjectManagerCreationClassName", "CIM_ObjectManager",
           CMPI_chars);
  CMAddKey(op, "ObjectManagerName", getSfcbUuid(), CMPI_chars);
  CMAddKey(op, "SystemCreationClassName", "CIM_ComputerSystem",
           CMPI_chars);
  hostName[0] = 0;
  gethostname(hostName, 511);
  CMAddKey(op, "SystemName", hostName, CMPI_chars);

  gatherNameSpacesData(dn, strlen(dn), rslt, op, NULL);

  _SFCB_RETURN(st);
}

/*
 * EnumInstanceNames for CIM_Service className Currently used for
 * CIM_ObjectManager, CIM_IndicationService 
 */

static CMPIStatus
ServiceProviderEnumInstanceNames(CMPIInstanceMI * mi,
                                 const CMPIContext *ctx,
                                 const CMPIResult *rslt,
                                 const CMPIObjectPath * ref,
                                 const char *className, const char *sccn)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  char            hostName[512];
  CMPIObjectPath *op;

  _SFCB_ENTER(TRACE_PROVIDERS, "ServiceProviderEnumInstanceNames");

  op = CMNewObjectPath(_broker, "root/interop", className, NULL);

  CMAddKey(op, "CreationClassName", className, CMPI_chars);
  CMAddKey(op, "SystemCreationClassName", sccn, CMPI_chars);
  hostName[0] = 0;
  gethostname(hostName, 511);
  CMAddKey(op, "SystemName", hostName, CMPI_chars);
  CMAddKey(op, "Name", getSfcbUuid(), CMPI_chars);

  CMReturnObjectPath(rslt, op);

  _SFCB_RETURN(st);
}

static CMPIInstance*
makeObjectManager()
{
  char            str[512];
  CMPIUint16      state;
  CMPIBoolean     bul = 0;
  CMPIObjectPath* op = CMNewObjectPath(_broker, "root/interop", 
                                       "CIM_ObjectManager", NULL);
  CMPIInstance* ci = CMNewInstance(_broker, op, NULL);

  CMSetProperty(ci, "CreationClassName", "CIM_ObjectManager", CMPI_chars);
  CMSetProperty(ci, "SystemCreationClassName", "CIM_ComputerSystem",
                CMPI_chars);
  str[0] = 0;
  gethostname(str, 511);
  CMSetProperty(ci, "SystemName", str, CMPI_chars);
  CMSetProperty(ci, "Name", getSfcbUuid(), CMPI_chars);
  CMSetProperty(ci, "GatherStatisticalData", &bul, CMPI_boolean);
  CMSetProperty(ci, "ElementName", "sfcb", CMPI_chars);
  CMSetProperty(ci, "Description", PACKAGE_STRING, CMPI_chars);
  state = 5;
  CMSetProperty(ci, "EnabledState", &state, CMPI_uint16);
  CMSetProperty(ci, "RequestedState", &state, CMPI_uint16);
  state = 2;
  CMSetProperty(ci, "EnabledDefault", &state, CMPI_uint16);
  return ci;
}

static CMPIStatus
ObjectManagerProviderEnumInstances(CMPIInstanceMI * mi,
                                   const CMPIContext *ctx,
                                   const CMPIResult *rslt,
                                   const CMPIObjectPath * ref,
                                   const char **properties)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };

  _SFCB_ENTER(TRACE_PROVIDERS, "ObjectManagerProviderEnumInstances");

  CMReturnInstance(rslt, makeObjectManager());

  _SFCB_RETURN(st);
}

static CMPIObjectPath* makeIndServiceOP() {

  CMPIStatus st = { CMPI_RC_OK, 0 };
  char str[512];
  CMPIObjectPath* op=CMNewObjectPath(_broker,"root/interop","CIM_IndicationService",&st);
  CMAddKey(op,"CreationClassName","CIM_IndicationService",CMPI_chars);
  CMAddKey(op,"SystemCreationClassName","CIM_ComputerSystem",CMPI_chars);
  str[0]=str[511]=0;
  gethostname(str,511);
  CMAddKey(op,"SystemName",str,CMPI_chars);
  CMAddKey(op,"Name",getSfcbUuid(),CMPI_chars);

  return op;
}

static CMPIStatus makeIndService(CMPIInstance *ci) 
{
  CMPIStatus st = { CMPI_RC_OK, 0 };

  CMPIBoolean filterCreation=1;
  CMPIUint16      retryAttempts,subRemoval;
  CMPIUint32      tmp,retryInterval,subRemovalInterval; 


  // Get the retry parameters from the config file
  getControlUNum("DeliveryRetryInterval", &retryInterval);
  getControlUNum("DeliveryRetryAttempts", &tmp);
  if (tmp > UINT16_MAX) {
    // Exceeded max range, set to default
    mlogf(M_ERROR, M_SHOW, "--- Value for DeliveryRetryAttempts exceeds range, using default.\n");
    retryAttempts=3;
  } else {
    retryAttempts=(CMPIUint16) tmp;
  }
  getControlUNum("SubscriptionRemovalTimeInterval", &subRemovalInterval);
  getControlUNum("SubscriptionRemovalAction", &tmp);
  if (tmp > UINT16_MAX) {
    // Exceeded max range, set to default
    mlogf(M_ERROR, M_SHOW, "--- Value for SubscriptionRemovalAction exceeds range, using default.\n");
    subRemoval=2;
  } else {
    subRemoval= (CMPIUint16) tmp;
  }

  CMSetProperty(ci,"CreationClassName","CIM_IndicationService",CMPI_chars);
  CMSetProperty(ci,"SystemCreationClassName","CIM_ComputerSystem",CMPI_chars);
  CMSetProperty(ci,"Name",getSfcbUuid(),CMPI_chars);
  CMSetProperty(ci,"FilterCreationEnabled",&filterCreation,CMPI_boolean);
  CMSetProperty(ci,"ElementName","sfcb",CMPI_chars);
  CMSetProperty(ci,"Description",PACKAGE_STRING,CMPI_chars);
  CMSetProperty(ci,"DeliveryRetryAttempts",&retryAttempts,CMPI_uint16);
  CMSetProperty(ci,"DeliveryRetryInterval",&retryInterval,CMPI_uint32);
  CMSetProperty(ci,"SubscriptionRemovalAction",&subRemoval,CMPI_uint16);
  CMSetProperty(ci,"SubscriptionRemovalTimeInterval",&subRemovalInterval,CMPI_uint32);

  return st;
}

static CMPIStatus
IndServiceProviderGetInstance(CMPIInstanceMI * mi,
                              const CMPIContext *ctx,
                              const CMPIResult *rslt,
                              const CMPIObjectPath * ref,
                              const char **properties)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIInstance* ci = NULL;

  _SFCB_ENTER(TRACE_PROVIDERS, "IndServiceProviderGetInstance");

  CMPIObjectPath* op = makeIndServiceOP();
  /* compare object paths to see if we're being asked for the one we have 
     if ref is NULL, then skip it, because we're haning an ei call */   
  if ((ref !=NULL) && objectpathCompare(op, ref)) {
    st.rc=CMPI_RC_ERR_NOT_FOUND;
    _SFCB_RETURN(st);
  }

  ci = ISinst->ft->clone(ISinst, &st);
  memLinkInstance(ci);
  if (properties) {
    ci->ft->setPropertyFilter(ci, properties, NULL);
  }
  CMReturnInstance(rslt,ci);
  CMReturnDone(rslt);

  _SFCB_RETURN(st);

}

static CMPIStatus
IndServiceProviderEnumInstances(CMPIInstanceMI * mi,
                                const CMPIContext *ctx,
                                const CMPIResult *rslt,
                                const CMPIObjectPath * ref,
                                const char **properties)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };

  _SFCB_ENTER(TRACE_PROVIDERS, "IndServiceProviderEnumInstances");

  CMReturnInstance(rslt, ISinst);
  CMReturnDone(rslt);
  _SFCB_RETURN(st);
}

static CMPIStatus
IndServiceCapabilitiesProviderEnumInstanceNames(CMPIInstanceMI * mi,
                                                const CMPIContext *ctx,
                                                const CMPIResult *rslt,
                                                const CMPIObjectPath * ref)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIObjectPath *op;

  _SFCB_ENTER(TRACE_PROVIDERS,
              "IndServiceCapabilitiesProviderEnumInstanceNames");

  op = CMNewObjectPath(_broker, "root/interop",
                       "SFCB_IndicationServiceCapabilities", NULL);
  CMAddKey(op, "InstanceID", "CIM:SFCB_ISC", CMPI_chars);
  CMReturnObjectPath(rslt, op);
  CMReturnDone(rslt);

  _SFCB_RETURN(st);
}

static CMPIStatus
IndServiceCapabilitiesProviderEnumInstances(CMPIInstanceMI * mi,
                                            const CMPIContext *ctx,
                                            const CMPIResult *rslt,
                                            const CMPIObjectPath * ref,
                                            const char **properties)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIObjectPath *op;
  CMPIInstance   *ci = NULL;
  long ctl=0;

  _SFCB_ENTER(TRACE_PROVIDERS,
              "IndServiceCapabilitiesProviderEnumInstances");

  CMPIContext    *ctxLocal;
  ctxLocal = native_clone_CMPIContext(ctx);
  CMPIValue       val;
  val.string = sfcb_native_new_CMPIString("$DefaultProvider$", NULL, 0);
  ctxLocal->ft->addEntry(ctxLocal, "rerouteToProvider", &val, CMPI_string);

  op = CMNewObjectPath(_broker, "root/interop",
                       "SFCB_IndicationServiceCapabilities", NULL);
  CMAddKey(op, "InstanceID", "CIM:SFCB_ISC", CMPI_chars);
  ci = CBGetInstance(_broker, ctxLocal, op, properties, &st);

  // Get the config options for these max vals and add them to the instance
  getControlNum("MaxListenerDestinations", &ctl);
  CMPIValue max = {.uint32 = ctl };
  CMSetProperty(ci,"MaxListenerDestinations",&max,CMPI_uint32);
  
  getControlNum("MaxActiveSubscriptions", &ctl);
  max.uint32 = ctl;
  CMSetProperty(ci,"MaxActiveSubscriptions",&max,CMPI_uint32);

  CMReturnInstance(rslt, ci);
  CMReturnDone(rslt);

  if (ctxLocal)
    CMRelease(ctxLocal);
  _SFCB_RETURN(st);
}

// ---------------------------------------------------------------

static CMPIStatus
ComMechProviderEnumInstanceNames(CMPIInstanceMI * mi,
                                 const CMPIContext *ctx,
                                 const CMPIResult *rslt,
                                 const CMPIObjectPath * ref)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  char            hostName[512];
  CMPIObjectPath *op;

  _SFCB_ENTER(TRACE_PROVIDERS, "ComMechProviderEnumInstanceNames");

  op = CMNewObjectPath(_broker, "root/interop",
                       "SFCB_CIMXMLCommunicationMechanism", NULL);

  CMAddKey(op, "SystemCreationClassName", "CIM_ObjectManager", CMPI_chars);
  CMAddKey(op, "CreationClassName",
           "SFCB_CIMXMLCommunicationMechanism", CMPI_chars);
  hostName[0] = 0;
  gethostname(hostName, 511);
  CMAddKey(op, "SystemName", hostName, CMPI_chars);
  CMAddKey(op, "Name", getSfcbUuid(), CMPI_chars);

  CMReturnObjectPath(rslt, op);

  _SFCB_RETURN(st);
}

static CMPIStatus
ComMechProviderEnumInstances(CMPIInstanceMI * mi,
                             const CMPIContext *ctx,
                             const CMPIResult *rslt,
                             const CMPIObjectPath * ref,
                             const char **properties)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  char            hostName[512];
  CMPIObjectPath *op;
  CMPIInstance   *ci;
  CMPIUint16      mech;
  CMPIBoolean     bul = 0;
  unsigned int    i;

  CMPIArray      *fps;
  CMPIUint16      fpa[6] = { 2, 3, 5, 6, 7, 9 };
  CMPIArray      *as;
  CMPIUint16      aa[1] = { 3 };

  _SFCB_ENTER(TRACE_PROVIDERS, "ComMechProviderEnumInstanceNames");

  op = CMNewObjectPath(_broker, "root/interop",
                       "SFCB_CIMXMLCommunicationMechanism", NULL);
  ci = CMNewInstance(_broker, op, NULL);

  CMSetProperty(ci, "SystemCreationClassName", "CIM_ObjectManager",
                CMPI_chars);
  CMSetProperty(ci, "CreationClassName",
                "SFCB_CIMXMLCommunicationMechanism", CMPI_chars);
  hostName[0] = 0;
  gethostname(hostName, 511);
  CMSetProperty(ci, "SystemName", hostName, CMPI_chars);
  CMSetProperty(ci, "Name", getSfcbUuid(), CMPI_chars);
  /*
   * Version of CIM-XML that is supported 
   */
  CMSetProperty(ci, "Version", "1.0", CMPI_chars);

  mech = 2;
  CMSetProperty(ci, "CommunicationMechanism", &mech, CMPI_uint16);

  fps =
      CMNewArray(_broker, sizeof(fpa) / sizeof(CMPIUint16), CMPI_uint16,
                 NULL);
  for (i = 0; i < sizeof(fpa) / sizeof(CMPIUint16); i++)
    CMSetArrayElementAt(fps, i, &fpa[i], CMPI_uint16);
  CMSetProperty(ci, "FunctionalProfilesSupported", &fps, CMPI_uint16A);

  as = CMNewArray(_broker, sizeof(aa) / sizeof(CMPIUint16), CMPI_uint16,
                  NULL);
  for (i = 0; i < sizeof(aa) / sizeof(CMPIUint16); i++)
    CMSetArrayElementAt(as, i, &aa[i], CMPI_uint16);
  CMSetProperty(ci, "AuthenticationMechanismsSupported", &as,
                CMPI_uint16A);

  CMSetProperty(ci, "MultipleOperationsSupported", &bul, CMPI_boolean);

  CMSetProperty(ci, "CIMValidated", &bul, CMPI_boolean);

  CMReturnInstance(rslt, ci);

  _SFCB_RETURN(st);
}

static CMPIStatus
ServiceProviderGetInstance(CMPIInstanceMI * mi,
                           const CMPIContext *ctx,
                           const CMPIResult *rslt,
                           const CMPIObjectPath * ref,
                           const char **properties, const char *className)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIString     *name = CMGetKey(ref, "name", NULL).value.string;

  _SFCB_ENTER(TRACE_PROVIDERS, "ServiceProviderGetInstance");

  if (name && name->hdl) {
    if (strcasecmp((char *) name->hdl, getSfcbUuid()) == 0) {
      if (strcasecmp(className, "cim_objectmanager") == 0)
        return ObjectManagerProviderEnumInstances(mi, ctx, rslt, ref,
                                                  properties);
      if (strcasecmp(className, "sfcb_cimxmlcommunicationMechanism")
          == 0)
        return ComMechProviderEnumInstances(mi, ctx, rslt, ref,
                                            properties);
      if (strcasecmp(className, "cim_indicationservice") == 0)
        return IndServiceProviderGetInstance(mi, ctx, rslt, ref,
                                             properties);
    }

    else
      st.rc = CMPI_RC_ERR_NOT_FOUND;
  } else
    st.rc = CMPI_RC_ERR_INVALID_PARAMETER;

  _SFCB_RETURN(st);
}

// ---------------------------------------------------------------

static CMPIStatus
ServerProviderCleanup(CMPIInstanceMI * mi, const CMPIContext *ctx,
                      CMPIBoolean terminate)
{
  CMRelease(ISinst);
  return okSt;
}

static CMPIStatus
ServerProviderGetInstance(CMPIInstanceMI * mi,
                          const CMPIContext *ctx,
                          const CMPIResult *rslt,
                          const CMPIObjectPath * ref,
                          const char **properties)
{
  CMPIString     *cls = CMGetClassName(ref, NULL);

  if (strcasecmp((char *) cls->hdl, "cim_namespace") == 0)
    return NameSpaceProviderGetInstance(mi, ctx, rslt, ref, properties);
  if (strcasecmp((char *) cls->hdl, "cim_objectmanager") == 0)
    return ServiceProviderGetInstance(mi, ctx, rslt, ref, properties,
                                      "cim_objectmanager");
  if (strcasecmp
      ((char *) cls->hdl, "sfcb_cimxmlcommunicationMechanism") == 0)
    return ServiceProviderGetInstance(mi, ctx, rslt, ref, properties,
                                      "sfcb_cimxmlcommunicationMechanism");
  if (strcasecmp((char *) cls->hdl, "cim_indicationservice") == 0)
    return ServiceProviderGetInstance(mi, ctx, rslt, ref, properties,
                                      "cim_indicationservice");
  if (CMClassPathIsA
      (_broker, ref, "CIM_IndicationServiceCapabilities", NULL))
    return IndServiceCapabilitiesProviderEnumInstances(mi, ctx, rslt, ref,
                                                       properties);

  return invClassSt;
}

static CMPIStatus
ServerProviderEnumInstanceNames(CMPIInstanceMI * mi,
                                const CMPIContext *ctx,
                                const CMPIResult *rslt,
                                const CMPIObjectPath * ref)
{
  CMPIString     *cls = CMGetClassName(ref, NULL);

  if (strcasecmp((char *) cls->hdl, "cim_namespace") == 0)
    return NameSpaceProviderEnumInstanceNames(mi, ctx, rslt, ref);
  if (strcasecmp((char *) cls->hdl, "cim_objectmanager") == 0)
    return ServiceProviderEnumInstanceNames(mi, ctx, rslt, ref,
                                            "CIM_ObjectManager",
                                            "CIM_ComputerSystem");
  if (strcasecmp
      ((char *) cls->hdl, "sfcb_cimxmlcommunicationMechanism") == 0)
    return ComMechProviderEnumInstanceNames(mi, ctx, rslt, ref);
  if (strcasecmp((char *) cls->hdl, "cim_indicationservice") == 0)
    return ServiceProviderEnumInstanceNames(mi, ctx, rslt, ref,
                                            "CIM_IndicationService",
                                            "CIM_ComputerSystem");
  if (CMClassPathIsA
      (_broker, ref, "CIM_IndicationServiceCapabilities", NULL))
    return IndServiceCapabilitiesProviderEnumInstanceNames(mi, ctx, rslt,
                                                           ref);

  return okSt;
}

static CMPIStatus
ServerProviderEnumInstances(CMPIInstanceMI * mi,
                            const CMPIContext *ctx,
                            const CMPIResult *rslt,
                            const CMPIObjectPath * ref,
                            const char **properties)
{
  CMPIString     *cls = CMGetClassName(ref, NULL);

  if (strcasecmp((char *) cls->hdl, "cim_namespace") == 0)
    return NameSpaceProviderEnumInstances(mi, ctx, rslt, ref, properties);
  if (strcasecmp((char *) cls->hdl, "cim_objectmanager") == 0)
    return ObjectManagerProviderEnumInstances(mi, ctx, rslt, ref,
                                              properties);
  if (strcasecmp
      ((char *) cls->hdl, "sfcb_cimxmlcommunicationMechanism") == 0)
    return ComMechProviderEnumInstances(mi, ctx, rslt, ref, properties);
  if (strcasecmp((char *) cls->hdl, "cim_interopservice") == 0) /* do we * 
                                                                 * still * 
                                                                 * need *
                                                                 * this? */
    return ComMechProviderEnumInstances(mi, ctx, rslt, ref, properties);
  if (strcasecmp((char *) cls->hdl, "cim_indicationservice") == 0)
    return IndServiceProviderEnumInstances(mi, ctx, rslt, ref, properties);
  if (CMClassPathIsA
      (_broker, ref, "cim_indicationservicecapabilities", NULL))
    return IndServiceCapabilitiesProviderEnumInstances(mi, ctx, rslt, ref,
                                                       properties);

  return okSt;
}

/* ServerProviderCreateInstance */
static CMPIStatus notSupCMPI_CI(ServerProvider);

static CMPIStatus
ServerProviderModifyInstance(CMPIInstanceMI * mi,
                             const CMPIContext *ctx,
                             const CMPIResult *rslt,
                             const CMPIObjectPath * cop,
                             const CMPIInstance *ci,
                             const char **properties)
{
  CMPIStatus      rc = notSupSt;

  if (!CMClassPathIsA(_broker, cop, "cim_indicationservice", NULL))
    return rc;
  // Check that we have the right OP for IndicationService
  if ( objectpathCompare(cop,ISinst->ft->getObjectPath(ISinst,NULL))) 
    return notFoundSt;

  // Get the settable props
  int settable=0;
  CMPIObjectPath *iscop = CMNewObjectPath(_broker, "root/interop", "SFCB_IndicationServiceCapabilities",NULL);
  CMPIEnumeration *iscenm = _broker->bft->enumerateInstances(_broker, ctx, iscop, NULL, NULL);
  CMPIData iscinst = CMGetNext(iscenm, NULL);
  CMPIData iscprop = CMGetProperty(iscinst.value.inst, "DeliveryRetryAttemptsIsSettable", NULL);
  if (!iscprop.value.boolean) settable++;
  iscprop = CMGetProperty(iscinst.value.inst, "DeliveryRetryIntervalIsSettable", NULL);
  if (!iscprop.value.boolean) settable++;
  iscprop = CMGetProperty(iscinst.value.inst, "SubscriptionRemovalActionIsSettable", NULL);
  if (!iscprop.value.boolean) settable++;
  iscprop = CMGetProperty(iscinst.value.inst, "SubscriptionRemovalTimeIntervalIsSettable", NULL);
  if (!iscprop.value.boolean) settable++;

  if (iscop) CMRelease(iscop);
  if (iscenm) CMRelease(iscenm);

  // If any of the settable props were false, no MI of indication service allowed
  // We're assuming that this is the only reason for doing an MI
  if (settable > 0) return rc;

  // Ok, all is well to continue with the operation...
  // Just replace the global instance with the new one.
  ISinst=ci->ft->clone(ci,NULL);
  CMReturnInstance(rslt, ci);
  return okSt;
} 

/* ServerProviderDeleteInstance */
static CMPIStatus notSupCMPI_DI(ServerProvider);

/* ServerProviderExecQuery */
static CMPIStatus notSupCMPI_EQ(ServerProvider);

void
ServerProviderInitInstances(const CMPIContext *ctx)
{

  CMPIStatus st;
  CMPIObjectPath *ISop = makeIndServiceOP();
  // This instance is always recreated, changes don't persist
  ISinst = CMNewInstance(_broker, ISop, &st);
  makeIndService(ISinst);
  memUnlinkInstance(ISinst); // Prevent cleanup of the instance
  return;
}

CMInstanceMIStub(ServerProvider, ServerProvider, _broker,
                 ServerProviderInitInstances(ctx));

/*---------------------- Association interface --------------------------*/

/* ServerProviderAssociationCleanup */
static CMPIStatus okCleanup(ServerProvider,Association);

/** \brief buildAssoc - Builds the Association instances
 *
 *  buildAssoc returns a set of instances represented 
 *  by op that is passed in. 
 *  The propertyList is used as a filter on the results
 *  and the target determines if names or instances should
 *  be returned.
 */

CMPIStatus
buildAssoc(const CMPIContext *ctx,
           const CMPIResult *rslt,
           const CMPIObjectPath * op,
           const char **propertyList, const char *target)
{
  CMPIEnumeration *enm = NULL;
  CMPIStatus      rc = { CMPI_RC_OK, NULL };

  if (strcasecmp(target, "AssocNames") == 0) {
    enm = _broker->bft->enumerateInstanceNames(_broker, ctx, op, &rc);
    while (enm && enm->ft->hasNext(enm, &rc)) {
      CMReturnObjectPath(rslt, (enm->ft->getNext(enm, &rc).value.ref));
    }
  } else if (strcasecmp(target, "Assocs") == 0) {
    enm = _broker->bft->enumerateInstances(_broker, ctx, op, NULL, &rc);
    while (enm && enm->ft->hasNext(enm, &rc)) {
      CMPIData        inst = CMGetNext(enm, &rc);
      if (propertyList) {
        CMSetPropertyFilter(inst.value.inst, propertyList, NULL);
      }
      CMReturnInstance(rslt, (inst.value.inst));
    }
  }
  if (enm)
    CMRelease(enm);
  CMReturnDone(rslt);
  CMReturn(CMPI_RC_OK);
}

/** \brief buildRefs - Builds the Reference instances
 *
 *  buildAssoc returns a set of instances of the
 *  SFCB_ServiceAffectsElement class that associate
 *  the objects represented by op to the IndicationService.
 *  isop is an objectPath pointer to the IndicationService,
 *  saeop is an objectPath pointer to SFCB_ServiceAffectsElement.
 *  The propertyList is used as a filter on the results
 *  and the target determines if names or instances should
 *  be returned.
 */
CMPIStatus
buildRefs(const CMPIContext *ctx,
          const CMPIResult *rslt,
          const CMPIObjectPath * op,
          const CMPIObjectPath * isop,
          const CMPIObjectPath * saeop,
          const char **propertyList, const char *target)
{
  CMPIEnumeration *enm = NULL;
  CMPIEnumeration *isenm = NULL;
  CMPIStatus      rc = { CMPI_RC_OK, NULL };
  CMPIStatus      rc2 = { CMPI_RC_OK, NULL };
  CMPIInstance   *ci;

  // Get the single instance of IndicationService
  isenm = _broker->bft->enumerateInstanceNames(_broker, ctx, isop, &rc);
  CMPIData        isinst = CMGetNext(isenm, &rc);
  // Create an instance of SAE
  ci = CMNewInstance(_broker, saeop, &rc2);
  CMSetProperty(ci, "AffectingElement", &(isinst.value.ref), CMPI_ref);

  if (CMGetKeyCount(op, NULL) == 0) {
    enm = _broker->bft->enumerateInstanceNames(_broker, ctx, op, &rc);
    while (enm && CMHasNext(enm, &rc)) {
      CMPIData        inst = CMGetNext(enm, &rc);
      CMSetProperty(ci, "AffectedElement", &(inst.value.ref), CMPI_ref);
      if (strcasecmp(target, "Refs") == 0) {
        if (propertyList) {
          CMSetPropertyFilter(ci, propertyList, NULL);
        }
        CMReturnInstance(rslt, ci);
      } else {
        CMReturnObjectPath(rslt, CMGetObjectPath(ci, NULL));
      }
    }
  } else {
    CMSetProperty(ci, "AffectedElement", &(op), CMPI_ref);
    if (strcasecmp(target, "Refs") == 0) {
      if (propertyList) {
        CMSetPropertyFilter(ci, propertyList, NULL);
      }
      CMReturnInstance(rslt, ci);
    } else {
      CMReturnObjectPath(rslt, CMGetObjectPath(ci, NULL));
    }
  }

  if (ci)
    CMRelease(ci);
  if (enm)
    CMRelease(enm);
  if (isenm)
    CMRelease(isenm);
  CMReturnDone(rslt);
  CMReturn(CMPI_RC_OK);
}

/** \brief buildObj - Builds the Association or Reference instances
 *
 *  buildObj calls buildAssoc or buildRefs as required.
 *  op is the target objectPath.
 *  isop is an objectPath pointer to the IndicationService,
 *  saeop is an objectPath pointer to SFCB_ServiceAffectsElement.
 *  The propertyList is used as a filter on the results
 *  and the target determines if names or instances should
 *  be returned and whether association or references.
 */
CMPIStatus
buildObj(const CMPIContext *ctx,
         const CMPIResult *rslt,
         const char *resultClass,
         const CMPIObjectPath * op,
         const CMPIObjectPath * isop,
         const CMPIObjectPath * saeop,
         const char **propertyList, const char *target)
{
  CMPIStatus      rc = { CMPI_RC_OK, NULL };

  if (((strcasecmp(target, "Assocs") == 0)
       || (strcasecmp(target, "AssocNames") == 0))
      && ((resultClass == NULL)
          || (CMClassPathIsA(_broker, op, resultClass, &rc) == 1))) {
    // Association was requested
    buildAssoc(ctx, rslt, op, propertyList, target);
  } else if (((strcasecmp(target, "Refs") == 0)
              || (strcasecmp(target, "RefNames") == 0))
             && ((resultClass == NULL)
                 || (CMClassPathIsA(_broker, saeop, resultClass, &rc) ==
                     1))) {
    // Reference was requested
    buildRefs(ctx, rslt, op, isop, saeop, propertyList, target);
  }
  CMReturnDone(rslt);
  CMReturn(CMPI_RC_OK);
}

/** \brief makeCIM_System - Builds a CIM_System instance
 *  Creates an instance for a (dummy) CIM_System
*/
CMPIStatus
makeCIM_System(CMPIInstance *csi)
{
  CMSetProperty(csi, "CreationClassName", "CIM_System", CMPI_chars);
  CMSetProperty(csi, "Name", getSfcbUuid(), CMPI_chars);
  CMReturn(CMPI_RC_OK);
}

/** \brief makeHostedService - Builds a CIM_HostedDependency instance:
 *  
 *  CIM_HostedService 
 *  CIM_NamespaceInManager
 *
 *  Creates and returns the instance (or name) of a CIM_HostedService
 *  association between CIM_System and CIM_IndicationService
*/
CMPIStatus
makeHostedService(CMPIAssociationMI * mi,
                  const CMPIContext *ctx,
                  const CMPIResult *rslt,
                  const CMPIObjectPath * dop, /* op of dependent */
                  const CMPIObjectPath * hdop, /* op of assoc class */
                  const CMPIObjectPath * aop, /* op of antecedent */
                  const char **propertyList, const char *target)
{
  CMPIEnumeration *denm = NULL;
  CMPIStatus      rc = { CMPI_RC_OK, NULL };
  CMPIInstance   *hdi,
                 *anti;

  /* make an instance for the antecedent 
     (System or ObjectManager) */
  CMPIString* acn = CMGetClassName(aop, NULL);
  if (strcasecmp(CMGetCharPtr(acn), "CIM_System") == 0) {
    anti = CMNewInstance(_broker, aop, &rc);
    makeCIM_System(anti);
  }
  else if (strcasecmp(CMGetCharPtr(acn), "CIM_ObjectManager") == 0) {
    anti = makeObjectManager();
  }
  else {  /* should never happen */
    rc.rc = CMPI_RC_ERR_FAILED;
    return rc;
  }

  /* Get the SINGLE instance of the dependent 
     (IndicationService or Namespace) */
  CMPIString* dcn = CMGetClassName(dop, NULL);
  CMPIValue depop;
  if (strcasecmp(CMGetCharPtr(dcn), "CIM_IndicationService") == 0) {
    denm = _broker->bft->enumerateInstanceNames(_broker, ctx, dop, &rc);
    CMPIData        dinst = CMGetNext(denm, &rc);
    depop = dinst.value;
  }
  else if (strcasecmp(CMGetCharPtr(dcn), "CIM_Namespace") == 0) {
    depop.ref = (CMPIObjectPath*)dop;
  }
  else {  /* should never happen */
    rc.rc = CMPI_RC_ERR_FAILED;
    return rc;
  }

  /* Create an instance of the assoc class */
  hdi = CMNewInstance(_broker, hdop, &rc);
  CMPIValue       antiop;
  antiop.ref = CMGetObjectPath(anti, NULL);

  CMSetProperty(hdi, "Dependent", &(depop), CMPI_ref);
  CMSetProperty(hdi, "Antecedent", &(antiop), CMPI_ref);
  if (strcasecmp(target, "Refs") == 0) {
    if (propertyList) {
      CMSetPropertyFilter(hdi, propertyList, NULL);
    }
    CMReturnInstance(rslt, hdi);
  } else {
    CMReturnObjectPath(rslt, CMGetObjectPath(hdi, NULL));
  }
  if (anti)
    CMRelease(anti);
  if (hdi)
    CMRelease(hdi);
  if (denm)
    CMRelease(denm);
  CMReturnDone(rslt);
  CMReturn(CMPI_RC_OK);
}

/** \brief makeElementConforms - Builds a SFCB_ElementConformsToProfile instance
 *  
 *  Creates and returns the instance (or name) of a SFCB_ElementConformsToProfile
 *  association between SFCB_RegisteredProfile and CIM_IndicationService
*/
CMPIStatus
makeElementConforms(CMPIAssociationMI * mi,
                    const CMPIContext *ctx,
                    const CMPIResult *rslt,
                    const CMPIObjectPath * isop,
                    const CMPIObjectPath * ecop,
                    CMPIObjectPath * rpop,
                    const char **propertyList, const char *target)
{
  CMPIStatus      rc = { CMPI_RC_OK, NULL };
  CMPIInstance   *eci = NULL;

  CMAddKey(rpop, "InstanceID", "CIM:SFCB_IP", CMPI_chars);
  // Create an instance 
  eci = CMNewInstance(_broker, ecop, &rc);
  CMPIValue ISval = {.ref = ISinst->ft->getObjectPath(ISinst,NULL) };
  CMSetProperty(eci, "ManagedElement", &(ISval), CMPI_ref);
  CMSetProperty(eci, "ConformantStandard", &(rpop), CMPI_ref);
  if (strcasecmp(target, "Refs") == 0) {
    if (propertyList) {
      CMSetPropertyFilter(eci, propertyList, NULL);
    }
    CMReturnInstance(rslt, eci);
  } else {
    CMReturnObjectPath(rslt, CMGetObjectPath(eci, NULL));
  }
  if (eci)
    CMRelease(eci);
  CMReturnDone(rslt);
  CMReturn(CMPI_RC_OK);
}

static CMPIStatus handleAssocSAE(CMPIAssociationMI* mi, 
					   const CMPIContext* ctx, 
					   const CMPIResult* rslt, 
					   const CMPIObjectPath* cop, 
					   const char* resultClass, 
					   const char* role, 
					   const char* resultRole, 
					   const char** propertyList, 
					   const char* target, 
					   CMPIObjectPath* saeop) 
{
  CMPIStatus      rc = { CMPI_RC_OK, NULL };

    // Get pointers to all the interesting classes.
    CMPIObjectPath* ldop =
        CMNewObjectPath(_broker, CMGetCharPtr(CMGetNameSpace(cop, &rc)),
                        "CIM_listenerdestination", &rc);
    CMPIObjectPath* ifop =
        CMNewObjectPath(_broker, CMGetCharPtr(CMGetNameSpace(cop, &rc)),
                        "CIM_indicationfilter", &rc);
    CMPIObjectPath* isop =
        CMNewObjectPath(_broker, CMGetCharPtr(CMGetNameSpace(cop, &rc)),
                        "CIM_indicationservice", &rc);
    if ((ldop == NULL) || (ifop == NULL) || (isop == NULL)) {
      CMSetStatusWithChars(_broker, &rc, CMPI_RC_ERR_FAILED,
                           "Create CMPIObjectPath failed.");
      return rc;
    }

    if ((role == NULL || (strcasecmp(role, "affectingelement") == 0))
        && (resultRole == NULL
            || (strcasecmp(resultRole, "affectedelement") == 0))
        && CMClassPathIsA(_broker, cop, "cim_indicationservice", &rc) == 1) {
      // We were given an IndicationService, so we need to return 
      // IndicationFilters and ListenerDestinations
      // Get IndicationFilters
      buildObj(ctx, rslt, resultClass, ifop, isop, saeop, propertyList,
               target);
      // Get ListenerDestinations
      buildObj(ctx, rslt, resultClass, ldop, isop, saeop, propertyList,
               target);
    }
    if ((role == NULL || strcasecmp(role, "affectedelement") == 0)
        && (resultRole == NULL
            || strcasecmp(resultRole, "affectingelement") == 0)
        &&
        ((CMClassPathIsA(_broker, cop, "cim_indicationfilter", &rc) == 1)
         || (CMClassPathIsA(_broker, cop, "cim_listenerdestination", &rc)
             == 1))) {
      // We were given either an IndicationFilter, or a
      // ListenerDestination,
      if ((strcasecmp(target, "Assocs") == 0)
          || (strcasecmp(target, "AssocNames") == 0)) {
        // Here we need the IndicationService only
        buildObj(ctx, rslt, resultClass, isop, isop, saeop, propertyList,
                 target);
      } else {
        // Here we need the refs for the given object
        buildObj(ctx, rslt, resultClass, cop, isop, saeop, propertyList,
                 target);
      }
    }
    return rc;
}

static CMPIStatus handleAssocHostedService(CMPIAssociationMI* mi, 
					   const CMPIContext* ctx, 
					   const CMPIResult* rslt, 
					   const CMPIObjectPath* cop, 
					   const char* resultClass, 
					   const char* role, 
					   const char* resultRole, 
					   const char** propertyList, 
					   const char* target, 
					   CMPIObjectPath* hsop) 
{
    CMPIStatus      rc = { CMPI_RC_OK, NULL };

    CMPIObjectPath* isop =
        CMNewObjectPath(_broker, CMGetCharPtr(CMGetNameSpace(cop, &rc)),
                        "CIM_indicationservice", &rc);
    CMPIObjectPath* csop = CMNewObjectPath(_broker, "root/cimv2", "CIM_System", &rc);
    if ((csop == NULL) || (isop == NULL)) {
      CMSetStatusWithChars(_broker, &rc, CMPI_RC_ERR_FAILED,
                           "Create CMPIObjectPath failed.");
      return rc;
    }

    if ((role == NULL || (strcasecmp(role, "dependent") == 0))
        && (resultRole == NULL
            || (strcasecmp(resultRole, "antecedent") == 0))
        && CMClassPathIsA(_broker, cop, "cim_indicationservice", &rc) == 1) {
      // An IndicationService was passed in, so we need to return either
      // the 
      // CIM_System instance or a CIM_HostedService association instance
      if (((strcasecmp(target, "Assocs") == 0)
           || (strcasecmp(target, "AssocNames") == 0))
          && (resultClass == NULL
              || (strcasecmp(resultClass, "CIM_System") == 0))) {
        // Return the CIM_System instance
        CMPIInstance* cci = CMNewInstance(_broker, csop, &rc);
        makeCIM_System(cci);
        if (strcasecmp(target, "Assocs") == 0) {
          if (propertyList) {
            CMSetPropertyFilter(cci, propertyList, NULL);
          }
          CMReturnInstance(rslt, cci);
        } else {
          CMReturnObjectPath(rslt, CMGetObjectPath(cci, NULL));
        }
        if (cci)
          CMRelease(cci);

      } else if (resultClass == NULL
                 || (strcasecmp(resultClass, "CIM_HostedService") == 0)) {
        // Return the CIM_HostedService instance
        makeHostedService(mi, ctx, rslt, isop, hsop, csop, propertyList,
                          target);
      } 

    }
    if ((role == NULL || strcasecmp(role, "antecedent") == 0)
        && (resultRole == NULL || strcasecmp(resultRole, "dependent") == 0)
        && (CMClassPathIsA(_broker, cop, "cim_system", &rc) == 1)) {
      // A CIM_System was passed in so wee need to return either the
      // CIM_IndicationService instance or a CIM_HostedService association 
      // instance
      if (((strcasecmp(target, "Assocs") == 0)
           || (strcasecmp(target, "AssocNames") == 0))
          && (resultClass == NULL
              || (strcasecmp(resultClass, "CIM_IndicationService") ==
                  0))) {
        // Return the CIM_IndicationService instance
        CMPIInstance *inst = ISinst->ft->clone(ISinst,NULL);
        memLinkInstance(inst);
        if (strcasecmp(target, "Assocs") == 0) {
          if (propertyList) {
            CMSetPropertyFilter(inst, propertyList, NULL);
          }
          CMReturnInstance(rslt,inst);
        } else {
          CMReturnObjectPath(rslt, CMGetObjectPath(inst, NULL));
        }
      } else if (resultClass == NULL
                 || (strcasecmp(resultClass, "CIM_HostedService") == 0)) {
        // Return the CIM_HostedService instance
        makeHostedService(mi, ctx, rslt, isop, hsop, csop, propertyList,
                          target);
      }
    }
    return rc;
}

static CMPIStatus handleAssocNIM(CMPIAssociationMI* mi, 
                                           const CMPIContext* ctx, 
                                           const CMPIResult* rslt, 
                                           const CMPIObjectPath* cop, 
                                           const char* resultClass, 
                                           const char* role, 
                                           const char* resultRole, 
                                           const char** propertyList, 
                                           const char* target, 
                                           CMPIObjectPath* nimop) 
{
    CMPIStatus      rc = { CMPI_RC_OK, NULL };

    CMPIObjectPath* nsop =
        CMNewObjectPath(_broker, CMGetCharPtr(CMGetNameSpace(cop, &rc)),
                        "CIM_Namespace", &rc);
    CMPIObjectPath* omop = CMNewObjectPath(_broker, "root/interop", "CIM_ObjectManager", &rc);
    if ((nsop == NULL) || (omop == NULL)) {
      CMSetStatusWithChars(_broker, &rc, CMPI_RC_ERR_FAILED,
                           "Create CMPIObjectPath failed.");
      return rc;
    }
    
    /* A Namespace was passed in, so we need to return either
       the ObjectManager instance or an association instance */
    if ((role == NULL || (strcasecmp(role, "dependent") == 0))
        && (resultRole == NULL
            || (strcasecmp(resultRole, "antecedent") == 0))
        && CMClassPathIsA(_broker, cop, "cim_namespace", &rc) == 1) {
      if (((strcasecmp(target, "Assocs") == 0)
           || (strcasecmp(target, "AssocNames") == 0))
          && (resultClass == NULL
              || (strcasecmp(resultClass, "CIM_ObjectManager") == 0))) {

        /* check it if the ObjectManager cop exists */
        CBGetInstance(_broker, ctx, cop, NULL, &rc);
        if (rc.rc != CMPI_RC_OK)
          return rc;

        /* Return the CIM_ObjectManager instance */
        CMPIInstance* cci = makeObjectManager();
        if (strcasecmp(target, "Assocs") == 0) {
          if (propertyList) {
            CMSetPropertyFilter(cci, propertyList, NULL);
          }
          CMReturnInstance(rslt, cci);
        } else {
          CMReturnObjectPath(rslt, CMGetObjectPath(cci, NULL));
        }
        if (cci)
          CMRelease(cci);

      } else if (resultClass == NULL
                 || (strcasecmp(resultClass, "CIM_NamespaceInManager") == 0)) {
        /* Return the NIM instance */
        makeHostedService(mi, ctx, rslt, nsop, nimop, omop, propertyList,
                          target);
      } 

    }

    /* An ObjectManager was passed in so wee need to return either the
     * Namespace instances or a CIM_NamespaceInManager association 
     * instance */
    if ((role == NULL || strcasecmp(role, "antecedent") == 0)
        && (resultRole == NULL || strcasecmp(resultRole, "dependent") == 0)
        && (CMClassPathIsA(_broker, cop, "cim_objectmanager", &rc) == 1)) {

      /* check it if the ObjectManager cop exists */
      CBGetInstance(_broker, ctx, cop, NULL, &rc);
      if (rc.rc != CMPI_RC_OK)
        return rc;

      if (((strcasecmp(target, "Assocs") == 0)
           || (strcasecmp(target, "AssocNames") == 0))
          && (resultClass == NULL
              || (strcasecmp(resultClass, "CIM_Namespace") ==
                  0))) {
        /* Return the CIM_Namespace instances */
        CMPIEnumeration* nsenm =
            CBEnumInstances(_broker, ctx, nsop, NULL, &rc);
        CMPIData        inst = CMGetNext(nsenm, &rc);
        for (; inst.state == CMPI_goodValue; inst = CMGetNext(nsenm, &rc)) {
          
          if (strcasecmp(target, "Assocs") == 0) {
            if (propertyList) {
              CMSetPropertyFilter(inst.value.inst, propertyList, NULL);
            }
            CMReturnInstance(rslt, (inst.value.inst));
          } else {
            CMReturnObjectPath(rslt, CMGetObjectPath(inst.value.inst, NULL));
          }
        }
        if (nsenm)
          CMRelease(nsenm);
      } else if (resultClass == NULL
                 || (strcasecmp(resultClass, "CIM_NamespaceInManager") == 0)) {
        /* Return the NIM instance */
        makeHostedService(mi, ctx, rslt, nsop, nimop, omop, propertyList,
                          target);
      }
    }
    return rc;
}

static CMPIStatus handleAssocElementConforms(CMPIAssociationMI* mi, 
					   const CMPIContext* ctx, 
					   const CMPIResult* rslt, 
					   const CMPIObjectPath* cop, 
					   const char* resultClass, 
					   const char* role, 
					   const char* resultRole, 
					   const char** propertyList, 
					   const char* target, 
					     CMPIObjectPath* ecpop) 
{
    CMPIStatus      rc = { CMPI_RC_OK, NULL };

    CMPIObjectPath* isop =
        CMNewObjectPath(_broker, CMGetCharPtr(CMGetNameSpace(cop, &rc)),
                        "CIM_indicationservice", &rc);
    CMPIObjectPath* rpop =
        CMNewObjectPath(_broker, "root/interop", "SFCB_RegisteredProfile",
                        &rc);
    if ((rpop == NULL) || (isop == NULL)) {
      CMSetStatusWithChars(_broker, &rc, CMPI_RC_ERR_FAILED,
                           "Create CMPIObjectPath failed.");
      return rc;
    }
    if ((role == NULL || (strcasecmp(role, "ManagedElement") == 0))
        && (resultRole == NULL
            || (strcasecmp(resultRole, "ConformantStandard") == 0))
        && CMClassPathIsA(_broker, cop, "cim_indicationservice", &rc) == 1) {
      // An IndicationService was passed in, so we need to return either
      // the 
      // CIM_RegisteredProfile instance or a SFCB_ElementConformstoProfile
      // association instance
      if (((strcasecmp(target, "Assocs") == 0)
           || (strcasecmp(target, "AssocNames") == 0))
          && (resultClass == NULL
              || (strcasecmp(resultClass, "SFCB_RegisteredProfile") ==
                  0))) {
        // Return the CIM_RegisteredProfile instance
        // buildAssoc(ctx,rslt,rpop,propertyList,target);
        CMAddKey(rpop, "InstanceID", "CIM:SFCB_IP", CMPI_chars);
        if (strcasecmp(target, "AssocNames") == 0) {
          CMReturnObjectPath(rslt, rpop);
        } else if (strcasecmp(target, "Assocs") == 0) {
          CMPIInstance   *lci =
              CBGetInstance(_broker, ctx, rpop, NULL, &rc);
          if (propertyList) {
            CMSetPropertyFilter(lci, propertyList, NULL);
          }
          CMReturnInstance(rslt, lci);
        }
      } else if (resultClass == NULL
                 ||
                 (strcasecmp(resultClass, "SFCB_ElementConformsToProfile")
                  == 0)
                 ||
                 (strcasecmp(resultClass, "CIM_ElementConformsToProfile")
                  == 0)) {
        // Return the SFCB_ElementConformsToProfile association
        makeElementConforms(mi, ctx, rslt, isop, ecpop, rpop, propertyList,
                            target);
      }
    }
    if ((role == NULL || strcasecmp(role, "antecedent") == 0)
        && (resultRole == NULL || strcasecmp(resultRole, "dependent") == 0)
        && (CMClassPathIsA(_broker, cop, "cim_RegisteredProfile", &rc) ==
            1)) {
      // A CIM_RegisteredProfile was passed in so wee need to return
      // either the
      // CIM_IndicationService instance or a SFCB_ElementConformsToProfile
      // association instance
      if (((strcasecmp(target, "Assocs") == 0)
           || (strcasecmp(target, "AssocNames") == 0))
          && (resultClass == NULL
              || (strcasecmp(resultClass, "CIM_IndicationService") ==
                  0))) {
        // Return the CIM_IndicationService instance
        buildAssoc(ctx, rslt, isop, propertyList, target);
      } else if (resultClass == NULL
                 ||
                 (strcasecmp(resultClass, "SFCB_ElementConformsToProfile")
                  == 0)
                 ||
                 (strcasecmp(resultClass, "CIM_ElementConformsToProfile")
                  == 0)) {
        // Return the SFCB_ElementConformsToProfile association
        makeElementConforms(mi, ctx, rslt, isop, ecpop, rpop, propertyList,
                            target);
      }
    }

    return rc;
}

/** \brief getAssociators - Builds the Association or Reference instances
 *
 *  Determines what needs to be returned for the various associator and
 *  reference calls.
 */
CMPIStatus
getAssociators(CMPIAssociationMI * mi,
               const CMPIContext *ctx,
               const CMPIResult *rslt,
               const CMPIObjectPath * cop,
               const char *assocClass,
               const char *resultClass,
               const char *role,
               const char *resultRole,
               const char **propertyList, const char *target)
{

  CMPIStatus      rc = { CMPI_RC_OK, NULL };
  CMPIObjectPath *saeop = NULL;
  CMPIObjectPath *hsop = NULL,
    *ecpop = NULL,
    *nimop = NULL;

  // Make sure role & resultRole are valid
  if (role && resultRole && (strcasecmp(role, resultRole) == 0)) {
    CMSetStatusWithChars(_broker, &rc, CMPI_RC_ERR_FAILED,
                         "role and resultRole cannot be equal.");
    return rc;
  }
  if (role && (strcasecmp(role, "AffectingElement") != 0)
      && (strcasecmp(role, "AffectedElement") != 0)
      && (strcasecmp(role, "ConformantStandard") != 0)
      && (strcasecmp(role, "ManagedElement") != 0)
      && (strcasecmp(role, "Antecedent") != 0)
      && (strcasecmp(role, "Dependent") != 0)) {
    CMSetStatusWithChars(_broker, &rc, CMPI_RC_ERR_FAILED,
                         "Invalid value for role .");
    return rc;
  }
  if (resultRole && (strcasecmp(resultRole, "AffectingElement") != 0)
      && (strcasecmp(resultRole, "AffectedElement") != 0)
      && (strcasecmp(resultRole, "ConformantStandard") != 0)
      && (strcasecmp(resultRole, "ManagedElement") != 0)
      && (strcasecmp(resultRole, "Antecedent") != 0)
      && (strcasecmp(resultRole, "Dependent") != 0)) {
    CMSetStatusWithChars(_broker, &rc, CMPI_RC_ERR_FAILED,
                         "Invalid value for resultRole .");
    return rc;
  }

  saeop =
      CMNewObjectPath(_broker, CMGetCharPtr(CMGetNameSpace(cop, &rc)),
                      "SFCB_ServiceAffectsElement", &rc);
  hsop =
      CMNewObjectPath(_broker, "root/interop", "CIM_HostedService", &rc);
  nimop =
      CMNewObjectPath(_broker, "root/interop", "CIM_NamespaceInManager", &rc);
  ecpop =
      CMNewObjectPath(_broker, "root/interop",
                      "SFCB_ElementConformsToProfile", &rc);
  if ((saeop == NULL) || (hsop == NULL) || (ecpop == NULL)) {
    CMSetStatusWithChars(_broker, &rc, CMPI_RC_ERR_FAILED,
                         "Create CMPIObjectPath failed.");
    return rc;
  }
  /* Make sure we are getting a request for the right assoc class */
  /* Handle SFCB_ServiceAffectsElement */
  if ((assocClass == NULL)
      || (CMClassPathIsA(_broker, saeop, assocClass, &rc) == 1)) {
    rc = handleAssocSAE(mi, ctx, rslt, cop, resultClass, role, resultRole, propertyList, target, saeop);
    if (rc.rc) return rc;
  }

  /* Handle CIM_HostedService */
  if ((assocClass == NULL)
      || (CMClassPathIsA(_broker, hsop, assocClass, &rc) == 1)) {
    rc = handleAssocHostedService(mi, ctx, rslt, cop, resultClass, role, resultRole, propertyList, target, hsop);
    if (rc.rc) return rc;
  }
  /* Handle CIM_NamespaceInManager */
  if ((assocClass == NULL)
      || (CMClassPathIsA(_broker, nimop, assocClass, &rc) == 1)) {
    rc = handleAssocNIM(mi, ctx, rslt, cop, resultClass, role, resultRole, propertyList, target, nimop);
    if (rc.rc) return rc;
  }
  /* Handle ElementConformstoProfile */
  if (((assocClass == NULL)
      || (CMClassPathIsA(_broker, ecpop, assocClass, &rc) == 1)) 
      && (CMClassPathIsA(_broker, cop, "cim_indicationservice", &rc) == 1)) {
    rc = handleAssocElementConforms(mi, ctx, rslt, cop, resultClass, role, resultRole, propertyList, target, ecpop);
    if (rc.rc) return rc;
  }

  CMReturnDone(rslt);
  CMReturn(CMPI_RC_OK);
}

CMPIStatus
ServerProviderAssociators(CMPIAssociationMI * mi,
                          const CMPIContext *ctx,
                          const CMPIResult *rslt,
                          const CMPIObjectPath * cop,
                          const char *assocClass,
                          const char *resultClass,
                          const char *role,
                          const char *resultRole,
                          const char **propertyList)
{

  _SFCB_ENTER(TRACE_PROVIDERS, "ServerProviderAssociators");
  CMPIStatus      rc = { CMPI_RC_OK, NULL };
  rc = getAssociators(mi, ctx, rslt, cop, assocClass, resultClass, role,
                      resultRole, propertyList, "Assocs");
  _SFCB_RETURN(rc);
}

CMPIStatus
ServerProviderAssociatorNames(CMPIAssociationMI * mi,
                              const CMPIContext *ctx,
                              const CMPIResult *rslt,
                              const CMPIObjectPath * cop,
                              const char *assocClass,
                              const char *resultClass,
                              const char *role, const char *resultRole)
{
  _SFCB_ENTER(TRACE_PROVIDERS, "ServerProviderAssociatorNames");
  CMPIStatus      rc = { CMPI_RC_OK, NULL };
  rc = getAssociators(mi, ctx, rslt, cop, assocClass, resultClass, role,
                      resultRole, NULL, "AssocNames");
  _SFCB_RETURN(rc);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
ServerProviderReferences(CMPIAssociationMI * mi,
                         const CMPIContext *ctx,
                         const CMPIResult *rslt,
                         const CMPIObjectPath * cop,
                         const char *resultClass,
                         const char *role, const char **propertyList)
{
  CMPIStatus      rc = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_PROVIDERS, "ServerProviderReferences");
  rc = getAssociators(mi, ctx, rslt, cop, NULL, resultClass, role, NULL,
                      propertyList, "Refs");
  _SFCB_RETURN(rc);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
ServerProviderReferenceNames(CMPIAssociationMI * mi,
                             const CMPIContext *ctx,
                             const CMPIResult *rslt,
                             const CMPIObjectPath * cop,
                             const char *resultClass, const char *role)
{
  CMPIStatus      rc = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_PROVIDERS, "ServerProviderReferenceNames");
  rc = getAssociators(mi, ctx, rslt, cop, NULL, resultClass, role, NULL,
                      NULL, "RefNames");
  _SFCB_RETURN(rc);
}

CMAssociationMIStub(ServerProvider, ServerProvider, _broker, CMNoHook);
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
