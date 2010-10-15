#include "cimRequest.h"
#include "native.h"
#include "cimXmlParser.h"
#define _GNU_SOURCE
#include <string.h>


/* CimRs resource types */
#define RES_NS                    1
#define RES_NS_Collection         2
#define RES_Class                 3
#define RES_ClassCollection       4
#define RES_ClassAssocCollection  5
#define RES_ClassRefCollection    6
#define RES_ClassMethCollection   7
#define RES_ClassMeth             8
#define RES_Inst                  9
#define RES_InstCollection       10
#define RES_InstAssocCollection  11
#define RES_InstRefCollection    12
#define RES_InstMeth             13
#define RES_InstMethCollection   14
#define RES_Quals                15
#define RES_QuallsCollection     16

int parseCimRsQueryParams(char* p) {
  return 0;
}

typedef struct _cimrsreq {
  int scope;
#define SCOPE_NAMESPACE        1
#define SCOPE_NS_COLL          2
#define SCOPE_CLASS            3
#define SCOPE_CL_COLL          4
#define SCOPE_CL_METH          5
#define SCOPE_CL_METH_COLL     6
#define SCOPE_CL_ASSOC_COLL    7
#define SCOPE_CL_REF_COLL      8
#define SCOPE_INSTANCE         9
#define SCOPE_INST_COLL       10
#define SCOPE_INST_METH       11
#define SCOPE_INST_METH_COLL  12
#define SCOPE_INST_ASSOC_COLL 13
#define SCOPE_INST_REF_COLL   14
  char* path; /* strdup'd */
  char* ns;
  char* cn;
  char* meth; /* for class meth or inst meth */
  char* keyList;
  //CMPIArray *sortedKeyList;
  char** sortedKeys;
  int keyCount;
  /* query stuff */
} CimRsReq;

static int parseInstanceFragment(CimRsReq* req, char* fragment);

/* decode */
static char* percentDecode(char* s) {
// MCS This is a horrible horrible thing
  if (strstr(s,"cimv2")) {
    return "root/cimv2";
  }
  if (strstr(s,"interop")) {
    return "root/interop";
  }
  return s;
}

/* for parsing constant ending fragments, such as: 
   /cimrs/namespaces
   /cimrs/namespaces/{ns}/classes?istopclass
   /cimrs/namespaces/{ns}/classes/{cn}/instances/{keylist}/methods
 */
static int checkEndingFragment(char* fragment, char* name, int nameLen) {
  /* /cimrs/namespaces/{ns}/classes/{cn}/associators */
  if (fragment[nameLen] == 0) {
    fprintf(stderr, " request for %s\n", name);
    return 0;
  }
  /* something like /cimrs/namespaces/{ns}/classes/{cn}/associatorswithcrapattheend */
  else
    return -1;
}

static int parseMethodFragment(CimRsReq* req, char* fragment, const int coll, const int name) {

  char* meth;

  meth = strpbrk(fragment, "/");
  if (meth == NULL) {
    req->scope = coll;
    return checkEndingFragment(fragment, "methods", 7);
  }
  else {
    req->scope = name;
    req->meth = ++meth;
    return 0;
  }
}

int parseCimRsPath(char* p, CimRsReq* req) {

  char* path;
  char* tmp;
  char* ns;
  char* cn;
  char* query;

  req->path = strdup(p);
  path = req->path;

  if (strncasecmp(path, "/cimrs", 6) != 0) {
    return -1;
  }

  //tmp = strpbrk(++path, "/");
  tmp = path + 6; /* Skip '/cimrs' since we've already checked that */
  /* "/cimrs/namespaces" should be the start of every req path */
  if (strncasecmp(++tmp, "namespaces", 10) != 0) {
    return -1;
  }

  /* get the query string if there is one */
  query = strpbrk(path, "?"); /* do we only need to check this for GET? */
  if (query != NULL) {
    *query = 0;
    parseCimRsQueryParams(++query);
    fprintf(stderr, " has query\n");
  }

  ns = strpbrk(tmp, "/");

  /* we got "/cimrs/namespaces" or "/cimrs/namespaceswithcrapafterit */
  if (ns == NULL) {
    req->scope = SCOPE_NS_COLL;
    return checkEndingFragment(tmp, "namespaces", 10); 
  }

  tmp = strpbrk(++ns, "/");

  if (tmp == NULL) {
    /* we got "/cimrs/namespaces/{ns} */
    fprintf(stderr, " request for namespace '%s'\n", ns);
    req->scope = SCOPE_NAMESPACE;
    return 0;
  }

  *tmp = 0; /* null-out the '/' so that we get just the namespace in ns */

  req->ns = percentDecode(ns);

  /* should be /cimrs/namespaces/{ns}/classes */
  /* TODO: could be "qualifiers" as well */
  if (strncasecmp(++tmp, "classes", 7) != 0) {
    return -1;
  }

  cn = strpbrk(tmp, "/");

  if (cn == NULL) {
    req->scope = SCOPE_CL_COLL;
    return checkEndingFragment(tmp, "classes", 7);
  }
  
  tmp = strpbrk(++cn, "/");

  if (tmp == NULL) {
    /* /cimrs/namespaces/{ns}/classes/{cn} */
    req->scope = SCOPE_CLASS;
    return 0;
  }

  tmp = strpbrk(cn, "/");
  *tmp = 0;

  req->cn = cn;

  tmp++;

  /* /cimrs/namespaces/{ns}/classes/{cn}/instances */
  if (strncasecmp(tmp, "instances", 9) == 0) {
    return parseInstanceFragment(req, tmp);
  }

  /* /cimrs/namespaces/{ns}/classes/{cn}/associators */
  else if (strncasecmp(tmp, "associators", 11) == 0) {
    req->scope = SCOPE_CL_ASSOC_COLL;
    return checkEndingFragment(tmp, "associators", 11);
  }
  /* /cimrs/namespaces/{ns}/classes/{cn}/references */
  else if (strncasecmp(tmp, "references", 10) == 0) {
    req->scope = SCOPE_CL_REF_COLL;
    return checkEndingFragment(tmp, "references", 10);
  }
  /* /cimrs/namespaces/{ns}/classes/{cn}/methods */
  else if (strncasecmp(tmp, "methods", 7) == 0) {
    return parseMethodFragment(req, tmp, SCOPE_CL_METH_COLL, SCOPE_CL_METH);
  }

  return -1;
}



static int parseInstanceFragment(CimRsReq* req, char* fragment) {

  char* tmp;
  char* keyList = strpbrk(fragment, "/");
  
  if (keyList == NULL) {
    req->scope = SCOPE_INST_COLL;
    return checkEndingFragment(fragment, "instances", 9);
  }
  else {
    keyList++; /* TODO: key list syntax validation? */
    tmp = strpbrk(keyList, "/");
    /* /cimrs/namespaces/{ns}/classes/{cn}/instances/{keylist} */
    if (tmp == NULL) {
      req->scope = SCOPE_INSTANCE;
      req->keyList = percentDecode(keyList);
      return 0;
    }

    *tmp = 0;
    tmp++;    
    req->keyList = percentDecode(keyList);

    /* /cimrs/namespaces/{ns}/classes/{cn}/instances/{keylist}/associators */
    if (strncasecmp(tmp, "associators", 11) == 0) {
      req->scope = SCOPE_INST_ASSOC_COLL;
      return checkEndingFragment(tmp, "associators", 11);
    }
    /* /cimrs/namespaces/{ns}/classes/{cn}/instances/{keylist}/references */
    else if (strncasecmp(tmp, "references", 10) == 0) {
      req->scope = SCOPE_INST_REF_COLL;
      return checkEndingFragment(tmp, "references", 10);
    }
    /* /cimrs/namespaces/{ns}/classes/{cn}/instances/{keylist}methods */
    else if (strncasecmp(tmp, "methods", 7) == 0) {
      return parseMethodFragment(req, tmp, SCOPE_INST_METH_COLL, SCOPE_INST_METH);
    }
  }

  return -1;
}

static void
buildRSGetInstanceRequest(CimRequestContext *ctx, CimRsReq *rsReq, RequestHdr *reqHdr)
{ 
printf("MCS in BGIR\n");
  CMPIObjectPath *path;
  CMPIValue       val;
  CMPIType        type;
  GetInstanceReq *sreq;
  int             sreqSize = sizeof(GetInstanceReq);
  CMPIValue      *valp;
  int             i,
                  m;
  RequestHdr     *hdr = reqHdr;
  BinRequestContext *binCtx = hdr->binCtx;
  binCtx->oHdr = calloc(1, sizeof(OperationHdr));
  binCtx->oHdr->nameSpace=setCharsMsgSegment(rsReq->ns);
  binCtx->oHdr->className=setCharsMsgSegment(rsReq->cn);
  binCtx->oHdr->type=OPS_GetInstance;
  binCtx->oHdr->count=2;
  reqHdr->opType = OPS_GetInstance;
  

/*
  if (req->properties)
    sreqSize += req->properties * sizeof(MsgSegment);
*/
//sreqSize+=sizeof(MsgSegment);
  sreq = calloc(1, sreqSize);
  sreq->hdr.operation = OPS_GetInstance;
  //sreq->hdr.count = req->properties + 2;
  sreq->hdr.count = 0 + 2;

  
/*
  for (i = 0, m = req->instanceName.bindings.next; i < m; i++) {
  //this call is in CimXmlOps and should be moved
    valp =       
        getKeyValueTypePtr(req->instanceName.bindings.keyBindings[i].type,
                           req->instanceName.bindings.keyBindings[i].value,
                           &req->instanceName.bindings.keyBindings[i].ref,
                           &val, &type, req->op.nameSpace.data);
    CMAddKey(path, req->instanceName.bindings.keyBindings[i].name, valp,
             type);
  }
*/
  //valp = getKeyValueTypePtr("string","Mike",NULL, &val, &type, rsReq->ns);
//val.chars="Mike";
  //char * keyval=strdup("Mike");
  char * keyval,*keyname,*saveptr;
  path = TrackedCMPIObjectPath(rsReq->ns, rsReq->cn, NULL);
  keyval=strtok_r(rsReq->keyList,",",&saveptr);
  //for (i = 0; i < rsReq->sortedKeyList->ft->getSize(rsReq->sortedKeyList, NULL); i++)
  for (i = 0; i < rsReq->keyCount; i++)
  {
    //keyname=CMGetCharPtr(rsReq->sortedKeyList->ft->getElementAt(rsReq->sortedKeyList, i, NULL).value.string);
    keyname=rsReq->sortedKeys[i];
    printf("MCS key:%d %s\n", i,keyname);
    if (keyval == NULL) {
      printf("Missing key value for key %s\n",keyname);
      // Need to abort or something, and should be an mlog call
    } else {
      CMAddKey(path, keyname, keyval, CMPI_chars);
      keyval=strtok_r(NULL,",",&saveptr);
    }
  }


printf("MCS path: %s\n",CMGetCharPtr(CMObjectPathToString(path,NULL)));

  sreq->objectPath = setObjectPathMsgSegment(path);
  sreq->principal = setCharsMsgSegment(hdr->principal);

  binCtx->bHdr = &sreq->hdr;
  binCtx->bHdr->flags = FL_localOnly;
  binCtx->rHdr = hdr;
  binCtx->bHdrSize = sreqSize;
  binCtx->chunkedMode = binCtx->xmlAs = binCtx->noResp = 0;
  binCtx->pAs = NULL;

printf("MCS done\n");
}

int getSortedKeys(CimRsReq *rsReq)
{
  CMPIObjectPath *op;
  CMPIStatus rc;
  CMPIResult *rslt;
  CMPIArray  *klist;
  char ** keynames; 
  keynames=malloc(sizeof(char*)*4); //but need this dynam
  
  CMPIContext *ctx = native_new_CMPIContext(MEM_NOT_TRACKED, NULL);
  //rc=ClassProviderGetClass(NULL,ctx,rslt,op,NULL);
  int keyCount=0;
  printf("MCS cn %s ns %s\n",rsReq->cn,rsReq->ns);
  op = NewCMPIObjectPath(rsReq->ns, rsReq->cn,&rc);
  printf("MCS op rc:%d %s\n",rc.rc,rc.msg);
  /*
  keyCount=CMGetKeyCount(op,&rc);
  printf("MCS keycount:%u rc:%d %s\n",keyCount,rc.rc,rc.msg);
  rc=ClassProviderGetClass(NULL,ctx,rslt,op,NULL);
  printf("MCS gc rc:%d %s\n",rc.rc,rc.msg);
  */
  CMPIConstClass *cc = getConstClass(rsReq->ns,rsReq->cn);
  printf("MCS gcc %d\n",cc->refCount);
  CMPIString * CCCN = cc->ft->getClassName(cc,NULL);
  printf("MCS gcc %s\n",CCCN->ft->getCharPtr(CCCN,NULL));
  klist = cc->ft->getKeyList(cc);
  //rsReq->sortedKeyList=klist;
  CMPICount kcount = klist->ft->getSize(klist, NULL);
  printf("MCS keycount:%u\n",kcount);
  //CMPIData keyname=klist->ft->getElementAt(klist,0,NULL);
  int i;
  rsReq->keyCount=0;
  for (i = 0; i < klist->ft->getSize(klist, NULL); i++)
  {
    printf("MCS key:%d %s\n", i,CMGetCharPtr(klist->ft->getElementAt(klist, i, NULL).value.string));
    //keynames[i]=malloc(sizeof(char) * sizeof(CMGetCharPtr(klist->ft->getElementAt(klist, i, NULL).value.string)));
    keynames[i]=malloc(sizeof(char) * strlen(CMGetCharPtr(klist->ft->getElementAt(klist, i, NULL).value.string))+2);
    strcpy(keynames[i],CMGetCharPtr(klist->ft->getElementAt(klist, i, NULL).value.string));
    printf("MCS akey %s\n",keynames[i]);
    rsReq->keyCount++;
  }
//OK Now need to actually sort them before sticking the request
  rsReq->sortedKeys=keynames;
  //rsReq->sortedKeyList=klist; // this should go away
}

RequestHdr
scanCimRsRequest(CimRequestContext *ctx, char *cimRsData, int *rc)
{
  fprintf(stderr, "path is '%s'\nverb is '%s'\n", ctx->path, ctx->verb);
  //RequestHdr *reqHdr = calloc(1,sizeof(*reqHdr);
  RequestHdr reqHdr = { NULL, 0, 0, 0, 0, 0, 0, 0,
                        NULL, 0, 0, 0, NULL, 0, 0,
                      };

  if (strncasecmp(ctx->path, "/cimrs", 6) != 0) {
    // We are not the parser you are looking for.
    *rc=1;
    return reqHdr;
  }
  *rc=0;
      

/*
  ctx.path = path;
  ctx.httpOp = tmp;
 */
  CimRsReq req = {0, NULL, NULL, NULL, NULL};
  int prc = parseCimRsPath(ctx->path, &req);
  fprintf(stderr, "parseCimPath returned %d\n", prc);
  if (prc == 0) {
    fprintf(stderr, "scope: %d\n", req.scope);
    if (req.ns) fprintf(stderr, "%s", req.ns);
    if (req.cn) fprintf(stderr, ":%s", req.cn);
    if (req.keyList) fprintf(stderr, ".%s", req.keyList);
    if (req.meth) fprintf(stderr, " %s", req.meth);
    fprintf(stderr, "\n");
  }
  reqHdr.binCtx = calloc(1, sizeof(BinRequestContext));
  reqHdr.principal = ctx->principal;
  reqHdr.sessionId = ctx->sessionId;
  if (strcmp(ctx->verb,"GET") == 0) {
    if (req.scope == SCOPE_INSTANCE) {
      fprintf(stderr,"MCS is a gi\n");
      int rrc=getSortedKeys(&req);
      buildRSGetInstanceRequest(ctx,&req,&reqHdr);
      fprintf(stderr,"MCS gotreq %s\n",reqHdr.className);
    }
  }

 //*rc = PARSERC_OK;
 return reqHdr;
}

/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
