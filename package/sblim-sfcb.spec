#
# spec file for package sblim-sfcb (Version 1.3.7)
#
# Copyright (c) 2010 SUSE LINUX Products GmbH, Nuernberg, Germany.
#
# All modifications and additions to the file contributed by third parties
# remain the property of their copyright owners, unless otherwise agreed
# upon. The license for this file, and modifications and additions to the
# file, is the same license as for the pristine package itself (unless the
# license for the pristine package is not an Open Source License, in which
# case the license is the MIT License). An "Open Source License" is a
# license that conforms to the Open Source Definition (Version 1.9)
# published by the Open Source Initiative.

# Please submit bugfixes or comments via http://bugs.opensuse.org/
#


BuildRoot:      %{_tmppath}/%{name}-%{version}-build
Summary:        Small Footprint CIM Broker

Name:           sblim-sfcb
Version:        1.3.7
Release:        0.<RELEASE9>
Group:          System/Management
License:        Other uncritical OpenSource License; "CPL 1.0 ..."; CPL 1.0
Url:            http://sblim.sf.net/
Source0:        %{name}-%{version}.tar.bz2
Source1:        autoconfiscate.sh
%if 0%{?suse_version}
Source2:        sblim-sfcb.init
%endif
Source3:        autoconfiscate.sh-mofc
Source4:        sfcb-pam.conf
Source5:        %{name}-rpmlintrc
Source6:        susefirewall.conf
Patch1:         0001-uds_auth.patch
Patch7:         0007-automake.patch
Patch8:         0008-enable-hex-trace-mask.patch
Patch12:        0012-check-prevent-various-buffer-overflows.patch
Patch17:        0017-abort-on-socket-error-with-better-error-msg.patch
Patch18:        0090-buffer-size-check-in-localConnectServer.patch
Patch390:       0390-control_c_limit_h.patch

# dont wget the cim-schema
Patch420:       0420-minimal-postinstall.patch

# print path failing opendir()
Patch430:       0430-fileRepository-opendir-error.patch

# from http://github.com/kkaempf/sblim-sfcb/branches/sles11-sp1
Patch10001:     10001-2949454-Memory-leak-in-ServerProviderInitInstances.patch
Patch10002:     10002-2950773-Leak-in-indCIMXMLHandler.c.patch
Patch10003:     10005-2952912-resultSockets-variable-is-not-used-threadsaf.patch
Patch10004:     10006-2952616-internalProvider-is-not-Threadsafe.patch
Patch10005:     10007-2948647-getObjectPath-may-dereference-NULL-pointer.patch
Patch10006:     10008-2967257-prefer-CMPI_chars-over-CMPI_classNameString.patch
Patch10007:     10009-2968656-bnc578189-clone-cmpi_chars-return-value-for-.patch

Patch11000:     11000-2980524-bnc591396-Collate-namespaces-in-providerRegister-correctly.patch
Patch11001:     11001-2980524-bnc591396-Check-for-conflicting-provider-registrations.patch
Patch11002:     11002-2980524-bnc591396-Release-merged-ProviderInfo-when-collating-namespace.patch

Patch12000:     12000-2984214-bnc595258-double-free-error-cimXmlGen.c-triggered-by-Assocator.patch

Patch13000:     13000-2978930-bnc593168-Reduce-memory-leak-in-slp-operation.patch

Patch14000:     14000-2984436-bnc591060-Match-startLogging-and-closeLogging-calls.patch

Provides:       cimserver
Provides:       cim-server
%if 0%{?suse_version} >= 1030
BuildRequires:  libcurl-devel
%else
BuildRequires:  curl-devel
%endif
BuildRequires:  libtool
BuildRequires:  zlib-devel
BuildRequires:  openssl-devel
BuildRequires:  pam-devel
BuildRequires:  cim-schema
BuildRequires:  sblim-sfcc-devel
%if 0%{?rhel_version} != 501
BuildRequires:  openslp-devel
%endif
BuildRequires:  bison flex
BuildRequires:  unzip

Requires:       curl
%if 0%{?suse_version} < 1120
# unneeded explicit lib dependency
Requires:       zlib
%endif
Requires:       openssl
Requires:       pam
# Added NWP - dependency on cim-schema instead of inbuilt schema
Requires:       cim-schema
PreReq:         /usr/sbin/groupadd /usr/sbin/groupmod

%description
Small Footprint CIM Broker (sfcb) is a CIM server conforming to the CIM
Operations over HTTP protocol. It is robust, with low resource
consumption and therefore specifically suited for embedded and resource
constrained environments. sfcb supports providers written against the
Common Manageability Programming Interface (CMPI).



%prep
%setup -q
%patch1 -p1 -b .0001-uds_auth.patch
%patch7 -p1 -b .0007-automake.patch
%patch8 -p1 -b .0008-enable-hex-trace-mask.patch
%patch12 -p1 -b .0012-check-prevent-various-buffer-overflows.patch
%patch17 -p1 -b .0017-abort-on-socket-error-with-better-error-msg.patch
%patch18 -p1 -b .0090-buffer-size-check-in-localConnectServer.patch
%patch390 -p1 -b .0390-control_c_limit_h.patch
%patch420 -p0 -b .0420-minimal-postinstall.patch
%patch430 -p0 -b .0430-fileRepository-opendir-error.patch

%patch10001 -p1
%patch10002 -p1
%patch10003 -p1
%patch10004 -p1
%patch10005 -p1
%patch10006 -p1
%patch10007 -p1

%patch11000 -p1
%patch11001 -p1
%patch11002 -p1

%patch12000 -p1

%patch13000 -p1

%patch14000 -p1

export PATCH_GET=0

%build
#autoreconf -f -i
cp %SOURCE1 .
cp %SOURCE3 mofc/autoconfiscate.sh
chmod +x mofc/autoconfiscate.sh
# 1.3.6 tarball seems incomplete. (issue #2931327 upstream)
# might be able to remove the following workaround in a future version. 
mkdir -p test/finaltest
mkdir -p test/TestProviders/tests
mkdir -p test/commands
mkdir -p test/wbemcli
mkdir -p test/xmltest
mkdir -p test/unittest
mkdir -p test/slptest
mkdir -p test/localtests
touch test/finaltest/Makefile.in
touch test/TestProviders/tests/Makefile.in
touch test/commands/Makefile.in
touch test/wbemcli/Makefile.in
touch test/xmltest/Makefile.in
touch test/unittest/Makefile.in
touch test/slptest/Makefile.in
touch test/localtests/Makefile.in
sh ./autoconfiscate.sh
#if test -d mofc; then cd mofc && autoreconf -f -i; fi
#%%configure --enable-debug --enable-ssl --enable-pam --enable-ipv6 CIMSCHEMA_SOURCE=%{SOURCE1} CIMSCHEMA_MOF=cimv216.mof CIMSCHEMA_SUBDIRS=y
mkdir -p m4
%if 0%{?rhel_version} == 501
WITH_SLP=
%else
WITH_SLP=--enable-slp
%endif
%configure --enable-debug --enable-ssl --enable-pam --enable-ipv6 \
            --enable-uds $WITH_SLP
make

%install
%makeinstall
make postinstall DESTDIR=$RPM_BUILD_ROOT
# comment out - NWP - removing schema pkg
#make DESTDIR=$RPM_BUILD_ROOT install-cimschema
# remove docs from wrong dir.  They are handled by %doc macro in files list
rm -r $RPM_BUILD_ROOT/usr/share/doc
# remove unused libtool files
rm -f $RPM_BUILD_ROOT/%{_libdir}/*a
# make the cmpi directory that sfcb will own - for SuSE Autobuild checks of rpm directory ownership
mkdir $RPM_BUILD_ROOT/%{_libdir}/cmpi
%if 0%{?suse_version}
# override the default-installed sfcb init script - use the one from Source2
# due to /etc/SuSE-release not available in autobuild, so won't install
# correct init script
install %SOURCE2 $RPM_BUILD_ROOT/etc/init.d/sfcb
ln -s /etc/init.d/sfcb $RPM_BUILD_ROOT/usr/sbin/rcsfcb
%endif
# Added NWP 5/14/08 - transition to using cim-schema rpm instead of internal-built schema
ln -sf /usr/share/mof/cim-current $RPM_BUILD_ROOT/%{_datadir}/sfcb/CIM
install -m 0644 %SOURCE4 $RPM_BUILD_ROOT/etc/pam.d/sfcb
rm $RPM_BUILD_ROOT%{_libdir}/sfcb/*.la
%if 0%{?suse_version}
# firewall service definition
mkdir -p $RPM_BUILD_ROOT/etc/sysconfig/SuSEfirewall2.d/services
install -m 0644 %SOURCE6 $RPM_BUILD_ROOT/etc/sysconfig/SuSEfirewall2.d/services/sblim-sfcb
%endif
echo "%defattr(-,root,root)" > _pkg_list
# Added NWP 5/14/08 - moved from 'files schema'
echo "%dir %{_datadir}/sfcb/" >> _pkg_list
find $RPM_BUILD_ROOT/%{_datadir}/sfcb -type f | grep -v $RPM_BUILD_ROOT/%{_datadir}/sfcb/CIM >> _pkg_list
# Added next line - NWP - declaring link to CIM as part of pkg
echo "%{_datadir}/sfcb/CIM" >> _pkg_list
# end add NWP
sed s?$RPM_BUILD_ROOT??g _pkg_list > _pkg_list_2
mv -f _pkg_list_2 _pkg_list
echo "%dir %{_libdir}/cmpi/" >> _pkg_list
echo "%dir %{_sysconfdir}/sfcb/" >> _pkg_list
echo "%dir %{_libdir}/sfcb" >> _pkg_list
echo "%config %{_sysconfdir}/sfcb/*" >> _pkg_list
echo "%config %{_sysconfdir}/pam.d/*" >> _pkg_list
%if 0%{?suse_version}
echo "%config %{_sysconfdir}/sysconfig/SuSEfirewall2.d/services/sblim-sfcb" >> _pkg_list
%endif
echo "%doc README COPYING AUTHORS" >> _pkg_list
echo "%doc %{_datadir}/man/man1/*" >> _pkg_list
echo "%{_sysconfdir}/init.d/sfcb" >> _pkg_list
echo "%{_localstatedir}/lib/sfcb" >> _pkg_list
echo "%{_bindir}/*" >> _pkg_list
echo "%{_sbindir}/*" >> _pkg_list
echo "%{_libdir}/sfcb/*.so*" >> _pkg_list
echo =======================================
cat _pkg_list

%clean
%{__rm} -rf $RPM_BUILD_ROOT

%pre
/usr/sbin/groupadd -r sfcb >/dev/null 2>&1 || :
/usr/sbin/groupmod -A root sfcb >/dev/null 2>&1 || :
# cleanup up schema directory (bnc#590196)
if [ -d %{_datadir}/sfcb/CIM -a \( \! -L /usr/share/sfcb/CIM \) ]
then
  rm -rf %{_datadir}/sfcb/CIM
fi

%post 
test -n "$FIRST_ARG" || FIRST_ARG=$1
#removed NWP, placed into init script for first service startup
#%{_datadir}/sfcb/genSslCert.sh %{_sysconfdir}/sfcb
%if 0%{?suse_version}
%{fillup_and_insserv -f sfcb}
%endif
if test "$FIRST_ARG" -eq 1 ; then
   sfcbrepos -f 2> /dev/null || :
fi
# else we do it in postun instead.
/sbin/ldconfig
exit 0

%preun
%stop_on_removal sfcb

%postun
test -n "$FIRST_ARG" || FIRST_ARG=$1
/sbin/ldconfig
if test "$FIRST_ARG" -ge 1 ; then
   sfcbrepos -f 2> /dev/null || :
fi
%restart_on_update sfcb
%insserv_cleanup

%files -f _pkg_list

%changelog
