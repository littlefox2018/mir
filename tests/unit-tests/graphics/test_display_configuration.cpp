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
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#include "mir/graphics/display_configuration.h"

#include <gtest/gtest.h>

namespace mg = mir::graphics;
namespace geom = mir::geometry;

namespace
{

mg::DisplayConfigurationOutput const tmpl_output
{
    mg::DisplayConfigurationOutputId{3},
    mg::DisplayConfigurationCardId{2},
    mg::DisplayConfigurationOutputType::dvid,
    {
        {geom::PixelFormat::abgr_8888}
    },
    {
        {geom::Size{10, 20}, 60.0},
        {geom::Size{10, 20}, 59.0},
        {geom::Size{15, 20}, 59.0}
    },
    0,
    geom::Size{10, 20},
    true,
    true,
    geom::Point(),
    2,
    0
};

}

TEST(DisplayConfiguration, card_equality)
{
    mg::DisplayConfigurationCardId const id{1};
    size_t const max_outputs{3};

    mg::DisplayConfigurationCard const card1{id, max_outputs};
    mg::DisplayConfigurationCard const card2 = card1;

    EXPECT_EQ(card1, card1);
    EXPECT_EQ(card1, card2);
    EXPECT_EQ(card2, card1);
}

TEST(DisplayConfiguration, card_inequality)
{
    mg::DisplayConfigurationCardId const id1{1};
    mg::DisplayConfigurationCardId const id2{2};
    size_t const max_outputs1{3};
    size_t const max_outputs2{4};

    mg::DisplayConfigurationCard const card1{id1, max_outputs1};
    mg::DisplayConfigurationCard const card2{id1, max_outputs2};
    mg::DisplayConfigurationCard const card3{id2, max_outputs1};

    EXPECT_NE(card1, card2);
    EXPECT_NE(card2, card1);
    EXPECT_NE(card2, card3);
    EXPECT_NE(card3, card2);
    EXPECT_NE(card1, card3);
    EXPECT_NE(card3, card1);
}

TEST(DisplayConfiguration, mode_equality)
{
    geom::Size const size{10, 20};
    double const vrefresh{59.9};

    mg::DisplayConfigurationMode const mode1{size, vrefresh};
    mg::DisplayConfigurationMode const mode2 = mode1;

    EXPECT_EQ(mode1, mode1);
    EXPECT_EQ(mode1, mode2);
    EXPECT_EQ(mode2, mode1);
}

TEST(DisplayConfiguration, mode_inequality)
{
    geom::Size const size1{10, 20};
    geom::Size const size2{10, 21};
    double const vrefresh1{59.9};
    double const vrefresh2{60.0};

    mg::DisplayConfigurationMode const mode1{size1, vrefresh1};
    mg::DisplayConfigurationMode const mode2{size1, vrefresh2};
    mg::DisplayConfigurationMode const mode3{size2, vrefresh1};

    EXPECT_NE(mode1, mode2);
    EXPECT_NE(mode2, mode1);
    EXPECT_NE(mode2, mode3);
    EXPECT_NE(mode3, mode2);
    EXPECT_NE(mode1, mode3);
    EXPECT_NE(mode3, mode1);
}

TEST(DisplayConfiguration, output_equality)
{
    mg::DisplayConfigurationOutput const output1 = tmpl_output;
    mg::DisplayConfigurationOutput output2 = tmpl_output;

    EXPECT_EQ(output1, output1);
    EXPECT_EQ(output1, output2);
    EXPECT_EQ(output2, output1);
}

TEST(DisplayConfiguration, output_inequality_id)
{
    mg::DisplayConfigurationOutput const output1 = tmpl_output;
    mg::DisplayConfigurationOutput output2 = tmpl_output;
    mg::DisplayConfigurationOutput output3 = tmpl_output;

    output2.id = mg::DisplayConfigurationOutputId{15};
    output3.card_id = mg::DisplayConfigurationCardId{12};

    EXPECT_NE(output1, output2);
    EXPECT_NE(output2, output1);
    EXPECT_NE(output2, output3);
    EXPECT_NE(output3, output2);
    EXPECT_NE(output1, output3);
    EXPECT_NE(output3, output1);
}

TEST(DisplayConfiguration, output_inequality_modes)
{
    mg::DisplayConfigurationOutput const output1 = tmpl_output;
    mg::DisplayConfigurationOutput output2 = tmpl_output;
    mg::DisplayConfigurationOutput output3 = tmpl_output;

    std::vector<mg::DisplayConfigurationMode> const modes2
    {
        {geom::Size{10, 20}, 60.0},
        {geom::Size{10, 20}, 59.9},
        {geom::Size{15, 20}, 59.0}
    };
    std::vector<mg::DisplayConfigurationMode> const modes3
    {
        {geom::Size{10, 20}, 60.0},
        {geom::Size{10, 20}, 59.0}
    };

    output2.modes = modes2;
    output3.modes = modes3;

    EXPECT_NE(output1, output2);
    EXPECT_NE(output2, output1);
    EXPECT_NE(output2, output3);
    EXPECT_NE(output3, output2);
    EXPECT_NE(output1, output3);
    EXPECT_NE(output3, output1);
}

TEST(DisplayConfiguration, output_inequality_physical_size)
{
    mg::DisplayConfigurationOutput const output1 = tmpl_output;
    mg::DisplayConfigurationOutput output2 = tmpl_output;

    geom::Size const physical_size2{11, 20};

    output2.physical_size_mm = physical_size2;

    EXPECT_NE(output1, output2);
    EXPECT_NE(output2, output1);
}

TEST(DisplayConfiguration, output_inequality_connection)
{
    mg::DisplayConfigurationOutput const output1 = tmpl_output;
    mg::DisplayConfigurationOutput output2 = tmpl_output;

    output2.connected = false;

    EXPECT_NE(output1, output2);
    EXPECT_NE(output2, output1);
}

TEST(DisplayConfiguration, output_inequality_current_mode)
{
    mg::DisplayConfigurationOutput const output1 = tmpl_output;
    mg::DisplayConfigurationOutput output2 = tmpl_output;

    output2.current_mode_index = 0;

    EXPECT_NE(output1, output2);
    EXPECT_NE(output2, output1);
}

TEST(DisplayConfiguration, output_inequality_preferred_mode)
{
    mg::DisplayConfigurationOutput const output1 = tmpl_output;
    mg::DisplayConfigurationOutput output2 = tmpl_output;

    output2.preferred_mode_index = 1;

    EXPECT_NE(output1, output2);
    EXPECT_NE(output2, output1);
}
