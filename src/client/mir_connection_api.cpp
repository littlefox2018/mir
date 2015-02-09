/*
 * Copyright © 2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
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
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#define MIR_LOG_COMPONENT "MirConnectionAPI"

#include "mir_connection_api.h"
#include "mir_toolkit/mir_connection.h"
#include "mir_toolkit/mir_client_library_drm.h"
#include "mir/default_configuration.h"
#include "mir/raii.h"

#include "mir_connection.h"
//#include "egl_native_display_container.h"
#include "default_connection_configuration.h"
#include "display_configuration.h"
#include "error_connections.h"
#include "mir/uncaught.h"

// Temporary include to ease client transition from mir_connection_drm* APIs.
// to mir_connection_platform_operation().
// TODO: Remove when transition is complete
#include "../platforms/mesa/include/mir_toolkit/mesa/platform_operation.h"

#include <unordered_set>
#include <cstddef>
#include <cstring>

namespace mcl = mir::client;

namespace
{
// assign_result is compatible with all 2-parameter callbacks
void assign_result(void* result, void** context)
{
    if (context)
        *context = result;
}

class DefaultMirConnectionAPI : public mcl::MirConnectionAPI
{
public:
    MirWaitHandle* connect(
        mcl::ConfigurationFactory configuration,
        char const* socket_file,
        char const* name,
        mir_connected_callback callback,
        void* context) override
    {
        try
        {
            std::string sock;
            if (socket_file)
                sock = socket_file;
            else
            {
                auto socket_env = getenv("MIR_SOCKET");
                if (socket_env)
                    sock = socket_env;
                else
                    sock = mir::default_server_socket;
            }

            auto const conf = configuration(sock);

            std::unique_ptr<MirConnection> connection{new MirConnection(*conf)};
            auto const result = connection->connect(name, callback, context);
            connection.release();
            return result;
        }
        catch (std::exception const& x)
        {
            MirConnection* error_connection = new MirConnection(x.what());
            mcl::ErrorConnections::instance().insert(error_connection);
            callback(error_connection, context);
            return nullptr;
        }
    }

    void release(MirConnection* connection) override
    {
        if (!mcl::ErrorConnections::instance().contains(connection))
        {
            try
            {
                auto wait_handle = connection->disconnect();
                wait_handle->wait_for_all();
            }
            catch (std::exception const& ex)
            {
                // We're implementing a C API so no exceptions are to be
                // propagated. And that's OK because if disconnect() fails,
                // we don't care why. We're finished with the connection anyway.

                MIR_LOG_UNCAUGHT_EXCEPTION(ex);
            }
        }
        else
        {
            mcl::ErrorConnections::instance().remove(connection);
        }

        delete connection;
    }

    mcl::ConfigurationFactory configuration_factory() override
    {
        return [](std::string const& socket) {
            return std::unique_ptr<mcl::ConnectionConfiguration>{
                new mcl::DefaultConnectionConfiguration{socket}
            };
        };
    }
};

DefaultMirConnectionAPI default_api;
}

mcl::MirConnectionAPI* mir_connection_api_impl{&default_api};

MirWaitHandle* mir_connect(
    char const* socket_file,
    char const* name,
    mir_connected_callback callback,
    void* context)
{
    try
    {
        return mir_connection_api_impl->connect(mir_connection_api_impl->configuration_factory(),
                                                socket_file,
                                                name,
                                                callback,
                                                context);
    }
    catch (std::exception const& ex)
    {
        MIR_LOG_UNCAUGHT_EXCEPTION(ex);
        return nullptr;
    }
}

MirConnection* mir_connect_sync(
    char const* server,
    char const* app_name)
{
    MirConnection* conn = nullptr;
    mir_wait_for(mir_connect(server, app_name,
                             reinterpret_cast<mir_connected_callback>
                                             (assign_result),
                             &conn));
    return conn;
}

bool mir_connection_is_valid(MirConnection* connection)
{
    return MirConnection::is_valid(connection);
}

char const* mir_connection_get_error_message(MirConnection* connection)
{
    return connection->get_error_message();
}

void mir_connection_release(MirConnection* connection)
{
    try
    {
        return mir_connection_api_impl->release(connection);
    }
    catch (std::exception const& ex)
    {
        MIR_LOG_UNCAUGHT_EXCEPTION(ex);
    }
}

void mir_connection_get_platform(
    MirConnection* connection,
    MirPlatformPackage* platform_package)
{
    connection->populate(*platform_package);
}

void mir_connection_set_lifecycle_event_callback(
    MirConnection* connection,
    mir_lifecycle_event_callback callback,
    void* context)
{
    if (!mcl::ErrorConnections::instance().contains(connection))
        connection->register_lifecycle_event_callback(callback, context);
}

//TODO: DEPRECATED: remove this function
void mir_connection_get_display_info(
    MirConnection* connection,
    MirDisplayInfo* display_info)
{
    auto const config = mir::raii::deleter_for(
        mir_connection_create_display_config(connection),
        &mir_display_config_destroy);

    if (config->num_outputs < 1)
        return;

    MirDisplayOutput* state = nullptr;
    // We can't handle more than one display, so just populate based on the first
    // active display we find.
    for (unsigned int i = 0; i < config->num_outputs; ++i)
    {
        if (config->outputs[i].used && config->outputs[i].connected &&
            config->outputs[i].current_mode < config->outputs[i].num_modes)
        {
            state = &config->outputs[i];
            break;
        }
    }
    // Oh, oh! No connected outputs?!
    if (state == nullptr)
    {
        memset(display_info, 0, sizeof(*display_info));
        return;
    }

    MirDisplayMode mode = state->modes[state->current_mode];

    display_info->width = mode.horizontal_resolution;
    display_info->height = mode.vertical_resolution;

    unsigned int format_items;
    if (state->num_output_formats > mir_supported_pixel_format_max)
         format_items = mir_supported_pixel_format_max;
    else
         format_items = state->num_output_formats;

    display_info->supported_pixel_format_items = format_items;
    for(auto i=0u; i < format_items; i++)
    {
        display_info->supported_pixel_format[i] = state->output_formats[i];
    }
}

MirDisplayConfiguration* mir_connection_create_display_config(
    MirConnection* connection)
{
    if (connection)
        return connection->create_copy_of_display_config();
    return nullptr;
}

void mir_connection_set_display_config_change_callback(
    MirConnection* connection,
    mir_display_config_callback callback,
    void* context)
{
    if (connection)
        connection->register_display_change_callback(callback, context);
}

void mir_display_config_destroy(MirDisplayConfiguration* configuration)
{
    mcl::delete_config_storage(configuration);
}

MirWaitHandle* mir_connection_apply_display_config(
    MirConnection* connection,
    MirDisplayConfiguration* display_configuration)
{
    try
    {
        return connection ? connection->configure_display(display_configuration) : nullptr;
    }
    catch (std::exception const& ex)
    {
        MIR_LOG_UNCAUGHT_EXCEPTION(ex);
        return nullptr;
    }
}

MirEGLNativeDisplayType mir_connection_get_egl_native_display(
    MirConnection* connection)
{
    return connection->egl_native_display();
}

void mir_connection_get_available_surface_formats(
    MirConnection* connection,
    MirPixelFormat* formats,
    unsigned const int format_size,
    unsigned int* num_valid_formats)
{
    if ((connection) && (formats) && (num_valid_formats))
        connection->available_surface_formats(formats, format_size, *num_valid_formats);
}

extern "C"
{
MirWaitHandle* new_mir_connection_platform_operation(
    MirConnection* connection,
    MirPlatformMessage const* request,
    mir_platform_operation_callback callback, void* context);
MirWaitHandle* old_mir_connection_platform_operation(
    MirConnection* connection, int /* opcode */,
    MirPlatformMessage const* request,
    mir_platform_operation_callback callback, void* context);
}

__asm__(".symver new_mir_connection_platform_operation,mir_connection_platform_operation@@MIR_CLIENT_8.3");
MirWaitHandle* new_mir_connection_platform_operation(
    MirConnection* connection,
    MirPlatformMessage const* request,
    mir_platform_operation_callback callback, void* context)
{
    try
    {
        return connection->platform_operation(request, callback, context);
    }
    catch (std::exception const& ex)
    {
        MIR_LOG_UNCAUGHT_EXCEPTION(ex);
        return nullptr;
    }

}

// TODO: Remove when we bump so name
__asm__(".symver old_mir_connection_platform_operation,mir_connection_platform_operation@MIR_CLIENT_8");
MirWaitHandle* old_mir_connection_platform_operation(
    MirConnection* connection, int /* opcode */,
    MirPlatformMessage const* request,
    mir_platform_operation_callback callback, void* context)
{
    return new_mir_connection_platform_operation(connection, request, callback, context);
}

/**************************
 * DRM specific functions *
 **************************/

namespace
{

struct AuthMagicPlatformOperationContext
{
    mir_drm_auth_magic_callback callback;
    void* context;
};

void platform_operation_to_auth_magic_callback(
    MirConnection*, MirPlatformMessage* response, void* context)
{
    auto const response_msg = mir::raii::deleter_for(
        response,
        &mir_platform_message_release);
    auto const auth_magic_context =
        std::unique_ptr<AuthMagicPlatformOperationContext>{
            static_cast<AuthMagicPlatformOperationContext*>(context)};

    auto response_data = mir_platform_message_get_data(response_msg.get());
    auto auth_response = reinterpret_cast<MirMesaAuthMagicResponse const*>(response_data.data);

    auth_magic_context->callback(auth_response->status, auth_magic_context->context);
}

void assign_set_gbm_device_status(
    MirConnection*, MirPlatformMessage* response, void* context)
{
    auto const response_msg = mir::raii::deleter_for(
        response,
        &mir_platform_message_release);

    auto const response_data = mir_platform_message_get_data(response_msg.get());
    auto const set_gbm_device_response_ptr =
        reinterpret_cast<MirMesaSetGBMDeviceResponse const*>(response_data.data);

    auto status_ptr = static_cast<int*>(context);
    *status_ptr = set_gbm_device_response_ptr->status;
}

}

MirWaitHandle* mir_connection_drm_auth_magic(MirConnection* connection,
                                             unsigned int magic,
                                             mir_drm_auth_magic_callback callback,
                                             void* context)
{
    auto const msg = mir::raii::deleter_for(
        mir_platform_message_create(MirMesaPlatformOperation::auth_magic),
        &mir_platform_message_release);

    auto const auth_magic_op_context =
        new AuthMagicPlatformOperationContext{callback, context};

    MirMesaAuthMagicRequest request;
    request.magic = magic;

    mir_platform_message_set_data(msg.get(), &request, sizeof(request));

    return new_mir_connection_platform_operation(
        connection,
        msg.get(),
        platform_operation_to_auth_magic_callback,
        auth_magic_op_context);
}

int mir_connection_drm_set_gbm_device(MirConnection* connection,
                                      struct gbm_device* gbm_dev)
{
    MirMesaSetGBMDeviceRequest const request{gbm_dev};

    auto const msg = mir::raii::deleter_for(
        mir_platform_message_create(MirMesaPlatformOperation::set_gbm_device),
        &mir_platform_message_release);

    mir_platform_message_set_data(msg.get(), &request, sizeof(request));

    static int const success{0};
    int status{-1};

    auto wh = new_mir_connection_platform_operation(
        connection,
        msg.get(),
        assign_set_gbm_device_status,
        &status);

    mir_wait_for(wh);

    return status == success;
}
