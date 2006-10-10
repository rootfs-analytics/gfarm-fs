Summary: GfarmFS-FUSE
Name: gfarmfs-fuse
Version: 1.3.0
Release: 2
License: BSD
Group: Applications/Internet
Vendor: National Institute of Advanced Industrial Science and Technology
URL: http://datafarm.apgrid.org/
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%define prefix   %{_prefix}

Requires: fuse >= 2.5, fuse-libs >= 2.5

%description
GfarmFS-FUSE enables you to mount a Gfarm filesystem in userspace via
FUSE mechanism.

%prep
%setup -q

%build
./configure --prefix=%{prefix}

%install
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT
make
mkdir -p $RPM_BUILD_ROOT%{prefix}/bin
install -m 755 gfarmfs $RPM_BUILD_ROOT%{prefix}/bin/gfarmfs
install -m 755 contrib/gfarmfs-exec/gfarmfs-exec.sh $RPM_BUILD_ROOT%{prefix}/bin/gfarmfs-exec.sh
install -m 755 contrib/mount.gfarmfs/mount.gfarmfs $RPM_BUILD_ROOT%{prefix}/bin/mount.gfarmfs
install -m 755 contrib/mount.gfarmfs/umount.gfarmfs $RPM_BUILD_ROOT%{prefix}/bin/umount.gfarmfs

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%{prefix}/bin/gfarmfs
%{prefix}/bin/gfarmfs-exec.sh
%{prefix}/bin/mount.gfarmfs
%{prefix}/bin/umount.gfarmfs
%doc README README.ja ChangeLog ChangeLog.ja

%changelog
* Thu Sep 07 2006  <tatebe@gmail.com> 1.3.0-2
- Add mount.gfarmfs and umount.gfarmfs.
* Wed Aug 30 2006  <tatebe@gmail.com> 1.3.0-1
- Add gfarmfs-exec.sh that is a wrapper script to execute a program via
  a batch queing system.
* Mon Jul  3 2006  <takuya@soum.co.jp> 1.3.0-0
- Initial build.
