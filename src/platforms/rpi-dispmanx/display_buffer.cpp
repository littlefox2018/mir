/*
 * Copyright © 2019 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Christopher James Halse Rogers <christopher.halse.rogers@canonical.com>
 */

#include "display_buffer.h"
#include "buffer_allocator.h"

#include "mir/graphics/egl_error.h"
#include "mir/renderer/sw/pixel_source.h"

#include <boost/throw_exception.hpp>
#include <algorithm>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/matrix_decompose.hpp>
#include <mir/fatal.h>

namespace mg = mir::graphics;

namespace
{
typedef struct {
    DISPMANX_ELEMENT_HANDLE_T element;
    int width;   /* This is necessary because dispmanx elements are not queriable. */
    int height;
} EGL_DISPMANX_WINDOW_T;

auto create_fullscreen_dispmanx_element(
    DISPMANX_DISPLAY_HANDLE_T display,
    mir::geometry::Size const& size) -> DISPMANX_ELEMENT_HANDLE_T
{
    VC_RECT_T source_rect, dest_rect;

    dest_rect.x = 0;
    dest_rect.y = 0;
    dest_rect.width = size.width.as_uint32_t();
    dest_rect.height = size.height.as_uint32_t();

    source_rect.x = 0;
    source_rect.y = 0;
    source_rect.width = size.width.as_uint32_t() << 16;
    source_rect.height = size.height.as_uint32_t() << 16;

    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);

    VC_DISPMANX_ALPHA_T alpha {
        DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS,
        255,
        0
    };

    DISPMANX_ELEMENT_HANDLE_T display_element =
        vc_dispmanx_element_add(
            update,
            display,
            0,
            &dest_rect,
            0,
            &source_rect,
            DISPMANX_PROTECTION_NONE,
            &alpha,
            nullptr,
            DISPMANX_NO_ROTATE);

    vc_dispmanx_update_submit_sync(update);

    return display_element;
}

auto surface_for_element(
    DISPMANX_ELEMENT_HANDLE_T display_element,
    mir::geometry::Size const& size,
    EGLDisplay dpy,
    EGLConfig config) -> EGLSurface
{
    static EGL_DISPMANX_WINDOW_T native_window {
        display_element,
        size.width.as_int(),
        size.height.as_int()
    };

    auto const surface = eglCreateWindowSurface(
        dpy,
        config,
        reinterpret_cast<EGLNativeWindowType>(&native_window), // Who typedef'd EGLNativeWindowType as unsigned long?!
        NULL);

    if (surface == EGL_NO_SURFACE)
    {
        BOOST_THROW_EXCEPTION((mg::egl_error("Failed to create EGL surface for display")));
    }

    return surface;
}
}

mg::rpi::DisplayBuffer::DisplayBuffer(
    geometry::Size const& size,
    DISPMANX_DISPLAY_HANDLE_T display,
    EGLDisplay dpy,
    EGLConfig config,
    EGLContext ctx)
    : view{{0, 0}, size},
      dpy{dpy},
      ctx{ctx},
      config{config},
      display_handle{display},
      egl_target_element{DISPMANX_NO_HANDLE},
      surface{EGL_NO_SURFACE}
{
}

void mg::rpi::DisplayBuffer::for_each_display_buffer(std::function<void(mg::DisplayBuffer&)> const& f)
{
    f(*this);
}

void mg::rpi::DisplayBuffer::post()
{
}

std::chrono::milliseconds mg::rpi::DisplayBuffer::recommended_sleep() const
{
    return std::chrono::milliseconds();
}
mir::geometry::Rectangle mg::rpi::DisplayBuffer::view_area() const
{
    return view;
}

namespace
{
bool is_dispmanx_capable_buffer(mg::Buffer const& buffer)
{
    return
        dynamic_cast<mg::rpi::DispmanXBuffer const*>(&buffer) ||
        dynamic_cast<mir::renderer::software::PixelSource const*>(&buffer);
}

bool transform_is_representable(glm::mat4 const& transform)
{
    // TODO: We can handle arbitrary scaling, 90° rotations, and mirroring
    // For now, just bail on *any* transformation.
    return transform == glm::mat4{1};
}

bool renderable_is_overlay_candidate(std::shared_ptr<mg::Renderable> const& renderable)
{
    return
        transform_is_representable(renderable->transformation()) &&
        is_dispmanx_capable_buffer(*renderable->buffer());
}

auto vc_image_type_from_mir_pf(MirPixelFormat format) -> VC_IMAGE_TYPE_T
{
    switch(format)
    {
    case mir_pixel_format_xbgr_8888:
        return VC_IMAGE_XRGB8888;
    default:
        BOOST_THROW_EXCEPTION((std::runtime_error{"Unexpected pixel format"}));
    }
}

auto dispmanx_handle_for_renderable(mg::Renderable const& renderable)
    -> DISPMANX_RESOURCE_HANDLE_T
{
    auto const buffer = renderable.buffer().get();
    if (auto dispmanx_buffer = dynamic_cast<mg::rpi::DispmanXBuffer const*>(buffer))
    {
        return static_cast<DISPMANX_RESOURCE_HANDLE_T>(*dispmanx_buffer);
    }
    else if (auto const pixel_source = dynamic_cast<mir::renderer::software::PixelSource*>(buffer))
    {
        uint32_t dummy;
        auto const vc_format = vc_image_type_from_mir_pf(buffer->pixel_format());
        auto const width = buffer->size().width.as_uint32_t();
        auto const height = buffer->size().height.as_uint32_t();
        auto handle = vc_dispmanx_resource_create(vc_format, width, height, &dummy);

        pixel_source->read(
            [handle, vc_format, stride = pixel_source->stride().as_uint32_t(), width, height]
            (unsigned char const* data)
            {
                VC_RECT_T rect;
                vc_dispmanx_rect_set(&rect, 0, 0, width, height);

                vc_dispmanx_resource_write_data(
                    handle,
                    vc_format,
                    stride,
                    const_cast<unsigned char*>(data),
                    &rect);
            });

        return handle;
    }
    else
    {
        BOOST_THROW_EXCEPTION((
            std::runtime_error{"We accidentally tried to use overlays without checking the buffers are overlay-capable"})q);
    }
}
}

bool mg::rpi::DisplayBuffer::overlay(mg::RenderableList const& renderlist)
{
    if (!std::all_of(
        renderlist.begin(),
        renderlist.end(),
        &renderable_is_overlay_candidate))
    {
        return false;
    }

    auto update_handle = vc_dispmanx_update_start(0);

    // TODO: Better algorithm than “remove everything from last frame, add everything from this”

    // First, remove everything
    for (auto const& element : current_elements)
    {
        vc_dispmanx_element_remove(update_handle, element);
    }
    current_elements.clear();

    // Now, add the renderables
    for (uint32_t layer = 0; layer < renderlist.size(); ++layer)
    {
        auto const renderable = renderlist[layer];
        VC_RECT_T dest_rect, src_rect;

        // Destination rect coördinates are in integer pixels…
        vc_dispmanx_rect_set(
            &dest_rect,
            renderable->screen_position().top_left.x.as_uint32_t(),
            renderable->screen_position().top_left.y.as_uint32_t(),
            renderable->screen_position().size.width.as_uint32_t(),
            renderable->screen_position().size.height.as_uint32_t());

        // …but source rect coödinates are in 16.16 fixed point.
        vc_dispmanx_rect_set(
            &src_rect,
            0, 0,
            renderable->buffer()->size().width.as_uint32_t() << 16,
            renderable->buffer()->size().height.as_uint32_t() << 16);

        VC_DISPMANX_ALPHA_T alpha_flags = {
            static_cast<DISPMANX_FLAGS_ALPHA_T>(DISPMANX_FLAGS_ALPHA_FROM_SOURCE | DISPMANX_FLAGS_ALPHA_MIX),
            static_cast<uint8_t>(renderable->alpha() * 255),
            0
        };

        current_elements.push_back(
            vc_dispmanx_element_add(
                update_handle,
                display_handle,
                layer,
                &dest_rect,
                dispmanx_handle_for_renderable(*renderlist[layer]),
                &src_rect,
                DISPMANX_PROTECTION_NONE,
                &alpha_flags,
                0,
                DISPMANX_NO_ROTATE));
        if (current_elements.back() == DISPMANX_NO_HANDLE)
        {
            BOOST_THROW_EXCEPTION((std::runtime_error{"Failed to add element to DispmanX display list"}));
        }
    }

    vc_dispmanx_update_submit_sync(update_handle);

    return true;
}
glm::mat2 mg::rpi::DisplayBuffer::transformation() const
{
    return glm::mat2(1);
}
mg::NativeDisplayBuffer* mg::rpi::DisplayBuffer::native_display_buffer()
{
    return this;
}

void mg::rpi::DisplayBuffer::make_current()
{
    if (current_elements.size() != 1 || current_elements.front() != egl_target_element)
    {
        auto update_handle = vc_dispmanx_update_start(0);

        for (auto const& element : current_elements)
        {
            vc_dispmanx_element_remove(update_handle, element);
        }
        current_elements.clear();

        egl_target_element = create_fullscreen_dispmanx_element(display_handle, view.size);
        current_elements.push_back(egl_target_element);
        surface = surface_for_element(egl_target_element, view.size, dpy, config);
    }

    if (eglMakeCurrent(dpy, surface, surface, ctx) != EGL_TRUE)
    {
        BOOST_THROW_EXCEPTION(mg::egl_error("Failed to make context current"));
    }
}

void mg::rpi::DisplayBuffer::release_current()
{
    if (eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) != EGL_TRUE)
    {
        BOOST_THROW_EXCEPTION(mg::egl_error("Failed to release context"));
    }
}

void mg::rpi::DisplayBuffer::swap_buffers()
{
    if (eglSwapBuffers(dpy, surface) != EGL_TRUE)
    {
        BOOST_THROW_EXCEPTION((mg::egl_error("Failed to swap buffers")));
    }
}

void mg::rpi::DisplayBuffer::bind()
{
}
