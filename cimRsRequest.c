
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

typedef struct requestHdr {
  int rc;
  const char *msg;
  int code;      /* http response code */
  int allowed;   /* if code == 405, bitmask of methods for 'Allowed:' header */

  const char *ns;
  int seen_ns;
  const char *className;
  int seen_className;
  const char **keys;
  enum http_method method;
  char *query;
  char *query_lang;
  const char *qualifier;
  int seen_qualifier;
} RequestHdr;

typedef struct cim_rs_response {
  int rc;
  const char *msg;
  int code; /* http response code */
  int allowed;           /* if code == 405, bitmask of methods for 'Allowed:' header */
  int json;   /* 0: xml, 1: json */
} CimRsResponse;

typedef struct handler {
  CimRsResponse* (*handler) (CimXmlRequestContext *, RequestHdr *hdr);
} Handler;

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

static CimRsResponse *
createRsResponse(int rc, const char *msg, int code)
{
  CimRsResponse *resp = (CimRsResponse *)malloc(sizeof(CimRsResponse));

  resp->rc = rc;
  if (msg) resp->msg = msg;
  resp->code = code;

  return resp;
}

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

  for (c = 0, i = 0; i < binCtx->rCount; i++) {
    for (j = 0; j < resp[i]->count; c++, j++) {
      if (binCtx->type == CMPI_ref)
        object = relocateSerializedObjectPath(resp[i]->object[j].data);
      else if (binCtx->type == CMPI_instance)
        object = relocateSerializedInstance(resp[i]->object[j].data);
      else if (binCtx->type == CMPI_class) {
        object = relocateSerializedConstClass(resp[i]->object[j].data);
      }

      rc = arraySetElementNotTrackedAt(ar, c, (CMPIValue *) & object,
                                       binCtx->type);
    }
  }

  enm = sfcb_native_new_CMPIEnumeration(ar, NULL);
  sb = UtilFactory->newStrinBuffer(1024);
/*FIXME
  if (binCtx->oHdr->type == OPS_EnumerateClassNames)
    enum2xml(enm, sb, binCtx->type, XML_asClassName, binCtx->bHdr->flags);
  else if (binCtx->oHdr->type == OPS_EnumerateClasses)
    enum2xml(enm, sb, binCtx->type, XML_asClass, binCtx->bHdr->flags);
  else
    enum2xml(enm, sb, binCtx->type, binCtx->xmlAs, binCtx->bHdr->flags);
*/
  _SFCB_RETURN(sb);
}

static          CimRsResponse*
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

  rs = createRsResponse(0, (char *)sb, 200);
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

static          CimRsResponse*
getClass(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "getClass");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  CMPIConstClass *cls;
  UtilStringBuffer *sb;
  int             irc,
                  i,
                  sreqSize = sizeof(GetClassReq);       // -sizeof(MsgSegment);
  BinResponseHdr *resp;
  GetClassReq    *sreq;

  XtokGetClass   *req = (XtokGetClass *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  if (req->properties)
    sreqSize += req->properties * sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_GetClass;
  sreq->hdr.count = req->properties + 2;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(ctx->principal);
  sreq->hdr.sessionId = ctx->sessionId;

  for (i = 0; i < req->properties; i++)
    sreq->properties[i] =
        setCharsMsgSegment(req->propertyList.values[i].value);

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq->hdr;
  binCtx.bHdr->flags = req->flags;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sreqSize;
  binCtx.chunkedMode = binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    resp = invokeProvider(&binCtx);
    closeProviderContext(&binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      cls = relocateSerializedConstClass(resp->object[0].data);
      sb = UtilFactory->newStrinBuffer(1024);
      cls2xml(cls, sb, binCtx.bHdr->flags);
      if (resp) {
        free(resp);
      }
      free(sreq);
      _SFCB_RETURN(iMethodResponse(hdr, sb));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    free(sreq);
    _SFCB_RETURN(rs);
  }
  free(sreq);
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
deleteClass(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "deleteClass");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  int             irc;
  BinResponseHdr *resp;
  DeleteClassReq  sreq;

  XtokDeleteClass *req = (XtokDeleteClass *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  memset(&sreq, 0, sizeof(sreq));
  sreq.hdr.operation = OPS_DeleteClass;
  sreq.hdr.count = 2;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  sreq.objectPath = setObjectPathMsgSegment(path);
  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.hdr.sessionId = ctx->sessionId;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.bHdr->flags = 0;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.chunkedMode = binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    resp = invokeProvider(&binCtx);
    closeProviderContext(&binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      if (resp) {
        free(resp);
      }
      _SFCB_RETURN(iMethodResponse(hdr, NULL));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
createClass(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "createClass");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  CMPIConstClass  cls;
  ClClass        *cl;
  ClClass        *tmp;
  int             irc;
  BinResponseHdr *resp;
  CreateClassReq  sreq = BINREQ(OPS_CreateClass, 3);

  XtokProperty   *p = NULL;
  XtokProperties *ps = NULL;
  XtokQualifier  *q = NULL;
  XtokQualifiers *qs = NULL;
  XtokMethod     *m = NULL;
  XtokMethods    *ms = NULL;
  XtokParam      *r = NULL;
  XtokParams     *rs = NULL;
  XtokClass      *c;
  CMPIData        d;
  CMPIParameter   pa;
  XtokCreateClass *req = (XtokCreateClass *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);

  cl = ClClassNew(req->op.className.data,
                  req->superClass ? req->superClass : NULL);
  c = &req->cls;

  qs = &c->qualifiers;
  for (q = qs->first; q; q = q->next) {
    if (q->value.value == NULL) {
      d.state = CMPI_nullValue;
      d.value.uint64 = 0;
    } else {
      d.state = CMPI_goodValue;
      d.value = str2CMPIValue(q->type, q->value, NULL, NULL);
    }
    d.type = q->type;
    ClClassAddQualifier(&cl->hdr, &cl->qualifiers, q->name, d);
  }

  ps = &c->properties;
  for (p = ps->first; p; p = p->next) {
    ClProperty     *prop;
    int             propId;
    if (p->val.val.value == NULL) {
      d.state = CMPI_nullValue;
      d.value.uint64 = 0;
    } else {
      d.state = CMPI_goodValue;
      d.value =
          str2CMPIValue(p->valueType, p->val.val, &p->val.ref,
                        req->op.nameSpace.data);
    }
    d.type = p->valueType;
    propId = ClClassAddProperty(cl, p->name, d, p->referenceClass);

    qs = &p->val.qualifiers;
    prop =
        ((ClProperty *) ClObjectGetClSection(&cl->hdr, &cl->properties)) +
        propId - 1;
    for (q = qs->first; q; q = q->next) {
      if (q->value.value == NULL) {
        d.state = CMPI_nullValue;
        d.value.uint64 = 0;
      } else {
        d.state = CMPI_goodValue;
        d.value = str2CMPIValue(q->type, q->value, NULL, NULL);
      }
      d.type = q->type;
      ClClassAddPropertyQualifier(&cl->hdr, prop, q->name, d);
    }
  }

  ms = &c->methods;
  for (m = ms->first; m; m = m->next) {
    ClMethod       *meth;
    ClParameter    *parm;
    int             methId,
                    parmId;

    methId = ClClassAddMethod(cl, m->name, m->type);
    meth =
        ((ClMethod *) ClObjectGetClSection(&cl->hdr, &cl->methods)) +
        methId - 1;

    qs = &m->qualifiers;
    for (q = qs->first; q; q = q->next) {
      if (q->value.value == NULL) {
        d.state = CMPI_nullValue;
        d.value.uint64 = 0;
      } else {
        d.state = CMPI_goodValue;
        d.value = str2CMPIValue(q->type, q->value, NULL, NULL);
      }
      d.type = q->type;
      ClClassAddMethodQualifier(&cl->hdr, meth, q->name, d);
    }

    rs = &m->params;
    for (r = rs->first; r; r = r->next) {
      pa.type = r->type;
      pa.arraySize = (unsigned int) r->arraySize;
      pa.refName = r->refClass;
      parmId = ClClassAddMethParameter(&cl->hdr, meth, r->name, pa);
      parm = ((ClParameter *)
              ClObjectGetClSection(&cl->hdr,
                                   &meth->parameters)) + methId - 1;

      qs = &r->qualifiers;
      for (q = qs->first; q; q = q->next) {
        if (q->value.value == NULL) {
          d.state = CMPI_nullValue;
          d.value.uint64 = 0;
        } else {
          d.state = CMPI_goodValue;
          d.value = str2CMPIValue(q->type, q->value, NULL, NULL);
        }
        d.type = q->type;
        ClClassAddMethParamQualifier(&cl->hdr, parm, q->name, d);
      }
    }
  }

  tmp = cl;
  cl = ClClassRebuildClass(cl, NULL);
  free(tmp);
  cls = initConstClass(cl);

  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.path = setObjectPathMsgSegment(path);
  sreq.cls = setConstClassMsgSegment(&cls);
  sreq.hdr.sessionId = ctx->sessionId;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.chunkedMode = binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    resp = invokeProvider(&binCtx);
    closeProviderContext(&binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      if (resp) {
        free(resp);
      }
      _SFCB_RETURN(iMethodResponse(hdr, NULL));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
enumClassNames(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "enumClassNames");
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

static          CimRsResponse*
enumClasses(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "enumClasses");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  EnumClassesReq  sreq = BINREQ(OPS_EnumerateClasses, 2);
  int             irc,
                  l = 0,
      err = 0;
  BinResponseHdr **resp;

  XtokEnumClasses *req = (XtokEnumClasses *) hdr->cimRequest;
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
  binCtx.type = CMPI_class;
  binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.chunkFncs = ctx->chunkFncs;

  if (noChunking || ctx->teTrailers == 0)
    hdr->chunkedMode = binCtx.chunkedMode = 0;
  else {
    sreq.hdr.flags |= FL_chunked;
    hdr->chunkedMode = binCtx.chunkedMode = 1;
  }
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    _SFCB_TRACE(1, ("--- Calling Providers"));
    resp = invokeProviders(&binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Provider"));

    closeProviderContext(&binCtx);

    if (noChunking || ctx->teTrailers == 0) {
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
    freeResponseHeaders(resp, &binCtx);

    rs.chunkedMode = 1;
    rs.rc = err;
    rs.errMsg = NULL;
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
getInstance(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "getInstance");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  CMPIInstance   *inst;
  CMPIType        type;
  CMPIValue       val,
                 *valp;
  UtilStringBuffer *sb;
  int             irc,
                  i,
                  m,
                  sreqSize = sizeof(GetInstanceReq);    // -sizeof(MsgSegment);
  BinResponseHdr *resp;
  CimRsResponse*    rsegs;
  GetInstanceReq *sreq;
  XtokGetInstance *req = (XtokGetInstance *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  if (req->properties)
    sreqSize += req->properties * sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_GetInstance;
  sreq->hdr.count = req->properties + 2;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  for (i = 0, m = req->instanceName.bindings.next; i < m; i++) {
    valp =
        getKeyValueTypePtr(req->instanceName.bindings.keyBindings[i].type,
                           req->instanceName.bindings.keyBindings[i].value,
                           &req->instanceName.bindings.keyBindings[i].ref,
                           &val, &type, req->op.nameSpace.data);
    CMAddKey(path, req->instanceName.bindings.keyBindings[i].name, valp,
             type);
  }
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(ctx->principal);
  sreq->hdr.sessionId = ctx->sessionId;

  for (i = 0; i < req->properties; i++)
    sreq->properties[i] =
        setCharsMsgSegment(req->propertyList.values[i].value);

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq->hdr;
  binCtx.bHdr->flags = req->flags;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sreqSize;
  binCtx.chunkedMode = binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    resp = invokeProvider(&binCtx);
    closeProviderContext(&binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      inst = relocateSerializedInstance(resp->object[0].data);
      sb = UtilFactory->newStrinBuffer(1024);
      instance2xml(inst, sb, binCtx.bHdr->flags);
      rsegs = iMethodResponse(hdr, sb);
      free(sreq);
      if (resp) {
        free(resp);
      }
      _SFCB_RETURN(rsegs);
    }
    free(sreq);
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    _SFCB_RETURN(rs);
  }
  free(sreq);
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
deleteInstance(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "deleteInstance");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  CMPIType        type;
  CMPIValue       val,
                 *valp;
  int             irc,
                  i,
                  m;
  BinResponseHdr *resp;
  DeleteInstanceReq sreq = BINREQ(OPS_DeleteInstance, 2);

  XtokDeleteInstance *req = (XtokDeleteInstance *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  for (i = 0, m = req->instanceName.bindings.next; i < m; i++) {
    valp =
        getKeyValueTypePtr(req->instanceName.bindings.keyBindings[i].type,
                           req->instanceName.bindings.keyBindings[i].value,
                           &req->instanceName.bindings.keyBindings[i].ref,
                           &val, &type, req->op.nameSpace.data);
    CMAddKey(path, req->instanceName.bindings.keyBindings[i].name, valp,
             type);
  }
  sreq.objectPath = setObjectPathMsgSegment(path);
  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.hdr.sessionId = ctx->sessionId;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.chunkedMode = binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    resp = invokeProvider(&binCtx);
    closeProviderContext(&binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      if (resp) {
        free(resp);
      }
      _SFCB_RETURN(iMethodResponse(hdr, NULL));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
createInstance(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMRS, "createInst");
  BinRequestContext binCtx;
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  CMPIInstance   *inst;
  CMPIValue       val;
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  UtilStringBuffer *sb;
  int             irc;
  BinResponseHdr *resp;
  CreateInstanceReq sreq = BINREQ(OPS_CreateInstance, 3);
  XtokProperty   *p = NULL;

  XtokCreateInstance *req = (XtokCreateInstance *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  inst = TrackedCMPIInstance(path, NULL);

  for (p = req->instance.properties.first; p; p = p->next) {
    if (p->val.val.value) {
      val =
          str2CMPIValue(p->valueType, p->val.val, &p->val.ref,
                        req->op.nameSpace.data);
      CMSetProperty(inst, p->name, &val, p->valueType);
    }
  }

  sreq.instance = setInstanceMsgSegment(inst);
  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.hdr.sessionId = ctx->sessionId;

  path = inst->ft->getObjectPath(inst, &st);
  /*
   * if st.rc is set the class was probably not found and the path is
   * NULL, so we don't set it. Let the provider manager handle unknown
   * class. 
   */
  if (!st.rc) {
    sreq.path = setObjectPathMsgSegment(path);
  }

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.chunkedMode = binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    resp = invokeProvider(&binCtx);
    closeProviderContext(&binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      path = relocateSerializedObjectPath(resp->object[0].data);
      sb = UtilFactory->newStrinBuffer(1024);
      instanceName2xml(path, sb);
      if (resp) {
        free(resp);
      }
      _SFCB_RETURN(iMethodResponse(hdr, sb));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
modifyInstance(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "modifyInstance");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  CMPIInstance   *inst;
  CMPIType        type;
  CMPIValue       val,
                 *valp;
  int             irc,
                  i,
                  m,
                  sreqSize = sizeof(ModifyInstanceReq); // -sizeof(MsgSegment);
  BinResponseHdr *resp;
  ModifyInstanceReq *sreq;
  XtokInstance   *xci;
  XtokInstanceName *xco;
  XtokProperty   *p = NULL;

  XtokModifyInstance *req = (XtokModifyInstance *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  if (req->properties)
    sreqSize += req->properties * sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_ModifyInstance;
  sreq->hdr.count = req->properties + 3;

  for (i = 0; i < req->properties; i++) {
    sreq->properties[i] =
        setCharsMsgSegment(req->propertyList.values[i].value);
  }
  xci = &req->namedInstance.instance;
  xco = &req->namedInstance.path;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  for (i = 0, m = xco->bindings.next; i < m; i++) {
    valp = getKeyValueTypePtr(xco->bindings.keyBindings[i].type,
                              xco->bindings.keyBindings[i].value,
                              &xco->bindings.keyBindings[i].ref,
                              &val, &type, req->op.nameSpace.data);

    CMAddKey(path, xco->bindings.keyBindings[i].name, valp, type);
  }

  inst = TrackedCMPIInstance(path, NULL);
  for (p = xci->properties.first; p; p = p->next) {
    if (p->val.val.value) {
      val =
          str2CMPIValue(p->valueType, p->val.val, &p->val.ref,
                        req->op.nameSpace.data);
      CMSetProperty(inst, p->name, &val, p->valueType);
    }
  }
  sreq->instance = setInstanceMsgSegment(inst);
  sreq->path = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(ctx->principal);
  sreq->hdr.sessionId = ctx->sessionId;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq->hdr;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sreqSize;
  binCtx.chunkedMode = binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    resp = invokeProvider(&binCtx);
    closeProviderContext(&binCtx);
    free(sreq);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      if (resp) {
        free(resp);
      }
      _SFCB_RETURN(iMethodResponse(hdr, NULL));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
  free(sreq);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
enumInstanceNames(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "enumInstanceNames");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  EnumInstanceNamesReq sreq = BINREQ(OPS_EnumerateInstanceNames, 2);
  int             irc,
                  l = 0,
      err = 0;
  BinResponseHdr **resp;
  CimRsResponse*    rs;
  XtokEnumInstanceNames *req = (XtokEnumInstanceNames *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  sreq.objectPath = setObjectPathMsgSegment(path);
  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.hdr.sessionId = ctx->sessionId;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.bHdr->flags = 0;
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

static          CimRsResponse*
enumInstances(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "enumInstances");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0

  CMPIObjectPath *path;
  EnumInstancesReq *sreq;
  int             irc,
                  l = 0,
      err = 0,
      i,
      sreqSize = sizeof(EnumInstancesReq);      // -sizeof(MsgSegment);
  BinResponseHdr **resp;
  XtokEnumInstances *req = (XtokEnumInstances *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  if (req->properties)
    sreqSize += req->properties * sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_EnumerateInstances;
  sreq->hdr.count = req->properties + 2;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  sreq->principal = setCharsMsgSegment(ctx->principal);
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->hdr.sessionId = ctx->sessionId;

  for (i = 0; i < req->properties; i++) {
    sreq->properties[i] =
        setCharsMsgSegment(req->propertyList.values[i].value);
  }

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq->hdr;
  binCtx.bHdr->flags = req->flags;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sreqSize;
  binCtx.commHndl = ctx->commHndl;

  binCtx.type = CMPI_instance;
  binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.chunkFncs = ctx->chunkFncs;

  if (noChunking || ctx->teTrailers == 0)
    hdr->chunkedMode = binCtx.chunkedMode = 0;
  else {
    sreq->hdr.flags |= FL_chunked;
    hdr->chunkedMode = binCtx.chunkedMode = 1;
  }
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);
  _SFCB_TRACE(1, ("--- Provider context gotten irc: %d", irc));

  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    _SFCB_TRACE(1, ("--- Calling Providers"));
    resp = invokeProviders(&binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Providers"));
    closeProviderContext(&binCtx);

    if (noChunking || ctx->teTrailers == 0) {
      if (err == 0) {
        rs = genResponses(&binCtx, resp, l);
      } else {
        rs = iMethodErrResponse(hdr, getErrSegment(resp[err - 1]->rc,
                                                   (char *) resp[err -
                                                                 1]->object
                                                   [0].data));
      }
      freeResponseHeaders(resp, &binCtx);
      free(sreq);
      _SFCB_RETURN(rs);
    }
    freeResponseHeaders(resp, &binCtx);
    free(sreq);
    rs.chunkedMode = 1;
    rs.rc = err;
    rs.errMsg = NULL;
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
  free(sreq);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
execQuery(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMRS, "execQuery");
  BinRequestContext binCtx;
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0

  CMPIObjectPath *path;
  ExecQueryReq    sreq = BINREQ(OPS_ExecQuery, 4);
  int             irc,
                  l = 0,
      err = 0;
  BinResponseHdr **resp;
  QLStatement    *qs = NULL;
  char          **fCls;

  XtokExecQuery  *req = (XtokExecQuery *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  qs = parseQuery(MEM_TRACKED, (char *) req->op.query.data,
                  (char *) req->op.queryLang.data, NULL, &irc);

  fCls = qs->ft->getFromClassList(qs);
  if (irc) {
    _SFCB_RETURN(iMethodErrResponse(hdr,
                                    getErrSegment
                                    (CMPI_RC_ERR_INVALID_QUERY,
                                     "syntax error in query.")));
  }
  if (fCls == NULL || *fCls == NULL) {
    _SFCB_RETURN(iMethodErrResponse(hdr,
                                    getErrSegment
                                    (CMPI_RC_ERR_INVALID_QUERY,
                                     "required from clause is missing.")));
  }
  req->op.className = setCharsMsgSegment(*fCls);

  path = TrackedCMPIObjectPath(req->op.nameSpace.data, *fCls, NULL);

  sreq.objectPath = setObjectPathMsgSegment(path);
  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.query = setCharsMsgSegment((char *) req->op.query.data);
  sreq.queryLang = setCharsMsgSegment((char *) req->op.queryLang.data);
  sreq.hdr.sessionId = ctx->sessionId;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.bHdr->flags = 0;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.commHndl = ctx->commHndl;
  binCtx.type = CMPI_instance;
  binCtx.xmlAs = XML_asObj;
  binCtx.noResp = 0;
  binCtx.chunkFncs = ctx->chunkFncs;

  if (noChunking || ctx->teTrailers == 0)
    hdr->chunkedMode = binCtx.chunkedMode = 0;
  else {
    sreq.hdr.flags |= FL_chunked;
    hdr->chunkedMode = binCtx.chunkedMode = 1;
  }
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));

  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    _SFCB_TRACE(1, ("--- Calling Provider"));
    resp = invokeProviders(&binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Provider"));
    closeProviderContext(&binCtx);

    if (noChunking || ctx->teTrailers == 0) {
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
    freeResponseHeaders(resp, &binCtx);
    rs.chunkedMode = 1;
    rs.rc = err;
    rs.errMsg = NULL;
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
associatorNames(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "associatorNames");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  AssociatorNamesReq sreq = BINREQ(OPS_AssociatorNames, 6);
  int             irc,
                  i,
                  m,
                  l = 0,
      err = 0;
  BinResponseHdr **resp;
  CMPIType        type;
  CMPIValue       val,
                 *valp;

  XtokAssociatorNames *req = (XtokAssociatorNames *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  for (i = 0, m = req->objectName.bindings.next; i < m; i++) {
    valp = getKeyValueTypePtr(req->objectName.bindings.keyBindings[i].type,
                              req->objectName.bindings.keyBindings[i].
                              value,
                              &req->objectName.bindings.keyBindings[i].ref,
                              &val, &type, req->op.nameSpace.data);
    CMAddKey(path, req->objectName.bindings.keyBindings[i].name, valp,
             type);
  }

  if (req->objectName.bindings.next == 0) {
    _SFCB_RETURN(iMethodErrResponse
                 (hdr,
                  getErrSegment(CMPI_RC_ERR_NOT_SUPPORTED,
                                "AssociatorNames operation for classes not supported")));
  }
  if (!req->objNameSet) {
    _SFCB_RETURN(iMethodErrResponse(hdr, getErrSegment
                                    (CMPI_RC_ERR_INVALID_PARAMETER,
                                     "ObjectName parameter required")));
  }

  sreq.objectPath = setObjectPathMsgSegment(path);

  sreq.resultClass = req->op.resultClass;
  sreq.role = req->op.role;
  sreq.assocClass = req->op.assocClass;
  sreq.resultRole = req->op.resultRole;
  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.hdr.sessionId = ctx->sessionId;

  req->op.className = req->op.assocClass;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.bHdr->flags = 0;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.commHndl = ctx->commHndl;
  binCtx.type = CMPI_ref;
  binCtx.xmlAs = XML_asObjectPath;
  binCtx.noResp = 0;
  binCtx.chunkedMode = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);
  _SFCB_TRACE(1, ("--- Provider context gotten"));

  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    _SFCB_TRACE(1, ("--- Calling Providers"));
    resp = invokeProviders(&binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Providers"));

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

static          CimRsResponse*
associators(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMRS, "associators");

  BinRequestContext binCtx;
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  AssociatorsReq *sreq;
  int             irc,
                  i,
                  m,
                  l = 0,
      err = 0,
      sreqSize = sizeof(AssociatorsReq);        // -sizeof(MsgSegment);
  BinResponseHdr **resp;
  CMPIType        type;
  CMPIValue       val,
                 *valp;

  XtokAssociators *req = (XtokAssociators *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  if (req->properties)
    sreqSize += req->properties * sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_Associators;
  sreq->hdr.count = req->properties + 6;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  for (i = 0, m = req->objectName.bindings.next; i < m; i++) {
    valp = getKeyValueTypePtr(req->objectName.bindings.keyBindings[i].type,
                              req->objectName.bindings.keyBindings[i].
                              value,
                              &req->objectName.bindings.keyBindings[i].ref,
                              &val, &type, req->op.nameSpace.data);
    CMAddKey(path, req->objectName.bindings.keyBindings[i].name, valp,
             type);
  }

  if (req->objectName.bindings.next == 0) {
    free(sreq);
    _SFCB_RETURN(iMethodErrResponse
                 (hdr,
                  getErrSegment(CMPI_RC_ERR_NOT_SUPPORTED,
                                "Associator operation for classes not supported")));
  }
  if (!req->objNameSet) {
    free(sreq);
    _SFCB_RETURN(iMethodErrResponse(hdr, getErrSegment
                                    (CMPI_RC_ERR_INVALID_PARAMETER,
                                     "ObjectName parameter required")));
  }

  sreq->objectPath = setObjectPathMsgSegment(path);

  sreq->resultClass = req->op.resultClass;
  sreq->role = req->op.role;
  sreq->assocClass = req->op.assocClass;
  sreq->resultRole = req->op.resultRole;
  sreq->hdr.flags = req->flags;
  sreq->principal = setCharsMsgSegment(ctx->principal);
  sreq->hdr.sessionId = ctx->sessionId;

  for (i = 0; i < req->properties; i++)
    sreq->properties[i] =
        setCharsMsgSegment(req->propertyList.values[i].value);

  req->op.className = req->op.assocClass;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq->hdr;
  binCtx.bHdr->flags = req->flags;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sreqSize;
  binCtx.commHndl = ctx->commHndl;
  binCtx.type = CMPI_instance;
  binCtx.xmlAs = XML_asObj;
  binCtx.noResp = 0;
  binCtx.pAs = NULL;
  binCtx.chunkFncs = ctx->chunkFncs;

  if (noChunking || ctx->teTrailers == 0)
    hdr->chunkedMode = binCtx.chunkedMode = 0;
  else {
    sreq->hdr.flags |= FL_chunked;
    hdr->chunkedMode = binCtx.chunkedMode = 1;
  }

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    _SFCB_TRACE(1, ("--- Calling Provider"));
    resp = invokeProviders(&binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Provider"));

    closeProviderContext(&binCtx);

    if (noChunking || ctx->teTrailers == 0) {
      if (err == 0) {
        rs = genResponses(&binCtx, resp, l);
      } else {
        rs = iMethodErrResponse(hdr, getErrSegment(resp[err - 1]->rc,
                                                   (char *) resp[err -
                                                                 1]->object
                                                   [0].data));
      }
      freeResponseHeaders(resp, &binCtx);
      free(sreq);
      _SFCB_RETURN(rs);
    }

    freeResponseHeaders(resp, &binCtx);
    free(sreq);
    rs.chunkedMode = 1;
    rs.rc = err;
    rs.errMsg = NULL;
    _SFCB_RETURN(rs);
  }
  free(sreq);
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}


static          CimRsResponse*
referenceNames(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMRS, "referenceNames");
  BinRequestContext binCtx;
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  ReferenceNamesReq sreq = BINREQ(OPS_ReferenceNames, 4);
  int             irc,
                  i,
                  m,
                  l = 0,
      err = 0;
  BinResponseHdr **resp;
  CMPIType        type;
  CMPIValue       val,
                 *valp;

  XtokReferenceNames *req = (XtokReferenceNames *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  for (i = 0, m = req->objectName.bindings.next; i < m; i++) {
    valp = getKeyValueTypePtr(req->objectName.bindings.keyBindings[i].type,
                              req->objectName.bindings.keyBindings[i].
                              value,
                              &req->objectName.bindings.keyBindings[i].ref,
                              &val, &type, req->op.nameSpace.data);
    CMAddKey(path, req->objectName.bindings.keyBindings[i].name, valp,
             type);
  }

  if (req->objectName.bindings.next == 0) {
    _SFCB_RETURN(iMethodErrResponse
                 (hdr,
                  getErrSegment(CMPI_RC_ERR_NOT_SUPPORTED,
                                "ReferenceNames operation for classes not supported")));
  }
  if (!req->objNameSet) {
    _SFCB_RETURN(iMethodErrResponse(hdr, getErrSegment
                                    (CMPI_RC_ERR_INVALID_PARAMETER,
                                     "ObjectName parameter required")));
  }

  sreq.objectPath = setObjectPathMsgSegment(path);

  sreq.resultClass = req->op.resultClass;
  sreq.role = req->op.role;
  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.hdr.sessionId = ctx->sessionId;

  req->op.className = req->op.resultClass;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.bHdr->flags = 0;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.commHndl = ctx->commHndl;
  binCtx.type = CMPI_ref;
  binCtx.xmlAs = XML_asObjectPath;
  binCtx.noResp = 0;
  binCtx.chunkedMode = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);
  _SFCB_TRACE(1, ("--- Provider context gotten"));

  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    _SFCB_TRACE(1, ("--- Calling Providers"));
    resp = invokeProviders(&binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Providers"));

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

static          CimRsResponse*
references(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMRS, "references");

  BinRequestContext binCtx;
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  ReferencesReq  *sreq;
  int             irc,
                  i,
                  m,
                  l = 0,
      err = 0,
      sreqSize = sizeof(ReferencesReq); // -sizeof(MsgSegment);
  BinResponseHdr **resp;
  CMPIType        type;
  CMPIValue       val,
                 *valp;

  XtokReferences *req = (XtokReferences *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  if (req->properties)
    sreqSize += req->properties * sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_References;
  sreq->hdr.count = req->properties + 4;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  for (i = 0, m = req->objectName.bindings.next; i < m; i++) {
    valp = getKeyValueTypePtr(req->objectName.bindings.keyBindings[i].type,
                              req->objectName.bindings.keyBindings[i].
                              value,
                              &req->objectName.bindings.keyBindings[i].ref,
                              &val, &type, req->op.nameSpace.data);
    CMAddKey(path, req->objectName.bindings.keyBindings[i].name, valp,
             type);
  }

  if (req->objectName.bindings.next == 0) {
    free(sreq);
    _SFCB_RETURN(iMethodErrResponse
                 (hdr,
                  getErrSegment(CMPI_RC_ERR_NOT_SUPPORTED,
                                "References operation for classes not supported")));
  }
  if (!req->objNameSet) {
    free(sreq);
    _SFCB_RETURN(iMethodErrResponse(hdr, getErrSegment
                                    (CMPI_RC_ERR_INVALID_PARAMETER,
                                     "ObjectName parameter required")));
  }

  sreq->objectPath = setObjectPathMsgSegment(path);

  sreq->resultClass = req->op.resultClass;
  sreq->role = req->op.role;
  sreq->hdr.flags = req->flags;
  sreq->principal = setCharsMsgSegment(ctx->principal);
  sreq->hdr.sessionId = ctx->sessionId;

  for (i = 0; i < req->properties; i++)
    sreq->properties[i] =
        setCharsMsgSegment(req->propertyList.values[i].value);

  req->op.className = req->op.resultClass;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq->hdr;
  binCtx.bHdr->flags = req->flags;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sreqSize;
  binCtx.commHndl = ctx->commHndl;
  binCtx.type = CMPI_instance;
  binCtx.xmlAs = XML_asObj;
  binCtx.noResp = 0;
  binCtx.pAs = NULL;
  binCtx.chunkFncs = ctx->chunkFncs;

  if (noChunking || ctx->teTrailers == 0)
    hdr->chunkedMode = binCtx.chunkedMode = 0;
  else {
    sreq->hdr.flags |= FL_chunked;
    hdr->chunkedMode = binCtx.chunkedMode = 1;
  }

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    _SFCB_TRACE(1, ("--- Calling Provider"));
    resp = invokeProviders(&binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Provider"));
    closeProviderContext(&binCtx);

    if (noChunking || ctx->teTrailers == 0) {
      if (err == 0) {
        rs = genResponses(&binCtx, resp, l);
      } else {
        rs = iMethodErrResponse(hdr, getErrSegment(resp[err - 1]->rc,
                                                   (char *) resp[err -
                                                                 1]->object
                                                   [0].data));
      }
      freeResponseHeaders(resp, &binCtx);
      free(sreq);
      _SFCB_RETURN(rs);
    }
    freeResponseHeaders(resp, &binCtx);
    free(sreq);
    rs.chunkedMode = 1;
    rs.rc = err;
    rs.errMsg = NULL;
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
  free(sreq);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

int
updateMethodParamTypes(RequestHdr * hdr)
{

  _SFCB_ENTER(TRACE_CIMRS, "updateMethodParamTypes");
#if 0
  CMPIConstClass *cls = NULL;
  ClMethod       *meth;
  ClParameter    *param = NULL;
  int             i,
                  m;
  ClClass        *cl;
  char           *mname;
  XtokParamValue *ptok;
  int             p,
                  pm;

  XtokMethodCall *req = (XtokMethodCall *) hdr->cimRequest;
  cls =
      getConstClass((char *) req->op.nameSpace.data,
                    (char *) req->op.className.data);
  if (!cls) {
    _SFCB_RETURN(CMPI_RC_ERR_INVALID_CLASS);
  }

  cl = (ClClass *) cls->hdl;

  /*
   * check that the method specified in req exists in class 
   */
  for (i = 0, m = ClClassGetMethodCount(cl); i < m; i++) {
    ClClassGetMethodAt(cl, i, NULL, &mname, NULL);
    if (strcasecmp(req->method, mname) == 0) {
      break;
    }
  }
  if (i == m) {
    _SFCB_RETURN(CMPI_RC_ERR_METHOD_NOT_FOUND);
  }

  meth = ((ClMethod *) ClObjectGetClSection(&cl->hdr, &cl->methods)) + i;

  /*
   * loop through all params from parsed req 
   */
  for (ptok = req->paramValues.first; ptok; ptok = ptok->next) {
    CMPIParameter   pdata;
    char           *sname;

    /*
     * loop through all params for meth 
     */
    for (p = 0, pm = ClClassGetMethParameterCount(cl, i); p < pm; p++) {
      ClClassGetMethParameterAt(cl, meth, p, &pdata, &sname);

      if (strcasecmp(sname, ptok->name) == 0) {
        // fprintf(stderr, "%s matches %s", sname, ptok->name);
        param = ((ClParameter *)
                 ClObjectGetClSection(&cl->hdr, &meth->parameters)) + p;
        break;
      }
    }
    if (p == pm) {
      _SFCB_RETURN(CMPI_RC_ERR_INVALID_PARAMETER);
    }
    // fprintf(stderr, " pdata.type=%u (expec), ptok->type=%u\n",
    // pdata.type, ptok->type);
    /*
     * special case: EmbeddedInstance. Parser will set type to instance,
     * but repository would have type as string. Check here to not fail
     * the else-if below. 
     */
    if (param && (ptok->type & CMPI_instance)) {
      int             isEI = 0;
      int             qcount =
          ClClassGetMethParmQualifierCount(cl, meth, i);
      for (; qcount > 0; qcount--) {
        char           *qname;
        ClClassGetMethParamQualifierAt(cl, param, qcount, NULL, &qname);
        if (strcmp(qname, "EmbeddedInstance") == 0) {
          // fprintf(stderr, " is EmbeddedInstance\n");
          isEI = 1;
          break;
        }
      }
      if (isEI)
        continue;
    }

    if (ptok->type == 0) {
      /*
       * Type was unknown, fill it in 
       */
      // printf("parameter %s missing type, using %s\n", sname,
      // paramType(pdata.type));
      ptok->type = pdata.type;
    } else if (ptok->type != pdata.type) {
      /*
       * Parameter type mismatch 
       */
      _SFCB_RETURN(CMPI_RC_ERR_TYPE_MISMATCH);
    }
  }
#endif
  _SFCB_RETURN(CMPI_RC_OK);
}

static          CimRsResponse*
invokeMethod(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "invokeMethod");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  CMPIArgs       *out;
  CMPIType        type;
  CMPIValue       val,
                 *valp;
  UtilStringBuffer *sb;
  int             irc,
                  i,
                  m,
                  rc,
                  vmpt = 0;
  BinResponseHdr *resp;
  CimRsResponse*    rsegs;
  InvokeMethodReq sreq = BINREQ(OPS_InvokeMethod, 5);
  CMPIArgs       *in = TrackedCMPIArgs(NULL);
  XtokParamValue *p;

  XtokMethodCall *req = (XtokMethodCall *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  if (req->instName)
    for (i = 0, m = req->instanceName.bindings.next; i < m; i++) {
      valp =
          getKeyValueTypePtr(req->instanceName.bindings.keyBindings[i].
                             type,
                             req->instanceName.bindings.keyBindings[i].
                             value,
                             &req->instanceName.bindings.keyBindings[i].
                             ref, &val, &type, req->op.nameSpace.data);
      CMAddKey(path, req->instanceName.bindings.keyBindings[i].name, valp,
               type);
    }
  sreq.objectPath = setObjectPathMsgSegment(path);
  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.hdr.sessionId = ctx->sessionId;

  if (getControlBool("validateMethodParamTypes", &vmpt))
    vmpt = 1;

  for (p = req->paramValues.first; p; p = p->next) {
    /*
     * Update untyped params (p->type==0) and verify those that were
     * specified 
     */
    if (p->type == 0 || vmpt) {
      rc = updateMethodParamTypes(hdr);

      if (rc != CMPI_RC_OK) {
        rsegs = methodErrResponse(hdr, getErrSegment(rc, NULL));
        _SFCB_RETURN(rsegs);
      }
    }

    if (p->value.value) {
      CMPIValue       val = str2CMPIValue(p->type, p->value, &p->valueRef,
                                          req->op.nameSpace.data);
      CMAddArg(in, p->name, &val, p->type);
    }
  }

  sreq.in = setArgsMsgSegment(in);
  sreq.out = setArgsMsgSegment(NULL);
  sreq.method = setCharsMsgSegment(req->method);

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.bHdr->flags = 0;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(InvokeMethodReq);
  binCtx.chunkedMode = binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_SFCB_PROVIDER) {
    if(*req->method == '_') {
      CimRsResponse*  rs;
      rs = methodErrResponse(hdr, getErrSegment(CMPI_RC_ERR_ACCESS_DENIED, NULL));
      closeProviderContext(&binCtx);
      _SFCB_RETURN(rs);
    } else {
      irc = MSG_X_PROVIDER;
    }
  }
                                                
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    resp = invokeProvider(&binCtx);
    closeProviderContext(&binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      sb = UtilFactory->newStrinBuffer(1024);
      if (resp->rvValue) {
        if (resp->rv.type == CMPI_chars) {
          resp->rv.value.chars = (long) resp->rvEnc.data + (char *) resp;
        } else if (resp->rv.type == CMPI_dateTime) {
          resp->rv.value.dateTime =
              sfcb_native_new_CMPIDateTime_fromChars((long) resp->rvEnc.
                                                     data + (char *) resp,
                                                     NULL);
        }
        SFCB_APPENDCHARS_BLOCK(sb, "<RETURNVALUE PARAMTYPE=\"");
        sb->ft->appendChars(sb, paramType(resp->rv.type));
        SFCB_APPENDCHARS_BLOCK(sb, "\">\n");
        value2xml(resp->rv, sb, 1);
        SFCB_APPENDCHARS_BLOCK(sb, "</RETURNVALUE>\n");
      }
      out = relocateSerializedArgs(resp->object[0].data);
      args2xml(out, sb);
      rsegs = methodResponse(hdr, sb);
      if (resp) {
        free(resp);
      }
      _SFCB_RETURN(rsegs);
    }
    rs = methodErrResponse(hdr, getErrSegment(resp->rc,
                                              (char *) resp->object[0].
                                              data));
    if (resp) {
      free(resp);
    }
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
getProperty(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "getProperty");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  CMPIInstance   *inst;
  CMPIData        data;
  CMPIStatus      rc;
  UtilStringBuffer *sb;
  CMPIString     *tmpString = NewCMPIString(NULL, NULL);
  int             irc;
  BinResponseHdr *resp;
  CimRsResponse*    rsegs;
  GetPropertyReq  sreq = BINREQ(OPS_GetProperty, 3);

  XtokGetProperty *req = (XtokGetProperty *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data,
                            req->instanceName.className, &rc);

  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.path = setObjectPathMsgSegment(path);
  sreq.name = setCharsMsgSegment(req->name);
  sreq.hdr.sessionId = ctx->sessionId;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.chunkedMode = binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    resp = invokeProvider(&binCtx);
    closeProviderContext(&binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      inst = relocateSerializedInstance(resp->object[0].data);
      sb = UtilFactory->newStrinBuffer(1024);
      data = inst->ft->getProperty(inst, req->name, NULL);
      data2xml(&data, NULL, tmpString, NULL, NULL, 0, NULL, 0, sb, NULL, 0,
               0);
      CMRelease(tmpString);
      rsegs = iMethodResponse(hdr, sb);
      if (resp) {
        free(resp);
      }
      _SFCB_RETURN(rsegs);
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    CMRelease(tmpString);
    _SFCB_RETURN(rs);
  }
  CMRelease(tmpString);
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
setProperty(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "setProperty");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  CMPIInstance   *inst;
  CMPIType        t;
  CMPIStatus      rc;
  CMPIValue       val;
  int             irc;
  BinResponseHdr *resp;
  SetPropertyReq  sreq = BINREQ(OPS_SetProperty, 3);

  XtokSetProperty *req = (XtokSetProperty *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data,
                            req->instanceName.className, &rc);

  inst = internal_new_CMPIInstance(MEM_TRACKED, NULL, NULL, 1);

  if (req->newVal.type == 0) {
    t = guessType(req->newVal.val.value);
  } else if (req->newVal.type == CMPI_ARRAY) {
    t = guessType(req->newVal.arr.values[0].value) | CMPI_ARRAY;
  } else {
    t = req->newVal.type;
  }
  if (t != CMPI_null) {
    val =
        str2CMPIValue(t, req->newVal.val, &req->newVal.ref,
                      req->op.nameSpace.data);
    CMSetProperty(inst, req->propertyName, &val, t);
  } else {
    val.string = 0;
    CMSetProperty(inst, req->propertyName, 0, t);
  }

  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.path = setObjectPathMsgSegment(path);
  sreq.inst = setInstanceMsgSegment(inst);
  sreq.hdr.sessionId = ctx->sessionId;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.chunkedMode = binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));

  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    resp = invokeProvider(&binCtx);
    closeProviderContext(&binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      if (resp) {
        free(resp);
      }
      _SFCB_RETURN(iMethodResponse(hdr, NULL));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

#ifdef HAVE_QUALREP
static          CimRsResponse*
enumQualifiers(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;

  _SFCB_ENTER(TRACE_CIMRS, "enumQualifiers");

  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  EnumClassNamesReq sreq = BINREQ(OPS_EnumerateQualifiers, 2);
  int             irc;
  BinResponseHdr *resp;
  XtokEnumQualifiers *req = (XtokEnumQualifiers *) hdr->cimRequest;

  path = TrackedCMPIObjectPath(req->op.nameSpace.data, NULL, NULL);
  sreq.objectPath = setObjectPathMsgSegment(path);
  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.hdr.sessionId = ctx->sessionId;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.commHndl = ctx->commHndl;
  binCtx.type = CMPI_qualifierDecl;
  binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.chunkedMode = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    _SFCB_TRACE(1, ("--- Calling Providers"));
    resp = invokeProvider(&binCtx);
    _SFCB_TRACE(1, ("--- Back from Provider"));
    closeProviderContext(&binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      rs = genQualifierResponses(&binCtx, resp);
    } else {
      rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                                 (char *) resp->object[0].
                                                 data));
    }
    if (resp) {
      free(resp);
    }
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
getQualifier(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "getQualifier");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  CMPIQualifierDecl *qual;
  CMPIStatus      rc;
  UtilStringBuffer *sb;
  int             irc;
  BinResponseHdr *resp;
  CimRsResponse*    rsegs;
  GetQualifierReq sreq = BINREQ(OPS_GetQualifier, 2);

  XtokGetQualifier *req = (XtokGetQualifier *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path = TrackedCMPIObjectPath(req->op.nameSpace.data, req->name, &rc); // abuse 
                                                                        // 
  // 
  // classname 
  // for 
  // qualifier 
  // name

  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.path = setObjectPathMsgSegment(path);
  sreq.hdr.sessionId = ctx->sessionId;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.chunkedMode = binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    resp = invokeProvider(&binCtx);
    closeProviderContext(&binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      qual = relocateSerializedQualifier(resp->object[0].data);
      sb = UtilFactory->newStrinBuffer(1024);
      qualifierDeclaration2xml(qual, sb);
      rsegs = iMethodResponse(hdr, sb);
      if (resp) {
        free(resp);
      }
      _SFCB_RETURN(rsegs);
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
deleteQualifier(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "deleteQualifier");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  CMPIStatus      rc;
  int             irc;
  BinResponseHdr *resp;
  DeleteQualifierReq sreq = BINREQ(OPS_DeleteQualifier, 2);

  XtokDeleteQualifier *req = (XtokDeleteQualifier *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path = TrackedCMPIObjectPath(req->op.nameSpace.data, req->name, &rc); // abuse 
                                                                        // 
  // 
  // classname 
  // for 
  // qualifier 
  // name

  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.path = setObjectPathMsgSegment(path);
  sreq.hdr.sessionId = ctx->sessionId;

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.chunkedMode = binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    resp = invokeProvider(&binCtx);
    closeProviderContext(&binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      if (resp) {
        free(resp);
      }
      _SFCB_RETURN(iMethodResponse(hdr, NULL));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}

static          CimRsResponse*
setQualifier(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "setQualifier");
  memset(&binCtx, 0, sizeof(BinRequestContext));
#if 0
  CMPIObjectPath *path;
  CMPIQualifierDecl qual;
  CMPIData        d;
  ClQualifierDeclaration *q;
  int             irc;
  BinResponseHdr *resp;
  SetQualifierReq sreq = BINREQ(OPS_SetQualifier, 3);

  XtokSetQualifier *req = (XtokSetQualifier *) hdr->cimRequest;

  path = TrackedCMPIObjectPath(req->op.nameSpace.data, NULL, NULL);
  q = ClQualifierDeclarationNew(req->op.nameSpace.data,
                                req->qualifierdeclaration.name);

  if (req->qualifierdeclaration.overridable)
    q->flavor |= ClQual_F_Overridable;
  if (req->qualifierdeclaration.tosubclass)
    q->flavor |= ClQual_F_ToSubclass;
  if (req->qualifierdeclaration.toinstance)
    q->flavor |= ClQual_F_ToInstance;
  if (req->qualifierdeclaration.translatable)
    q->flavor |= ClQual_F_Translatable;
  if (req->qualifierdeclaration.isarray)
    q->type |= CMPI_ARRAY;

  if (req->qualifierdeclaration.type)
    q->type |= req->qualifierdeclaration.type;

  if (req->qualifierdeclaration.scope.class)
    q->scope |= ClQual_S_Class;
  if (req->qualifierdeclaration.scope.association)
    q->scope |= ClQual_S_Association;
  if (req->qualifierdeclaration.scope.reference)
    q->scope |= ClQual_S_Reference;
  if (req->qualifierdeclaration.scope.property)
    q->scope |= ClQual_S_Property;
  if (req->qualifierdeclaration.scope.method)
    q->scope |= ClQual_S_Method;
  if (req->qualifierdeclaration.scope.parameter)
    q->scope |= ClQual_S_Parameter;
  if (req->qualifierdeclaration.scope.indication)
    q->scope |= ClQual_S_Indication;
  q->arraySize = req->qualifierdeclaration.arraySize;

  if (req->qualifierdeclaration.data.value.value) {     // default value
    // is set
    d.state = CMPI_goodValue;
    d.type = q->type;           // "specified" type
    d.type |= req->qualifierdeclaration.data.type;      // actual type

    // default value declared - isarray attribute must match, if set
    if (req->qualifierdeclaration.isarrayIsSet)
      if (!req->qualifierdeclaration.
          isarray ^ !(req->qualifierdeclaration.data.type & CMPI_ARRAY))
        _SFCB_RETURN(iMethodErrResponse
                     (hdr,
                      getErrSegment(CMPI_RC_ERROR,
                                    "ISARRAY attribute and default value conflict")));

    d.value = str2CMPIValue(d.type, req->qualifierdeclaration.data.value,
                            (XtokValueReference *) &
                            req->qualifierdeclaration.data.valueArray,
                            NULL);
    ClQualifierAddQualifier(&q->hdr, &q->qualifierData,
                            req->qualifierdeclaration.name, d);
  } else {                      // no default value - rely on ISARRAY
    // attr, check if it's set
    /*
     * if(!req->qualifierdeclaration.isarrayIsSet)
     * _SFCB_RETURN(iMethodErrResponse(hdr, getErrSegment(CMPI_RC_ERROR,
     * "ISARRAY attribute MUST be present if the Qualifier declares no
     * default value")));
     */
    q->qualifierData.sectionOffset = 0;
    q->qualifierData.used = 0;
    q->qualifierData.max = 0;
  }

  qual = initQualifier(q);

  sreq.qualifier = setQualifierMsgSegment(&qual);
  sreq.principal = setCharsMsgSegment(ctx->principal);
  sreq.hdr.sessionId = ctx->sessionId;
  sreq.path = setObjectPathMsgSegment(path);

  binCtx.oHdr = (OperationHdr *) req;
  binCtx.bHdr = &sreq.hdr;
  binCtx.rHdr = hdr;
  binCtx.bHdrSize = sizeof(sreq);
  binCtx.chunkedMode = binCtx.xmlAs = binCtx.noResp = 0;
  binCtx.pAs = NULL;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(&binCtx, (OperationHdr *) req);

  _SFCB_TRACE(1, ("--- Provider context gotten"));

  if (irc == MSG_X_PROVIDER) {
    CimRsResponse*    rs;
    resp = invokeProvider(&binCtx);
    closeProviderContext(&binCtx);
    qual.ft->release(&qual);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      if (resp) {
        free(resp);
      }
      _SFCB_RETURN(iMethodResponse(hdr, NULL));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    _SFCB_RETURN(rs);
  }
  closeProviderContext(&binCtx);
  qual.ft->release(&qual);
	#endif
  _SFCB_RETURN(ctxErrResponse(hdr, &binCtx));
}
#endif

static          CimRsResponse*
notSupported(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  return createRsResponse(7, "Operation not supported xx", 501);
}

static Handler  handlers[] = {
  {notSupported},               // dummy
  {getClass},                   // OPS_GetClass 1
  {getInstance},                // OPS_GetInstance 2
  {deleteClass},                // OPS_DeleteClass 3
  {deleteInstance},             // OPS_DeleteInstance 4
  {createClass},                // OPS_CreateClass 5
  {createInstance},             // OPS_CreateInstance 6
  {notSupported},               // OPS_ModifyClass 7
  {modifyInstance},             // OPS_ModifyInstance 8
  {enumClasses},                // OPS_EnumerateClasses 9
  {enumClassNames},             // OPS_EnumerateClassNames 10
  {enumInstances},              // OPS_EnumerateInstances 11
  {enumInstanceNames},          // OPS_EnumerateInstanceNames 12
  {execQuery},                  // OPS_ExecQuery 13
  {associators},                // OPS_Associators 14
  {associatorNames},            // OPS_AssociatorNames 15
  {references},                 // OPS_References 16
  {referenceNames},             // OPS_ReferenceNames 17
  {getProperty},                // OPS_GetProperty 18
  {setProperty},                // OPS_SetProperty 19
#ifdef HAVE_QUALREP
  {getQualifier},               // OPS_GetQualifier 20
  {setQualifier},               // OPS_SetQualifier 21
  {deleteQualifier},            // OPS_DeleteQualifier 22
  {enumQualifiers},             // OPS_EnumerateQualifiers 23
#else
  {notSupported},               // OPS_GetQualifier 20
  {notSupported},               // OPS_SetQualifier 21
  {notSupported},               // OPS_DeleteQualifier 22
  {notSupported},               // OPS_EnumerateQualifiers 23
#endif
  {invokeMethod},               // OPS_InvokeMethod 24
};


static          CimRsResponse*
enumNameSpaces(CimXmlRequestContext * ctx, RequestHdr * hdr)
{
  BinRequestContext binCtx;
  _SFCB_ENTER(TRACE_CIMRS, "enumNameSpaces");
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
  char *next; /* ptr to next element in path */
  char *q; /* url query */
	
  _SFCB_ENTER(TRACE_CIMRS, "scanCimRsRequest");

  _SFCB_TRACE(1, ("--- method '%s', path '%s'", method, path));

  hdr->rc = 0;
	
  /*
   * check the method
   * 
   */

  if (strcasecmp(method, "GET") == 0) {
    hdr->method = HTTP_GET;
  }
  else if (strcasecmp(method, "PUT") == 0) {
    hdr->method = HTTP_PUT;
  }
  else if (strcasecmp(method, "POST") == 0) {
    hdr->method = HTTP_POST;
  }
  else if (strcasecmp(method, "DELETE") == 0) {
    hdr->method = HTTP_DELETE;
  }
  else {
    hdr->rc = 1;
    hdr->code = 405; /* method not allowed */
    hdr->allowed = HTTP_GET|HTTP_PUT|HTTP_POST|HTTP_DELETE;
    _SFCB_RETURN(hdr);
  }


  /*
   * split off the query
   * 
   */

  q = strchr(path, '?');
  if (q) *q++ = '\0';
	
  /*
   * parse the path
   * 
   */

  if (strcasecmp(urlelement(path,&next), "namespaces") != 0) {
    hdr->rc = 1;
    hdr->msg = "path must start with /namespaces";
    _SFCB_RETURN(hdr);
  }
  hdr->seen_ns = 1;
  hdr->ns = urlelement(next, &next);
  fprintf(stderr, "namespaces '%s'\n", hdr->ns);

  e = urlelement(next, &next);
  if (strcasecmp(e, "classes") == 0) {
    hdr->seen_className = 1;
    hdr->className = urlelement(next, &next);
    fprintf(stderr, "classes '%s'\n", hdr->className);
    if (hdr->className) {
      /*
       * seen /classes/{classname}/
       */
      e = urlelement(next, &next);
      if (strcasecmp(e, "associators") == 0) {
      }
      else if (strcasecmp(e, "references") == 0) {
      }
      else if (strcasecmp(e, "methods") == 0) {
      }
      else if (strcasecmp(e, "instances") == 0) {
      }
      else {
      }
    }
    else if (q) {
    }
  }
  else if (strcasecmp(e, "qualifiers") == 0) {
    hdr->seen_qualifier = 1;
    hdr->qualifier = urlelement(next, &next);
    fprintf(stderr, "qualifiers '%s'\n", hdr->qualifier);
  }
  else if (strcasecmp(e, "query") == 0) {
    hdr->className = urlelement(next, &next);
    fprintf(stderr, "classes '%s'\n", hdr->className);
  }
  else {
    /*
     * -> just 'namespaces'
     */
    if (hdr->method != HTTP_GET) {
      hdr->rc = 1;
      hdr->code = 405;
      hdr->allowed = HTTP_GET;
      _SFCB_RETURN(hdr);
    }
  }
/*static          CimRsResponse*
enumNameSpaces(CimXmlRequestContext * ctx, RequestHdr * hdr)
*/
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

  sprintf(str, "HTTP/1.1 %d OK\r\n", resp->code);
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
	strcat(o, b->method);
	o += strlen(b->method);
      }
      b++;
    }
    *o++ = '\0';
    sprintf(str, "Allowed: %s\r\n", out);
  }
     
  commWrite(conn_fd, str, strlen(str));
  commWrite(conn_fd, cont, strlen(cont));
  sprintf(str, "Content-Length: %d\r\n", len);
  commWrite(conn_fd, str, strlen(str));
  commWrite(conn_fd, cach, strlen(cach));
  if (ctx->keepaliveTimeout == 0 || ctx->numRequest >= ctx->keepaliveMaxRequest) {
    commWrite(conn_fd, cclose, strlen(cclose));
  }
  commWrite(conn_fd, end, strlen(end));

  if (resp->msg)
     commWrite(conn_fd, (char *)resp->msg, strlen(resp->msg));
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
    resp = createRsResponse(0, NULL, 200);
  }
#if 0
  else {
    HeapControl    *hc;
    Handler         hdlr;

    hc = markHeap();
    hdlr = handlers[hdr.opType];
    rs = hdlr.handler(ctx, &hdr);
    releaseHeap(hc);

    ctx->className = hdr.className;
    ctx->operation = hdr.opType;
  }
  rs.buffer = hdr.xmlBuffer;
#endif
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
