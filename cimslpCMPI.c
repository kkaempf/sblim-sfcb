/*
 * cimslpCMPI.c
 *
 * (C) Copyright IBM Corp. 2006
 *
 * THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
 * ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
 * CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
 *
 * You can obtain a current copy of the Eclipse Public License from
 * http://www.opensource.org/licenses/eclipse-1.0.php
 *
 * Author:        Sven Schuetz <sven@de.ibm.com>
 * Contributions:
 *
 * Description:
 *
 * Functions getting slp relevant information from the CIMOM
 *
 */

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "cimslpCMPI.h"
#include "cimslpUtil.h"
#include "trace.h"
#include "config.h"
#include "control.h"

/* TODO: stick this in a header for all of SFCB */
char           *interOpNS = "root/interop";
typedef int     (*getSlpHostname) (char **hostname);
extern void     libraryName(const char *dir, const char *location,
                            const char *fullName, int buf_size);
extern char    *configfile;
CMPIContext    *prepareUpcall(const CMPIContext *ctx);

extern int getCustomSlpHostname(char **hn);

/* this is pulled from SFCC; 
   TODO: combine this with SFCC's (and value.c's as well)
 */
static char * value2Chars (CMPIType type, CMPIValue *value)
{
  char str[2048], *p;
  CMPIString *cStr;

  str[0]=0;
  if (type & CMPI_ARRAY) {
  }
  else if (type & CMPI_ENC) {

    switch (type) {
    case CMPI_instance:
      break;

    case CMPI_ref:
      if (value->ref) {
	cStr=value->ref->ft->toString(value->ref,NULL);
	p = strdup((char *) cStr->hdl);
	CMRelease(cStr);
      } else {
	p = strdup("NULL");
      }

      return p;

    case CMPI_args:
      break;

    case CMPI_filter:
      break;

    case CMPI_string:
    case CMPI_numericString:
    case CMPI_booleanString:
    case CMPI_dateTimeString:
    case CMPI_classNameString:
      return strdup((value->string && value->string->hdl) ?
		    (char*)value->string->hdl : "NULL");

    case CMPI_dateTime:
      if (value->dateTime) {
	cStr=CMGetStringFormat(value->dateTime,NULL);
	p = strdup((char *) cStr->hdl);
	CMRelease(cStr);
      } else
	p = strdup("NULL");
      return p;
    }

  }
  else if (type & CMPI_SIMPLE) {

    switch (type) {
    case CMPI_boolean:
      return strdup(value->boolean ? "true" : "false");

    case CMPI_char16:
      break;
    }

  }
  else if (type & CMPI_INTEGER) {

    switch (type) {
    case CMPI_uint8:
      sprintf(str, "%u", value->uint8);
      return strdup(str);
    case CMPI_sint8:
      sprintf(str, "%d", value->sint8);
      return strdup(str);
    case CMPI_uint16:
      sprintf(str, "%u", value->uint16);
      return strdup(str);
    case CMPI_sint16:
      sprintf(str, "%d", value->sint16);
      return strdup(str);
    case CMPI_uint32:
      sprintf(str, "%u", value->uint32);
      return strdup(str);
    case CMPI_sint32:
      sprintf(str, "%d", value->sint32);
      return strdup(str);
    case CMPI_uint64:
      sprintf(str, "%llu", value->uint64);
      return strdup(str);
    case CMPI_sint64:
      sprintf(str, "%lld", value->sint64);
      return strdup(str);
    }

  }
  else if (type & CMPI_REAL) {

    switch (type) {
    case CMPI_real32:
      sprintf(str, "%g", value->real32);
      return strdup(str);
    case CMPI_real64:
      sprintf(str, "%g", value->real64);
      return strdup(str);
    }

  }
  return strdup(str);
}

// transforms numerical values into their string counterpart
// utilizing the Values and ValueMap qualifiers
char*
transformValue(char *cssf, CMPIObjectPath * op, char *propertyName)
{
  CMPIData        qd;
  CMPIStatus      status;
  char           *valuestr;

  _SFCB_ENTER(TRACE_SLP, "transformValue");

  qd = CMGetPropertyQualifier(op, propertyName, "ValueMap", &status);
  if (status.rc) {
    printf("getPropertyQualifier failed ... Status: %d\n", status.rc);
    return NULL;
  }

  if (CMIsArray(qd)) {
    CMPIArray      *arr = qd.value.array;
    CMPIType        eletyp = qd.type & ~CMPI_ARRAY;
    int             j = 0;
    int             n;
    n = CMGetArrayCount(arr, NULL);
    CMPIData        ele;
    ele = CMGetArrayElementAt(arr, j, NULL);
    valuestr = value2Chars(eletyp, &ele.value);
    j++;
    while (strcmp(valuestr, cssf)) {
      free(valuestr);
      ele = CMGetArrayElementAt(arr, j, NULL);
      valuestr = value2Chars(eletyp, &ele.value);
      if (j == n) {
        free(valuestr);
        return cssf;     // nothing found, probably "NULL" ->
        // return it
      }
      j++;
    }
    free(valuestr);
    free(cssf);
    if (j - 1 <= n) {
      qd = CMGetPropertyQualifier(op, propertyName, "Values", &status);
      arr = qd.value.array;
      eletyp = qd.type & ~CMPI_ARRAY;
      ele = CMGetArrayElementAt(arr, j - 1, NULL);
      cssf = value2Chars(eletyp, &ele.value);
      return cssf;
    } else {
      // printf("No Valuemap Entry for %s in %s. Exiting ...\n", cssf,
      // propertyName);
      return NULL;
    }
  }

  else {
    // printf("No qualifier found for %s. Exiting ...\n", propertyName);
    return NULL;
  }
}

static void
transformValueArray(char **cssf, CMPIObjectPath * op, char *propertyName)
{
  int             i;

  for (i = 0; cssf[i] != NULL; i++) {
    cssf[i] = transformValue(cssf[i], op, propertyName);
  }
  return;
}

static CMPIInstance  **
myGetInstances(const CMPIBroker *_broker, const CMPIContext * ctx,
               const char *path, const char *objectname, const char** props)
{
  CMPIStatus      status;
  CMPIObjectPath *objectpath;
  CMPIEnumeration *enumeration;
  CMPIInstance  **retArr = NULL;

  _SFCB_ENTER(TRACE_SLP, "myGetInstances");

  objectpath = CMNewObjectPath(_broker, path, objectname, NULL);

  enumeration = CBEnumInstances(_broker, ctx, objectpath, props, &status);
  if (status.rc != CMPI_RC_OK) {
    retArr = NULL;
  }

  if (!status.rc) {
    if (CMHasNext(enumeration, NULL)) {
      CMPIArray      *arr;
      int             n,
                      i;

      arr = CMToArray(enumeration, NULL);
      n = CMGetArrayCount(arr, NULL);
      retArr = malloc(sizeof(CMPIInstance *) * (n + 1));
      for (i = 0; i < n; i++) {
        CMPIData        ele = CMGetArrayElementAt(arr, i, NULL);
        retArr[i] = CMClone(ele.value.inst, NULL);
      }
      retArr[n] = NULL;
    }
  }
  if (status.msg)
    CMRelease(status.msg);
  if (objectpath)
    CMRelease(objectpath);
  if (enumeration)
    CMRelease(enumeration);
  _SFCB_RETURN(retArr);
}

static char          **
myGetRegProfiles(const CMPIBroker *_broker, CMPIInstance **instances,
                 const CMPIContext * ctx)
{
  CMPIObjectPath *objectpath;
  CMPIEnumeration *enumeration = NULL;
  CMPIStatus      status;
  char          **retArr;
  int             i,
                  j = 0;

  _SFCB_ENTER(TRACE_SLP, "myGetRegProfiles");

  // count instances
  for (i = 0; instances != NULL && instances[i] != NULL; i++) {
  }

  if (i == 0) {
    _SFCB_RETURN(NULL);;
  }
  // allocating memory for the return array
  // a little too much memory will be allocated, since not each instance
  // is a RegisteredProfile, for which a
  // string needs to be constructed ... but allocating dynamically would
  // involve too much burden and overhead (?)

  retArr = (char **) malloc((i + 1) * sizeof(char *));

  for (i = 0; instances[i] != NULL; i++) {

    // check to see if this profile wants to be advertised
    CMPIArray      *atArray;
    atArray =
        CMGetProperty(instances[i], "AdvertiseTypes", &status).value.array;
    if (status.rc == CMPI_RC_ERR_NO_SUCH_PROPERTY || atArray == NULL
        || CMGetArrayElementAt(atArray, 0, &status).value.uint16 != 3) {

      _SFCB_TRACE(1,
                  ("--- profile does not want to be advertised; skipping"));
      continue;
    }

    objectpath = instances[i]->ft->getObjectPath(instances[i], &status);
    if (status.rc) {
      // no object path ??
      free(retArr);
      _SFCB_RETURN(NULL);
    }
    objectpath->ft->setNameSpace(objectpath, interOpNS);

    if (enumeration)
      CMRelease(enumeration);
    enumeration = CBAssociatorNames(_broker, ctx,
                                    objectpath,
                                    "CIM_SubProfileRequiresProfile",
                                    NULL, "Dependent", NULL, NULL);

    // if the result is not null, we are operating on a
    // CIM_RegisteredSubProfile, which we don't want
    if (!enumeration || !CMHasNext(enumeration, &status)) {
      CMPIData        propertyData;

      propertyData = instances[i]->ft->getProperty(instances[i],
                                                   "RegisteredOrganization",
                                                   &status);
      char           *profilestring;
      profilestring = value2Chars(propertyData.type, &propertyData.value);

      profilestring = transformValue(profilestring, CMGetObjectPath(instances[i], NULL),
		     "RegisteredOrganization");

      propertyData = instances[i]->ft->getProperty(instances[i],
                                                   "RegisteredName",
                                                   &status);

      char *tempString = value2Chars(propertyData.type, &propertyData.value);

      profilestring = realloc(profilestring,
			      strlen(profilestring) + strlen(tempString) + 2);
      profilestring = strcat(profilestring, ":");
      profilestring = strcat(profilestring, tempString);
      free(tempString);

      // now search for a CIM_RegisteredSubProfile for this instance
      if (enumeration)
        CMRelease(enumeration);
      enumeration = CBAssociators(_broker, ctx, objectpath,
                                  "CIM_SubProfileRequiresProfile",
                                  NULL, "Antecedent", NULL, NULL, NULL);
      if (!enumeration || !CMHasNext(enumeration, NULL)) {
        retArr[j] = strdup(profilestring);
        j++;
      } else
        while (CMHasNext(enumeration, &status)) {
          CMPIData data = CMGetNext(enumeration, NULL);
          propertyData = CMGetProperty(data.value.inst, "RegisteredName", &status);
          char *subprofilestring = value2Chars(propertyData.type, &propertyData.value);
          retArr[j] = (char *) malloc(strlen(profilestring) + strlen(subprofilestring) + 2);
          sprintf(retArr[j], "%s:%s", profilestring, subprofilestring);
          j++;
          free(subprofilestring);
        }
      free(profilestring);
    }
    if (objectpath)
      CMRelease(objectpath);
  }
  retArr[j] = NULL;

  if (enumeration)
    CMRelease(enumeration);
  if (status.msg)
    CMRelease(status.msg);

  _SFCB_RETURN(retArr);
}

static char           *
myGetProperty(CMPIInstance *instance, char *propertyName)
{

  CMPIData        propertyData;
  CMPIStatus      status;

  if (!instance)
    return NULL;

  propertyData = instance->ft->getProperty(instance, propertyName, &status);

  if (!status.rc) {
    return value2Chars(propertyData.type, &propertyData.value);
  } else {
    return NULL;
  }
}

static char          **
myGetPropertyArrayFromArray(CMPIInstance **instances, char *propertyName)
{
  int             i;
  char          **propertyArray;

  // count elements
  for (i = 0; instances != NULL && instances[i] != NULL; i++) {
  }

  if (i == 0) {
    return NULL;
  }

  propertyArray = malloc((i + 1) * sizeof(char *));

  for (i = 0; instances[i] != NULL; i++) {
    propertyArray[i] = myGetProperty(instances[i], propertyName);
  }
  propertyArray[i] = NULL;
  return propertyArray;

}

static char          **
myGetPropertyArray(CMPIInstance *instance, char *propertyName)
{

  CMPIData        propertyData;
  CMPIStatus      status;
  char          **propertyArray = NULL;

  propertyData = instance->ft->getProperty(instance, propertyName, &status);
  if (!status.rc && propertyData.state != CMPI_nullValue) {
    if (CMIsArray(propertyData)) {
      CMPIArray      *arr = propertyData.value.array;
      CMPIType        eletyp = propertyData.type & ~CMPI_ARRAY;
      int             n,
                      i;
      n = CMGetArrayCount(arr, NULL);
      propertyArray = malloc(sizeof(char *) * (n + 1));
      for (i = 0; i < n; i++) {
        CMPIData        ele = CMGetArrayElementAt(arr, i, NULL);
        propertyArray[i] = value2Chars(eletyp, &ele.value);
      }
      propertyArray[n] = NULL;

    }
  }
  return propertyArray;
}

static char           *
getUrlSyntax(char *sn, char *cs, char *port)
{
  char           *url_syntax;

  // colon, double slash, colon, \0, service:wbem = 18
  url_syntax =
      (char *) malloc((strlen(sn) + strlen(cs) + strlen(port) + 18) * sizeof(char));
  sprintf(url_syntax, "%s://%s:%s", cs, sn, port);

  free(sn);

  return url_syntax;
}

static void
buildAttrString(char *name, char *value, char *attrstring)
{
  int length;
  int size = 1024;

  if (value == NULL) {
    return;
  }

  length = strlen(attrstring) + strlen(value) + strlen(name) + 5;

  if (length > size) {
    // make sure that string is big enough to hold the result
    // multiply with 3 so that we do not have to enlarge the next run
    // already
    size = size + (length * 3);
    attrstring = (char *) realloc(attrstring, size * sizeof(char));
  }

  if (strlen(attrstring) != 0) {
    strcat(attrstring, ",");
  }

  sprintf(attrstring, "%s(%s=%s)", attrstring, name, value);

  return;
}

static void
buildAttrStringFromArray(char *name, char **value, char *attrstring)
{
  int             length = 0;
  int             i;
  int             finalAttrLen = 0;
  int size = 1024;

  if (value == NULL) {
    return;
  }

  for (i = 0; value[i] != NULL; i++) {
    length += strlen(value[i]);
  }

  // Account for the comma delimiters which will be inserted into the
  // string between each element in the array, one per value array entry. 
  // Err on the side of caution and still count the trailing comma, though 
  // it will be clobbered by the final ")" at the very end.
  length += i;

  length = length + strlen(attrstring) + strlen(name) + 5;

  if (length > size) {
    // make sure that string is big enough to hold the result multiply with 
    // 3 so that we do not have to enlarge the next run already
    size = size + (length * 3);
    attrstring = (char *) realloc(attrstring, size * sizeof(char));
  }

  if (strlen(attrstring) != 0) {
    strcat(attrstring, ",");
  }

  strcat(attrstring, "(");
  strcat(attrstring, name);
  strcat(attrstring, "=");
  for (i = 0; value[i] != NULL; i++) {
    strcat(attrstring, value[i]);
    strcat(attrstring, ",");
  }
  // Includes the trailing ",", which must be replaced by a ")" followed
  // by a NULL
  // string delimiter.
  finalAttrLen = strlen(attrstring);
  attrstring[finalAttrLen - 1] = ')';
  attrstring[finalAttrLen] = '\0';

  if (finalAttrLen + 1 > size) {
    // buffer overrun. Better to abort here rather than discovering a heap 
    // 
    // 
    // curruption later
    printf
        ("--- Error:  Buffer overrun in %s. Content size: %d  Buffer size: %d\n",
         "buildAttrStringFromArray", finalAttrLen + 1, size);
    abort();
  }

  return;
}

static const char* ATTR_PREFIX = "(template-type=wbem),(template-version=1.0),(template-description=This template describes the attributes used for advertising WBEM Servers.)";

char*
getSLPData(cimomConfig cfg, const CMPIBroker *_broker, const CMPIContext *ctx, char** url_syntax)
{
  CMPIInstance  **ci;
  char           *sn;

  // const char **pf_om;
  // pf_om = malloc(sizeof(char *) * 4);
  // pf_om[0] = "SystemName";
  // pf_om[1] = "ElementName";
  // pf_om[2] = "Description";
  // pf_om[3] = "Name";

  _SFCB_ENTER(TRACE_SLP, "getSLPData");

  char* attrstring;
  int size = 1024;
  attrstring = malloc(sizeof(char) * size);
  attrstring[0] = 0;

  strcpy(attrstring, ATTR_PREFIX);

  // extract all relavant stuff for SLP out of CIM_ObjectManager

  // construct the server string
  // TODO: add property filter
  ci = myGetInstances(_broker, ctx, interOpNS, "CIM_ObjectManager", NULL);
  if (ci) {
    sn = myGetProperty(ci[0], "SystemName");

#ifdef SLP_HOSTNAME_LIB
  getCustomSlpHostname(&sn);
#endif
    *url_syntax = getUrlSyntax(sn, cfg.commScheme, cfg.port);
    buildAttrString("template-url-syntax", *url_syntax, attrstring);

    char* service_hi_name = myGetProperty(ci[0], "ElementName");
    buildAttrString("service-hi-name", service_hi_name, attrstring);
    free(service_hi_name);

    char* service_hi_description = myGetProperty(ci[0], "Description");
    buildAttrString("service-hi-description", service_hi_description, attrstring);
    free(service_hi_description);

    char* service_id = myGetProperty(ci[0], "Name");
    buildAttrString("service-id", service_id, attrstring);
    free(service_id);

    freeInstArr(ci);
  }
  // extract all relavant stuff for SLP out of
  // CIM_ObjectManagerCommunicationMechanism
  ci = myGetInstances(_broker, ctx, interOpNS, "CIM_ObjectManagerCommunicationMechanism", NULL);
  if (ci) {
    char* CommunicationMechanism = myGetProperty(ci[0], "CommunicationMechanism");
    // do the transformations from numbers to text via the qualifiers
    CommunicationMechanism = transformValue(CommunicationMechanism, CMGetObjectPath(ci[0], NULL), "CommunicationMechanism");
    buildAttrString("CommunicationMechanism", CommunicationMechanism, attrstring);
    free(CommunicationMechanism);

    char* OtherCommunicationMechanismDescription = myGetProperty(ci[0], "OtherCommunicationMechanism");
    buildAttrString("OtherCommunicationMechanismDescription", OtherCommunicationMechanismDescription, attrstring);
    free(OtherCommunicationMechanismDescription);

    buildAttrString("InteropSchemaNamespace", interOpNS, attrstring);

    char* ProtocolVersion = myGetProperty(ci[0], "Version");
    buildAttrString("ProtocolVersion", ProtocolVersion, attrstring);
    free(ProtocolVersion);

    char** FunctionalProfilesSupported = myGetPropertyArray(ci[0], "FunctionalProfilesSupported");
    transformValueArray(FunctionalProfilesSupported, CMGetObjectPath(ci[0], NULL), "FunctionalProfilesSupported");
    buildAttrStringFromArray("FunctionalProfilesSupported",  FunctionalProfilesSupported, attrstring);
    freeArr(FunctionalProfilesSupported);

    char** FunctionalProfileDescriptions = myGetPropertyArray(ci[0], "FunctionalProfileDescriptions");
    buildAttrStringFromArray("FunctionalProfileDescriptions", FunctionalProfileDescriptions, attrstring);
    freeArr(FunctionalProfileDescriptions);

    char* MultipleOperationsSupported = myGetProperty(ci[0], "MultipleOperationsSupported");
    buildAttrString("MultipleOperationsSupported", MultipleOperationsSupported, attrstring);
    free(MultipleOperationsSupported);

    char** AuthenticationMechanismsSupported = myGetPropertyArray(ci[0], "AuthenticationMechanismsSupported");
    transformValueArray(AuthenticationMechanismsSupported, CMGetObjectPath(ci[0], NULL),
			"AuthenticationMechanismsSupported");
    buildAttrStringFromArray("AuthenticationMechanismsSupported", AuthenticationMechanismsSupported, attrstring);
    freeArr(AuthenticationMechanismsSupported);

    char** AuthenticationMechansimDescriptions = myGetPropertyArray(ci[0], "AuthenticationMechansimDescriptions");
    buildAttrStringFromArray("AuthenticationMechansimDescriptions", AuthenticationMechansimDescriptions, attrstring);
    freeArr(AuthenticationMechansimDescriptions);

    freeInstArr(ci);
  }
  // extract all relavant stuff for SLP out of CIM_Namespace
  ci = myGetInstances(_broker, ctx, interOpNS, "CIM_Namespace", NULL);
  if (ci) {
    char** Namespace = myGetPropertyArrayFromArray(ci, "Name");
    buildAttrStringFromArray("Namespace", Namespace, attrstring);
    freeArr(Namespace);

    char** Classinfo = myGetPropertyArrayFromArray(ci, "ClassInfo");
    buildAttrStringFromArray("Classinfo", Classinfo, attrstring);
    freeArr(Classinfo);

    freeInstArr(ci);
  }

  // extract all relavant stuff for SLP out of CIM_RegisteredProfile
  ci = myGetInstances(_broker, ctx, interOpNS, "CIM_RegisteredProfile", NULL);
  if (ci) {
    char** RegisteredProfilesSupported = myGetRegProfiles(_broker, ci, ctx);
    buildAttrStringFromArray("RegisteredProfilesSupported", RegisteredProfilesSupported, attrstring);
    freeArr(RegisteredProfilesSupported);

    freeInstArr(ci);
  }

  return attrstring;
}

/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
