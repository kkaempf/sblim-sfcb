
/*
 * control.c
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
 * Author:        Adrian Schuur <schuur@de.ibm.com>
 *
 * Description:
 *
 * sfcb.cfg config parser.
 *
 */

#include <sfcCommon/utilft.h>
#include "support.h"
#include "mlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef SFCB_CONFDIR
#define SFCB_CONFDIR "/etc/sfcb"
#endif

#ifndef SFCB_STATEDIR
#define SFCB_STATEDIR "/var/lib/sfcb"
#endif

#ifndef SFCB_LIBDIR
#define SFCB_LIBDIR "/usr/lib"
#endif

union ctl_num {
  _Bool b;
  unsigned int uint;
  long slong;
  unsigned long ulong;
};

typedef struct control {
  char           *id;
#define CTL_STRING  0
#define CTL_USTRING 1 /* unstripped string */
#define CTL_BOOL    2
#define CTL_LONG    3
#define CTL_ULONG   4
#define CTL_UINT    5
  int             type;
  char           *strValue;
  union ctl_num   intValue;
  int             dupped;
} Control;

static UtilHashTable *ct = NULL;

char           *configfile = NULL;
char           *ip4List= NULL;
char           *ip6List= NULL;

char          **origArgv;
int             origArgc;
unsigned int    labelProcs;

/**
 * Kindly null terminate, always, even if might overwrite
 * the last char of the truncated string. 
*/
inline char *strncpy_kind(char *to, char *from, size_t size) {
  strncpy(to, from, size);
  *(to + size - 1) = '\0';
  return to;
}

/**
 * Helper functions for labeling a process by modifying the process argv string.
 * On the first call, convert the original argv[] to a contiguous string by
 * replacing inter-arg nulls with spaces and adding a trailing space; then
 * append appendstr.  On subsequent calls only append appendstr.  Passing
 * NULL for appendstr forces the function to act as if called for the first
 * time, i.e. reset the static ptr and prepare the argv[] string.
 */
void append2Argv(char *appendstr) {
  int i;
  static char *ptr = NULL;
  if (!ptr || !appendstr) { /* called for the 1st time or intentionally reset */ 
    for (i=1 ; i < origArgc ; i++)
      *(*(origArgv+i) - 1) = ' ';  /* replace inter-arg nulls with spaces */
    ptr = origArgv[origArgc - 1];
  }
  if (appendstr)
    /* Enforce limit of appending no more than labelProcs chars */
    ptr += strlen(strncpy_kind(ptr, appendstr,
        labelProcs - (ptr - origArgv[origArgc - 1])  + 1));
}

/**
 * Restore the process argv[] to its original form by replacing inter-arg spaces
 * with nulls. May be required prior to spawning a child process. To prepare for
 * a full restart, additionally remove the pad argument.
 */
void restoreOrigArgv(int removePad) {
  int i;
  for (i=1 ; i < origArgc ; i++)
    *(*(origArgv+i) - 1) = '\0';  /* restore inter-arg nulls */

  if (removePad)
    origArgv[origArgc - 1] = NULL;
}

/* Control initial values
 { property, type, string value, numeric value} */
static Control init[] = {
  {"ip4AddrList", CTL_STRING, NULL, {0}},
  {"ip6AddrList", CTL_STRING, NULL, {0}},
  {"httpPort", CTL_LONG, NULL, {.slong=5988}},
  {"enableHttp", CTL_BOOL, NULL, {.b=1}},
  {"enableUds", CTL_BOOL,  NULL, {.b=1}},
  {"httpProcs", CTL_LONG, NULL, {.slong=8}},
  {"httpsPort", CTL_LONG, NULL, {.slong=5989}},
  {"enableHttps", CTL_BOOL, NULL, {.b=0}},
  {"httpLocalOnly", CTL_BOOL, NULL, {.b=0}},
  {"httpUserSFCB", CTL_BOOL, NULL, {.b=1}},
  {"httpUser", CTL_STRING, "", {0}},
#ifdef HAVE_SLP
  {"enableSlp", CTL_BOOL, NULL, {.b=1}},
  {"slpRefreshInterval", CTL_LONG, NULL, {.slong=600}},
#endif
  {"provProcs", CTL_LONG, NULL, {.slong=32}},
  {"sfcbCustomLib", CTL_STRING, "sfcCustomLib", {0}},
  {"basicAuthLib", CTL_STRING, "sfcBasicAuthentication", {0}},
  {"basicAuthEntry", CTL_STRING, "_sfcBasicAuthenticate", {0}},
  {"doBasicAuth", CTL_BOOL, NULL, {.b=0}},
  {"doUdsAuth", CTL_BOOL, NULL, {.b=0}},

  {"useChunking", CTL_STRING, "true", {0}},
  {"chunkSize", CTL_LONG, NULL, {.slong=50000}},
  {"maxChunkObjCount", CTL_ULONG, NULL, {.ulong=0}},

  {"trimWhitespace", CTL_BOOL, NULL, {.b=1}},

  {"keepaliveTimeout", CTL_LONG, NULL, {.slong=15}},
  {"keepaliveMaxRequest", CTL_LONG, NULL, {.slong=10}},
  {"selectTimeout", CTL_LONG, NULL, {.slong=5}},
  {"maxBindAttempts", CTL_LONG, NULL, {.slong=8}},

  {"providerSampleInterval", CTL_LONG, NULL, {.slong=30}},
  {"providerTimeoutInterval", CTL_LONG, NULL, {.slong=60}},
  {"providerAutoGroup", CTL_BOOL, NULL, {.b=1}},
  {"providerDefaultUserSFCB", CTL_BOOL, NULL, {.b=1}},
  {"providerDefaultUser", CTL_STRING, "", {0}},

  {"sslKeyFilePath", CTL_STRING, SFCB_CONFDIR "/file.pem", {0}},
  {"sslCertificateFilePath", CTL_STRING, SFCB_CONFDIR "/server.pem", {0}},
  {"sslCertList", CTL_STRING, SFCB_CONFDIR "/clist.pem", {0}},
  {"sslCiphers", CTL_STRING, "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH", {0}},
  {"sslDhParamsFilePath", CTL_STRING, NULL, {0}},
  {"sslEcDhCurveName", CTL_STRING, "secp224r1", {0}},

  {"registrationDir", CTL_STRING, SFCB_STATEDIR "/registration", {0}},
  {"providerDirs", CTL_USTRING, SFCB_LIBDIR " " CMPI_LIBDIR " " LIBDIR, {0}},

  {"enableInterOp", CTL_BOOL, NULL, {.b=1}},
  {"sslClientTrustStore", CTL_STRING, SFCB_CONFDIR "/client.pem", {0}},
  {"sslClientCertificate", CTL_STRING, "ignore", {0}},
  {"sslIndicationReceiverCert", CTL_STRING, "ignore", {0}},
  {"certificateAuthLib", CTL_STRING, "sfcCertificateAuthentication", {0}},
  {"localSocketPath", CTL_STRING, "/tmp/sfcbLocalSocket", {0}},
  {"httpSocketPath", CTL_STRING, "/tmp/sfcbHttpSocket", {0}},
  {"socketPathGroupPerm", CTL_STRING, NULL, {0}},

  {"traceFile", CTL_STRING, "stderr", {0}},
  {"traceLevel", CTL_LONG, NULL, {.slong=0}},
  {"traceMask", CTL_LONG, NULL, {.slong=0}},

  {"httpMaxContentLength", CTL_UINT, NULL, {.uint=100000000}},
  {"validateMethodParamTypes", CTL_BOOL, NULL, {.b=0}},
  {"maxMsgLen", CTL_ULONG, NULL, {.ulong=10000000}},
  {"networkInterface", CTL_USTRING, NULL, {0}},
  {"DeliveryRetryInterval", CTL_UINT, NULL, {.uint=20}},
  {"DeliveryRetryAttempts", CTL_UINT, NULL, {.uint=3}},
  {"SubscriptionRemovalTimeInterval", CTL_UINT, NULL, {.uint=2592000}},
  {"SubscriptionRemovalAction", CTL_UINT, NULL, {.uint=2}},
  {"indicationDeliveryThreadLimit", CTL_LONG, NULL, {.slong=30}},
  {"indicationDeliveryThreadTimeout", CTL_LONG, NULL, {.slong=0}},
  {"MaxListenerDestinations", CTL_LONG, NULL, {.slong=100}},
  {"MaxActiveSubscriptions", CTL_LONG, NULL, {.slong=100}},
  {"indicationCurlTimeout", CTL_LONG, NULL, {.slong=10}},
};

static Control *cache;

void
sunsetControl()
{
  int             i,
                  m;
  for (i = 0, m = sizeof(init) / sizeof(Control); i < m; i++) {
    if(cache[i].dupped) {
      free(cache[i].strValue);
      cache[i].dupped = 0;
    }
  }
  if (ct) {
    ct->ft->release(ct);
    ct=NULL;
  }
  if (cache)
    free(cache);
}

static int 
getUNum(char* str, unsigned long *val, unsigned long max) {

  if (isdigit(str[0])) {
    unsigned long   tmp = strtoul(str, NULL, 0);
    if (tmp < max) {
      *val = tmp;
      return 0;
    }
  }
  *val = 0;
  return -1;
}

int
setupControl(char *fn)
{
  FILE           *in;
  char            fin[1024],
                 *stmt = NULL;
  unsigned short  n = 0,
                  err = 0;
  unsigned int    i,
                  m;
  CntlVals        rv;
  char *configFile;

  if (ct)
    return 0;

  if (fn) {
    if (strlen(fn) >= sizeof(fin))
      mlogf(M_ERROR,M_SHOW, "--- \"%s\" too long\n", fn);
    strncpy(fin,fn,sizeof(fin));
  } 
  else if ((configFile = getenv("SFCB_CONFIG_FILE")) != NULL && configFile[0] != '\0') {
    if (strlen(configFile) >= sizeof(fin))
      mlogf(M_ERROR,M_SHOW, "--- \"%s\" too long\n", configFile);
    strncpy(fin,configFile,sizeof(fin));
  } else {
    strncpy(fin, SFCB_CONFDIR "/sfcb.cfg", sizeof(fin));
  }
  fin[sizeof(fin)-1] = '\0';

  if (fin[0] == '/')
    mlogf(M_INFO, M_SHOW, "--- Using %s\n", fin);
  else
    mlogf(M_INFO, M_SHOW, "--- Using ./%s\n", fin);
  in = fopen(fin, "r");
  if (in == NULL) {
    mlogf(M_ERROR, M_SHOW, "--- %s not found\n", fin);
    return -2;
  }

  /* populate HT with default values */
  ct = UtilFactory->newHashTable(61, UtilHashTable_charKey |
                                 UtilHashTable_ignoreKeyCase);

  cache = malloc(sizeof(init));
  memcpy(cache, init, sizeof(init));

  for (i = 0, m = sizeof(init) / sizeof(Control); i < m; i++) {
    ct->ft->put(ct, cache[i].id, &cache[i]);
  }

  /* run through the config file lines */
  while (fgets(fin, 1024, in)) {
    n++;
    if (stmt)
      free(stmt);
    stmt = strdup(fin);
    switch (cntlParseStmt(fin, &rv)) {
    case 0:
    case 1:
      mlogf(M_ERROR, M_SHOW,
            "--- control statement not recognized: \n\t%d: %s\n", n, stmt);
      err = 1;
      break;
    case 2:
      for (i = 0; i < sizeof(init) / sizeof(Control); i++) {
        if (strcmp(rv.id, cache[i].id) == 0) {
          /* unstripped character string */
          if (cache[i].type == CTL_USTRING) {
            cache[i].strValue = strdup(rv.val);
            if (strchr(cache[i].strValue, '\n'))
              *(strchr(cache[i].strValue, '\n')) = 0;
            cache[i].dupped = 1;
          }
          /* string */
          else if (cache[i].type == CTL_STRING) {
            cache[i].strValue = strdup(cntlGetVal(&rv));
            cache[i].dupped = 1;
          }
          /* numeric */
          else {

            char* val = cntlGetVal(&rv);
            long slval;
            unsigned long ulval;

            switch (cache[i].type) {

            case CTL_BOOL:
              if (strcasecmp(val, "true") == 0) {
                cache[i].intValue.b = 1;
              }
              else if (strcasecmp(val, "false") == 0) {
                cache[i].intValue.b = 0;
              }
              else {
                err = 1;
              }
              break;

            case CTL_LONG:
              slval = strtol(val, NULL, 0);
              cache[i].intValue.slong = slval;
              break;

            case CTL_ULONG:
              if (getUNum(val, &ulval, ULONG_MAX) == 0) {
                cache[i].intValue.ulong = ulval;
              }
              else {
                err = 1;
              }
              break;

            case CTL_UINT:
              if (getUNum(val, &ulval, UINT_MAX) == 0) {
                cache[i].intValue.uint = (unsigned int)ulval;
              }
              else {
                err = 1;
              }
              break;
            }

            if (!err) {
              ct->ft->put(ct, cache[i].id, &cache[i]);
            }

          }
          if (err) break;

          goto ok;
        }
      }
      mlogf(M_ERROR, M_SHOW, "--- invalid control statement: \n\t%d: %s\n",
            n, stmt);
      err = 1;
    ok:
      break;
    case 3:
      break;  /* control line is just a comment */
    }
    if (err) break;
  }

  if (stmt)
    free(stmt);

  fclose(in);

  if (err) {
    mlogf(M_INFO, M_SHOW,
          "--- Broker terminated because of previous error(s)\n");
    exit(1);
  }

  return 0;
}

int
getControlChars(char *id, char **val)
{
  Control        *ctl;
  int             rc = -1;

  if (ct == NULL) {
    setupControl(configfile);
  }

  if ((ctl = ct->ft->get(ct, id))) {
    if (ctl->type == CTL_STRING || ctl->type == CTL_USTRING) {
      *val = ctl->strValue;
      return 0;
    }
    rc = -2;
  }
  *val = NULL;
  return rc;
}

int
getControlNum(char *id, long *val)
{
  Control        *ctl;
  int             rc = -1;

  if (ct == NULL) {
    setupControl(configfile);
  }

  if ((ctl = ct->ft->get(ct, id))) {
    if (ctl->type == CTL_LONG) {
      *val = ctl->intValue.slong;
      return 0;
    }
    rc = -2;
  }
  *val = 0;
  return rc;
}

int
getControlUNum(char *id, unsigned int *val)
{
  Control        *ctl;
  int             rc = -1;

  if (ct == NULL) {
    setupControl(configfile);
  }

  if ((ctl = ct->ft->get(ct, id))) {
    if (ctl->type == CTL_UINT) {
      *val = ctl->intValue.uint;
      return 0;
    }
    rc = -2;
  }
  *val = 0;
  return rc;
}

int
getControlULong(char *id, unsigned long *val)
{
  Control        *ctl;
  int             rc = -1;

  if (ct == NULL) {
    setupControl(configfile);
  }

  if ((ctl = ct->ft->get(ct, id))) {
    if (ctl->type == CTL_ULONG) {
      *val = ctl->intValue.ulong;
      return 0;
    }
    rc = -2;
  }
  *val = 0;
  return rc;
}

int
getControlBool(char *id, int *val)
{
  if (ct == NULL) {
    setupControl(configfile);
  }

  Control        *ctl;
  int             rc = -1;
  if ((ctl = ct->ft->get(ct, id))) {
    if (ctl->type == CTL_BOOL) {
      *val = ctl->intValue.b;
      return 0;
    }
    rc = -2;
  }
  *val = 0;
  return rc;
}


/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
