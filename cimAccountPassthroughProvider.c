
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
 * This provider's only function is to handle an InvokeMethod request
 * for UpdateExpiredPassword and pass it on to the CIM_Account provider
 *
 */

#include "cmpi/cmpidt.h"
#include "cmpi/cmpift.h"
#include "cmpi/cmpimacs.h"

#include <stdlib.h>
#include <string.h>
#include "trace.h"
#include "native.h"

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


CMPIStatus
CimAccountPassthroughProviderMethodCleanup(CMPIMethodMI * mi,
                             const CMPIContext *ctx, CMPIBoolean terminate)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_PROVIDERS, "CimAccountPassthroughProviderMethodCleanup");
  _SFCB_RETURN(st);
}

CMPIStatus
CimAccountPassthroughProviderInvokeMethod(CMPIMethodMI * mi,
                            const CMPIContext *ctx,
                            const CMPIResult *rslt,
                            const CMPIObjectPath * ref,
                            const char *methodName,
                            const CMPIArgs * in, CMPIArgs * out)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };

  _SFCB_ENTER(TRACE_PROVIDERS, "CimAccountPassthroughProviderInvokeMethod");

  _SFCB_TRACE(1, ("--- Method: %s", methodName));

  CMPIData arg = CMGetArg(in, "UserPassword", &st);
  if (st.rc != CMPI_RC_OK) {
    setStatus(&st, CMPI_RC_ERR_NOT_FOUND, 
              "Required argument UserPassword missing");
    _SFCB_RETURN(st);
  }
  const char* newPW = CMGetCharPtr(arg.value.string);
  fprintf(stderr, "  newPW is \"%s\"\n", newPW);

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
    if (caInst) {  /* ok to send ModifyInstance request to CIM_Account prov */

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

      CMPIValue av; 
      av.string = st.msg;
      CMAddArg(out, "Message", &av, CMPI_string);
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
