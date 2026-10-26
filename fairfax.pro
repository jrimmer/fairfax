QT += core gui widgets sql qml quick quickcontrols2 network
# Desktop notifications and the file-chooser portal use D-Bus on Linux;
# macOS and Windows use the tray and native dialogs instead.
linux: QT += dbus

CONFIG += c++20 release
TARGET = fairfax
TEMPLATE = app
# The one place the app version lives: bin/build-macos and bin/build-windows.ps1
# read it for the app bundle and the installer, and it is compiled in below.
VERSION = 1.0.3
DEFINES += FAIRFAX_VERSION=\\\"$$VERSION\\\"

# SVG is needed by --render-icon on Windows (the .ico) and macOS (the .png
# that sips turns into the .icns). Linux makes its icon from the installed SVG.
win32|macx: QT += svg

# Listed explicitly so a half-written file in src/ never breaks the build.
HEADERS += src/agent.h src/alarms.h src/appsetup.h src/backend.h src/dateparse.h src/exchange.h src/mailtask.h src/imapclient.h src/mailfetch.h src/oauth.h src/filepicker.h src/gitsync.h src/synctree.h src/foldersync.h src/webdav.h src/synchub.h src/restorelists.h src/model.h src/nlparse.h src/query.h src/recurrence.h src/rowmodel.h src/schedule.h src/store.h src/theme.h src/views.h src/markdown.h src/automation.h src/calendar.h src/myday.h src/sounds.h src/selftest.h src/todoist.h src/cli.h src/tray.h src/traymenu.h src/keyring.h src/hyprland.h src/diagnostics.h src/searchrules.h src/shortcuts.h src/caldavclient.h src/vtodo.h src/caldavsync.h src/ziparchive.h
SOURCES += src/agent.cpp src/alarms.cpp src/appsetup.cpp src/backend.cpp src/dateparse.cpp src/demo.cpp src/exchange.cpp src/mailtask.cpp src/imapclient.cpp src/mailfetch.cpp src/oauth.cpp src/filepicker.cpp src/gitsync.cpp src/synctree.cpp src/foldersync.cpp src/webdav.cpp src/synchub.cpp src/restorelists.cpp src/main.cpp src/model.cpp src/nlparse.cpp src/query.cpp src/recurrence.cpp src/rowmodel.cpp src/schedule.cpp src/store.cpp src/theme.cpp src/views.cpp src/markdown.cpp src/automation.cpp src/calendar.cpp src/myday.cpp src/sounds.cpp src/selftest.cpp src/todoist.cpp src/cli.cpp src/tray.cpp src/traymenu.cpp src/keyring.cpp src/hyprland.cpp src/diagnostics.cpp src/searchrules.cpp src/shortcuts.cpp src/caldavclient.cpp src/vtodo.cpp src/caldavsync.cpp src/ziparchive.cpp

RESOURCES += src/resources.qrc

# macOS: a proper .app bundle named Fairfax; Windows: no console window.
macx {
    TARGET = Fairfax
    # Supported: Apple Silicon Macs on macOS 14.4 or later (Info.plist says the same).
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 14.4
    # The bundle's Info.plist (bundle id, name, the fairfax:// URL scheme,
    # high resolution) lives in the repo, so bin/build-macos only fills in the
    # version and the icon.
    QMAKE_INFO_PLIST = $$PWD/pkgbuild/macos/Info.plist
    # bin/build-macos makes this from pkgbuild/fairfax.svg before qmake runs.
    exists($$PWD/pkgbuild/macos/fairfax.icns) {
        ICON = $$PWD/pkgbuild/macos/fairfax.icns
    }
}
win32 {
    CONFIG += windows
    # The Credential Manager (src/keyring.cpp).
    LIBS += -ladvapi32
    # Made by bin/build-windows.ps1, either with ImageMagick or with the app's
    # own --render-icon option (which is why the script may build twice).
    exists($$PWD/pkgbuild/windows/fairfax.ico) {
        RC_ICONS = $$PWD/pkgbuild/windows/fairfax.ico
    }
}


# Zipped task-database backups are read with zlib. Windows has no system zlib:
# point ZLIB_DIR (an environment variable) at a prefix with include/zlib.h and
# lib/zlib.lib, such as vcpkg's x64-windows-static-md install; without it the
# zip path is compiled out and the importer asks for the backup to be unzipped
# first.
unix: LIBS += -lz
win32 {
    ZLIB_PREFIX = $$(ZLIB_DIR)
    isEmpty(ZLIB_PREFIX) {
        DEFINES += FAIRFAX_NO_ZLIB
    } else {
        INCLUDEPATH += $$ZLIB_PREFIX/include
        LIBS += -L$$ZLIB_PREFIX/lib -lzlib
    }
}
