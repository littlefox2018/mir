/*
 * Copyright © 2013 Canonical Ltd.
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
 * Authored by: Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "mir/compositor/native_buffer_handle.h"

#include "src/server/graphics/android/server_render_window.h"

#include "mir_test_doubles/mock_buffer.h"
#include "mir_test_doubles/mock_swapper.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <stdexcept>

namespace mtd=mir::test::doubles;
namespace geom=mir::geometry;
namespace mga=mir::graphics::android;
namespace mc=mir::compositor;

namespace
{

#if 0
struct MockFBHardware : public FBHardwarePoster 
{
    MOCK_METHOD0(composition_complete());
    MOCK_METHOD1(post_buffer, void(std::shared_ptr<mc::Buffer>));
};
#endif

struct ServerRenderWindowTest : public ::testing::Test
{
    virtual void SetUp()
    {
        mock_buffer1 = std::make_shared<mtd::MockBuffer>(geom::Size{geom::Width{3}, geom::Height{3}},
                                                        geom::Stride{34}, geom::PixelFormat::argb_8888);
        mock_buffer2 = std::make_shared<mtd::MockBuffer>(geom::Size{geom::Width{3}, geom::Height{3}},
                                                        geom::Stride{34}, geom::PixelFormat::argb_8888);
        mock_buffer3 = std::make_shared<mtd::MockBuffer>(geom::Size{geom::Width{3}, geom::Height{3}},
                                                        geom::Stride{34}, geom::PixelFormat::argb_8888);
        mock_swapper = std::make_shared<mtd::MockSwapper>(mock_buffer1);
    }

    std::shared_ptr<mtd::MockBuffer> mock_buffer1;
    std::shared_ptr<mtd::MockBuffer> mock_buffer2;
    std::shared_ptr<mtd::MockBuffer> mock_buffer3;
    std::shared_ptr<mtd::MockSwapper> mock_swapper;
};
}

TEST_F(ServerRenderWindowTest, driver_wants_a_buffer)
{
    using namespace testing;
    mga::ServerRenderWindow render_window(mock_swapper);

    auto stub_anw = std::make_shared<mc::NativeBufferHandle>();

    EXPECT_CALL(*mock_swapper, compositor_acquire(_,_))
        .Times(1);
    EXPECT_CALL(*mock_buffer1, native_buffer_handle())
        .Times(1)
        .WillOnce(Return(stub_anw));

    auto rc_buffer = render_window.driver_requests_buffer();

    EXPECT_EQ(stub_anw.get(), rc_buffer);
}

TEST_F(ServerRenderWindowTest, driver_is_done_with_a_buffer_properly)
{
    using namespace testing;
    mga::ServerRenderWindow render_window(mock_swapper);

    auto stub_anw = std::make_shared<mc::NativeBufferHandle>();

    mc::BufferID id{442}, returned_id;

    EXPECT_CALL(*mock_swapper, compositor_acquire(_,_))
        .Times(1)
        .WillOnce(DoAll(SetArgReferee<0>(mock_buffer1),SetArgReferee<1>(id)));
    EXPECT_CALL(*mock_buffer1, native_buffer_handle())
        .Times(1)
        .WillOnce(Return(stub_anw));
    EXPECT_CALL(*mock_buffer1, id())
        .Times(1)
        .WillOnce(Return(id));

    render_window.driver_requests_buffer();
    testing::Mock::VerifyAndClearExpectations(mock_swapper.get());

    EXPECT_CALL(*mock_swapper, compositor_release(_))
        .Times(1)
        .WillOnce(SaveArg<0>(&returned_id));
    render_window.driver_returns_buffer(stub_anw.get());
    testing::Mock::VerifyAndClearExpectations(mock_swapper.get());

    EXPECT_EQ(id, returned_id);
}

/* note: in real usage, sync is enforced by the swapper class. we make use of the mock's non-blocking 
         to do the tests. */
TEST_F(ServerRenderWindowTest, driver_wants_a_few_buffer_)
{
    using namespace testing;
    mc::BufferID id1{4}, id2{5}, id3{6};
    mga::ServerRenderWindow render_window(mock_swapper);

    auto stub_anw1 = std::make_shared<mc::NativeBufferHandle>();
    auto stub_anw2 = std::make_shared<mc::NativeBufferHandle>();
    auto stub_anw3 = std::make_shared<mc::NativeBufferHandle>();

    EXPECT_CALL(*mock_swapper, compositor_acquire(_,_))
        .Times(3)
        .WillOnce(DoAll(SetArgReferee<0>(mock_buffer1),SetArgReferee<1>(id2)))
        .WillOnce(DoAll(SetArgReferee<0>(mock_buffer2),SetArgReferee<1>(id3)))
        .WillOnce(DoAll(SetArgReferee<0>(mock_buffer3),SetArgReferee<1>(id1)));
    EXPECT_CALL(*mock_buffer1, native_buffer_handle())
        .Times(1)
        .WillOnce(Return(stub_anw1));
    EXPECT_CALL(*mock_buffer1, id())
        .Times(1)
        .WillOnce(Return(id1));

    EXPECT_CALL(*mock_buffer2, native_buffer_handle())
        .Times(1)
        .WillOnce(Return(stub_anw2));
    EXPECT_CALL(*mock_buffer2, id())
        .Times(1)
        .WillOnce(Return(id2));

    EXPECT_CALL(*mock_buffer3, native_buffer_handle())
        .Times(1)
        .WillOnce(Return(stub_anw3));
    EXPECT_CALL(*mock_buffer3, id())
        .Times(1)
        .WillOnce(Return(id3));

    auto handle1 = render_window.driver_requests_buffer();
    auto handle2 = render_window.driver_requests_buffer();
    auto handle3 = render_window.driver_requests_buffer();

    testing::InSequence sequence_enforcer;
    EXPECT_CALL(*mock_swapper, compositor_release(id2))
        .Times(1);
    EXPECT_CALL(*mock_swapper, compositor_release(id3))
        .Times(1);
    EXPECT_CALL(*mock_swapper, compositor_release(id1))
        .Times(1);

    render_window.driver_returns_buffer(handle2); 
    render_window.driver_returns_buffer(handle3); 
    render_window.driver_returns_buffer(handle1);
}

TEST_F(ServerRenderWindowTest, throw_if_driver_returns_weird_buffer)
{
    using namespace testing;
    mga::ServerRenderWindow render_window(mock_swapper);

    auto stub_anw = std::make_shared<mc::NativeBufferHandle>();

    EXPECT_CALL(*mock_swapper, compositor_release(_))
        .Times(0);

    EXPECT_THROW({
        render_window.driver_returns_buffer(nullptr);
    }, std::runtime_error); 
}

#if 0
TEST_F(ServerRenderWindowTest, driver_inquires_about_format)
{
    EXPECT_CALL(mock_buffer1_swapper, compositor_acquire)
        .Times(1)
        .WillOnce(Return( buffer));
    EXPECT_CALL(buffer, format())
        .Times(1)
        .WillOnce(Return(test_pf));

    mga::ServerRenderWindow render_window(mock_buffer1_swapper);
    rc_format = render_window.driver_requests_info(format);
    EXPECT_EQ(test_pf, rc_format); 
}
#endif 
