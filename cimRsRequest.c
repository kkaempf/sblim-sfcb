
/*
 * cimRsRequest.c
 *
 * © Copyright Novell Inc, 2010
 *
 * THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
 * ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
 * CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
 *
 * You can obtain a current copy of the Eclipse Public License from
 * http://www.opensource.org/licenses/eclipse-1.0.php
 *
 * Author:       Klaus Kaempf <kkaempf@suse.de>
 *
 * Description:
 * CIM RS (CIM REST) parser
 *
 */

#include "cmpidt.h"
#include "cmpidtx.h"
#include "cimJsonGen.h"
#include "cimXmlRequest.h"
#include "msgqueue.h"
#include "cmpidt.h"
#include "constClass.h"

#ifdef HAVE_QUALREP
#include "qualifier.h"
#endif

#include "objectImpl.h"

#include "native.h"
#include "trace.h"
#include "utilft.h"
#include "string.h"

#include "queryOperation.h"
#include "config.h"
#include "control.h"

#include "providerMgr.h"

#ifdef LOCAL_CONNECT_ONLY_ENABLE
// from httpAdapter.c
int             noChunking = 0;
#endif                          // LOCAL_CONNECT_ONLY_ENABLE

enum http_method {
  HTTP_GET = 1,
  HTTP_PUT = 2,
  HTTP_POST = 4,
  HTTP_DELETE = 8
};

/*
 * URI operations
 * 
 */

enum cimrs_op {
  OP_NONE = 0,
  OP_NAMESPACES,          /* /namespaces/{namespace} */
  OP_CLASSES,             /* /namespaces/{namespace}/classes[?sc={classname}] */
  OP_CLASS,               /* /namespaces/{namespace}/classes/{classname} */
  OP_CLASS_ASSOCIATORS,   /* /namespaces/{namespace}/classes/{classname}/associators */
  OP_CLASS_REFERENCES,    /* /namespaces/{namespace}/classes/{classname}/references */
  OP_CLASS_METHODS,       /* /namespaces/{namespace}/classes/{classname}/methods/{methodname} */
  OP_INSTANCE,            /* /namespaces/{namespace}/classes/{classname}/instances */
  OP_INSTANCE_ASSOCIATORS,/* /namespaces/{namespace}/classes/{classname}/instances/{keylist}/associators */
  OP_INSTANCE_REFERENCES, /* /namespaces/{namespace}/classes/{classname}/instances/{keylist}/references */
  OP_INSTANCE_METHODS,    /* /namespaces/{namespace}/classes/{classname}/instances/{keylist}/methods/{methodname} */
  OP_QUALIFIERS,          /* /namespaces/{namespace}/qualifiers/{qualifiername} */
  OP_QUERY,               /* /namespaces/{namespace}/query?ql={lang}&qs={query} */
  OP_MAX
};

typedef struct requestHdr {
  int rc;
  const char *msg;
  int code;          /* http response code */
  int allowed;       /* if code == 405, bitmask of methods for 'Allowed:' header */
  enum http_method http_meth;
  char *uri_q;       /* ...?foo= at end of url */
  enum cimrs_op op;  /* operation detected by parsing the uri */

  const char *ns;
  const char *classname;
  const char *keys;
  const char *method;
  const char *query;
  const char *query_lang;
  const char *qualifier;
} RequestHdr;

typedef struct cim_rs_response {
  int rc;
  const char *msg;
  int code; /* http response code */
  int allowed;           /* if code == 405, bitmask of methods for 'Allowed:' header */
  int json;   /* 0: xml, 1: json */
} CimRsResponse;

extern int      noChunking;

extern CMPIBroker *Broker;
extern UtilStringBuffer *newStringBuffer(int s);
extern UtilStringBuffer *instanceToString(CMPIInstance *ci, char **props);
extern const char *getErrorId(int c);
extern const char *instGetClassName(CMPIInstance *ci);

extern CMPIData opGetKeyCharsAt(CMPIObjectPath * cop, unsigned int index,
                                const char **name, CMPIStatus *rc);
extern BinResponseHdr *invokeProvider(BinRequestContext * ctx);
extern CMPIArgs *relocateSerializedArgs(void *area);
extern CMPIObjectPath *relocateSerializedObjectPath(void *area);
extern CMPIInstance *relocateSerializedInstance(void *area);
extern CMPIConstClass *relocateSerializedConstClass(void *area);
extern MsgSegment setInstanceMsgSegment(CMPIInstance *ci);
extern MsgSegment setArgsMsgSegment(CMPIArgs * args);
extern MsgSegment setConstClassMsgSegment(CMPIConstClass * cl);
extern void     closeProviderContext(BinRequestContext * ctx);
extern CMPIStatus arraySetElementNotTrackedAt(CMPIArray *array,
                                              CMPICount index,
                                              CMPIValue * val,
                                              CMPIType type);
extern CMPIConstClass initConstClass(ClClass * cl);
extern CMPIQualifierDecl initQualifier(ClQualifierDeclaration * qual);
extern CMPIString *NewCMPIString(const char *ptr, CMPIStatus *rc);

extern char    *opsName[];

#if 0
static char    *cimMsg[] = {
  "ok",
  "A general error occured that is not covered by a more specific error code",
  "Access to a CIM resource was not available to the client",
  "The target namespace does not exist",
  "One or more parameter values passed to the method were invalid",
  "The specified Class does not exist",
  "The requested object could not be found",
  "The requested operation is not supported",
  "Operation cannot be carried out on this class since it has subclasses",
  "Operation cannot be carried out on this class since it has instances",
  "Operation cannot be carried out since the specified superclass does not exist",
  "Operation cannot be carried out because an object already exists",
  "The specified Property does not exist",
  "The value supplied is incompatible with the type",
  "The query language is not recognized or supported",
  "The query is not valid for the specified query language",
  "The extrinsic Method could not be executed",
  "The specified extrinsic Method does not exist"
};


static char    *
paramType(CMPIType type)
{
  switch (type & ~CMPI_ARRAY) {
  case CMPI_chars:
  case CMPI_string:
  case CMPI_instance:
    return "string";
  case CMPI_sint64:
    return "sint64";
  case CMPI_uint64:
    return "uint64";
  case CMPI_sint32:
    return "sint32";
  case CMPI_uint32:
    return "uint32";
  case CMPI_sint16:
    return "sint16";
  case CMPI_uint16:
    return "uint16";
  case CMPI_uint8:
    return "uint8";
  case CMPI_sint8:
    return "sint8";
  case CMPI_boolean:
    return "boolean";
  case CMPI_char16:
    return "char16";
  case CMPI_real32:
    return "real32";
  case CMPI_real64:
    return "real64";
  case CMPI_dateTime:
    return "datetime";
  case CMPI_ref:
    return "reference";
  }
  mlogf(M_ERROR, M_SHOW, "%s(%d): invalid data type %d %x\n", __FILE__,
        __LINE__, (int) type, (int) type);
  abort();
  return "*??*";
}

#endif

/*
 * check if HTTP method is allowed
 * fill error information in hdr if not
 * 
 */

static int
method_allowed(RequestHdr *hdr, int methods)
{
  if ((hdr->http_meth & methods) == 0) {
    hdr->rc = 1;
    hdr->code = 405;
    hdr->allowed = methods;
    fprintf (stderr,"--- method BAD\n");
    return 0;
  }
  return 1;
}

void
dumpSegments(RespSegment * rs)
{
  int             i;
  if (rs) {
    printf("[");
    for (i = 0; i < 7; i++) {
      if (rs[i].txt) {
        if (rs[i].mode == 2) {
          UtilStringBuffer *sb = (UtilStringBuffer *) rs[i].txt;
          printf("%s", sb->ft->getCharPtr(sb));
        } else
          printf("%s", rs[i].txt);
      }
    }
    printf("]\n");
  }
}

UtilStringBuffer *
segments2stringBuffer(RespSegment * rs)
{
  int             i;
  UtilStringBuffer *sb = newStringBuffer(4096);

  if (rs) {
    for (i = 0; i < 7; i++) {
      if (rs[i].txt) {
        if (rs[i].mode == 2) {
          UtilStringBuffer *sbt = (UtilStringBuffer *) rs[i].txt;
          sb->ft->appendChars(sb, sbt->ft->getCharPtr(sbt));
        } else
          sb->ft->appendChars(sb, rs[i].txt);
      }
    }
  }
  return sb;
}


/*
 * create generic response
 * 
 */

static CimRsResponse *
createRsResponse(int rc, const char *msg, int code)
{
  CimRsResponse *resp = (CimRsResponse *)malloc(sizeof(CimRsResponse));

  resp->rc = rc;
  if (msg) resp->msg = msg;
  resp->code = code;

  return resp;
}


/*
 * create error response
 * 
 */

static CimRsResponse *
ctxErrResponse(RequestHdr * hdr, BinRequestContext * ctx)
{
  CimRsResponse *resp;

  if (ctx->rc) {
    switch (ctx->rc) {
    case MSG_X_NOT_SUPPORTED:
      resp = createRsResponse(CMPI_RC_ERR_NOT_SUPPORTED, "Operation not supported", 501); /* not implemented */
    break;
    case MSG_X_INVALID_CLASS:
    resp = createRsResponse(CMPI_RC_ERR_INVALID_CLASS, "Class not found", 404); /* not found */
    break;
    case MSG_X_INVALID_NAMESPACE:
    resp = createRsResponse(CMPI_RC_ERR_INVALID_NAMESPACE, "Invalid namespace", 400); /* bad request */
    break;
    case MSG_X_PROVIDER_NOT_FOUND:
    resp = createRsResponse(CMPI_RC_ERR_NOT_FOUND, "Provider not found or not loadable", 404); /* not found */
    break;
    case MSG_X_FAILED: {
      MsgXctl        *xd = ctx->ctlXdata;
      resp = createRsResponse(CMPI_RC_ERR_FAILED, xd->data, 500); /* internal error */
      break;
      }
    default: {
      char            msg[256];
      sprintf(msg, "Internal error - %d\n", ctx->rc);
      resp = createRsResponse(CMPI_RC_ERR_FAILED, msg, 500); /* internal error */
      }
    }
  }
  else {
    resp = createRsResponse(0, NULL, 200); /* OK */
  }
  return resp;
};


static UtilStringBuffer *
genEnumResponses(BinRequestContext * binCtx,
                 BinResponseHdr ** resp, int arrLen)
{
  int             i,
                  c,
                  j;
  void           *object;
  CMPIArray      *ar;
  UtilStringBuffer *sb;
  CMPIEnumeration *enm;
  CMPIStatus      rc;

  _SFCB_ENTER(TRACE_CIMRS, "genEnumResponses");

  ar = TrackedCMPIArray(arrLen, binCtx->type, NULL);

  _SFCB_TRACE(1, ("--- array %p, arrLen %d, rcount %d, type %04x", ar, arrLen, binCtx->rCount, binCtx->type));
  for (c = 0, i = 0; i < binCtx->rCount; i++) {
    _SFCB_TRACE(1, ("--- resp[%d] count %d", i, resp[i]->count));
    for (j = 0; j < resp[i]->count; c++, j++) {
      if (binCtx->type == CMPI_ref)
        object = relocateSerializedObjectPath(resp[i]->object[j].data);
      else if (binCtx->type == CMPI_instance)
        object = relocateSerializedInstance(resp[i]->object[j].data);
      else if (binCtx->type == CMPI_string) {
        object = CMNewString(Broker, resp[i]->object[j].data, NULL);
      }
      else if (binCtx->type == CMPI_class)
        object = relocateSerializedConstClass(resp[i]->object[j].data);
      else
	_SFCB_TRACE(1, ("--- *** unhandled"));
        
      rc = arraySetElementNotTrackedAt(ar, c, (CMPIValue *) & object,
                                       binCtx->type);
    }
  }

  /* Array -> Enumeration */
  enm = sfcb_native_new_CMPIEnumeration(ar, NULL);
  sb = UtilFactory->newStrinBuffer(1024);

  if (binCtx->oHdr->type == OPS_EnumerateClassNames)
    enum2json(enm, sb, binCtx->type, "classname", binCtx->bHdr->flags);
  else if (binCtx->oHdr->type == OPS_EnumerateClasses)
    enum2json(enm, sb, binCtx->type, "class", binCtx->bHdr->flags);
  else if (binCtx->oHdr->type == OPS_EnumerateNamespaces)
    enum2json(enm, sb, binCtx->type, "namespaces", binCtx->bHdr->flags);
  else
    enum2json(enm, sb, binCtx->type, "enum", binCtx->bHdr->flags);

  _SFCB_RETURN(sb);
}

static CimRsResponse*
genResponses(BinRequestContext * binCtx,
             BinResponseHdr ** resp, int arrlen)
{
  CimRsResponse    *rs;
  UtilStringBuffer *sb;
  void           *genheap;
#ifdef SFCB_DEBUG
  struct rusage   us,
                  ue;
  struct timeval  sv,
                  ev;

  if (_sfcb_trace_mask & TRACE_RESPONSETIMING) {
    gettimeofday(&sv, NULL);
    getrusage(RUSAGE_SELF, &us);
  }
#endif

  _SFCB_ENTER(TRACE_CIMRS, "genResponses");

  genheap = markHeap();
  sb = genEnumResponses(binCtx, resp, arrlen);

  rs = createRsResponse(0, sb->ft->getCharPtr(sb), 200);
#ifdef SFCB_DEBUG
  if (_sfcb_trace_mask & TRACE_RESPONSETIMING) {
    gettimeofday(&ev, NULL);
    getrusage(RUSAGE_SELF, &ue);
    _sfcb_trace(1, __FILE__, __LINE__,
                _sfcb_format_trace
                ("-#- XML Enum Response Generation %.5u %s-%s real: %f user: %f sys: %f \n",
                 binCtx->bHdr->sessionId, opsName[binCtx->bHdr->operation],
                 binCtx->oHdr->className.data, timevalDiff(&sv, &ev),
                 timevalDiff(&us.ru_utime, &ue.ru_utime),
                 timevalDiff(&us.ru_stime, &ue.ru_stime)));
  }
#endif
  releaseHeap(genheap);
  _SFCB_RETURN(rs);
  // _SFCB_RETURN(iMethodResponse(binCtx->rHdr, sb));
}

#if 0
#ifdef HAVE_QUALREP
static          CimRsResponse*
genQualifierResponses(BinRequestContext * binCtx, BinResponseHdr * resp)
{
  CimRsResponse*    rs;
  UtilStringBuffer *sb;
  CMPIArray      *ar;
  int             j;
  CMPIEnumeration *enm;
  void           *object;
  CMPIStatus      rc;
  void           *genheap;

  _SFCB_ENTER(TRACE_CIMRS, "genQualifierResponses");
  genheap = markHeap();
  ar = TrackedCMPIArray(resp->count, binCtx->type, NULL);

  for (j = 0; j < resp->count; j++) {
    object = relocateSerializedQualifier(resp->object[j].data);
    rc = arraySetElementNotTrackedAt(ar, j, (CMPIValue *) & object,
                                     binCtx->type);
  }

  enm = sfcb_native_new_CMPIEnumeration(ar, NULL);
  sb = UtilFactory->newStrinBuffer(1024);
/*FIXME
  qualiEnum2xml(enm, sb);
 */
  rs = createRsResponse(0, (char *)sb, 200);
  releaseHeap(genheap);
  _SFCB_RETURN(rs);
}
#endif
#endif

CimRsResponse*
genFirstChunkResponses(BinRequestContext * binCtx,
                       BinResponseHdr ** resp, int arrlen, int moreChunks)
{
  UtilStringBuffer *sb;

  sb = genEnumResponses(binCtx, resp, arrlen);

  return createRsResponse(0, (char *)sb, 200);
}

CimRsResponse*
genChunkResponses(BinRequestContext * binCtx,
                  BinResponseHdr ** resp, int arrlen)
{
  return createRsResponse(0, (char *) genEnumResponses(binCtx, resp, arrlen), 200);

}

CimRsResponse*
genLastChunkResponses(BinRequestContext * binCtx,
                      BinResponseHdr ** resp, int arrlen)
{
  UtilStringBuffer *sb;

  sb = genEnumResponses(binCtx, resp, arrlen);

  return createRsResponse(0, (char *)sb, 200);
}

CimRsResponse*
genFirstChunkErrorResponse(BinRequestContext * binCtx, int rc, char *msg)
{
  return createRsResponse(rc, msg, 500);
}

/*==================================================================*/

/*
 * opNamespaces()
 * 
 * operate on namespaces
 * 
 * hdr: request header (contains op)
 * 
 * returns: CimRsResponse if ok
 *          NULL on error (fills error information in hdr)
 * 
 */

static CimRsResponse*
opNamespaces(RequestHdr * hdr, CimXmlRequestContext * ctx )
{
  BinRequestContext binCtx;
  OperationHdr ophdr;
  int irc;
  EnumNamespacesReq sreq = BINREQ(OPS_EnumerateNamespaces, 2);

  _SFCB_ENTER(TRACE_CIMRS, "opNamespaces");
  _SFCB_TRACE(1, ("--- method %04x", hdr->http_meth));
	
  if (!method_allowed(hdr, HTTP_GET)) {
    _SFCB_RETURN(NULL);
  }
  memset(&binCtx, 0, sizeof(BinRequestContext));

  _SFCB_TRACE(1, ("--- method good"));

  hdr->code = 200;
	
  /*
   * Set up incoming req information
   */

  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.hdr.flags = 0;
  sreq.hdr.sessionId = ctx->sessionId;

  ophdr.type = sreq.hdr.operation;
  ophdr.options = 0;
  ophdr.count = 0;

  ophdr.nameSpace.data = (char *)hdr->ns;
  ophdr.nameSpace.type = CMPI_string;
  ophdr.nameSpace.length = hdr->ns ? strlen(hdr->ns) : 0;
  ophdr.className.data = NULL;
  ophdr.className.type = 0;
  ophdr.className.length = 0;

  sreq.ns = setCharsMsgSegment(ophdr.nameSpace.data);

  binCtx.oHdr = &ophdr;
  binCtx.bHdr = &sreq.hdr;
  binCtx.bHdr->flags = 0;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.commHndl = ctx->commHndl;
  binCtx.type = CMPI_string;
  binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.chunkedMode = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, &ophdr);

  _SFCB_TRACE(1, ("--- Provider context gotten: %d", irc));

  if (irc == MSG_X_PROVIDER) {
    BinResponseHdr **resp;
    CimRsResponse *rs;
    int l = 0, err = 0;

    resp = invokeProviders(&binCtx, &err, &l);
    if (err == 0)
      rs = genResponses(&binCtx, resp, l);
    else {
      rs = NULL;
      _SFCB_TRACE(1, ("--- Error getting namespaces"));
    }
    closeProviderContext(&binCtx);

    _SFCB_RETURN(rs);
  }

  closeProviderContext(&binCtx);
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}


/*
 * opClasses()
 * 
 * operate on classes
 * 
 * hdr: request header (contains op)
 * 
 * returns: CimRsResponse if ok
 *          NULL on error (fills error information in hdr)
 * 
 */

static CimRsResponse*
opClasses(RequestHdr * hdr, CimXmlRequestContext * ctx)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "opClasses");
  if (!method_allowed(hdr, HTTP_GET)) {
    _SFCB_RETURN(NULL);
  }
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  EnumClassNamesReq sreq = BINREQ(OPS_EnumerateClassNames, 2);
  int             irc,
                  l = 0,
      err = 0;
  BinResponseHdr **resp;
  CimRsResponse*    rs;

  XtokEnumClassNames *req = (XtokEnumClassNames *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  sreq.objectPath = setObjectPathMsgSegment(path);
  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.hdr.flags = req->flags;
  sreq.hdr.sessionId = ctx->sessionId;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.bHdr->flags = req->flags;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.commHndl = ctx->commHndl;
  binCtx.type = CMPI_ref;
  binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.chunkedMode = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    _SFCB_TRACE(1, ("--- Calling Providers"));
    resp = invokeProviders(&binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Provider"));
    closeProviderContext(&binCtx);
    if (err == 0) {
      rs = genResponses(&binCtx, resp, l);
    } else {
      rs = iMethodErrResponse(hdr, getErrSegment(resp[err - 1]->rc,
                                                 (char *) resp[err -
                                                               1]->object
                                                 [0].data));
    }
    freeResponseHeaders(resp, &binCtx);
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

/*==================================================================*/

/*
 * extract element from url path
 * and advance *next ptr to end of element+1
 * 
 */

static const char *
urlelement(char *path, char **next)
{
  char *slash;
  if (path != NULL
      && *path != '\0') {
    slash = strchr(path, '/');
    if (slash) *slash++ = '\0';
  }
  else {
    slash = NULL;
  }
  if (next) *next = slash;
  return path;
}


static RequestHdr *
scanCimRsRequest(const char *method, char *path)
{
  RequestHdr *hdr = (RequestHdr *)calloc(1, sizeof(RequestHdr));

  const char *e; /* url element */
  char *next; /* ptr to next (slash-separated) element in path */
	
  _SFCB_ENTER(TRACE_CIMRS, "scanCimRsRequest");

  _SFCB_TRACE(1, ("--- method '%s', path '%s'", method, path));

  hdr->rc = 0;
  hdr->op = OP_NONE;
	
  /*
   * check the method
   * 
   */

  if (strcasecmp(method, "GET") == 0) {
    hdr->http_meth = HTTP_GET;
  }
  else if (strcasecmp(method, "PUT") == 0) {
    hdr->http_meth = HTTP_PUT;
  }
  else if (strcasecmp(method, "POST") == 0) {
    hdr->http_meth = HTTP_POST;
  }
  else if (strcasecmp(method, "DELETE") == 0) {
    hdr->http_meth = HTTP_DELETE;
  }
  else {
    method_allowed(hdr, HTTP_GET|HTTP_PUT|HTTP_POST|HTTP_DELETE);
    _SFCB_RETURN(hdr);
  }


  /*
   * split off the query
   * 
   */

  hdr->uri_q = strchr(path, '?');
  if (hdr->uri_q) *hdr->uri_q++ = '\0';

  /*
   * parse the path
   * 
   */

  if (strcasecmp(urlelement(path,&next), "namespaces") != 0) {
    hdr->rc = 1;
    hdr->code = 404;
    hdr->msg = "path must start with /namespaces";
    _SFCB_RETURN(hdr);
  }
  hdr->ns = urlelement(next, &next);

  e = urlelement(next, &next);
  if (e == NULL) {
    hdr->op = OP_NAMESPACES;
  }
  else if (strcasecmp(e, "classes") == 0) {
    hdr->classname = urlelement(next, &next);
    if (hdr->classname == NULL) {
      /*
       * seen /classes
       */
      hdr->op = OP_CLASSES;
    }
    else {
      /*
       * seen /classes/{classname}
       */
      e = urlelement(next, &next);
      if (e == NULL) {
	hdr->op = OP_CLASS;
      }
      else if (strcasecmp(e, "associators") == 0) {
	hdr->op = OP_CLASS_ASSOCIATORS;
      }
      else if (strcasecmp(e, "references") == 0) {
	hdr->op = OP_CLASS_REFERENCES;
      }
      else if (strcasecmp(e, "methods") == 0) {
	hdr->op = OP_CLASS_METHODS;
	hdr->method = urlelement(next, &next);
      }
      else if (strcasecmp(e, "instances") == 0) {
        /*
         * seen /classes/{classname}/instances
         */
        hdr->keys = urlelement(next, &next);
        e = urlelement(next, &next);
	if (e == NULL) {
	  hdr->op = OP_INSTANCE;
	}
        else if (strcasecmp(e, "associators") == 0) {
	  hdr->op = OP_INSTANCE_ASSOCIATORS;
        }
        else if (strcasecmp(e, "references") == 0) {
	  hdr->op = OP_INSTANCE_REFERENCES;
        }
        else if (strcasecmp(e, "methods") == 0) {
	  hdr->op = OP_INSTANCE_METHODS;
	  hdr->method = urlelement(next, &next);
        }
        else {
	  hdr->rc = 1;
          hdr->code = 404;
	  hdr->msg = "Unknown operation on instance";
        }
      }
      else {
	hdr->rc = 1;
        hdr->code = 404;
	hdr->msg = "Unknown operation on class";
      }
    }
  }
#ifdef HAVE_QUALREP
  else if (strcasecmp(e, "qualifiers") == 0) {
    hdr->qualifier = urlelement(next, &next);
    hdr->op = OP_QUALIFIERS;
  }
#endif
  else if (strcasecmp(e, "query") == 0) {
    hdr->classname = urlelement(next, &next);
    hdr->op = OP_QUERY;
  }
  else {
    _SFCB_TRACE(1, ("--- path unrecognized", path));
    hdr->rc = 1;
    hdr->code = 404;
  }

  _SFCB_RETURN(hdr);
}


static void
writeResponse(CimXmlRequestContext *ctx, CimRsResponse *resp)
{

  static char     cont[] =
      { "Content-Type: application/xml; charset=\"utf-8\"\r\n" };
  static char     cach[] = { "Cache-Control: no-cache\r\n" };
  static char     cclose[] = "Connection: close\r\n";
  static char     end[] = { "\r\n" };
  char            str[256];
  int             len;

  struct commHndl conn_fd = *ctx->commHndl;

  _SFCB_ENTER(TRACE_CIMRS, "writeResponse");

  if (resp->msg)
    len = strlen(resp->msg);
  else
    len = 0;
  _SFCB_TRACE(1, ("--- msg len %d", len));

  sprintf(str, "HTTP/1.1 %d OK\r\n", resp->code);
  commWrite(conn_fd, str, strlen(str));
  _SFCB_TRACE(1, ("---> %s", str));

  if (resp->code == 405) {
    char out[256];
    char *o = out;
    struct allowed { int code; char *method; } a[] = {
      { HTTP_GET, "GET" },
      { HTTP_PUT, "PUT" },
      { HTTP_POST, "POST" },
      { HTTP_DELETE, "DELETE" },
      { 0, NULL }	    
    };
    struct allowed *b;
    b = a;
    while (b->code) {
      if (b->code & resp->allowed) {
	if (o > out) *o++ = ',';
	strcpy(o, b->method);
	o += strlen(b->method);
      }
      b++;
    }
    *o++ = '\0';
    sprintf(str, "Allowed: %s\r\n", out);
    commWrite(conn_fd, str, strlen(str));
    _SFCB_TRACE(1, ("---> %s", str));
  }
     
  commWrite(conn_fd, cont, strlen(cont));
  sprintf(str, "Content-Length: %d\r\n", len);
  commWrite(conn_fd, str, strlen(str));
  commWrite(conn_fd, cach, strlen(cach));
  if (ctx->keepaliveTimeout == 0 || ctx->numRequest >= ctx->keepaliveMaxRequest) {
    commWrite(conn_fd, cclose, strlen(cclose));
  }
  commWrite(conn_fd, end, strlen(end));

  if (resp->msg) {
     _SFCB_TRACE(1, ("--- msg '%s'", resp->msg));
     commWrite(conn_fd, (char *)resp->msg, strlen(resp->msg));
  }
  commWrite(conn_fd, end, strlen(end));
  commFlush(conn_fd);

  _SFCB_EXIT();
}


void
handleCimRsRequest(CimXmlRequestContext * ctx)
{
  CimRsResponse  *resp;
  RequestHdr     *hdr;

  hdr = scanCimRsRequest(ctx->method, ctx->path);
  if (hdr->rc) {
    resp = createRsResponse(hdr->rc, hdr->msg, hdr->code);
    resp->allowed = hdr->allowed;
  }
  else {
    HeapControl    *hc;

    hc = markHeap();

    switch (hdr->op) {
    case OP_NONE:
      resp = createRsResponse(1, "Bad cim-rs operation", 404);
      break;
    case OP_NAMESPACES:          /* /namespaces/{namespace} */
      resp = opNamespaces(hdr, ctx);
      break;
    case OP_CLASSES:             /* /namespaces/{namespace}/classes[?sc={classname}] */
      resp = opClasses(hdr, ctx);
      break;
    case OP_CLASS:               /* /namespaces/{namespace}/classes/{classname} */
    case OP_CLASS_ASSOCIATORS:   /* /namespaces/{namespace}/classes/{classname}/associators */
    case OP_CLASS_REFERENCES:    /* /namespaces/{namespace}/classes/{classname}/references */
    case OP_CLASS_METHODS:       /* /namespaces/{namespace}/classes/{classname}/methods/{methodname} */
    case OP_INSTANCE:            /* /namespaces/{namespace}/classes/{classname}/instances */
    case OP_INSTANCE_ASSOCIATORS:/* /namespaces/{namespace}/classes/{classname}/instances/{keylist}/associators */
    case OP_INSTANCE_REFERENCES: /* /namespaces/{namespace}/classes/{classname}/instances/{keylist}/references */
    case OP_INSTANCE_METHODS:    /* /namespaces/{namespace}/classes/{classname}/instances/{keylist}/methods/{methodname} */
#ifdef HAVE_QUALREP
    case OP_QUALIFIERS:          /* /namespaces/{namespace}/qualifiers/{qualifiername} */
#endif
    case OP_QUERY:               /* /namespaces/{namespace}/query?ql={lang}&qs={query} */
    default:
      resp = createRsResponse(1, "Unsupported cim-rs operation", 404);
      break;
    }
    releaseHeap(hc);
  }

  if (resp == NULL) {
    resp = createRsResponse(hdr->rc, hdr->msg, hdr->code);
    if (hdr->code == 405) resp->allowed = hdr->allowed;
  }
  free(hdr);
  writeResponse(ctx, resp);
  free(resp);
  return;
}

/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
