# $Id: Makefile.am,v 1.105 2010/02/10 20:51:15 buccella Exp $  
#
#  Makefile.am
# 
#   © Copyright IBM Corp. 2009
# 
#  THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
#  ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
#  CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
# 
#  You can obtain a current copy of the Eclipse Public License from
#  http://www.opensource.org/licenses/eclipse-1.0.php
# 
#  Author:        Michael Chase-Salerno <bratac@linux.vnet.ibm.com>
#  Contributions: 
# 
#  Description:
# 
#  Makefile process input for sfcb tests.
# 
# 

MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS=-I$(top_srcdir)
AM_LDFLAGS=-L"$(top_builddir)/.libs"

TESTS_ENVIRONMENT = SFCB_TRACE_FILE="/tmp/sfcbtracetest"

TESTS = xmlUnescape newCMPIInstance EmbeddedTests newDateTime

check_PROGRAMS = xmlUnescape newCMPIInstance EmbeddedTests newDateTime

xmlUnescape_SOURCES = xmlUnescape.c
xmlUnescape_LDADD = -lsfcBrokerCore -lsfcCimXmlCodec

newCMPIInstance_SOURCES = newCMPIInstance.c
newCMPIInstance_LDADD = -lsfcBrokerCore -lsfcCimXmlCodec

EmbeddedTests_SOURCES = EmbeddedTests.c 
EmbeddedTests_LDADD = -lsfcBrokerCore -lsfcCimXmlCodec

newDateTime_SOURCES = newDateTime.c
newDateTime_LDADD = -lsfcBrokerCore
