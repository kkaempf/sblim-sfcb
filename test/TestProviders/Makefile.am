# $Id: Makefile.am,v 1.105 2010/02/10 20:51:15 buccella Exp $  
#
#  Makefile.am
# 
#   © Copyright IBM Corp. 2013
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
#  Makefile process input for sfcb testProviders tests.
# 
# 

MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS=-I$(top_srcdir)
AM_LDFLAGS=-L"$(top_builddir)/.libs"

sfcbprovtestdir=$(libdir)/sfcb

sfcbprovtest_LTLIBRARIES = \
   libTestAssociationProvider.la \
   libTestMethodProvider.la \
   libTestInstanceProvider.la \
   libTestPulledInstanceProvider.la \
   libTestMiscProvider.la \
   libTestExecQueryProvider.la \
   libTestIndicationProvider.la

libTestAssociationProvider_la_SOURCES = cmpiTestAssociationProvider.c
libTestAssociationProvider_la_LIBADD = -lsfcBrokerCore

libTestMethodProvider_la_SOURCES = cmpiTestMethodProvider.c
libTestMethodProvider_la_LIBADD = -lsfcBrokerCore

libTestInstanceProvider_la_SOURCES = cmpiTestInstanceProvider.c
libTestInstanceProvider_la_LIBADD = -lsfcBrokerCore

libTestPulledInstanceProvider_la_SOURCES = cmpiTestPulledInstanceProvider.c
libTestPulledInstanceProvider_la_LIBADD = -lsfcBrokerCore

libTestMiscProvider_la_SOURCES = cmpiTestMiscProvider.c
libTestMiscProvider_la_LIBADD = -lsfcBrokerCore

libTestExecQueryProvider_la_SOURCES = cmpiTestExecQueryProvider.c
libTestExecQueryProvider_la_LIBADD = -lsfcBrokerCore

libTestIndicationProvider_la_SOURCES = cmpiTestIndicationProvider.c
libTestIndicationProvider_la_LIBADD = -lsfcBrokerCore

