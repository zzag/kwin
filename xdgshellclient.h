/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2018 David Edmundson <davidedmundson@kde.org>
Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

#pragma once

#include "shellsurfaceclient.h"
#include "xdgdecorationv1interface.h"
#include "xdgshellinterface.h"

#include <QQueue>
#include <QTimer>

namespace KWayland
{
namespace Server
{
class AppMenuInterface;
class PlasmaShellSurfaceInterface;
class ServerSideDecorationInterface;
class ServerSideDecorationPaletteInterface;
}
}

namespace KWin
{

class XdgSurfaceConfigure
{
public:
    virtual ~XdgSurfaceConfigure();

    void setGeometry(const QRect &rect);
    QRect geometry() const;

    void setSerial(quint32 serial);
    quint32 serial() const;

private:
    QRect m_geometry;
    quint32 m_serial = 0;
};

class XdgSurfaceClient : public ShellSurfaceClient
{
    Q_OBJECT

public:
    XdgSurfaceClient(XdgSurfaceInterface *shellSurface);
    ~XdgSurfaceClient() override;

    QRect inputGeometry() const override;
    QRect bufferGeometry() const override;
    QSize clientSize() const override;
    QMatrix4x4 inputTransformation() const override;
    using AbstractClient::setFrameGeometry;
    void setFrameGeometry(int x, int y, int w, int h, ForceGeometry_t force = NormalGeometrySet) override;
    using AbstractClient::move;
    void move(int x, int y, ForceGeometry_t force = NormalGeometrySet) override;
    bool isShown(bool shaded_is_shown) const override;
    bool isHiddenInternal() const override;
    void hideClient(bool hide) override;
    void destroyClient() override;

    QRect frameRectToBufferRect(const QRect &rect) const;
    QRect requestedFrameGeometry() const;
    QPoint requestedPos() const;
    QRect requestedClientGeometry() const;
    QSize requestedClientSize() const;
    QRect clientGeometry() const;
    bool isClosing() const;
    bool isHidden() const;
    bool isUnmapped() const;

Q_SIGNALS:
    void windowMapped();
    void windowUnmapped();

protected:
    void addDamage(const QRegion &damage) override;

    virtual XdgSurfaceConfigure *sendRoleConfigure() const = 0;
    virtual void handleRoleCommit();

    XdgSurfaceConfigure *lastAcknowledgedConfigure() const;
    void scheduleConfigure();
    void sendConfigure();
    void requestGeometry(const QRect &rect);
    void updateGeometry(const QRect &rect);

private:
    void handleConfigureAcknowledged(quint32 serial);
    void handleCommit();
    void handleNextWindowGeometry();
    bool haveNextWindowGeometry() const;
    void setHaveNextWindowGeometry();
    void resetHaveNextWindowGeometry();
    QRect adjustMoveResizeGeometry(const QRect &rect) const;
    void updateGeometryRestoreHack();
    void updateDepth();
    void internalShow();
    void internalHide();
    void internalMap();
    void internalUnmap();
    void cleanGrouping();
    void cleanTabBox();

    XdgSurfaceInterface *m_shellSurface;
    QTimer *m_configureTimer;
    QQueue<XdgSurfaceConfigure *> m_configureEvents;
    XdgSurfaceConfigure *m_lastAcknowledgedConfigure = nullptr;
    QRect m_windowGeometry;
    QRect m_requestedFrameGeometry;
    QRect m_bufferGeometry;
    QRect m_requestedClientGeometry;
    QRect m_clientGeometry;
    bool m_isClosing = false;
    bool m_isHidden = false;
    bool m_isUnmapped = true;
    bool m_haveNextWindowGeometry = false;
};

class XdgToplevelConfigure : public XdgSurfaceConfigure
{
public:
    void setStates(const XdgToplevelInterface::States &states);
    XdgToplevelInterface::States states() const;

private:
    XdgToplevelInterface::States m_states;
};

class XdgToplevelClient : public XdgSurfaceClient
{
    Q_OBJECT

    enum class PingReason {
        CloseWindow,
        FocusWindow,
    };

public:
    XdgToplevelClient(XdgToplevelInterface *shellSurface);
    ~XdgToplevelClient() override;

    void debug(QDebug &stream) const override;
    NET::WindowType windowType(bool direct = false, int supported_types = 0) const override;
    MaximizeMode maximizeMode() const override;
    MaximizeMode requestedMaximizeMode() const override;
    QSize minSize() const override;
    QSize maxSize() const override;
    bool isFullScreen() const override;
    bool isMovableAcrossScreens() const override;
    bool isMovable() const override;
    bool isResizable() const override;
    bool isCloseable() const override;
    bool isFullScreenable() const override;
    bool isMaximizable() const override;
    bool isMinimizable() const override;
    bool isTransient() const override;
    bool userCanSetFullScreen() const override;
    bool userCanSetNoBorder() const override;
    bool noBorder() const override;
    void setNoBorder(bool set) override;
    void updateDecoration(bool check_workspace_pos, bool force = false) override;
    void updateColorScheme() override;
    bool supportsWindowRules() const override;
    void takeFocus() override;
    bool wantsInput() const override;
    bool dockWantsInput() const override;
    bool hasStrut() const override;
    void showOnScreenEdge() override;
    bool isInitialPositionSet() const override;
    void setFullScreen(bool set, bool user) override;
    void closeWindow() override;

    void installAppMenu(KWayland::Server::AppMenuInterface *appMenu);
    void installServerDecoration(KWayland::Server::ServerSideDecorationInterface *decoration);
    void installPalette(KWayland::Server::ServerSideDecorationPaletteInterface *palette);
    void installPlasmaShellSurface(KWayland::Server::PlasmaShellSurfaceInterface *shellSurface);
    void installXdgDecoration(XdgToplevelDecorationV1Interface *decoration);

protected:
    XdgSurfaceConfigure *sendRoleConfigure() const override;
    void handleRoleCommit() override;
    void doMinimize() override;
    void doResizeSync() override;
    void doSetActive() override;
    bool acceptsFocus() const override;
    void changeMaximize(bool horizontal, bool vertical, bool adjust) override;
    Layer layerForDock() const override;

private:
    void handleWindowTitleChanged();
    void handleWindowClassChanged();
    void handleWindowMenuRequested(KWayland::Server::SeatInterface *seat,
                                   const QPoint &surfacePos, quint32 serial);
    void handleMoveRequested(KWayland::Server::SeatInterface *seat, quint32 serial);
    void handleResizeRequested(KWayland::Server::SeatInterface *seat,
                               Qt::Edges, quint32 serial);
    void handleStatesAcknowledged(const XdgToplevelInterface::States &states);
    void handleMaximizeRequested();
    void handleUnmaximizeRequested();
    void handleFullscreenRequested(KWayland::Server::OutputInterface *output);
    void handleUnfullscreenRequested();
    void handleMinimizeRequested();
    void handleTransientForChanged();
    void handleForeignTransientForChanged(KWayland::Server::SurfaceInterface *child);
    void handlePingTimeout(quint32 serial);
    void handlePingDelayed(quint32 serial);
    void handlePongReceived(quint32 serial);
    void initialize();
    void updateMaximizeMode(MaximizeMode maximizeMode);
    void updateFullScreenMode(bool set);
    void updateShowOnScreenEdge();
    void setupWindowManagementIntegration();
    void setupPlasmaShellIntegration();
    void sendPing(PingReason reason);

    QPointer<KWayland::Server::PlasmaShellSurfaceInterface> m_plasmaShellSurface;
    QPointer<KWayland::Server::AppMenuInterface> m_appMenuInterface;
    QPointer<KWayland::Server::ServerSideDecorationPaletteInterface> m_paletteInterface;
    QPointer<KWayland::Server::ServerSideDecorationInterface> m_serverDecoration;
    QPointer<XdgToplevelDecorationV1Interface> m_xdgDecoration;
    XdgToplevelInterface *m_shellSurface;
    XdgToplevelInterface::States m_lastAcknowledgedStates;
    QMap<quint32, PingReason> m_pings;
    QRect m_fullScreenGeometryRestore;
    NET::WindowType m_windowType = NET::Normal;
    MaximizeMode m_maximizeMode = MaximizeRestore;
    MaximizeMode m_requestedMaximizeMode = MaximizeRestore;
    bool m_isFullScreen = false;
    bool m_userNoBorder = false;
    bool m_isTransient = false;
    bool m_isInitialized = false;
};

class XdgPopupClient : public XdgSurfaceClient
{
    Q_OBJECT

public:
    XdgPopupClient(XdgPopupInterface *shellSurface);
    ~XdgPopupClient() override;

    void debug(QDebug &stream) const override;
    NET::WindowType windowType(bool direct = false, int supported_types = 0) const override;
    bool hasPopupGrab() const override;
    void popupDone() override;
    bool isPopupWindow() const override;
    bool isTransient() const override;
    bool isResizable() const override;
    bool isMovable() const override;
    bool isMovableAcrossScreens() const override;
    bool hasTransientPlacementHint() const override;
    QRect transientPlacement(const QRect &bounds) const override;
    bool isCloseable() const override;
    void closeWindow() override;
    void updateColorScheme() override;
    bool noBorder() const override;
    bool userCanSetNoBorder() const override;
    void setNoBorder(bool set) override;
    void updateDecoration(bool check_workspace_pos, bool force = false) override;
    void showOnScreenEdge() override;
    bool wantsInput() const override;
    void takeFocus() override;
    bool supportsWindowRules() const override;

protected:
    bool acceptsFocus() const override;
    XdgSurfaceConfigure *sendRoleConfigure() const override;

private:
    void handleGrabRequested(KWayland::Server::SeatInterface *seat, quint32 serial);
    void initialize();

    XdgPopupInterface *m_shellSurface;
    bool m_haveExplicitGrab = false;
};

} // namespace KWin
