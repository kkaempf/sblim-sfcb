%{

/*
 * cimXmlOps.y
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
 * Author:       Adrian Schuur <schuur@de.ibm.com>
 *
 * Description:
 *
 * CIM XML grammar for sfcb.
 *
*/


/*
**==============================================================================
**
** Includes
**
**==============================================================================
*/

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "cmpidtx.h"
#include "cimXmlGen.h"
#include "cimXmlParser.h"
#include "cimXmlOps.h"
#include "objectImpl.h"
#include "constClass.h"
#include "native.h"
#include "control.h"

extern CMPIConstClass initConstClass(ClClass * cl);
extern MsgSegment setConstClassMsgSegment(CMPIConstClass * cl);
extern MsgSegment setInstanceMsgSegment(CMPIInstance *ci);
extern MsgSegment setArgsMsgSegment(CMPIArgs * args);
#ifdef HAVE_QUALREP
extern CMPIQualifierDecl initQualifier(ClQualifierDeclaration * qual);
#endif

int updateMethodParamTypes(RequestHdr * hdr);
//
// Define the global parser state object:
//

#define YYERROR_VERBOSE 1


/* assumed max number of elements; used for initial malloc */
#define VALUEARRAY_MAX_START 32
#define VALUEREFARRAY_MAX_START 32
#define KEYBINDING_MAX_START 12

#ifdef LOCAL_CONNECT_ONLY_ENABLE
// from httpAdapter.c
int             noChunking = 0;
#endif                          // LOCAL_CONNECT_ONLY_ENABLE
extern int      noChunking;



extern int yyerror(void *, const char *);
extern int yylex (void *lvalp, ParserControl *parm);
//extern MsgSegment setInstanceMsgSegment(const CMPIInstance *ci);


static void setRequest(void *parm, void *req, unsigned long size, int type)
{
   ((ParserControl*)parm)->reqHdr.cimRequestLength=size;
   ((ParserControl*)parm)->reqHdr.cimRequest=malloc(size);
   memcpy(((ParserControl*)parm)->reqHdr.cimRequest,req,size);
   ((ParserControl*)parm)->reqHdr.opType = type;
}

static void
buildAssociatorNamesRequest(void *parm)
{
  CMPIObjectPath *path;
  AssociatorNamesReq *sreq;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  int       i, m;
  BinRequestContext *binCtx = hdr->binCtx;
  CMPIType        type;
  CMPIValue       val,
                 *valp;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "buildAssociatorNamesRequest");
  memset(binCtx, 0, sizeof(BinRequestContext));
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
    free(hdr->binCtx->bHdr);
    hdr->rc = CMPI_RC_ERR_NOT_SUPPORTED;
    hdr->errMsg = strdup("AssociatorNames operation for classes not supported.");
    return;
  }
  if (!req->objNameSet) {
    free(hdr->binCtx->bHdr);
    hdr->rc = CMPI_RC_ERR_INVALID_PARAMETER;
    hdr->errMsg = strdup("ObjectName parameter required.");
    return;
  }
  sreq = calloc(1, sizeof(*sreq)); 
  sreq->hdr.operation = OPS_AssociatorNames;
  sreq->hdr.count = AIN_REQ_REG_SEGMENTS;

  sreq->objectPath = setObjectPathMsgSegment(path);

  sreq->resultClass = req->op.resultClass;
  sreq->role = req->op.role;
  sreq->assocClass = req->op.assocClass;
  sreq->resultRole = req->op.resultRole;
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->hdr.sessionId = hdr->sessionId;

  req->op.className = req->op.assocClass;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = 0;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq); 
  binCtx->type = CMPI_ref;
  binCtx->xmlAs = XML_asObjectPath;
  binCtx->noResp = 0;
  binCtx->pAs = NULL;

}

static void
buildAssociatorsRequest(void *parm)
{

  CMPIObjectPath *path;
  AssociatorsReq *sreq;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  int             i,m,
      sreqSize = sizeof(AssociatorsReq);        // -sizeof(MsgSegment);
  BinRequestContext *binCtx = hdr->binCtx;
  CMPIType        type;
  CMPIValue       val,
                 *valp;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "buildAssociatorsRequest");

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokAssociators *req = (XtokAssociators *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);

  if (req->objectName.bindings.next == 0) {
    free(hdr->binCtx->bHdr);
    hdr->rc = CMPI_RC_ERR_NOT_SUPPORTED;
    hdr->errMsg = strdup("Associator operation for classes not supported.");
    return;
  }
  if (!req->objNameSet) {
    free(hdr->binCtx->bHdr);
    hdr->rc = CMPI_RC_ERR_INVALID_PARAMETER;
    hdr->errMsg = strdup("ObjectName parameter required.");
    return;
  }

  if (req->properties)
    sreqSize += req->properties * sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_Associators;
  sreq->hdr.count = req->properties + AI_REQ_REG_SEGMENTS;

  for (i = 0, m = req->objectName.bindings.next; i < m; i++) {
    valp = getKeyValueTypePtr(req->objectName.bindings.keyBindings[i].type,
                              req->objectName.bindings.keyBindings[i].
                              value,
                              &req->objectName.bindings.keyBindings[i].ref,
                              &val, &type, req->op.nameSpace.data);
    CMAddKey(path, req->objectName.bindings.keyBindings[i].name, valp,
             type);
  }

  sreq->objectPath = setObjectPathMsgSegment(path);

  sreq->resultClass = req->op.resultClass;
  sreq->role = req->op.role;
  sreq->assocClass = req->op.assocClass;
  sreq->resultRole = req->op.resultRole;
  sreq->hdr.flags = req->flags;
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->hdr.sessionId = hdr->sessionId;

  for (i = 0; i < req->properties; i++)
    sreq->properties[i] =
        setCharsMsgSegment(req->propertyList.values[i].value);

  req->op.className = req->op.assocClass;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = req->flags;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sreqSize;
  binCtx->type = CMPI_instance;
  binCtx->xmlAs = XML_asObj;
  binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildEnumInstanceRequest(void *parm)
{
  CMPIObjectPath *path;
  EnumInstancesReq *sreq;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  int    i,
      sreqSize = sizeof(EnumInstancesReq);      // -sizeof(MsgSegment);
  BinRequestContext *binCtx = hdr->binCtx;
  
  _SFCB_ENTER(TRACE_CIMXMLPROC, "buildEnumInstancesRequest");

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokEnumInstances *req = (XtokEnumInstances *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  if (req->properties)
    sreqSize += req->properties * sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_EnumerateInstances;
  sreq->hdr.count = req->properties + EI_REQ_REG_SEGMENTS;

  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->hdr.sessionId = hdr->sessionId;

  for (i = 0; i < req->properties; i++) {
    sreq->properties[i] =
        setCharsMsgSegment(req->propertyList.values[i].value);
  }

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = req->flags;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sreqSize;

  binCtx->type = CMPI_instance;
  binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildCreateInstanceRequest(void *parm)
{
  CMPIObjectPath *path;
  CMPIInstance   *inst;
  CMPIValue       val;
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  CreateInstanceReq *sreq;
  XtokProperty   *p = NULL;

  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;
  CMPIStatus rc = {CMPI_RC_OK, NULL};

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokCreateInstance *req = (XtokCreateInstance *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  inst = TrackedCMPIInstance(path, NULL);

  sreq = calloc(1, sizeof(CreateInstanceReq));
  sreq->hdr.operation = OPS_CreateInstance;
  sreq->hdr.count = CI_REQ_REG_SEGMENTS;


  for (p = req->instance.properties.first; p; p = p->next) {
    if (p->val.val.value) {
      val =
          str2CMPIValue(p->valueType, p->val.val, &p->val.ref,
                        req->op.nameSpace.data, &rc);
      CMSetProperty(inst, p->name, &val, p->valueType);
    }
  }

  sreq->instance = setInstanceMsgSegment(inst);
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->hdr.sessionId = hdr->sessionId;

  path = inst->ft->getObjectPath(inst, &st);
  /*
   * if st.rc is set the class was probably not found and the path is
   * NULL, so we don't set it. Let the provider manager handle unknown
   * class. 
   */
  if (!st.rc) {
    sreq->path = setObjectPathMsgSegment(path);
  }

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq);
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildGetInstanceRequest(void *parm)
{
  CMPIObjectPath *path;
  CMPIValue       val;
  CMPIType        type;
  GetInstanceReq *sreq;
  int             sreqSize = sizeof(GetInstanceReq);
  CMPIValue      *valp;
  int             i,
                  m;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;

  XtokGetInstance *req = (XtokGetInstance *)hdr->cimRequest;
  hdr->className = req->op.className.data;

  if (req->properties)
    sreqSize += req->properties * sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_GetInstance;
  sreq->hdr.count = req->properties + GI_REQ_REG_SEGMENTS;

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
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->hdr.sessionId = hdr->sessionId;

  for (i = 0; i < req->properties; i++) {
    sreq->properties[i] =
        setCharsMsgSegment(req->propertyList.values[i].value);
  }

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = req->flags;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sreqSize;
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildGetClassRequest(void *parm)
{
  CMPIObjectPath *path;
  int             i,
                  sreqSize = sizeof(GetClassReq);       // -sizeof(MsgSegment);
  GetClassReq    *sreq;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokGetClass   *req = (XtokGetClass *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  if (req->properties)
    sreqSize += req->properties * sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_GetClass;
  sreq->hdr.count=req->properties+GC_REQ_REG_SEGMENTS;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->hdr.sessionId = hdr->sessionId;

  for (i = 0; i < req->properties; i++)
    sreq->properties[i] =
        setCharsMsgSegment(req->propertyList.values[i].value);

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = req->flags;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sreqSize;
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildDeleteClassRequest(void *parm)
{
  CMPIObjectPath *path;
  DeleteClassReq *sreq;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokDeleteClass *req = (XtokDeleteClass *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  sreq = calloc(1, sizeof(DeleteClassReq));
  sreq->hdr.operation = OPS_DeleteClass;
  sreq->hdr.count = DC_REQ_REG_SEGMENTS;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = 0;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq);
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildDeleteInstanceRequest(void *parm)
{
  CMPIObjectPath *path;
  int             i, m;
  CMPIType        type;
  CMPIValue       val,
                  *valp;
  DeleteInstanceReq *sreq;
  int             sreqSize = sizeof(DeleteInstanceReq);
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;
  memset(binCtx, 0, sizeof(BinRequestContext));

  XtokDeleteInstance *req = (XtokDeleteInstance *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_DeleteInstance;
  sreq->hdr.count = DI_REQ_REG_SEGMENTS;

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
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sreqSize;
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildCreateClassRequest(void *parm)
{
  CMPIConstClass *cls;
  CMPIObjectPath *path;
  ClClass        *cl;
  ClClass        *tmp;
  CreateClassReq *sreq;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;

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
  CMPIStatus      rc = {CMPI_RC_OK, NULL};

  memset(binCtx, 0, sizeof(BinRequestContext));
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
      d.value = str2CMPIValue(q->type, q->value, NULL, NULL, &rc);
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
                        req->op.nameSpace.data, &rc);
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
        d.value = str2CMPIValue(q->type, q->value, NULL, NULL, &rc);
      }
      d.type = q->type;
      ClClassAddPropertyQualifier(&cl->hdr, prop, q->name, d);
    }
  }

  ms = &c->methods;
  for (m = ms->first; m; m = m->next) {
    ClMethod       *meth;
    ClParameter    *cl_parm;
    int             methId;

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
        d.value = str2CMPIValue(q->type, q->value, NULL, NULL, &rc);
      }
      d.type = q->type;
      ClClassAddMethodQualifier(&cl->hdr, meth, q->name, d);
    }

    rs = &m->params;
    for (r = rs->first; r; r = r->next) {
      pa.type = r->type;
      pa.arraySize = (unsigned int) r->arraySize;
      pa.refName = r->refClass;
      ClClassAddMethParameter(&cl->hdr, meth, r->name, pa);
      cl_parm = ((ClParameter *)
              ClObjectGetClSection(&cl->hdr,
                                   &meth->parameters)) + methId - 1;

      qs = &r->qualifiers;
      for (q = qs->first; q; q = q->next) {
        if (q->value.value == NULL) {
          d.state = CMPI_nullValue;
          d.value.uint64 = 0;
        } else {
          d.state = CMPI_goodValue;
          d.value = str2CMPIValue(q->type, q->value, NULL, NULL, &rc);
        }
        d.type = q->type;
        ClClassAddMethParamQualifier(&cl->hdr, cl_parm, q->name, d);
      }
    }
  }

  tmp = cl;
  cl = ClClassRebuildClass(cl, NULL);
  ClClassFreeClass(tmp);
  cls = calloc(1, sizeof(*cls));
  *cls = initConstClass(cl);

  int sreqSize = sizeof(*sreq) + (3 * sizeof(MsgSegment));
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_CreateClass;
  sreq->hdr.count = CC_REQ_REG_SEGMENTS;
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->path = setObjectPathMsgSegment(path);
  sreq->cls = setConstClassMsgSegment(cls);
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sreqSize;
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static int
buildModifyInstanceRequest(void *parm)
{
  CMPIObjectPath *path;
  CMPIInstance   *inst;
  CMPIType        type;
  CMPIValue       val,
                 *valp;
  int             i,
                  m,
                  sreqSize = sizeof(ModifyInstanceReq); // -sizeof(MsgSegment);
  ModifyInstanceReq *sreq;
  XtokInstance   *xci;
  XtokInstanceName *xco;
  XtokProperty   *p = NULL;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;
  CMPIStatus rc = {CMPI_RC_OK, NULL};
  int err = 0;

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokModifyInstance *req = (XtokModifyInstance *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  if (req->properties)
    sreqSize += req->properties * sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_ModifyInstance;
  sreq->hdr.count = req->properties + MI_REQ_REG_SEGMENTS;

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
                        req->op.nameSpace.data, &rc);
      if (rc.rc != CMPI_RC_OK) { /* bugzilla 75543 */
         binCtx->rc = rc.rc;
	 err = 1;
         break;
      }
      CMSetProperty(inst, p->name, &val, p->valueType);
    }
  }
  sreq->instance = setInstanceMsgSegment(inst);
  sreq->path = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sreqSize;
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
  return err;
}

static void
buildEnumClassesRequest(void *parm)
{
  CMPIObjectPath *path;
  EnumClassesReq *sreq;
  int             sreqSize;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "buildEnumClassesRequest");

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokEnumClasses *req = (XtokEnumClasses *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  sreqSize = sizeof(*sreq);// + 2 * sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_EnumerateClasses;
  sreq->hdr.count = EC_REQ_REG_SEGMENTS;
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->hdr.flags = req->flags;
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = req->flags;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq);
  binCtx->type = CMPI_class;
  binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildEnumClassNamesRequest(void *parm)
{
  CMPIObjectPath *path;
  EnumClassNamesReq *sreq;// = BINREQ(OPS_EnumerateClassNames, 2);
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "enumClassNames");

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokEnumClassNames *req = (XtokEnumClassNames *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  sreq = calloc(1, sizeof(*sreq));
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->hdr.operation = OPS_EnumerateClassNames;
  sreq->hdr.count = ECN_REQ_REG_SEGMENTS;
  sreq->hdr.flags = req->flags;
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = req->flags;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq);
  binCtx->type = CMPI_ref;
  binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->chunkedMode = 0;
  binCtx->pAs = NULL;
}

static void
buildEnumInstanceNamesRequest(void *parm)
{
  _SFCB_ENTER(TRACE_CIMXMLPROC, "buildEnumInstanceNamesRequest");
  CMPIObjectPath *path;
  EnumInstanceNamesReq *sreq;// = BINREQ(OPS_EnumerateInstanceNames, 2);
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;

  memset(binCtx, 0, sizeof(BinRequestContext));

  XtokEnumInstanceNames *req = (XtokEnumInstanceNames *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  sreq = calloc(1, sizeof(*sreq));
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->hdr.operation = OPS_EnumerateInstanceNames;
  sreq->hdr.count = EIN_REQ_REG_SEGMENTS;
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = 0;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq);
  binCtx->type = CMPI_ref;
  binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->chunkedMode = 0;
  binCtx->pAs = NULL;
}

static void
buildExecQueryRequest(void *parm)
{
  CMPIObjectPath *path;
  ExecQueryReq   *sreq;// = BINREQ(OPS_ExecQuery, 4);
  int             irc;
  QLStatement    *qs = NULL;
  char          **fCls;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokExecQuery  *req = (XtokExecQuery *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  qs = parseQuery(MEM_TRACKED, (char *) req->op.query.data,
                  (char *) req->op.queryLang.data, NULL, NULL, &irc);

  fCls = qs->ft->getFromClassList(qs);
  if (irc) {
    hdr->rc = CMPI_RC_ERR_INVALID_QUERY;
    hdr->errMsg = strdup("syntax error in query.");
    return;
  }
  if (fCls == NULL || *fCls == NULL) {
    hdr->rc = CMPI_RC_ERR_INVALID_QUERY;
    hdr->errMsg = strdup("required from clause is missing.");
    return;
  }
  req->op.className = setCharsMsgSegment(*fCls);

  path = TrackedCMPIObjectPath(req->op.nameSpace.data, *fCls, NULL);

  sreq = calloc(1, sizeof(*sreq));
  sreq->hdr.operation = OPS_ExecQuery;
  sreq->hdr.count = EQ_REQ_REG_SEGMENTS;
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->query = setCharsMsgSegment((char *) req->op.query.data);
  sreq->queryLang = setCharsMsgSegment((char *) req->op.queryLang.data);
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = 0;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq);
  binCtx->type = CMPI_instance;
  binCtx->xmlAs = XML_asObj;
  binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildReferencesRequest(void *parm)
{
  CMPIObjectPath *path;
  ReferencesReq  *sreq;
  int             i,
                  m,
                  sreqSize = sizeof(ReferencesReq);
  CMPIType        type;
  CMPIValue       val,
                 *valp;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokReferences *req = (XtokReferences *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  if (req->properties)
    sreqSize += req->properties * sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_References;
  sreq->hdr.count = req->properties + RI_REQ_REG_SEGMENTS;

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
    hdr->rc = CMPI_RC_ERR_NOT_SUPPORTED;
    hdr->errMsg = strdup("References operation for classes not supported");
    return;
  }
  if (!req->objNameSet) {
    free(sreq);
    hdr->rc = CMPI_RC_ERR_INVALID_PARAMETER;
    hdr->errMsg = strdup("ObjectName parameter required");
    return;
  }

  sreq->objectPath = setObjectPathMsgSegment(path);

  sreq->resultClass = req->op.resultClass;
  sreq->role = req->op.role;
  sreq->hdr.flags = req->flags;
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->hdr.sessionId = hdr->sessionId;

  for (i = 0; i < req->properties; i++)
    sreq->properties[i] =
        setCharsMsgSegment(req->propertyList.values[i].value);

  req->op.className = req->op.resultClass;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = req->flags;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sreqSize;
  binCtx->type = CMPI_instance;
  binCtx->xmlAs = XML_asObj;
  binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildReferenceNamesRequest(void *parm)
{
  CMPIObjectPath *path = NULL;
  ReferenceNamesReq *sreq;// = BINREQ(OPS_ReferenceNames, 4);
  int             i,
                  m;
  CMPIType        type;
  CMPIValue       val,
                 *valp;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;

  memset(binCtx, 0, sizeof(BinRequestContext));
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
    hdr->rc = CMPI_RC_ERR_NOT_SUPPORTED;
    hdr->errMsg = strdup("ReferenceNames operation for classes not supported");
    return;
  }
  if (!req->objNameSet) {
    hdr->rc = CMPI_RC_ERR_INVALID_PARAMETER;
    hdr->errMsg = strdup("ObjectName parameter required");
    return;
  }

  sreq = calloc(1, sizeof(*sreq));
  sreq->hdr.operation = OPS_ReferenceNames;
  sreq->hdr.count = RIN_REQ_REG_SEGMENTS;
  sreq->objectPath = setObjectPathMsgSegment(path);

  sreq->resultClass = req->op.resultClass;
  sreq->role = req->op.role;
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->hdr.sessionId = hdr->sessionId;

  req->op.className = req->op.resultClass;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = 0;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq);
  binCtx->type = CMPI_ref;
  binCtx->xmlAs = XML_asObjectPath;
  binCtx->noResp = 0;
  binCtx->chunkedMode = 0;
  binCtx->pAs = NULL;
}

static void
buildGetPropertyRequest(void *parm)
{

  CMPIObjectPath *path;
  CMPIStatus      rc;
  GetPropertyReq  *sreq;//BINREQ(OPS_GetProperty, 3);
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;
  int i, m;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "buildGetPropertyRequest");

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokGetProperty *req = (XtokGetProperty *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data,
                            req->instanceName.className, &rc);
  for (i = 0, m = req->instanceName.bindings.next; i < m; i++) {
    CMPIType type;
    CMPIValue val, *valp;

    valp = getKeyValueTypePtr(req->instanceName.bindings.keyBindings[i].type,
                              req->instanceName.bindings.keyBindings[i].value,
                              &req->instanceName.bindings.keyBindings[i].ref,
                              &val, &type, req->op.nameSpace.data);
    CMAddKey(path, req->instanceName.bindings.keyBindings[i].name, valp, type);
  }

  sreq = calloc(1, sizeof(*sreq));
  sreq->hdr.operation = OPS_GetProperty;
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->path = setObjectPathMsgSegment(path);
  sreq->name = setCharsMsgSegment(req->name);
  sreq->hdr.sessionId = hdr->sessionId;
  sreq->hdr.count = 3;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq);
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;

}

static void
buildSetPropertyRequest(void *parm)
{
  CMPIObjectPath *path;
  CMPIInstance   *inst;
  CMPIType        t, type;
  CMPIStatus      rc;
  CMPIValue       val, *valp;
  int             i, m;
  SetPropertyReq *sreq;// = BINREQ(OPS_SetProperty, 3);
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;
  CMPIStatus      st = {CMPI_RC_OK, NULL};

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokSetProperty *req = (XtokSetProperty *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data,
                            req->instanceName.className, &rc);
  for (i = 0, m = req->instanceName.bindings.next; i < m; i++) {
    valp = getKeyValueTypePtr(req->instanceName.bindings.keyBindings[i].type,
                              req->instanceName.bindings.keyBindings[i].value,
                              &req->instanceName.bindings.keyBindings[i].ref,
                              &val, &type, req->op.nameSpace.data);
    CMAddKey(path, req->instanceName.bindings.keyBindings[i].name, valp, type);
  }

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
                      req->op.nameSpace.data, &st);
    CMSetProperty(inst, req->propertyName, &val, t);
  } else {
    val.string = 0;
    CMSetProperty(inst, req->propertyName, 0, t);
  }

  sreq = calloc(1, sizeof(*sreq));
  sreq->hdr.operation = OPS_SetProperty;
  sreq->hdr.count = 3;
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->path = setObjectPathMsgSegment(path);
  sreq->inst = setInstanceMsgSegment(inst);
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq);
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

#ifdef HAVE_QUALREP

static void
buildGetQualifierRequest(void *parm)
{
  CMPIObjectPath *path;
  CMPIStatus      rc;
  GetQualifierReq *sreq;// = BINREQ(OPS_GetQualifier, 2);
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokGetQualifier *req = (XtokGetQualifier *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path = TrackedCMPIObjectPath(req->op.nameSpace.data, req->name, &rc);

  sreq = calloc(1, sizeof(*sreq));
  sreq->hdr.operation = OPS_GetQualifier;
  sreq->hdr.count = 2;
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->path = setObjectPathMsgSegment(path);
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq);
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildDeleteQualifierRequest(void *parm)
{
  CMPIObjectPath *path;
  CMPIStatus      rc;
  DeleteQualifierReq *sreq;// = BINREQ(OPS_DeleteQualifier, 2);
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokDeleteQualifier *req = (XtokDeleteQualifier *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  /* abuse classname for qualifier name */
  path = TrackedCMPIObjectPath(req->op.nameSpace.data, req->name, &rc);

  sreq = calloc(1, sizeof(*sreq));
  sreq->hdr.operation = OPS_DeleteQualifier;
  sreq->hdr.count = 2;
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->path = setObjectPathMsgSegment(path);
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq);
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildSetQualifierRequest(void *parm)
{
  CMPIObjectPath *path;
  CMPIQualifierDecl *qual;
  CMPIData        d;
  ClQualifierDeclaration *q;
  SetQualifierReq *sreq;// = BINREQ(OPS_SetQualifier, 3);
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;
  CMPIStatus rc = {CMPI_RC_OK, NULL};

  memset(binCtx, 0, sizeof(BinRequestContext));
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
        hdr->rc = CMPI_RC_ERROR;
        hdr->errMsg = "ISARRAY attribute and default value conflict";
        return;

    d.value = str2CMPIValue(d.type, req->qualifierdeclaration.data.value,
                            (XtokValueReference *) &
                            req->qualifierdeclaration.data.valueArray,
                            NULL, &rc);
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

  qual = malloc(sizeof(*qual));
  *qual = initQualifier(q);

  sreq = calloc(1, sizeof(*sreq));
  sreq->hdr.operation = OPS_SetQualifier;
  sreq->hdr.count = 3;
  sreq->qualifier = setQualifierMsgSegment(qual);
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->hdr.sessionId = hdr->sessionId;
  sreq->path = setObjectPathMsgSegment(path);

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq);
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildEnumQualifiersRequest(void *parm)
{
  CMPIObjectPath *path;
  EnumClassNamesReq *sreq;// = BINREQ(OPS_EnumerateQualifiers, 2);
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "enumQualifiers");

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokEnumQualifiers *req = (XtokEnumQualifiers *) hdr->cimRequest;

  path = TrackedCMPIObjectPath(req->op.nameSpace.data, NULL, NULL);
  sreq = calloc(1, sizeof(*sreq));
  sreq->hdr.operation = OPS_EnumerateQualifiers;
  sreq->hdr.count = 2;
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq);
  binCtx->type = CMPI_qualifierDecl;
  binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->chunkedMode = 0;
  binCtx->pAs = NULL;
}
#else

static void buildDeleteQualifierRequest(void *parm) { return; }
static void buildGetQualifierRequest(void *parm) { return; }
static void buildSetQualifierRequest(void *parm) { return; }
static void buildEnumQualifiersRequest(void *parm) { return; }
#endif  // HAVE_QUALREP

static void
buildInvokeMethodRequest(void *parm)
{
  CMPIObjectPath *path;
  CMPIType        type;
  CMPIValue       val,
                 *valp;
  int             i,
                  m,
                  rc,
                  vmpt = 0;
  InvokeMethodReq *sreq;// = BINREQ(OPS_InvokeMethod, 5);
  CMPIArgs       *in = TrackedCMPIArgs(NULL);
  XtokParamValue *p;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;
  CMPIStatus     st = {CMPI_RC_OK, NULL};

  memset(binCtx, 0, sizeof(BinRequestContext));
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
  sreq = calloc(1, sizeof(*sreq));
  sreq->hdr.operation = OPS_InvokeMethod;
  sreq->hdr.count = IM_REQ_REG_SEGMENTS;
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->hdr.sessionId = hdr->sessionId;

  if (getControlBool("validateMethodParamTypes", &vmpt))
    vmpt = 1;

  for (p = req->paramValues.first; p; p = p->next) {
    /*
     * Update untyped params (p->type==0) and verify those that were specified.
     * (Unlikely to be untyped at this point. Even if no paramtype is given in
     * the XML, the rule actions would assign one.)
     */
    if (p->type == 0 || p->type == CMPI_ARRAY || p->type == CMPI_class || 
        p->type == CMPI_instance || vmpt) {
      rc = updateMethodParamTypes(hdr);

      if (rc != CMPI_RC_OK) {
        free(sreq);
        hdr->rc = rc;
        hdr->errMsg = NULL;
        return;
      }
    }
    /*
     * TODO: Add support for PARAMVALUE subelements CLASS, CLASSNAME, INSTANCE,
     * INSTANCENAME, VALUE.NAMEDINSTANCE, in addition to EmbeddedInstance.
     */
    if (p->value.value) {
      CMPIValue val = str2CMPIValue(p->type, p->value, &p->valueRef, req->op.nameSpace.data, &st);
      if (st.rc) {
        free(sreq);
        hdr->rc = st.rc;
        hdr->errMsg = NULL;
        return;
      }
      CMAddArg(in, p->name, &val, p->type);
    }

  }

  sreq->in = setArgsMsgSegment(in);
  sreq->out = setArgsMsgSegment(NULL);
  sreq->method = setCharsMsgSegment(req->method);

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = 0;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq);
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

int
updateMethodParamTypes(RequestHdr * hdr)
{

  _SFCB_ENTER(TRACE_CIMXMLPROC, "updateMethodParamTypes");

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
          ClClassGetMethParmQualifierCount(cl, meth, p);
      for (; qcount > 0; qcount--) {
        char           *qname;
        ClClassGetMethParamQualifierAt(cl, param, (qcount - 1), NULL, &qname);
        if (strcmp(qname, "EmbeddedInstance") == 0) {
          // fprintf(stderr, " is EmbeddedInstance\n");
          isEI = 1;
          break;
          /*
           * TODO: For PARAMVALUE subelements CLASS, CLASSNAME, INSTANCE,
           * INSTANCENAME, VALUE.NAMEDINSTANCE, we probably want to skip
           * the below repository check as well.
           */
        }
      }
      if (isEI)
        continue;
    }

    if ((ptok->type == 0) || (ptok->type == CMPI_ARRAY)) {
      /*
       * Type was unknown, fill it in 
       * (unlikely at this point, see comment in buildInvokeMethodRequest)
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
  _SFCB_RETURN(CMPI_RC_OK);
}

static void addProperty(XtokProperties *ps, XtokProperty *p)
{
   XtokProperty *np;
   np=malloc(sizeof(*np));
   memcpy(np,p,sizeof(XtokProperty));
   np->next=NULL;
   if (ps->last) {
      ps->last->next=np;
   }
   else ps->first=np;
   ps->last=np;
}

static void addParamValue(XtokParamValues *vs, XtokParamValue *v)
{
   XtokParamValue *nv;
   nv=malloc(sizeof(*nv));
   memcpy(nv,v,sizeof(XtokParamValue));
   nv->next=NULL;
   if (vs->last) {
      vs->last->next=nv;
   }
   else vs->first=nv;
   vs->last=nv;
}

static void addQualifier(XtokQualifiers *qs, XtokQualifier *q)
{
   XtokQualifier *nq;
   nq=malloc(sizeof(*nq));
   memcpy(nq,q,sizeof(XtokQualifier));
   nq->next=NULL;
   if (qs->last) {
      qs->last->next=nq;
   }
   else qs->first=nq;
   qs->last=nq;
}

static void addMethod(XtokMethods *ms, XtokMethod *m)
{
   XtokMethod *nm;
   nm=malloc(sizeof(*nm));
   memcpy(nm,m,sizeof(XtokMethod));
   nm->next=NULL;
   if (ms->last) {
      ms->last->next=nm;
   }
   else ms->first=nm;
   ms->last=nm;
}

static void addParam(XtokParams *ps, XtokParam *p)
{
   XtokParam *np;
   np=malloc(sizeof(*np));
   memcpy(np,p,sizeof(XtokParam));
   np->next=NULL;
   if (ps->last) {
      ps->last->next=np;
   }
   else ps->first=np;
   ps->last=np;
}

static void
buildOpenEnumInstanceRequest(void *parm)
{
  CMPIObjectPath *path;
  EnumInstancesReq *sreq;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  int    i,
      sreqSize = sizeof(EnumInstancesReq);      // -sizeof(MsgSegment);
  BinRequestContext *binCtx = hdr->binCtx;
  
  _SFCB_ENTER(TRACE_CIMXMLPROC, "buildOpenEnumInstanceRequest");

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokOpenEnumInstances *req = (XtokOpenEnumInstances *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  if (req->properties)
    sreqSize += req->properties * sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = req->op.type;
  sreq->hdr.count = req->properties + EI_REQ_REG_SEGMENTS;

  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->hdr.sessionId = hdr->sessionId;

  for (i = 0; i < req->properties; i++) {
    sreq->properties[i] =
        setCharsMsgSegment(req->propertyList.values[i].value);
  }

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = req->flags;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sreqSize;

  binCtx->type = CMPI_instance;
  binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildPullInstancesRequest(void *parm)
{
  CMPIObjectPath *path;
  EnumInstancesReq *sreq;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  int sreqSize = sizeof(EnumInstancesReq);  // TODO
  BinRequestContext *binCtx = hdr->binCtx;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "buildPullInstancesRequest");

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokPullInstances *req = (XtokPullInstances *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path = TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data, NULL);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = req->op.type;

  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sreqSize;

  binCtx->type = CMPI_instance;
  binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}


static void
buildCloseEnumerationRequest(void *parm)
{
  CMPIObjectPath *path;
  EnumInstancesReq *sreq;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  int sreqSize = sizeof(EnumInstancesReq);  // TODO
  BinRequestContext *binCtx = hdr->binCtx;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "buildCloseEnumerationRequest");

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokCloseEnumeration *req = (XtokCloseEnumeration *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path = TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data, NULL);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = req->op.type;

  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sreqSize;

  binCtx->type = CMPI_instance;
  binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}

static void
buildEnumerationCountRequest(void *parm)
{
  CMPIObjectPath *path;
  EnumInstancesReq *sreq;
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  int sreqSize = sizeof(EnumInstancesReq);  // TODO
  BinRequestContext *binCtx = hdr->binCtx;

  _SFCB_ENTER(TRACE_CIMXMLPROC, "buildEnumerationCountRequest");

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokEnumerationCount *req = (XtokEnumerationCount *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path = TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data, NULL);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = req->op.type;

  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->userRole = setCharsMsgSegment(hdr->role);
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sreqSize;

  binCtx->type = CMPI_instance;
  binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
}


%}

%pure-parser
%parse-param { void* parm };
%lex-param { void *parm };

/*
**==============================================================================
**
** Union used to pass tokens from Lexer to this Parser.
**
**==============================================================================
*/

%union
{
   int                           intValue;
   char                          boolValue;
   char*                         className;
   void*                         tokCim;
   uint64_t                      enumerationContext;

   XtokMessage                   xtokMessage;
   XtokNameSpace                 xtokNameSpace;
   char*                         xtokLocalNameSpacePath;
   XtokNameSpacePath             xtokNameSpacePath;
   XtokHost                      xtokHost;
   XtokInstancePath              xtokInstancePath;
   XtokLocalInstancePath         xtokLocalInstancePath;
   XtokLocalClassPath            xtokLocalClassPath;

   XtokValue                     xtokValue;
   XtokValueArray                xtokValueArray;
   XtokValueReference            xtokValueReference;
   XtokValueRefArray             xtokValueRefArray;

   XtokInstanceName              xtokInstanceName;
   XtokKeyBinding                xtokKeyBinding;
   XtokKeyBindings               xtokKeyBindings;
   XtokKeyValue                  xtokKeyValue;

   XtokClass                     xtokClass;
   XtokInstance                  xtokInstance;
   XtokInstanceData              xtokInstanceData;
   XtokNamedInstance             xtokNamedInstance;

   XtokProperty                  xtokProperty;
   XtokPropertyData              xtokPropertyData;

   XtokMethod                    xtokMethod;
   XtokMethodData                xtokMethodData;
   XtokQualifier                 xtokQualifier;
   XtokQualifierDeclaration      xtokQualifierDeclaration;
   XtokQualifierDeclarationData  xtokQualifierDeclarationData;
   XtokQualifiers                xtokQualifiers;

   XtokParamValues               xtokParamValues;
   XtokParamValue                xtokParamValue;
   XtokParam                     xtokParam;
   XtokMethodCall                xtokMethodCall;

   XtokGetClassParmsList         xtokGetClassParmsList;
   XtokGetClassParms             xtokGetClassParms;
   XtokGetClass                  xtokGetClass;

   XtokEnumClassNames            xtokEnumClassNames;
   XtokEnumClassNamesParmsList   xtokEnumClassNamesParmsList;
   XtokEnumClassNamesParms       xtokEnumClassNamesParms;

   XtokEnumClasses               xtokEnumClasses;
   XtokEnumClassesParmsList      xtokEnumClassesParmsList;
   XtokEnumClassesParms          xtokEnumClassesParms;

   XtokGetInstance               xtokGetInstance;
   XtokGetInstanceParmsList      xtokGetInstanceParmsList;
   XtokGetInstanceParms          xtokGetInstanceParms;

   XtokDeleteClass               xtokDeleteClass;
   XtokDeleteClassParm           xtokDeleteClassParm;

   XtokDeleteInstance            xtokDeleteInstance;
   XtokDeleteInstanceParm        xtokDeleteInstanceParm;

   XtokCreateClass               xtokCreateClass;
   XtokCreateClassParm           xtokCreateClassParm;
 
   XtokCreateInstance            xtokCreateInstance;
   XtokCreateInstanceParm        xtokCreateInstanceParm;

   XtokModifyInstance            xtokModifyInstance;
   XtokModifyInstanceParmsList   xtokModifyInstanceParmsList;
   XtokModifyInstanceParms       xtokModifyInstanceParms;

   XtokEnumInstanceNames         xtokEnumInstanceNames;

   XtokEnumInstances             xtokEnumInstances;
   XtokEnumInstancesParmsList    xtokEnumInstancesParmsList;
   XtokEnumInstancesParms        xtokEnumInstancesParms;

   XtokExecQuery                 xtokExecQuery;
   
   XtokAssociators               xtokAssociators;
   XtokAssociatorsParmsList      xtokAssociatorsParmsList;
   XtokAssociatorsParms          xtokAssociatorsParms;

   XtokReferences                xtokReferences;
   XtokReferencesParmsList       xtokReferencesParmsList;
   XtokReferencesParms           xtokReferencesParms;

   XtokAssociatorNames           xtokAssociatorNames;
   XtokAssociatorNamesParmsList  xtokAssociatorNamesParmsList;
   XtokAssociatorNamesParms      xtokAssociatorNamesParms;

   XtokReferenceNames            xtokReferenceNames;
   XtokReferenceNamesParmsList   xtokReferenceNamesParmsList;
   XtokReferenceNamesParms       xtokReferenceNamesParms;

   XtokSetQualifier              xtokSetQualifier;
   XtokSetQualifierParm          xtokSetQualifierParm;
   
   XtokEnumQualifiers            xtokEnumQualifiers;
   
   XtokGetQualifier              xtokGetQualifier;
   XtokGetQualifierParm          xtokGetQualifierParm;

   XtokDeleteQualifier           xtokDeleteQualifier;
   XtokDeleteQualifierParm       xtokDeleteQualifierParm;
   
   XtokGetProperty               xtokGetProperty;
   XtokGetPropertyParm           xtokGetPropertyParm;
   
   XtokSetProperty               xtokSetProperty;
   XtokSetPropertyParms          xtokSetPropertyParms;
   XtokSetPropertyParmsList      xtokSetPropertyParmsList;
   
   XtokOpenEnumInstancePaths                xtokOpenEnumInstancePaths;
   XtokOpenEnumInstancePathsParmsList       xtokOpenEnumInstancePathsParmsList;
   XtokOpenEnumInstancePathsParms           xtokOpenEnumInstancePathsParms;

   XtokOpenEnumInstances                    xtokOpenEnumInstances;
   XtokOpenEnumInstancesParmsList           xtokOpenEnumInstancesParmsList;
   XtokOpenEnumInstancesParms               xtokOpenEnumInstancesParms;

   XtokOpenAssociatorInstancePaths          xtokOpenAssociatorInstancePaths;
   XtokOpenAssociatorInstancePathsParmsList xtokOpenAssociatorInstancePathsParmsList;
   XtokOpenAssociatorInstancePathsParms     xtokOpenAssociatorInstancePathsParms;

   XtokOpenAssociatorInstances              xtokOpenAssociatorInstances;
   XtokOpenAssociatorInstancesParmsList     xtokOpenAssociatorInstancesParmsList;
   XtokOpenAssociatorInstancesParms         xtokOpenAssociatorInstancesParms;

   XtokOpenReferenceInstancePaths           xtokOpenReferenceInstancePaths;
   XtokOpenReferenceInstancePathsParmsList  xtokOpenReferenceInstancePathsParmsList;
   XtokOpenReferenceInstancePathsParms      xtokOpenReferenceInstancePathsParms;

   XtokOpenReferenceInstances               xtokOpenReferenceInstances;
   XtokOpenReferenceInstancesParmsList      xtokOpenReferenceInstancesParmsList;
   XtokOpenReferenceInstancesParms          xtokOpenReferenceInstancesParms;

   XtokOpenQueryInstances                   xtokOpenQueryInstances;
   XtokOpenQueryInstancesParmsList          xtokOpenQueryInstancesParmsList;
   XtokOpenQueryInstancesParms              xtokOpenQueryInstancesParms;

   XtokPullInstances                   xtokPullInstances;
   XtokPullInstancesParmsList          xtokPullInstancesParmsList;
   XtokPullInstancesParms              xtokPullInstancesParms;

   XtokPullInstancesWithPath           xtokPullInstancesWithPath;
   XtokPullInstancesWithPathParmsList  xtokPullInstancesWithPathParmsList;
   XtokPullInstancesWithPathParms      xtokPullInstancesWithPathParms;

   XtokPullInstancePaths               xtokPullInstancePaths;
   XtokPullInstancePathsParmsList      xtokPullInstancePathsParmsList;
   XtokPullInstancePathsParms          xtokPullInstancePathsParms;

   XtokCloseEnumeration                xtokCloseEnumeration;

   XtokEnumerationCount                xtokEnumerationCount;

   XtokNewValue                  xtokNewValue;
   
   XtokScope                     xtokScope;
   
};

%token <tokCim>                  XTOK_XML
%token <intValue>                ZTOK_XML

%token <tokCim>                  XTOK_CIM
%token <intValue>                ZTOK_CIM

%token <xtokMessage>             XTOK_MESSAGE
%token <intValue>                ZTOK_MESSAGE
%type  <xtokMessage>             message

%token <intValue>                XTOK_SIMPLEREQ
%token <intValue>                ZTOK_SIMPLEREQ

%token <xtokGetClass>            XTOK_GETCLASS
%type  <xtokGetClass>            getClass
%type  <xtokGetClassParmsList>   getClassParmsList
%type  <xtokGetClassParms>       getClassParms

%token <xtokEnumClassNames>      XTOK_ENUMCLASSNAMES
%type  <xtokEnumClassNames>      enumClassNames
%type  <xtokEnumClassNamesParmsList> enumClassNamesParmsList
%type  <xtokEnumClassNamesParms> enumClassNamesParms

%token <xtokEnumClasses>         XTOK_ENUMCLASSES
%type  <xtokEnumClasses>         enumClasses
%type  <xtokEnumClassesParmsList> enumClassesParmsList
%type  <xtokEnumClassesParms>    enumClassesParms

%token <xtokCreateClass>         XTOK_CREATECLASS
%type  <xtokCreateClass>         createClass
%type  <xtokCreateClassParm>     createClassParm

%token <xtokCreateInstance>      XTOK_CREATEINSTANCE
%type  <xtokCreateInstance>      createInstance
%type  <xtokCreateInstanceParm>  createInstanceParm

%token <xtokDeleteClass>         XTOK_DELETECLASS
%type  <xtokDeleteClass>         deleteClass
%type  <xtokDeleteClassParm>     deleteClassParm

%token <xtokDeleteInstance>      XTOK_DELETEINSTANCE
%type  <xtokDeleteInstance>      deleteInstance
%type  <xtokDeleteInstanceParm>  deleteInstanceParm

%token <xtokModifyInstance>      XTOK_MODIFYINSTANCE
%type  <xtokModifyInstance>      modifyInstance
%type  <xtokModifyInstanceParmsList> modifyInstanceParmsList
%type  <xtokModifyInstanceParms>  modifyInstanceParms

%token <xtokGetInstance>         XTOK_GETINSTANCE
%type  <xtokGetInstance>         getInstance
%type  <xtokGetInstanceParmsList> getInstanceParmsList
%type  <xtokGetInstanceParms>    getInstanceParms

%token <xtokEnumInstanceNames>   XTOK_ENUMINSTANCENAMES
%type  <xtokEnumInstanceNames>   enumInstanceNames

%token <xtokEnumInstances>       XTOK_ENUMINSTANCES
%type  <xtokEnumInstances>       enumInstances
%type  <xtokEnumInstancesParmsList> enumInstancesParmsList
%type  <xtokEnumInstancesParms>  enumInstancesParms

%token <xtokExecQuery>           XTOK_EXECQUERY
%type  <xtokExecQuery>           execQuery

%token <xtokAssociators>         XTOK_ASSOCIATORS
%type  <xtokAssociators>         associators
%type  <xtokAssociatorsParmsList> associatorsParmsList
%type  <xtokAssociatorsParms>    associatorsParms

%token <xtokReferences>          XTOK_REFERENCES
%type  <xtokReferences>          references
%type  <xtokReferencesParmsList> referencesParmsList
%type  <xtokReferencesParms>     referencesParms

%token <xtokAssociatorNames>     XTOK_ASSOCIATORNAMES
%type  <xtokAssociatorNames>     associatorNames
%type  <xtokAssociatorNamesParmsList> associatorNamesParmsList
%type  <xtokAssociatorNamesParms> associatorNamesParms

%token <xtokReferenceNames>      XTOK_REFERENCENAMES
%type  <xtokReferenceNames>      referenceNames
%type  <xtokReferenceNamesParmsList> referenceNamesParmsList
%type  <xtokReferenceNamesParms> referenceNamesParms

%token <xtokSetQualifier>        XTOK_SETQUALIFIER
%type  <xtokSetQualifier>        setQualifier
%type  <xtokSetQualifierParm>    setQualifierParm

%token <xtokSetProperty>           XTOK_SETPROPERTY
%type  <xtokSetProperty>           setProperty
%type  <xtokSetPropertyParms>      setPropertyParms
%type  <xtokSetPropertyParmsList>  setPropertyParmsList

%token <xtokEnumQualifiers>      XTOK_ENUMQUALIFIERS
%type  <xtokEnumQualifiers>      enumQualifiers

%token <xtokGetQualifier>        XTOK_GETQUALIFIER
%type  <xtokGetQualifier>        getQualifier
%type  <xtokGetQualifierParm>    getQualifierParm

%token <xtokDeleteQualifier>     XTOK_DELETEQUALIFIER
%type  <xtokDeleteQualifier>     deleteQualifier
%type  <xtokDeleteQualifierParm> deleteQualifierParm

%token <xtokGetProperty>        XTOK_GETPROPERTY
%type  <xtokGetProperty>        getProperty
%type  <xtokGetPropertyParm>    getPropertyParm

%token <xtokOpenEnumInstancePaths>                XTOK_OPENENUMINSTANCEPATHS
%type  <xtokOpenEnumInstancePaths>                openEnumInstancePaths
%type  <xtokOpenEnumInstancePathsParmsList>       openEnumInstancePathsParmsList
%type  <xtokOpenEnumInstancePathsParms>           openEnumInstancePathsParms

%token <xtokOpenEnumInstances>                    XTOK_OPENENUMINSTANCES
%type  <xtokOpenEnumInstances>                    openEnumInstances
%type  <xtokOpenEnumInstancesParmsList>           openEnumInstancesParmsList
%type  <xtokOpenEnumInstancesParms>               openEnumInstancesParms

%token <xtokOpenAssociatorInstancePaths>          XTOK_OPENASSOCIATORINSTANCEPATHS
%type  <xtokOpenAssociatorInstancePaths>          openAssociatorInstancePaths
%type  <xtokOpenAssociatorInstancePathsParmsList> openAssociatorInstancePathsParmsList
%type  <xtokOpenAssociatorInstancePathsParms>     openAssociatorInstancePathsParms

%token <xtokOpenAssociatorInstances>              XTOK_OPENASSOCIATORINSTANCES
%type  <xtokOpenAssociatorInstances>              openAssociatorInstances
%type  <xtokOpenAssociatorInstancesParmsList>     openAssociatorInstancesParmsList
%type  <xtokOpenAssociatorInstancesParms>         openAssociatorInstancesParms

%token <xtokOpenReferenceInstancePaths>           XTOK_OPENREFERENCEINSTANCEPATHS
%type  <xtokOpenReferenceInstancePaths>           openReferenceInstancePaths
%type  <xtokOpenReferenceInstancePathsParmsList>  openReferenceInstancePathsParmsList
%type  <xtokOpenReferenceInstancePathsParms>      openReferenceInstancePathsParms

%token <xtokOpenReferenceInstances>               XTOK_OPENREFERENCEINSTANCES
%type  <xtokOpenReferenceInstances>               openReferenceInstances
%type  <xtokOpenReferenceInstancesParmsList>      openReferenceInstancesParmsList
%type  <xtokOpenReferenceInstancesParms>          openReferenceInstancesParms

%token <xtokOpenQueryInstances>                   XTOK_OPENQUERYINSTANCES
%type  <xtokOpenQueryInstances>                   openQueryInstances
%type  <xtokOpenQueryInstancesParmsList>          openQueryInstancesParmsList
%type  <xtokOpenQueryInstancesParms>              openQueryInstancesParms

%token <xtokPullInstances>                  XTOK_PULLINSTANCES
%type  <xtokPullInstances>                  pullInstances
%type  <xtokPullInstancesParmsList>         pullInstancesParmsList
%type  <xtokPullInstancesParms>             pullInstancesParms

%token <xtokPullInstancesWithPath>          XTOK_PULLINSTANCESWITHPATH
%type  <xtokPullInstancesWithPath>          pullInstancesWithPath
%type  <xtokPullInstancesWithPathParmsList> pullInstancesWithPathParmsList
%type  <xtokPullInstancesWithPathParms>     pullInstancesWithPathParms

%token <xtokPullInstancePaths>              XTOK_PULLINSTANCEPATHS
%type  <xtokPullInstancePaths>              pullInstancePaths
%type  <xtokPullInstancePathsParmsList>     pullInstancePathsParmsList
%type  <xtokPullInstancePathsParms>         pullInstancePathsParms

%token <xtokCloseEnumeration>               XTOK_CLOSEENUMERATION
%type  <xtokCloseEnumeration>               closeEnumeration

%token <xtokEnumerationCount>               XTOK_ENUMERATIONCOUNT
%type  <xtokEnumerationCount>               enumerationCount


%token <intValue>                ZTOK_IMETHODCALL

%token <intValue>                XTOK_METHODCALL
%token <intValue>                ZTOK_METHODCALL
%type  <xtokMethodCall>          methodCall

%type <tokCim>                   cimOperation

%token <xtokNameSpacePath>       XTOK_NAMESPACEPATH
%token <intValue>                ZTOK_NAMESPACEPATH
%type  <xtokNameSpacePath>       nameSpacePath

%token <xtokLocalNameSpacePath>  XTOK_LOCALNAMESPACEPATH
%token <intValue>                ZTOK_LOCALNAMESPACEPATH
%type  <xtokLocalNameSpacePath>  localNameSpacePath

%token <xtokNameSpace>           XTOK_NAMESPACE
%token <intValue>                ZTOK_NAMESPACE
%type  <xtokNameSpace>           namespaces

%token <intValue>                ZTOK_IPARAMVALUE

%token <xtokHost>                XTOK_HOST
%type  <xtokHost>                host
%token <intValue>                ZTOK_HOST

%token <xtokValue>               XTOK_VALUE
%type  <xtokValue>               value
%token <intValue>                ZTOK_VALUE

%token <xtokValueCdata>          XTOK_CDATA
%token <intValue>                ZTOK_CDATA

%token <xtokValueArray>          XTOK_VALUEARRAY
%type  <xtokValueArray>          valueArray
%type  <xtokValueArray>          valueList
%token <intValue>                ZTOK_VALUEARRAY

%token <intValueReference>       XTOK_VALUEREFERENCE
%type  <xtokValueReference>      valueReference
%token <intValue>                ZTOK_VALUEREFERENCE

%token <xtokValueRefArray>       XTOK_VALUEREFARRAY
%type  <xtokValueRefArray>       valueRefArray
%type  <xtokValueRefArray>       valueRefList
%token <intValue>                ZTOK_VALUEREFARRAY

%token <className>               XTOK_CLASSNAME
%token <intValue>                ZTOK_CLASSNAME
%type  <className>               className

%token <xtokInstanceName>        XTOK_INSTANCENAME
%token <intValue>                ZTOK_INSTANCENAME
%type  <xtokInstanceName>        instanceName

%token <xtokKeyBinding>          XTOK_KEYBINDING
%token <intValue>                ZTOK_KEYBINDING
%type  <xtokKeyBinding>          keyBinding
%type  <xtokKeyBindings>         keyBindings

%token <xtokKeyValue>            XTOK_KEYVALUE
%token <intValue>                ZTOK_KEYVALUE

%token <boolValue>               XTOK_IP_LOCALONLY
%token <boolValue>               XTOK_IP_INCLUDEQUALIFIERS
%token <boolValue>               XTOK_IP_INCLUDECLASSORIGIN
%token <boolValue>               XTOK_IP_DEEPINHERITANCE
%token <className>               XTOK_IP_CLASSNAME
%token <instance>                XTOK_IP_INSTANCE
%token <xmodifiedInstance>       XTOK_IP_MODIFIEDINSTANCE
%token <xtokInstanceName>        XTOK_IP_INSTANCENAME
%token <xtokInstanceName>        XTOK_IP_OBJECTNAME
%token <className>               XTOK_IP_ASSOCCLASS
%token <className>               XTOK_IP_RESULTCLASS
%token <className>               XTOK_IP_ROLE
%token <className>               XTOK_IP_RESULTROLE
%token <className>               XTOK_IP_QUERY
%token <className>               XTOK_IP_QUERYLANG
%token <class>                   XTOK_IP_CLASS
%token <qualifierDeclaration>    XTOK_IP_QUALIFIERDECLARATION
%token <value>                   XTOK_IP_QUALIFIERNAME
%token <value>                   XTOK_IP_PROPERTYNAME
%token <newValue>                XTOK_IP_NEWVALUE
%token <className>               XTOK_IP_FILTERQUERYLANG
%token <className>               XTOK_IP_FILTERQUERY
%token <boolValue>               XTOK_IP_CONTINUEONERROR
%token <boolValue>               XTOK_IP_ENDOFSEQUENCE   // OUT-only param
%token <boolValue>               XTOK_IP_RETURNQUERYRESULTCLASS
%token <value>                   XTOK_IP_OPERATIONTIMEOUT
%token <value>                   XTOK_IP_MAXOBJECTCOUNT
%token <enumerationContext>      XTOK_IP_ENUMERATIONCONTEXT
%type  <enumerationContext>      enumerationContext

%token <xtokPropertyList>        XTOK_IP_PROPERTYLIST
%type  <boolValue>               boolValue

%token <xtokNamedInstance>       XTOK_VALUENAMEDINSTANCE
%token <intValue>                ZTOK_VALUENAMEDINSTANCE
%type  <xtokNamedInstance>       namedInstance

%token <xtokQualifier>           XTOK_QUALIFIER
%type  <xtokQualifier>           qualifier
%token <intValue>                ZTOK_QUALIFIER

%token <xtokQualifierDeclaration> XTOK_QUALIFIERDECLARATION
%type  <xtokQualifierDeclaration> qualifierDeclaration
%type  <xtokQualifierDeclarationData> qualifierDeclarationData
%token <intValue>                ZTOK_QUALIFIERDECLARATION

%token <xtokScope>               XTOK_SCOPE
%type  <xtokScope>               scope
%token <intValue>                ZTOK_SCOPE

%token <xtokProperty>            XTOK_PROPERTY
%token <intValue>                ZTOK_PROPERTY
%token <xtokPropertyArray>       XTOK_PROPERTYARRAY
%token <intValue>                ZTOK_PROPERTYARRAY
%token <xtokProperty>            XTOK_PROPERTYREFERENCE
%token <intValue>                ZTOK_PROPERTYREFERENCE

%type  <xtokPropertyData>        propertyData
%type  <xtokQualifiers>          qualifierList
%type  <xtokProperty>            property

%token <xtokParam>               XTOK_PARAM
%type  <xtokParam>               parameter
%token <intValue>                ZTOK_PARAM
%token <xtokParam>               XTOK_PARAMARRAY
%token <intValue>                ZTOK_PARAMARRAY
%token <xtokParam>               XTOK_PARAMREF
%token <intValue>                ZTOK_PARAMREF
%token <xtokParam>               XTOK_PARAMREFARRAY
%token <intValue>                ZTOK_PARAMREFARRAY

%token <xtokMethod>              XTOK_METHOD
%type  <xtokMethod>              method
%token <intValue>                ZTOK_METHOD
%type  <xtokMethodData>          methodData

%token <xtokClass>               XTOK_CLASS
%token <intValue>                ZTOK_CLASS
%type  <xtokClass>               class
%type  <xtokClassData>           classData

%token <xtokInstance>            XTOK_INSTANCE
%token <intValue>                ZTOK_INSTANCE
%type  <xtokInstance>            instance
%type  <xtokInstanceData>        instanceData

%type  <xtokNewValue>            newValue

%type  <xtokParamValues>         paramValues
%type  <xtokParamValue>          paramValue
%token <xtokParamValue>          XTOK_PARAMVALUE
%token <intValue>                ZTOK_PARAMVALUE

%type  <xtokInstancePath>        instancePath
%token <xtokInstancePath>        XTOK_INSTANCEPATH
%token <intValue>                ZTOK_INSTANCEPATH

%type  <xtokLocalInstancePath>   localInstancePath
%token <xtokLocalInstancePath>   XTOK_LOCALINSTANCEPATH
%token <intValue>                ZTOK_LOCALINSTANCEPATH

%type  <xtokLocalClassPath>      localClassPath
%token <xtokLocalClassPath>      XTOK_LOCALCLASSPATH
%token <intValue>                ZTOK_LOCALCLASSPATH

%%

/*
**==============================================================================
**
** The grammar itself.
**
**==============================================================================
*/

start
    : XTOK_XML ZTOK_XML cimOperation
    {
    }
;

cimOperation
    : XTOK_CIM message ZTOK_CIM
    {
    }
;

message
    : XTOK_MESSAGE simpleReq ZTOK_MESSAGE
    {
    }
;

simpleReq
    : XTOK_SIMPLEREQ iMethodCall ZTOK_SIMPLEREQ
    {
    }
;

iMethodCall
    : XTOK_GETCLASS getClass ZTOK_IMETHODCALL
    {
    }
    | XTOK_ENUMCLASSNAMES enumClassNames ZTOK_IMETHODCALL
    {
    }
    | XTOK_ENUMCLASSES enumClasses ZTOK_IMETHODCALL
    {
    }
    | XTOK_DELETEINSTANCE deleteInstance ZTOK_IMETHODCALL
    {
    }
    | XTOK_GETINSTANCE getInstance ZTOK_IMETHODCALL
    {
    }
    | XTOK_MODIFYINSTANCE modifyInstance ZTOK_IMETHODCALL
    {
    }
    | XTOK_CREATEINSTANCE createInstance ZTOK_IMETHODCALL
    {
    }
    | XTOK_ENUMINSTANCENAMES enumInstanceNames ZTOK_IMETHODCALL
    {
    }
    | XTOK_ENUMINSTANCES enumInstances ZTOK_IMETHODCALL
    {
    }
    | XTOK_ASSOCIATORS associators ZTOK_IMETHODCALL
    {
    }
    | XTOK_ASSOCIATORNAMES associatorNames ZTOK_IMETHODCALL
    {
    }
    | XTOK_REFERENCES references ZTOK_IMETHODCALL
    {
    }
    | XTOK_REFERENCENAMES referenceNames ZTOK_IMETHODCALL
    {
    }
    | XTOK_EXECQUERY execQuery ZTOK_IMETHODCALL
    {
    }
    | XTOK_METHODCALL methodCall ZTOK_METHODCALL
    {
    }
    | XTOK_DELETECLASS deleteClass ZTOK_IMETHODCALL
    {
    }
    | XTOK_CREATECLASS createClass ZTOK_IMETHODCALL
    {
    }
    | XTOK_ENUMQUALIFIERS enumQualifiers ZTOK_IMETHODCALL
    {
    }
    | XTOK_SETQUALIFIER setQualifier ZTOK_IMETHODCALL
    {
    }
    | XTOK_GETQUALIFIER getQualifier ZTOK_IMETHODCALL
    {
    } 
    | XTOK_DELETEQUALIFIER deleteQualifier ZTOK_IMETHODCALL
    {
    }
    | XTOK_GETPROPERTY getProperty ZTOK_IMETHODCALL
    {
    }
    | XTOK_SETPROPERTY setProperty ZTOK_IMETHODCALL
    {
    }
    | XTOK_OPENENUMINSTANCEPATHS openEnumInstancePaths ZTOK_IMETHODCALL
    {
    }
    | XTOK_OPENENUMINSTANCES openEnumInstances ZTOK_IMETHODCALL
    {
    }
    | XTOK_OPENASSOCIATORINSTANCES openAssociatorInstances ZTOK_IMETHODCALL
    {
    }
    | XTOK_OPENASSOCIATORINSTANCEPATHS openAssociatorInstancePaths ZTOK_IMETHODCALL
    {
    }
    | XTOK_OPENREFERENCEINSTANCES openReferenceInstances ZTOK_IMETHODCALL
    {
    }
    | XTOK_OPENREFERENCEINSTANCEPATHS openReferenceInstancePaths ZTOK_IMETHODCALL
    {
    }
    | XTOK_OPENQUERYINSTANCES openQueryInstances ZTOK_IMETHODCALL
    {
    }
    | XTOK_PULLINSTANCES pullInstances ZTOK_IMETHODCALL
    {
    }
    | XTOK_PULLINSTANCESWITHPATH pullInstancesWithPath ZTOK_IMETHODCALL
    {
    }
    | XTOK_PULLINSTANCEPATHS pullInstancePaths ZTOK_IMETHODCALL
    {
    }
    | XTOK_CLOSEENUMERATION closeEnumeration ZTOK_IMETHODCALL
    {
    }
    | XTOK_ENUMERATIONCOUNT enumerationCount ZTOK_IMETHODCALL
    {
    }
    
;

/*
 *    methodCall
*/

methodCall
    : localClassPath 
    {
       $$.op.count = IM_REQ_REG_SEGMENTS;
       $$.op.type = OPS_InvokeMethod;
       $$.op.nameSpace=setCharsMsgSegment($1.path);
       $$.op.className=setCharsMsgSegment($1.className);
       $$.instName=0;
       $$.paramValues.first=NULL;
       $$.paramValues.last=NULL;
       
       setRequest(parm,&$$,sizeof(XtokMethodCall),OPS_InvokeMethod);
       buildInvokeMethodRequest(parm);
    }   
    | localClassPath paramValues
    {
       $$.op.count = IM_REQ_REG_SEGMENTS;
       $$.op.type = OPS_InvokeMethod;
       $$.op.nameSpace=setCharsMsgSegment($1.path);
       $$.op.className=setCharsMsgSegment($1.className);
       $$.instName=0;
       $$.paramValues=$2;
       
       setRequest(parm,&$$,sizeof(XtokMethodCall),OPS_InvokeMethod);
       buildInvokeMethodRequest(parm);
    }   
    | localInstancePath 
    {
       $$.op.count = IM_REQ_REG_SEGMENTS;
       $$.op.type = OPS_InvokeMethod;
       $$.op.nameSpace=setCharsMsgSegment($1.path);
       $$.op.className=setCharsMsgSegment($1.instanceName.className);
       $$.instanceName=$1.instanceName;
       $$.instName=1;
       $$.paramValues.first=NULL;
       $$.paramValues.last=NULL;
       
       setRequest(parm,&$$,sizeof(XtokMethodCall),OPS_InvokeMethod);
       buildInvokeMethodRequest(parm);
    }   
    | localInstancePath paramValues
    {
       $$.op.count = IM_REQ_REG_SEGMENTS;
       $$.op.type = OPS_InvokeMethod;
       $$.op.nameSpace=setCharsMsgSegment($1.path);
       $$.op.className=setCharsMsgSegment($1.instanceName.className);
       $$.instanceName=$1.instanceName;
       $$.instName=1;
       $$.paramValues=$2;
              
       setRequest(parm,&$$,sizeof(XtokMethodCall),OPS_InvokeMethod);
       buildInvokeMethodRequest(parm);
    }   
;    

paramValues
    : paramValue
    {
      $$.first = NULL;
      $$.last = NULL;
      addParamValue(&$$,&$1);
    }
    | paramValues paramValue
    {
      addParamValue(&$$,&$2);
    }
;

paramValue
    : XTOK_PARAMVALUE ZTOK_PARAMVALUE
    {
       $$.value.value=NULL;
       $$.type=0;
    }   
    | XTOK_PARAMVALUE value ZTOK_PARAMVALUE
    {
       $$.value=$2;
       if($$.value.type == typeValue_Instance) {
          $$.type = CMPI_instance;
       } else 
       if($$.value.type == typeValue_Class) {
          $$.type = CMPI_class;
       }
    }   
    | XTOK_PARAMVALUE valueArray ZTOK_PARAMVALUE
    {
       $$.valueArray=$2;
       $$.type|=CMPI_ARRAY;
       
       if($$.valueArray.values) {
          if($$.valueArray.values[0].type == typeValue_Instance)
          	$$.type = CMPI_instance | CMPI_ARRAY;
          else if($$.valueArray.values[0].type == typeValue_Class)
          	$$.type = CMPI_class | CMPI_ARRAY;          	
       }
    }   
    | XTOK_PARAMVALUE valueReference ZTOK_PARAMVALUE
    {
       $$.valueRef=$2;
       $$.type=CMPI_ref;
    }   
    | XTOK_PARAMVALUE valueRefArray ZTOK_PARAMVALUE
    {
       $$.valueRefArray=$2;
       $$.type=CMPI_ARRAY | CMPI_ref;
    }   
    /* Support new subelements in DSP0201 v2.3.1 */
    | XTOK_PARAMVALUE className ZTOK_PARAMVALUE
    {
       $$.className=$2;
       if (!$$.type) $$.type=CMPI_class;  /* should this be CMPI_string? */
    }
    | XTOK_PARAMVALUE instanceName ZTOK_PARAMVALUE
    {
       $$.instanceName=$2;
       if (!$$.type) $$.type=CMPI_instance;
    }
    | XTOK_PARAMVALUE class ZTOK_PARAMVALUE
    {
       //$$.class=$2;
       if (!$$.type) $$.type=CMPI_class;
    }
    | XTOK_PARAMVALUE instance ZTOK_PARAMVALUE
    {
       $$.instance=$2;
       if (!$$.type) $$.type=CMPI_instance;
    }
    | XTOK_PARAMVALUE namedInstance ZTOK_PARAMVALUE
    {
       $$.namedInstance=$2;
       /* should we ignore paramtype from the XML and always set to CMPI_instance? */
       if (!$$.type) $$.type=CMPI_instance;
    }
;

/*
 *    getProperty
*/
getProperty
    : localNameSpacePath getPropertyParm
	{
       $$.op.count = 3;
       $$.op.type = OPS_GetProperty;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.instanceName.className);
       $$.name = $2.name;
       $$.instanceName = $2.instanceName;
       setRequest(parm,&$$,sizeof(XtokGetProperty),OPS_GetProperty);
       buildGetPropertyRequest(parm);
	}
;

getPropertyParm
	: XTOK_IP_PROPERTYNAME value ZTOK_IPARAMVALUE XTOK_IP_INSTANCENAME instanceName ZTOK_IPARAMVALUE
	{
		$$.name = $2.value;
		$$.instanceName = $5;
	}
	| XTOK_IP_INSTANCENAME instanceName ZTOK_IPARAMVALUE XTOK_IP_PROPERTYNAME value ZTOK_IPARAMVALUE
	{
		$$.name = $5.value;
		$$.instanceName = $2;
	}
;


/*
 *    setProperty
*/
setProperty
    : localNameSpacePath
	{
       $$.op.count = 3;
       $$.op.type = OPS_SetProperty;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.propertyName = NULL;

       setRequest(parm,&$$,sizeof(XtokSetProperty),OPS_SetProperty);
       buildSetPropertyRequest(parm);
	}
	| localNameSpacePath setPropertyParmsList
	{
       $$.op.count = 3;
       $$.op.type = OPS_SetProperty;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.instanceName.className);
       $$.newVal = $2.newVal;
       $$.instanceName = $2.instanceName;
       $$.propertyName = $2.propertyName;
       
       setRequest(parm,&$$,sizeof(XtokSetProperty),OPS_SetProperty);
       buildSetPropertyRequest(parm);
	}
;

setPropertyParmsList
	: setPropertyParms
	{
       $$.newVal = $1.newVal;
       $$.instanceName = $1.instanceName;
       $$.propertyName = $1.propertyName;
	}
	| setPropertyParmsList setPropertyParms
	{
		if($2.propertyName) {
			$$.propertyName = $2.propertyName;
		}
		else if($2.instanceName.className) {
			$$.instanceName = $2.instanceName;
		}
		else {
			$$.newVal = $2.newVal;
		}
	}
;

setPropertyParms
	: XTOK_IP_INSTANCENAME instanceName ZTOK_IPARAMVALUE
	{
		$$.instanceName = $2;
		$$.propertyName = NULL;
		$$.newVal.type = 0;
		$$.newVal.val.value = NULL;
	}
	| XTOK_IP_PROPERTYNAME value ZTOK_IPARAMVALUE
	{
		$$.propertyName = $2.value;
		$$.instanceName.className = NULL;
		$$.newVal.type = 0;
		$$.newVal.val.value = NULL;
	}
	| XTOK_IP_NEWVALUE newValue ZTOK_IPARAMVALUE
	{
		$$.newVal = $2;
		$$.propertyName = NULL;
		$$.instanceName.className = NULL;
	}
;

newValue
	: value
	{
		if($1.type == typeValue_Instance) {
			$$.type = CMPI_instance;
		}
		else if($1.type == typeValue_Class) {
			$$.type = CMPI_class;
		}
		else {
			$$.type = 0;
		}
		$$.val = $1;
	}
	| valueArray
	{
		$$.arr = $1;
		$$.type = CMPI_ARRAY;
	}
	| valueReference
	{
		$$.ref = $1;
		$$.type = CMPI_ref;
	}
;

/*
 *    getQualifier
*/
getQualifier
    : localNameSpacePath getQualifierParm
    {
       $$.op.count = 2;
       $$.op.type = OPS_GetQualifier;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.name = $2.name;
       setRequest(parm,&$$,sizeof(XtokGetQualifier),OPS_GetQualifier);
       buildGetQualifierRequest(parm);
    }
;

getQualifierParm
    : XTOK_IP_QUALIFIERNAME value ZTOK_IPARAMVALUE
    {
       $$.name = $2.value;
    }
;

/*
 *    deleteQualifier
*/
deleteQualifier
    : localNameSpacePath deleteQualifierParm
    {
       $$.op.count = 2;
       $$.op.type = OPS_DeleteQualifier;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.name = $2.name;
       setRequest(parm,&$$,sizeof(XtokDeleteQualifier),OPS_DeleteQualifier);
       buildDeleteQualifierRequest(parm);
    }
;

deleteQualifierParm
    : XTOK_IP_QUALIFIERNAME value ZTOK_IPARAMVALUE
    {
       $$.name = $2.value;
    }
;

/*
 *    enumQualifiers
*/
enumQualifiers
    : localNameSpacePath
    {
       $$.op.count = 2;
       $$.op.type = OPS_EnumerateQualifiers;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       setRequest(parm,&$$,sizeof(XtokEnumQualifiers),OPS_EnumerateQualifiers);
       buildEnumQualifiersRequest(parm);
    }
;
/*
 *    setQualifier
*/


setQualifier
    : localNameSpacePath setQualifierParm
    {
       $$.op.count = 3;
       $$.op.type = OPS_SetQualifier;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);       
       $$.qualifierdeclaration = $2.qualifierdeclaration;

       setRequest(parm,&$$,sizeof(XtokSetQualifier),OPS_SetQualifier);
       buildSetQualifierRequest(parm);
    }
;


setQualifierParm
    : XTOK_IP_QUALIFIERDECLARATION qualifierDeclaration ZTOK_IPARAMVALUE
    {
       $$.qualifierdeclaration = $2;
    }
;

/*
 *    getClass
*/

getClass
    : localNameSpacePath
    {
       $$.op.count = GC_REQ_REG_SEGMENTS;
       $$.op.type = OPS_GetClass;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
//     $$.flags = FL_localOnly;
       $$.flags = FL_localOnly|FL_includeQualifiers;
       $$.propertyList.values = NULL;
       $$.properties=0;

       setRequest(parm,&$$,sizeof(XtokGetClass),OPS_GetClass);
       buildGetClassRequest(parm);
    }
    | localNameSpacePath getClassParmsList
    {
       $$.op.count = GC_REQ_REG_SEGMENTS;
       $$.op.type = OPS_GetClass;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.className);
//     $$.flags = ($2.flags &  $2.flagsSet) | ((~$2.flagsSet) & (FL_localOnly));
       $$.flags = ($2.flags &  $2.flagsSet) | ((~$2.flagsSet) & (FL_localOnly|FL_includeQualifiers));
       $$.propertyList = $2.propertyList;
       $$.properties=$2.properties;

       setRequest(parm,&$$,sizeof(XtokGetClass),OPS_GetClass);
       buildGetClassRequest(parm);
    }
;

getClassParmsList
    : getClassParms
    {
       $$.flags=$1.flags;
       $$.flagsSet=$1.flagsSet;
       if ($1.clsNameSet) $$.className=$1.className;
       $$.clsNameSet = $1.clsNameSet;
       if ($1.propertyList.values) {
          $$.propertyList=$1.propertyList;
          $$.properties=$1.properties;
       }
    }
    | getClassParmsList getClassParms
    {
       $$.flags=$1.flags|$2.flags;
       $$.flagsSet=$1.flagsSet|$2.flagsSet;
       if ($2.clsNameSet) $$.className=$2.className;
       $$.clsNameSet |= $2.clsNameSet;
       if ($2.propertyList.values) {
          $$.propertyList=$2.propertyList;
          $$.properties=$2.properties;
       }
    }
;

getClassParms
    : XTOK_IP_CLASSNAME className ZTOK_IPARAMVALUE
    {
       $$.className = $2;
       $$.flags = $$.flagsSet = 0 ;
       $$.clsNameSet = 1;
       $$.propertyList.values=0;
       $$.properties=0;
    }
    | XTOK_IP_LOCALONLY boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_localOnly : 0 ;
       $$.flagsSet = FL_localOnly;
       $$.properties=$$.clsNameSet=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_LOCALONLY ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_INCLUDEQUALIFIERS boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_includeQualifiers : 0 ;
       $$.flagsSet = FL_includeQualifiers;
       $$.properties=$$.clsNameSet=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_INCLUDEQUALIFIERS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_INCLUDECLASSORIGIN boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_includeClassOrigin : 0 ;
       $$.flagsSet = FL_includeClassOrigin;
       $$.properties=$$.clsNameSet=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_INCLUDECLASSORIGIN ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_PROPERTYLIST valueArray ZTOK_IPARAMVALUE
    {
       $$.propertyList=$2;
       $$.properties=$2.next;
       $$.clsNameSet=0;
       $$.flags = $$.flagsSet = 0 ;
    }
    | XTOK_IP_PROPERTYLIST ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
;


/*
 *    enumClassNames
*/

enumClassNames
    : localNameSpacePath
    {
       $$.op.count = ECN_REQ_REG_SEGMENTS;
       $$.op.type = OPS_EnumerateClassNames;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.flags = 0;

       setRequest(parm,&$$,sizeof(XtokEnumClassNames),OPS_EnumerateClassNames);
       buildEnumClassNamesRequest(parm);
    }
    | localNameSpacePath enumClassNamesParmsList
    {
       $$.op.count = ECN_REQ_REG_SEGMENTS;
       $$.op.type = OPS_EnumerateClassNames;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.className);
       $$.flags = $2.flags;

       setRequest(parm,&$$,sizeof(XtokEnumClassNames),OPS_EnumerateClassNames);
       buildEnumClassNamesRequest(parm);
    }
;

enumClassNamesParmsList
    : enumClassNamesParms
    {
       if ($1.className) $$.className=$1.className;
       $$.flags=$1.flags;
    }
    | enumClassNamesParmsList enumClassNamesParms
    {
       if ($2.className) $$.className=$2.className;
       $$.flags = ($2.flags & $2.flagsSet) | ((~$2.flagsSet) & FL_deepInheritance);
    }
;

enumClassNamesParms
    : XTOK_IP_CLASSNAME ZTOK_IPARAMVALUE
    {
       $$.className = NULL;
       $$.flags = $$.flagsSet = 0 ;
    }
    | XTOK_IP_CLASSNAME className ZTOK_IPARAMVALUE
    {
       $$.className = $2;
       $$.flags = $$.flagsSet = 0 ;
    }
    | XTOK_IP_DEEPINHERITANCE boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_deepInheritance : 0 ;
       $$.flagsSet = FL_deepInheritance;
       $$.className=0;
    }
    | XTOK_IP_DEEPINHERITANCE ZTOK_IPARAMVALUE
    {
       $$.className = NULL;
       $$.flags = $$.flagsSet = 0 ;
    }    
;

/*
 *    enumClasses
*/

enumClasses
    : localNameSpacePath
    {
       $$.op.count = EC_REQ_REG_SEGMENTS;
       $$.op.type = OPS_EnumerateClasses;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.flags = FL_localOnly|FL_includeQualifiers;

       setRequest(parm,&$$,sizeof(XtokEnumClasses),OPS_EnumerateClasses);
       buildEnumClassesRequest(parm);
    }
    | localNameSpacePath enumClassesParmsList
    {
       $$.op.count = EC_REQ_REG_SEGMENTS;
       $$.op.type = OPS_EnumerateClasses;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.className);
       $$.flags = ($2.flags & $2.flagsSet) | ((~$2.flagsSet) & (FL_localOnly|FL_includeQualifiers));

       setRequest(parm,&$$,sizeof(XtokEnumClasses),OPS_EnumerateClasses);
       buildEnumClassesRequest(parm);
    }
;

enumClassesParmsList
    : enumClassesParms
    {
       $$.flags=$1.flags;
       $$.flagsSet=$1.flagsSet;
       if ($1.className) $$.className=$1.className;
    }
    | enumClassesParmsList enumClassesParms
    {
       $$.flags=$1.flags|$2.flags;
       $$.flagsSet=$1.flagsSet|$2.flagsSet;
       if ($2.className) $$.className=$2.className;
    }
;

enumClassesParms
    : XTOK_IP_CLASSNAME ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_CLASSNAME className ZTOK_IPARAMVALUE
    {
       $$.className = $2;
       $$.flags = $$.flagsSet = 0 ;
    }
    | XTOK_IP_DEEPINHERITANCE ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_DEEPINHERITANCE boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_deepInheritance : 0 ;
       $$.flagsSet = FL_deepInheritance;
       $$.className=0;
    }
    | XTOK_IP_LOCALONLY ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_LOCALONLY boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_localOnly : 0 ;
       $$.flagsSet = FL_localOnly;
       $$.className=0;
    }
    | XTOK_IP_INCLUDEQUALIFIERS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_INCLUDEQUALIFIERS boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_includeQualifiers : 0 ;
       $$.flagsSet = FL_includeQualifiers;
       $$.className=0;
    }
    | XTOK_IP_INCLUDECLASSORIGIN ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_INCLUDECLASSORIGIN boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_includeClassOrigin : 0 ;
       $$.flagsSet = FL_includeClassOrigin;
       $$.className=0;
    }
;

/*
 *    getInstance
*/

getInstance
    : localNameSpacePath
    {
       $$.op.count = GI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_GetInstance;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.flags = FL_localOnly;
       $$.propertyList.values = NULL;
       $$.properties=0;
       $$.instNameSet = 0;
       //$$.userRole=setCharsMsgSegment($$.op.role);

       setRequest(parm,&$$,sizeof(XtokGetInstance),OPS_GetInstance);
       buildGetInstanceRequest(parm);
    }
    | localNameSpacePath getInstanceParmsList
    {
       $$.op.count = GI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_GetInstance;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.instanceName.className);
       $$.flags = ($2.flags & $2.flagsSet) | ((~$2.flagsSet) & (FL_localOnly));
       $$.instanceName = $2.instanceName;
       $$.instNameSet = $2.instNameSet;
       $$.propertyList = $2.propertyList;
       $$.properties=$2.properties;

       setRequest(parm,&$$,sizeof(XtokGetInstance),OPS_GetInstance);
       buildGetInstanceRequest(parm);
    }
;

getInstanceParmsList
    : getInstanceParms
    {
       $$.flags=$1.flags;
       $$.flagsSet=$1.flagsSet;
       if ($1.instNameSet) $$.instanceName=$1.instanceName;
       $$.instNameSet = $1.instNameSet;
       if ($1.propertyList.values) {
          $$.propertyList=$1.propertyList;
          $$.properties=$1.properties;
       }
    }
    | getInstanceParmsList getInstanceParms
    {
       $$.flags=$1.flags|$2.flags;
       $$.flagsSet=$1.flagsSet|$2.flagsSet;
       if ($2.instNameSet) $$.instanceName=$2.instanceName;
       $$.instNameSet = $2.instNameSet;
       if ($2.propertyList.values) {
          $$.propertyList=$2.propertyList;
          $$.properties=$2.properties;
       }
    }
;

getInstanceParms
    : XTOK_IP_INSTANCENAME instanceName ZTOK_IPARAMVALUE
    {
       $$.instanceName = $2;
       $$.flags = $$.flagsSet = 0 ;
       $$.propertyList.values=0;
       $$.instNameSet = 1;
       $$.properties=0;
    }
    | XTOK_IP_LOCALONLY boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_localOnly : 0 ;
       $$.flagsSet = FL_localOnly;
       $$.propertyList.values=0;
       $$.properties=$$.instNameSet=0;
    }
    | XTOK_IP_LOCALONLY ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_INCLUDEQUALIFIERS boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_includeQualifiers : 0 ;
       $$.flagsSet = FL_includeQualifiers;
       $$.propertyList.values=0;
       $$.properties=$$.instNameSet=0;
    }
    | XTOK_IP_INCLUDEQUALIFIERS ZTOK_IPARAMVALUE
    {
      memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_INCLUDECLASSORIGIN boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_includeClassOrigin : 0 ;
       $$.flagsSet = FL_includeClassOrigin;
       $$.propertyList.values=0;
       $$.properties=$$.instNameSet=0;
    }
    | XTOK_IP_INCLUDECLASSORIGIN ZTOK_IPARAMVALUE
    {
      memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_PROPERTYLIST valueArray ZTOK_IPARAMVALUE
    {
       $$.propertyList=$2;
       $$.properties=$2.next;
       $$.instNameSet=0;
       $$.flags = $$.flagsSet = 0 ;
    }
    | XTOK_IP_PROPERTYLIST ZTOK_IPARAMVALUE
    {
      memset(&$$, 0, sizeof($$));
    }    
;


/*
 *    createClass
*/


createClass
    : localNameSpacePath
    {
       $$.op.count = CC_REQ_REG_SEGMENTS;
       $$.op.type = OPS_CreateClass;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.superClass=NULL;

       setRequest(parm,&$$,sizeof(XtokCreateClass),OPS_CreateClass);
       buildCreateClassRequest(parm);
    }
    | localNameSpacePath createClassParm
    {
       $$.op.count = CC_REQ_REG_SEGMENTS;
       $$.op.type = OPS_CreateClass;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.cls.className);
       $$.superClass=$2.cls.superClass;
       $$.cls = $2.cls;

       setRequest(parm,&$$,sizeof(XtokCreateClass),OPS_CreateClass);
       buildCreateClassRequest(parm);
    }
;

createClassParm
    : XTOK_IP_CLASS class ZTOK_IPARAMVALUE
    {
       $$.cls = $2;
    }
;


/*
 *    createInstance
*/


createInstance
    : localNameSpacePath
    {
       $$.op.count = CI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_CreateInstance;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);

       setRequest(parm,&$$,sizeof(XtokCreateInstance),OPS_CreateInstance);
       buildCreateInstanceRequest(parm);
    }
    | localNameSpacePath createInstanceParm
    {
       $$.op.count = CI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_CreateInstance;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.instance.className);
       $$.instance = $2.instance;

       setRequest(parm,&$$,sizeof(XtokCreateInstance),OPS_CreateInstance);
       buildCreateInstanceRequest(parm);
    }
;


createInstanceParm
    : XTOK_IP_INSTANCE instance ZTOK_IPARAMVALUE
    {
       $$.instance = $2;
    }
;

/*
 *    modifyInstance
*/


modifyInstance
    : localNameSpacePath
    {
       $$.op.count = MI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_ModifyInstance;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.flags = FL_includeQualifiers;
       $$.propertyList.values = 0;
       $$.properties=0;

       setRequest(parm,&$$,sizeof(XtokModifyInstance),OPS_ModifyInstance);
       if (buildModifyInstanceRequest(parm)) yyerror(parm, "Invalid Parameter");
    }
    | localNameSpacePath modifyInstanceParmsList
    {
       $$.op.count = MI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_ModifyInstance;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.namedInstance.path.className);
       $$.namedInstance = $2.namedInstance;
       $$.flags = $2.flags | ((~$2.flagsSet) & (FL_includeQualifiers));
       $$.propertyList = $2.propertyList;
       $$.properties=$2.properties;

       setRequest(parm,&$$,sizeof(XtokModifyInstance),OPS_ModifyInstance);
       if (buildModifyInstanceRequest(parm)) yyerror(parm, "Invalid Parameter");
    }
;

modifyInstanceParmsList
    : modifyInstanceParms
    {
       $$.flags=$1.flags;
       $$.flagsSet=$1.flagsSet;
       if ($1.namedInstSet) $$.namedInstance=$1.namedInstance;
       $$.namedInstSet = $1.namedInstSet;
       if ($1.propertyList.values) {
          $$.propertyList=$1.propertyList;
          $$.properties=$1.properties;
       }
    }
    | modifyInstanceParmsList modifyInstanceParms
    {
       $$.flags=$1.flags|$2.flags;
       $$.flagsSet=$1.flagsSet|$2.flagsSet;
       if ($2.namedInstSet) $$.namedInstance=$2.namedInstance;
       $$.namedInstSet = $2.namedInstSet;
       if ($2.propertyList.values) {
          $$.propertyList=$2.propertyList;
          $$.properties=$2.properties;
       }
    }
;


modifyInstanceParms
    : XTOK_IP_MODIFIEDINSTANCE namedInstance ZTOK_IPARAMVALUE
    {
       $$.namedInstance=$2;
       $$.namedInstSet=1;
       $$.propertyList.values=NULL;
       $$.properties=0;
       $$.flags = $$.flagsSet = 0 ;
    }
    | XTOK_IP_INCLUDEQUALIFIERS boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_includeQualifiers : 0 ;
       $$.flagsSet = FL_includeQualifiers;
       $$.propertyList.values=0;
       $$.properties=$$.namedInstSet=0;
    }
    | XTOK_IP_INCLUDEQUALIFIERS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_PROPERTYLIST valueArray ZTOK_IPARAMVALUE
    {
       $$.propertyList=$2;
       $$.properties=$2.next;
       $$.namedInstSet=0;
       $$.flags = $$.flagsSet = 0 ;
    }
    | XTOK_IP_PROPERTYLIST ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
;


/*
 *    deleteClass
*/

deleteClass
    : localNameSpacePath
    {
       $$.op.count = DC_REQ_REG_SEGMENTS;
       $$.op.type = OPS_DeleteClass;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);

       setRequest(parm,&$$,sizeof(XtokDeleteClass),OPS_DeleteClass);
       buildDeleteClassRequest(parm);
    }
    | localNameSpacePath deleteClassParm
    {
       $$.op.count = DC_REQ_REG_SEGMENTS;
       $$.op.type = OPS_DeleteClass;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.className);
       $$.className = $2.className;

       setRequest(parm,&$$,sizeof(XtokDeleteClass),OPS_DeleteClass);
       buildDeleteClassRequest(parm);
    }
;


deleteClassParm
    : XTOK_IP_CLASSNAME className ZTOK_IPARAMVALUE
    {
       $$.className = $2;
    }
;


/*
 *    deleteInstance
*/

deleteInstance
    : localNameSpacePath
    {
       $$.op.count = DI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_DeleteInstance;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);

       setRequest(parm,&$$,sizeof(XtokDeleteInstance),OPS_DeleteInstance);
       buildDeleteInstanceRequest(parm);
    }
    | localNameSpacePath deleteInstanceParm
    {
       $$.op.count = DI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_DeleteInstance;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.instanceName.className);
       $$.instanceName = $2.instanceName;

       setRequest(parm,&$$,sizeof(XtokDeleteInstance),OPS_DeleteInstance);
       buildDeleteInstanceRequest(parm);
    }
;


deleteInstanceParm
    : XTOK_IP_INSTANCENAME instanceName ZTOK_IPARAMVALUE
    {
       $$.instanceName = $2;
    }
;



/*
 *    enumInstanceNames
*/

enumInstanceNames
    : localNameSpacePath XTOK_IP_CLASSNAME className ZTOK_IPARAMVALUE
    {
       $$.op.count = EIN_REQ_REG_SEGMENTS;
       $$.op.type = OPS_EnumerateInstanceNames;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($3);

       setRequest(parm,&$$,sizeof(XtokEnumInstanceNames),OPS_EnumerateInstanceNames);
       buildEnumInstanceNamesRequest(parm);
    }
;


/*
 *    enumInstances
*/


enumInstances
    : localNameSpacePath
    {
       $$.op.count = EI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_EnumerateInstances;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.flags = FL_localOnly | FL_deepInheritance;
       $$.propertyList.values = NULL;
       $$.properties=0;

       setRequest(parm,&$$,sizeof(XtokEnumInstances),OPS_EnumerateInstances);
       buildEnumInstanceRequest(parm);
    }
    | localNameSpacePath enumInstancesParmsList
    {
       $$.op.count = EI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_EnumerateInstances;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.className);
       $$.flags = ($2.flags & $2.flagsSet) | ((~$2.flagsSet) & (FL_localOnly));
       $$.propertyList = $2.propertyList;
       $$.properties=$2.properties;

       setRequest(parm,&$$,sizeof(XtokEnumInstances),OPS_EnumerateInstances);
       buildEnumInstanceRequest(parm);
    }
;

enumInstancesParmsList
    : enumInstancesParms
    {
       $$.flags=$1.flags;
       $$.flagsSet=$1.flagsSet;
       if ($1.className) $$.className=$1.className;
       if ($1.propertyList.values) {
          $$.propertyList=$1.propertyList;
          $$.properties=$1.properties;
       }
    }
    | enumInstancesParmsList enumInstancesParms
    {
       $$.flags=$1.flags|$2.flags;
       $$.flagsSet=$1.flagsSet|$2.flagsSet;
       if ($2.className) $$.className=$2.className;
       if ($2.propertyList.values) {
          $$.propertyList=$2.propertyList;
          $$.properties=$2.properties;
       }
    }
;

enumInstancesParms
    : XTOK_IP_CLASSNAME className ZTOK_IPARAMVALUE
    {
       $$.className = $2;
       $$.flags = $$.flagsSet = 0 ;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_LOCALONLY boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_localOnly : 0 ;
       $$.flagsSet = FL_localOnly;
       $$.className=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_LOCALONLY ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_INCLUDEQUALIFIERS boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_includeQualifiers : 0 ;
       $$.flagsSet = FL_includeQualifiers;
       $$.className=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_INCLUDEQUALIFIERS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_DEEPINHERITANCE boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_deepInheritance : 0 ;
       $$.flagsSet = FL_deepInheritance;
       $$.className=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_DEEPINHERITANCE ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_INCLUDECLASSORIGIN boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_includeClassOrigin : 0 ;
       $$.flagsSet = FL_includeClassOrigin;
       $$.className=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_INCLUDECLASSORIGIN ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_PROPERTYLIST valueArray ZTOK_IPARAMVALUE
    {
       $$.propertyList=$2;
       $$.properties=$2.next;
       $$.className=0;
       $$.flags = $$.flagsSet = 0 ;
    }
    | XTOK_IP_PROPERTYLIST ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
;




/*
 *    execQuery
*/

execQuery
    : localNameSpacePath 
          XTOK_IP_QUERY value ZTOK_IPARAMVALUE
          XTOK_IP_QUERYLANG value ZTOK_IPARAMVALUE
    {
       $$.op.count = EQ_REQ_REG_SEGMENTS;
       $$.op.type = OPS_ExecQuery;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.query=setCharsMsgSegment($3.value);
       $$.op.queryLang=setCharsMsgSegment($6.value);

       setRequest(parm,&$$,sizeof(XtokExecQuery),OPS_ExecQuery);
       buildExecQueryRequest(parm);
    }        
    | localNameSpacePath 
          XTOK_IP_QUERYLANG value ZTOK_IPARAMVALUE
          XTOK_IP_QUERY value ZTOK_IPARAMVALUE
    {
       $$.op.count = EQ_REQ_REG_SEGMENTS;
       $$.op.type = OPS_ExecQuery;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.query=setCharsMsgSegment($6.value);
       $$.op.queryLang=setCharsMsgSegment($3.value);

       setRequest(parm,&$$,sizeof(XtokExecQuery),OPS_ExecQuery);
       buildExecQueryRequest(parm);
    }        
;    
    
    
/*
 *    associators
*/


associators
    : localNameSpacePath
    {
       $$.op.count = AI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_Associators;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.op.assocClass=setCharsMsgSegment(NULL);
       $$.op.resultClass=setCharsMsgSegment(NULL);
       $$.op.role=setCharsMsgSegment(NULL);
       $$.op.resultRole=setCharsMsgSegment(NULL);
       $$.flags = 0;
       $$.objNameSet = 0;
       $$.propertyList.values = 0;
       $$.properties=0;

       setRequest(parm,&$$,sizeof(XtokAssociators),OPS_Associators);
       buildAssociatorsRequest(parm);
    }
    | localNameSpacePath associatorsParmsList
    {
       $$.op.count = AI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_Associators;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.objectName.className);
       $$.op.assocClass=setCharsMsgSegment($2.assocClass);
       $$.op.resultClass=setCharsMsgSegment($2.resultClass);
       $$.op.role=setCharsMsgSegment($2.role);
       $$.op.resultRole=setCharsMsgSegment($2.resultRole);
       $$.flags = ($2.flags & $2.flagsSet) | (~$2.flagsSet & 0);
       $$.objectName = $2.objectName;
       $$.objNameSet = $2.objNameSet;
       $$.propertyList = $2.propertyList;
       $$.properties=$2.properties;

       setRequest(parm,&$$,sizeof(XtokAssociators),OPS_Associators);
       buildAssociatorsRequest(parm);
    }
;

associatorsParmsList
    : associatorsParms
    {
       $$.flags=$1.flags;
       $$.flagsSet=$1.flagsSet;
       if ($1.objNameSet)  {
          $$.objectName=$1.objectName;
          $$.objNameSet = $1.objNameSet;
       }
       $$.assocClass=$1.assocClass;
       $$.resultClass=$1.resultClass;
       $$.role=$1.role;
       $$.resultRole=$1.resultRole;
       if ($1.propertyList.values) {
          $$.propertyList=$1.propertyList;
          $$.properties=$1.properties;
       }
    }
    | associatorsParmsList associatorsParms
    {
       $$.flags=$1.flags|$2.flags;
       $$.flagsSet=$1.flagsSet|$2.flagsSet;
       if ($2.assocClass) $$.assocClass=$2.assocClass;
       else if ($2.resultClass) $$.resultClass=$2.resultClass;
       else if ($2.role) $$.role=$2.role;
       else if ($2.resultRole) $$.resultRole=$2.resultRole;
       else if ($2.objNameSet) {
          $$.objectName=$2.objectName;
          $$.objNameSet = $2.objNameSet;
       }
       else if ($2.propertyList.values) {
          $$.propertyList=$2.propertyList;
          $$.properties=$2.properties;
       }
    }
;

associatorsParms
    : XTOK_IP_OBJECTNAME instanceName ZTOK_IPARAMVALUE
    {
       $$.objectName = $2;
       $$.objNameSet = 1;
       $$.flags = $$.flagsSet = 0 ;
       $$.assocClass=$$.resultClass=$$.role=$$.resultRole=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_OBJECTNAME className ZTOK_IPARAMVALUE
    {
       // This is an unsupported operation. To ensure we handle with a friendly
       // error message, make it appear as if INSTANCENAME with no KEYBINDING
       // was passed in the XML.
       $$.objectName.className = $2;
       $$.objectName.bindings.next=0;
       $$.objectName.bindings.keyBindings=NULL;
       $$.objNameSet = 1;
       $$.flags = $$.flagsSet = 0;
       $$.resultClass=$$.role=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_ASSOCCLASS className ZTOK_IPARAMVALUE
    {
       $$.assocClass = $2;
       $$.objNameSet=$$.flags = $$.flagsSet = 0 ;
       $$.resultClass=$$.role=$$.resultRole=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_ASSOCCLASS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_RESULTCLASS className ZTOK_IPARAMVALUE
    {
       $$.resultClass = $2;
       $$.objNameSet=$$.flags = $$.flagsSet = 0 ;
       $$.assocClass=$$.role=$$.resultRole=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_RESULTCLASS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_ROLE value ZTOK_IPARAMVALUE
    {
       $$.role = $2.value;
       $$.objNameSet=$$.flags = $$.flagsSet = 0 ;
       $$.assocClass=$$.resultClass=$$.resultRole=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_ROLE ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_RESULTROLE value ZTOK_IPARAMVALUE
    {
       $$.resultRole = $2.value;
       $$.objNameSet=$$.flags = $$.flagsSet = 0 ;
       $$.assocClass=$$.resultClass=$$.role=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_RESULTROLE ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_INCLUDEQUALIFIERS boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_includeQualifiers : 0 ;
       $$.flagsSet = FL_includeQualifiers;
       $$.objNameSet=0;
       $$.assocClass=$$.resultClass=$$.role=$$.resultRole=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_INCLUDEQUALIFIERS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_INCLUDECLASSORIGIN boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_includeClassOrigin : 0 ;
       $$.flagsSet = FL_includeClassOrigin;
       $$.objNameSet=0;
       $$.assocClass=$$.resultClass=$$.role=$$.resultRole=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_INCLUDECLASSORIGIN ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_PROPERTYLIST valueArray ZTOK_IPARAMVALUE
    {
       $$.propertyList=$2;
       $$.properties=$2.next;
       $$.objNameSet=$$.flags = $$.flagsSet = 0 ;
       $$.assocClass=$$.resultClass=$$.role=$$.resultRole=0;
    }
    | XTOK_IP_PROPERTYLIST ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
;




/*
 *    references
*/


references
    : localNameSpacePath
    {
       $$.op.count = RI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_References;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.op.resultClass=setCharsMsgSegment(NULL);
       $$.op.role=setCharsMsgSegment(NULL);
       $$.flags = 0;
       $$.objNameSet = 0;
       $$.propertyList.values = 0;
       $$.properties=0;

       setRequest(parm,&$$,sizeof(XtokReferences),OPS_References);
       buildReferencesRequest(parm);
    }
    | localNameSpacePath referencesParmsList
    {
       $$.op.count = RI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_References;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.objectName.className);
       $$.op.resultClass=setCharsMsgSegment($2.resultClass);
       $$.op.role=setCharsMsgSegment($2.role);
       $$.flags = ($2.flags & $2.flagsSet) | (~$2.flagsSet & 0);
       $$.objectName = $2.objectName;
       $$.objNameSet = $2.objNameSet;
       $$.propertyList = $2.propertyList;
       $$.properties=$2.properties;

       setRequest(parm,&$$,sizeof(XtokReferences),OPS_References);
       buildReferencesRequest(parm);
    }
;

referencesParmsList
    : referencesParms
    {
       $$.flags=$1.flags;
       $$.flagsSet=$1.flagsSet;
       if ($1.objNameSet)  {
          $$.objectName=$1.objectName;
          $$.objNameSet = $1.objNameSet;
       }
       $$.resultClass=$1.resultClass;
       $$.role=$1.role;
       if ($1.propertyList.values) {
          $$.propertyList=$1.propertyList;
          $$.properties=$1.properties;
       }
    }
    | referencesParmsList referencesParms
    {
       $$.flags=$1.flags|$2.flags;
       $$.flagsSet=$1.flagsSet|$2.flagsSet;
       if ($2.resultClass) $$.resultClass=$2.resultClass;
       else if ($2.role) $$.role=$2.role;
       else if ($2.objNameSet) {
          $$.objectName=$2.objectName;
          $$.objNameSet = $2.objNameSet;
       }
       else if ($2.propertyList.values) {
          $$.propertyList=$2.propertyList;
          $$.properties=$2.properties;
       }
   }
;

referencesParms
    : XTOK_IP_OBJECTNAME instanceName ZTOK_IPARAMVALUE
    {
       $$.objectName = $2;
       $$.objNameSet = 1;
       $$.flags = $$.flagsSet = 0 ;
       $$.resultClass=$$.role=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_OBJECTNAME className ZTOK_IPARAMVALUE
    {
       $$.objectName.className = $2;
       $$.objectName.bindings.next=0;
       $$.objectName.bindings.keyBindings=NULL;
       $$.objNameSet = 1;
       $$.flags = $$.flagsSet = 0;
       $$.resultClass=$$.role=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_RESULTCLASS className ZTOK_IPARAMVALUE
    {
       $$.resultClass = $2;
       $$.objNameSet=$$.flags = $$.flagsSet = 0 ;
       $$.role=0;
       $$.properties=0;
       $$.propertyList.values=0;
   }
    | XTOK_IP_RESULTCLASS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_ROLE value ZTOK_IPARAMVALUE
    {
       $$.role = $2.value;
       $$.objNameSet=$$.flags = $$.flagsSet = 0 ;
       $$.resultClass=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_ROLE ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_INCLUDEQUALIFIERS boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_includeQualifiers : 0 ;
       $$.flagsSet = FL_includeQualifiers;
       $$.objNameSet=0;
       $$.resultClass=$$.role=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_INCLUDEQUALIFIERS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_INCLUDECLASSORIGIN boolValue ZTOK_IPARAMVALUE
    {
       $$.flags = $2 ? FL_includeClassOrigin : 0 ;
       $$.flagsSet = FL_includeClassOrigin;
       $$.objNameSet=0;
       $$.resultClass=$$.role=0;
       $$.properties=0;
       $$.propertyList.values=0;
    }
    | XTOK_IP_INCLUDECLASSORIGIN ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_PROPERTYLIST valueArray ZTOK_IPARAMVALUE
    {
       $$.propertyList=$2;
       $$.properties=$2.next;
       $$.objNameSet=$$.flags = $$.flagsSet = 0 ;
       $$.resultClass=$$.role=0;
    }
    | XTOK_IP_PROPERTYLIST ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
;


/*
 *    associatorNames
*/


associatorNames
    : localNameSpacePath
    {
       $$.op.count = AIN_REQ_REG_SEGMENTS;
       $$.op.type = OPS_AssociatorNames;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.op.assocClass=setCharsMsgSegment(NULL);
       $$.op.resultClass=setCharsMsgSegment(NULL);
       $$.op.role=setCharsMsgSegment(NULL);
       $$.op.resultRole=setCharsMsgSegment(NULL);
       $$.objNameSet = 0;

       setRequest(parm,&$$,sizeof(XtokAssociatorNames),OPS_AssociatorNames);
       buildAssociatorNamesRequest(parm);
    }
    | localNameSpacePath associatorNamesParmsList
    {
       $$.op.count = AIN_REQ_REG_SEGMENTS;
       $$.op.type = OPS_AssociatorNames;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.objectName.className);
       $$.op.assocClass=setCharsMsgSegment($2.assocClass);
       $$.op.resultClass=setCharsMsgSegment($2.resultClass);
       $$.op.role=setCharsMsgSegment($2.role);
       $$.op.resultRole=setCharsMsgSegment($2.resultRole);
       $$.objectName = $2.objectName;
       $$.objNameSet = $2.objNameSet;
       setRequest(parm,&$$,sizeof(XtokAssociatorNames),OPS_AssociatorNames);
       buildAssociatorNamesRequest(parm);
    }
;

associatorNamesParmsList
    : associatorNamesParms
    {
       if ($1.objNameSet)  {
          $$.objectName=$1.objectName;
          $$.objNameSet = $1.objNameSet;
       }
      $$.assocClass=$1.assocClass;
      $$.resultClass=$1.resultClass;
      $$.role=$1.role;
      $$.resultRole=$1.resultRole;
    }
    | associatorNamesParmsList associatorNamesParms
    {
       if ($2.assocClass) $$.assocClass=$2.assocClass;
       else if ($2.resultClass) $$.resultClass=$2.resultClass;
       else if ($2.role) $$.role=$2.role;
       else if ($2.resultRole) $$.resultRole=$2.resultRole;
       else if ($2.objNameSet) {
          $$.objectName=$2.objectName;
          $$.objNameSet = $2.objNameSet;
       }
    }
;

associatorNamesParms
    : XTOK_IP_OBJECTNAME instanceName ZTOK_IPARAMVALUE
    {
       $$.objectName = $2;
       $$.objNameSet = 1;
       $$.assocClass=$$.resultClass=$$.role=$$.resultRole=0;
    }
    | XTOK_IP_OBJECTNAME className ZTOK_IPARAMVALUE
    {
       $$.objectName.className = $2;
       $$.objectName.bindings.next=0;
       $$.objectName.bindings.keyBindings=NULL;
       $$.objNameSet = 1;
       $$.resultClass=$$.role=0;
    }
    | XTOK_IP_ASSOCCLASS className ZTOK_IPARAMVALUE
    {
       $$.assocClass = $2;
       $$.objNameSet = 0 ;
       $$.resultClass=$$.role=$$.resultRole=0;
    }
    | XTOK_IP_ASSOCCLASS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_RESULTCLASS className ZTOK_IPARAMVALUE
    {
       $$.resultClass = $2;
       $$.objNameSet = 0 ;
       $$.assocClass=$$.role=$$.resultRole=0;
    }
    | XTOK_IP_RESULTCLASS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_ROLE value ZTOK_IPARAMVALUE
    {
       $$.role = $2.value;
       $$.objNameSet = 0 ;
       $$.assocClass=$$.resultClass=$$.resultRole=0;
    }
    | XTOK_IP_ROLE ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_RESULTROLE value ZTOK_IPARAMVALUE
    {
       $$.resultRole = $2.value;
       $$.objNameSet= 0 ;
       $$.assocClass=$$.resultClass=$$.role=0;
    }
    | XTOK_IP_RESULTROLE ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }    
;



/*
 *    referenceNames
*/


referenceNames
    : localNameSpacePath
    {
       $$.op.count = RIN_REQ_REG_SEGMENTS;
       $$.op.type = OPS_ReferenceNames;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.op.resultClass=setCharsMsgSegment(NULL);
       $$.op.role=setCharsMsgSegment(NULL);
       $$.objNameSet = 0;

       setRequest(parm,&$$,sizeof(XtokReferenceNames),OPS_ReferenceNames);
       buildReferenceNamesRequest(parm);
    }
    | localNameSpacePath referenceNamesParmsList
    {
       $$.op.count = RIN_REQ_REG_SEGMENTS;
       $$.op.type = OPS_ReferenceNames;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.objectName.className);
       $$.op.resultClass=setCharsMsgSegment($2.resultClass);
       $$.op.role=setCharsMsgSegment($2.role);
       $$.objectName = $2.objectName;
       $$.objNameSet = $2.objNameSet;

       setRequest(parm,&$$,sizeof(XtokReferenceNames),OPS_ReferenceNames);
       buildReferenceNamesRequest(parm);
    }
;

referenceNamesParmsList
    : referenceNamesParms
    {
      if ($1.objNameSet)  {
          $$.objectName=$1.objectName;
          $$.objNameSet = $1.objNameSet;
      }
      $$.resultClass = $1.resultClass;
      $$.role = $1.role;
    }
    | referenceNamesParmsList referenceNamesParms
    {
       if($2.objNameSet) {
          $$.objectName=$2.objectName;
          $$.objNameSet=$2.objNameSet;
       }
       else if($2.resultClass) $$.resultClass=$2.resultClass;
       else if($2.role) $$.role=$2.role;              
    }
;

referenceNamesParms
    : XTOK_IP_OBJECTNAME instanceName ZTOK_IPARAMVALUE
    {
       $$.objectName = $2;
       $$.objNameSet = 1;
       $$.resultClass=$$.role=0;
    }
    | XTOK_IP_OBJECTNAME className ZTOK_IPARAMVALUE
    {
       $$.objectName.className = $2;
       $$.objectName.bindings.next=0;
       $$.objectName.bindings.keyBindings=NULL;
       $$.objNameSet = 1;
       $$.resultClass=$$.role=0;
    }
    | XTOK_IP_RESULTCLASS className ZTOK_IPARAMVALUE
    {
       $$.resultClass = $2;
       $$.objNameSet = 0;
       $$.role=0;
    }
    | XTOK_IP_RESULTCLASS ZTOK_IPARAMVALUE
    {
      memset(&$$, 0, sizeof($$));
    }    
    | XTOK_IP_ROLE value ZTOK_IPARAMVALUE
    {
       $$.role = $2.value;
       $$.objNameSet = 0 ;
       $$.resultClass=0;
    }
    | XTOK_IP_ROLE ZTOK_IPARAMVALUE
    {
      memset(&$$, 0, sizeof($$));
    }    
;


/*
 *    valueNamedInstance
*/

namedInstance
    : XTOK_VALUENAMEDINSTANCE instanceName instance ZTOK_VALUENAMEDINSTANCE
    {
        $$.path=$2;
	$$.instance=$3;
    }
;


/*
 *    class
*/

class
    : XTOK_CLASS classData ZTOK_CLASS
    {
       if (((ParserControl*)parm)->Qs) 
          $$.qualifiers=((ParserControl*)parm)->qualifiers;
       else memset(&$$.qualifiers,0,sizeof($$.qualifiers));
       if (((ParserControl*)parm)->Ps) 
          $$.properties=((ParserControl*)parm)->properties;
       else memset(&$$.properties,0,sizeof($$.properties));
       if (((ParserControl*)parm)->Ms) 
          $$.methods=((ParserControl*)parm)->methods;
       else memset(&$$.methods,0,sizeof($$.methods));
    }
;

classData
    : /* empty */ {;}
    | classData qualifier
    {
       ((ParserControl*)parm)->Qs++;
       addQualifier(&(((ParserControl*)parm)->qualifiers),&$2);
    }
    | classData property     {
       ((ParserControl*)parm)->Ps++;
       addProperty(&(((ParserControl*)parm)->properties),&$2);
    }
    | classData method     {
        ((ParserControl*)parm)->Ms++;
        addMethod(&(((ParserControl*)parm)->methods),&$2);
    }
;

method
    : XTOK_METHOD methodData ZTOK_METHOD
    {
       if (((ParserControl*)parm)->MQs) 
          $$.qualifiers=$2.qualifiers;
       else memset(&$$.qualifiers,0,sizeof($$.qualifiers));
       if (((ParserControl*)parm)->MPs) 
          $$.params=$2.params;
       else memset(&$$.params,0,sizeof($$.params));
       ((ParserControl*)parm)->MQs=0; 
       ((ParserControl*)parm)->MPs=0; 
       ((ParserControl*)parm)->MPQs=0; 
    }   
;

methodData 
    : /* empty */ {;}
    | methodData qualifier
    {
       if (((ParserControl*)parm)->MQs==0) 
          memset(&$$.qualifiers,0,sizeof($$.qualifiers));
       ((ParserControl*)parm)->MQs++;
       addQualifier(&($$.qualifiers),&$2);
    }      
    | methodData XTOK_PARAM parameter ZTOK_PARAM 
    {
       if (((ParserControl*)parm)->MPs==0) 
          memset(&$$.params,0,sizeof($$.params));
       ((ParserControl*)parm)->MPs++;
       if (((ParserControl*)parm)->MPQs) 
          $2.qualifiers=$3.qualifiers;
       else memset(&$2.qualifiers,0,sizeof($2.qualifiers));
       addParam(&($$.params),&$2);
       ((ParserControl*)parm)->MPQs=0; 
    }      
;  

parameter 
    : /* empty */ {;}
    | parameter qualifier
    {
       if (((ParserControl*)parm)->MPQs==0) 
          memset(&$$.qualifiers,0,sizeof($$.qualifiers));
       ((ParserControl*)parm)->MPQs++; 
       addQualifier(&($$.qualifiers),&$2);
    }
;


/*
 *    instance
*/

instance
    : XTOK_INSTANCE instanceData ZTOK_INSTANCE
    {
       if($2.qualifiers.first)
          $$.qualifiers=$2.qualifiers;
       else memset(&$$.qualifiers,0,sizeof($$.qualifiers));
       
       if($2.properties.first)
          $$.properties=$2.properties;
       else memset(&$$.properties,0,sizeof($$.properties)); 
    }
;

instanceData 
    : /* empty */ 
    {
       $$.properties.last=0;
       $$.properties.first=0;
       $$.qualifiers.last=0;
       $$.qualifiers.first=0;       
    }
    | instanceData qualifier 
    {
       addQualifier(&($$.qualifiers),&$2);
    }
    | instanceData property 
    {
       addProperty(&($$.properties),&$2);
    }
;

/*
 *    qualifierDeclaration
*/

qualifierDeclaration
    : /* empty */ {;}
    | XTOK_QUALIFIERDECLARATION scope qualifierDeclarationData ZTOK_QUALIFIERDECLARATION
    {
    	$$.scope = $2;
    	$$.data = $3;
    }     
;

qualifierDeclarationData
    : /* empty */
    {
    	$$.value.value = NULL;
    }
    | value
    {
    	$$.value = $1;
    	$$.type = 0;
    }
    | valueArray
    {
    	$$.valueArray=$1;
    	$$.type=CMPI_ARRAY;
    }    
;   
/*
 *   scope
*/

scope
    : /* empty */ {;}
    | XTOK_SCOPE ZTOK_SCOPE
	{
	}
;

/*
 *    property
*/

property
    : XTOK_PROPERTY qualifierList propertyData ZTOK_PROPERTY
    {
       $3.qualifiers=$2;
       $$.val=$3;
       
       if($$.val.val.value) {
          if($$.val.val.type == typeValue_Instance)
             $$.valueType = CMPI_instance;
          else if($$.val.val.type == typeValue_Class)
             $$.valueType = CMPI_class;
       }
    }  
    | XTOK_PROPERTYREFERENCE qualifierList propertyData ZTOK_PROPERTYREFERENCE
    {
       $3.qualifiers=$2;
       $$.val=$3;
    }
    | XTOK_PROPERTYARRAY qualifierList propertyData ZTOK_PROPERTYARRAY
    {
       $3.qualifiers=$2;
       $$.val=$3;
       
       if($$.val.list.values) {
          if($$.val.list.values[0].type == typeValue_Instance)
          	$$.valueType = CMPI_instance | CMPI_ARRAY;
          if($$.val.list.values[0].type == typeValue_Class)
          	$$.valueType = CMPI_class | CMPI_ARRAY;          	
       }     
    }
;

qualifierList
    :
    {
      $$.first = $$.last = NULL;
    }
    | qualifierList qualifier
    {
       addQualifier(&$1,&$2);
       $$ = $1;
    }
;

propertyData 
    :
    {
       $$.val.value = NULL;
       $$.list.values = NULL;
       $$.val.type = 0;
    }   
    | value
    {
       $$.val=$1;
    }  
    | valueReference
    {
       $$.ref=$1;
    }
    | valueArray
    {
       $$.list=$1;
    }
;  



/*
 *    qualifier
*/

qualifier
    : XTOK_QUALIFIER value ZTOK_QUALIFIER
    {
       $$.value=$2;
    }
    | XTOK_QUALIFIER valueArray ZTOK_QUALIFIER
    {
       $$.valueArray=$2;
       $$.type |= CMPI_ARRAY;
    }
;

/*
 *    localNameSpacePath 
*/



localNameSpacePath
    : XTOK_LOCALNAMESPACEPATH namespaces ZTOK_LOCALNAMESPACEPATH
    {
       $$=$2.cns;
    }
;

namespaces
    : XTOK_NAMESPACE ZTOK_NAMESPACE
    {
       $$.cns=strdup($1.ns);
    }
    | namespaces XTOK_NAMESPACE ZTOK_NAMESPACE
    {
       int l=strlen($1.cns)+strlen($2.ns)+2;
       $$.cns=malloc(l);
       strcpy($$.cns,$1.cns);
       strcat($$.cns,"/");
       strcat($$.cns,$2.ns);
       free($1.cns);
    }
;


nameSpacePath
    : XTOK_NAMESPACEPATH host localNameSpacePath ZTOK_NAMESPACEPATH
    {
       $$.host=$2;
       $$.nameSpacePath=$3;
    }
;

host
    : XTOK_HOST ZTOK_HOST
    {
    }
;

instancePath
    : XTOK_INSTANCEPATH nameSpacePath instanceName ZTOK_INSTANCEPATH
    {
       $$.path=$2;
       $$.instanceName=$3;
       $$.type=1;
    }
 /*
    | nameSpacePath instanceName
    {
    }
    | XTOK_CLASSPATH nameSpacePath className  ZTOK_CLASSPATH
    {
    }
    | XTOK_LOCALCLASSPATH localNameSpacePath className ZTOK_LOCALCLASSPATH
    {
    } */
;

localInstancePath
    : XTOK_LOCALINSTANCEPATH localNameSpacePath instanceName ZTOK_LOCALINSTANCEPATH
    {
       $$.path=$2;
       $$.instanceName=$3;
       $$.type=1;
    }
;

localClassPath
    : XTOK_LOCALCLASSPATH localNameSpacePath className ZTOK_LOCALCLASSPATH
    {
       $$.path=$2;
       $$.className=$3;
       $$.type=1;
    }
;
/*
 *    value
*/


value
    : XTOK_VALUE instance ZTOK_VALUE    /* not really standard... */
    {
       $$.instance = malloc(sizeof(XtokInstance));
       $$.instance = memcpy($$.instance, &$2, sizeof(XtokInstance));
       $$.type=typeValue_Instance;
    }
    | XTOK_VALUE XTOK_CDATA instance ZTOK_CDATA ZTOK_VALUE
    {
       $$.instance = malloc(sizeof(XtokInstance));
       $$.instance = memcpy($$.instance, &$3, sizeof(XtokInstance));
       $$.type=typeValue_Instance;
    }
    | XTOK_VALUE XTOK_CDATA class ZTOK_CDATA ZTOK_VALUE
    {
       $$.type=typeValue_Class;
    }
    | XTOK_VALUE ZTOK_VALUE
    {
       $$.value=$1.value;
       $$.type=typeValue_charP;
    }
;

valueArray
    : XTOK_VALUEARRAY ZTOK_VALUEARRAY
	{
	  $$.values=malloc(sizeof(XtokValue));
	  $$.next=0;
	} 
    | XTOK_VALUEARRAY valueList ZTOK_VALUEARRAY
    {
	  $$ = $2;
	  $$.values[$$.next].value = NULL;
	  if($$.next == 0) $$.next = 1;
	}
;

valueList
	:
        value
        {
          $$.next=1;
          $$.max=VALUEARRAY_MAX_START;
          $$.values=malloc(sizeof(XtokValue)*($$.max));
          $$.values[0]=$1;
        }
        | valueList value
        {
          if ($$.next == $$.max) { /* max was hit; let's bump it up 50% */
            $$.max = (int)($$.max * ((float)3)/2);
            $$.values=realloc(($$.values), sizeof(XtokValue)*($$.max));
          }
          $$.values[$$.next]=$2;
          $$.next++;
        }
;

valueReference
    : XTOK_VALUEREFERENCE instancePath ZTOK_VALUEREFERENCE
    {
       $$.instancePath=$2;
       $$.type=typeValRef_InstancePath;
    }
    | XTOK_VALUEREFERENCE localInstancePath ZTOK_VALUEREFERENCE
    {
       $$.localInstancePath=$2;
       $$.type=typeValRef_LocalInstancePath;
    }
    | XTOK_VALUEREFERENCE instanceName ZTOK_VALUEREFERENCE
    {
       $$.instanceName=$2;
       $$.type=typeValRef_InstanceName;
    }
;

valueRefArray
    : XTOK_VALUEREFARRAY valueRefList ZTOK_VALUEREFARRAY
    {
       $$=$2;
    }
;

valueRefList
    : valueReference
    {
       $$.next=1;
       $$.max=VALUEREFARRAY_MAX_START;
       $$.values=malloc(sizeof(XtokValueReference)*($$.max));
       $$.values[0]=$1;
    }
    | valueRefList valueReference
    {
       if ($$.next == $$.max) { /* max was hit; let's bump it up 50% */
         $$.max = (int)($$.max * ((float)3)/2);
         $$.values=realloc(($$.values), sizeof(XtokValueReference)*($$.max));
       }
       $$.values[$$.next]=$2;
       $$.next++;
    }
;

boolValue
    : XTOK_VALUE ZTOK_VALUE
    {
    if (strcasecmp($1.value,"true")==0) $$=1;
    if (strcasecmp($1.value,"false")==0) $$=0;
    }
;

/*
 *    openEnumInstancePaths
*/

openEnumInstancePaths
    : localNameSpacePath
    {
       $$.op.count = EIN_REQ_REG_SEGMENTS;
       $$.op.type = OPS_OpenEnumerateInstancePaths;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.flags=0;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildEnumInstanceNamesRequest(parm);   // TODO
    }
    | localNameSpacePath openEnumInstancePathsParmsList
    {
       $$.op.count = EIN_REQ_REG_SEGMENTS;
       $$.op.type = OPS_OpenEnumerateInstancePaths;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.className);
       $$.flags = ($2.flags & $2.flagsSet) | ((~$2.flagsSet) & (FL_localOnly));
       $$.operationTimeout=$2.operationTimeout;
       $$.maxObjectCount=$2.maxObjectCount;
       $$.filterQuery=$2.filterQuery;
       $$.filterQueryLang=$2.filterQueryLang;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildEnumInstanceNamesRequest(parm);   // TODO
    }
;

openEnumInstancePathsParmsList
    : openEnumInstancePathsParms
    {
       if ($1.className) $$.className=$1.className;
       $$.flags=$1.flags;
       $$.flagsSet=$1.flagsSet;
       if ($1.operationTimeout) $$.operationTimeout=$1.operationTimeout;
       if ($1.maxObjectCount) $$.maxObjectCount=$1.maxObjectCount;
       if ($1.filterQuery) $$.filterQuery=$1.filterQuery;
       if ($1.filterQueryLang) $$.filterQueryLang=$1.filterQueryLang;
    }
    | openEnumInstancePathsParmsList openEnumInstancePathsParms
    {
       if ($2.className) $$.className=$2.className;
       $$.flags=$1.flags|$2.flags;
       $$.flagsSet=$1.flagsSet|$2.flagsSet;
       if ($2.operationTimeout) $$.operationTimeout=$2.operationTimeout;
       if ($2.maxObjectCount) $$.maxObjectCount=$2.maxObjectCount;
       if ($2.filterQuery) $$.filterQuery=$2.filterQuery;
       if ($2.filterQueryLang) $$.filterQueryLang=$2.filterQueryLang;
    }
;

openEnumInstancePathsParms
    : XTOK_IP_CLASSNAME className ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.className = $2;
    }
    | XTOK_IP_CONTINUEONERROR boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_continueOnError : 0 ;
       $$.flagsSet = FL_continueOnError;
    }
    | XTOK_IP_CONTINUEONERROR ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_OPERATIONTIMEOUT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.operationTimeout=atoi($2.value);
    }
    | XTOK_IP_OPERATIONTIMEOUT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_MAXOBJECTCOUNT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.maxObjectCount=atoi($2.value);
    }
    | XTOK_IP_MAXOBJECTCOUNT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_FILTERQUERY value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.filterQuery=$2.value;
    }
    | XTOK_IP_FILTERQUERY ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_FILTERQUERYLANG value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.filterQueryLang=$2.value;
    }
    | XTOK_IP_FILTERQUERYLANG ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
;


/*
 *    openEnumInstances
*/

openEnumInstances
    : localNameSpacePath
    {
       $$.op.count = EI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_OpenEnumerateInstances;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.flags = FL_localOnly | FL_deepInheritance;
       $$.propertyList.values = NULL;
       $$.properties=0;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildOpenEnumInstanceRequest(parm);
    }
    | localNameSpacePath openEnumInstancesParmsList
    {
       $$.op.count = EI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_OpenEnumerateInstances;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.className);
       $$.flags = ($2.flags & $2.flagsSet) | ((~$2.flagsSet) & (FL_localOnly));
       $$.propertyList = $2.propertyList;
       $$.properties=$2.properties;
       $$.operationTimeout=$2.operationTimeout;
       $$.maxObjectCount=$2.maxObjectCount;
       $$.filterQuery=$2.filterQuery;
       $$.filterQueryLang=$2.filterQueryLang;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildOpenEnumInstanceRequest(parm);
    }
;

openEnumInstancesParmsList
    : openEnumInstancesParms
    {
       $$.flags=$1.flags;
       $$.flagsSet=$1.flagsSet;
       if ($1.className) $$.className=$1.className;
       if ($1.propertyList.values) {
          $$.propertyList=$1.propertyList;
          $$.properties=$1.properties;
       }
       if ($1.operationTimeout) $$.operationTimeout=$1.operationTimeout;
       if ($1.maxObjectCount) $$.maxObjectCount=$1.maxObjectCount;
       if ($1.filterQuery) $$.filterQuery=$1.filterQuery;
       if ($1.filterQueryLang) $$.filterQueryLang=$1.filterQueryLang;
    }
    | openEnumInstancesParmsList openEnumInstancesParms
    {
       $$.flags=$1.flags|$2.flags;
       $$.flagsSet=$1.flagsSet|$2.flagsSet;
       if ($2.className) $$.className=$2.className;
       if ($2.propertyList.values) {
          $$.propertyList=$2.propertyList;
          $$.properties=$2.properties;
       }
       if ($2.operationTimeout) $$.operationTimeout=$2.operationTimeout;
       if ($2.maxObjectCount) $$.maxObjectCount=$2.maxObjectCount;
       if ($2.filterQuery) $$.filterQuery=$2.filterQuery;
       if ($2.filterQueryLang) $$.filterQueryLang=$2.filterQueryLang;
    }
;

openEnumInstancesParms
    : XTOK_IP_CLASSNAME className ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.className = $2;
    }
    | XTOK_IP_LOCALONLY boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_localOnly : 0 ;
       $$.flagsSet = FL_localOnly;
    }
    | XTOK_IP_LOCALONLY ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_INCLUDEQUALIFIERS boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_includeQualifiers : 0 ;
       $$.flagsSet = FL_includeQualifiers;
    }
    | XTOK_IP_INCLUDEQUALIFIERS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_DEEPINHERITANCE boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_deepInheritance : 0 ;
       $$.flagsSet = FL_deepInheritance;
    }
    | XTOK_IP_DEEPINHERITANCE ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_INCLUDECLASSORIGIN boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_includeClassOrigin : 0 ;
       $$.flagsSet = FL_includeClassOrigin;
    }
    | XTOK_IP_INCLUDECLASSORIGIN ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_PROPERTYLIST valueArray ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.propertyList=$2;
       $$.properties=$2.next;
    }
    | XTOK_IP_PROPERTYLIST ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_CONTINUEONERROR boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_continueOnError : 0 ;
       $$.flagsSet = FL_continueOnError;
    }
    | XTOK_IP_CONTINUEONERROR ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_OPERATIONTIMEOUT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.operationTimeout=atoi($2.value);
    }
    | XTOK_IP_OPERATIONTIMEOUT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_MAXOBJECTCOUNT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.maxObjectCount=atoi($2.value);
    }
    | XTOK_IP_MAXOBJECTCOUNT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_FILTERQUERY value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.filterQuery=$2.value;
    }
    | XTOK_IP_FILTERQUERY ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_FILTERQUERYLANG value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.filterQueryLang=$2.value;
    }
    | XTOK_IP_FILTERQUERYLANG ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
;


/*
 *    openAssociatorInstancePaths
*/

openAssociatorInstancePaths
    : localNameSpacePath
    {
       $$.op.count = AIN_REQ_REG_SEGMENTS;
       $$.op.type = OPS_OpenAssociatorInstancePaths;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.op.assocClass=setCharsMsgSegment(NULL);
       $$.op.resultClass=setCharsMsgSegment(NULL);
       $$.op.role=setCharsMsgSegment(NULL);
       $$.op.resultRole=setCharsMsgSegment(NULL);
       $$.objNameSet = 0;
       // TODO what initialization needs to be done here?
       //$$.flags = FL_localOnly | FL_deepInheritance;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildAssociatorNamesRequest(parm);  // TODO
    }
    | localNameSpacePath openAssociatorInstancePathsParmsList
    {
       $$.op.count = AIN_REQ_REG_SEGMENTS;
       $$.op.type = OPS_OpenAssociatorInstancePaths;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.objectName.className);
       $$.op.assocClass=setCharsMsgSegment($2.assocClass);
       $$.op.resultClass=setCharsMsgSegment($2.resultClass);
       $$.op.role=setCharsMsgSegment($2.role);
       $$.op.resultRole=setCharsMsgSegment($2.resultRole);
       $$.objectName = $2.objectName;
       $$.objNameSet = $2.objNameSet;
       $$.flags = ($2.flags & $2.flagsSet) | ((~$2.flagsSet) & (FL_localOnly));
       $$.operationTimeout=$2.operationTimeout;
       $$.maxObjectCount=$2.maxObjectCount;
       $$.filterQuery=$2.filterQuery;
       $$.filterQueryLang=$2.filterQueryLang;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildAssociatorNamesRequest(parm);  // TODO
    }
;

openAssociatorInstancePathsParmsList
    : openAssociatorInstancePathsParms
    {
       if ($1.objNameSet)  {
          $$.objectName=$1.objectName;
          $$.objNameSet=$1.objNameSet;
       }
       $$.assocClass=$1.assocClass;
       $$.resultClass=$1.resultClass;
       $$.role=$1.role;
       $$.resultRole=$1.resultRole;
       $$.flags=$1.flags;
       $$.flagsSet=$1.flagsSet;
       if ($1.operationTimeout) $$.operationTimeout=$1.operationTimeout;
       if ($1.maxObjectCount) $$.maxObjectCount=$1.maxObjectCount;
       if ($1.filterQuery) $$.filterQuery=$1.filterQuery;
       if ($1.filterQueryLang) $$.filterQueryLang=$1.filterQueryLang;
    }
    | openAssociatorInstancePathsParmsList openAssociatorInstancePathsParms
    {
       if ($2.assocClass) $$.assocClass=$2.assocClass;
       else if ($2.resultClass) $$.resultClass=$2.resultClass;
       else if ($2.role) $$.role=$2.role;
       else if ($2.resultRole) $$.resultRole=$2.resultRole;
       else if ($2.objNameSet) {
          $$.objectName=$2.objectName;
          $$.objNameSet=$2.objNameSet;
       }
       $$.flags=$1.flags|$2.flags;
       $$.flagsSet=$1.flagsSet|$2.flagsSet;
       if ($2.operationTimeout) $$.operationTimeout=$2.operationTimeout;
       if ($2.maxObjectCount) $$.maxObjectCount=$2.maxObjectCount;
       if ($2.filterQuery) $$.filterQuery=$2.filterQuery;
       if ($2.filterQueryLang) $$.filterQueryLang=$2.filterQueryLang;
    }
;

openAssociatorInstancePathsParms
    : XTOK_IP_INSTANCENAME instanceName ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.objectName = $2;
       $$.objNameSet = 1;
    }
    | XTOK_IP_ASSOCCLASS className ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.assocClass = $2;
    }
    | XTOK_IP_ASSOCCLASS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_RESULTCLASS className ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.resultClass = $2;
    }
    | XTOK_IP_RESULTCLASS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_ROLE value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.role = $2.value;
    }
    | XTOK_IP_ROLE ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_RESULTROLE value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.resultRole = $2.value;
    }
    | XTOK_IP_RESULTROLE ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_CONTINUEONERROR boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_continueOnError : 0 ;
       $$.flagsSet = FL_continueOnError;
    }
    | XTOK_IP_CONTINUEONERROR ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_OPERATIONTIMEOUT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.operationTimeout=atoi($2.value);
    }
    | XTOK_IP_OPERATIONTIMEOUT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_MAXOBJECTCOUNT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.maxObjectCount=atoi($2.value);
    }
    | XTOK_IP_MAXOBJECTCOUNT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_FILTERQUERY value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.filterQuery=$2.value;
    }
    | XTOK_IP_FILTERQUERY ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_FILTERQUERYLANG value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.filterQueryLang=$2.value;
    }
    | XTOK_IP_FILTERQUERYLANG ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
;


/*
 *    openAssociatorInstances
*/

openAssociatorInstances
    : localNameSpacePath
    {
       $$.op.count = AI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_OpenAssociatorInstances;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.op.assocClass=setCharsMsgSegment(NULL);
       $$.op.resultClass=setCharsMsgSegment(NULL);
       $$.op.role=setCharsMsgSegment(NULL);
       $$.op.resultRole=setCharsMsgSegment(NULL);
       $$.flags = 0;
       $$.objNameSet = 0;
       $$.propertyList.values = 0;
       $$.properties=0;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildAssociatorsRequest(parm);  // TODO
    }
    | localNameSpacePath openAssociatorInstancesParmsList
    {
       $$.op.count = AI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_OpenAssociatorInstances;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.objectName.className);
       $$.op.assocClass=setCharsMsgSegment($2.assocClass);
       $$.op.resultClass=setCharsMsgSegment($2.resultClass);
       $$.op.role=setCharsMsgSegment($2.role);
       $$.op.resultRole=setCharsMsgSegment($2.resultRole);
       $$.flags = ($2.flags & $2.flagsSet) | (~$2.flagsSet & 0);
       $$.objectName = $2.objectName;
       $$.objNameSet = $2.objNameSet;
       $$.propertyList = $2.propertyList;
       $$.properties=$2.properties;
       $$.operationTimeout=$2.operationTimeout;
       $$.maxObjectCount=$2.maxObjectCount;
       $$.filterQuery=$2.filterQuery;
       $$.filterQueryLang=$2.filterQueryLang;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildAssociatorsRequest(parm);  // TODO
    }
;

openAssociatorInstancesParmsList
    : openAssociatorInstancesParms
    {
       $$.flags=$1.flags;
       $$.flagsSet=$1.flagsSet;
       if ($1.objNameSet)  {
          $$.objectName=$1.objectName;
          $$.objNameSet = $1.objNameSet;
       }
       $$.assocClass=$1.assocClass;
       $$.resultClass=$1.resultClass;
       $$.role=$1.role;
       $$.resultRole=$1.resultRole;
       if ($1.propertyList.values) {
          $$.propertyList=$1.propertyList;
          $$.properties=$1.properties;
       }
       if ($1.operationTimeout) $$.operationTimeout=$1.operationTimeout;
       if ($1.maxObjectCount) $$.maxObjectCount=$1.maxObjectCount;
       if ($1.filterQuery) $$.filterQuery=$1.filterQuery;
       if ($1.filterQueryLang) $$.filterQueryLang=$1.filterQueryLang;
    }
    | openAssociatorInstancesParmsList openAssociatorInstancesParms
    {
       $$.flags=$1.flags|$2.flags;
       $$.flagsSet=$1.flagsSet|$2.flagsSet;
       if ($2.assocClass) $$.assocClass=$2.assocClass;
       else if ($2.resultClass) $$.resultClass=$2.resultClass;
       else if ($2.role) $$.role=$2.role;
       else if ($2.resultRole) $$.resultRole=$2.resultRole;
       else if ($2.objNameSet) {
          $$.objectName=$2.objectName;
          $$.objNameSet=$2.objNameSet;
       }
       else if ($2.propertyList.values) {
          $$.propertyList=$2.propertyList;
          $$.properties=$2.properties;
       }
       if ($2.operationTimeout) $$.operationTimeout=$2.operationTimeout;
       if ($2.maxObjectCount) $$.maxObjectCount=$2.maxObjectCount;
       if ($2.filterQuery) $$.filterQuery=$2.filterQuery;
       if ($2.filterQueryLang) $$.filterQueryLang=$2.filterQueryLang;
    }
;

openAssociatorInstancesParms
    : XTOK_IP_INSTANCENAME instanceName ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.objectName = $2;
       $$.objNameSet = 1;
    }
    | XTOK_IP_ASSOCCLASS className ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.assocClass = $2;
    }
    | XTOK_IP_ASSOCCLASS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_RESULTCLASS className ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.resultClass = $2;
    }
    | XTOK_IP_RESULTCLASS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_ROLE value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.role = $2.value;
    }
    | XTOK_IP_ROLE ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_RESULTROLE value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.resultRole = $2.value;
    }
    | XTOK_IP_RESULTROLE ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_INCLUDEQUALIFIERS boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_includeQualifiers : 0 ;
       $$.flagsSet = FL_includeQualifiers;
    }
    | XTOK_IP_INCLUDEQUALIFIERS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_INCLUDECLASSORIGIN boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_includeClassOrigin : 0 ;
       $$.flagsSet = FL_includeClassOrigin;
    }
    | XTOK_IP_INCLUDECLASSORIGIN ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_PROPERTYLIST valueArray ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.propertyList=$2;
       $$.properties=$2.next;
    }
    | XTOK_IP_PROPERTYLIST ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_CONTINUEONERROR boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_continueOnError : 0 ;
       $$.flagsSet = FL_continueOnError;
    }
    | XTOK_IP_CONTINUEONERROR ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_OPERATIONTIMEOUT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.operationTimeout=atoi($2.value);
    }
    | XTOK_IP_OPERATIONTIMEOUT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_MAXOBJECTCOUNT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.maxObjectCount=atoi($2.value);
    }
    | XTOK_IP_MAXOBJECTCOUNT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_FILTERQUERY value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.filterQuery=$2.value;
    }
    | XTOK_IP_FILTERQUERY ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_FILTERQUERYLANG value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.filterQueryLang=$2.value;
    }
    | XTOK_IP_FILTERQUERYLANG ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
;


/*
 *    openReferenceInstancePaths
*/

openReferenceInstancePaths
    : localNameSpacePath
    {
       $$.op.count = RIN_REQ_REG_SEGMENTS;
       $$.op.type = OPS_OpenReferenceInstancePaths;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.op.resultClass=setCharsMsgSegment(NULL);
       $$.op.role=setCharsMsgSegment(NULL);
       $$.objNameSet=0;
       // TODO what initialization needs to be done here?
       //$$.flags = FL_localOnly | FL_deepInheritance;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildReferenceNamesRequest(parm);  // TODO
    }
    | localNameSpacePath openReferenceInstancePathsParmsList
    {
       $$.op.count = RIN_REQ_REG_SEGMENTS;
       $$.op.type = OPS_OpenReferenceInstancePaths;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.objectName.className);
       $$.op.resultClass=setCharsMsgSegment($2.resultClass);
       $$.op.role=setCharsMsgSegment($2.role);
       $$.objectName=$2.objectName;
       $$.objNameSet=$2.objNameSet;
       $$.flags = ($2.flags & $2.flagsSet) | ((~$2.flagsSet) & (FL_localOnly));
       $$.operationTimeout=$2.operationTimeout;
       $$.maxObjectCount=$2.maxObjectCount;
       $$.filterQuery=$2.filterQuery;
       $$.filterQueryLang=$2.filterQueryLang;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildReferenceNamesRequest(parm);  // TODO
    }
;

openReferenceInstancePathsParmsList
    : openReferenceInstancePathsParms
    {
       if ($1.objNameSet)  {
          $$.objectName=$1.objectName;
          $$.objNameSet=$1.objNameSet;
       }
       $$.resultClass=$1.resultClass;
       $$.role=$1.role;
       $$.flags=$1.flags;
       $$.flagsSet=$1.flagsSet;
       if ($1.operationTimeout) $$.operationTimeout=$1.operationTimeout;
       if ($1.maxObjectCount) $$.maxObjectCount=$1.maxObjectCount;
       if ($1.filterQuery) $$.filterQuery=$1.filterQuery;
       if ($1.filterQueryLang) $$.filterQueryLang=$1.filterQueryLang;
    }
    | openReferenceInstancePathsParmsList openReferenceInstancePathsParms
    {
       if($2.objNameSet) {
          $$.objectName=$2.objectName;
          $$.objNameSet=$2.objNameSet;
       }
       else if($2.resultClass) $$.resultClass=$2.resultClass;
       else if($2.role) $$.role=$2.role;
       $$.flags=$1.flags|$2.flags;
       $$.flagsSet=$1.flagsSet|$2.flagsSet;
       if ($2.operationTimeout) $$.operationTimeout=$2.operationTimeout;
       if ($2.maxObjectCount) $$.maxObjectCount=$2.maxObjectCount;
       if ($2.filterQuery) $$.filterQuery=$2.filterQuery;
       if ($2.filterQueryLang) $$.filterQueryLang=$2.filterQueryLang;
    }
;

openReferenceInstancePathsParms
    : XTOK_IP_INSTANCENAME instanceName ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.objectName = $2;
       $$.objNameSet = 1;
    }
    | XTOK_IP_RESULTCLASS className ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.resultClass = $2;
    }
    | XTOK_IP_RESULTCLASS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_ROLE value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.role = $2.value;
    }
    | XTOK_IP_ROLE ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_CONTINUEONERROR boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_continueOnError : 0 ;
       $$.flagsSet = FL_continueOnError;
    }
    | XTOK_IP_CONTINUEONERROR ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_OPERATIONTIMEOUT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.operationTimeout=atoi($2.value);
    }
    | XTOK_IP_OPERATIONTIMEOUT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_MAXOBJECTCOUNT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.maxObjectCount=atoi($2.value);
    }
    | XTOK_IP_MAXOBJECTCOUNT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_FILTERQUERY value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.filterQuery=$2.value;
    }
    | XTOK_IP_FILTERQUERY ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_FILTERQUERYLANG value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.filterQueryLang=$2.value;
    }
    | XTOK_IP_FILTERQUERYLANG ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
;


/*
 *    openReferenceInstances
*/

openReferenceInstances
    : localNameSpacePath
    {
       $$.op.count = RI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_OpenReferenceInstances;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.op.resultClass=setCharsMsgSegment(NULL);
       $$.op.role=setCharsMsgSegment(NULL);
       $$.flags = 0;
       $$.objNameSet = 0;
       $$.propertyList.values = 0;
       $$.properties=0;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildReferencesRequest(parm);  // TODO
    }
    | localNameSpacePath openReferenceInstancesParmsList
    {
       $$.op.count = RI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_OpenReferenceInstances;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.objectName.className);
       $$.op.resultClass=setCharsMsgSegment($2.resultClass);
       $$.op.role=setCharsMsgSegment($2.role);
       $$.flags = ($2.flags & $2.flagsSet) | (~$2.flagsSet & 0);
       $$.objectName = $2.objectName;
       $$.objNameSet = $2.objNameSet;
       $$.propertyList = $2.propertyList;
       $$.properties=$2.properties;
       $$.operationTimeout=$2.operationTimeout;
       $$.maxObjectCount=$2.maxObjectCount;
       $$.filterQuery=$2.filterQuery;
       $$.filterQueryLang=$2.filterQueryLang;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildReferencesRequest(parm);  // TODO
    }
;

openReferenceInstancesParmsList
    : openReferenceInstancesParms
    {
       $$.flags=$1.flags;
       $$.flagsSet=$1.flagsSet;
       if ($1.objNameSet)  {
          $$.objectName=$1.objectName;
          $$.objNameSet = $1.objNameSet;
       }
       $$.resultClass=$1.resultClass;
       $$.role=$1.role;
       if ($1.propertyList.values) {
          $$.propertyList=$1.propertyList;
          $$.properties=$1.properties;
       }
       if ($1.operationTimeout) $$.operationTimeout=$1.operationTimeout;
       if ($1.maxObjectCount) $$.maxObjectCount=$1.maxObjectCount;
       if ($1.filterQuery) $$.filterQuery=$1.filterQuery;
       if ($1.filterQueryLang) $$.filterQueryLang=$1.filterQueryLang;
    }
    | openReferenceInstancesParmsList openReferenceInstancesParms
    {
       $$.flags=$1.flags|$2.flags;
       $$.flagsSet=$1.flagsSet|$2.flagsSet;
       if ($2.resultClass) $$.resultClass=$2.resultClass;
       else if ($2.role) $$.role=$2.role;
       else if ($2.objNameSet) {
          $$.objectName=$2.objectName;
          $$.objNameSet=$2.objNameSet;
       }
       else if ($2.propertyList.values) {
          $$.propertyList=$2.propertyList;
          $$.properties=$2.properties;
       }
       if ($2.operationTimeout) $$.operationTimeout=$2.operationTimeout;
       if ($2.maxObjectCount) $$.maxObjectCount=$2.maxObjectCount;
       if ($2.filterQuery) $$.filterQuery=$2.filterQuery;
       if ($2.filterQueryLang) $$.filterQueryLang=$2.filterQueryLang;
   }
;

openReferenceInstancesParms
    : XTOK_IP_INSTANCENAME instanceName ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.objectName = $2;
       $$.objNameSet = 1;
    }
    | XTOK_IP_RESULTCLASS className ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.resultClass = $2;
    }
    | XTOK_IP_RESULTCLASS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_ROLE value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.role = $2.value;
    }
    | XTOK_IP_ROLE ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_INCLUDEQUALIFIERS boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_includeQualifiers : 0 ;
       $$.flagsSet = FL_includeQualifiers;
    }
    | XTOK_IP_INCLUDEQUALIFIERS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_INCLUDECLASSORIGIN boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_includeClassOrigin : 0 ;
       $$.flagsSet = FL_includeClassOrigin;
    }
    | XTOK_IP_INCLUDECLASSORIGIN ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_PROPERTYLIST valueArray ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.propertyList=$2;
       $$.properties=$2.next;
    }
    | XTOK_IP_PROPERTYLIST ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_CONTINUEONERROR boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_continueOnError : 0 ;
       $$.flagsSet = FL_continueOnError;
    }
    | XTOK_IP_CONTINUEONERROR ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_OPERATIONTIMEOUT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.operationTimeout=atoi($2.value);
    }
    | XTOK_IP_OPERATIONTIMEOUT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_MAXOBJECTCOUNT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.maxObjectCount=atoi($2.value);
    }
    | XTOK_IP_MAXOBJECTCOUNT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_FILTERQUERY value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.filterQuery=$2.value;
    }
    | XTOK_IP_FILTERQUERY ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_FILTERQUERYLANG value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.filterQueryLang=$2.value;
    }
    | XTOK_IP_FILTERQUERYLANG ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
;


/*
 *    openQueryInstances
*/

openQueryInstances
    : localNameSpacePath
    {
       $$.op.count = EQ_REQ_REG_SEGMENTS;
       $$.op.type = OPS_OpenQueryInstances;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildExecQueryRequest(parm);  // TODO
    }
    | localNameSpacePath openQueryInstancesParmsList
    {
       $$.op.count = EQ_REQ_REG_SEGMENTS;
       $$.op.type = OPS_OpenQueryInstances;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.flags = ($2.flags & $2.flagsSet) | ((~$2.flagsSet) & (FL_localOnly));
       $$.operationTimeout=$2.operationTimeout;
       $$.maxObjectCount=$2.maxObjectCount;
       $$.filterQuery=$2.filterQuery;
       $$.filterQueryLang=$2.filterQueryLang;
       $$.op.query=setCharsMsgSegment($2.filterQuery);
       $$.op.queryLang=setCharsMsgSegment($2.filterQueryLang);

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildExecQueryRequest(parm);  // TODO
    }
;

openQueryInstancesParmsList
    : openQueryInstancesParms
    {
       $$.flags=$1.flags;
       $$.flagsSet=$1.flagsSet;
       if ($1.operationTimeout) $$.operationTimeout=$1.operationTimeout;
       if ($1.maxObjectCount) $$.maxObjectCount=$1.maxObjectCount;
       if ($1.filterQuery) $$.filterQuery=$1.filterQuery;
       if ($1.filterQueryLang) $$.filterQueryLang=$1.filterQueryLang;
    }
    | openQueryInstancesParmsList openQueryInstancesParms
    {
       $$.flags=$1.flags|$2.flags;
       $$.flagsSet=$1.flagsSet|$2.flagsSet;
       if ($2.operationTimeout) $$.operationTimeout=$2.operationTimeout;
       if ($2.maxObjectCount) $$.maxObjectCount=$2.maxObjectCount;
       if ($2.filterQuery) $$.filterQuery=$2.filterQuery;
       if ($2.filterQueryLang) $$.filterQueryLang=$2.filterQueryLang;
    }
;

openQueryInstancesParms
    : XTOK_IP_RETURNQUERYRESULTCLASS boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_returnQueryResultClass : 0 ;
       $$.flagsSet = FL_returnQueryResultClass;
    }
    | XTOK_IP_RETURNQUERYRESULTCLASS ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_CONTINUEONERROR boolValue ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.flags = $2 ? FL_continueOnError : 0 ;
       $$.flagsSet = FL_continueOnError;
    }
    | XTOK_IP_CONTINUEONERROR ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_OPERATIONTIMEOUT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.operationTimeout=atoi($2.value);
    }
    | XTOK_IP_OPERATIONTIMEOUT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_MAXOBJECTCOUNT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.maxObjectCount=atoi($2.value);
    }
    | XTOK_IP_MAXOBJECTCOUNT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_FILTERQUERY value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.filterQuery=$2.value;
    }
    | XTOK_IP_FILTERQUERY ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_FILTERQUERYLANG value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.filterQueryLang=$2.value;
    }
    | XTOK_IP_FILTERQUERYLANG ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
;

/*
 *    pullInstances
*/

pullInstances
    : localNameSpacePath
    {
       $$.op.count = EI_REQ_REG_SEGMENTS;  // TODO
       $$.op.type = OPS_PullInstances;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildPullInstancesRequest(parm);
    }
    | localNameSpacePath pullInstancesParmsList
    {
       $$.op.count = EI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_PullInstances;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.maxObjectCount=$2.maxObjectCount;
       $$.enumerationContext=$2.enumerationContext;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildPullInstancesRequest(parm);
    }
;

pullInstancesParmsList
    : pullInstancesParms
    {
       if ($1.maxObjectCount) $$.maxObjectCount=$1.maxObjectCount;
       if ($1.enumerationContext) $$.enumerationContext=$1.enumerationContext;
    }
    | pullInstancesParmsList pullInstancesParms
    {
       if ($2.maxObjectCount) $$.maxObjectCount=$2.maxObjectCount;
       if ($2.enumerationContext) $$.enumerationContext=$2.enumerationContext;
    }
;

pullInstancesParms
    : XTOK_IP_MAXOBJECTCOUNT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.maxObjectCount=atoi($2.value);
    }
    | XTOK_IP_MAXOBJECTCOUNT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_ENUMERATIONCONTEXT enumerationContext ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.enumerationContext=$2;
    }
    | XTOK_IP_ENUMERATIONCONTEXT ZTOK_IPARAMVALUE // TODO empty value; should this be allowed?
    {
       memset(&$$, 0, sizeof($$));
    }
;


/*
 *    pullInstancesWithPath
*/

pullInstancesWithPath
    : localNameSpacePath
    {
       $$.op.count = EI_REQ_REG_SEGMENTS;  // TODO
       $$.op.type = OPS_PullInstancesWithPath;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);

       setRequest(parm,&$$,sizeof($$),$$.op.type);
//     buildPullInstancePathsRequest(parm);
       buildPullInstancesRequest(parm); // TODO  Or maybe this is acceptable...
    }
    | localNameSpacePath pullInstancesWithPathParmsList
    {
       $$.op.count = EI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_PullInstancesWithPath;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.maxObjectCount=$2.maxObjectCount;
       $$.enumerationContext=$2.enumerationContext;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
//     buildPullInstancesWithPathRequest(parm);
       buildPullInstancesRequest(parm);
    }
;

pullInstancesWithPathParmsList
    : pullInstancesWithPathParms
    {
       if ($1.maxObjectCount) $$.maxObjectCount=$1.maxObjectCount;
       if ($1.enumerationContext) $$.enumerationContext=$1.enumerationContext;
    }
    | pullInstancesWithPathParmsList pullInstancesWithPathParms
    {
       if ($2.maxObjectCount) $$.maxObjectCount=$2.maxObjectCount;
       if ($2.enumerationContext) $$.enumerationContext=$2.enumerationContext;
    }
;

pullInstancesWithPathParms
    : XTOK_IP_MAXOBJECTCOUNT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.maxObjectCount=atoi($2.value);
    }
    | XTOK_IP_MAXOBJECTCOUNT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_ENUMERATIONCONTEXT enumerationContext ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.enumerationContext=$2;
    }
    | XTOK_IP_ENUMERATIONCONTEXT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
;


/*
 *    pullInstancePaths
*/

pullInstancePaths
    : localNameSpacePath
    {
       $$.op.count = EI_REQ_REG_SEGMENTS;  // TODO
       $$.op.type = OPS_PullInstances;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);

       setRequest(parm,&$$,sizeof($$),$$.op.type);
//     buildPullInstancePathsRequest(parm);
       buildPullInstancesRequest(parm); // TODO  Or maybe this is acceptable...
    }
    | localNameSpacePath pullInstancePathsParmsList
    {
       $$.op.count = EI_REQ_REG_SEGMENTS;
       $$.op.type = OPS_PullInstances;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.maxObjectCount=$2.maxObjectCount;
       $$.enumerationContext=$2.enumerationContext;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
//     buildPullInstancePathsRequest(parm);
       buildPullInstancesRequest(parm);
    }
;

pullInstancePathsParmsList
    : pullInstancePathsParms
    {
       if ($1.maxObjectCount) $$.maxObjectCount=$1.maxObjectCount;
       if ($1.enumerationContext) $$.enumerationContext=$1.enumerationContext;
    }
    | pullInstancePathsParmsList pullInstancePathsParms
    {
       if ($2.maxObjectCount) $$.maxObjectCount=$2.maxObjectCount;
       if ($2.enumerationContext) $$.enumerationContext=$2.enumerationContext;
    }
;

pullInstancePathsParms
    : XTOK_IP_MAXOBJECTCOUNT value ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.maxObjectCount=atoi($2.value);
    }
    | XTOK_IP_MAXOBJECTCOUNT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
    | XTOK_IP_ENUMERATIONCONTEXT enumerationContext ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
       $$.enumerationContext=$2;
    }
    | XTOK_IP_ENUMERATIONCONTEXT ZTOK_IPARAMVALUE
    {
       memset(&$$, 0, sizeof($$));
    }
;

/*
 *    closeEnumeration
*/

closeEnumeration
    : localNameSpacePath XTOK_IP_ENUMERATIONCONTEXT enumerationContext ZTOK_IPARAMVALUE
    {
       $$.op.count = EI_REQ_REG_SEGMENTS;  // TODO
       $$.op.type = OPS_CloseEnumeration;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.enumerationContext=$3;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildCloseEnumerationRequest(parm);
    }
;

/*
 *    enumerationCount
*/

enumerationCount
    : localNameSpacePath XTOK_IP_ENUMERATIONCONTEXT enumerationContext ZTOK_IPARAMVALUE
    {
       $$.op.count = EI_REQ_REG_SEGMENTS;  // TODO
       $$.op.type = OPS_EnumerationCount;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.enumerationContext=$3;

       setRequest(parm,&$$,sizeof($$),$$.op.type);
       buildEnumerationCountRequest(parm);
    }
;


enumerationContext
    : XTOK_VALUE ZTOK_VALUE
    {
    $$=atol($1.value); // TODO need some checking here
    }
;

className
    : XTOK_CLASSNAME ZTOK_CLASSNAME
    {
    }
;


/*
 *    instanceName
*/


instanceName
    : XTOK_INSTANCENAME ZTOK_INSTANCENAME
    {
       $$.className=$1.className;
       $$.bindings.next=0;
       $$.bindings.keyBindings=NULL;
    }
    | XTOK_INSTANCENAME keyBindings ZTOK_INSTANCENAME
    {
       $$.className=$1.className;
       $$.bindings=$2;
    }
;

keyBindings
    : keyBinding
    {
       $$.next=1;
       $$.max=KEYBINDING_MAX_START;
       $$.keyBindings=calloc(($$.max),sizeof(XtokKeyBinding));
       $$.keyBindings[0].name=$1.name;
       $$.keyBindings[0].value=$1.value;
       $$.keyBindings[0].type=$1.type;
       $$.keyBindings[0].ref=$1.ref;
    }
    | keyBindings keyBinding
    {
       if ($$.next == $$.max) { /* max was hit; let's bump it up 50% */
         $$.max = (int)($$.max * ((float)3)/2);
         $$.keyBindings=realloc(($$.keyBindings), sizeof(XtokKeyBinding)*($$.max));
       }
       $$.keyBindings[$$.next].name=$2.name;
       $$.keyBindings[$$.next].value=$2.value;
       $$.keyBindings[$$.next].type=$2.type;
       $$.keyBindings[$$.next].ref=$2.ref;
       $$.next++;
    }
;

keyBinding
    : XTOK_KEYBINDING XTOK_KEYVALUE ZTOK_KEYVALUE ZTOK_KEYBINDING
    {
       $$.name=$1.name;
       $$.value=$2.value;
       $$.type=$2.valueType;
    }
    | XTOK_KEYBINDING valueReference ZTOK_KEYBINDING
    {
       $$.name=$1.name;
       $$.value=NULL;
       $$.type="ref";
       $$.ref=$2;
    }
;

%%
