#include "CimClientLib/cmci.h"
#include "CimClientLib/native.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "show.h"

void
showObjectPath(CMPIObjectPath * objectpath)
{
  CMPIString     *namespace =
      objectpath->ft->getNameSpace(objectpath, NULL);
  CMPIString     *classname =
      objectpath->ft->getClassName(objectpath, NULL);
  int             numkeys = objectpath->ft->getKeyCount(objectpath, NULL);
  int             i;
  char           *cv;

  if (namespace && namespace->hdl) {
    printf("namespace=%s\n", (char *) namespace->hdl);
  }

  if (classname && classname->hdl) {
    printf("classname=%s\n", (char *) classname->hdl);
  }

  if (numkeys) {
    printf("keys:\n");
    for (i = 0; i < numkeys; i++) {
      CMPIString     *keyname;
      CMPIData        data = objectpath->ft->getKeyAt(objectpath, i,
                                                      &keyname, NULL);
      printf("\t%s=%s\n", (char *) keyname->hdl,
             cv = value2Chars(data.type, &data.value));
      if (cv)
        free(cv);
      if (keyname)
        CMRelease(keyname);
    }
  }

  if (classname)
    CMRelease(classname);
  if (namespace)
    CMRelease(namespace);
}

static char    *
CMPIState_str(CMPIValueState state)
{
  char           *retval;

  switch (state) {
  case CMPI_goodValue:
    retval = "CMPI_goodValue";
    break;

  case CMPI_nullValue:
    retval = "CMPI_nullValue";
    break;

  case CMPI_keyValue:
    retval = "CMPI_keyValue";
    break;

  case CMPI_notFound:
    retval = "CMPI_notFound";
    break;

  case CMPI_badValue:
    retval = "CMPI_badValue";
    break;

  default:
    retval = "!Unknown CMPIValueState!";
    break;
  }

  return retval;
}

void
showProperty(CMPIData data, char *name)
{
  char           *valuestr;
  CMPIValueState  state = data.state & ~CMPI_keyValue;
  if (state == CMPI_goodValue) {
    if (CMIsArray(data)) {
      CMPIArray      *arr = data.value.array;
      CMPIType        eletyp = data.type & ~CMPI_ARRAY;
      int             j,
                      n;
      n = CMGetArrayCount(arr, NULL);
      for (j = 0; j < n; ++j) {
        CMPIData        ele = CMGetArrayElementAt(arr, j, NULL);
        valuestr = value2Chars(eletyp, &ele.value);
        printf("\t%s[%d]=%s\n", name, j, valuestr);
        free(valuestr);
      }
    } else {
      if (state == CMPI_goodValue) {
        valuestr = value2Chars(data.type, &data.value);
        printf("\t%s=%s\n", name, valuestr);
        free(valuestr);
      }
    }
  } else {
    printf("\t%s=%d(%s)\n", name, data.state, CMPIState_str(data.state));
  }
}

void
showInstance(CMPIInstance *instance)
{
  CMPIObjectPath *objectpath = instance->ft->getObjectPath(instance, NULL);
  CMPIString     *objectpathname =
      objectpath->ft->toString(objectpath, NULL);
  CMPIString     *namespace =
      objectpath->ft->getNameSpace(objectpath, NULL);
  CMPIString     *classname =
      objectpath->ft->getClassName(objectpath, NULL);
  int             numkeys = objectpath->ft->getKeyCount(objectpath, NULL);
  int             numproperties =
      instance->ft->getPropertyCount(instance, NULL);
  int             i;

  if (objectpathname && objectpathname->hdl) {
    printf("objectpath=%s\n", (char *) objectpathname->hdl);
  }

  if (namespace && namespace->hdl) {
    printf("namespace=%s\n", (char *) namespace->hdl);
  }

  if (classname && classname->hdl) {
    printf("classname=%s\n", (char *) classname->hdl);
  }

  if (numkeys) {
    printf("keys:\n");
    for (i = 0; i < numkeys; i++) {
      CMPIString     *keyname;
      CMPIData        data = objectpath->ft->getKeyAt(objectpath, i,
                                                      &keyname, NULL);
      char           *ptr = NULL;
      printf("\t%s=%s\n", (char *) keyname->hdl,
             (ptr = value2Chars(data.type, &data.value)));
      free(ptr);
      if (keyname)
        CMRelease(keyname);
    }
  } else {
    printf("No keys!\n");
  }

  if (numproperties) {
    printf("properties:\n");
    for (i = 0; i < numproperties; i++) {
      CMPIString     *propertyname;
      CMPIData        data = instance->ft->getPropertyAt(instance, i,
                                                         &propertyname,
                                                         NULL);
      showProperty(data, (char *) propertyname->hdl);
      CMRelease(propertyname);
    }
  } else {
    printf("No properties!\n");
  }

  if (classname)
    CMRelease(classname);
  if (namespace)
    CMRelease(namespace);
  if (objectpathname)
    CMRelease(objectpathname);
  if (objectpath)
    CMRelease(objectpath);
}

char *type2Chars(CMPIType type)
{
   if (type == CMPI_boolean) return "boolean";
   if (type == CMPI_dateTime) return "datetime";
   if (type == CMPI_uint8) return "uint8";
   if (type == CMPI_uint16) return "uint16";
   if (type == CMPI_uint32) return "uint32";
   if (type == CMPI_uint64) return "uint64";
   if (type == CMPI_sint8) return "sint8";
   if (type == CMPI_sint16) return "sint16";
   if (type == CMPI_sint32) return "sint32";
   if (type == CMPI_sint64) return "sint64";
   if (type == CMPI_real32) return "real32";
   if (type == CMPI_real64) return "real64";
   if (type == (CMPI_ARRAY | CMPI_chars)) return "ARRAY of chars";
   if (type == (CMPI_ARRAY | CMPI_string)) return "ARRAY of strings";
   if (type & (CMPI_INTEGER | CMPI_REAL)) return "numeric";
   if (type & CMPI_chars) return "chars";
   if (type & CMPI_string) return "string";
   // TODO: handle remaining types
#define SBUFLEN 32
   char str[SBUFLEN];
   snprintf(str, SBUFLEN, "unknown [CMPIType=%d]", (unsigned short) type);
   return strdup(str);
}

void
showClass(CMPIConstClass * class)
{
  CMPIString     *classname = class->ft->getClassName(class, NULL);
  int             numproperties = class->ft->getPropertyCount(class, NULL);
  int             numqualifiers = class->ft->getQualifierCount(class, NULL);
  int             nummethods = class->ft->getMethodCount(class, NULL);
  int             numparameters;
  int             i, j;
  char           *cv;

  if (classname && classname->hdl) {
    printf("classname=%s\n", (char *) classname->hdl);
  }

  if (numproperties) {
    printf("properties:\n");
    for (i = 0; i < numproperties; i++) {
      CMPIString     *propertyname;
      CMPIData        data = class->ft->getPropertyAt(class, i,
                                                      &propertyname, NULL);
      if (propertyname) {
        CMPIData        data = class->ft->getPropertyQualifier(class,
                                                               (char *)
                                                               propertyname->hdl,
                                                               "KEY",
                                                               NULL);
        if (data.state != CMPI_nullValue && data.value.boolean) {
          printf("[KEY]");
        }
      }
      if (data.state == 0) {
        printf("\t%s=%s\n", (char *) propertyname->hdl,
               cv = value2Chars(data.type, &data.value));
        if (cv)
          free(cv);
      } else
        printf("\t%s=NIL\n", (char *) propertyname->hdl);
      if (propertyname) {
        CMRelease(propertyname);
      }
    }
  }

  if (numqualifiers)
  {
    printf("qualifiers:\n");
    for (i=0; i<numqualifiers; i++)
    {
      CMPIString * qualifiername;
      CMPIData data = class->ft->getQualifierAt(class, i, &qualifiername, NULL);

      if (data.state==0)
      {
        cv = value2Chars(data.type, &data.value);
        printf("\t%s=\"%.60s%s\n", (char *) qualifiername->hdl, cv,
            (strlen(cv) > 60) ? "...\"" : "\"");
        if(cv) free(cv);      
      }
      else
        printf("\t%s=NIL\n", (char *)qualifiername->hdl);

      if (qualifiername) CMRelease(qualifiername);
    }
  }

  if (nummethods)
  {
    printf("methods:\n");
    for (i=0; i<nummethods; i++)
    {
      CMPIString * methodname;
      CMPIData data = class->ft->getMethodAt(class, i, &methodname, NULL);

      printf("\tMethod=%s (%s)\n", (char *) methodname->hdl,
          type2Chars(data.type));

      numparameters = class->ft->getMethodParameterCount(class,
          (char *) methodname->hdl, NULL );

      if (numparameters)
      {
        printf("\tmethod parameters:\n");
        for (j=0; j<numparameters; j++)
        {
          CMPIString * parametername;
          CMPIData data = class->ft->getMethodParameterAt(class,
              (char *) methodname->hdl, j, &parametername, NULL );

          printf("\t\t%s (%s)\n", (char *) parametername->hdl,
              type2Chars(data.type));

          if (parametername) CMRelease(parametername);
        }
      }

      numqualifiers = class->ft->getMethodQualifierCount(class,
          (char *) methodname->hdl, NULL );

      if (numqualifiers)
      {
        printf("\tmethod qualifiers:\n");
        for (j=0; j<numqualifiers; j++)
        {
          CMPIString * qualifiername;
          CMPIData data = class->ft->getMethodQualifierAt(class,
              (char *) methodname->hdl, j, &qualifiername, NULL );

          if (data.state==0)
          {
            if (data.type & CMPI_ARRAY) {
              // TODO: properly print array values here
              printf("\t\t%s (%s)\n", (char *) qualifiername->hdl,
                  type2Chars(data.type));
            }
            else {
              cv = value2Chars(data.type, &data.value);
              printf("\t\t%s (%s)=\"%.60s%s\n", (char *) qualifiername->hdl,
                  type2Chars(data.type), cv,
                  (strlen(cv) > 60) ? "...\"" : "\"");
              if(cv) free(cv);
            }
          }
          else
            printf("\t\t%s=NIL\n", (char *)qualifiername->hdl);

          if (qualifiername) CMRelease(qualifiername);
        }
      }

      if (methodname) CMRelease(methodname);
    }
  }

  if (classname)
    CMRelease(classname);
}
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
