/*
 * Copyright © 2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
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
 * Authored by: Thomas Guest <thomas.guest@canonical.com>
 */

#ifndef MIR_DISPLAY_SERVER_TEST_FIXTURE
#define MIR_DISPLAY_SERVER_TEST_FIXTURE

#include "mir/process/process.h"

#include "testing_process_manager.h"
#include <gtest/gtest.h>

#include <memory>

namespace mir
{

struct TestingClientOptions
{
    // Code to run in client process
    virtual void operator()() = 0;
};

// The test fixture sets up and tears down a display server for use
// in display server tests.
class DefaultDisplayServerTestFixture : public testing::Test
{
public:
    DefaultDisplayServerTestFixture();
    ~DefaultDisplayServerTestFixture();

    static void SetUpTestCase();
    static void TearDownTestCase();

    void launch_client_process(TestingClientOptions& functor);

private:
    static TestingProcessManager process_manager;

    virtual void TearDown();
    static std::shared_ptr<mir::graphics::Renderer> make_renderer();
    static std::shared_ptr<mir::compositor::BufferAllocationStrategy> make_buffer_allocation_strategy();
};

// The test fixture sets up and tears down a display server for use
// in display server tests.
class BespokeDisplayServerTestFixture : public testing::Test
{
public:
    BespokeDisplayServerTestFixture();
    ~BespokeDisplayServerTestFixture();

    void launch_server_process(TestingServerOptions& functor);

    void launch_client_process(TestingClientOptions& functor);

private:
    TestingProcessManager process_manager;

    virtual void SetUp();
    virtual void TearDown();
};
}

using mir::DefaultDisplayServerTestFixture;
using mir::BespokeDisplayServerTestFixture;
using mir::TestingClientOptions;
using mir::TestingServerOptions;

#endif // MIR_DISPLAY_SERVER_TEST_FIXTURE
