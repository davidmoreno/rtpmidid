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
* Tue Jan 06 2026 David Moreno <dmoreno@coralbits.com> - 26.01-1
- Docker support: Added Dockerfile, docker-compose, and comprehensive Docker documentation
- RPM package support: Full support for building RPM packages with multi-distro and multi-arch support (including riscv64)
- Log level configuration: New CMake option to set log level at compile time (1=debug, 2=info, 3=warning, 4=error)
- Fixed ALSA listener cleanup to avoid double removal and invalid peer access
- Improved error handling in control socket with robust client removal and better JSON parsing
- Fixed hardware auto-announce: corrected regex checks and fixed sending data from network to ALSA
- Fixed incorrect time unit in log output
- Added missing build dependency (ninja)
- Updated README with Docker usage instructions and packaging documentation
- Improved packaging system with new targets for Docker-based builds

* Sun Dec 15 2024 David Moreno <dmoreno@coralbits.com> - 24.12-1
- Raw MIDI devices - Can configure them in the ini file, command line or cli util.
- Optionally use std::format instead of fmt library, where available.
- Improvements to the TUI.
- Improvements to the INI configuration file: rawmidi devices, can set local UDP port for clients. Use regex for auto add to the autodiscover and announcement (positive and negative)
- More tests.

* Tue Dec 05 2023 David Moreno <dmoreno@coralbits.com> - 23.12b2-1
- Packaging fixes
- Compiles with newest libfmt
- No mallocs on the hot path
- Fixes connecting. Improved timeouts.

* Tue Oct 24 2023 David Moreno <dmoreno@coralbits.com> - 23.10b1-1
- New midirouter implementation
- Accepts an ini file as initial configuration.
- Control socket is more powerful and complete.

* Thu Nov 04 2021 David Moreno <dmoreno@coralbits.com> - 21.11-1
- Several MIDI packets in a single packet. This solves several issues with third party implementations.
- Resassembly of SysEx packages
- Internal improvements
- More tests
- Log colors only on terminals
- Parsing or Real Time Events
- Moved mdns_rtpmidi into the library

* Fri Jul 23 2021 David Moreno <dmoreno@coralbits.com> - 21.07-1
- Bugfix: bad ordering of events
- More MIDI events
- Bad acess initiator / ssrc
- Bigger buffer size for sysex

* Sat Jul 11 2020 David Moreno <dmoreno@coralbits.com> - 20.07-1
- Improved Mac OS and iOS support
- Control CLI
- LGPL2.1 Library
- Improved stability

* Sun Apr 05 2020 David Moreno <dmoreno@coralbits.com> - 20.04.5-1
- Initial Release.


