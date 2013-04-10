/*
This provider will enumerate an arbitrary number of instances 
or instance names. The number of instances returned is determined
by the value of the instCount variable. The value of instCount 
can be set via the setCount method call which takes a uint32 
parameter, InstanceCount. For example:

wbemcli cm http://localhost:5988/root/cimv2:TEST_PulledInstance setCount.InstanceCount=86

*/
#include "cmpi/cmpidt.h"
#include "cmpi/cmpift.h"
#include "cmpi/cmpimacs.h"
#include <string.h>
#include <sys/time.h>
#include "sfcbmacs.h"

#define _ClassName "TEST_PulledInstance"
static const CMPIBroker *_broker;

// This holds the number of instances to return
static unsigned int instCount=16;

CMPIStatus
TestPulledInstanceProviderEnumInstanceNames(CMPIInstanceMI * mi,
                                      const CMPIContext *ctx,
                                      const CMPIResult *rslt,
                                      const CMPIObjectPath * ref)
{
  CMPIStatus      rc = { CMPI_RC_OK, NULL };
  CMPIObjectPath *lop=CMNewObjectPath(_broker,"root/cimv2","TEST_PulledInstance",NULL);
  unsigned int    j = 0;
  CMPIValue jv;

  for (j = 1; j <= instCount; j++) {
    jv.uint32=j;
    CMAddKey(lop,"Identifier",&jv,CMPI_uint32);
    CMReturnObjectPath(rslt, lop);
  }
  CMReturnDone(rslt);
  CMReturn(CMPI_RC_OK);
}

CMPIStatus
TestPulledInstanceProviderEnumInstances(CMPIInstanceMI * mi,
                                  const CMPIContext *ctx,
                                  const CMPIResult *rslt,
                                  const CMPIObjectPath * op,
                                  const char **properties)
{
  CMPIStatus      rc = { CMPI_RC_OK, NULL };
  unsigned int    j = 0;
  CMPIData        data;
  CMPIObjectPath *lop=CMNewObjectPath(_broker,"root/cimv2","TEST_PulledInstance",NULL);
  CMPIInstance *ci;
  CMPIValue jv;

  for (j = 1; j <= instCount; j++) {
    jv.uint32=j;
    CMAddKey(lop,"Identifier",&jv,CMPI_uint32);
    ci=CMNewInstance(_broker,lop,NULL);
    CMSetProperty(ci,"Identifier",&jv,CMPI_uint32);
    CMReturnInstance(rslt, ci);
  }
  CMReturnDone(rslt);
  CMReturn(CMPI_RC_OK);
}

CMPIStatus
TestPulledInstanceProviderInvokeMethod(CMPIMethodMI * mi,
                            const CMPIContext *ctx,
                            const CMPIResult *rslt,
                            const CMPIObjectPath * ref,
                            const char *methodName,
                            const CMPIArgs * in, CMPIArgs * out)
{

  if (strcasecmp(methodName, "setCount") == 0) {
    instCount=in->ft->getArg(in, "InstanceCount", NULL).value.uint32;
  } else {
    CMReturn(CMPI_RC_ERR_METHOD_NOT_FOUND);
  }
  CMReturn(CMPI_RC_OK);
}

static CMPIStatus okCleanup(TestPulledInstanceProvider,Method);

CMPIStatus
TestPulledInstanceProviderCleanup(CMPIInstanceMI * mi,
                       const CMPIContext *ctx, CMPIBoolean terminate)
{
  CMReturn(CMPI_RC_OK);
}

/*
 * Stubs
*/
static CMPIStatus notSupCMPI_EQ(TestPulledInstanceProvider);
static CMPIStatus notSupCMPI_CI(TestPulledInstanceProvider);
static CMPIStatus notSupCMPI_DI(TestPulledInstanceProvider);
static CMPIStatus notSupCMPI_MI(TestPulledInstanceProvider);
static CMPIStatus notSupCMPI_GI(TestPulledInstanceProvider);

/*
 * ---------------------------------------------------------------------------
 * Provider Factory 
 * ---------------------------------------------------------------------------
 */

CMInstanceMIStub(TestPulledInstanceProvider, TestPulledInstanceProvider, _broker, CMNoHook) 
CMMethodMIStub(TestPulledInstanceProvider, TestPulledInstanceProvider, _broker, CMNoHook);

/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
