Summary: GfarmFS-FUSE
Name: gfarmfs-fuse
Version: 1.3.0
Release: 1
License: BSD
Group: Applications/Internet
Vendor: National Institute of Advanced Industrial Science and Technology
URL: http://datafarm.apgrid.org/
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%define prefix   %{_prefix}

%define gfarm_gsi %(rpm -q gfarm-gsi-libs 2>&1 > /dev/null && echo 1 || echo 0)

%if %{gfarm_gsi}
%define libgfarm_name    gfarm-gsi-libs
%else
%define libgfarm_name    gfarm-libs
%endif

Requires: fuse >= 2.5, fuse-libs >= 2.5, %{libgfarm_name} >= 1.3

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

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%{prefix}/bin/gfarmfs
%{prefix}/bin/gfarmfs-exec.sh
%doc README README.ja ChangeLog ChangeLog.ja

%changelog
* Wed Aug 30 2006  <tatebe@gmail.com> 1.3.0-1
- Add gfarmfs-exec.sh that is a wrapper script to execute a program via
  a batch queing system.
* Mon Jul  3 2006  <takuya@soum.co.jp> 1.3.0-0
- Initial build.
