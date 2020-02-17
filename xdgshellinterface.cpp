/*
 * Copyright (C) 2020 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "xdgshellinterface.h"
#include "xdgshellinterface_p.h"

#include <KWayland/Server/display.h>
#include <KWayland/Server/output_interface.h>
#include <KWayland/Server/seat_interface.h>

#include <QTimer>

using namespace KWayland::Server;

namespace KWin
{

// TODO: Reset the surface when it becomes unmapped.

XdgShellInterfacePrivate::XdgShellInterfacePrivate(XdgShellInterface *shell)
    : q(shell)
{
}

static wl_client *clientFromXdgSurface(XdgSurfaceInterface *surface)
{
    return XdgSurfaceInterfacePrivate::get(surface)->resource()->client();
}

void XdgShellInterfacePrivate::registerXdgSurface(XdgSurfaceInterface *surface)
{
    xdgSurfaces.insert(clientFromXdgSurface(surface), surface);
}

void XdgShellInterfacePrivate::unregisterXdgSurface(XdgSurfaceInterface *surface)
{
    xdgSurfaces.remove(clientFromXdgSurface(surface), surface);
}

/**
 * @todo Whether the ping is delayed or has timed out is out of domain of the XdgShellInterface.
 * Such matter must be handled somewhere else, e.g. XdgToplevelClient, not here!
 */
void XdgShellInterfacePrivate::registerPing(quint32 serial)
{
    QTimer *timer = new QTimer(q);
    timer->setInterval(1000);
    QObject::connect(timer, &QTimer::timeout, q, [this, serial, attempt = 0]() mutable {
        ++attempt;
        if (attempt == 1) {
            emit q->pingDelayed(serial);
            return;
        }
        emit q->pingTimeout(serial);
        delete pings.take(serial);
    });
    pings.insert(serial, timer);
    timer->start();
}

XdgShellInterfacePrivate *XdgShellInterfacePrivate::get(XdgShellInterface *shell)
{
    return shell->d.data();
}

void XdgShellInterfacePrivate::xdg_wm_base_destroy(Resource *resource)
{
    if (xdgSurfaces.contains(resource->client())) {
        wl_resource_post_error(resource->handle, XDG_WM_BASE_ERROR_DEFUNCT_SURFACES,
                               "xdg_wm_base was destroyed before children");
        return;
    }
    wl_resource_destroy(resource->handle);
}

void XdgShellInterfacePrivate::xdg_wm_base_create_positioner(Resource *resource, uint32_t id)
{
    wl_resource *positionerResource = wl_resource_create(resource->client(), &xdg_positioner_interface,
                                                         wl_resource_get_version(resource->handle), id);
    new XdgPositionerPrivate(positionerResource);
}

void XdgShellInterfacePrivate::xdg_wm_base_get_xdg_surface(Resource *resource, uint32_t id,
                                                           ::wl_resource *surfaceResource)
{
    SurfaceInterface *surface = SurfaceInterface::get(surfaceResource);

    if (surface->buffer()) {
        wl_resource_post_error(resource->handle, XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
                               "xdg_surface must not have a buffer at creation");
        return;
    }

    wl_resource *xdgSurfaceResource = wl_resource_create(resource->client(), &xdg_surface_interface,
                                                         wl_resource_get_version(resource->handle), id);

    XdgSurfaceInterface *xdgSurface = new XdgSurfaceInterface(q, surface, xdgSurfaceResource);
    registerXdgSurface(xdgSurface);
}

void XdgShellInterfacePrivate::xdg_wm_base_pong(Resource *resource, uint32_t serial)
{
    Q_UNUSED(resource)
    if (QTimer *timer = pings.take(serial)) {
        delete timer;
        emit q->pongReceived(serial);
    }
}

XdgShellInterface::XdgShellInterface(Display *display, QObject *parent)
    : QObject(parent)
    , d(new XdgShellInterfacePrivate(this))
{
    d->display = display;
    d->init(*display, 1);
}

XdgShellInterface::~XdgShellInterface()
{
}

Display *XdgShellInterface::display() const
{
    return d->display;
}

quint32 XdgShellInterface::ping(XdgSurfaceInterface *surface)
{
    ::wl_client *client = clientFromXdgSurface(surface);

    XdgShellInterfacePrivate::Resource *clientResource = d->resourceMap().value(client);
    if (!clientResource)
        return 0;

    quint32 serial = d->display->nextSerial();
    d->send_ping(clientResource->handle, serial);
    d->registerPing(serial);

    return serial;
}

XdgSurfaceInterfacePrivate::XdgSurfaceInterfacePrivate(XdgSurfaceInterface *xdgSurface)
    : q(xdgSurface)
{
}

void XdgSurfaceInterfacePrivate::commit()
{
    if (current.windowGeometry != next.windowGeometry) {
        current.windowGeometry = next.windowGeometry;
        emit q->windowGeometryChanged(current.windowGeometry);
    }
}

XdgSurfaceInterfacePrivate *XdgSurfaceInterfacePrivate::get(XdgSurfaceInterface *surface)
{
    return surface->d.data();
}

void XdgSurfaceInterfacePrivate::xdg_surface_destroy_resource(Resource *resource)
{
    Q_UNUSED(resource)
    XdgShellInterfacePrivate::get(shell)->unregisterXdgSurface(q);
    delete q;
}

void XdgSurfaceInterfacePrivate::xdg_surface_destroy(Resource *resource)
{
    if (toplevel || popup) {
        qWarning() << "Tried to destroy xdg_surface before its role object";
    }
    wl_resource_destroy(resource->handle);
}

void XdgSurfaceInterfacePrivate::xdg_surface_get_toplevel(Resource *resource, uint32_t id)
{
    if (SurfaceRole::get(surface)) {
        wl_resource_post_error(resource->handle, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
                               "xdg_surface has already been constructured");
        return;
    }

    wl_resource *toplevelResource = wl_resource_create(resource->client(), &xdg_toplevel_interface,
                                                       wl_resource_get_version(resource->handle), id);

    toplevel = new XdgToplevelInterface(q, toplevelResource);
    emit shell->toplevelCreated(toplevel);
}

void XdgSurfaceInterfacePrivate::xdg_surface_get_popup(Resource *resource, uint32_t id,
                                                       ::wl_resource *parentResource,
                                                       ::wl_resource *positionerResource)
{
    if (SurfaceRole::get(surface)) {
        wl_resource_post_error(resource->handle, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
                               "xdg_surface has already been constructured");
        return;
    }

    XdgPositioner positioner = XdgPositioner::get(positionerResource);
    if (!positioner.isComplete()) {
        wl_resource_post_error(resource->handle, XDG_WM_BASE_ERROR_INVALID_POSITIONER,
                               "xdg_positioner is incomplete");
        return;
    }

    // Notice that the parent surface may be null, in which case it must be specified via
    // "some other protocol", before committing the initial state. However, we don't have
    // support for any such protocol right now.
    XdgSurfaceInterface *parentSurface = XdgSurfaceInterface::get(parentResource);
    if (!parentSurface) {
        wl_resource_post_error(resource->handle, -1, "parent surface is not set");
        return;
    }

    wl_resource *popupResource = wl_resource_create(resource->client(), &xdg_popup_interface,
                                                    wl_resource_get_version(resource->handle), id);

    popup = new XdgPopupInterface(q, parentSurface, positioner, popupResource);
    emit shell->popupCreated(popup);
}

void XdgSurfaceInterfacePrivate::xdg_surface_set_window_geometry(Resource *resource,
                                                                 int32_t x, int32_t y,
                                                                 int32_t width, int32_t height)
{
    if (!toplevel && !popup) {
        wl_resource_post_error(resource->handle, XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                               "xdg_surface must have a role");
        return;
    }

    if (width < 1 || height < 1) {
        wl_resource_post_error(resource->handle, -1, "invalid window geometry size");
        return;
    }

    next.windowGeometry = QRect(x, y, width, height);
}

void XdgSurfaceInterfacePrivate::xdg_surface_ack_configure(Resource *resource, uint32_t serial)
{
    Q_UNUSED(resource)
    emit q->configureAcknowledged(serial);
}

XdgSurfaceInterface::XdgSurfaceInterface(XdgShellInterface *shell, SurfaceInterface *surface,
                                         ::wl_resource *resource)
    : d(new XdgSurfaceInterfacePrivate(this))
{
    d->shell = shell;
    d->surface = surface;
    d->init(resource);
}

XdgSurfaceInterface::~XdgSurfaceInterface()
{
}

XdgToplevelInterface *XdgSurfaceInterface::toplevel() const
{
    return d->toplevel;
}

XdgPopupInterface *XdgSurfaceInterface::popup() const
{
    return d->popup;
}

XdgShellInterface *XdgSurfaceInterface::shell() const
{
    return d->shell;
}

SurfaceInterface *XdgSurfaceInterface::surface() const
{
    return d->surface;
}

QRect XdgSurfaceInterface::windowGeometry() const
{
    return d->current.windowGeometry;
}

XdgSurfaceInterface *XdgSurfaceInterface::get(::wl_resource *resource)
{
    if (auto surface = QtWaylandServer::xdg_surface::Resource::fromResource(resource)) {
        return static_cast<XdgSurfaceInterfacePrivate *>(surface->object())->q;
    }
    return nullptr;
}

XdgToplevelInterfacePrivate::XdgToplevelInterfacePrivate(XdgToplevelInterface *toplevel,
                                                         XdgSurfaceInterface *surface)
    : SurfaceRole(surface->surface())
    , q(toplevel)
    , xdgSurface(surface)
{
}

void XdgToplevelInterfacePrivate::commit()
{
    auto xdgSurfacePrivate = XdgSurfaceInterfacePrivate::get(xdgSurface);

    if (xdgSurfacePrivate->isConfigured) {
        xdgSurfacePrivate->commit();
    } else {
        emit q->initializeRequested();
        return;
    }

    if (current.minimumSize != next.minimumSize) {
        current.minimumSize = next.minimumSize;
        emit q->minimumSizeChanged(current.minimumSize);
    }
    if (current.maximumSize != next.maximumSize) {
        current.maximumSize = next.maximumSize;
        emit q->maximumSizeChanged(current.maximumSize);
    }
}

void XdgToplevelInterfacePrivate::xdg_toplevel_destroy_resource(Resource *resource)
{
    Q_UNUSED(resource)
    delete q;
}

void XdgToplevelInterfacePrivate::xdg_toplevel_destroy(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

void XdgToplevelInterfacePrivate::xdg_toplevel_set_parent(Resource *resource,
                                                          ::wl_resource *parentResource)
{
    Q_UNUSED(resource)
    XdgToplevelInterface *parent = XdgToplevelInterface::get(parentResource);
    if (parentXdgToplevel == parent) {
        return;
    }
    parentXdgToplevel = parent;
    emit q->parentXdgToplevelChanged();
}

void XdgToplevelInterfacePrivate::xdg_toplevel_set_title(Resource *resource, const QString &title)
{
    Q_UNUSED(resource)
    if (windowTitle == title) {
        return;
    }
    windowTitle = title;
    emit q->windowTitleChanged(title);
}

void XdgToplevelInterfacePrivate::xdg_toplevel_set_app_id(Resource *resource, const QString &app_id)
{
    Q_UNUSED(resource)
    if (windowClass == app_id) {
        return;
    }
    windowClass = app_id;
    emit q->windowClassChanged(app_id);
}

void XdgToplevelInterfacePrivate::xdg_toplevel_show_window_menu(Resource *resource, ::wl_resource *seatResource,
                                                                uint32_t serial, int32_t x, int32_t y)
{
    auto xdgSurfacePrivate = XdgSurfaceInterfacePrivate::get(xdgSurface);

    if (!xdgSurfacePrivate->isConfigured) {
        wl_resource_post_error(resource->handle, XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                               "surface has not been configured yet");
        return;
    }

    SeatInterface *seat = SeatInterface::get(seatResource);
    emit q->windowMenuRequested(seat, QPoint(x, y), serial);
}

void XdgToplevelInterfacePrivate::xdg_toplevel_move(Resource *resource, ::wl_resource *seatResource, uint32_t serial)
{
    auto xdgSurfacePrivate = XdgSurfaceInterfacePrivate::get(xdgSurface);

    if (!xdgSurfacePrivate->isConfigured) {
        wl_resource_post_error(resource->handle, XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                               "surface has not been configured yet");
        return;
    }

    SeatInterface *seat = SeatInterface::get(seatResource);
    emit q->moveRequested(seat, serial);
}

void XdgToplevelInterfacePrivate::xdg_toplevel_resize(Resource *resource, ::wl_resource *seatResource,
                                                      uint32_t serial, uint32_t xdgEdges)
{
    auto xdgSurfacePrivate = XdgSurfaceInterfacePrivate::get(xdgSurface);

    if (!xdgSurfacePrivate->isConfigured) {
        wl_resource_post_error(resource->handle, XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                               "surface has not been configured yet");
        return;
    }

    SeatInterface *seat = SeatInterface::get(seatResource);

    Qt::Edges edges;
    if (xdgEdges & XDG_TOPLEVEL_RESIZE_EDGE_TOP) {
        edges |= Qt::TopEdge;
    }
    if (xdgEdges & XDG_TOPLEVEL_RESIZE_EDGE_RIGHT) {
        edges |= Qt::RightEdge;
    }
    if (xdgEdges & XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM) {
        edges |= Qt::BottomEdge;
    }
    if (xdgEdges & XDG_TOPLEVEL_RESIZE_EDGE_LEFT) {
        edges |= Qt::LeftEdge;
    }

    emit q->resizeRequested(seat, edges, serial);
}

void XdgToplevelInterfacePrivate::xdg_toplevel_set_max_size(Resource *resource, int32_t width, int32_t height)
{
    if (width < 0 || height < 0) {
        wl_resource_post_error(resource->handle, -1, "width and height must be positive or zero");
        return;
    }
    next.maximumSize = QSize(width, height);
}

void XdgToplevelInterfacePrivate::xdg_toplevel_set_min_size(Resource *resource, int32_t width, int32_t height)
{
    if (width < 0 || height < 0) {
        wl_resource_post_error(resource->handle, -1, "width and height must be positive or zero");
        return;
    }
    next.minimumSize = QSize(width, height);
}

void XdgToplevelInterfacePrivate::xdg_toplevel_set_maximized(Resource *resource)
{
    Q_UNUSED(resource)
    emit q->maximizeRequested();
}

void XdgToplevelInterfacePrivate::xdg_toplevel_unset_maximized(Resource *resource)
{
    Q_UNUSED(resource)
    emit q->unmaximizeRequested();
}

void XdgToplevelInterfacePrivate::xdg_toplevel_set_fullscreen(Resource *resource, ::wl_resource *outputResource)
{
    Q_UNUSED(resource)
    OutputInterface *output = OutputInterface::get(outputResource);
    emit q->fullscreenRequested(output);
}

void XdgToplevelInterfacePrivate::xdg_toplevel_unset_fullscreen(Resource *resource)
{
    Q_UNUSED(resource)
    emit q->unfullscreenRequested();
}

void XdgToplevelInterfacePrivate::xdg_toplevel_set_minimized(Resource *resource)
{
    Q_UNUSED(resource)
    emit q->minimizeRequested();
}

XdgToplevelInterface::XdgToplevelInterface(XdgSurfaceInterface *surface, ::wl_resource *resource)
    : d(new XdgToplevelInterfacePrivate(this, surface))
{
    d->init(resource);
}

XdgToplevelInterface::~XdgToplevelInterface()
{
}

XdgShellInterface *XdgToplevelInterface::shell() const
{
    return d->xdgSurface->shell();
}

XdgSurfaceInterface *XdgToplevelInterface::xdgSurface() const
{
    return d->xdgSurface;
}

SurfaceInterface *XdgToplevelInterface::surface() const
{
    return d->xdgSurface->surface();
}

XdgToplevelInterface *XdgToplevelInterface::parentXdgToplevel() const
{
    return d->parentXdgToplevel;
}

QString XdgToplevelInterface::windowTitle() const
{
    return d->windowTitle;
}

QString XdgToplevelInterface::windowClass() const
{
    return d->windowClass;
}

QSize XdgToplevelInterface::minimumSize() const
{
    return d->current.minimumSize.isEmpty() ? QSize(0, 0) : d->current.minimumSize;
}

QSize XdgToplevelInterface::maximumSize() const
{
    return d->current.maximumSize.isEmpty() ? QSize(INT_MAX, INT_MAX) : d->current.maximumSize;
}

quint32 XdgToplevelInterface::sendConfigure(const QSize &size, const States &states)
{
    // Note that the states listed in the configure event must be an array of uint32_t.

    uint32_t statesData[4] = { 0 };
    int i = 0;

    if (states & State::MaximizedHorizontal && states & State::MaximizedVertical) {
        statesData[i++] = XDG_TOPLEVEL_STATE_MAXIMIZED;
    }
    if (states & State::FullScreen) {
        statesData[i++] = XDG_TOPLEVEL_STATE_FULLSCREEN;
    }
    if (states & State::Resizing) {
        statesData[i++] = XDG_TOPLEVEL_STATE_RESIZING;
    }
    if (states & State::Activated) {
        statesData[i++] = XDG_TOPLEVEL_STATE_ACTIVATED;
    }

    const QByteArray xdgStates = QByteArray::fromRawData(reinterpret_cast<char *>(statesData),
                                                         sizeof(uint32_t) * i);
    const quint32 serial = xdgSurface()->shell()->display()->nextSerial();

    d->send_configure(size.width(), size.height(), xdgStates);

    auto xdgSurfacePrivate = XdgSurfaceInterfacePrivate::get(xdgSurface());
    xdgSurfacePrivate->send_configure(serial);
    xdgSurfacePrivate->isConfigured = true;

    return serial;
}

void XdgToplevelInterface::sendClose()
{
    d->send_close();
}

XdgToplevelInterface *XdgToplevelInterface::get(::wl_resource *resource)
{
    if (auto toplevel = QtWaylandServer::xdg_toplevel::Resource::fromResource(resource)) {
        return static_cast<XdgToplevelInterfacePrivate *>(toplevel->object())->q;
    }
    return nullptr;
}

XdgPopupInterfacePrivate::XdgPopupInterfacePrivate(XdgPopupInterface *popup,
                                                   XdgSurfaceInterface *surface)
    : SurfaceRole(surface->surface())
    , q(popup)
    , xdgSurface(surface)
{
}

void XdgPopupInterfacePrivate::commit()
{
    auto xdgSurfacePrivate = XdgSurfaceInterfacePrivate::get(xdgSurface);

    if (xdgSurfacePrivate->isConfigured) {
        xdgSurfacePrivate->commit();
    } else {
        emit q->initializeRequested();
    }
}

void XdgPopupInterfacePrivate::xdg_popup_destroy_resource(Resource *resource)
{
    Q_UNUSED(resource)
    delete q;
}

void XdgPopupInterfacePrivate::xdg_popup_destroy(Resource *resource)
{
    // TODO: We need to post an error with the code XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP if
    // this popup is not the topmost grabbing popup. We most likely need a grab abstraction or
    // something to determine whether the given popup has an explicit grab.
    wl_resource_destroy(resource->handle);
}

void XdgPopupInterfacePrivate::xdg_popup_grab(Resource *resource, ::wl_resource *seatHandle, uint32_t serial)
{
    Q_UNUSED(resource)
    SeatInterface *seat = SeatInterface::get(seatHandle);
    emit q->grabRequested(seat, serial);
}

/**
 * Constructs an XdgPopupInterface for the given xdg-surface @p surface.
 */
XdgPopupInterface::XdgPopupInterface(XdgSurfaceInterface *surface,
                                     XdgSurfaceInterface *parentSurface,
                                     const XdgPositioner &positioner,
                                     ::wl_resource *resource)
    : d(new XdgPopupInterfacePrivate(this, surface))
{
    d->parentXdgSurface = parentSurface;
    d->positioner = positioner;
    d->init(resource);
}

XdgPopupInterface::~XdgPopupInterface()
{
}

XdgSurfaceInterface *XdgPopupInterface::parentXdgSurface() const
{
    return d->parentXdgSurface;
}

XdgSurfaceInterface *XdgPopupInterface::xdgSurface() const
{
    return d->xdgSurface;
}

SurfaceInterface *XdgPopupInterface::surface() const
{
    return d->xdgSurface->surface();
}

XdgPositioner XdgPopupInterface::positioner() const
{
    return d->positioner;
}

quint32 XdgPopupInterface::sendConfigure(const QRect &rect)
{
    const quint32 serial = xdgSurface()->shell()->display()->nextSerial();

    d->send_configure(rect.x(), rect.y(), rect.width(), rect.height());

    auto xdgSurfacePrivate = XdgSurfaceInterfacePrivate::get(xdgSurface());
    xdgSurfacePrivate->send_configure(serial);
    xdgSurfacePrivate->isConfigured = true;

    return serial;
}

void XdgPopupInterface::sendPopupDone()
{
    d->send_popup_done();
}

XdgPopupInterface *XdgPopupInterface::get(::wl_resource *resource)
{
    if (auto popup = QtWaylandServer::xdg_popup::Resource::fromResource(resource)) {
        return static_cast<XdgPopupInterfacePrivate *>(popup->object())->q;
    }
    return nullptr;
}

XdgPositionerPrivate::XdgPositionerPrivate(::wl_resource *resource)
    : data(new XdgPositionerData)
{
    init(resource);
}

XdgPositionerPrivate *XdgPositionerPrivate::get(wl_resource *resource)
{
    if (auto positioner = QtWaylandServer::xdg_positioner::Resource::fromResource(resource)) {
        return static_cast<XdgPositionerPrivate *>(positioner->object());
    }
    return nullptr;
}

void XdgPositionerPrivate::xdg_positioner_destroy_resource(Resource *resource)
{
    Q_UNUSED(resource)
    delete this;
}

void XdgPositionerPrivate::xdg_positioner_destroy(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

void XdgPositionerPrivate::xdg_positioner_set_size(Resource *resource, int32_t width, int32_t height)
{
    if (width < 1 || height < 1) {
        wl_resource_post_error(resource->handle, XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "width and height must be positive and non-zero");
        return;
    }
    data->size = QSize(width, height);
}

void XdgPositionerPrivate::xdg_positioner_set_anchor_rect(Resource *resource, int32_t x, int32_t y,
                                                   int32_t width, int32_t height)
{
    if (width < 1 || height < 1) {
        wl_resource_post_error(resource->handle, XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "width and height must be positive and non-zero");
        return;
    }
    data->anchorRect = QRect(x, y, width, height);
}

void XdgPositionerPrivate::xdg_positioner_set_anchor(Resource *resource, uint32_t anchor)
{
    if (anchor > XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT) {
        wl_resource_post_error(resource->handle, XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "unknown anchor point");
        return;
    }

    switch (anchor) {
    case XDG_POSITIONER_ANCHOR_TOP:
        data->anchorEdges = Qt::TopEdge;
        break;
    case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
        data->anchorEdges = Qt::TopEdge | Qt::RightEdge;
        break;
    case XDG_POSITIONER_ANCHOR_RIGHT:
        data->anchorEdges = Qt::RightEdge;
        break;
    case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
        data->anchorEdges = Qt::BottomEdge | Qt::RightEdge;
        break;
    case XDG_POSITIONER_ANCHOR_BOTTOM:
        data->anchorEdges = Qt::BottomEdge;
        break;
    case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
        data->anchorEdges = Qt::BottomEdge | Qt::LeftEdge;
        break;
    case XDG_POSITIONER_ANCHOR_LEFT:
        data->anchorEdges = Qt::LeftEdge;
        break;
    case XDG_POSITIONER_ANCHOR_TOP_LEFT:
        data->anchorEdges = Qt::TopEdge | Qt::LeftEdge;
        break;
    default:
        data->anchorEdges = Qt::Edges();
        break;
    }
}

void XdgPositionerPrivate::xdg_positioner_set_gravity(Resource *resource, uint32_t gravity)
{
    if (gravity > XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT) {
        wl_resource_post_error(resource->handle, XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "unknown gravity direction");
        return;
    }

    switch (gravity) {
    case XDG_POSITIONER_GRAVITY_TOP:
        data->gravityEdges = Qt::TopEdge;
        break;
    case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
        data->gravityEdges = Qt::TopEdge | Qt::RightEdge;
        break;
    case XDG_POSITIONER_GRAVITY_RIGHT:
        data->gravityEdges = Qt::RightEdge;
        break;
    case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT:
        data->gravityEdges = Qt::BottomEdge | Qt::RightEdge;
        break;
    case XDG_POSITIONER_GRAVITY_BOTTOM:
        data->gravityEdges = Qt::BottomEdge;
        break;
    case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
        data->gravityEdges = Qt::BottomEdge | Qt::LeftEdge;
        break;
    case XDG_POSITIONER_GRAVITY_LEFT:
        data->gravityEdges = Qt::LeftEdge;
        break;
    case XDG_POSITIONER_GRAVITY_TOP_LEFT:
        data->gravityEdges = Qt::TopEdge | Qt::LeftEdge;
        break;
    default:
        data->gravityEdges = Qt::Edges();
        break;
    }
}

void XdgPositionerPrivate::xdg_positioner_set_constraint_adjustment(Resource *resource,
                                                                    uint32_t constraint_adjustment)
{
    Q_UNUSED(resource)

    if (constraint_adjustment & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X) {
        data->flipConstraintAdjustments |= Qt::Horizontal;
    } else {
        data->flipConstraintAdjustments &= ~Qt::Horizontal;
    }

    if (constraint_adjustment & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y) {
        data->flipConstraintAdjustments |= Qt::Vertical;
    } else {
        data->flipConstraintAdjustments &= ~Qt::Vertical;
    }

    if (constraint_adjustment & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X) {
        data->slideConstraintAdjustments |= Qt::Horizontal;
    } else {
        data->slideConstraintAdjustments &= ~Qt::Horizontal;
    }

    if (constraint_adjustment & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y) {
        data->slideConstraintAdjustments |= Qt::Vertical;
    } else {
        data->slideConstraintAdjustments &= ~Qt::Vertical;
    }

    if (constraint_adjustment & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X) {
        data->resizeConstraintAdjustments |= Qt::Horizontal;
    } else {
        data->resizeConstraintAdjustments &= ~Qt::Horizontal;
    }

    if (constraint_adjustment & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y) {
        data->resizeConstraintAdjustments |= Qt::Vertical;
    } else {
        data->resizeConstraintAdjustments &= ~Qt::Vertical;
    }
}

void XdgPositionerPrivate::xdg_positioner_set_offset(Resource *resource, int32_t x, int32_t y)
{
    Q_UNUSED(resource)
    data->offset = QPoint(x, y);
}

XdgPositioner::XdgPositioner()
    : d(new XdgPositionerData)
{
}

XdgPositioner::XdgPositioner(const XdgPositioner &other)
    : d(other.d)
{
}

XdgPositioner::~XdgPositioner()
{
}

XdgPositioner &XdgPositioner::operator=(const XdgPositioner &other)
{
    d = other.d;
    return *this;
}

bool XdgPositioner::isComplete() const
{
    return d->size.isValid() && d->anchorRect.isValid();
}

Qt::Orientations XdgPositioner::slideConstraintAdjustments() const
{
    return d->slideConstraintAdjustments;
}

Qt::Orientations XdgPositioner::flipConstraintAdjustments() const
{
    return d->flipConstraintAdjustments;
}

Qt::Orientations XdgPositioner::resizeConstraintAdjustments() const
{
    return d->resizeConstraintAdjustments;
}

Qt::Edges XdgPositioner::anchorEdges() const
{
    return d->anchorEdges;
}

Qt::Edges XdgPositioner::gravityEdges() const
{
    return d->gravityEdges;
}

QSize XdgPositioner::size() const
{
    return d->size;
}

QRect XdgPositioner::anchorRect() const
{
    return d->anchorRect;
}

QPoint XdgPositioner::offset() const
{
    return d->offset;
}

XdgPositioner XdgPositioner::get(::wl_resource *resource)
{
    XdgPositionerPrivate *xdgPositionerPrivate = XdgPositionerPrivate::get(resource);
    if (xdgPositionerPrivate) {
        return XdgPositioner(xdgPositionerPrivate->data);
    }
    return XdgPositioner();
}

XdgPositioner::XdgPositioner(const QSharedDataPointer<XdgPositionerData> &data)
    : d(data)
{
}

} // namespace KWin
