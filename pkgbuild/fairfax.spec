# Maintainer: Jason Rimmer <jason@rimmer.net>
#
# Built by `bin/package-linux rpm` on Fedora: rpmbuild runs it with
#   --define "fairfax_src /work" --define "fairfax_version <version>"
# after bin/build has already produced build/fairfax in that tree.
# %install is scripts/stage-install, the one file list, so the packaged tree
# is identical to the Arch and Debian packages.

Name:           fairfax
Version:        %{fairfax_version}
Release:        1%{?dist}
Summary:        A keyboard-first task manager for Omarchy
License:        MIT
URL:            https://github.com/jrimmer/fairfax
BuildRequires:  zlib-devel

# The binary is already built and the package is only assembled from it, so
# there is nothing for the debuginfo machinery to split out.
%global debug_package %{nil}

# rpmbuild adds the ELF Requires (libQt6Core.so.6()(64bit) and friends)
# automatically. These are the QML modules, the Qt platform and Wayland
# plugins, and the runtime tools it cannot see: QtQuick, Layouts and Shapes
# live in qt6-qtdeclarative, Controls in qt6-qtquickcontrols2, the Wayland
# platform plugin in qt6-qtwayland, the offscreen plugin in qt6-qtbase-gui,
# and the SQLite SQL driver inside qt6-qtbase.
Requires:       qt6-qtbase
Requires:       qt6-qtbase-gui
Requires:       qt6-qtdeclarative
Requires:       qt6-qtquickcontrols2
Requires:       qt6-qtwayland
Requires:       git
Requires:       python3
Requires:       xdg-desktop-portal

%description
Fairfax is a keyboard-first task manager for Omarchy:
three panels (lists, tasks, tags), focus lists, start dates, projects and
checklists, and single-key commands for everything. It keeps tasks in a local
SQLite database and syncs them through a git repository you choose.

%prep

%build

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}
FAIRFAX_BIN=%{fairfax_src}/build/fairfax \
    %{fairfax_src}/scripts/stage-install %{buildroot} /usr

%files
/usr/bin/fairfax
/usr/bin/frfx
%dir /usr/lib/fairfax
/usr/lib/fairfax/fairfax-omarchy-setup
/usr/share/bash-completion/completions/fairfax
/usr/share/bash-completion/completions/frfx
/usr/share/zsh/site-functions/_fairfax
/usr/share/fish/vendor_completions.d/fairfax.fish
/usr/share/fish/vendor_completions.d/frfx.fish
/usr/share/licenses/fairfax/LICENSE
/usr/share/icons/hicolor/scalable/apps/fairfax.svg
/usr/share/applications/fairfax.desktop

%changelog
* Sat Oct 17 2026 Jason Rimmer <jason@rimmer.net> - %{fairfax_version}-1
- Package Fairfax for Fedora.
