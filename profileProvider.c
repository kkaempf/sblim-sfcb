
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

#include "cmpi/cmpidt.h"
#include "cmpi/cmpift.h"
#include "cmpi/cmpimacs.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "fileRepository.h"
#include <sfcCommon/utilft.h>
#include "trace.h"
#include "providerMgr.h"
#include "internalProvider.h"
#include "native.h"
#include "objectpath.h"
#include <time.h>

#ifdef HAVE_SLP
#include <slp.h>
#include "cimslpCMPI.h"
#include "cimslpUtil.h"
#include "control.h"
pthread_t       slpUpdateThread;
pthread_once_t  slpUpdateInitMtx = PTHREAD_ONCE_INIT;
pthread_mutex_t slpUpdateMtx = PTHREAD_MUTEX_INITIALIZER;
int             slpLifeTime = SLP_LIFETIME_DEFAULT;
// This is an awfully brutish way
// to track two adapters.
char           *http_url = NULL;
char           *https_url = NULL;

static cimomConfig cfgHttp;
static cimomConfig cfgHttps;
static char* httpAdvert = NULL;
static char* httpsAdvert = NULL;

/* flag to know if the advert has already been generated */
static int registered = 0;

static int     enableHttp,
               enableHttps;
static int     enableSlp = 0;

// originally from cimslpSLP.c ***********************************************/

static void
onErrorFnc(SLPHandle hslp, SLPError errcode, void *cookie)
{
  *(SLPError *) cookie = errcode;

  if (errcode) {
    printf("Callback Code %i\n", errcode);
  }
}

static void
deregisterCIMService(const char *urlsyntax)
{
  SLPHandle       hslp;
  SLPError        callbackerr = 0;
  SLPError        err = 0;

  _SFCB_ENTER(TRACE_SLP, "deregisterCIMService");
  err = SLPOpen("", SLP_FALSE, &hslp);
  if (err != SLP_OK) {
    _SFCB_TRACE(1, ("Error opening slp handle %i\n", err));
  }
  err = SLPDereg(hslp, urlsyntax, onErrorFnc, &callbackerr);
  if ((err != SLP_OK) || (callbackerr != SLP_OK)) {
    printf
        ("--- Error deregistering service with slp (%i) ... it will now timeout\n",
         err);
    _SFCB_TRACE(4, ("--- urlsyntax: %s\n", urlsyntax));
  }
  SLPClose(hslp);
}

static int
registerCIMService(char** attrstring, int slpLifeTime, char **urlsyntax)
{
  SLPHandle       hslp;
  SLPError        err = 0;
  SLPError        callbackerr = 0;
  int             retCode = 0;

  _SFCB_ENTER(TRACE_SLP, "registerCIMService");

  err = SLPOpen("", SLP_FALSE, &hslp);
  if (err != SLP_OK) {
    printf("Error opening slp handle %i\n", err);
    retCode = err;
  }

  err = SLPReg(hslp,
               *urlsyntax,
               slpLifeTime,
               NULL, *attrstring, SLP_TRUE, onErrorFnc, &callbackerr);
  if(callbackerr != SLP_OK)
    _SFCB_TRACE(2, ("--- SLP registration error, *urlsyntax = \"%s\"\n", *urlsyntax));

  if ((err != SLP_OK) || (callbackerr != SLP_OK)) {
    printf("Error registering service with slp %i\n", err);
    retCode = err;
  }

  if (callbackerr != SLP_OK) {
    printf("Error registering service with slp %i\n", callbackerr);
    retCode = callbackerr;
  }

  SLPClose(hslp);

  _SFCB_RETURN(retCode);
}
/* end from cimslpSLP.c ******************************************************/

static void
freeCFG(cimomConfig * cfg)
{

  free(cfg->cimhost);
  free(cfg->cimpassword);
  free(cfg->cimuser);
  free(cfg->commScheme);
  free(cfg->port);
}

static void
setUpDefaults(cimomConfig * cfg)
{
  cfg->commScheme = strdup("http");
  cfg->cimhost = strdup("localhost");
  cfg->port = strdup("5988");
  cfg->cimuser = strdup("");
  cfg->cimpassword = strdup("");
  cfg->keyFile = NULL;
  cfg->trustStore = NULL;
  cfg->certFile = NULL;
}

static void
setUpTimes(int *slpLifeTime, int *sleepTime)
{
  if (*slpLifeTime < 16) {
    *slpLifeTime = 16;
  }
  if (*slpLifeTime > SLP_LIFETIME_MAXIMUM) {
    *slpLifeTime = SLP_LIFETIME_DEFAULT;
  }

  *sleepTime = *slpLifeTime - 15;
}

#endif

#define LOCALCLASSNAME "ProfileProvider"

/*
 * ------------------------------------------------------------------------- 
 */

static const CMPIBroker *_broker;

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
  _SFCB_ENTER(TRACE_INDPROVIDER, "ProfileProviderInvokeMethod");
  CMPIStatus      st = { CMPI_RC_ERR_NOT_SUPPORTED, NULL };
  /* this may /seem/ useless, but we use startupProvider() to load 
     profileProvider via a method call; UPDATE_SLP_REG does the work on init */
  if (strcmp(methodName, "_startup")) {
    st.rc = CMPI_RC_OK;
  }
  _SFCB_RETURN(st);
}

CMPIStatus ProfileProviderMethodCleanup(CMPIMethodMI * mi,  
					const CMPIContext * ctx, CMPIBoolean terminate)  
{  
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_INDPROVIDER, "ProfileProviderCleanup");
#ifdef HAVE_SLP
  if (slpUpdateThread) {
    // Tell SLP update thread that we're shutting down
    _SFCB_TRACE(1, ("--- Stopping SLP thread"));
    pthread_kill(slpUpdateThread, SIGUSR2);
    // Wait for thread to complete
    pthread_join(slpUpdateThread, NULL);
    _SFCB_TRACE(1, ("--- SLP Thread stopped"));
  }
#endif // HAVE_SLP
  _SFCB_RETURN(st);
}


#ifdef HAVE_SLP
#define UPDATE_SLP_REG  spawnUpdateThread(ctx)
static void 
updateSLPReg(const CMPIContext *ctx, int slpLifeTime)
{
  long            i;
  int             errC = 0;

  _SFCB_ENTER(TRACE_SLP, "updateSLPReg");

  pthread_mutex_lock(&slpUpdateMtx);

  void* hc = markHeap();

  if(!enableSlp) {
    _SFCB_TRACE(1, ("--- SLP disabled"));
    pthread_mutex_unlock(&slpUpdateMtx);
    _SFCB_EXIT();
  }

  /* generate the advertisement. the values shouldn't be changing, 
     so only do this the first time */
  if (!registered) {
    setUpDefaults(&cfgHttp);
    setUpDefaults(&cfgHttps);
    char* url_syntax;

    getControlBool("enableHttp", &enableHttp);
    if (enableHttp) {
      getControlNum("httpPort", &i);
      free(cfgHttp.port);
      cfgHttp.port = malloc(6 * sizeof(char));    // portnumber has max. 5
      // digits
      sprintf(cfgHttp.port, "%d", (int) i);
      httpAdvert = getSLPData(cfgHttp, _broker, ctx, &url_syntax);
      httpAdvert = realloc(httpAdvert, (strlen(httpAdvert) + 1) * sizeof(char));
      freeCFG(&cfgHttp);

      /*
       * We have 2x urlsyntax:
       * css.url_syntax, which is just the url, e.g.
       *  http://somehost:someport
       * urlsyntax, which is the complete service string as required by slp, e.g.
       *  service:wbem:http://somehost:someport
       */
      http_url = malloc(strlen(url_syntax) + 14);
      sprintf(http_url, "service:wbem:%s", url_syntax);
      free(url_syntax);
    }

    getControlBool("enableHttps", &enableHttps);
    if (enableHttps) {
      free(cfgHttps.commScheme);
      cfgHttps.commScheme = strdup("https");
      getControlNum("httpsPort", &i);
      free(cfgHttps.port);
      cfgHttps.port = malloc(6 * sizeof(char));   // portnumber has max. 5
      // digits 
      sprintf(cfgHttps.port, "%d", (int) i);
      /* these aren't used right now, but maybe in the future */
      getControlChars("sslClientTrustStore", &cfgHttps.trustStore);
      getControlChars("sslCertificateFilePath", &cfgHttps.certFile);
      getControlChars("sslKeyFilePath", &cfgHttps.keyFile);

      httpsAdvert = getSLPData(cfgHttps, _broker, ctx, &url_syntax);
      httpsAdvert = realloc(httpsAdvert, (strlen(httpsAdvert) + 1) * sizeof(char));
      freeCFG(&cfgHttps);

      https_url = malloc(strlen(url_syntax) + 14);
      sprintf(https_url, "service:wbem:%s", url_syntax);
      free(url_syntax);

    }

    registered = 1;
  }


  /* register */
  if (enableHttp) {
    if((errC = registerCIMService(&httpAdvert, slpLifeTime,
                                  &http_url)) != 0) {
      _SFCB_TRACE(1, ("--- Error registering http with SLP: %d", errC));
    }
  }
  if (enableHttps) {
    if((errC = registerCIMService(&httpsAdvert, slpLifeTime,
                                  &https_url)) != 0) {
      _SFCB_TRACE(1, ("--- Error registering https with SLP: %d", errC));
    }
  }
  
  releaseHeap(hc);
  pthread_mutex_unlock(&slpUpdateMtx);
  return;
}

static int slp_shutting_down = 0;

static void
handle_sig_slp(int signum)
{
  //Indicate that slp is shutting down
  slp_shutting_down = 1;
}

static void
slpUpdateInit(void)
{
  slpUpdateThread = pthread_self();
}

static void *
slpUpdate(void *args)
{
  int             sleepTime;
  long            i;
  extern char    *configfile;

  // set slpUpdateThread to appropriate thread info
  pthread_once(&slpUpdateInitMtx, slpUpdateInit);
  // exit thread if another already exists
  if(!pthread_equal(slpUpdateThread, pthread_self())) return NULL;
  _SFCB_ENTER(TRACE_SLP, "slpUpdate");
  //Setup signal handlers
  struct sigaction sa;
  sa.sa_handler = handle_sig_slp;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR2, &sa, NULL);
 
  //Get context from args
  CMPIContext *ctx = (CMPIContext *)args;
  // Get enableSlp config value
  setupControl(configfile);
  // If enableSlp is false, we don't really need this thread
  getControlBool("enableSlp", &enableSlp);
  if(!enableSlp) {
    _SFCB_TRACE(1, ("--- SLP disabled in config. Update thread not starting."));
    _SFCB_RETURN(NULL);
  }
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
    _SFCB_TRACE(4, ("--- timeLeft: %d, slp_shutting_down: %s\n",
            timeLeft, slp_shutting_down ? "true" : "false"));
  }
  //End loop
  CMRelease(ctx);
  if(http_url) {
    _SFCB_TRACE(2, ("--- Deregistering http advertisement"));
    deregisterCIMService(http_url);
    free(httpAdvert);
  }
  if(https_url) {
    _SFCB_TRACE(2, ("--- Deregistering https advertisement"));
    deregisterCIMService(https_url);
    free(httpsAdvert);
  }
  _SFCB_RETURN(NULL);
}

static void
spawnUpdateThread(const CMPIContext *ctx)
{
  pthread_attr_t  attr;
  pthread_t       newThread;
  int             rc = 0;
  //CMPIStatus      st = { 0 , NULL };
  void           *thread_args = NULL;
  thread_args = (void *)native_clone_CMPIContext(ctx);

  // create a thread
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  rc = pthread_create(&newThread, &attr, slpUpdate, thread_args);
  if(rc) {
    mlogf(M_ERROR, M_SHOW, "--- Could not create SLP update thread. SLP disabled.");
    /* note: without SLP running, this provider is pretty useless.  But 
       if it's marked "unload: never" there's not much we can do from here */
  }
}

#else // no HAVE_SLP
#define UPDATE_SLP_REG CMNoHook
#endif // HAVE_SLP

CMMethodMIStub(ProfileProvider, ProfileProvider, _broker, UPDATE_SLP_REG);

/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
