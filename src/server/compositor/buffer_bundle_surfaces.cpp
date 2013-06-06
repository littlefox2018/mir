/*
 * Copyright © 2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
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
 * Authored by:
 * Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "mir/compositor/buffer_bundle_surfaces.h"
#include "buffer_swapper_master.h"
#include "mir/compositor/buffer_properties.h"

#include "temporary_buffers.h"

namespace mc = mir::compositor;
namespace geom = mir::geometry;
namespace ms = mir::surfaces;

/* TODO: this class could probably be combined with SwapperSwitcher */
mc::BufferBundleSurfaces::BufferBundleSurfaces(
    std::shared_ptr<BufferSwapperMaster>const& swapper,
    BufferProperties const& buffer_properties)
     : swapper(swapper),
       size(buffer_properties.size),
       pixel_format(buffer_properties.format)
{
}

mc::BufferBundleSurfaces::BufferBundleSurfaces(std::shared_ptr<BufferSwapperMaster> const& swapper)
     : swapper(std::move(swapper)),
       size(),
       pixel_format(geometry::PixelFormat::abgr_8888)
{
}

mc::BufferBundleSurfaces::~BufferBundleSurfaces()
{
}

std::shared_ptr<ms::GraphicRegion> mc::BufferBundleSurfaces::lock_back_buffer()
{
    return std::make_shared<mc::TemporaryCompositorBuffer>(swapper);
}

std::shared_ptr<mc::Buffer> mc::BufferBundleSurfaces::secure_client_buffer()
{
    return std::make_shared<mc::TemporaryClientBuffer>(swapper);
}

geom::PixelFormat mc::BufferBundleSurfaces::get_bundle_pixel_format()
{
    return pixel_format;
}

geom::Size mc::BufferBundleSurfaces::bundle_size()
{
    return size;
}

void mc::BufferBundleSurfaces::force_requests_to_complete()
{
    swapper->force_client_completion();
}
