/*
 * Copyright (C) 2018 Marius Gripsgard <marius@ubports.com>
 * Copyright (C) 2020 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "xwayland_wm_shellsurface.h"
#include "xwayland_wm_surface.h"
#include "xwayland_log.h"

#include <string.h>

#include <experimental/optional>
#include <map>

namespace mf = mir::frontend;

namespace
{
auto wm_resize_edge_to_mir_resize_edge(uint32_t wm_resize_edge) -> std::experimental::optional<MirResizeEdge>
{
    switch (wm_resize_edge)
    {
    case _NET_WM_MOVERESIZE_SIZE_TOP:           return mir_resize_edge_north;
    case _NET_WM_MOVERESIZE_SIZE_BOTTOM:        return mir_resize_edge_south;
    case _NET_WM_MOVERESIZE_SIZE_LEFT:          return mir_resize_edge_west;
    case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:       return mir_resize_edge_northwest;
    case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:    return mir_resize_edge_southwest;
    case _NET_WM_MOVERESIZE_SIZE_RIGHT:         return mir_resize_edge_east;
    case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:      return mir_resize_edge_northeast;
    case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:   return mir_resize_edge_southeast;
    default:                                    return std::experimental::nullopt;
    }
}
}

mf::XWaylandWMSurface::XWaylandWMSurface(XWaylandWM *wm, xcb_create_notify_event_t *event)
    : xwm(wm),
      window(event->window),
      props_dirty(true),
      init{
          event->parent,
          {event->x, event->y},
          {event->width, event->height},
          (bool)event->override_redirect},
      shell_surface_destroyed{std::make_shared<bool>(true)}
{
    xcb_get_geometry_cookie_t const geometry_cookie = xcb_get_geometry(xwm->get_xcb_connection(), window);

    uint32_t const values[]{
        XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE};
    xcb_change_window_attributes(xwm->get_xcb_connection(), window, XCB_CW_EVENT_MASK, values);

    xcb_get_geometry_reply_t* const geometry_reply =
        xcb_get_geometry_reply(xwm->get_xcb_connection(), geometry_cookie, nullptr);

    if (!geometry_reply)
        mir::log_error("xcb gemom reply faled");
}

mf::XWaylandWMSurface::~XWaylandWMSurface()
{
    aquire_shell_surface([](auto shell_surface)
        {
            delete shell_surface;
        });
}

void mf::XWaylandWMSurface::dirty_properties()
{
    std::lock_guard<std::mutex> lock{mutex};
    props_dirty = true;
}

void mf::XWaylandWMSurface::set_surface(WlSurface* wayland_surface)
{
    auto const shell_surface = xwm->build_shell_surface(this, wayland_surface);
    shell_surface_unsafe = shell_surface;
    shell_surface_destroyed = shell_surface->destroyed_flag();
    shell_surface->set_server_side_decorated(!init.override_redirect);

    {
        std::lock_guard<std::mutex> lock{mutex};
        if (!properties.title.empty())
            shell_surface->set_title(properties.title);
    }

    // If a buffer has alread been committed, we need to create the scene::Surface without waiting for next commit
    if (wayland_surface->buffer_size())
        shell_surface->create_scene_surface();

    xcb_flush(xwm->get_xcb_connection());
}

void mf::XWaylandWMSurface::set_workspace(int workspace)
{
    // Passing a workspace < 0 deletes the property
    if (workspace >= 0)
    {
        xcb_change_property(xwm->get_xcb_connection(), XCB_PROP_MODE_REPLACE, window, xwm->xcb_atom.net_wm_desktop,
                            XCB_ATOM_CARDINAL, 31, 1, &workspace);
    }
    else
    {
        xcb_delete_property(xwm->get_xcb_connection(), window, xwm->xcb_atom.net_wm_desktop);
    }
    xcb_flush(xwm->get_xcb_connection());
}

void mf::XWaylandWMSurface::set_wm_state(WmState state)
{
    uint32_t const properties[]{
        state,
        XCB_WINDOW_NONE // Icon window
    };

    xcb_change_property(
        xwm->get_xcb_connection(),
        XCB_PROP_MODE_REPLACE,
        window, xwm->xcb_atom.wm_state,
        xwm->xcb_atom.wm_state, 32,
        length_of(properties), properties);
    xcb_flush(xwm->get_xcb_connection());
}

void mf::XWaylandWMSurface::set_net_wm_state()
{
    std::vector<uint32_t> properties;

    {
        std::lock_guard<std::mutex> lock{mutex};

        if (fullscreen)
            properties.push_back(xwm->xcb_atom.net_wm_state_fullscreen);

        if (maximized)
        {
            properties.push_back(xwm->xcb_atom.net_wm_state_maximized_horz);
            properties.push_back(xwm->xcb_atom.net_wm_state_maximized_vert);
        }
    }

    xcb_change_property(
        xwm->get_xcb_connection(),
        XCB_PROP_MODE_REPLACE,
        window,
        xwm->xcb_atom.net_wm_state,
        XCB_ATOM_ATOM, 32, // type and format
        properties.size(), properties.data());

    xcb_flush(xwm->get_xcb_connection());
}

void mf::XWaylandWMSurface::read_properties()
{
    std::lock_guard<std::mutex> lock{mutex};

    if (!props_dirty)
        return;
    props_dirty = false;

    std::map<xcb_atom_t, xcb_atom_t> props;
    props[XCB_ATOM_WM_CLASS] = XCB_ATOM_STRING;
    props[XCB_ATOM_WM_NAME] = XCB_ATOM_STRING;
    props[XCB_ATOM_WM_TRANSIENT_FOR] = XCB_ATOM_WINDOW;
    props[xwm->xcb_atom.wm_protocols] = TYPE_WM_PROTOCOLS;
    props[xwm->xcb_atom.wm_normal_hints] = TYPE_WM_NORMAL_HINTS;
    props[xwm->xcb_atom.net_wm_state] = TYPE_NET_WM_STATE;
    props[xwm->xcb_atom.net_wm_window_type] = XCB_ATOM_ATOM;
    props[xwm->xcb_atom.net_wm_name] = XCB_ATOM_STRING;
    props[xwm->xcb_atom.motif_wm_hints] = TYPE_MOTIF_WM_HINTS;

    std::map<xcb_atom_t, xcb_get_property_cookie_t> cookies;
    for (const auto &atom : props)
    {
        xcb_get_property_cookie_t cookie =
            xcb_get_property(xwm->get_xcb_connection(), 0, window, atom.first, XCB_ATOM_ANY, 0, 2048);
        cookies[atom.first] = cookie;
    }

    properties.deleteWindow = 0;

    for (const auto &atom_ptr : props)
    {
        xcb_atom_t atom = atom_ptr.first;
        xcb_get_property_reply_t *reply = xcb_get_property_reply(xwm->get_xcb_connection(), cookies[atom], nullptr);
        if (!reply)
        {
            // Bad window, usually
            continue;
        }

        if (reply->type == XCB_ATOM_NONE)
        {
            // No such info
            free(reply);
            continue;
        }

        switch (props[atom])
        {
        case XCB_ATOM_STRING:
        {
            char *p = strndup(reinterpret_cast<char *>(xcb_get_property_value(reply)),
                              xcb_get_property_value_length(reply));
            if (atom == XCB_ATOM_WM_CLASS) {
                properties.appId = std::string(p);
            } else if (atom == XCB_ATOM_WM_NAME || xwm->xcb_atom.net_wm_name) {
                properties.title = std::string(p);
            } else {
                free(p);
            }
            break;
        }
        case XCB_ATOM_WINDOW:
        {
            break;
        }
        case XCB_ATOM_ATOM:
        {
            if (atom == xwm->xcb_atom.net_wm_window_type)
            {
            }
            break;
        }
        case TYPE_WM_PROTOCOLS:
        {
            xcb_atom_t *atoms = reinterpret_cast<xcb_atom_t *>(xcb_get_property_value(reply));
            for (uint32_t i = 0; i < reply->value_len; ++i)
                if (atoms[i] == xwm->xcb_atom.wm_delete_window)
                    properties.deleteWindow = 1;
            break;
        }
        case TYPE_WM_NORMAL_HINTS:
        {
            break;
        }
        case TYPE_NET_WM_STATE:
        {
            xcb_atom_t *value = reinterpret_cast<xcb_atom_t *>(xcb_get_property_value(reply));
            uint32_t i;
            for (i = 0; i < reply->value_len; i++)
            {
                if (value[i] == xwm->xcb_atom.net_wm_state_fullscreen && !fullscreen)
                {
                    fullscreen = true;
                }
            }
            if (value[i] == xwm->xcb_atom.net_wm_state_maximized_horz && !maximized)
            {
                maximized = true;
            }
            if (value[i] == xwm->xcb_atom.net_wm_state_maximized_vert && !maximized)
            {
                maximized = true;
            }
            break;
        }
        case TYPE_MOTIF_WM_HINTS:
            break;
        default:
            break;
        }

        free(reply);
    }
}

void mf::XWaylandWMSurface::move_resize(uint32_t detail)
{

    if (detail == _NET_WM_MOVERESIZE_MOVE)
    {
        aquire_shell_surface([](auto shell_surface)
        {
            shell_surface->initiate_interactive_move();
        });
    }
    else if (auto const edge = wm_resize_edge_to_mir_resize_edge(detail))
    {
        aquire_shell_surface([edge](auto shell_surface)
        {
            shell_surface->initiate_interactive_resize(edge.value());
        });
    }
    else
    {
        mir::log_warning("XWaylandWMSurface::move_resize() called with unknown detail %d", detail);
    }
}

void mf::XWaylandWMSurface::send_resize(const geometry::Size& new_size)
{
    uint32_t const mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;

    uint32_t const values[]{
        new_size.width.as_uint32_t(),
        new_size.height.as_uint32_t()};

    xcb_configure_window(xwm->get_xcb_connection(), window, mask, values);
    xcb_flush(xwm->get_xcb_connection());
}

void mf::XWaylandWMSurface::send_close_request()
{
    xcb_destroy_window(xwm->get_xcb_connection(), window);
    xcb_flush(xwm->get_xcb_connection());
}

void mf::XWaylandWMSurface::aquire_shell_surface(std::function<void(XWaylandWMShellSurface* shell_surface)>&& work)
{
    xwm->run_on_wayland_thread(
        [work = move(work), shell_surface = shell_surface_unsafe, destroyed = shell_surface_destroyed]()
        {
            if (!*destroyed)
                work(shell_surface);
        });
}
