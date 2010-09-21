/*
 * For 2169807: XML parser does not handle all character references
 *
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define CMPI_PLATFORM_LINUX_GENERIC_GNU

#include <cimXmlParser.h>
#include <cimRequest.h>

extern RequestHdr scanCimXmlRequest(CimRequestContext *ctx, char *xmlData, int *rc);

int
main(void)
{
  int             rval = 0;
  int             rc = 0;
  CimRequestContext    ctx;
  XmlBuffer *xmb;

  // we'll wrap our test inside a VALUE tag, call scan, and check the
  // results
  char           *thestr =
      "<VALUE>&abc&&def&lt;&#x48;&gt;&#101;&#108;&#108;&#111;&#x20;&quot;&apos;2xspace:&#32;&#x20;2xcrlf:&#xa;&#10;&#32;&#119;&#x4f;&#x52;&#x4C;&#x44;.&#invalidstring;&#no_semi_so_not_valid&#invalid_followed_by_invalid#20;&lt;after_invalid&gt;&#invalid_at_end</VALUE>";
  // char *thestr = "<?xml version=\"1.0\" encoding=\"utf-8\"?><CIM
  // CIMVERSION=\"2.0\" DTDVERSION=\"2.0\"><MESSAGE ID=\"4711\"
  // PROTOCOLVERSION=\"1.0\"><SIMPLEREQ><VALUE>&abc&&def&lt;&#x48;&gt;&#101;&#108;&#108;&#111;&#x20;&quot;&apos;2xspace:&#32;&#x20;2xcrlf:&#xa;&#10;&#32;&#119;&#x4f;&#x52;&#x4C;&#x44;.&#invalidstring;&#no_semi_so_not_valid&#invalid_followed_by_invalid#20;&lt;after_invalid&gt;&#invalid_at_end</VALUE></SIMPLEREQ></MESSAGE></CIM>";
  // printf("The input string is: [%s]\n", thestr);

  char           *expectedResults =
      "<VALUE>&abc&&def<H>ello \"'2xspace:  2xcrlf:\n\n wORLD.&#invalidstring;&#no_semi_so_not_valid&#invalid_followed_by_invalid#20;<after_invalid>&#invalid_at_end";
  // char *expectedResults="<VALUE>&abc&&def<H>ello \"'2xspace:
  // 2xcrlf:\n\n
  // wORLD.&#invalidstring;&#no_semi_so_not_valid&#another_invalid_with_invalid_follow#20;<after_invalid>&#invalid_at_end</VALUE>";
  ctx.contentType="application/xml";

  RequestHdr      results = scanCimXmlRequest(&ctx, thestr, &rc);
  xmb=(XmlBuffer*)results.buffer;

  printf("\"sfcXmlerror: syntax error\" above is expected.\n");
  rval = strcmp(xmb->base, expectedResults);
  if (rval) {
    printf
        ("xmlUnescape Failed...\n\nEXPECTED:    [%s]\n\nRECEIVED:    [%s]\n",
         expectedResults, xmb->base);
    printf("  buffer.last: %s\n", xmb->last);
    printf("  buffer.cur: %s\n", xmb->cur);

  }
  return rval;
}
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
