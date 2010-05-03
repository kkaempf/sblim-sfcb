
/*
 * profileProvider.c
 *
 * (C) Copyright IBM Corp. 2008
 *
 * THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
 * ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
 * CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
 *
 * You can obtain a current copy of the Eclipse Public License from
 * http://www.opensource.org/licenses/eclipse-1.0.php
 *
 * Author:     Chris Buccella <buccella@linux.vnet.ibm.com>
 *
 * Description:
 *
 * A provider for sfcb implementing CIM_RegisteredProfile
 *
 * Based on the InteropProvider by Adrian Schuur
 *
 */

#include "cmpidt.h"
#include "cmpift.h"
#include "cmpimacs.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "fileRepository.h"
#include "utilft.h"
#include "trace.h"
#include "providerMgr.h"
#include "internalProvider.h"
#include "native.h"
#include "objectpath.h"
#include <time.h>
#include "cimslp.h"
#include "cimslpCMPI.h"

#ifdef HAVE_SLP
#include "control.h"
pthread_t slpUpdateThread;
// This is an awfully brutish way
// to track two adapters.
char           *http_url = NULL;
char           *http_attr = "NULL";
char           *https_url = NULL;
char           *https_attr = "NULL";
#endif

#define LOCALCLASSNAME "ProfileProvider"

extern void     setStatus(CMPIStatus *st, CMPIrc rc, char *msg);

/*
 * ------------------------------------------------------------------------- 
 */

static const CMPIBroker *_broker;

typedef struct profile {
  char           *InstanceID;   /* <OrgID>:<LocalID> */
  unsigned int    RegisteredOrganization;       /* "other" = 1, DMTF = 2,
                                                 * ... */
  char           *RegisteredName;       /* maxlen 256 */
  char           *RegisteredVersion;    /* major.minor.update */
  unsigned int    AdvertiseTypes;       /* other = 1, not = 2, slp = 3 */
  /*
   * should be an int[], but we're only supporting 2 or 3 right now 
   */
  char           *OtherRegisteredOrganzation;   /* only if RegOrg = 1 */
  char           *AdvertiseTypeDescriptions;    /* only if AdvertiseTypes
                                                 * = 1 */
} Profile;

/*
 * ------------------------------------------------------------------------- 
 */

/*
 * checks if an object path is for the /root/interop or /root/pg_interop 
 */
static int
interOpNameSpace(const CMPIObjectPath * cop, CMPIStatus *st)
{
  char           *ns = (char *) CMGetNameSpace(cop, NULL)->hdl;
  if (strcasecmp(ns, "root/interop") && strcasecmp(ns, "root/pg_interop")) {
    if (st)
      setStatus(st, CMPI_RC_ERR_FAILED,
                "Object must reside in root/interop");
    return 0;
  }
  return 1;
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIContext *
prepareUpcall(const CMPIContext *ctx)
{
  /*
   * used to invoke the internal provider in upcalls, otherwise we will be 
   * routed here again
   */
  CMPIContext    *ctxLocal;
  ctxLocal = native_clone_CMPIContext(ctx);
  CMPIValue       val;
  val.string = sfcb_native_new_CMPIString("$DefaultProvider$", NULL, 0);
  ctxLocal->ft->addEntry(ctxLocal, "rerouteToProvider", &val, CMPI_string);
  return ctxLocal;
}

/*
 * ------------------------------------------------------------------------- 
 */

/*
 * make a getProfileProperties as well?
 */

static int
setProfileProperties(const CMPIInstance *in, Profile * prof,
                     CMPIStatus *st)
{
  if (in && prof) {

    CMSetProperty(in, "InstanceID", prof->InstanceID, CMPI_chars);
    CMSetProperty(in, "RegisteredName", prof->RegisteredName, CMPI_chars);
    CMSetProperty(in, "RegisteredVersion", prof->RegisteredVersion,
                  CMPI_chars);
    CMSetProperty(in, "RegisteredOrganization",
                  (CMPIValue *) & (prof->RegisteredOrganization),
                  CMPI_uint16);

    CMPIArray      *at = CMNewArray(_broker, 1, CMPI_uint16, st);
    CMSetArrayElementAt(at, 0, (CMPIValue *) & (prof->AdvertiseTypes),
                        CMPI_uint16);
    CMSetProperty(in, "AdvertiseTypes", (CMPIValue *) & (at),
                  CMPI_uint16A);

    return 0;
  } else {
    return -1;
  }
}

/*
 * ------------------------------------------------------------------ *
 * InterOp initialization
 * ------------------------------------------------------------------ 
 */

static void
initProfiles(const CMPIBroker * broker, const CMPIContext *ctx)
{

  CMPIObjectPath *op;
  const CMPIInstance *ci;
  CMPIContext    *ctxLocal;
  CMPIStatus      st;

  _SFCB_ENTER(TRACE_INDPROVIDER, "initProfiles");

  ctxLocal = prepareUpcall((CMPIContext *) ctx);

  /*
   * Add Profile Registration profile 
   */
  op = CMNewObjectPath(broker, "root/interop", "sfcb_registeredprofile",
                       &st);
  ci = CMNewInstance(broker, op, &st);
  Profile        *prof = (Profile *) malloc(sizeof(Profile));
  prof->InstanceID = "CIM:SFCB_PR";
  prof->RegisteredOrganization = 2;
  prof->RegisteredName = "Profile Registration";
  prof->RegisteredVersion = "1.0.0";
  prof->AdvertiseTypes = 3;

  CMAddKey(op, "InstanceID", prof->InstanceID, CMPI_chars);
  setProfileProperties(ci, prof, &st);

  broker->bft->createInstance(broker, ctxLocal, op, ci, &st);

  free(prof);

  // should really check if st.rc != 0 or 11...

  _SFCB_EXIT();
}

/*
 * --------------------------------------------------------------------------
 */
/*
 * Instance Provider Interface 
 */
/*
 * --------------------------------------------------------------------------
 */

CMPIStatus
ProfileProviderCleanup(CMPIInstanceMI * mi,
                       const CMPIContext *ctx, CMPIBoolean terminate)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_INDPROVIDER, "ProfileProviderCleanup");
#ifdef HAVE_SLP
  // Tell SLP update thread that we're shutting down
  _SFCB_TRACE(1, ("--- Stopping SLP thread"));
  fprintf(stderr, "SMS - here 1\n");
  pthread_kill(slpUpdateThread, SIGUSR2);
  // Wait for thread to complete
  pthread_join(slpUpdateThread, NULL);
  _SFCB_TRACE(1, ("--- SLP Thread stopped"));
  fprintf(stderr, "SMS - here 2\n");
#endif // HAVE_SLP
  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
ProfileProviderEnumInstanceNames(CMPIInstanceMI * mi,
                                 const CMPIContext *ctx,
                                 const CMPIResult *rslt,
                                 const CMPIObjectPath * ref)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIEnumeration *enm;
  CMPIContext    *ctxLocal;
  _SFCB_ENTER(TRACE_INDPROVIDER, "ProfileProviderEnumInstanceNames");

  ctxLocal = prepareUpcall((CMPIContext *) ctx);
  enm = _broker->bft->enumerateInstanceNames(_broker, ctxLocal, ref, &st);
  CMRelease(ctxLocal);

  while (enm && enm->ft->hasNext(enm, &st)) {
    CMReturnObjectPath(rslt, (enm->ft->getNext(enm, &st)).value.ref);
  }
  if (enm)
    CMRelease(enm);
  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
ProfileProviderEnumInstances(CMPIInstanceMI * mi,
                             const CMPIContext *ctx,
                             const CMPIResult *rslt,
                             const CMPIObjectPath * ref,
                             const char **properties)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIEnumeration *enm;
  CMPIContext    *ctxLocal;
  _SFCB_ENTER(TRACE_INDPROVIDER, "ProfileProviderEnumInstances");

  ctxLocal = prepareUpcall((CMPIContext *) ctx);
  enm =
      _broker->bft->enumerateInstances(_broker, ctxLocal, ref, properties,
                                       &st);
  CMRelease(ctxLocal);

  while (enm && enm->ft->hasNext(enm, &st)) {
    CMReturnInstance(rslt, (enm->ft->getNext(enm, &st)).value.inst);
  }
  if (enm)
    CMRelease(enm);
  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
ProfileProviderGetInstance(CMPIInstanceMI * mi,
                           const CMPIContext *ctx,
                           const CMPIResult *rslt,
                           const CMPIObjectPath * cop,
                           const char **properties)
{

  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIContext    *ctxLocal;
  CMPIInstance   *ci;

  _SFCB_ENTER(TRACE_INDPROVIDER, "ProfileProviderGetInstance");

  ctxLocal = prepareUpcall((CMPIContext *) ctx);

  ci = _broker->bft->getInstance(_broker, ctxLocal, cop, properties, &st);
  if (st.rc == CMPI_RC_OK) {
    CMReturnInstance(rslt, ci);
  }

  CMRelease(ctxLocal);

  _SFCB_RETURN(st);

}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
ProfileProviderCreateInstance(CMPIInstanceMI * mi,
                              const CMPIContext *ctx,
                              const CMPIResult *rslt,
                              const CMPIObjectPath * cop,
                              const CMPIInstance *ci)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIContext    *ctxLocal;
  //cimomConfig     cfg;

  _SFCB_ENTER(TRACE_INDPROVIDER, "ProfileProviderCreateInstance");

  ctxLocal = prepareUpcall((CMPIContext *) ctx);
  CMReturnObjectPath(rslt,
                     _broker->bft->createInstance(_broker, ctxLocal, cop,
                                                  ci, &st));
  CMRelease(ctxLocal);
  //updateSLPRegistration
  //updateSLPReg(ctx);

  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
ProfileProviderModifyInstance(CMPIInstanceMI * mi,
                              const CMPIContext *ctx,
                              const CMPIResult *rslt,
                              const CMPIObjectPath * cop,
                              const CMPIInstance *ci,
                              const char **properties)
{
  CMPIStatus      st = { CMPI_RC_ERR_NOT_SUPPORTED, NULL };
  _SFCB_ENTER(TRACE_INDPROVIDER, "ProfileProviderModifyInstance");
  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
ProfileProviderDeleteInstance(CMPIInstanceMI * mi,
                              const CMPIContext *ctx,
                              const CMPIResult *rslt,
                              const CMPIObjectPath * cop)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIContext    *ctxLocal;

  _SFCB_ENTER(TRACE_INDPROVIDER, "ProfileProviderDeleteInstance");

  ctxLocal = prepareUpcall((CMPIContext *) ctx);
  st = _broker->bft->deleteInstance(_broker, ctxLocal, cop);
  CMRelease(ctxLocal);
  //updateSLPRegistration
  //updateSLPReg(ctx);

  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
ProfileProviderExecQuery(CMPIInstanceMI * mi,
                         const CMPIContext *ctx,
                         const CMPIResult *rslt,
                         const CMPIObjectPath * cop,
                         const char *lang, const char *query)
{
  CMPIStatus      st = { CMPI_RC_ERR_NOT_SUPPORTED, NULL };
  _SFCB_ENTER(TRACE_INDPROVIDER, "ProfileProviderExecQuery");
  _SFCB_RETURN(st);
}

/*
 * --------------------------------------------------------------------------
 */
/*
 * Method Provider Interface 
 */
/*
 * --------------------------------------------------------------------------
 */

CMPIStatus
ProfileProviderMethodCleanup(CMPIMethodMI * mi,
                             const CMPIContext *ctx, CMPIBoolean terminate)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_INDPROVIDER, "ProfileProviderMethodCleanup");
  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
ProfileProviderInvokeMethod(CMPIMethodMI * mi,
                            const CMPIContext *ctx,
                            const CMPIResult *rslt,
                            const CMPIObjectPath * ref,
                            const char *methodName,
                            const CMPIArgs * in, CMPIArgs * out)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };

  _SFCB_ENTER(TRACE_INDPROVIDER, "ProfileProviderInvokeMethod");

  if (interOpNameSpace(ref, &st) != 1)
    _SFCB_RETURN(st);

  _SFCB_TRACE(1, ("--- Method: %s", methodName));

  if (strcasecmp(methodName, "_startup") == 0) {
    initProfiles(_broker, ctx);
  }

  else {
    _SFCB_TRACE(1, ("--- Invalid request method: %s", methodName));
    setStatus(&st, CMPI_RC_ERR_METHOD_NOT_FOUND, "Invalid request method");
  }

  _SFCB_RETURN(st);
}

#ifdef HAVE_SLP
#define UPDATE_SLP_REG  spawnUpdateThread(ctx)
void 
updateSLPReg(const CMPIContext *ctx, int slpLifeTime)
{
  cimSLPService   service;
  cimomConfig     cfgHttp,
                  cfgHttps;
  int             enableHttp,
                  enableHttps = 0;
  long            i;

  extern char    *configfile;

  _SFCB_ENTER(TRACE_SLP, "slpAgent");

  setupControl(configfile);

  setUpDefaults(&cfgHttp);
  setUpDefaults(&cfgHttps);

  sleep(1);

  if (!getControlBool("enableHttp", &enableHttp)) {
    getControlNum("httpPort", &i);
    free(cfgHttp.port);
    cfgHttp.port = malloc(6 * sizeof(char));    // portnumber has max. 5
    // digits
    sprintf(cfgHttp.port, "%d", (int) i);
  }
  if (!getControlBool("enableHttps", &enableHttps)) {
    free(cfgHttps.commScheme);
    cfgHttps.commScheme = strdup("https");
    getControlNum("httpsPort", &i);
    free(cfgHttps.port);
    cfgHttps.port = malloc(6 * sizeof(char));   // portnumber has max. 5
    // digits 
    sprintf(cfgHttps.port, "%d", (int) i);
    getControlChars("sslClientTrustStore", &cfgHttps.trustStore);
    getControlChars("sslCertificateFilePath:", &cfgHttps.certFile);
    getControlChars("sslKeyFilePath", &cfgHttps.keyFile);
  }

  //CMPIContext *ctxLocal = prepareUpcall(ctx);
  service = getSLPData(cfgHttp, _broker, ctx, http_url);
  int errC = 0;
  if((errC = registerCIMService(service, slpLifeTime,
                                &http_url, &http_attr)) != 0) {
    _SFCB_TRACE(1, ("--- Error registering http with SLP: %d", errC));
  }

  service = getSLPData(cfgHttps, _broker, ctx, https_url);
  if((errC = registerCIMService(service, slpLifeTime,
                                &https_url, &https_attr)) != 0) {
    _SFCB_TRACE(1, ("--- Error registering https with SLP: %d", errC));
  }
  
  freeCFG(&cfgHttp);
  freeCFG(&cfgHttps);
  return;
}

static int slp_shutting_down = 0;

void
handle_sig_slp(int signum)
{
  //Indicate that slp is shutting down
  slp_shutting_down = 1;
}

void *
slpUpdateForever(void *args)
{
  int             slpLifeTime = SLP_LIFETIME_DEFAULT;
  int             sleepTime;
  long            i;

  //Setup signal handlers
  struct sigaction sa;
  sa.sa_handler = handle_sig_slp;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR2, &sa, NULL);
 
  //Get context from args
  CMPIContext *ctx = (CMPIContext *)args;
  //Get configured value for refresh interval
  getControlNum("slpRefreshInterval", &i);
  slpLifeTime = (int) i;
  setUpTimes(&slpLifeTime, &sleepTime);
  
  //Start update loop
  while(1) {
    //Update reg
    updateSLPReg(ctx, slpLifeTime);
    //Sleep for refresh interval
    int timeLeft = sleep(sleepTime);
    if(slp_shutting_down) break;
    fprintf(stderr, "SMS - timeLeft: %d, slp_shutting_down: %s\n",
            timeLeft, slp_shutting_down ? "true" : "false");
  }
  //End loop
  deregisterCIMService(http_url);
  deregisterCIMService(https_url);
  return NULL;
}

void
spawnUpdateThread(const CMPIContext *ctx)
{
  pthread_attr_t  attr;
  int             rc = 0;
  //CMPIStatus      st = { 0 , NULL };
  void           *thread_args = NULL;
  thread_args = (void *)native_clone_CMPIContext(ctx);

  // create a thread
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  rc = pthread_create(&slpUpdateThread, &attr, slpUpdateForever, thread_args);
  if(rc) {
    // deal with thread creation error
    exit(1);
  }
}

#endif // HAVE_SLP
/*
 * ------------------------------------------------------------------ *
 * Instance MI Factory NOTE: This is an example using the convenience
 * macros. This is OK as long as the MI has no special requirements, i.e.
 * to store data between calls.
 * ------------------------------------------------------------------ 
 */

//CMInstanceMIStub(ProfileProvider, ProfileProvider, _broker, CMNoHook);
CMInstanceMIStub(ProfileProvider, ProfileProvider, _broker, UPDATE_SLP_REG);

CMMethodMIStub(ProfileProvider, ProfileProvider, _broker, CMNoHook);
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
