# How to build (ex.): rpmbuild --bb --define 'dist .fc5' gfarmfs.spec
Summary: GfarmFS-FUSE
Name: gfarmfs-fuse
Version: 2.0.0
Release: 1%{?dist}
License: BSD
Group: Applications/Internet
Vendor: National Institute of Advanced Industrial Science and Technology
URL: http://datafarm.apgrid.org/
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%define prefix   %{_prefix}

#BuildRequires: fuse-devel >= 2.5, gfarm-libs >= 1.4, gfarm-devel >= 1.4

Requires: fuse >= 2.5, fuse-libs >= 2.5, gfarm-libs >= 1.4, gfarm-client >= 1.4

%description
GfarmFS-FUSE enables you to mount a Gfarm filesystem in userspace via
FUSE mechanism.

%prep
%setup -q

%build
./configure --prefix=%{prefix}
make

%install
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT
make DESTDIR=${RPM_BUILD_ROOT} install

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
* Tue Mar  6 2007  <takuya@soum.co.jp> 2.0.0-1
- Use 'dist' definition for filename.
- Comment out BuildRequires.
* Thu Feb 15 2007  <takuya@soum.co.jp> 2.0.0-0
- Update version.
* Fri Nov 17 2006  <takuya@soum.co.jp> 1.4.0-1
- Use "make install". ("contrib/" is included in Makefile by automake.)
* Tue Nov 14 2006  <takuya@soum.co.jp> 1.4.0-0
- Depend on gfarm-libs and gfarm-client.
- Add BuildRequires.
* Thu Sep  7 2006  <tatebe@gmail.com> 1.3.0-2
- Add mount.gfarmfs and umount.gfarmfs.
* Wed Aug 30 2006  <tatebe@gmail.com> 1.3.0-1
- Add gfarmfs-exec.sh that is a wrapper script to execute a program via
  a batch queing system.
* Mon Jul  3 2006  <takuya@soum.co.jp> 1.3.0-0
- Initial build.
