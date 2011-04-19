
/*
 * cimAccountPassthroughProvider.c
 *
 * (C) Copyright IBM Corp. 2011
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
 * 
 *
 */

#include "cmpi/cmpidt.h"
#include "cmpi/cmpift.h"
#include "cmpi/cmpimacs.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "fileRepository.h"
#include "utilft.h"
#include "trace.h"
#include "queryOperation.h"
#include "providerMgr.h"
#include "internalProvider.h"
#include "native.h"
#include "objectpath.h"
#include <time.h>
#include "instance.h"

/*
 * ------------------------------------------------------------------------- 
 */

static const CMPIBroker *_broker;

void
setStatus(CMPIStatus *st, CMPIrc rc, char *msg)
{
  st->rc = rc;
  if (rc != 0 && msg)
    st->msg = sfcb_native_new_CMPIString(msg, NULL, 0);
  else
    st->msg = NULL;
}

static int
interOpNameSpace(const CMPIObjectPath * cop, CMPIStatus *st)
{
  char           *ns = (char *) CMGetNameSpace(cop, NULL)->hdl;
  fprintf(stderr, "  ns is: %s\n", ns);
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

// static CMPIContext *
// prepareUpcall(const CMPIContext *ctx)
// {
//   /*
//    * used to invoke the internal provider in upcalls, otherwise we will be 
//    * routed to this provider again
//    */
//   CMPIContext    *ctxLocal;
//   ctxLocal = native_clone_CMPIContext(ctx);
//   CMPIValue       val;
//   val.string = sfcb_native_new_CMPIString("$DefaultProvider$", NULL, 0);
//   ctxLocal->ft->addEntry(ctxLocal, "rerouteToProvider", &val, CMPI_string);
//   return ctxLocal;
// }

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
CimAccountPassthroughProviderMethodCleanup(CMPIMethodMI * mi,
                             const CMPIContext *ctx, CMPIBoolean terminate)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_INDPROVIDER, "CimAccountPassthroughProviderMethodCleanup");
  _SFCB_RETURN(st);
}

/*
 * ------------------------------------------------------------------------- 
 */

CMPIStatus
CimAccountPassthroughProviderInvokeMethod(CMPIMethodMI * mi,
                            const CMPIContext *ctx,
                            const CMPIResult *rslt,
                            const CMPIObjectPath * ref,
                            const char *methodName,
                            const CMPIArgs * in, CMPIArgs * out)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };

  _SFCB_ENTER(TRACE_INDPROVIDER, "CimAccountPassthroughProviderInvokeMethod");

  /* TODO: get rid of this? */
  if (interOpNameSpace(ref, &st) != 1)
    _SFCB_RETURN(st);

  _SFCB_TRACE(1, ("--- Method: %s", methodName));

  if (strcasecmp(methodName, "UpdateExpiredPassword") == 0) {
    fprintf(stderr, "  IM UpdateExpiredPassword\n");
    CMPIObjectPath *caOp = CMNewObjectPath(_broker, "root/cimv2", "cim_account", &st);

    CMPIData principal = CMGetContextEntry(ctx, "CMPIPrincipal", &st);
    char* httpUser = CMGetCharPtr(principal.value.string);

    /* Important! We assume the Name key = the expired HTTP user */
    CMPIData item, nameKey;
    CMPIString* nameKeyStr;
    CMPIEnumeration *enm = CBEnumInstanceNames(_broker, ctx, caOp, &st);
    CMPIInstance *caInst = NULL;

    while (enm && CMHasNext(enm, &st)) {
      fprintf(stderr, "  got an instance of CIM_Account\n");
      item = CMGetNext(enm, &st);
      caOp = item.value.ref;
      nameKey = CMGetKey(caOp, "Name", &st);
      if (st.rc == CMPI_RC_OK) {
	nameKeyStr = nameKey.value.string;
	if (strcmp(CMGetCharsPtr(nameKeyStr, &st), httpUser) == 0) {
	  fprintf(stderr, "  name matches\n");
	  caInst = CBGetInstance(_broker, ctx, caOp, NULL, &st);
	  break;
	}
      }
    }
    if (caInst) {
      /* ok to send ModifyInstance request to CIM_Account provider */
      char* newPW = "bob";
      CMPIString* npwv;
      npwv = CMNewString(_broker, newPW, NULL);
      fprintf(stderr, " npwv is %s\n", CMGetCharsPtr(npwv, NULL));
      
      CMPIArray *pwArray = CMNewArray(_broker, 1, CMPI_string, &st);

      st = CMSetArrayElementAt(pwArray, 0, (CMPIValue*)&(npwv), CMPI_string);
      fprintf(stderr, "  after set st = %d\n", st.rc);
      
      CMPIData d = CMGetArrayElementAt(pwArray, 0, NULL);
      CMPIString* s = d.value.string;
      fprintf(stderr, "in the array is %s\n", CMGetCharsPtr(s, NULL));
       
      CMSetProperty(caInst, "UserPassword", (CMPIValue*)&(pwArray), CMPI_stringA);
      st = CBModifyInstance(_broker, ctx, caOp, caInst, NULL);
      fprintf(stderr, "st from modify is %d\n", st.rc);
    }
    else {      /* no caInst; probably wrong principal (UserName didn't match) */
      fprintf(stderr, "  Name didn't match\n"); 
      _SFCB_TRACE(1, ("--- Invalid request method: %s", methodName));
      setStatus(&st, CMPI_RC_ERR_METHOD_NOT_FOUND, "Invalid request method");
    }
  }
  else {
    _SFCB_TRACE(1, ("--- Invalid request method: %s", methodName));
    setStatus(&st, CMPI_RC_ERR_METHOD_NOT_FOUND, "Invalid request method");
  }

  _SFCB_RETURN(st);
}


CMMethodMIStub(CimAccountPassthroughProvider, CimAccountPassthroughProvider, _broker, CMNoHook);

/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
