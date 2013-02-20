#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define TRUE 1
#define FALSE 0

#define CMPI_PLATFORM_LINUX_GENERIC_GNU

#include "native.h"
#include "support.h"
#include "objectImpl.h"

#ifdef SFCB_IX86
#define SFCB_ASM(x) asm(x)
#else
#define SFCB_ASM(x)
#endif

extern CMPIDateTime *NewCMPIDateTime(CMPIStatus *rc);
extern CMPIDateTime *NewCMPIDateTimeFromBinary(CMPIUint64 binTime,
    CMPIBoolean interval, CMPIStatus *rc);
extern CMPIDateTime *NewCMPIDateTimeFromChars(const char *utcTime,
    CMPIStatus *rc);

CMPIDateTime   *dt,
               *dt1,
               *dt2,
               *dt3;
CMPIStatus      st,
                st1,
                st2,
                st3;

int
main(void)
{
  int             rc = 0;
  const char     *str = "00000024163645.123456:000";
  char *current, *result;
  long long blob;

  printf("Performing NewDateTime tests.... \n");
  printf("- Getting current time from NewCMPIDateTime()...\n");
  dt = (CMPIDateTime *) NewCMPIDateTime(&st);
  printf("  Current time in CMPIDateTime: %s\n",
      current = (char*) dt->ft->getStringFormat(dt, NULL )->hdl);
  printf("  Current time in usec since 1970-01-01: %ld\n",
      blob = (long long) dt->ft->getBinaryFormat(dt, NULL ));
  printf(
      "- Converting back to CMPIDateTime using NewCMPIDateTimeFromBinary()...\n");
  dt = (CMPIDateTime *) NewCMPIDateTimeFromBinary((CMPIUint64) blob, FALSE,
      &st1);
  printf("  Convert time in CMPIDateTime: %s\n",
      result = (char*) dt->ft->getStringFormat(dt, NULL )->hdl);
  if (!strcmp(current, result))
    printf("  Result matches the original string.\n");
  else {
    printf("  Result does not match the original string.\n");
    rc = 1;
  }

  printf("Performing interval tests....\n");
  printf("- Getting a 1s interval from NewCMPIDateTime()...\n");
  blob = (CMPIUint64) 1000000;
  dt1 = (CMPIDateTime *) NewCMPIDateTimeFromBinary(blob, TRUE, &st1);
  printf("  Interval in CMPIDateTime: %s\n",
      result = (char*) dt1->ft->getStringFormat(dt1, NULL )->hdl);

  printf("- Getting a test interval from NewCMPIDateTime()...\n");
  dt2 = (CMPIDateTime *) NewCMPIDateTimeFromChars(str, &st2);
  printf("  Interval in CMPIDateTime: %s\n",
      result = (char*) dt2->ft->getStringFormat(dt2, NULL )->hdl);

  printf("Performing NewCMPIDateTimeFromChars() tests....\n");
  printf("- Creating a test date in standard time...\n");
  char cimDt[26];
  char buffr[11];
  long usecs = 0L;
  struct tm bdt;

  memset(&bdt, 0, sizeof(struct tm));
  bdt.tm_isdst = -1;
  bdt.tm_year = 2015 - 1900;
  bdt.tm_mon = 2 - 1;
  bdt.tm_mday = 19;

  mktime(&bdt); // get value of tm_gmtoff

  strftime(cimDt, 26, "%Y%m%d%H%M%S.", &bdt);
  snprintf(buffr, 11, "%6.6ld%+4.3ld", usecs, bdt.tm_gmtoff / 60);
  strcat(cimDt, buffr);
  str = cimDt;
  dt3 = (CMPIDateTime *) NewCMPIDateTimeFromChars(str, &st3);
  printf("  Test date in CMPIDateTime: %s\n",
      (char*) dt3->ft->getStringFormat(dt3, NULL )->hdl);
  printf("  Test date in usec since 1970-01-01: %ld\n",
      blob = (long long) dt3->ft->getBinaryFormat(dt3, NULL ));
  printf(
      "- Converting back to CMPIDateTime using NewCMPIDateTimeFromBinary()...\n");
  dt3 = (CMPIDateTime *) NewCMPIDateTimeFromBinary((CMPIUint64) blob, FALSE,
      &st1);
  printf("  Convert time in CMPIDateTime: %s\n",
      result = (char*) dt3->ft->getStringFormat(dt3, NULL )->hdl);
  if (!strcmp(str, result))
    printf("  Result matches the original string.\n");
  else {
    printf("  Result does not match the original string.\n");
    rc = 1;
  }

  printf("- Creating a test date in daylight saving time...\n");
  bdt.tm_mon = 8 - 1;
  mktime(&bdt);
  strftime(cimDt, 26, "%Y%m%d%H%M%S.", &bdt);
  snprintf(buffr, 11, "%6.6ld%+4.3ld", usecs, bdt.tm_gmtoff / 60);
  strcat(cimDt, buffr);
  str = cimDt;
  dt3 = (CMPIDateTime *) NewCMPIDateTimeFromChars(str, &st3);
  printf("  Test date in CMPIDateTime: %s\n",
      (char*) dt3->ft->getStringFormat(dt3, NULL )->hdl);
  printf("  Test date in usec since 1970-01-01: %ld\n",
      blob = (long long) dt3->ft->getBinaryFormat(dt3, NULL ));
  printf(
      "- Converting back to CMPIDateTime using NewCMPIDateTimeFromBinary()...\n");
  dt3 = (CMPIDateTime *) NewCMPIDateTimeFromBinary((CMPIUint64) blob, FALSE,
      &st1);
  printf("  Convert time in CMPIDateTime: %s\n",
      result = (char*) dt3->ft->getStringFormat(dt3, NULL )->hdl);
  if (!strcmp(str, result))
    printf("  Result matches the original string.\n");
  else {
    printf("  Result does not match the original string.\n");
    rc = 1;
  }

  printf("st.rc = %d \n ", st.rc);
  printf("st1.rc = %d \n ", st1.rc);
  printf("st2.rc = %d \n ", st2.rc);
  if (CMIsInterval((const CMPIDateTime *) dt2, &st2))
    printf("dt2 is Interval...  \n");

  if (st.rc != CMPI_RC_OK) {
    printf("\tNEWCMPIDateTime returned: %s\n", (char *) st.rc);
    rc = 1;
  }
  if (st1.rc != CMPI_RC_OK) {
    printf("\tNEWCMPIDateTimeFromBinary returned: %s\n", (char *) st1.rc);
    rc = 1;
  }
  if (st2.rc != CMPI_RC_OK) {
    printf("\tNEWCMPIDateTimeFromChars returned: %s\n", (char *) st2.rc);
    rc = 1;
  }

  return (rc);
}
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
