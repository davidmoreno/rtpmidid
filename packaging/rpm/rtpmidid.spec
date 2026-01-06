%global debug_package %{nil}

Name:           rtpmidid
Version:        24.12~14~g0c9c3
Release:        1%{?dist}
Summary:        Real Time Protocol Musical Instrument Digital Interface Daemon (RTP-MIDI)
License:        GPLv2+ and LGPLv2+
URL:            https://github.com/davidmoreno/rtpmidid
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  avahi-devel
BuildRequires:  alsa-lib-devel
BuildRequires:  python3
BuildRequires:  pandoc
BuildRequires:  git
BuildRequires:  systemd-rpm-macros

Requires:       python3
Requires:       avahi
Requires:       alsa-lib

%description
rtpmidid allows you to share ALSA sequencer devices on the network using RTP
MIDI, and import other network shared RTP MIDI devices.

rtpmidid is an user daemon, and when a RTP MIDI device is announced using mDNS
(also known as Zeroconf, Avahi, and multicast DNS) it exposes this ALSA
sequencer port.

%package libs
Summary:        RTP MIDI library
Requires:       %{name} = %{version}-%{release}

%description libs
rtpmidid allows you to share ALSA sequencer devices on the network using RTP
MIDI, and import other network shared RTP MIDI devices.

This package provides the shared library for rtpmidi which allows you to
integrate RTP MIDI in your application.

%package devel
Summary:        RTP MIDI library development files
Requires:       %{name}-libs = %{version}-%{release}

%description devel
rtpmidid allows you to share ALSA sequencer devices on the network using RTP
MIDI, and import other network shared RTP MIDI devices.

This package provides the development headers to librtpmidid.

%prep
%setup -q

%build
make build-deb

%install
rm -rf %{buildroot}

# Install main package
make install-rtpmidid DESTDIR=%{buildroot} SYSCONFDIR=%{_sysconfdir} PREFIX=%{_prefix}

# Install library packages
make install-librtpmidid0 DESTDIR=%{buildroot} SYSCONFDIR=%{_sysconfdir} PREFIX=%{_prefix}
make install-librtpmidid0-dev DESTDIR=%{buildroot} SYSCONFDIR=%{_sysconfdir} PREFIX=%{_prefix}

# Move systemd service to correct location
if [ -d %{buildroot}%{_sysconfdir}/systemd ]; then
    mkdir -p %{buildroot}%{_unitdir}
    mv %{buildroot}%{_sysconfdir}/systemd/system/* %{buildroot}%{_unitdir}/ 2>/dev/null || true
    rm -rf %{buildroot}%{_sysconfdir}/systemd
fi

# Move libraries to correct libdir (lib64 on x86_64, lib on other arches)
if [ -d %{buildroot}/usr/lib ] && [ "%{_libdir}" != "/usr/lib" ]; then
    mkdir -p %{buildroot}%{_libdir}
    mv %{buildroot}/usr/lib/lib*.so* %{buildroot}%{_libdir}/ 2>/dev/null || true
    mv %{buildroot}/usr/lib/lib*.a %{buildroot}%{_libdir}/ 2>/dev/null || true
fi

# Ensure rtpmidid-cli is executable
chmod +x %{buildroot}%{_bindir}/rtpmidid-cli

%files
%{_bindir}/rtpmidid
%attr(0755, root, root) %{_bindir}/rtpmidid-cli
%{_unitdir}/rtpmidid.service
%config(noreplace) %{_sysconfdir}/rtpmidid/default.ini
%{_mandir}/man1/rtpmidid.1*
%{_mandir}/man1/rtpmidid-cli.1*
%{_docdir}/rtpmidid/README.md
%{_docdir}/rtpmidid/LICENSE.txt

%files libs
%{_libdir}/librtpmidid.so.*
%{_docdir}/librtpmidid0/*

%files devel
%{_libdir}/lib*.so
%{_libdir}/lib*.a
%{_includedir}/rtpmidid
%{_docdir}/librtpmidid0-dev/*

%changelog
* %(date +"%a %b %d %Y") David Moreno <dmoreno@coralbits.com> - %{version}-%{release}
- Initial RPM package


