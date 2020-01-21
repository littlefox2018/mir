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
 * Written with alot of help of Weston Xwm
 *
 */

#include "xwayland_wm.h"
#include "xwayland_log.h"
#include "xwayland_wm_shellsurface.h"
#include "xwayland_wm_surface.h"
#include "xwayland_wm_shell.h"

#include "mir/dispatch/multiplexing_dispatchable.h"
#include "mir/dispatch/readable_fd.h"
#include "mir/fd.h"
#include "mir/terminate_with_current_exception.h"

#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sstream>
#include <boost/throw_exception.hpp>

#ifndef ARRAY_LENGTH
#define ARRAY_LENGTH(a) mir::frontend::length_of(a)
#endif

#define CURSOR_ENTRY(x)        \
    {                          \
        (x), ARRAY_LENGTH((x)) \
    }

static const char *bottom_left_corners[] = {"bottom_left_corner", "sw-resize", "size_bdiag"};

static const char *bottom_right_corners[] = {"bottom_right_corner", "se-resize", "size_fdiag"};

static const char *bottom_sides[] = {"bottom_side", "s-resize", "size_ver"};

static const char *left_ptrs[] = {"left_ptr", "default", "top_left_arrow", "left-arrow"};

static const char *left_sides[] = {"left_side", "w-resize", "size_hor"};

static const char *right_sides[] = {"right_side", "e-resize", "size_hor"};

static const char *top_left_corners[] = {"top_left_corner", "nw-resize", "size_fdiag"};

static const char *top_right_corners[] = {"top_right_corner", "ne-resize", "size_bdiag"};

static const char *top_sides[] = {"top_side", "n-resize", "size_ver"};

struct cursor_alternatives
{
    const char **names;
    size_t count;
};

static const struct cursor_alternatives cursors[] = {
    CURSOR_ENTRY(top_sides),           CURSOR_ENTRY(bottom_sides),         CURSOR_ENTRY(left_sides),
    CURSOR_ENTRY(right_sides),         CURSOR_ENTRY(top_left_corners),     CURSOR_ENTRY(top_right_corners),
    CURSOR_ENTRY(bottom_left_corners), CURSOR_ENTRY(bottom_right_corners), CURSOR_ENTRY(left_ptrs)};

namespace mf = mir::frontend;

namespace
{
template<typename T>
auto data_buffer_to_debug_string(T* data, size_t elements) -> std::string
{
    std::stringstream ss{"["};
    for (T* i = data; i != data + elements; i++)
    {
        if (i != data)
            ss << ", ";
        ss << *i;
    }
    ss << "]";
    return ss.str();
}
}

mf::XWaylandWM::XWaylandWM(std::shared_ptr<WaylandConnector> wayland_connector, wl_client* wayland_client, int fd)
    : wm_fd{fd},
      xcb_connection{xcb_connect_to_fd(wm_fd, nullptr)},
      xcb_atom{xcb_connection},
      wayland_connector(wayland_connector),
      dispatcher{std::make_shared<mir::dispatch::MultiplexingDispatchable>()},
      wayland_client{wayland_client}
{
    start();
}

mf::XWaylandWM::~XWaylandWM()
{
    destroy();
}

void mf::XWaylandWM::destroy() {
  if (event_thread) {
    dispatcher->remove_watch(wm_dispatcher);
    event_thread.reset();
  }

  // xcb_cursors == 2 when its empty
  if (xcb_cursors.size() != 2) {
    mir::log_info("Cleaning cursors");
    for (auto xcb_cursor : xcb_cursors)
      xcb_free_cursor(xcb_connection, xcb_cursor);
  }
  if (xcb_connection != nullptr)
    xcb_disconnect(xcb_connection);
  close(wm_fd);
}

void mf::XWaylandWM::start()
{
    if (xcb_connection_has_error(xcb_connection))
    {
        mir::log_error("XWAYLAND: xcb_connect_to_fd failed");
        close(wm_fd);
        return;
    }

    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(xcb_connection));
    xcb_screen = iter.data;

    wm_dispatcher =
        std::make_shared<mir::dispatch::ReadableFd>(mir::Fd{mir::IntOwnedFd{wm_fd}}, [this]() { handle_events(); });
    dispatcher->add_watch(wm_dispatcher);

    event_thread = std::make_unique<mir::dispatch::ThreadedDispatcher>(
        "Mir/X11 WM Reader", dispatcher, []() { mir::terminate_with_current_exception(); });

    wm_get_resources();
    setup_visual_and_colormap();

    uint32_t const attrib_values[]{
        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_PROPERTY_CHANGE};

    xcb_change_window_attributes(xcb_connection, xcb_screen->root, XCB_CW_EVENT_MASK, attrib_values);

    xcb_composite_redirect_subwindows(xcb_connection, xcb_screen->root, XCB_COMPOSITE_REDIRECT_MANUAL);

    xcb_atom_t const supported[]{
        xcb_atom.net_wm_moveresize,
        xcb_atom.net_wm_state,
        xcb_atom.net_wm_state_fullscreen,
        xcb_atom.net_wm_state_maximized_vert,
        xcb_atom.net_wm_state_maximized_horz,
        xcb_atom.net_active_window};

    xcb_change_property(
        xcb_connection,
        XCB_PROP_MODE_REPLACE,
        xcb_screen->root,
        xcb_atom.net_supported,
        XCB_ATOM_ATOM, 32, // type and format
        length_of(supported), supported);

    set_net_active_window(XCB_WINDOW_NONE);
    wm_selector();

    xcb_flush(xcb_connection);

    create_wm_cursor();
    set_cursor(xcb_screen->root, CursorLeftPointer);

    create_wm_window();
    xcb_flush(xcb_connection);
}

void mf::XWaylandWM::wm_selector()
{
    xcb_selection_request.requestor = XCB_NONE;

    uint32_t const values[]{
        XCB_EVENT_MASK_PROPERTY_CHANGE};

    xcb_selection_window = xcb_generate_id(xcb_connection);

    xcb_create_window(
        xcb_connection,
        XCB_COPY_FROM_PARENT,
        xcb_selection_window,
        xcb_screen->root,
        0, 0, // position
        10, 10, // size
        0, // border width
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        xcb_screen->root_visual,
        XCB_CW_EVENT_MASK,
        values);

    xcb_set_selection_owner(xcb_connection, xcb_selection_window, xcb_atom.clipboard_manager, XCB_TIME_CURRENT_TIME);

    uint32_t const mask =
        XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
        XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
        XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;

    xcb_xfixes_select_selection_input(xcb_connection, xcb_selection_window, xcb_atom.clipboard, mask);
}

void mf::XWaylandWM::set_cursor(xcb_window_t id, const CursorType &cursor)
{
    if (xcb_cursor == cursor)
        return;

    xcb_cursor = cursor;
    uint32_t cursor_value_list = xcb_cursors[cursor];
    xcb_change_window_attributes(xcb_connection, id, XCB_CW_CURSOR, &cursor_value_list);
    xcb_flush(xcb_connection);
}

void mf::XWaylandWM::create_wm_cursor()
{
    const char *name;
    int count = ARRAY_LENGTH(cursors);

    xcb_cursors.clear();
    xcb_cursors.reserve(count);

    for (int i = 0; i < count; i++)
    {
        for (size_t j = 0; j < cursors[i].count; j++)
        {
            name = cursors[i].names[j];
            xcb_cursors.push_back(xcb_cursor_library_load_cursor(name));
            if (xcb_cursors[i] != static_cast<xcb_cursor_t>(-1))
                break;
        }
    }
}

void mf::XWaylandWM::create_wm_window()
{
    std::string const wm_name{"Mir XWM"};

    xcb_window = xcb_generate_id(xcb_connection);
    xcb_create_window(xcb_connection, XCB_COPY_FROM_PARENT, xcb_window, xcb_screen->root, 0, 0, 10, 10, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, xcb_screen->root_visual, 0, NULL);

    xcb_change_property(xcb_connection, XCB_PROP_MODE_REPLACE, xcb_window, xcb_atom.net_supporting_wm_check,
                        XCB_ATOM_WINDOW, 32, /* format */
                        1, &xcb_window);

    xcb_change_property(xcb_connection, XCB_PROP_MODE_REPLACE, xcb_window, xcb_atom.net_wm_name, xcb_atom.utf8_string,
                        8, /* format */
                        wm_name.size(), wm_name.c_str());

    xcb_change_property(xcb_connection, XCB_PROP_MODE_REPLACE, xcb_screen->root, xcb_atom.net_supporting_wm_check,
                        XCB_ATOM_WINDOW, 32, /* format */
                        1, &xcb_window);

    /* Claim the WM_S0 selection even though we don't support
     * the --replace functionality. */
    xcb_set_selection_owner(xcb_connection, xcb_window, xcb_atom.wm_s0, XCB_TIME_CURRENT_TIME);

    xcb_set_selection_owner(xcb_connection, xcb_window, xcb_atom.net_wm_cm_s0, XCB_TIME_CURRENT_TIME);
}

void mf::XWaylandWM::set_net_active_window(xcb_window_t window)
{
    xcb_change_property(xcb_connection, XCB_PROP_MODE_REPLACE, xcb_screen->root, xcb_atom.net_active_window,
                        xcb_atom.window, 32, 1, &window);
}

auto mf::XWaylandWM::build_shell_surface(
    XWaylandWMSurface* wm_surface,
    WlSurface* wayland_surface) -> XWaylandWMShellSurface*
{
    auto const shell = std::static_pointer_cast<XWaylandWMShell>(wayland_connector->get_extension("x11-support"));
    return shell->build_shell_surface(wm_surface, wayland_client, wayland_surface);
}

auto mf::XWaylandWM::get_wm_surface(xcb_window_t xcb_window) -> std::experimental::optional<std::shared_ptr<XWaylandWMSurface>>
{
    auto const surface = surfaces.find(xcb_window);
    if (surface == surfaces.end() || !surface->second)
        return std::experimental::nullopt;
    else
        return surface->second;
}

void mf::XWaylandWM::run_on_wayland_thread(std::function<void()>&& work)
{
    wayland_connector->run_on_wayland_display([work = move(work)](auto){ work(); });
}

/* Events */
void mf::XWaylandWM::handle_events()
{
    bool got_events = false;

    while (xcb_generic_event_t* const event = xcb_poll_for_event(xcb_connection))
    {
        try
        {
            handle_event(event);
        }
        catch (...)
        {
            log(
                logging::Severity::warning,
                MIR_LOG_COMPONENT,
                std::current_exception(),
                "Failed to handle xcb event.");
        }
        free(event);
        got_events = true;
    }

    if (got_events)
    {
        xcb_flush(xcb_connection);
    }
}

void mf::XWaylandWM::handle_event(xcb_generic_event_t* event)
{
    int type = event->response_type & ~0x80;
    switch (type)
    {
    case XCB_BUTTON_PRESS:
    case XCB_BUTTON_RELEASE:
        if (verbose_xwayland_logging_enabled())
            log_debug("XCB_BUTTON_RELEASE");
        //(reinterpret_cast<xcb_button_press_event_t *>(event));
        break;
    case XCB_ENTER_NOTIFY:
        if (verbose_xwayland_logging_enabled())
            log_debug("XCB_ENTER_NOTIFY");
        //(reinterpret_cast<xcb_enter_notify_event_t *>(event));
        break;
    case XCB_LEAVE_NOTIFY:
        if (verbose_xwayland_logging_enabled())
            log_debug("XCB_LEAVE_NOTIFY");
        //(reinterpret_cast<xcb_leave_notify_event_t *>(event));
        break;
    case XCB_MOTION_NOTIFY:
        handle_motion_notify(reinterpret_cast<xcb_motion_notify_event_t *>(event));
        //(reinterpret_cast<xcb_motion_notify_event_t *>(event));
        break;
    case XCB_CREATE_NOTIFY:
        handle_create_notify(reinterpret_cast<xcb_create_notify_event_t *>(event));
        break;
    case XCB_MAP_REQUEST:
        handle_map_request(reinterpret_cast<xcb_map_request_event_t *>(event));
        break;
    case XCB_MAP_NOTIFY:
        if (verbose_xwayland_logging_enabled())
            log_debug("XCB_MAP_NOTIFY");
        //(reinterpret_cast<xcb_map_notify_event_t *>(event));
        break;
    case XCB_UNMAP_NOTIFY:
        handle_unmap_notify(reinterpret_cast<xcb_unmap_notify_event_t *>(event));
        break;
    case XCB_REPARENT_NOTIFY:
        if (verbose_xwayland_logging_enabled())
            log_debug("XCB_REPARENT_NOTIFY");
        //(reinterpret_cast<xcb_reparent_notify_event_t *>(event));
        break;
    case XCB_CONFIGURE_REQUEST:
        handle_configure_request(reinterpret_cast<xcb_configure_request_event_t *>(event));
        break;
    case XCB_CONFIGURE_NOTIFY:
        handle_configure_notify(reinterpret_cast<xcb_configure_notify_event_t *>(event));
        break;
    case XCB_DESTROY_NOTIFY:
        handle_destroy_notify(reinterpret_cast<xcb_destroy_notify_event_t *>(event));
        break;
    case XCB_MAPPING_NOTIFY:
        if (verbose_xwayland_logging_enabled())
            log_debug("XCB_MAPPING_NOTIFY");
        break;
    case XCB_PROPERTY_NOTIFY:
        handle_property_notify(reinterpret_cast<xcb_property_notify_event_t *>(event));
        break;
    case XCB_CLIENT_MESSAGE:
        handle_client_message(reinterpret_cast<xcb_client_message_event_t *>(event));
        break;
    case XCB_FOCUS_IN:
        if (verbose_xwayland_logging_enabled())
            log_debug("XCB_FOCUS_IN");
        //(reinterpret_cast<xcb_focus_in_event_t *>(event));
    default:
        break;
    }
}

void mf::XWaylandWM::handle_property_notify(xcb_property_notify_event_t *event)
{
    if (verbose_xwayland_logging_enabled())
    {
        if (event->state == XCB_PROPERTY_DELETE)
        {
            log_debug(
                "XCB_PROPERTY_NOTIFY (%s).%s: deleted",
                get_window_debug_string(event->window).c_str(),
                get_atom_name(event->atom).c_str());
        }
        else
        {
            xcb_get_property_cookie_t const cookie = xcb_get_property(
                xcb_connection,
                0, // don't delete
                event->window,
                event->atom,
                XCB_ATOM_ANY,
                0,
                2048);

            xcb_get_property_reply_t* const reply = xcb_get_property_reply(xcb_connection, cookie, NULL);

            auto const prop_name = get_atom_name(event->atom);
            auto const reply_str = get_reply_debug_string(reply);

            log_debug(
                "XCB_PROPERTY_NOTIFY (%s).%s: %s",
                get_window_debug_string(event->window).c_str(),
                prop_name.c_str(),
                reply_str.c_str());

            free(reply);
        }
    }

    if (auto const surface = get_wm_surface(event->window))
    {
        surface.value()->dirty_properties();
    }
}

void mf::XWaylandWM::handle_create_notify(xcb_create_notify_event_t *event)
{
    if (verbose_xwayland_logging_enabled())
    {
        log_debug("XCB_CREATE_NOTIFY parent: %s", get_window_debug_string(event->parent).c_str());
        log_debug("                  window: %s", get_window_debug_string(event->window).c_str());
        log_debug("                  position: %d, %d", event->x, event->y);
        log_debug("                  size: %dx%d", event->width, event->height);
        log_debug("                  override_redirect: %s", event->override_redirect ? "yes" : "no");

        if (event->border_width)
            log_warning("border width unsupported (border width %d)", event->border_width);
    }

    if (!is_ours(event->window))
    {
        if (surfaces.find(event->window) != surfaces.end())
            BOOST_THROW_EXCEPTION(
                std::runtime_error(get_window_debug_string(event->window) + " created, but already known"));

        surfaces[event->window] = std::make_shared<XWaylandWMSurface>(this, event);
    }
}

void mf::XWaylandWM::handle_motion_notify(xcb_motion_notify_event_t *event)
{
    if (verbose_xwayland_logging_enabled())
    {
        log_debug("XCB_MOTION_NOTIFY root: %s", get_window_debug_string(event->root).c_str());
        log_debug("                  event: %s", get_window_debug_string(event->event).c_str());
        log_debug("                  child: %s", get_window_debug_string(event->child).c_str());
        log_debug("                  root pos: %d, %d", event->root_x, event->root_y);
        log_debug("                  event pos: %d, %d", event->event_x, event->event_y);
    }
}

void mf::XWaylandWM::handle_destroy_notify(xcb_destroy_notify_event_t *event)
{
    if (verbose_xwayland_logging_enabled())
    {
        log_debug(
            "XCB_DESTROY_NOTIFY window: %s, event: %s",
            get_window_debug_string(event->window).c_str(),
            get_window_debug_string(event->event).c_str());
    }

    surfaces.erase(event->window);
}

void mf::XWaylandWM::handle_map_request(xcb_map_request_event_t *event)
{
    if (verbose_xwayland_logging_enabled())
    {
        log_debug(
            "XCB_MAP_REQUEST %s with parent %s",
            get_window_debug_string(event->window).c_str(),
            get_window_debug_string(event->parent).c_str());
    }

    if (auto const surface = get_wm_surface(event->window))
    {
        surface.value()->read_properties();
        surface.value()->set_workspace(0);
        surface.value()->apply_mir_state_to_window(mir_window_state_restored); // TODO: get the actual state
        xcb_map_window(xcb_connection, event->window);
        xcb_flush(xcb_connection);
    }
}

void mf::XWaylandWM::handle_unmap_notify(xcb_unmap_notify_event_t *event)
{
    if (verbose_xwayland_logging_enabled())
    {
        log_debug(
            "XCB_UNMAP_NOTIFY %s with event %s",
            get_window_debug_string(event->window).c_str(),
            get_window_debug_string(event->event).c_str());
    }

    if (is_ours(event->window))
        return;

    // We just ignore the ICCCM 4.1.4 synthetic unmap notify
    // as it may come in after we've destroyed the window
    if (event->response_type & ~0x80)
        return;

    if (auto const surface = get_wm_surface(event->window))
    {
        surface.value()->unmap();
        surface.value()->set_workspace(-1);
        xcb_unmap_window(xcb_connection, event->window);
        xcb_flush(xcb_connection);
    }
}

void mf::XWaylandWM::handle_client_message(xcb_client_message_event_t *event)
{
    if (verbose_xwayland_logging_enabled())
    {
        log_debug(
            "XCB_CLIENT_MESSAGE %s on %s",
            get_atom_name(event->type).c_str(),
            get_window_debug_string(event->window).c_str());
    }

    if (auto const surface = get_wm_surface(event->window))
    {
        if (event->type == xcb_atom.net_wm_moveresize)
            handle_move_resize(surface.value(), event);
        else if (event->type == xcb_atom.net_wm_state)
            surface.value()->net_wm_state_client_message(event->data.data32);
        else if (event->type == xcb_atom.wm_change_state)
            surface.value()->wm_change_state_client_message(event->data.data32);
        else if (event->type == xcb_atom.wl_surface_id)
            handle_surface_id(surface.value(), event);
    }
}

void mf::XWaylandWM::handle_move_resize(std::shared_ptr<XWaylandWMSurface> surface, xcb_client_message_event_t *event)
{
    if (!surface || !event)
        return;

    int detail = event->data.data32[2];
    surface->move_resize(detail);
}

void mf::XWaylandWM::handle_surface_id(std::shared_ptr<XWaylandWMSurface> surface, xcb_client_message_event_t *event)
{
    if (!surface || !event)
        return;

    uint32_t id = event->data.data32[0];

    wayland_connector->run_on_wayland_display([client=wayland_client, id, surface](auto)
        {
            wl_resource* resource = wl_client_get_object(client, id);
            auto* wlsurface = resource ? WlSurface::from(resource) : nullptr;
            if (wlsurface)
                surface->set_surface(wlsurface);
        });

    // TODO: handle unpaired surfaces!
}

void mf::XWaylandWM::handle_configure_request(xcb_configure_request_event_t *event)
{
    if (verbose_xwayland_logging_enabled())
    {
        log_debug("XCB_CONFIGURE_REQUEST parent: %s", get_window_debug_string(event->parent).c_str());
        log_debug("                      window: %s", get_window_debug_string(event->window).c_str());
        log_debug("                      sibling: %s", get_window_debug_string(event->sibling).c_str());
        log_debug("                      position: %d, %d", event->x, event->y);
        log_debug("                      size: %dx%d", event->width, event->height);

        if (event->border_width)
            log_warning("border width unsupported (border width %d)", event->border_width);
    }

    std::vector<uint32_t> values;

    if (event->value_mask & XCB_CONFIG_WINDOW_X)
    {
        values.push_back(event->x);
    }

    if (event->value_mask & XCB_CONFIG_WINDOW_Y)
    {
        values.push_back(event->y);
    }

    if (event->value_mask & XCB_CONFIG_WINDOW_WIDTH)
    {
        values.push_back(event->width);
    }

    if (event->value_mask & XCB_CONFIG_WINDOW_HEIGHT)
    {
        values.push_back(event->height);
    }

    if (event->value_mask & XCB_CONFIG_WINDOW_SIBLING)
    {
        values.push_back(event->sibling);
    }

    if (event->value_mask & XCB_CONFIG_WINDOW_STACK_MODE)
    {
        values.push_back(event->stack_mode);
    }

    if (!values.empty())
    {
        xcb_configure_window(xcb_connection, event->window, event->value_mask, values.data());
        xcb_flush(xcb_connection);
    }
}

void mf::XWaylandWM::handle_configure_notify(xcb_configure_notify_event_t *event)
{
    if (verbose_xwayland_logging_enabled())
    {
        log_debug("XCB_CONFIGURE_NOTIFY event: %s", get_window_debug_string(event->event).c_str());
        log_debug("                     window: %s", get_window_debug_string(event->window).c_str());
        log_debug("                     above_sibling: %s", get_window_debug_string(event->above_sibling).c_str());
        log_debug("                     position: %d, %d", event->x, event->y);
        log_debug("                     size: %dx%d", event->width, event->height);
        log_debug("                     override_redirect: %s", event->override_redirect ? "yes" : "no");

        if (event->border_width)
            log_warning("border width unsupported (border width %d)", event->border_width);
    }
}

// Cursor
xcb_cursor_t mf::XWaylandWM::xcb_cursor_image_load_cursor(const XcursorImage *img)
{
    xcb_connection_t *c = xcb_connection;
    xcb_screen_iterator_t s = xcb_setup_roots_iterator(xcb_get_setup(c));
    xcb_screen_t *screen = s.data;
    xcb_gcontext_t gc;
    xcb_pixmap_t pix;
    xcb_render_picture_t pic;
    xcb_cursor_t cursor;
    int stride = img->width * 4;

    pix = xcb_generate_id(c);
    xcb_create_pixmap(c, 32, pix, screen->root, img->width, img->height);

    pic = xcb_generate_id(c);
    xcb_render_create_picture(c, pic, pix, xcb_format_rgba.id, 0, 0);

    gc = xcb_generate_id(c);
    xcb_create_gc(c, gc, pix, 0, 0);

    xcb_put_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, pix, gc, img->width, img->height, 0, 0, 0, 32, stride * img->height,
                  (uint8_t *)img->pixels);
    xcb_free_gc(c, gc);

    cursor = xcb_generate_id(c);
    xcb_render_create_cursor(c, cursor, pic, img->xhot, img->yhot);

    xcb_render_free_picture(c, pic);
    xcb_free_pixmap(c, pix);

    return cursor;
}

xcb_cursor_t mf::XWaylandWM::xcb_cursor_images_load_cursor(const XcursorImages *images)
{
    if (images->nimage != 1)
        return -1;

    return xcb_cursor_image_load_cursor(images->images[0]);
}

xcb_cursor_t mf::XWaylandWM::xcb_cursor_library_load_cursor(const char *file)
{
    xcb_cursor_t cursor;
    XcursorImages *images;
    char *v = NULL;
    int size = 0;

    if (!file)
        return 0;

    v = getenv("XCURSOR_SIZE");
    if (v)
        size = atoi(v);

    if (!size)
        size = 32;

    images = XcursorLibraryLoadImages(file, NULL, size);
    if (!images)
        return -1;

    cursor = xcb_cursor_images_load_cursor(images);
    XcursorImagesDestroy(images);

    return cursor;
}

void mf::XWaylandWM::wm_get_resources()
{
    xcb_xfixes_query_version_cookie_t xfixes_cookie;
    xcb_xfixes_query_version_reply_t *xfixes_reply;
    xcb_render_query_pict_formats_reply_t *formats_reply;
    xcb_render_query_pict_formats_cookie_t formats_cookie;
    xcb_render_pictforminfo_t *formats;
    uint32_t i;

    xcb_prefetch_extension_data(xcb_connection, &xcb_xfixes_id);
    xcb_prefetch_extension_data(xcb_connection, &xcb_composite_id);

    formats_cookie = xcb_render_query_pict_formats(xcb_connection);

    xfixes = xcb_get_extension_data(xcb_connection, &xcb_xfixes_id);
    if (!xfixes || !xfixes->present)
        log_warning("xfixes not available");

    xfixes_cookie = xcb_xfixes_query_version(xcb_connection, XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION);
    xfixes_reply = xcb_xfixes_query_version_reply(xcb_connection, xfixes_cookie, NULL);

    if (verbose_xwayland_logging_enabled())
        log_debug("xfixes version: %d.%d", xfixes_reply->major_version, xfixes_reply->minor_version);

    free(xfixes_reply);

    formats_reply = xcb_render_query_pict_formats_reply(xcb_connection, formats_cookie, 0);
    if (formats_reply == NULL)
        return;

    formats = xcb_render_query_pict_formats_formats(formats_reply);
    for (i = 0; i < formats_reply->num_formats; i++)
    {
        if (formats[i].direct.red_mask != 0xff && formats[i].direct.red_shift != 16)
            continue;
        if (formats[i].type == XCB_RENDER_PICT_TYPE_DIRECT && formats[i].depth == 24)
            xcb_format_rgb = formats[i];
        if (formats[i].type == XCB_RENDER_PICT_TYPE_DIRECT && formats[i].depth == 32 &&
            formats[i].direct.alpha_mask == 0xff && formats[i].direct.alpha_shift == 24)
            xcb_format_rgba = formats[i];
    }

    free(formats_reply);
}

auto mf::XWaylandWM::get_reply_debug_string(xcb_get_property_reply_t* reply) -> std::string
{
    if (reply == nullptr)
    {
        return "(null reply)";
    }
    else if (auto const text = get_reply_string(reply))
    {
        return "\"" + text.value() + "\"";
    }
    else if (reply->type == XCB_ATOM_ATOM)
    {
        xcb_atom_t const* atoms_ptr = (xcb_atom_t *)xcb_get_property_value(reply);
        std::vector<xcb_atom_t> atoms{atoms_ptr, atoms_ptr + reply->value_len};

        std::stringstream ss{"atoms: ["};
        bool first = true;
        for (auto const& atom : atoms)
        {
            if (!first)
                ss << ", ";
            first = false;
            ss << get_atom_name(atom);
        }
        ss << "]";

        return ss.str();
    }
    else
    {
        size_t const len = reply->value_len;
        std::stringstream ss;
        ss << reply->format << "bit " << get_atom_name(reply->type) << "[" << len << "]";
        if ((reply->type == XCB_ATOM_CARDINAL || reply->type == XCB_ATOM_INTEGER) && len < 32)
        {
            ss << ": ";
            void* const ptr = xcb_get_property_value(reply);
            switch (reply->type)
            {
            case XCB_ATOM_CARDINAL: // unsigned number
                switch (reply->format)
                {
                case 8: ss << data_buffer_to_debug_string((uint8_t*)ptr, len); break;
                case 16: ss << data_buffer_to_debug_string((uint16_t*)ptr, len); break;
                case 32: ss << data_buffer_to_debug_string((uint32_t*)ptr, len); break;
                }
                break;

            case XCB_ATOM_INTEGER: // signed number
                switch (reply->format)
                {
                case 8: ss << data_buffer_to_debug_string((int8_t*)ptr, len); break;
                case 16: ss << data_buffer_to_debug_string((int16_t*)ptr, len); break;
                case 32: ss << data_buffer_to_debug_string((int32_t*)ptr, len); break;
                }
                break;
            }
        }
        return ss.str();
    }
}

auto mf::XWaylandWM::get_window_debug_string(xcb_window_t window) -> std::string
{
    if (!window)
        return "null window";
    else if (window == xcb_screen->root)
        return "root window";
    else if (is_ours(window))
        return "our window " + std::to_string(window);
    else
        return "window " + std::to_string(window);
}

auto mf::XWaylandWM::get_reply_string(xcb_get_property_reply_t* reply) -> std::experimental::optional<std::string>
{
    if (reply->type == xcb_atom.string || reply->type == xcb_atom.utf8_string)
        return std::string{(const char *)xcb_get_property_value(reply), reply->value_len};
    else
        return std::experimental::nullopt;

}

auto mf::XWaylandWM::get_atom_name(xcb_atom_t atom) -> std::string
{
    // TODO: cache, for cheaper lookup or eliminate usage altogether

    if (atom == XCB_ATOM_NONE)
        return "None";

    xcb_get_atom_name_cookie_t const cookie = xcb_get_atom_name(xcb_connection, atom);
    xcb_get_atom_name_reply_t* const reply = xcb_get_atom_name_reply(xcb_connection, cookie, nullptr);

    std::string name;

    if (reply)
    {
        auto const name_data = xcb_get_atom_name_name(reply);
        auto const name_len = xcb_get_atom_name_name_length(reply);
        name = std::string{name_data, name_data + name_len};
    }
    else
    {
        name = "Atom " + std::to_string(atom);
    }

    free(reply);

    return name;
}

void mf::XWaylandWM::setup_visual_and_colormap()
{
    xcb_depth_iterator_t depthIterator = xcb_screen_allowed_depths_iterator(xcb_screen);
    xcb_visualtype_t *visualType = nullptr;
    xcb_visualtype_iterator_t visualTypeIterator;
    while (depthIterator.rem > 0)
    {
        if (depthIterator.data->depth == 32)
        {
            visualTypeIterator = xcb_depth_visuals_iterator(depthIterator.data);
            visualType = visualTypeIterator.data;
            break;
        }

        xcb_depth_next(&depthIterator);
    }

    if (!visualType)
    {
        mir::log_warning("No 32-bit visualtype");
        return;
    }

    xcb_visual_id = visualType->visual_id;
    xcb_colormap = xcb_generate_id(xcb_connection);
    xcb_create_colormap(xcb_connection, XCB_COLORMAP_ALLOC_NONE, xcb_colormap, xcb_screen->root, xcb_visual_id);
}

bool mf::XWaylandWM::is_ours(uint32_t id)
{
    auto setup = xcb_get_setup(xcb_connection);
    return (id & ~setup->resource_id_mask) == setup->resource_id_base;
}
