#include "cimRequest.h"

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
  /* query stuff */
} CimRsReq;

static int parseInstanceFragment(CimRsReq* req, char* fragment);

/* decode */
static char* percentDecode(char* s) {
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
  tmp = strpbrk(++path, "/");

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

RequestHdr
scanCimRsRequest(CimRequestContext *ctx, char *cimRsData, int *rc)
{
  fprintf(stderr, "path is '%s'\nverb is '%s'\n", ctx->path, ctx->verb);
  RequestHdr reqHdr = { NULL, 0, 0, 0, 0, 0, 0, 0,
                        NULL, 0, 0, 0, NULL, 0, 0,
                      };

  if (strncmp(ctx->contentType,"application/json",16) !=0 ) {
    // We're not the parser you are looking for.
    *rc=1;
    return reqHdr;
  }
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

  if (strcmp(ctx->verb,"GET")) {
    if (strcmp(ctx->path,"instances")) {
    }
  }
      

/*
  ctx.path = path;
  ctx.httpOp = tmp;

  CimRsReq req = {0, NULL, NULL, NULL, NULL};
  int prc = parseCimRsPath(path, &req);
  fprintf(stderr, "parseCimPath returned %d\n", prc);
  if (prc == 0) {
    fprintf(stderr, "scope: %d\n", req.scope);
    if (req.ns) fprintf(stderr, "%s", req.ns);
    if (req.cn) fprintf(stderr, ":%s", req.cn);
    if (req.keyList) fprintf(stderr, ".%s", req.keyList);
    if (req.meth) fprintf(stderr, " %s", req.meth);
    fprintf(stderr, "\n");
  }
 */
 *rc = PARSERC_ERR;
 return reqHdr;
}

/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
