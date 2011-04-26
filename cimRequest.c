
/*
 * $Id: cimRequest.c,v 1.57 2010/01/15 20:58:33 buccella Exp $
 *
 * © Copyright IBM Corp. 2005, 2007
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
 * CIM operations request handler .
 *
 */

#include "cmpi/cmpidt.h"
#include "cimXmlGen.h"
#include "cimXmlParser.h"
#include "msgqueue.h"
#include "constClass.h"

#ifdef HANDLER_CIMXML
#include "cimRequest.h"
#endif
  
#ifdef HANDLER_CIMRS
#include "cimRsRequest.h"
#endif
  
#ifdef HAVE_QUALREP
#include "qualifier.h"
#endif
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

#ifdef SFCB_IX86
#define SFCB_ASM(x) asm(x)
#else
#define SFCB_ASM(x)
#endif

#ifdef LOCAL_CONNECT_ONLY_ENABLE
// from httpAdapter.c
int             noChunking = 0;
#endif                          // LOCAL_CONNECT_ONLY_ENABLE

typedef struct handler {
  RespSegments(*handler) (CimRequestContext *, RequestHdr * hdr);
} Handler;

typedef struct scanner {
  RequestHdr (*scan) (CimRequestContext*, char*,int*);
} Scanner;

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
extern MsgSegment setConstClassMsgSegment(CMPIConstClass * cl);
extern void     closeProviderContext(BinRequestContext * ctx);
extern CMPIStatus arraySetElementNotTrackedAt(CMPIArray *array,
                                              CMPICount index,
                                              CMPIValue * val,
                                              CMPIType type);
extern CMPIConstClass initConstClass(ClClass * cl);
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
/*
 * static char *cimMsgId[] = { "", "CIM_ERR_FAILED",
 * "CIM_ERR_ACCESS_DENIED", "CIM_ERR_INVALID_NAMESPACE",
 * "CIM_ERR_INVALID_PARAMETER", "CIM_ERR_INVALID_CLASS",
 * "CIM_ERR_NOT_FOUND", "CIM_ERR_NOT_SUPPORTED",
 * "CIM_ERR_CLASS_HAS_CHILDREN", "CIM_ERR_CLASS_HAS_INSTANCES",
 * "CIM_ERR_INVALID_SUPERCLASS", "CIM_ERR_ALREADY_EXISTS",
 * "CIM_ERR_NO_SUCH_PROPERTY", "CIM_ERR_TYPE_MISMATCH",
 * "CIM_ERR_QUERY_LANGUAGE_NOT_SUPPORTED", "CIM_ERR_INVALID_QUERY",
 * "CIM_ERR_METHOD_NOT_AVAILABLE", "CIM_ERR_METHOD_NOT_FOUND", }; 
 */
static char     iResponseIntro1[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
    "<CIM CIMVERSION=\"2.0\" DTDVERSION=\"2.0\">\n" "<MESSAGE ID=\"";
static char     iResponseIntro2[] =
    "\" PROTOCOLVERSION=\"1.0\">\n"
    "<SIMPLERSP>\n" "<IMETHODRESPONSE NAME=\"";
static char     iResponseIntro3Error[] = "\">\n";
static char     iResponseIntro3[] = "\">\n" "<IRETURNVALUE>\n";
static char     iResponseTrailer1Error[] =
    "</IMETHODRESPONSE>\n" "</SIMPLERSP>\n" "</MESSAGE>\n" "</CIM>";
static char     iResponseTrailer1[] =
    "</IRETURNVALUE>\n"
    "</IMETHODRESPONSE>\n" "</SIMPLERSP>\n" "</MESSAGE>\n" "</CIM>";

static char     responseIntro1[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
    "<CIM CIMVERSION=\"2.0\" DTDVERSION=\"2.0\">\n" "<MESSAGE ID=\"";
static char     responseIntro2[] =
    "\" PROTOCOLVERSION=\"1.0\">\n"
    "<SIMPLERSP>\n" "<METHODRESPONSE NAME=\"";
static char     responseIntro3Error[] = "\">\n";
static char     responseIntro3[] = "\">\n";     // "<RETURNVALUE>\n";
static char     responseTrailer1Error[] =
    "</METHODRESPONSE>\n" "</SIMPLERSP>\n" "</MESSAGE>\n" "</CIM>";
static char     responseTrailer1[] =
    // "</RETURNVALUE>\n"
    "</METHODRESPONSE>\n" "</SIMPLERSP>\n" "</MESSAGE>\n" "</CIM>";

static char     exportIndIntro1[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
    "<CIM CIMVERSION=\"2.0\" DTDVERSION=\"2.0\">\n" "<MESSAGE ID=\"";
static char     exportIndIntro2[] =
    "\" PROTOCOLVERSION=\"1.0\">\n"
    "<SIMPLEEXPREQ>\n"
    "<EXPMETHODCALL NAME=\"ExportIndication\">\n"
    "<EXPPARAMVALUE NAME=\"NewIndication\">\n";
static char     exportIndTrailer1[] =
    "</EXPPARAMVALUE>\n"
    "</EXPMETHODCALL>\n" "</SIMPLEEXPREQ>\n" "</MESSAGE>\n" "</CIM>";

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
  SFCB_ASM("int $3");
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

static char    *
getErrSegment(int rc, char *m)
{
  char           *msg;
  char           *escapedMsg;

  if (m && *m) {
    escapedMsg = XMLEscape(m, NULL);
    msg = sfcb_snprintf("<ERROR CODE=\"%d\" DESCRIPTION=\"%s\"/>\n",
                        rc, escapedMsg);
    free(escapedMsg);
  } else if (rc > 0 && rc < 18) {
    msg = sfcb_snprintf("<ERROR CODE=\"%d\" DESCRIPTION=\"%s\"/>\n",
                        rc, cimMsg[rc]);
  } else {
    msg = sfcb_snprintf("<ERROR CODE=\"%d\"/>\n", rc);
  }
  return msg;
}
/*
 * static char *getErrorSegment(CMPIStatus rc) { if (rc.msg &&
 * rc.msg->hdl) { return getErrSegment(rc.rc, (char *) rc.msg->hdl); }
 * return getErrSegment(rc.rc, NULL); } 
 */
char           *
getErrTrailer(int id, int rc, char *m)
{
  char           *msg;

  if (m && *m)
    msg = sfcb_snprintf("CIMStatusCodeDescription: %s\r\n", m);
  else if (rc > 0 && rc < 18)
    msg = sfcb_snprintf("CIMStatusCodeDescription: %s\r\n", cimMsg[rc]);
  else
    msg = strdup("CIMStatusCodeDescription: *Unknown*\r\n");
  return msg;
}

static          RespSegments
iMethodErrResponse(RequestHdr * hdr, char *error)
{
  RespSegments    rs = {
    NULL, 0, 0, NULL,
    {{0, iResponseIntro1},
     {0, hdr->id},
     {0, iResponseIntro2},
     {0, hdr->iMethod},
     {0, iResponseIntro3Error},
     {1, error},
     {0, iResponseTrailer1Error},
     }
  };

  return rs;
};

static          RespSegments
methodErrResponse(RequestHdr * hdr, char *error)
{
  RespSegments    rs = {
    NULL, 0, 0, NULL,
    {{0, responseIntro1},
     {0, hdr->id},
     {0, responseIntro2},
     {0, hdr->iMethod},
     {0, responseIntro3Error},
     {1, error},
     {0, responseTrailer1Error},
     }
  };

  return rs;
};

#ifdef ALLOW_UPDATE_EXPIRED_PW

static char    *
getErrExpiredSegment()
{
  char* msg = sfcb_snprintf("<ERROR CODE=\"2\" \
DESCRIPTION=\"User Account Expired\">\n\
<INSTANCE CLASSNAME=\"CIM_Error\">\n\
<PROPERTY NAME=\"ErrorType\" TYPE=\"uint16\">\
<VALUE>1</VALUE></PROPERTY>\n\
<PROPERTY NAME=\"OtherErrorType\" TYPE=\"string\">\
<VALUE>Password Expired</VALUE></PROPERTY>\n\
<PROPERTY NAME=\"ProbableCause\" TYPE=\"uint16\">\
<VALUE>117</VALUE></PROPERTY>\n\
</INSTANCE>\n</ERROR>\n");

  return msg;
}

#endif /* ALLOW_UPDATE_EXPIRED_PW */

static          RespSegments
ctxErrResponse(RequestHdr * hdr, BinRequestContext * ctx, int meth)
{
  MsgXctl        *xd = ctx->ctlXdata;
  char            msg[256];
  CMPIrc          err;

  switch (ctx->rc) {
  case MSG_X_NOT_SUPPORTED:
    hdr->errMsg = strdup("Operation not supported yy");
    err = CMPI_RC_ERR_NOT_SUPPORTED;
    break;
  case MSG_X_INVALID_CLASS:
    hdr->errMsg = strdup("Class not found");
    err = CMPI_RC_ERR_INVALID_CLASS;
    break;
  case MSG_X_INVALID_NAMESPACE:
    hdr->errMsg = strdup("Invalid namespace");
    err = CMPI_RC_ERR_INVALID_NAMESPACE;
    break;
  case MSG_X_PROVIDER_NOT_FOUND:
    hdr->errMsg = strdup("Provider not found or not loadable");
    err = CMPI_RC_ERR_NOT_FOUND;
    break;
  case MSG_X_FAILED:
    hdr->errMsg = strdup(xd->data);
    err = CMPI_RC_ERR_FAILED;
    break;
  default:
    sprintf(msg, "Internal error - %d\n", ctx->rc);
    hdr->errMsg = strdup(msg);
    err = CMPI_RC_ERR_FAILED;
  }
  if (meth)
    return methodErrResponse(hdr, getErrSegment(err, hdr->errMsg));
  return iMethodErrResponse(hdr, getErrSegment(err, hdr->errMsg));
};

static          RespSegments
iMethodGetTrailer(UtilStringBuffer * sb)
{
  RespSegments    rs = { NULL, 0, 0, NULL,
    {{2, (char *) sb},
     {0, iResponseTrailer1},
     {0, NULL},
     {0, NULL},
     {0, NULL},
     {0, NULL},
     {0, NULL}}
  };

  _SFCB_ENTER(TRACE_CIMXMLPROC, "iMethodGetTrailer");
  _SFCB_RETURN(rs);
}

static          RespSegments
iMethodResponse(RequestHdr * hdr, UtilStringBuffer * sb)
{
  RespSegments    rs = { NULL, 0, 0, NULL,
    {{0, iResponseIntro1},
     {0, hdr->id},
     {0, iResponseIntro2},
     {0, hdr->iMethod},
     {0, iResponseIntro3},
     {2, (char *) sb},
     {0, iResponseTrailer1}}
  };
  _SFCB_ENTER(TRACE_CIMXMLPROC, "iMethodResponse");
  _SFCB_RETURN(rs);
};

static          RespSegments
methodResponse(RequestHdr * hdr, UtilStringBuffer * sb)
{
  RespSegments    rs = { NULL, 0, 0, NULL,
    {{0, responseIntro1},
     {0, hdr->id},
     {0, responseIntro2},
     {0, hdr->iMethod},
     {0, responseIntro3},
     {2, (char *) sb},
     {0, responseTrailer1}}
  };

  _SFCB_ENTER(TRACE_CIMXMLPROC, "methodResponse");
  _SFCB_RETURN(rs);
};

ExpSegments
exportIndicationReq(CMPIInstance *ci, char *id)
{
  UtilStringBuffer *sb = UtilFactory->newStrinBuffer(1024);
  ExpSegments     xs = {
    {{0, exportIndIntro1},
     {0, id},
     {0, exportIndIntro2},
     {0, NULL},
     {0, NULL},
     {2, (char *) sb},
     {0, exportIndTrailer1}}
  };

  _SFCB_ENTER(TRACE_CIMXMLPROC, "exportIndicationReq");
  instance2xml(ci, sb, 0);
  _SFCB_RETURN(xs);
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

  _SFCB_ENTER(TRACE_CIMXMLPROC, "genEnumResponses");

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

  if (binCtx->oHdr->type == OPS_EnumerateClassNames)
    enum2xml(enm, sb, binCtx->type, XML_asClassName, binCtx->bHdr->flags);
  else if (binCtx->oHdr->type == OPS_EnumerateClasses)
    enum2xml(enm, sb, binCtx->type, XML_asClass, binCtx->bHdr->flags);
  else
    enum2xml(enm, sb, binCtx->type, binCtx->xmlAs, binCtx->bHdr->flags);

  _SFCB_RETURN(sb);
}

static          RespSegments
genResponses(BinRequestContext * binCtx,
             BinResponseHdr ** resp, int arrlen)
{
  RespSegments    rs;
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

  _SFCB_ENTER(TRACE_CIMXMLPROC, "genResponses");

  genheap = markHeap();
  sb = genEnumResponses(binCtx, resp, arrlen);

  rs = iMethodResponse(binCtx->rHdr, sb);
  if (binCtx->pDone < binCtx->pCount)
    rs.segments[6].txt = NULL;
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
static          RespSegments
genQualifierResponses(BinRequestContext * binCtx, BinResponseHdr * resp)
{
  RespSegments    rs;
  UtilStringBuffer *sb;
  CMPIArray      *ar;
  int             j;
  CMPIEnumeration *enm;
  void           *object;
  CMPIStatus      rc;
  void           *genheap;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "genQualifierResponses");
  genheap = markHeap();
  ar = TrackedCMPIArray(resp->count, binCtx->type, NULL);

  for (j = 0; j < resp->count; j++) {
    object = relocateSerializedQualifier(resp->object[j].data);
    rc = arraySetElementNotTrackedAt(ar, j, (CMPIValue *) & object,
                                     binCtx->type);
  }

  enm = sfcb_native_new_CMPIEnumeration(ar, NULL);
  sb = UtilFactory->newStrinBuffer(1024);

  qualiEnum2xml(enm, sb);
  rs = iMethodResponse(binCtx->rHdr, sb);
  releaseHeap(genheap);
  _SFCB_RETURN(rs);
}
#endif

RespSegments
genFirstChunkResponses(BinRequestContext * binCtx,
                       BinResponseHdr ** resp, int arrlen, int moreChunks)
{
  UtilStringBuffer *sb;
  RespSegments    rs;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "genResponses");

  sb = genEnumResponses(binCtx, resp, arrlen);

  rs = iMethodResponse(binCtx->rHdr, sb);
  if (moreChunks || binCtx->pDone < binCtx->pCount)
    rs.segments[6].txt = NULL;
  _SFCB_RETURN(rs);
}

RespSegments
genChunkResponses(BinRequestContext * binCtx,
                  BinResponseHdr ** resp, int arrlen)
{
  RespSegments    rs = { NULL, 0, 0, NULL,
    {{2, NULL},
     {0, NULL},
     {0, NULL},
     {0, NULL},
     {0, NULL},
     {0, NULL},
     {0, NULL}}
  };

  _SFCB_ENTER(TRACE_CIMXMLPROC, "genChunkResponses");
  rs.segments[0].txt = (char *) genEnumResponses(binCtx, resp, arrlen);
  _SFCB_RETURN(rs);
}

RespSegments
genLastChunkResponses(BinRequestContext * binCtx,
                      BinResponseHdr ** resp, int arrlen)
{
  UtilStringBuffer *sb;
  RespSegments    rs;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "genResponses");

  sb = genEnumResponses(binCtx, resp, arrlen);

  rs = iMethodGetTrailer(sb);
  _SFCB_RETURN(rs);
}

RespSegments
genFirstChunkErrorResponse(BinRequestContext * binCtx, int rc, char *msg)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "genFirstChunkErrorResponse");
  _SFCB_RETURN(iMethodErrResponse(binCtx->rHdr, getErrSegment(rc, msg)));
}

static          RespSegments
getClass(CimRequestContext * ctx, RequestHdr * hdr)
{
  UtilStringBuffer *sb;
  int             irc;
  BinResponseHdr *resp;
  CMPIConstClass *cls;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "getClass");

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    resp = invokeProvider(hdr->binCtx);
    closeProviderContext(hdr->binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      cls = relocateSerializedConstClass(resp->object[0].data);
      sb = UtilFactory->newStrinBuffer(1024);
      cls2xml(cls, sb, hdr->binCtx->bHdr->flags);
      if (resp) {
        free(resp);
      }
      free(hdr->binCtx->bHdr);
      _SFCB_RETURN(iMethodResponse(hdr, sb));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  free(hdr->binCtx->bHdr);
  closeProviderContext(hdr->binCtx);

  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
deleteClass(CimRequestContext * ctx, RequestHdr * hdr)
{
  int             irc;
  BinResponseHdr *resp;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "deleteClass");

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    resp = invokeProvider(hdr->binCtx);
    closeProviderContext(hdr->binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      if (resp) {
        free(resp);
      }
      free(hdr->binCtx->bHdr);
      _SFCB_RETURN(iMethodResponse(hdr, NULL));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
createClass(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "createClass");
  int             irc;
  BinResponseHdr *resp;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    resp = invokeProvider(hdr->binCtx);
    closeProviderContext(hdr->binCtx);
    resp->rc--;
// TODO: How will we free this, now that it's getting
// built somewhere else?
    CMPIConstClass *cl = ((CMPIConstClass *)
                         ((CreateClassReq *)(hdr->binCtx->bHdr))
                         ->cls.data);
    ClClassFreeClass(cl->hdl);
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
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
enumClassNames(CimRequestContext * ctx, RequestHdr * hdr)
{
  int             irc,
                  l = 0,
                  err = 0;
  BinResponseHdr **resp;
  RespSegments    rs;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "enumClassNames");

  hdr->binCtx->commHndl = ctx->commHndl;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    _SFCB_TRACE(1, ("--- Calling Providers"));
    resp = invokeProviders(hdr->binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Provider"));
    closeProviderContext(hdr->binCtx);
    if (err == 0) {
      rs = genResponses(hdr->binCtx, resp, l);
    } else {
      rs = iMethodErrResponse(hdr, getErrSegment(resp[err - 1]->rc,
                                                 (char *) resp[err -
                                                               1]->object
                                                 [0].data));
    }
    freeResponseHeaders(resp, hdr->binCtx);
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
enumClasses(CimRequestContext * ctx, RequestHdr * hdr)
{
  int             l = 0,
                  irc,
                  err = 0;
  BinResponseHdr **resp;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "enumClasses");
  if (noChunking || ctx->teTrailers == 0)
    hdr->chunkedMode = hdr->binCtx->chunkedMode = 0;
  else {
    hdr->binCtx->bHdr->flags |= FL_chunked;
    hdr->chunkedMode = hdr->binCtx->chunkedMode = 1;
  }

  hdr->binCtx->commHndl = ctx->commHndl;
  hdr->binCtx->chunkFncs = ctx->chunkFncs;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    _SFCB_TRACE(1, ("--- Calling Providers"));
    resp = invokeProviders(hdr->binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Provider"));

    closeProviderContext(hdr->binCtx);

    if (noChunking || ctx->teTrailers == 0) {
      if (err == 0) {
        rs = genResponses(hdr->binCtx, resp, l);
      } else {
        rs = iMethodErrResponse(hdr, getErrSegment(resp[err - 1]->rc,
                                                   (char *) resp[err -
                                                                 1]->object
                                                   [0].data));
      }
      freeResponseHeaders(resp, hdr->binCtx);
      _SFCB_RETURN(rs);
    }
    freeResponseHeaders(resp, hdr->binCtx);

    rs.chunkedMode = 1;
    rs.rc = err;
    rs.errMsg = NULL;
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
getInstance(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "getInstance");
  CMPIInstance   *inst;
  UtilStringBuffer *sb;
  int             irc;
  BinResponseHdr *resp;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);
  _SFCB_TRACE(1, ("--- Provider context gotten"));

  if (irc == MSG_X_PROVIDER) {
    resp = invokeProvider(hdr->binCtx);
    closeProviderContext(hdr->binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      inst = relocateSerializedInstance(resp->object[0].data);
      sb = UtilFactory->newStrinBuffer(1024);
      instance2xml(inst, sb, hdr->binCtx->bHdr->flags);
      free(hdr->binCtx->bHdr);
      if (resp) {
        free(resp);
      }
      _SFCB_RETURN(iMethodResponse(hdr, sb));
    }
    free(hdr->binCtx->bHdr);
    RespSegments    rs;
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    _SFCB_RETURN(rs);
  }
  free(hdr->binCtx->bHdr);
  closeProviderContext(hdr->binCtx);

  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
deleteInstance(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "deleteInstance");
  int             irc;
  BinResponseHdr *resp;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);
  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    resp = invokeProvider(hdr->binCtx);
    closeProviderContext(hdr->binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      if (resp) {
        free(resp);
      }
      free(hdr->binCtx->bHdr);
      _SFCB_RETURN(iMethodResponse(hdr, NULL));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
createInstance(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "createInst");
  CMPIObjectPath *path;
  UtilStringBuffer *sb;
  int             irc;
  BinResponseHdr *resp;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);
  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    resp = invokeProvider(hdr->binCtx);
    closeProviderContext(hdr->binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      path = relocateSerializedObjectPath(resp->object[0].data);
      sb = UtilFactory->newStrinBuffer(1024);
      instanceName2xml(path, sb);
      if (resp) {
        free(resp);
      }
      free(hdr->binCtx->bHdr);
      _SFCB_RETURN(iMethodResponse(hdr, sb));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
modifyInstance(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "modifyInstance");
  int             irc;
  BinResponseHdr *resp;
  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    resp = invokeProvider(hdr->binCtx);
    closeProviderContext(hdr->binCtx);
    free(hdr->binCtx->bHdr);
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
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);

  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
enumInstanceNames(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "enumInstanceNames");
  int             irc,
                  l = 0,
                  err = 0;
  BinResponseHdr **resp;
  RespSegments    rs;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    _SFCB_TRACE(1, ("--- Calling Providers"));
    resp = invokeProviders(hdr->binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Provider"));

    closeProviderContext(hdr->binCtx);
    if (err == 0) {
      rs = genResponses(hdr->binCtx, resp, l);
    } else {
      rs = iMethodErrResponse(hdr, getErrSegment(resp[err - 1]->rc,
                                                 (char *) resp[err -
                                                               1]->object
                                                 [0].data));
    }
    freeResponseHeaders(resp, hdr->binCtx);
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
enumInstances(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "enumInstances");

  int             irc,
                  l = 0,
                  err = 0;
  BinResponseHdr **resp;

  if (noChunking || ctx->teTrailers == 0)
    hdr->chunkedMode = hdr->binCtx->chunkedMode = 0;
  else {
    hdr->binCtx->bHdr->flags |= FL_chunked;
    hdr->chunkedMode = hdr->binCtx->chunkedMode = 1;
  }

  hdr->binCtx->commHndl = ctx->commHndl;
  hdr->binCtx->chunkFncs = ctx->chunkFncs;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);
  _SFCB_TRACE(1, ("--- Provider context gotten irc: %d", irc));

  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    _SFCB_TRACE(1, ("--- Calling Providers"));
    resp = invokeProviders(hdr->binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Providers"));
    closeProviderContext(hdr->binCtx);

    if (noChunking || ctx->teTrailers == 0) {
      if (err == 0) {
        rs = genResponses(hdr->binCtx, resp, l);
      } else {
        rs = iMethodErrResponse(hdr, getErrSegment(resp[err - 1]->rc,
                                                   (char *) resp[err -
                                                                 1]->object
                                                   [0].data));
      }
      freeResponseHeaders(resp, hdr->binCtx);
      free(hdr->binCtx->bHdr);
      _SFCB_RETURN(rs);
    }
    freeResponseHeaders(resp, hdr->binCtx);
    free(hdr->binCtx->bHdr);
    rs.chunkedMode = 1;
    rs.rc = err;
    rs.errMsg = NULL;
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
execQuery(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "execQuery");

  int             irc,
                  l = 0,
      err = 0;
  BinResponseHdr **resp;

  hdr->binCtx->commHndl = ctx->commHndl;
  hdr->binCtx->chunkFncs = ctx->chunkFncs;

  if (noChunking || ctx->teTrailers == 0)
    hdr->chunkedMode = hdr->binCtx->chunkedMode = 0;
  else {
    hdr->binCtx->bHdr->flags |= FL_chunked;
    hdr->chunkedMode = hdr->binCtx->chunkedMode = 1;
  }

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));

  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    _SFCB_TRACE(1, ("--- Calling Provider"));
    resp = invokeProviders(hdr->binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Provider"));
    closeProviderContext(hdr->binCtx);

    if (noChunking || ctx->teTrailers == 0) {
      if (err == 0) {
        rs = genResponses(hdr->binCtx, resp, l);
      } else {
        rs = iMethodErrResponse(hdr, getErrSegment(resp[err - 1]->rc,
                                                   (char *) resp[err -
                                                                 1]->object
                                                   [0].data));
      }
      freeResponseHeaders(resp, hdr->binCtx);
      _SFCB_RETURN(rs);
    }
    freeResponseHeaders(resp, hdr->binCtx);
    rs.chunkedMode = 1;
    rs.rc = err;
    rs.errMsg = NULL;
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
associatorNames(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "associatorNames");
  int             irc,
                  l = 0,
      err = 0;
  BinResponseHdr **resp;

  hdr->binCtx->commHndl = ctx->commHndl;
  hdr->binCtx->chunkFncs = ctx->chunkFncs;
  hdr->chunkedMode = hdr->binCtx->chunkedMode = 0;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);
  _SFCB_TRACE(1, ("--- Provider context gotten"));

  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    _SFCB_TRACE(1, ("--- Calling Providers"));
    resp = invokeProviders(hdr->binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Providers"));

    closeProviderContext(hdr->binCtx);
    if (err == 0) {
      rs = genResponses(hdr->binCtx, resp, l);
    } else {
      rs = iMethodErrResponse(hdr, getErrSegment(resp[err - 1]->rc,
                                                 (char *) resp[err -
                                                               1]->object
                                                 [0].data));
    }
    freeResponseHeaders(resp, hdr->binCtx);
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  free(hdr->binCtx->bHdr);
  closeProviderContext(hdr->binCtx);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
associators(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "associators");

  int             irc,
                  l = 0,
                  err = 0;
  BinResponseHdr **resp;

  hdr->binCtx->commHndl = ctx->commHndl;
  hdr->binCtx->chunkFncs = ctx->chunkFncs;

  if (noChunking || ctx->teTrailers == 0)
    hdr->chunkedMode = hdr->binCtx->chunkedMode = 0;
  else {
    hdr->binCtx->bHdr->flags |= FL_chunked;
    hdr->chunkedMode = hdr->binCtx->chunkedMode = 1;
  }

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    _SFCB_TRACE(1, ("--- Calling Provider"));
    resp = invokeProviders(hdr->binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Provider"));

    closeProviderContext(hdr->binCtx);

    if (noChunking || ctx->teTrailers == 0) {
      if (err == 0) {
        rs = genResponses(hdr->binCtx, resp, l);
      } else {
        rs = iMethodErrResponse(hdr, getErrSegment(resp[err - 1]->rc,
                                                   (char *) resp[err -
                                                                 1]->object
                                                   [0].data));
      }
      freeResponseHeaders(resp, hdr->binCtx);
      free(hdr->binCtx->bHdr);
      _SFCB_RETURN(rs);
    }

    freeResponseHeaders(resp, hdr->binCtx);
    free(hdr->binCtx->bHdr);
    rs.chunkedMode = 1;
    rs.rc = err;
    rs.errMsg = NULL;
    _SFCB_RETURN(rs);
  }
  free(hdr->binCtx->bHdr);
  closeProviderContext(hdr->binCtx);

  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
referenceNames(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "referenceNames");
  int             irc,
                  l = 0,
                  err = 0;
  BinResponseHdr **resp;

  hdr->binCtx->commHndl = ctx->commHndl;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);
  _SFCB_TRACE(1, ("--- Provider context gotten"));

  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    _SFCB_TRACE(1, ("--- Calling Providers"));
    resp = invokeProviders(hdr->binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Providers"));

    closeProviderContext(hdr->binCtx);
    if (err == 0) {
      rs = genResponses(hdr->binCtx, resp, l);
    } else {
      rs = iMethodErrResponse(hdr, getErrSegment(resp[err - 1]->rc,
                                                 (char *) resp[err -
                                                               1]->object
                                                 [0].data));
    }
    freeResponseHeaders(resp, hdr->binCtx);
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
references(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "references");

  int             irc,
                  l = 0,
                  err = 0;
  BinResponseHdr **resp;

  hdr->binCtx->commHndl = ctx->commHndl;
  hdr->binCtx->chunkFncs = ctx->chunkFncs;

  if (noChunking || ctx->teTrailers == 0)
    hdr->chunkedMode = hdr->binCtx->chunkedMode = 0;
  else {
    hdr->binCtx->bHdr->flags |= FL_chunked;
    hdr->chunkedMode = hdr->binCtx->chunkedMode = 1;
  }

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    _SFCB_TRACE(1, ("--- Calling Provider"));
    resp = invokeProviders(hdr->binCtx, &err, &l);
    _SFCB_TRACE(1, ("--- Back from Provider"));
    closeProviderContext(hdr->binCtx);

    if (noChunking || ctx->teTrailers == 0) {
      if (err == 0) {
        rs = genResponses(hdr->binCtx, resp, l);
      } else {
        rs = iMethodErrResponse(hdr, getErrSegment(resp[err - 1]->rc,
                                                   (char *) resp[err -
                                                                 1]->object
                                                   [0].data));
      }
      freeResponseHeaders(resp, hdr->binCtx);
      free(hdr->binCtx->bHdr);
      _SFCB_RETURN(rs);
    }
    freeResponseHeaders(resp, hdr->binCtx);
    free(hdr->binCtx->bHdr);
    rs.chunkedMode = 1;
    rs.rc = err;
    rs.errMsg = NULL;
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);

  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
invokeMethod(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "invokeMethod");
  int             irc;
  CMPIArgs       *out;
  UtilStringBuffer *sb;
  BinResponseHdr *resp;
  RespSegments    rsegs;

  char *method_name = (char *) (((InvokeMethodReq *)
                                 (hdr->binCtx->bHdr))->method.data);
  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_SFCB_PROVIDER) {
    if(*method_name == '_') {
      RespSegments  rs;
      rs = methodErrResponse(hdr, getErrSegment(CMPI_RC_ERR_ACCESS_DENIED, NULL));
      closeProviderContext(hdr->binCtx);
      _SFCB_RETURN(rs);
    } else {
      irc = MSG_X_PROVIDER;
    }
  }
                                                
  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    resp = invokeProvider(hdr->binCtx);
    closeProviderContext(hdr->binCtx);
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
      free(hdr->binCtx->bHdr);
      _SFCB_RETURN(rsegs);
    }
    rs = methodErrResponse(hdr, getErrSegment(resp->rc,
                                              (char *) resp->object[0].
                                              data));
    if (resp) {
      free(resp);
    }
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 1));
}

static          RespSegments
getProperty(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "getProperty");
  CMPIInstance   *inst;
  CMPIData        data;
  UtilStringBuffer *sb;
  CMPIString     *tmpString = NewCMPIString(NULL, NULL);
  int             irc;
  BinResponseHdr *resp;
  RespSegments    rsegs;
  XtokGetProperty *req = (XtokGetProperty *) hdr->cimRequest;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    resp = invokeProvider(hdr->binCtx);
    closeProviderContext(hdr->binCtx);
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
      free(hdr->binCtx->bHdr);
      _SFCB_RETURN(rsegs);
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    CMRelease(tmpString);
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  CMRelease(tmpString);
  free(hdr->binCtx->bHdr);
  closeProviderContext(hdr->binCtx);

  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
setProperty(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "setProperty");
  int             irc;
  BinResponseHdr *resp;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));

  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    resp = invokeProvider(hdr->binCtx);
    closeProviderContext(hdr->binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      if (resp) {
        free(resp);
      }
      free(hdr->binCtx->bHdr);
      _SFCB_RETURN(iMethodResponse(hdr, NULL));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

#ifdef HAVE_QUALREP
static          RespSegments
enumQualifiers(CimRequestContext * ctx, RequestHdr * hdr)
{
  int             irc;
  BinResponseHdr *resp;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "enumQualifiers");

  hdr->binCtx->commHndl = ctx->commHndl;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    _SFCB_TRACE(1, ("--- Calling Providers"));
    resp = invokeProvider(hdr->binCtx);
    _SFCB_TRACE(1, ("--- Back from Provider"));
    closeProviderContext(hdr->binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      rs = genQualifierResponses(hdr->binCtx, resp);
    } else {
      rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                                 (char *) resp->object[0].
                                                 data));
    }
    if (resp) {
      free(resp);
    }
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
getQualifier(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "getQualifier");
  CMPIQualifierDecl *qual;
  UtilStringBuffer *sb;
  int             irc;
  BinResponseHdr *resp;
  RespSegments    rsegs;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    resp = invokeProvider(hdr->binCtx);
    closeProviderContext(hdr->binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      qual = relocateSerializedQualifier(resp->object[0].data);
      sb = UtilFactory->newStrinBuffer(1024);
      qualifierDeclaration2xml(qual, sb);
      rsegs = iMethodResponse(hdr, sb);
      if (resp) {
        free(resp);
      }
      free(hdr->binCtx->bHdr);
      _SFCB_RETURN(rsegs);
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
deleteQualifier(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "deleteQualifier");
  int             irc;
  BinResponseHdr *resp;

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));
  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    resp = invokeProvider(hdr->binCtx);
    closeProviderContext(hdr->binCtx);
    resp->rc--;
    if (resp->rc == CMPI_RC_OK) {
      if (resp) {
        free(resp);
      }
      free(hdr->binCtx->bHdr);
      _SFCB_RETURN(iMethodResponse(hdr, NULL));
    }
    rs = iMethodErrResponse(hdr, getErrSegment(resp->rc,
                                               (char *) resp->object[0].
                                               data));
    if (resp) {
      free(resp);
    }
    free(hdr->binCtx->bHdr);
    _SFCB_RETURN(rs);
  }
  closeProviderContext(hdr->binCtx);
  free(hdr->binCtx->bHdr);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}

static          RespSegments
setQualifier(CimRequestContext * ctx, RequestHdr * hdr)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "setQualifier");
  int             irc;
  BinResponseHdr *resp;
  CMPIQualifierDecl *qual = (CMPIQualifierDecl *)
                            (((SetQualifierReq *)
                              (hdr->binCtx->bHdr))->qualifier.data);

  _SFCB_TRACE(1, ("--- Getting Provider context"));
  irc = getProviderContext(hdr->binCtx);

  _SFCB_TRACE(1, ("--- Provider context gotten"));

  if (irc == MSG_X_PROVIDER) {
    RespSegments    rs;
    resp = invokeProvider(hdr->binCtx);
    closeProviderContext(hdr->binCtx);
    qual->ft->release(qual);
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
  closeProviderContext(hdr->binCtx);
  qual->ft->release(qual);
  _SFCB_RETURN(ctxErrResponse(hdr, hdr->binCtx, 0));
}
#endif

static          RespSegments
notSupported(CimRequestContext * ctx, RequestHdr * hdr)
{
  return iMethodErrResponse(hdr, strdup
                            ("<ERROR CODE=\"7\" DESCRIPTION=\"Operation not supported xx\"/>\n"));
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

RespSegments sendHdrToHandler(RequestHdr* hdr, CimRequestContext* ctx) {

  RespSegments    rs;
  Handler         hdlr;
  HeapControl    *hc;

  hc = markHeap();
  hdlr = handlers[hdr->opType];
  rs = hdlr.handler(ctx, hdr);
  releaseHeap(hc);

  ctx->className = hdr->className;
  ctx->operation = hdr->opType;

  return rs;
}

static Scanner scanners[] = {
#ifdef HANDLER_CIMRS
  {scanCimRsRequest},
#endif
#ifdef HANDLER_CIMXML
  {scanCimXmlRequest},
#endif
};

static int scanner_count = sizeof(scanners) / sizeof(Scanner);

RespSegments
handleCimRequest(CimRequestContext * ctx, int flags)
{
  RespSegments    rs;
  RequestHdr      hdr;
#ifdef SFCB_DEBUG
  struct rusage   us,
                  ue;
  struct timeval  sv,
                  ev;
  int             parserc = 1; /* scanner recognition code
                           0 = format understood
                           1 = format not understood */

  if (_sfcb_trace_mask & TRACE_RESPONSETIMING) {
    gettimeofday(&sv, NULL);
    getrusage(RUSAGE_SELF, &us);
  }
#endif
  _SFCB_ENTER(TRACE_CIMXMLPROC, "handleCimXmlRequest");

  /* Walk over known request scanners */
  int i=0;
  while (i < scanner_count) {
    /* Sending both params is a bit redundant, but it
     saves having to rework all of the operations
     at once. This should be changed after all ops
     are handled in the parser. */
    hdr = scanners[i].scan(ctx, ctx->cimDoc, &parserc);
    if (parserc == 0) {
      /* The scanner recognizes the request so we don't
       * need to give it to anymore scanners. */
      break;
    }
    i++;
  }
  if (parserc == 0) {
    // Found a valid parser
    /* This needs to be assigned here since hdr was
     returned by value. That means we can probably
     stop assigning it in the parser. It is also
     possible that we might not need this cycle in
     the data structure if we make some minor changes
     to the params we pass around. */
    hdr.binCtx->rHdr = &hdr;

#ifdef SFCB_DEBUG
    if (_sfcb_trace_mask & TRACE_RESPONSETIMING) {
      gettimeofday(&ev, NULL);
      getrusage(RUSAGE_SELF, &ue);
      _sfcb_trace(1, __FILE__, __LINE__,
                _sfcb_format_trace
                ("-#- Content Parsing %.5u %s-%s real: %f user: %f sys: %f \n",
                 ctx->sessionId, opsName[hdr.opType], "n/a",
                 timevalDiff(&sv, &ev), timevalDiff(&us.ru_utime,
                                                    &ue.ru_utime),
                 timevalDiff(&us.ru_stime, &ue.ru_stime)));
    }
#endif
    fprintf(stderr, "flags=%d\n", flags);
    if (hdr.rc) {
      if (hdr.methodCall) {
        rs = methodErrResponse(&hdr, getErrSegment(CMPI_RC_ERR_FAILED,
                                                 "invalid methodcall payload"));
      } else {
        if(!hdr.errMsg) hdr.errMsg = strdup("invalid imethodcall payload");
        rs = iMethodErrResponse(&hdr, getErrSegment(hdr.rc,
                                                  hdr.errMsg));
//      rs = iMethodErrResponse(&hdr, getErrSegment(CMPI_RC_ERR_FAILED,
//                                                  "invalid imethodcall XML"));
      }
    } 
#ifdef ALLOW_UPDATE_EXPIRED_PW
    else if (flags) {
      fprintf(stderr, "in hcr, flags set\n");
      /* request from user with an expired password AND requesting password update */
      if (flags == (HCR_UPDATE_PW + HCR_EXPIRED_PW) &&
          (strcasecmp(hdr.className, "SFCB_Account") == 0) && hdr.methodCall) {
        fprintf(stderr, " in hcr, got update_pw flag and expired flag\n");
	fprintf(stderr, "call to SFCB_Account\n");
	rs = sendHdrToHandler(&hdr, ctx);
      }
      else {    /* expired user tried to invoke non-UpdatePassword request */
	if (hdr.methodCall) { 
	  rs = methodErrResponse(&hdr, getErrExpiredSegment());
	} else {
	  rs = iMethodErrResponse(&hdr, getErrExpiredSegment());
	}
      }
    }
#endif  /* ALLOW_UPDATE_EXPIRED_PW */

    else {
      fprintf(stderr, "sendHdrToHandler (normal)\n");
      rs = sendHdrToHandler(&hdr, ctx);
//       hc = markHeap();
//       hdlr = handlers[hdr.opType];
//       rs = hdlr.handler(ctx, &hdr);
//       releaseHeap(hc);

//       ctx->className = hdr.className;
//       ctx->operation = hdr.opType;
    }
    rs.buffer = hdr.buffer;
    rs.rc=0;
  } else {
    // No valid parser found
    hdr.errMsg = strdup("Unrecognized content type");
    rs = iMethodErrResponse(&hdr, getErrSegment(hdr.rc, hdr.errMsg));
    rs.rc=1;
  }

  // This will be dependent on the type of request being processed.
  freeCimXmlRequest(hdr);

  return rs;
}

int
cleanupCimXmlRequest(RespSegments * rs)
{
  XmlBuffer *xmb = (XmlBuffer *)rs->buffer;
  free(xmb->base);
  free(xmb);
  return 0;
}
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
