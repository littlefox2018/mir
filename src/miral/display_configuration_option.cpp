/*
 * Copyright © 2014, 2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored By: Alan Griffiths <alan@octopull.co.uk>
 */

#include "miral/display_configuration_option.h"

#include <mir/graphics/default_display_configuration_policy.h>
#include <mir/graphics/display_configuration.h>
#include <mir/options/option.h>
#include <mir/server.h>
#include <mir_toolkit/mir_client_library.h>
#include <iostream>
#include <map>
#include <mir/abnormal_exit.h>
#include <mir/geometry/displacement.h>

namespace mg = mir::graphics;
using namespace mir::geometry;

namespace
{
char const* const display_config_opt = "display-config";
char const* const display_config_descr = "Display configuration [{clone,sidebyside,single,static=<filename>}]";

//char const* const clone_opt_val = "clone";
char const* const sidebyside_opt_val = "sidebyside";
char const* const single_opt_val = "single";
char const* const static_opt_val = "static=";

char const* const display_alpha_opt = "translucent";
char const* const display_alpha_descr = "Select a display mode with alpha[{on,off}]";

char const* const display_alpha_off = "off";
char const* const display_alpha_on = "on";

class PixelFormatSelector : public mg::DisplayConfigurationPolicy
{
public:
    PixelFormatSelector(std::shared_ptr<mg::DisplayConfigurationPolicy> const& base_policy, bool with_alpha);
    virtual void apply_to(mg::DisplayConfiguration& conf);
private:
    std::shared_ptr<mg::DisplayConfigurationPolicy> const base_policy;
    bool const with_alpha;
};

bool contains_alpha(MirPixelFormat format)
{
    return (format == mir_pixel_format_abgr_8888 ||
            format == mir_pixel_format_argb_8888);
}
}

PixelFormatSelector::PixelFormatSelector(
    std::shared_ptr<mg::DisplayConfigurationPolicy> const& base_policy,
    bool with_alpha) :
    base_policy{base_policy},
    with_alpha{with_alpha}
{}

void PixelFormatSelector::apply_to(mg::DisplayConfiguration& conf)
{
    base_policy->apply_to(conf);

    std::map<std::string, int> counts;

    conf.for_each_output(
        [&](mg::UserDisplayConfigurationOutput& conf_output)
        {
//
//            std::cout
//                << "/----------------------------------------------------------------------------\\\n";
//            std::cout << "{\n\tid: " << conf_output.id << "\n\tcard_id: " << conf_output.card_id
//                << "\n\ttype: " << mir_output_type_name(static_cast<MirOutputType>(conf_output.type))
//                << "\n\tmodes: [ ";
//
//            for (size_t i = 0; i < conf_output.modes.size(); ++i)
//            {
//                std::cout << conf_output.modes[i];
//                if (i != conf_output.modes.size() - 1)
//                    std::cout << ", ";
//            }
//
//            std::cout << "]" << std::endl;
//            std::cout << "\tpreferred_mode: " << conf_output.preferred_mode_index << std::endl;
//            std::cout << "\tphysical_size_mm: " << conf_output.physical_size_mm.width << "x" << conf_output.physical_size_mm.height << std::endl;
//            std::cout << "\tconnected: " << (conf_output.connected ? "true" : "false") << std::endl;
//            std::cout << "\tused: " << (conf_output.used ? "true" : "false") << std::endl;
//            std::cout << "\ttop_left: " << conf_output.top_left << std::endl;
//            std::cout << "\tcurrent_mode: " << conf_output.current_mode_index << " (";
//            if (conf_output.current_mode_index < conf_output.modes.size())
//                std::cout << conf_output.modes[conf_output.current_mode_index];
//            else
//                std::cout << "none";
//            std::cout << ")" << std::endl;
//
//            std::cout << "\tscale: " << conf_output.scale << std::endl;
//            std::cout << "\tform factor: ";
//            switch (conf_output.form_factor)
//            {
//            case mir_form_factor_monitor:
//                std::cout << "monitor";
//                break;
//            case mir_form_factor_projector:
//                std::cout << "projector";
//                break;
//            case mir_form_factor_tv:
//                std::cout << "TV";
//                break;
//            case mir_form_factor_phone:
//                std::cout << "phone";
//                break;
//            case mir_form_factor_tablet:
//                std::cout << "tablet";
//                break;
//            default:
//                std::cout << "UNKNOWN";
//            }
//            std::cout <<  std::endl;
//
//            std::cout << "\tcustom logical size: ";
//            if (conf_output.custom_logical_size.is_set())
//            {
//                auto const& size = conf_output.custom_logical_size.value();
//                std::cout << size.width.as_int() << "x" << size.height.as_int();
//            }
//            else
//            {
//                std::cout << "not set";
//            }
//            std::cout << std::endl;
//
//            std::cout << "\torientation: " << conf_output.orientation << '\n';
//            std::cout << "}" << std::endl;
//            std::cout << "\\----------------------------------------------------------------------------/\n";

            auto const type = mir_output_type_name(static_cast<MirOutputType>(conf_output.type));

            std::cout << "Output:\tcard_id: " << conf_output.card_id << "\tid: " << conf_output.id
                      << "\ttype: " << type << "\tcount: " << ++counts[type] << '\n';

            if (!conf_output.connected || !conf_output.used) return;

            auto const& pos = find_if(conf_output.pixel_formats.begin(),
                                      conf_output.pixel_formats.end(),
                                      [&](MirPixelFormat format) -> bool
                                          {
                                              return contains_alpha(format) == with_alpha;
                                          }
                                     );

            // keep the default settings if nothing was found
            if (pos == conf_output.pixel_formats.end())
                return;

            conf_output.current_format = *pos;
        });
}

namespace
{
class StaticDisplayConfigurationPolicy : public mg::DisplayConfigurationPolicy
{
public:
    StaticDisplayConfigurationPolicy(std::string const& filename);
    virtual void apply_to(mg::DisplayConfiguration& conf);
private:

    using Id = std::tuple<mg::DisplayConfigurationCardId, mg::DisplayConfigurationOutputId>;
    struct Config { mir::optional_value<Point> position; mir::optional_value<Size> size; };

    std::map<Id, Config> config;
};

size_t select_mode_index(size_t mode_index, std::vector<mg::DisplayConfigurationMode> const & modes)
{
    if (modes.empty())
        return std::numeric_limits<size_t>::max();

    if (mode_index >= modes.size())
        return 0;

    return mode_index;
}
}

StaticDisplayConfigurationPolicy::StaticDisplayConfigurationPolicy(std::string const& filename)
{
    puts(filename.c_str());

    // Just set up some dummy data
    config[Id{0, 1}].position = Point{0, 0};
    config[Id{0, 1}].size = Size{1024, 768};
    config[Id{0, 2}].position = Point{1024, 0};
    config[Id{0, 2}].size = Size{1024, 768};
}

void StaticDisplayConfigurationPolicy::apply_to(mg::DisplayConfiguration& conf)
{
    conf.for_each_output([&](mg::UserDisplayConfigurationOutput& conf_output)
        {
            if (conf_output.connected && conf_output.modes.size() > 0)
            {
                conf_output.used = true;
                conf_output.power_mode = mir_power_mode_on;
                conf_output.orientation = mir_orientation_normal;

                auto& conf = config[Id{conf_output.card_id, conf_output.id}];

                if (conf.position.is_set())
                {
                    conf_output.top_left = conf.position.value();
                }
                else
                {
                    conf_output.top_left = Point{0, 0};
                }

                size_t preferred_mode_index{select_mode_index(conf_output.preferred_mode_index, conf_output.modes)};
                conf_output.current_mode_index = preferred_mode_index;

                if (conf.size.is_set())
                {
                    for (auto mode = begin(conf_output.modes); mode != end(conf_output.modes); ++mode)
                    {
                        if (mode->size == conf.size.value())
                        {
                            conf_output.current_mode_index = distance(begin(conf_output.modes), mode);
                        }
                    }
                }
            }
            else
            {
                conf_output.used = false;
                conf_output.power_mode = mir_power_mode_off;
            }
        });
}

void miral::display_configuration_options(mir::Server& server)
{
    // Add choice of monitor configuration
    server.add_configuration_option(display_config_opt, display_config_descr,   sidebyside_opt_val);
    server.add_configuration_option(display_alpha_opt,  display_alpha_descr,    display_alpha_off);

    server.wrap_display_configuration_policy(
        [&](std::shared_ptr<mg::DisplayConfigurationPolicy> const& wrapped)
        -> std::shared_ptr<mg::DisplayConfigurationPolicy>
        {
            auto const options = server.get_options();
            auto display_layout = options->get<std::string>(display_config_opt);
            auto with_alpha = options->get<std::string>(display_alpha_opt) == display_alpha_on;

            auto layout_selector = wrapped;

            if (display_layout == sidebyside_opt_val)
                layout_selector = std::make_shared<mg::SideBySideDisplayConfigurationPolicy>();
            else if (display_layout == single_opt_val)
                layout_selector = std::make_shared<mg::SingleDisplayConfigurationPolicy>();
            else if (display_layout.compare(0, strlen(static_opt_val), static_opt_val) == 0)
                layout_selector = std::make_shared<StaticDisplayConfigurationPolicy>(display_layout.substr(strlen(static_opt_val)));

            // Whatever the layout select a pixel format with requested alpha
            return std::make_shared<PixelFormatSelector>(layout_selector, with_alpha);
        });
}
