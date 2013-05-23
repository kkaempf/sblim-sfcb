#include <stdio.h>
#include <string.h>
#include <time.h>

#define CMPI_VER_86

#include "cmpi/cmpidt.h"
#include "cmpi/cmpift.h"
#include "cmpi/cmpimacs.h"

static const CMPIBroker *broker;

unsigned char   CMPI_true = 1;
unsigned char   CMPI_false = 0;

static int      enabled = 0;
static int      activated = 0;
static int      activated2 = 0;
static int      _nextUID = 0;
static int gen1 = 0;
static int gen2 = 0;

static void
generateIndication(const char *methodname, const CMPIContext *ctx)
{

  CMPIInstance   *inst;
  CMPIObjectPath *cop;
  CMPIDateTime   *dat;
  CMPIArray      *ar;
  CMPIStatus      rc;
  char            buffer[32];

  if (enabled && activated) {
    cop = CMNewObjectPath(broker, "root/interop", "Test_Indication", &rc);
    inst = CMNewInstance(broker, cop, &rc);

    sprintf(buffer, "%d", _nextUID++);
    CMSetProperty(inst, "IndicationIdentifier", buffer, CMPI_chars);

    dat = CMNewDateTime(broker, &rc);
    CMSetProperty(inst, "IndicationTime", &dat, CMPI_dateTime);

    CMPIObjectPath* cop1 = CMNewObjectPath(broker, "root/cimv2", "Sample_Instance", &rc);
    CMPIValue value3;
    value3.string = CMNewString(broker, "I'm an EmbeddedObject", &rc);
    CMPIInstance* Einst;
    Einst = CMNewInstance(broker, cop1, &rc);
    CMSetProperty(Einst, "Message", &value3, CMPI_string);
    CMSetProperty(inst, "EInst", &Einst, CMPI_instance);

    CMSetProperty(inst, "MethodName", methodname, CMPI_chars);

    ar = CMNewArray(broker, 0, CMPI_string, &rc);
    CMSetProperty(inst, "CorrelatedIndications", &ar, CMPI_stringA);

    rc = CBDeliverIndication(broker, ctx, "root/interop", inst);
    if (rc.rc != CMPI_RC_OK) {
      fprintf(stderr, "+++ Could not send the indication!\n");
    }
    gen1++;
  }
  fprintf(stderr, "+++ generateIndication() done %d\n", gen1);
}

static void
generateIndication2(const char *methodname, const CMPIContext *ctx)
{

  CMPIInstance   *inst;
  CMPIObjectPath *cop;
  CMPIDateTime   *dat;
  CMPIArray      *ar;
  CMPIStatus      rc;
  char            buffer[32];

  if (enabled && activated2) {
    cop = CMNewObjectPath(broker, "root/interop2", "Test_Indication", &rc);
    inst = CMNewInstance(broker, cop, &rc);

    sprintf(buffer, "%d", _nextUID++);
    CMSetProperty(inst, "IndicationIdentifier", buffer, CMPI_chars);

    dat = CMNewDateTime(broker, &rc);
    CMSetProperty(inst, "IndicationTime", &dat, CMPI_dateTime);

    CMSetProperty(inst, "MethodName", methodname, CMPI_chars);

    ar = CMNewArray(broker, 0, CMPI_string, &rc);
    CMSetProperty(inst, "CorrelatedIndications", &ar, CMPI_stringA);

    rc = CBDeliverIndication(broker, ctx, "root/interop", inst);
    if (rc.rc != CMPI_RC_OK) {
      fprintf(stderr, "+++ Could not send the indication!\n");
    }
    gen2++;
  }
  fprintf(stderr, "+++ generateIndication2() done %d\n", gen2);
}


// ----------------------------------------------------------
// ---
// Method Provider
// ---
// ----------------------------------------------------------

CMPIStatus
indProvMethodCleanup(CMPIMethodMI * cThis, const CMPIContext *ctx,
                     CMPIBoolean term)
{
  CMReturn(CMPI_RC_OK);
}

CMPIStatus      indProvInvokeMethod
    (CMPIMethodMI * cThis, const CMPIContext *ctx,
     const CMPIResult *rslt, const CMPIObjectPath * cop,
     const char *method, const CMPIArgs * in, CMPIArgs * out) {
  CMPIValue       value;
  fprintf(stderr, "+++ indProvInvokeMethod()\n");

  if (enabled == 0) {
    fprintf(stderr, "+++ PROVIDER NOT ENABLED\n");
  } else {
    generateIndication(method, ctx);
    generateIndication2(method, ctx);
  }

  value.uint32 = 0;
  CMReturnData(rslt, &value, CMPI_uint32);
  CMReturnDone(rslt);
  CMReturn(CMPI_RC_OK);
}

// ----------------------------------------------------------
// ---
// Indication Provider
// ---
// ----------------------------------------------------------

CMPIStatus
indProvIndicationCleanup(CMPIIndicationMI * cThis, const CMPIContext *ctx,
                         CMPIBoolean term)
{
  CMReturn(CMPI_RC_OK);
}

CMPIStatus      indProvAuthorizeFilter
    (CMPIIndicationMI * cThis, const CMPIContext *ctx,
     const CMPISelectExp *filter, const char *clsName,
     const CMPIObjectPath * classPath, const char *owner) {

  char* op = CMGetCharPtr(CMObjectPathToString(classPath, NULL));
  fprintf (stderr, "+++ indProvAuthorizeFilter for %s %s\n", op, clsName);
  CMReturn(CMPI_RC_OK);
}

CMPIStatus      indProvMustPoll
    (CMPIIndicationMI * cThis, const CMPIContext *ctx,
     const CMPISelectExp *filter, const char *clsName,
     const CMPIObjectPath * classPath) {

  char* op = CMGetCharPtr(CMObjectPathToString(classPath, NULL));
  fprintf (stderr, "+++ indProvMustPoll for %s %s\n", op, clsName);
  CMReturn(CMPI_RC_OK);
}

CMPIStatus      indProvActivateFilter
    (CMPIIndicationMI * cThis, const CMPIContext *ctx,
     const CMPISelectExp *exp, const char *clsName,
     const CMPIObjectPath * classPath, CMPIBoolean firstActivation) {

  char* op = CMGetCharPtr(CMObjectPathToString(classPath, NULL));
  fprintf (stderr, "+++ indProvActivateFilter() for %s %s\n", op, clsName);

  if (strcmp(op, "root/interop:Test_Indication") == 0)
    activated = 1;
  else if (strcmp(op, "root/interop2:Test_Indication") == 0)
    activated2 = 1;
  
  CMReturn(CMPI_RC_OK);
}

CMPIStatus      indProvDeActivateFilter
    (CMPIIndicationMI * cThis, const CMPIContext *ctx,
     const CMPISelectExp *filter, const char *clsName,
     const CMPIObjectPath * classPath, CMPIBoolean lastActivation) {
  fprintf(stderr, "+++ indProvDeActivateFilter\n");

  char* op = CMGetCharPtr(CMObjectPathToString(classPath, NULL));
  fprintf (stderr, "+++ indProvDeActivateFilter for %s %s\n", op, clsName);

  if (strcmp(op, "root/interop:Test_Indication") == 0)
    activated = 0;
  else if (strcmp(op, "root/interop2:Test_Indication") == 0)
    activated2 = 0;

  CMReturn(CMPI_RC_OK);
}

CMPIStatus
indProvEnableIndications(CMPIIndicationMI * cThis, const CMPIContext *ctx)
{
  fprintf(stderr, "+++ indProvEnableIndications\n");
  enabled = 1;
  fprintf(stderr, "--- enabled: %d\n", enabled);
  CMReturn(CMPI_RC_OK);
}

CMPIStatus
indProvDisableIndications(CMPIIndicationMI * cThis, const CMPIContext *ctx)
{
  fprintf(stderr, "+++ indProvDisableIndications\n");
  enabled = 0;
  fprintf(stderr, "--- disabled: %d\n", enabled);
  CMReturn(CMPI_RC_OK);
}

// ----------------------------------------------------------
// ---
// Provider Factory Stubs
// ---
// ----------------------------------------------------------

CMMethodMIStub(indProv, TestIndicationProvider, broker, CMNoHook)
    // ----------------------------------------------------------
    CMIndicationMIStub(indProv, TestIndicationProvider, broker, CMNoHook)
    // ----------------------------------------------------------
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
