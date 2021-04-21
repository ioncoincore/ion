Name:           ion
Version:        5.0.99.0

# Release Start

Release:        e15e875%{?dist}

# Release End

Summary:        Ionoin Daemon
Group:          System/Base
License:        MIT
URL:            https://github.com/ioncoincore/ion.git
Source0:        ion-5.0.99.0.tgz

Patch0:		version.patch
Patch1:         aarch-compile.patch
Patch2:         5.0.00.patch
Patch3:         no-precompile_header.patch
Patch4:         opensuse.patch
Patch5:         0001-xcb-proto-for-new-python.patch
Patch6:         force_bootstrap.patch
Patch7:         qt-bootstrap.patch
Patch8:         xcb_proto.patch

BuildRequires: gcc, git, rpm-build, rpm-devel, autoconf, automake, libtool, gcc-c++, which, make, cmake, zlib-devel, python3, curl

%if 0%{?suse_version}
BuildRequires: libstdc++-devel, glibc-devel, binutils
%else
%if 0%{?mageia}
BuildRequires: libstdc++-static-devel, glibc-static-devel
%else
BuildRequires: glibc-static, libstdc++-static, rpm-build-libs, cpp
%endif
%endif

%description
Ion is a free open source peer-to-peer electronic cash system that
is completely decentralized, without the need for a central server or
trusted parties. Users hold the crypto keys to their own money and
transact directly with each other, with the help of a P2P network to
check for double-spending.

Requires: xdg-utils

%package ioncoin
Summary:	Ionoin Daemon and QT Wallet
Requires: %{name} = %{version}-%{release}

%description ioncoin
Ioncoin is a command-line (iond) and qt (ion-qt) wallet for the Ioncoin Crypto Coin

%package iond 
Summary:	Ionoin Daemon
Requires: %{name} = %{version}-%{release}

%description iond
Iond is a command-line (iond) wallet for the Ioncoin Crypto Coin

%package qt
Summary:	Ioncoin QT Wallet
Requires: %{name} = %{version}-%{release}

%description qt
Ion-qt is a qt (ion-qt) wallet for the Ioncoin Crypto Coin

%package doc
Summary: Documentation files for %{name}
Group: Development/Languages
Requires: %{name} = %{version}-%{release}

%description doc
This package contains documentation files for %{name}.

%package devel
Summary:	Ioncoin Development Files
Requires: %{name} = %{version}-%{release}

%description devel
Libraries and includes for ioncoin development

%prep
%setup -q
%patch0 -p1
%ifarch aarch64
%patch1 -p1
%endif
# only if building v5.0.00
#%patch2 -p1
# centos needs precompile_header disabled
%if 0%{?centos}
%patch3 -p1
%endif
# only if libs are put in lib64
#%if 0%{?suse_version}
#%patch4 -p1
#%endif
%patch7 -p1
%if 0%{?fedora}
%patch8 -p1
%endif
%if 0%{?mageia}
%patch8 -p1
%endif

%build
case $RPM_ARCH in
  x86_64)
    BUILD="x86_64-pc-linux-gnu"
    ;;
  i386)
    BUILD="i686-pc-linux-gnu"
    ;;
  aarch64)
    BUILD="aarch64-unknown-linux-gnu"
    ;;
  arm)
    BUILD="armv7l-unknown-linux-gnueabihf"
    ;;
  s390x)
    BUILD="s390x-ibm-linux-gnu"
    ;;
  *)
    echo "$RPM_ARCH Not Supported"
    break
    ;;
esac
%if 0%{?fedora}
mkdir -p depends/patches/xcb_proto
cp $RPM_SOURCE_DIR/0001-xcb-proto-for-new-python.patch depends/patches/xcb_proto
%endif
%if 0%{?mageia}
mkdir -p depends/patches/xcb_proto
cp $RPM_SOURCE_DIR/0001-xcb-proto-for-new-python.patch depends/patches/xcb_proto
%endif
cp $RPM_SOURCE_DIR/force_bootstrap.patch depends/patches/qt
HOST=${BUILD} make -j5 -C depends
#cp -pr ~/depends/* depends/
./autogen.sh
CONFIG_SITE=`pwd`/depends/${BUILD}/share/config.site ./configure --prefix=/usr --enable-glibc-back-compat --enable-reduce-exports --disable-bench --disable-gui-tests --disable-ccache --disable-maintainer-mode --disable-dependency-tracking --host=${BUILD}
HOST=${BUILD} make -j5 install DESTDIR=$RPM_BUILD_ROOT
/usr/bin/strip $RPM_BUILD_ROOT/usr/bin/*
mkdir -p $RPM_BUILD_ROOT/usr/share/doc/ion
mkdir -p $RPM_BUILD_ROOT/usr/share/pixmaps
mkdir -p $RPM_BUILD_ROOT/usr/share/applications
mkdir -p $RPM_BUILD_ROOT/usr/share/desktop-directories
mkdir -p $RPM_BUILD_ROOT/etc/audit/local.rules
mkdir -p $RPM_BUILD_ROOT/etc/sysconfig
mkdir -p $RPM_BUILD_ROOT/usr/lib/systemd/system
/usr/bin/install $RPM_BUILD_DIR/%{name}-%{version}/iond.service $RPM_BUILD_ROOT/usr/lib/systemd/system
/usr/bin/install $RPM_BUILD_DIR/%{name}-%{version}/iond.pp $RPM_BUILD_ROOT/etc/audit/local.rules
/usr/bin/install $RPM_BUILD_DIR/%{name}-%{version}/iond.te $RPM_BUILD_ROOT/etc/audit/local.rules
/usr/bin/install $RPM_BUILD_DIR/%{name}-%{version}/iond $RPM_BUILD_ROOT/etc/sysconfig
/usr/bin/install $RPM_BUILD_DIR/%{name}-%{version}/ioncoin-core.desktop $RPM_BUILD_ROOT/usr/share/applications
/usr/bin/install $RPM_BUILD_DIR/%{name}-%{version}/ioncoin-core.directory $RPM_BUILD_ROOT/usr/share/desktop-directories
/usr/bin/install share/pixmaps/* $RPM_BUILD_ROOT/usr/share/pixmaps
cp -r doc/* $RPM_BUILD_ROOT/usr/share/doc/ion
rm -rf $RPM_BUILD_ROOT/usr/share/doc/ioncoin
rm -rf $RPM_BUILD_ROOT/usr/lib/.build-id

#%clean
#mv $RPM_BUILD_DIR/%{name}-%{version} /usr/src/ioncoin-core.old/%{name}-%{version}-%{release}-`date +%d.%m.%y`
#rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%{_bindir}/ion-cli
%{_bindir}/ion-tx
%{_bindir}/test_ion

%files devel
%defattr(-,root,root,-)
/usr/include/ionconsensus.h
/usr/lib/libionconsensus.a
/usr/lib/libionconsensus.la
/usr/lib/libionconsensus.so
/usr/lib/libionconsensus.so.0
/usr/lib/libionconsensus.so.0.0.0
/usr/lib/pkgconfig/libionconsensus.pc

%files ioncoin
%defattr(-,root,root,-)
%{_bindir}/iond
%{_bindir}/ion-qt
/usr/share/pixmaps/*
/usr/share/applications/ioncoin-core.desktop
/usr/share/desktop-directories/ioncoin-core.directory
/etc/sysconfig/iond
/usr/lib/systemd/system/iond.service
/etc/audit/local.rules/*

%files iond
%defattr(-,root,root,-)
%{_bindir}/iond
/etc/sysconfig/iond
/usr/lib/systemd/system/iond.service
/etc/audit/local.rules/*

%files qt
%defattr(-,root,root,-)
%{_bindir}/ion-qt
/usr/share/pixmaps/*
/usr/share/applications/ioncoin-core.desktop
/usr/share/desktop-directories/ioncoin-core.directory

%files doc
%defattr(-,root,root,-)
/usr/share/doc/ion/*
/usr/share/man/man1/*

%post qt
/usr/bin/xdg-icon-resource install --size 64 /usr/share/pixmaps/ion64.png cevap-ioncoin
/usr/bin/xdg-desktop-menu install /usr/share/desktop-directories/ioncoin-core.directory /usr/share/applications/ioncoin-core.desktop
echo "##"
echo "# Ion-qt Package Installed"
echo "##"

%post iond
/usr/sbin/semodule -i /etc/audit/local.rules/iond.pp
/bin/systemctl daemon-reload
echo "##"
echo "# Install finished.  Edit USER in /etc/sysconfig/iond"
echo "##"

%post ioncoin
/usr/sbin/semodule -i /etc/audit/local.rules/iond.pp
/usr/bin/xdg-icon-resource install --size 64 /usr/share/pixmaps/ion64.png cevap-ioncoin
/usr/bin/xdg-desktop-menu install /usr/share/desktop-directories/ioncoin-core.directory /usr/share/applications/ioncoin-core.desktop
/bin/systemctl daemon-reload
echo "##"
echo "# Install finished.  Edit USER in /etc/sysconfig/iond"
echo "##"

%pre qt
if [ ! -z `/bin/pidof ion-qt`]
then
 /bin/kill -15 `/bin/pidof ion-qt`
fi

%pre iond
if [ ! -z `/bin/pidof iond`]
then
 /bin/kill -15 `/bin/pidof iond`
fi

%pre ioncoin
if [ ! -z `/bin/pidof iond`]
then
 /bin/kill -15 `/bin/pidof iond`
fi
if [ ! -z `/bin/pidof ion-qt`]
then
 /bin/kill -15 `/bin/pidof ion-qt`
fi

%preun qt
if [ ! -z `/bin/pidof ion-qt` ];then
 /bin/kill -15 `/bin/pidof ion-qt`
fi
/usr/bin/xdg-desktop-menu uninstall /usr/share/desktop-directories/ioncoin-core.directory /usr/share/applications/ioncoin-core.desktop
/usr/bin/xdg-icon-resource uninstall --size 64 cevap-ioncoin

%preun iond
if [ ! -z `/bin/pidof iond` ];then
 /bin/kill -15 `/bin/pidof iond`
fi

%preun ioncoin
if [ ! -z `/bin/pidof iond` ];then
 /bin/kill -15 `/bin/pidof iond`
fi
if [ ! -z `/bin/pidof ion-qt` ];then
 /bin/kill -15 `/bin/pidof ion-qt`
fi
/usr/bin/xdg-desktop-menu uninstall /usr/share/desktop-directories/ioncoin-core.directory /usr/share/applications/ioncoin-core.desktop
/usr/bin/xdg-icon-resource uninstall --size 64 cevap-ioncoin

%postun qt
echo "##"
echo "# Ion-QT Removed"
echo "##"

%postun iond
if [ -e /lib/systemd/system/iond.service ]
then
 /bin/systemctl disable iond 
 /bin/rm /lib/systemd/system/iond.service
fi
/bin/systemctl daemon-reload
/usr/sbin/semodule -r iond
echo "##"
echo "# Iond Removed"
echo "##"

%postun ioncoin
if [ -e /lib/systemd/system/iond.service ]
then
 /bin/systemctl disable iond
 /bin/rm /lib/systemd/system/iond.service
fi
/bin/systemctl daemon-reload
/usr/sbin/semodule -r iond
echo "##"
echo "# Ioncoin Package Removed"
echo "##"

%changelog
* Wed Apr 21 2021 ckti.ion <ckti@i2pmail.org> - 5.0.99.0-e15e875
- update to 5.0.00

* Sun Apr 26 2020 ckti <ckti@i2pmail.org> - 5.0.99.0-e17c0dfd2
- see https://bitbucket.org/ioncoin/ion/src/master/doc/release-notes.md for full changelog

* Thu Apr 19 2018 ckti.ion <ckti@i2pmail.org> - 3.0.4.0.CE-0da0dd8.2
- Issues Resolved in this Release:
- Change signal in pre/post scripts

* Tue Apr 10 2018 ckti.ion <ckti@i2pmail.org> - 3.0.4.0.CE-0da0dd8.1
- Issues Resolved in this Release:
- Automatic Wallet Update (#31)
- Fatal internal error (#19)
- Mac Qt Coin Control only shows max 10 addresses in tree mode (#17)
- Missing splashscreens for regtest and unittest (#10)
- Warning during compilation: suggest parentheses around '&&' within (#9)
- listtransactions and listunspent not showing transactions since last start 
- (#8)
- Local wallet Mints using 20K for Remote Master Node (#4)

* Wed Apr 04 2018 ckti.ion <ckti@i2pmail.org> - 3.0.4.0.CE-fa72830.1
- update to version fa72830.1
- Release 3.0.4.0 is a recommended, semi-mandatory update for all users.
- This release contains transaction creation bug fixes for xION spends,
- automint calculation adjustments, and other various updates/fixes.
- We have now zerocoin within ion. More info about what it is and how to use it
- will follow in announcements and further release info.
- Snapcraft is enabled and it can be simply installed by:
- sudo snap install --edge ion
- Stashedsend is now Swift-X
- We dropped MIDAS and will use DGW. More info will follow.
- Autominiting with zerocoin. More info will follow.
- We have new look and desing, currently it is a dirty version.
- It includes new GUI layout, new colors.
- Current source base is much faster and cleaner than ion's previous one.
- It uses all cpu's and there are no performance issues which we could
- observe, it is just much faster then previous source base.
- We have BIP38 including a tool with password encryption and
- decrpytion features
- We finaly have built in blockexplorer which works on all ion's networks.
- There are some new features which improve usability as well as user
- experience in general. More info to follow.
- Auto Wallet Backup

* Sat Mar 17 2018 ckti.ion <ckti@i2pmail.org> - 3.0.99.0-5b3ca01.1
- update to version 5b3ca01.1

* Tue Jan 02 2018 ckti.ion <ckti@i2pmail.org> - 2.1.6.3-2655d30.1
- update to version 2655d30.1

* Sat Nov 18 2017 ckti.ion <ckti@i2pmail.org> - 2.1.6.3-1ea608a.3
- add iond.te removal
- add xdg-icon-resource install
- add dependency of xdg-utils

* Mon Nov 06 2017 ckti.ion <ckti@i2pmail.org> - 2.1.6.3-1ea608a.2
- update to GIT commit 1ea608a.2
- add iond.pp SeLinux module and source iond.te

* Sat Nov 04 2017 ckti.ion <ckti@i2pmail.org> - 2.1.6.3-1ea608a.1
- update to GIT commit 1ea608a.1

* Mon Oct 23 2017 ckti.ion <ckti@i2pmail.org> - 2.1.6.3-5bc80c4.3
- add application menu items

* Sat Oct 14 2017 ckti.ion <ckti@i2pmail.org> - 2.1.6.3-5bc80c4.2
- rewrite iond.service

* Tue Oct 10 2017 ckti.ion <ckti@i2pmail.org> - 2.1.6.3-5bc80c4.1
- Initial Installation

