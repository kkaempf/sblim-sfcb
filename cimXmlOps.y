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
#include "cimXmlGen.h"
#include "cimXmlParser.h"
#include "objectImpl.h"
#include "constClass.h"
#include "native.h"

extern CMPIConstClass initConstClass(ClClass * cl);
extern MsgSegment setConstClassMsgSegment(CMPIConstClass * cl);
extern MsgSegment setInstanceMsgSegment(CMPIInstance *ci);

//
// Define the global parser state object:
//

#define YYPARSE_PARAM parm
#define YYLEX_PARAM parm
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



extern int yyerror(char*);
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
  sreq = calloc(1, sizeof(*sreq)); //right size MCS
  sreq->hdr.operation = OPS_AssociatorNames;
  sreq->hdr.count = 2; //MCS

  sreq->objectPath = setObjectPathMsgSegment(path);

  sreq->resultClass = req->op.resultClass;
  sreq->role = req->op.role;
  sreq->assocClass = req->op.assocClass;
  sreq->resultRole = req->op.resultRole;
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->hdr.sessionId = hdr->sessionId;

  req->op.className = req->op.assocClass;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = req->flags;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sizeof(*sreq); //MCS
  //binCtx->commHndl = ctx->commHndl;
  binCtx->type = CMPI_ref;
  binCtx->xmlAs = XML_asObjectPath;
  binCtx->noResp = 0;
  //binCtx->chunkedMode = 0;
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
  sreq->hdr.count = req->properties + 6;

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
  sreq->hdr.count = req->properties + 2;

  sreq->principal = setCharsMsgSegment(hdr->principal);
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

  memset(binCtx, 0, sizeof(BinRequestContext));
  XtokCreateInstance *req = (XtokCreateInstance *) hdr->cimRequest;
  hdr->className = req->op.className.data;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  inst = TrackedCMPIInstance(path, NULL);

  sreq = calloc(1, sizeof(CreateInstanceReq));
  sreq->hdr.operation = OPS_CreateInstance;
  sreq->hdr.count = 3;


  for (p = req->instance.properties.first; p; p = p->next) {
    if (p->val.val.value) {
      val =
          str2CMPIValue(p->valueType, p->val.val, &p->val.ref,
                        req->op.nameSpace.data);
      CMSetProperty(inst, p->name, &val, p->valueType);
    }
  }

  sreq->instance = setInstanceMsgSegment(inst);
  sreq->principal = setCharsMsgSegment(hdr->principal);
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
  sreq->principal = setCharsMsgSegment(hdr->principal);
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
  sreq->hdr.count = req->properties + 2;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(hdr->principal);
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
  sreq->hdr.count = 2;

  path =
      TrackedCMPIObjectPath(req->op.nameSpace.data, req->op.className.data,
                            NULL);
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(hdr->principal);
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
  sreq->hdr.count = 2;

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
    ClParameter    *cl_parm;
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
          d.value = str2CMPIValue(q->type, q->value, NULL, NULL);
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
  sreq->hdr.count = 3;
  sreq->principal = setCharsMsgSegment(hdr->principal);
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

static void
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

  memset(binCtx, 0, sizeof(BinRequestContext));
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
  sreq->principal = setCharsMsgSegment(hdr->principal);
  sreq->hdr.sessionId = hdr->sessionId;

  binCtx->oHdr = (OperationHdr *) req;
  binCtx->bHdr = &sreq->hdr;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sreqSize;
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;
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
  sreq->hdr.count = 2;
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(hdr->principal);
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
  sreq->hdr.operation = OPS_EnumerateClassNames;
  sreq->hdr.count = 2;
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
  sreq->hdr.operation = OPS_EnumerateInstanceNames;
  sreq->hdr.count = 2;
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
                  (char *) req->op.queryLang.data, NULL, &irc);

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
  sreq->hdr.count = 4;
  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(hdr->principal);
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
    hdr->rc = CMPI_RC_ERR_NOT_SUPPORTED;
    hdr->errMsg = "References operation for classes not supported";
    return;
  }
  if (!req->objNameSet) {
    free(sreq);
    hdr->rc = CMPI_RC_ERR_INVALID_PARAMETER;
    hdr->errMsg = "ObjectName parameter required";
    return;
  }

  sreq->objectPath = setObjectPathMsgSegment(path);

  sreq->resultClass = req->op.resultClass;
  sreq->role = req->op.role;
  sreq->hdr.flags = req->flags;
  sreq->principal = setCharsMsgSegment(hdr->principal);
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
    hdr->errMsg = "ReferenceNames operation for classes not supported";
    return;
  }
  if (!req->objNameSet) {
    hdr->rc = CMPI_RC_ERR_INVALID_PARAMETER;
    hdr->errMsg = "ObjectName parameter required";
    return;
  }

  sreq = calloc(1, sizeof(*sreq));
  sreq->hdr.operation = OPS_ReferenceNames;
  sreq->hdr.count = 4;
  sreq->objectPath = setObjectPathMsgSegment(path);

  sreq->resultClass = req->op.resultClass;
  sreq->role = req->op.role;
  sreq->principal = setCharsMsgSegment(hdr->principal);
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
buildSetPropertyRequest(void *parm)
{
  CMPIObjectPath *path;
  CMPIInstance   *inst;
  CMPIType        t;
  CMPIStatus      rc;
  CMPIValue       val;
  SetPropertyReq *sreq;// = BINREQ(OPS_SetProperty, 3);
  RequestHdr     *hdr = &(((ParserControl *)parm)->reqHdr);
  BinRequestContext *binCtx = hdr->binCtx;

  memset(binCtx, 0, sizeof(BinRequestContext));
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

static void addProperty(XtokProperties *ps, XtokProperty *p)
{
   XtokProperty *np;
   np=(XtokProperty*)malloc(sizeof(XtokProperty));
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
   nv=(XtokParamValue*)malloc(sizeof(XtokParamValue));
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
   nq=(XtokQualifier*)malloc(sizeof(XtokQualifier));
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
   nm=(XtokMethod*)malloc(sizeof(XtokMethod));
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
   np=(XtokParam*)malloc(sizeof(XtokParam));
   memcpy(np,p,sizeof(XtokParam));
   np->next=NULL;
   if (ps->last) {
      ps->last->next=np;
   }
   else ps->first=np;
   ps->last=np;
}

%}

%pure_parser

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
    
;

/*
 *    methodCall
*/

methodCall
    : localClassPath 
    {
       $$.op.count = 2;
       $$.op.type = OPS_InvokeMethod;
       $$.op.nameSpace=setCharsMsgSegment($1.path);
       $$.op.className=setCharsMsgSegment($1.className);
       $$.instName=0;
       $$.paramValues.first=NULL;
       $$.paramValues.last=NULL;
       
       setRequest(parm,&$$,sizeof(XtokMethodCall),OPS_InvokeMethod);
    }   
    | localClassPath paramValues
    {
       $$.op.count = 2;
       $$.op.type = OPS_InvokeMethod;
       $$.op.nameSpace=setCharsMsgSegment($1.path);
       $$.op.className=setCharsMsgSegment($1.className);
       $$.instName=0;
       $$.paramValues=$2;
       
       setRequest(parm,&$$,sizeof(XtokMethodCall),OPS_InvokeMethod);
    }   
    | localInstancePath 
    {
       $$.op.count = 2;
       $$.op.type = OPS_InvokeMethod;
       $$.op.nameSpace=setCharsMsgSegment($1.path);
       $$.op.className=setCharsMsgSegment($1.instanceName.className);
       $$.instanceName=$1.instanceName;
       $$.instName=1;
       $$.paramValues.first=NULL;
       $$.paramValues.last=NULL;
       
       setRequest(parm,&$$,sizeof(XtokMethodCall),OPS_InvokeMethod);
    }   
    | localInstancePath paramValues
    {
       $$.op.count = 2;
       $$.op.type = OPS_InvokeMethod;
       $$.op.nameSpace=setCharsMsgSegment($1.path);
       $$.op.className=setCharsMsgSegment($1.instanceName.className);
       $$.instanceName=$1.instanceName;
       $$.instName=1;
       $$.paramValues=$2;
              
       setRequest(parm,&$$,sizeof(XtokMethodCall),OPS_InvokeMethod);
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
       $$.type=-1;
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
       $$.op.count = 2;
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
       $$.op.count = 2;
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
       $$.op.count = 2;
       $$.op.type = OPS_EnumerateClassNames;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.flags = 0;

       setRequest(parm,&$$,sizeof(XtokEnumClassNames),OPS_EnumerateClassNames);
       buildEnumClassNamesRequest(parm);
    }
    | localNameSpacePath enumClassNamesParmsList
    {
       $$.op.count = 2;
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
       $$.op.count = 2;
       $$.op.type = OPS_EnumerateClasses;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.flags = FL_localOnly;

       setRequest(parm,&$$,sizeof(XtokEnumClasses),OPS_EnumerateClasses);
       buildEnumClassesRequest(parm);
    }
    | localNameSpacePath enumClassesParmsList
    {
       $$.op.count = 2;
       $$.op.type = OPS_EnumerateClasses;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.className);
       $$.flags = ($2.flags & $2.flagsSet) | ((~$2.flagsSet) & FL_localOnly);

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
       $$.op.count = 2;
       $$.op.type = OPS_GetInstance;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.flags = FL_localOnly;
       $$.propertyList.values = NULL;
       $$.properties=0;
       $$.instNameSet = 0;

       setRequest(parm,&$$,sizeof(XtokGetInstance),OPS_GetInstance);
       buildGetInstanceRequest(parm);
    }
    | localNameSpacePath getInstanceParmsList
    {
       $$.op.count = 2;
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
       $$.op.count = 3;
       $$.op.type = OPS_CreateClass;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.superClass=NULL;

       setRequest(parm,&$$,sizeof(XtokCreateClass),OPS_CreateClass);
       buildCreateClassRequest(parm);
    }
    | localNameSpacePath createClassParm
    {
       $$.op.count = 3;
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
       $$.op.count = 2;
       $$.op.type = OPS_CreateInstance;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);

       setRequest(parm,&$$,sizeof(XtokCreateInstance),OPS_CreateInstance);
       buildCreateInstanceRequest(parm);
    }
    | localNameSpacePath createInstanceParm
    {
       $$.op.count = 2;
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
       $$.op.count = 2;
       $$.op.type = OPS_ModifyInstance;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);
       $$.flags = FL_includeQualifiers;
       $$.propertyList.values = 0;
       $$.properties=0;

       setRequest(parm,&$$,sizeof(XtokModifyInstance),OPS_ModifyInstance);
       buildModifyInstanceRequest(parm);
    }
    | localNameSpacePath modifyInstanceParmsList
    {
       $$.op.count = 2;
       $$.op.type = OPS_ModifyInstance;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment($2.namedInstance.path.className);
       $$.namedInstance = $2.namedInstance;
       $$.flags = $2.flags | ((~$2.flagsSet) & (FL_includeQualifiers));
       $$.propertyList = $2.propertyList;
       $$.properties=$2.properties;

       setRequest(parm,&$$,sizeof(XtokModifyInstance),OPS_ModifyInstance);
       buildModifyInstanceRequest(parm);
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
       $$.op.count = 2;
       $$.op.type = OPS_DeleteClass;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);

       setRequest(parm,&$$,sizeof(XtokDeleteClass),OPS_DeleteClass);
       buildDeleteClassRequest(parm);
    }
    | localNameSpacePath deleteClassParm
    {
       $$.op.count = 2;
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
       $$.op.count = 2;
       $$.op.type = OPS_DeleteInstance;
       $$.op.nameSpace=setCharsMsgSegment($1);
       $$.op.className=setCharsMsgSegment(NULL);

       setRequest(parm,&$$,sizeof(XtokDeleteInstance),OPS_DeleteInstance);
       buildDeleteInstanceRequest(parm);
    }
    | localNameSpacePath deleteInstanceParm
    {
       $$.op.count = 2;
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
       $$.op.count = 2;
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
       $$.op.count = 2;
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
       $$.op.count = 2;
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
       $$.op.count = 3;
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
       $$.op.count = 3;
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
       $$.op.count = 6;
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
       $$.op.count = 6;
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
       $$.op.count = 4;
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
       $$.op.count = 4;
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
       $$.op.count = 6;
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
       $$.op.count = 6;
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
       $$.op.count = 4;
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
       $$.op.count = 4;
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
       $$.cns=(char*)malloc(l);
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
	  $$.values=(XtokValue*)malloc(sizeof(XtokValue));
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
          $$.values=(XtokValue*)malloc(sizeof(XtokValue)*($$.max));
          $$.values[0]=$1;
        }
        | valueList value
        {
          if ($$.next == $$.max) { /* max was hit; let's bump it up 50% */
            $$.max = (int)($$.max * ((float)3)/2);
            $$.values=(XtokValue*)realloc(($$.values), sizeof(XtokValue)*($$.max));
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
       $$.values=(XtokValueReference*)malloc(sizeof(XtokValueReference)*($$.max));
       $$.values[0]=$1;
    }
    | valueRefList valueReference
    {
       if ($$.next == $$.max) { /* max was hit; let's bump it up 50% */
         $$.max = (int)($$.max * ((float)3)/2);
         $$.values=(XtokValueReference*)realloc(($$.values), sizeof(XtokValueReference)*($$.max));
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
       $$.keyBindings=(XtokKeyBinding*)calloc(($$.max),sizeof(XtokKeyBinding));
       $$.keyBindings[0].name=$1.name;
       $$.keyBindings[0].value=$1.value;
       $$.keyBindings[0].type=$1.type;
       $$.keyBindings[0].ref=$1.ref;
    }
    | keyBindings keyBinding
    {
       if ($$.next == $$.max) { /* max was hit; let's bump it up 50% */
         $$.max = (int)($$.max * ((float)3)/2);
         $$.keyBindings=(XtokKeyBinding*)realloc(($$.keyBindings), sizeof(XtokKeyBinding)*($$.max));
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
