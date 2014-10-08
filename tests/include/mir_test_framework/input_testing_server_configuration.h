/*
 * Copyright © 2013 Canonical Ltd.
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
 * Authored by: Robert Carr <robert.carr@canonical.com>
 */


#ifndef MIR_TEST_INPUT_TESTING_SERVER_CONFIGURATION_H_
#define MIR_TEST_INPUT_TESTING_SERVER_CONFIGURATION_H_

#include "mir_test_framework/testing_server_configuration.h"

#include "mir/geometry/rectangle.h"

#include <map>
#include <string>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>

namespace mir
{
namespace input
{
namespace android
{
class FakeEventHub;
}
}
namespace test
{
namespace doubles
{
class FakeEventHubInputConfiguration;
}
}
}

namespace mir_test_framework
{

class InputTestingServerConfiguration : public TestingServerConfiguration
{
public:
    InputTestingServerConfiguration();
    explicit InputTestingServerConfiguration(std::vector<geometry::Rectangle> const& display_rects);

    void on_start();
    void on_exit();

    std::shared_ptr<droidinput::EventHubInterface> the_event_hub() override;
    std::shared_ptr<mir::input::InputDispatcher> the_input_dispatcher() override;
    std::shared_ptr<mir::shell::InputTargeter> the_input_targeter() override;
    std::shared_ptr<mir::input::InputSender> the_input_sender() override;

    std::shared_ptr<mir::input::android::FakeEventHub> fake_event_hub;

protected:
    virtual void inject_input() = 0;

    void wait_until_client_appears(std::string const& surface_name);

private:
    std::thread input_injection_thread;
};

}

#endif /* MIR_TEST_INPUT_TESTING_SERVER_CONFIGURATION_H_ */
