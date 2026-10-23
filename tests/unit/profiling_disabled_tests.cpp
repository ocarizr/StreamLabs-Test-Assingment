#include "pipeforge/diagnostics.hpp"

#include <gtest/gtest.h>

TEST(ProfilingDisabled, MacroArgumentsAreNotEvaluated)
{
    int side_effects = 0;
    PIPEFORGE_PROFILE_SCOPE(++side_effects, ++side_effects);
    PIPEFORGE_PROFILE_FUNCTION(++side_effects);
    EXPECT_EQ(side_effects, 0);
}
