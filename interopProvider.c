
/*
 * interopProvider.c
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
 * Author:     Adrian Schuur <schuur@de.ibm.com>
 *
 * Description:
 *
 * InternalProvider for sfcb.
 *
 */

#include "cmpi/cmpidt.h"
#include "cmpi/cmpift.h"
#include "cmpi/cmpimacs.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include "fileRepository.h"
#include <sfcCommon/utilft.h>
#include "trace.h"
#include "queryOperation.h"
#include "providerMgr.h"
#include "internalProvider.h"
#include "native.h"
#include "objectpath.h"
#include <time.h>
#include "instance.h"
#include "control.h"

#define LOCALCLASSNAME "InteropProvider"

/*
 * ------------------------------------------------------------------------- 
 */

extern CMPIInstance *relocateSerializedInstance(void *area);
extern char    *sfcb_value2Chars(CMPIType type, CMPIValue * value);
extern CMPIObjectPath *getObjectPath(char *path, char **msg);

extern void     closeProviderContext(BinRequestContext * ctx);
extern void     setStatus(CMPIStatus *st, CMPIrc rc, char *msg);
extern int      testNameSpace(char *ns, CMPIStatus *st);
extern void     memLinkObjectPath(CMPIObjectPath * op);
extern void     sfcbIndAuditLog(char *, char *);

// Counts to enforce limits from cfg file
static int LDcount=0;
static int AScount=0;


/*
 * ------------------------------------------------------------------------- 
 */

static const CMPIBroker *_broker;
static int      firstTime = 1;
int RIEnabled=0;

typedef struct filter {
  CMPIInstance   *fci;
  QLStatement    *qs;
  int             useCount;
  char           *query;
  char           *lang;
  char           *type;
  char           *sns;
  CMPIArray      *snsa;
} Filter;

typedef struct handler {
  CMPIInstance   *hci;
  CMPIObjectPath *hop;
  int             useCount;
} Handler;

typedef struct subscription {
  CMPIInstance   *sci;
  Filter         *fi;
  Handler        *ha;
} Subscription;

static UtilHashTable *filterHt = NULL;
static UtilHashTable *handlerHt = NULL;
static UtilHashTable *subscriptionHt = NULL;

// Mutex's to protect the hash tables
static pthread_mutex_t filterHTlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t handlerHTlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t subHTlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t subDelLock = PTHREAD_MUTEX_INITIALIZER;

/* for indication delivery */
static long MAX_IND_THREADS;
static long IND_THREAD_TO;
static sem_t availThreadsSem;
struct timespec availThreadWait;

typedef struct delivery_info {
  const CMPIContext* ctx;
  CMPIObjectPath *hop;  
  CMPIArgs* hin;
} DeliveryInfo;


/*
 * ------------------------------------------------------------------------- 
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

static CMPIContext *
prepareUpcall(const CMPIContext *ctx)
{
  /*
   * used to invoke the internal provider in upcalls, otherwise we will be 
   * routed here (interOpProvider) again
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

static Subscription *
addSubscription(const CMPIInstance *ci,
                const char *key, Filter * fi, Handler * ha)
{
  Subscription   *su;

  _SFCB_ENTER(TRACE_INDPROVIDER, "addSubscription");

  pthread_mutex_lock(&subHTlock);
  if (subscriptionHt == NULL) {
    subscriptionHt = UtilFactory->newHashTable(61, UtilHashTable_charKey);
    subscriptionHt->ft->setReleaseFunctions(subscriptionHt, free, NULL);
  }

  _SFCB_TRACE(1, ("-- Subscription: %s\n", key));

  su = subscriptionHt->ft->get(subscriptionHt, key);
  if (su) {
    pthread_mutex_unlock(&subHTlock);
    _SFCB_RETURN(NULL);
  }
  su = (Subscription *) malloc(sizeof(Subscription));
  su->sci = CMClone(ci, NULL);
  su->fi = fi;
  fi->useCount++;
  su->ha = ha;
  ha->useCount++;
  subscriptionHt->ft->put(subscriptionHt, key, su);

  pthread_mutex_unlock(&subHTlock);
  _SFCB_RETURN(su);
}

/*
 * ------------------------------------------------------------------------- 
 */

static Subscription *
getSubscription(char *key)
{
  Subscription   *su;

  _SFCB_ENTER(TRACE_INDPROVIDER, "getSubscription");

  if (subscriptionHt == NULL)
    return NULL;
  su = subscriptionHt->ft->get(subscriptionHt, key);

  _SFCB_RETURN(su);
}

/*
 * ------------------------------------------------------------------------- 
 */

static void
removeSubscription(Subscription * su, char *key)
{
  _SFCB_ENTER(TRACE_INDPROVIDER, "removeSubscription");

  pthread_mutex_lock(&subHTlock);
  if (subscriptionHt) {
    subscriptionHt->ft->remove(subscriptionHt, key);
    if (su) {
      if (su->fi)
        su->fi->useCount--;
      if (su->ha)
        su->ha->useCount--;
    }
  }
  if (su) {
    if (su->sci) {
      CMRelease(su->sci);
    }
    free(su);
  }

  pthread_mutex_unlock(&subHTlock);
  _SFCB_EXIT();
}

/*
 * ------------------------------------------------------------------------- 
 */

static Filter  *
addFilter(const CMPIInstance *ci,
          const char *key,
          QLStatement * qs,
          const char *query, const char *lang, const char *sns,
          const CMPIArray *snsa)
{
  Filter         *fi;

  _SFCB_ENTER(TRACE_INDPROVIDER, "addFilter");

  _SFCB_TRACE(1, ("--- Filter: >%s<", key));
  _SFCB_TRACE(1, ("--- query: >%s<", query));

  pthread_mutex_lock(&filterHTlock);
  if (filterHt == NULL) {
    filterHt = UtilFactory->newHashTable(61, UtilHashTable_charKey);
    filterHt->ft->setReleaseFunctions(filterHt, free, NULL);
  }

  fi = filterHt->ft->get(filterHt, key);
  if (fi) {
    pthread_mutex_unlock(&filterHTlock);
    _SFCB_RETURN(NULL);
  }

  fi = (Filter *) malloc(sizeof(Filter));
  fi->fci = CMClone(ci, NULL);
  fi->useCount = 0;
  fi->qs = qs;
  fi->query = strdup(query);
  fi->lang = strdup(lang);
  fi->sns = strdup(sns);
  if (snsa) fi->snsa=snsa->ft->clone(snsa, NULL);
  else fi->snsa = NULL;
  fi->type = NULL;
  filterHt->ft->put(filterHt, key, fi);
  pthread_mutex_unlock(&filterHTlock);
  _SFCB_RETURN(fi);
}

/*
 * ------------------------------------------------------------------------- 
 */

static Filter  *
getFilter(char *key)
{
  Filter         *fi;

  _SFCB_ENTER(TRACE_INDPROVIDER, "getFilter");
  _SFCB_TRACE(1, ("--- Filter: >%s<", key));

  if (filterHt == NULL)
    return NULL;
  fi = filterHt->ft->get(filterHt, key);

  _SFCB_RETURN(fi);
}

/*
 * ------------------------------------------------------------------------- 
 */

static void
removeFilter(Filter * fi, char *key)
{
  _SFCB_ENTER(TRACE_INDPROVIDER, "removeFilter");

  pthread_mutex_lock(&filterHTlock);
  if (filterHt) {
    filterHt->ft->remove(filterHt, key);
  }
  if (fi) {
    CMRelease(fi->fci);
    CMRelease(fi->qs);
    free(fi->query);
    free(fi->lang);
    free(fi->sns);
    if (fi->snsa) CMRelease(fi->snsa);
    free(fi);
  }

  pthread_mutex_unlock(&filterHTlock);
  _SFCB_EXIT();
}

/*
 * ------------------------------------------------------------------------- 
 */

static Handler *
addHandler(CMPIInstance *ci, CMPIObjectPath * op)
{
  Handler        *ha;
  char           *key;

  _SFCB_ENTER(TRACE_INDPROVIDER, "addHandler");

  pthread_mutex_lock(&handlerHTlock);
  if (handlerHt == NULL) {
    handlerHt = UtilFactory->newHashTable(61, UtilHashTable_charKey);
    handlerHt->ft->setReleaseFunctions(handlerHt, free, NULL);
  }

  key = normalizeObjectPathCharsDup(op);

  _SFCB_TRACE(1, ("--- Handler: %s", key));

  if ((ha = handlerHt->ft->get(handlerHt, key)) != NULL) {
    _SFCB_TRACE(1, ("--- Handler already registered %p", ha));
    if (key)
      free(key);
    pthread_mutex_unlock(&handlerHTlock);
    _SFCB_RETURN(NULL);
  }

  ha = (Handler *) malloc(sizeof(Handler));
  ha->hci = CMClone(ci, NULL);
  ha->hop = CMClone(op, NULL);
  ha->useCount = 0;
  handlerHt->ft->put(handlerHt, key, ha);

  pthread_mutex_unlock(&handlerHTlock);
  _SFCB_RETURN(ha);
}

/*
 * ------------------------------------------------------------------------- 
 */

static Handler *
getHandler(char *key)
{
  Handler        *ha;

  _SFCB_ENTER(TRACE_INDPROVIDER, "getHandler");

  if (handlerHt == NULL)
    return NULL;
  ha = handlerHt->ft->get(handlerHt, key);

  _SFCB_RETURN(ha);
}

/*
 * ------------------------------------------------------------------------- 
 */

static void
removeHandler(Handler * ha, char *key)
{
  _SFCB_ENTER(TRACE_INDPROVIDER, "removeHandler");

  pthread_mutex_lock(&handlerHTlock);
  if (handlerHt) {
    handlerHt->ft->remove(handlerHt, key);
  }
  if (ha) {
    CMRelease(ha->hci);
    CMRelease(ha->hop);
    free(ha);
  }

  pthread_mutex_unlock(&handlerHTlock);
  _SFCB_EXIT();
}

/* 
 * Similar to addHandler(), but useCount is maintained
 * don't need to check for handlerHt because we only get here
 * if getHandler does not return NULL
 */

static Handler *updateHandler(CMPIInstance *ci,
                              CMPIObjectPath * op)
{
  Handler *ha; 
  char *key;
   
  _SFCB_ENTER(TRACE_INDPROVIDER, "updateHandler");
      
  key=normalizeObjectPathCharsDup(op);
      
  _SFCB_TRACE(1,("--- Handler: %s",key));
   
  pthread_mutex_lock(&handlerHTlock);
  // do we need to check??
  if ((ha=handlerHt->ft->get(handlerHt,key))==NULL) {
    _SFCB_TRACE(1,("--- No handler %p",ha));
    if(key) free(key);
    pthread_mutex_unlock(&handlerHTlock);
    _SFCB_RETURN(NULL);
  }

  CMRelease(ha->hci);
  ha->hci=CMClone(ci,NULL);
  CMRelease(ha->hop);
  ha->hop=CMClone(op,NULL);
  handlerHt->ft->put(handlerHt,key,ha);
   
  pthread_mutex_unlock(&handlerHTlock);
  _SFCB_RETURN(ha);
}

/*
 * ------------------------------------------------------------------------- 
 */

extern int      isChild(const char *ns, const char *parent,
                        const char *child);

static int
isa(const char *sns, const char *child, const char *parent)
{
  int             rv;
  _SFCB_ENTER(TRACE_INDPROVIDER, "isa");

  if (strcasecmp(child, parent) == 0)
    return 1;
  rv = isChild(sns, parent, child);
  _SFCB_RETURN(rv);
}

/*
 * ------------------------------------------------------------------------- 
 */

extern CMPISelectExp *TempCMPISelectExp(QLStatement * qs);

/*
 * This generic function serves 4 kinds of request, specified by optype:
 * OPS_ActivateFilter     28
 * OPS_DeactivateFilter   29
 * OPS_DisableIndications 30
 * OPS_EnableIndications  31
 */
CMPIStatus
genericSubscriptionRequest(const char *principal,
                           const char *cn,
                           const char *type,
                           Filter * fi, int optype, int *rrc)
{
  CMPIObjectPath *path;
  CMPIStatus      st = { CMPI_RC_OK, NULL }, rc;
  IndicationReq   sreq = BINREQ(optype, 6);
  BinResponseHdr **resp = NULL;
  BinRequestContext binCtx;
  OperationHdr    req = { OPS_IndicationLookup, 2 };
  int             irc = 0,
      err,
      cnt,
      i;

  _SFCB_ENTER(TRACE_INDPROVIDER, "genericSubscriptionRequest");
  _SFCB_TRACE(4,
              ("principal %s, class %s, type %s, optype %d", principal, cn,
               type, optype));

  /*
   * Use SourceNamespaces[] when provided. Iterate through the array
   * of namespaces and activate filters.
   *
   * SourceNameSpace is used only when the SourceNamespaces[] is null
   */
   int n,j;
   char *save_sns = fi->sns; /* Save original fi->sns */
   char *tmpns = malloc(512); /* Length of namespace. Todo:look for #define */
   if (fi->snsa == NULL) n = 1; /* SourceNamespace when array is NULL */
   else n = CMGetArrayCount(fi->snsa, NULL);
   for (j=0; j < n; j++) {
       if (fi->snsa != NULL) {
          strcpy(tmpns,
          CMGetCharPtr(CMGetArrayElementAt(fi->snsa, j, NULL).value.string));
          fi->sns = tmpns; /* replacing the sns pointer */
          _SFCB_TRACE(4, ("--- activating filter ns[%d]:%s",j,fi->sns));
      }

      if (rrc)
         *rrc = 0;
      path = TrackedCMPIObjectPath(fi->sns, cn, &rc);

      sreq.principal = setCharsMsgSegment(principal);
      sreq.objectPath = setObjectPathMsgSegment(path);
      sreq.query = setCharsMsgSegment(fi->query);
      sreq.language = setCharsMsgSegment(fi->lang);
      sreq.type = setCharsMsgSegment((char *) type);
      fi->type = strdup(type);
      sreq.sns = setCharsMsgSegment(fi->sns);
      sreq.filterId = fi;

      req.nameSpace = setCharsMsgSegment(fi->sns);
      req.className = setCharsMsgSegment((char *) cn);

      memset(&binCtx, 0, sizeof(BinRequestContext));
      binCtx.oHdr = &req;
      binCtx.bHdr = &sreq.hdr;
      binCtx.bHdrSize = sizeof(sreq);
      binCtx.chunkedMode = binCtx.xmlAs = 0;

      _SFCB_TRACE(1, ("--- getProviderContext for %s-%s", fi->sns, cn));

      irc = getProviderContext(&binCtx);

      if (irc == MSG_X_PROVIDER) {
        _SFCB_TRACE(1, ("--- Invoking Providers"));
        /*
         * one good provider makes success 
         */
        resp = invokeProviders(&binCtx, &err, &cnt);
        if (err == 0) {
          setStatus(&st, 0, NULL);
        } else {
          setStatus(&st, resp[err - 1]->rc, NULL);
          for (i = 0; i < binCtx.pCount; i++) {
            if (resp[i]->rc == 0) {
              setStatus(&st, 0, NULL);
              break;
            }
          }
        }
      }

      else {
        if (rrc)
          *rrc = irc;
        if (irc == MSG_X_PROVIDER_NOT_FOUND)
          setStatus(&st, CMPI_RC_ERR_FAILED,
                "No eligible indication provider found");
        else {
          char            msg[512];
          snprintf(msg, 511,
               "Failing to find eligible indication provider. Rc: %d",
               irc);
          setStatus(&st, CMPI_RC_ERR_FAILED, msg);
        }
      }

      if (resp) {
        cnt = binCtx.pCount;
        while (cnt--) {
          if (resp[cnt]) {
            free(resp[cnt]);
          }
        }
        free(resp);
        closeProviderContext(&binCtx);
      }
      if (fi->type) {
        free(fi->type);
      }
   }
   fi->sns = save_sns; /* restore back fi->sns */
   if (tmpns) free(tmpns);

  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

int
fowardSubscription(const CMPIContext *ctx,
                   Filter * fi, int optype, CMPIStatus *st)
{
  CMPIStatus      rc;
  char           *principal = NULL;
  char          **fClasses = fi->qs->ft->getFromClassList(fi->qs);
  CMPIData        principalP = ctx->ft->getEntry(ctx, CMPIPrincipal, &rc);
  int             irc;
  int             activated = 0;

  _SFCB_ENTER(TRACE_INDPROVIDER, "fowardSubscription");

  if (rc.rc == CMPI_RC_OK) {
    principal = (char *) principalP.value.string->hdl;
    _SFCB_TRACE(1, ("--- principal=\"%s\"", principal));
  }

  /*
   * Go thru all the indication classes specified in the filter query and
   * activate each 
   */
  for (; *fClasses; fClasses++) {
    _SFCB_TRACE(1,
                ("--- indication class=\"%s\" namespace=\"%s\"", *fClasses,
                 fi->sns));

    /*
     * Check if this is a process indication 
     */
    if (isa(fi->sns, *fClasses, "CIM_ProcessIndication") ||
        isa(fi->sns, *fClasses, "CIM_InstCreation") ||
        isa(fi->sns, *fClasses, "CIM_InstDeletion") ||
        isa(fi->sns, *fClasses, "CIM_InstModification")) {
      *st =
          genericSubscriptionRequest(principal, *fClasses, *fClasses, fi,
                                     optype, &irc);
      if (st->rc == CMPI_RC_OK)
        activated++;
    }

    /*
     * Warn if this indication class is unknown and continue processing
     * the rest, if any 
     */
    else {
      _SFCB_TRACE(1, ("--- Unsupported/unrecognized indication class"));
    }
  }

  /*
   * Make sure at least one of the indication classes were successfully
   * activated 
   */
  if (!activated) {
    setStatus(st, CMPI_RC_ERR_NOT_SUPPORTED,
              "No supported indication classes in filter query"
              " or no provider found");
    _SFCB_RETURN(-1);
  }

  setStatus(st, CMPI_RC_OK, NULL);
  _SFCB_RETURN(0);
}

/*
 * ------------------------------------------------------------------------- 
 */

extern UtilStringBuffer *instanceToString(CMPIInstance *ci, char **props);

CMPIStatus
switchIndications(const CMPIContext *ctx,
                  const CMPIInstance *ci, int optype)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  Filter         *fi;
  CMPIObjectPath *op;
  char           *key;

  _SFCB_ENTER(TRACE_INDPROVIDER, "enableIndications()");

  op = CMGetProperty(ci, "filter", &st).value.ref;
  key = normalizeObjectPathCharsDup(op);
  fi = getFilter(key);
  if (key)
    free(key);

  fowardSubscription(ctx, fi, optype, &st);

  _SFCB_RETURN(st);
}

static CMPIStatus
processSubscription(const CMPIBroker * broker,
                    const CMPIContext *ctx,
                    const CMPIInstance *ci, const CMPIObjectPath * cop)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  Handler        *ha = NULL;
  Filter         *fi = NULL;
  Subscription   *su;
  CMPIObjectPath *op;
  char           *key,
                 *skey;
  CMPIDateTime   *dt;
  CMPIValue       val;

  _SFCB_ENTER(TRACE_INDPROVIDER, "processSubscription()");

  _SFCB_TRACE(1, ("--- checking for existing subscription"));
  skey = normalizeObjectPathCharsDup(cop);
  if (getSubscription(skey)) {
    _SFCB_TRACE(1, ("--- subscription already exists"));
    if (skey)
      free(skey);
    setStatus(&st, CMPI_RC_ERR_ALREADY_EXISTS, NULL);
    _SFCB_RETURN(st);
  }

  _SFCB_TRACE(1, ("--- getting new subscription filter"));
  op = CMGetProperty(ci, "filter", &st).value.ref;
  /* ht key does not contain ns; need to check for it again here */
  if (interOpNameSpace(op,&st) != 1) _SFCB_RETURN(st);

  if (op) {
    key = normalizeObjectPathCharsDup(op);
    if (key) {
      fi = getFilter(key);
      free(key);
    }
  }

  if (fi == NULL) {
    _SFCB_TRACE(1, ("--- cannot find specified subscription filter"));
    setStatus(&st, CMPI_RC_ERR_NOT_FOUND, "Filter not found");
    if (skey)
      free(skey);
    _SFCB_RETURN(st);
  }

  _SFCB_TRACE(1, ("--- getting new subscription handle"));
  op = CMGetProperty(ci, "handler", &st).value.ref;
  /* ht key does not contain ns; need to check for it again here */
  if (interOpNameSpace(op,&st) != 1) _SFCB_RETURN(st);

  if (op) {
    key = normalizeObjectPathCharsDup(op);
    if (key) {
      ha = getHandler(key);
      free(key);
    }
  }

  if (ha == NULL) {
    _SFCB_TRACE(1, ("--- cannot find specified subscription handler"));
    setStatus(&st, CMPI_RC_ERR_NOT_FOUND, "Handler not found");
    if (skey)
      free(skey);
    _SFCB_RETURN(st);
  }
   // Get current state
   CMPIData d = CMGetProperty(ci, "SubscriptionState", &st);
   if (d.state != CMPI_goodValue) {
       // Not given, assume enable
       val.uint16 = 2;
       st = CMSetProperty((CMPIInstance*)ci, "SubscriptionState", &val, CMPI_uint16);
       d.value.uint16=2;
   }
   if(d.value.uint16 == 2) {
      // Check if we are hitting the max
      long cfgmax;
      getControlNum("MaxActiveSubscriptions", &cfgmax);
      if (AScount+1 > cfgmax) {
         setStatus(&st,CMPI_RC_ERR_FAILED,"Subscription activation would exceed MaxActiveSubscription limit");
	 if (skey)
	   free(skey);
         return st;
      }
      AScount++;
   }

  _SFCB_TRACE(1, ("--- setting subscription start time"));
  dt = CMNewDateTime(_broker, NULL);
  CMSetProperty((CMPIInstance *) ci, "SubscriptionStartTime", &dt,
                CMPI_dateTime);

  su = addSubscription(ci, skey, fi, ha);

  /* send Activate filter request only if we haven't aleady */
  if (fi->useCount == 1) {
    fowardSubscription(ctx, fi, OPS_ActivateFilter, &st);
    if (st.rc != CMPI_RC_OK) removeSubscription(su, skey);
  }

  /* activation succesful, try to enable it */
  if (st.rc == CMPI_RC_OK) {
    /* only enable if state is 2 (default) */
    if(d.value.uint16 == 2 && fi->useCount == 1) {
	    fowardSubscription(ctx, fi, OPS_EnableIndications, &st);
    }
  }

  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------ *
 * InterOp initialization
 * ------------------------------------------------------------------ 
 */

void
initInterOp(const CMPIBroker * broker, const CMPIContext *ctx)
{
  CMPIObjectPath *op;
  CMPIEnumeration *enm;
  CMPIInstance   *ci;
  CMPIStatus      st;
  CMPIObjectPath *cop;
  CMPIContext    *ctxLocal;
  char           *key,
                 *query,
                 *lng,
                 *sns;
  CMPIArray      *snsa = NULL;
  QLStatement    *qs = NULL;
  int             rc;

  _SFCB_ENTER(TRACE_INDPROVIDER, "initInterOp");

  firstTime = 0;

  _SFCB_TRACE(1, ("--- checking for cim_indicationfilter"));
  op = CMNewObjectPath(broker, "root/interop", "cim_indicationfilter",
                       &st);
  ctxLocal = prepareUpcall((CMPIContext *) ctx);
  enm = _broker->bft->enumerateInstances(_broker, ctxLocal, op, NULL, &st);

  if (enm) {
    while (enm->ft->hasNext(enm, &st)
           && (ci = (enm->ft->getNext(enm, &st)).value.inst)) {
      cop = CMGetObjectPath(ci, &st);
      query = (char *) CMGetProperty(ci, "query", &st).value.string->hdl;
      lng =
          (char *) CMGetProperty(ci, "querylanguage",
                                 &st).value.string->hdl;
      sns =
          (char *) CMGetProperty(ci, "SourceNamespace",
                                 &st).value.string->hdl;
      snsa=ci->ft->getProperty(ci,"SourceNamespaces",&st).value.array;
      qs = parseQuery(MEM_NOT_TRACKED, query, lng, sns, snsa, &rc);
      key = normalizeObjectPathCharsDup(cop);
      addFilter(ci, key, qs, query, lng, sns, snsa);
    }
    CMRelease(enm);
  }

  CMPIObjectPath *isop=CMNewObjectPath(broker,"root/interop","CIM_IndicationService",NULL);
  CMPIEnumeration *isenm = broker->bft->enumerateInstances(broker, ctx, isop, NULL, NULL);
  CMPIData isinst=CMGetNext(isenm,NULL);
  CMPIData mc=CMGetProperty(isinst.value.inst,"DeliveryRetryAttempts",NULL);
  int RIEnabled=mc.value.uint16;
  mc = CMGetProperty(isinst.value.inst, "Name", NULL);

  _SFCB_TRACE(1, ("--- checking for cim_listenerdestination"));
  op = CMNewObjectPath(broker, "root/interop", "cim_listenerdestination",
                       &st);
  enm = _broker->bft->enumerateInstances(_broker, ctx, op, NULL, &st);

  if (enm) {
    // Loop through all the listeners
    CMPIData ld;
    int ldcount=0;
    char context[100];
    while (enm->ft->hasNext(enm, &st)
           && (ci = (enm->ft->getNext(enm, &st)).value.inst)) {
      cop = CMGetObjectPath(ci, &st);
      if (RIEnabled) {
         // check and set context for migrated listeners.
         CMPIInstance *ldi = _broker->bft->getInstance(_broker, ctxLocal, cop, NULL, NULL);
         ld = CMGetProperty(ldi, "SequenceContext", NULL);
         if (ld.state != CMPI_goodValue) {
             _SFCB_TRACE(1,("---  adding SequenceContext to migrated cim_listenerdestination"));
             // build and set the context string, we can't know the actual creation
             // time, so just use SFCB start time + index.
             ldcount++;
             sprintf (context,"%s#%sM%d#",mc.value.string->ft->getCharPtr(mc.value.string,NULL),sfcBrokerStart,ldcount);
             CMPIValue scontext;
             scontext.string = sfcb_native_new_CMPIString(context, NULL, 0);
             CMSetProperty(ldi, "SequenceContext", &scontext, CMPI_string);
         }
         // Reset the sequence numbers on sfcb restart
         CMPIValue zarro = {.sint64 = -1 };
         CMSetProperty(ldi, "LastSequenceNumber", &zarro, CMPI_sint64);
         CBModifyInstance(_broker, ctxLocal, cop, ldi, NULL);
      }
      addHandler(ci, cop);
    }
    CMRelease(enm);
  }
  _SFCB_TRACE(1, ("--- checking for cim_indicationsubscription"));
  op = CMNewObjectPath(broker, "root/interop",
                       "cim_indicationsubscription", &st);
  enm = _broker->bft->enumerateInstances(_broker, ctxLocal, op, NULL, &st);

  if (enm) {
    CMPIStatus st;
    while (enm->ft->hasNext(enm, &st)
           && (ci = (enm->ft->getNext(enm, &st)).value.inst)) {
      CMPIObjectPath *hop;
      cop = CMGetObjectPath(ci, &st);
      hop = CMGetKey(cop, "handler", NULL).value.ref;
      st = processSubscription(broker,ctx,ci,cop);
      /* if the on-disk repo is modified between startups, it is
         possible for a subscription instance to exist w/o a filter or
         handler. Clean out the useless sub if this is the case */
      if (st.rc == CMPI_RC_ERR_NOT_FOUND) {
        CBDeleteInstance(_broker, ctxLocal, cop);
      }
    }
    CMRelease(enm);
  }
  CMRelease(ctxLocal);

  getControlNum("indicationDeliveryThreadLimit",&MAX_IND_THREADS);
  getControlNum("indicationDeliveryThreadTimeout",&IND_THREAD_TO);
  sem_init(&availThreadsSem, 0, MAX_IND_THREADS);

  _SFCB_EXIT();
}


/*
 for CIM_IndicationSubscription we use the DeliveryFailureTime property to track
 when indication delivery first failed.  However, this property is not a part of
 the mof supplied by the DMTF, so we need to filter it out of instances being
 returned to the client
 */
void
filterInternalProps(CMPIInstance* ci) 
{

  CMPIStatus      pst = { CMPI_RC_OK, NULL };
  CMGetProperty(ci, "DeliveryFailureTime", &pst);
  /* prop is set, need to clear it out */
  if (pst.rc != CMPI_RC_ERR_NOT_FOUND) {
    filterFlagProperty(ci, "DeliveryFailureTime");
  }

  return;
}

/* feature #3495060 :76814 : Verify the filter and handler information */
CMPIStatus
verify_subscription(const CMPIContext * ctx,
        const CMPIObjectPath *cop, 
        const CMPIInstance *ci)
{ 
      CMPIContext *ctxlocal = NULL;
      CMPIStatus st = { CMPI_RC_OK, NULL };

      CMPIData sub_filter = CMGetProperty(ci, "Filter", &st);
      CMPIObjectPath *sub_filter_op = sub_filter.value.ref;
      ctxlocal = prepareUpcall((CMPIContext *)ctx);
      CMPIInstance *sub_filter_inst = CBGetInstance(_broker, ctxlocal,
                    sub_filter_op, NULL, &st);
      if (sub_filter_inst == NULL) {
         setStatus(&st,st.rc,"Invalid Subscription Filter");
         CMRelease(ctxlocal);
         return st;
      }

      CMPIData sub_handler = CMGetProperty(ci, "Handler", &st);
      CMPIObjectPath *sub_handler_op = sub_handler.value.ref;
      CMPIInstance *sub_handler_inst = CBGetInstance(_broker, ctxlocal,
                    sub_handler_op, NULL, &st);
      if (sub_handler_inst == NULL) {
         setStatus(&st,st.rc,"Invalid Subscription Handler");
         CMRelease(ctxlocal);
         return st;
      }

      CMRelease(ctxlocal);
      return st;
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
InteropProviderCleanup(CMPIInstanceMI * mi,
                       const CMPIContext *ctx, CMPIBoolean terminate)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderCleanup");
  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
InteropProviderEnumInstanceNames(CMPIInstanceMI * mi,
                                 const CMPIContext *ctx,
                                 const CMPIResult *rslt,
                                 const CMPIObjectPath * ref)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIEnumeration *enm;
  CMPIContext    *ctxLocal;
  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderEnumInstanceNames");

  if (interOpNameSpace(ref, NULL) != 1)
    _SFCB_RETURN(st);
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
InteropProviderEnumInstances(CMPIInstanceMI * mi,
                             const CMPIContext *ctx,
                             const CMPIResult *rslt,
                             const CMPIObjectPath * ref,
                             const char **properties)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIEnumeration *enm;
  CMPIContext    *ctxLocal;
  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderEnumInstances");

  if (interOpNameSpace(ref, NULL) != 1)
    _SFCB_RETURN(st);
  ctxLocal = prepareUpcall((CMPIContext *) ctx);
  enm =
      _broker->bft->enumerateInstances(_broker, ctxLocal, ref, properties,
                                       &st);
  CMRelease(ctxLocal);

  while (enm && enm->ft->hasNext(enm, &st)) {

    CMPIInstance* ci = (enm->ft->getNext(enm, &st)).value.inst;

    /* need to check IndicationSubscription, since it may contain props used internally by sfcb */
    CMPIObjectPath* cop = CMGetObjectPath(ci, &st);
    if (strcasecmp(CMGetCharPtr(CMGetClassName(cop, NULL)), "cim_indicationsubscription") == 0) {
      filterInternalProps(ci);
    }

    CMReturnInstance(rslt, ci);
  }
  if (enm)
    CMRelease(enm);
  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
InteropProviderGetInstance(CMPIInstanceMI * mi,
                           const CMPIContext *ctx,
                           const CMPIResult *rslt,
                           const CMPIObjectPath * cop,
                           const char **properties)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIContext    *ctxLocal;
  CMPIInstance   *ci;

  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderGetInstance");

  ctxLocal = prepareUpcall((CMPIContext *) ctx);

  ci = _broker->bft->getInstance(_broker, ctxLocal, cop, properties, &st);
  if (st.rc == CMPI_RC_OK) {

    /* need to check IndicationSubscription, since it may contain props used internally by sfcb */
    /* To get the filtered properties, use internalprovider GetInstance */
    if (strcasecmp(CMGetCharPtr(CMGetClassName(cop, NULL)), "cim_indicationsubscription") == 0) {
      filterInternalProps(ci);
    }

    CMReturnInstance(rslt, ci);
  }

  CMRelease(ctxLocal);

  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
InteropProviderCreateInstance(CMPIInstanceMI * mi,
                              const CMPIContext *ctx,
                              const CMPIResult *rslt,
                              const CMPIObjectPath * cop,
                              const CMPIInstance *ci)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIString     *cn = CMGetClassName(cop, NULL);
  const char     *cns = cn->ft->getCharPtr(cn, NULL);
  CMPIString     *ns = CMGetNameSpace(cop, NULL);
  const char     *nss = ns->ft->getCharPtr(ns, NULL);
  CMPIContext    *ctxLocal;
  CMPIInstance   *ciLocal;
  CMPIObjectPath *copLocal;
  CMPIValue       valSNS;

  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderCreateInstance");

  if (interOpNameSpace(cop, &st) != 1)
    _SFCB_RETURN(st);

  ciLocal = ci->ft->clone(ci, NULL);
  memLinkInstance(ciLocal);
  copLocal = cop->ft->clone(cop, NULL);
  memLinkObjectPath(copLocal);

  if (isa(nss, cns, "cim_indicationsubscription")) {
    _SFCB_TRACE(1, ("--- create cim_indicationsubscription"));

    st = verify_subscription(ctx, cop, ci); /* 3495060 */
    if (st.rc != CMPI_RC_OK) _SFCB_RETURN(st);

    st = processSubscription(_broker, ctx, ciLocal, copLocal);
  } else if (isa(nss, cns, "cim_indicationfilter")) {

    setCCN(copLocal,ciLocal,"CIM_ComputerSystem");
    QLStatement    *qs = NULL;
    int             rc,
                    i,
                    n,
                    m;
    char           *key = NULL,
        *ql,
        lng[16];
    CMPIString     *lang =
        ciLocal->ft->getProperty(ciLocal, "querylanguage",
                                 &st).value.string;
    CMPIString     *query =
        ciLocal->ft->getProperty(ciLocal, "query", &st).value.string;
    CMPIBoolean     iss = ciLocal->ft->getProperty(ciLocal,
                                                   "individualsubscriptionsupported",
                                                   &st).value.boolean;
    CMPIValue       iss_default = {.boolean = 1 };

    _SFCB_TRACE(1, ("--- create cim_indicationfilter"));

    /*
     * This property new as of 2.22.  Really only useful for
     * FilterCollections. If FCs are implemented, this will need updating. 
     */
    if (st.rc == CMPI_RC_OK && iss == 0) {
      setStatus(&st, CMPI_RC_ERR_NOT_SUPPORTED,
                "IndividualSubscriptionSupported property must be TRUE (FilterCollections not available)");
      _SFCB_RETURN(st);
    } else {
      CMSetProperty(ciLocal, "IndividualSubscriptionSupported",
                    &iss_default, CMPI_boolean);
    }

    if (lang == NULL || lang->hdl == NULL
        || query == NULL || query->hdl == NULL) {
      setStatus(&st, CMPI_RC_ERR_FAILED,
                "Query and/or Language property not found");
      _SFCB_RETURN(st);
    }

    CMPIString     *sns =
        ciLocal->ft->getProperty(ciLocal, "SourceNamespace",
                                 &st).value.string;
    CMPIArray      *snsa =
        ciLocal->ft->getProperty(ciLocal, "SourceNamespaces",
                                 &st).value.array;
    if ((sns == NULL || sns->hdl == NULL)
        && (snsa == NULL || snsa->hdl == NULL)) {
      /*
       * if sourcenamespace and sourcenamespaces are both NULL, the
       * namespace of the filter registration is assumed 
       */
      sns = sfcb_native_new_CMPIString("root/interop", NULL, 0);
      valSNS.string = sns;
      ciLocal->ft->setProperty(ciLocal, "SourceNamespace", &valSNS,
                               CMPI_string);
      CMSetStatus(&st, CMPI_RC_OK);
    } else if (sns == NULL || sns->hdl == NULL) {
      // Sourcenamespaces is set, but sourcenamespace is not, put the 1st
      // entry of
      // SourceNamespaces in Sourcenamespace
      sns = CMGetArrayElementAt(snsa, 0, NULL).value.string;
      valSNS.string = sns;
      ciLocal->ft->setProperty(ciLocal, "SourceNamespace", &valSNS,
                               CMPI_string);
      valSNS.array = snsa;
      ciLocal->ft->setProperty(ciLocal, "SourceNamespaces", &valSNS, CMPI_stringA);
      CMSetStatus(&st, CMPI_RC_OK);
    }
    // If SourceNamespaces isn't set, use the SourceNamespace
    if (snsa == NULL || snsa->hdl == NULL) {
      CMPIArray      *snsa =
          internal_new_CMPIArray(MEM_TRACKED, 1, CMPI_string, &st);
      valSNS.string = sns;
      CMSetArrayElementAt(snsa, 0, &valSNS, CMPI_string);
      valSNS.array = snsa;
      ciLocal->ft->setProperty(ciLocal, "SourceNamespaces", &valSNS,
                               CMPI_stringA);
      CMSetStatus(&st, CMPI_RC_OK);
    }

    /* SystemName is a key property.  According to DSP1054, the CIMOM must
       provide this if the client does not */
    CMPIString *sysname=ciLocal->ft->getProperty(ciLocal,"SystemName",&st).value.string;
    if (sysname == NULL || sysname->hdl == NULL) {
      char hostName[512];
      hostName[0]=0;
      gethostname(hostName,511); /* should be the same as SystemName of IndicationService */
      CMAddKey(copLocal, "SystemName", hostName, CMPI_chars);
      CMSetProperty(ciLocal,"SystemName",hostName,CMPI_chars);
      CMSetStatus(&st, CMPI_RC_OK);
    }

    for (ql = (char *) lang->hdl, i = 0, n = 0, m = strlen(ql); i < m; i++) {
      if (ql[i] > ' ')
        lng[n++] = ql[i];
      if (n >= 15)
        break;
    }
    lng[n] = 0;

    _SFCB_TRACE(2, ("--- CIM query language %s %s", lang->hdl, lng));
    if (strcasecmp(lng, "wql") && strcasecmp(lng, "cql")
        && strcasecmp(lng, "cim:cql")) {
      setStatus(&st, CMPI_RC_ERR_QUERY_LANGUAGE_NOT_SUPPORTED, NULL);
      _SFCB_RETURN(st);
    }

    key = normalizeObjectPathCharsDup(copLocal);
    if (getFilter(key)) {
      free(key);
      setStatus(&st, CMPI_RC_ERR_ALREADY_EXISTS, NULL);
      _SFCB_RETURN(st);
    }

    qs = parseQuery(MEM_NOT_TRACKED, (char *) query->hdl, lng,
                    (char *) sns->hdl, snsa, &rc);
    if (rc) {
      free(key);
      setStatus(&st, CMPI_RC_ERR_INVALID_QUERY, "Query parse error");
      CMRelease(qs);
      _SFCB_RETURN(st);
    }

    addFilter(ciLocal, key, qs, (char *) query->hdl, lng,
              (char *) sns->hdl, snsa);
  }

  else {
    setStatus(&st, CMPI_RC_ERR_NOT_SUPPORTED, "Class not supported");
    _SFCB_RETURN(st);
  }

  if (st.rc == CMPI_RC_OK) {
    ctxLocal = prepareUpcall((CMPIContext *) ctx);
    CMReturnObjectPath(rslt,
                       _broker->bft->createInstance(_broker, ctxLocal,
                                                    copLocal, ciLocal,
                                                    &st));
    sfcbIndAuditLog("CreateInstance-> ", 
                    CMGetCharPtr(CMObjectPathToString(cop, NULL)));
    CMRelease(ctxLocal);
  }

  _SFCB_RETURN(st);
}

/*
 * ModifyInstance only for IndicationSubscription.SubscriptionState
 */

CMPIStatus
InteropProviderModifyInstance(CMPIInstanceMI * mi,
                              const CMPIContext *ctx,
                              const CMPIResult *rslt,
                              const CMPIObjectPath * cop,
                              const CMPIInstance *ci,
                              const char **properties)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIString     *cn = CMGetClassName(cop, NULL);
  const char     *cns = cn->ft->getCharPtr(cn, NULL);
  CMPIContext    *ctxLocal;

  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderModifyInstance");

  if (isa("root/interop", cns, "cim_indicationsubscription")) {
    char           *key = normalizeObjectPathCharsDup(cop);
    _SFCB_TRACE(1, ("--- modify cim_indicationsubscription %s", key));
    Subscription   *su;
    CMPIInstance   *oldInst;

    /*
     * check if SubscriptionState changed enable/disableIndication 
     */
    pthread_mutex_lock(&subHTlock);
    su = getSubscription(key);
    free(key);
    if (!su) {
      st.rc = CMPI_RC_ERR_NOT_FOUND;
      pthread_mutex_unlock(&subHTlock);
      return st;
    }
    oldInst = su->sci;

    CMPIData        oldState =
        CMGetProperty(oldInst, "SubscriptionState", &st);
    CMPIData        newState = CMGetProperty(ci, "SubscriptionState", &st);

    if (newState.state == CMPI_goodValue) {
      if (newState.value.uint16 == 2 && oldState.value.uint16 != 2) {
        // Check if we've hit the max before we actvate
        long cfgmax;
        getControlNum("MaxActiveSubscriptions", &cfgmax);
        if (AScount+1 > cfgmax) {
            setStatus(&st,CMPI_RC_ERR_FAILED,"Subscription activation would exceed MaxActiveSubscription limit");
            pthread_mutex_unlock(&subHTlock);
            return st;
        }
        switchIndications(ctx, ci, OPS_EnableIndications);
        AScount++;
      } else if (newState.value.uint16 == 4 && oldState.value.uint16 != 4) {
        switchIndications(ctx, ci, OPS_DisableIndications);
        AScount--;
      }
    }
    /*
     * replace the instance in the hashtable
     */
    CMRelease(su->sci);
    su->sci = CMClone(ci, NULL);
    pthread_mutex_unlock(&subHTlock);

  } else if (isa("root/interop", cns, "cim_listenerdestination")) {
    char *key = normalizeObjectPathCharsDup(cop);
    _SFCB_TRACE(1,("--- modify cim_indicationsubscription %s",key));
    Handler *ha;
                 
    ha = getHandler(key);
    free(key);
    if(!ha) {
      st.rc = CMPI_RC_ERR_NOT_FOUND;
      return st;        
    }
    CMPIData newDest = CMGetProperty(ci, "Destination", &st);
                
    if(newDest.state != CMPI_goodValue) {
      st.rc = CMPI_RC_ERR_FAILED;
      return st;        
    }
    /*replace the instance in the hashtable*/
    CMRelease(ha->hci);
    ha->hci=CMClone(ci,NULL);
          
  }
  else setStatus(&st,CMPI_RC_ERR_NOT_SUPPORTED,"ModifyInstance for class not supported");

  if (st.rc == CMPI_RC_OK) {
    ctxLocal = prepareUpcall((CMPIContext *) ctx);
    st = _broker->bft->modifyInstance(_broker, ctxLocal, cop, ci,
                                      properties);
    sfcbIndAuditLog("Subscription:ModifyInstance-> ", 
                    CMGetCharPtr(CMObjectPathToString(cop, NULL)));
    CMRelease(ctxLocal);
  }
  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
InteropProviderDeleteInstance(CMPIInstanceMI * mi,
                              const CMPIContext *ctx,
                              const CMPIResult *rslt,
                              const CMPIObjectPath * cop)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIString     *cn = CMGetClassName(cop, NULL);
  const char     *cns = cn->ft->getCharPtr(cn, NULL);
  CMPIString     *ns = CMGetNameSpace(cop, NULL);
  const char     *nss = ns->ft->getCharPtr(ns, NULL);
  char           *key = normalizeObjectPathCharsDup(cop);
  Filter         *fi;
  Subscription   *su;
  CMPIContext    *ctxLocal;

  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderDeleteInstance");

  if (isa(nss, cns, "cim_indicationsubscription")) {
    pthread_mutex_lock(&subDelLock);
    _SFCB_TRACE(1, ("--- delete cim_indicationsubscription %s", key));
    if ((su = getSubscription(key))) {
      fi = su->fi;
      if (fi->useCount == 1) {
        char          **fClasses = fi->qs->ft->getFromClassList(fi->qs);
        for (; *fClasses; fClasses++) {
          char           *principal = ctx->ft->getEntry(ctx, CMPIPrincipal,
                                                        NULL).
              value.string->hdl;
          genericSubscriptionRequest(principal, *fClasses, cns, fi,
                                     OPS_DeactivateFilter, NULL);
        }
      }
      // get current state
      ctxLocal = prepareUpcall((CMPIContext *)ctx);
      CMPIInstance *ci = _broker->bft->getInstance(_broker, ctxLocal, cop, NULL, NULL);
      CMRelease(ctxLocal);
      CMPIData d = CMGetProperty(ci, "SubscriptionState", &st);
      if (d.state != CMPI_goodValue) {
         // Not given, assume enable
         d.value.uint16=2;
      }
      if(d.value.uint16 == 2) {
         // If this is an active sub, decrement the count
         AScount--;
      }
      removeSubscription(su, key);
    } else
      setStatus(&st, CMPI_RC_ERR_NOT_FOUND, NULL);
    pthread_mutex_unlock(&subDelLock);
  }

  else if (isa(nss, cns, "cim_indicationfilter")) {
    _SFCB_TRACE(1, ("--- delete cim_indicationfilter %s", key));

    if ((fi = getFilter(key))) {
      if (fi->useCount)
        setStatus(&st, CMPI_RC_ERR_FAILED, "Filter in use");
      else
        removeFilter(fi, key);
    } else
      setStatus(&st, CMPI_RC_ERR_NOT_FOUND, NULL);
  }

  else
    setStatus(&st, CMPI_RC_ERR_NOT_SUPPORTED, "Class not supported");

  if (st.rc == CMPI_RC_OK) {
    ctxLocal = prepareUpcall((CMPIContext *) ctx);
    st = _broker->bft->deleteInstance(_broker, ctxLocal, cop);
    sfcbIndAuditLog("DeleteInstance-> ", 
                    CMGetCharPtr(CMObjectPathToString(cop, NULL)));
    CMRelease(ctxLocal);
  }

  if (key)
    free(key);

  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
InteropProviderExecQuery(CMPIInstanceMI * mi,
                         const CMPIContext *ctx,
                         const CMPIResult *rslt,
                         const CMPIObjectPath * cop,
                         const char *lang, const char *query)
{
  CMPIStatus      st = { CMPI_RC_ERR_NOT_SUPPORTED, NULL };
  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderExecQuery");
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
InteropProviderMethodCleanup(CMPIMethodMI * mi,
                             const CMPIContext *ctx, CMPIBoolean terminate)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderMethodCleanup");
  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */


void * sendIndForDelivery(void *di) {

  _SFCB_ENTER(TRACE_INDPROVIDER, "sendIndForDelivery");

  DeliveryInfo* delInfo;
  delInfo = (DeliveryInfo*)di;
  CBInvokeMethod(_broker,delInfo->ctx,delInfo->hop,"_deliver",delInfo->hin,NULL,NULL);
  sleep(5);
  CMRelease((CMPIContext*)delInfo->ctx);
  CMRelease(delInfo->hop);
  CMRelease(delInfo->hin);
  free(di);
  sem_post(&availThreadsSem);
  pthread_exit(NULL);
}


CMPIStatus
InteropProviderInvokeMethod(CMPIMethodMI * mi,
                            const CMPIContext *ctx,
                            const CMPIResult *rslt,
                            const CMPIObjectPath * ref,
                            const char *methodName,
                            const CMPIArgs * in, CMPIArgs * out)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIStatus      fn_st = { CMPI_RC_ERR_FAILED, NULL};

  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderInvokeMethod");

  if (interOpNameSpace(ref, &st) != 1)
    _SFCB_RETURN(st);

  _SFCB_TRACE(1, ("--- Method: %s", methodName));

  if (strcasecmp(methodName, "_deliver") == 0) {
    HashTableIterator *i;
    Subscription   *su;
    char           *suName;
    char           *filtername = NULL;
    CMPIArgs       *hin = CMNewArgs(_broker, NULL);
    CMPIInstance   *indo = CMGetArg(in, "indication", NULL).value.inst;
    CMPIInstance   *ind = CMClone(indo, NULL); 
    void           *filterId =
        (void *) CMGetArg(in, "filterid", NULL).value.
#if SIZEOF_INT == SIZEOF_VOIDP
        uint32;
#else
        uint64;
#endif
    char           *ns =
        (char *) CMGetArg(in, "namespace", NULL).value.string->hdl;

    pthread_t ind_thread;
    pthread_attr_t it_attr;

    // Add indicationFilterName to the indication
    Filter *filter = filterId;
    CMPIData cd_name = CMGetProperty(filter->fci, "name", &fn_st);
    if (fn_st.rc == CMPI_RC_OK) {
      filtername = cd_name.value.string->hdl;
      _SFCB_TRACE(1,("--- %s: filter=%p, filter->sns=%s, filter->name=%s, filter namespace: %s", __FUNCTION__, filter, filter->sns, filtername, ns));
      fn_st = CMSetProperty(ind, "IndicationFilterName", filtername, CMPI_chars);
      if (fn_st.rc != CMPI_RC_OK) {
        _SFCB_TRACE(1,("--- %s: failed to add IndicationFilterName = %s rc=%d", __FUNCTION__, filtername, fn_st.rc));
      }
    }
    CMAddArg(hin, "indication", &ind, CMPI_instance);
    CMRelease(ind);
    CMAddArg(hin, "nameSpace", ns, CMPI_chars);

    pthread_mutex_lock(&subHTlock);
    if (subscriptionHt)
      for (i = subscriptionHt->ft->getFirst(subscriptionHt,
                                            (void **) &suName,
                                            (void **) &su); i;
           i =
           subscriptionHt->ft->getNext(subscriptionHt, i,
                                       (void **) &suName, (void **) &su)) {
        if ((void *) su->fi == filterId) {
          CMPIString     *str = CDToString(_broker, su->ha->hop, NULL);
          CMPIString     *ns = CMGetNameSpace(su->ha->hop, NULL);
          _SFCB_TRACE(1,
                      ("--- invoke handler %s %s", (char *) ns->hdl,
                       (char *) str->hdl));
          CMAddArg(hin, "subscription", &su->sci, CMPI_instance);

          pthread_attr_init(&it_attr);
          pthread_attr_setdetachstate(&it_attr, PTHREAD_CREATE_DETACHED);
          
          DeliveryInfo* di = malloc(sizeof(DeliveryInfo));
          di->ctx = native_clone_CMPIContext(ctx);
          di->hop = CMClone(su->ha->hop, NULL);
          di->hin = CMClone(hin, NULL);
	  if (IND_THREAD_TO > 0) {
	    availThreadWait.tv_sec = time(NULL) + IND_THREAD_TO;
	    while ((sem_timedwait(&availThreadsSem, &availThreadWait)) == -1) {
	      if (errno == ETIMEDOUT) {
		mlogf(M_ERROR,M_SHOW,"Timedout waiting to create indication delivery thread; dropping indication\n");
		break;
	      }
	      else   /* probably EINTR */
		continue;
	    }
	  }
	  else {
	    sem_wait(&availThreadsSem);
	  }
	  int pcrc = pthread_create(&ind_thread, &it_attr,&sendIndForDelivery,(void *) di);
	  
	  _SFCB_TRACE(1,("--- indication delivery thread status: %d", pcrc));
	  if (pcrc) 
	    mlogf(M_ERROR,M_SHOW,"pthread_create() failed for indication delivery thread\n");
        }
      }
    pthread_mutex_unlock(&subHTlock);
  }

  else if (strcasecmp(methodName, "_addHandler") == 0) {
    // check destination count
    long cfgmax;
    getControlNum("MaxListenerDestinations", &cfgmax);
    if (LDcount+1 > cfgmax) {
      setStatus(&st,CMPI_RC_ERR_FAILED,"Instance creation would exceed MaxListenerDestinations limit");
      _SFCB_RETURN(st);
    }
    LDcount++;

    CMPIInstance   *ci = in->ft->getArg(in, "handler", &st).value.inst;
    CMPIObjectPath *op = in->ft->getArg(in, "key", &st).value.ref;
    CMPIString     *str = CDToString(_broker, op, NULL);
    CMPIString     *ns = CMGetNameSpace(op, NULL);
    _SFCB_TRACE(1,
                ("--- _addHandler %s %s", (char *) ns->hdl,
                 (char *) str->hdl));
    addHandler(ci, op);
    sfcbIndAuditLog("CreateHandler-> ",  
                    CMGetCharPtr(CMObjectPathToString(op, NULL)));
  }

  else if (strcasecmp(methodName, "_removeHandler") == 0) {
    CMPIObjectPath *op = in->ft->getArg(in, "key", &st).value.ref;
    char           *key = normalizeObjectPathCharsDup(op);
    Handler        *ha = getHandler(key);
    if (ha) {
      if (ha->useCount) {
        setStatus(&st, CMPI_RC_ERR_FAILED, "Handler in use");
      } else
        removeHandler(ha, key);
        LDcount--;
        sfcbIndAuditLog("RemoveHandler-> ", 
                        CMGetCharPtr(CMObjectPathToString(op, NULL)));
    } else {
      setStatus(&st, CMPI_RC_ERR_NOT_FOUND, "Handler object not found");
    }
    if (key)
      free(key);
  }

  else if (strcasecmp(methodName, "_updateHandler") == 0) {
    CMPIInstance *ci=in->ft->getArg(in,"handler",&st).value.inst;
    CMPIObjectPath *op=in->ft->getArg(in,"key",&st).value.ref;
    CMPIString *str=CDToString(_broker,op,NULL);
    CMPIString *ns=CMGetNameSpace(op,NULL);
    _SFCB_TRACE(1,("--- _updateHandler %s %s",(char*)ns->hdl,(char*)str->hdl));
    updateHandler(ci,op);     
  }

  else if (strcasecmp(methodName, "_startup") == 0) {
    initInterOp(_broker, ctx);
    /* let httpAdapter know that he can continue */
    semRelease(sfcbSem,INIT_PROV_MGR_ID);
  }

  else {
    _SFCB_TRACE(1, ("--- Invalid request method: %s", methodName));
    setStatus(&st, CMPI_RC_ERR_METHOD_NOT_FOUND, "Invalid request method");
  }

  _SFCB_RETURN(st);
}

/*
 * --------------------------------------------------------------------------
 */
/*
 * Association Provider Interface 
 */
/*
 * --------------------------------------------------------------------------
 */

CMPIStatus
InteropProviderAssociationCleanup(CMPIAssociationMI * mi,
                                  const CMPIContext *ctx,
                                  CMPIBoolean terminate)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderAssociationCleanup");
  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
InteropProviderAssociators(CMPIAssociationMI * mi,
                           const CMPIContext *ctx,
                           const CMPIResult *rslt,
                           const CMPIObjectPath * cop,
                           const char *assocClass,
                           const char *resultClass,
                           const char *role,
                           const char *resultRole,
                           const char **propertyList)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIEnumeration *enm;
  CMPIContext    *ctxLocal;

  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderAssociators");

  if (interOpNameSpace(cop, &st) != 1)
    _SFCB_RETURN(st);

  ctxLocal = prepareUpcall((CMPIContext *) ctx);
  enm =
      _broker->bft->associators(_broker, ctxLocal, cop, assocClass,
                                resultClass, role, resultRole,
                                propertyList, &st);
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
InteropProviderAssociatorNames(CMPIAssociationMI * mi,
                               const CMPIContext *ctx,
                               const CMPIResult *rslt,
                               const CMPIObjectPath * cop,
                               const char *assocClass,
                               const char *resultClass,
                               const char *role, const char *resultRole)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIEnumeration *enm;
  CMPIContext    *ctxLocal;

  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderAssociatorNames");

  if (interOpNameSpace(cop, &st) != 1)
    _SFCB_RETURN(st);

  ctxLocal = prepareUpcall((CMPIContext *) ctx);
  enm =
      _broker->bft->associatorNames(_broker, ctxLocal, cop, assocClass,
                                    resultClass, role, resultRole, &st);
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
InteropProviderReferences(CMPIAssociationMI * mi,
                          const CMPIContext *ctx,
                          const CMPIResult *rslt,
                          const CMPIObjectPath * cop,
                          const char *resultClass,
                          const char *role, const char **propertyList)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIEnumeration *enm;
  CMPIContext    *ctxLocal;
  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderReferences");

  if (interOpNameSpace(cop, NULL) != 1)
    _SFCB_RETURN(st);
  ctxLocal = prepareUpcall((CMPIContext *) ctx);
  enm =
      _broker->bft->references(_broker, ctxLocal, cop, resultClass, role,
                               propertyList, &st);
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
InteropProviderReferenceNames(CMPIAssociationMI * mi,
                              const CMPIContext *ctx,
                              const CMPIResult *rslt,
                              const CMPIObjectPath * cop,
                              const char *resultClass, const char *role)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CMPIEnumeration *enm;
  CMPIContext    *ctxLocal;
  _SFCB_ENTER(TRACE_INDPROVIDER, "InteropProviderReferenceNames");

  if (interOpNameSpace(cop, NULL) != 1)
    _SFCB_RETURN(st);
  ctxLocal = prepareUpcall((CMPIContext *) ctx);
  enm =
      _broker->bft->referenceNames(_broker, ctxLocal, cop, resultClass,
                                   role, &st);
  CMRelease(ctxLocal);

  while (enm && enm->ft->hasNext(enm, &st)) {
    CMReturnObjectPath(rslt, (enm->ft->getNext(enm, &st)).value.ref);
  }
  if (enm)
    CMRelease(enm);
  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------ *
 * Instance MI Factory NOTE: This is an example using the convenience
 * macros. This is OK as long as the MI has no special requirements, i.e.
 * to store data between calls.
 * ------------------------------------------------------------------ 
 */

CMInstanceMIStub(InteropProvider, InteropProvider, _broker, CMNoHook);

CMAssociationMIStub(InteropProvider, InteropProvider, _broker, CMNoHook);

CMMethodMIStub(InteropProvider, InteropProvider, _broker, CMNoHook);
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
