#include "pipeforge/diagnostics.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include <gtest/gtest.h>

#include "test_files.hpp"

namespace {
using pipeforge::test_support::diagnostics_test;
using pipeforge::test_support::read_file;

TEST_F(diagnostics_test, CreatesAndFlushesTextLog)
{
    const auto path = file("application.log");
    pipeforge::diagnostic_session session("unit", {.log_file = path});
    session.log(pipeforge::log_level::info, "startup");
    EXPECT_TRUE(session.finish());
    EXPECT_NE(read_file(path).find("startup"), std::string::npos);
    EXPECT_FALSE(session.failed());
}

TEST_F(diagnostics_test, InvalidOutputPathIsReported)
{
    EXPECT_THROW((pipeforge::diagnostic_session("unit", {.log_file = file(".")})),
                 pipeforge::diagnostic_io_error);
}

TEST_F(diagnostics_test, ProfilesNestedScopes)
{
    if (!pipeforge::profiling_enabled) {
        GTEST_SKIP() << "Profiling compiled out";
    }

    const auto path = file("nested.json");
    pipeforge::diagnostic_session session("unit", {.trace_file = path});
    {
        PIPEFORGE_PROFILE_SCOPE(session, "outer");
        {
            PIPEFORGE_PROFILE_SCOPE(session, "inner");
        }
    }

    EXPECT_TRUE(session.finish());
    const auto trace = read_file(path);
    EXPECT_NE(trace.find("outer"), std::string::npos);
    EXPECT_NE(trace.find("inner"), std::string::npos);
    EXPECT_NE(trace.find("\"ph\":\"X\""), std::string::npos);
}
} // namespace
