#include "pipeforge/application.hpp"
#include "pipeforge/version.hpp"

#include <array>
#include <sstream>
#include <vector>

#include <gtest/gtest.h>

namespace {
TEST(Application, ReportsBuildVersion)
{
    std::ostringstream output;
    std::ostringstream errors;
    const std::array arguments{std::string_view{"--version"}};
    EXPECT_EQ(pipeforge::run_application("pipeforge-client", arguments, output, errors), 0);
    EXPECT_EQ(output.str(), "pipeforge-client " + std::string(pipeforge::version) + "\n");
    EXPECT_TRUE(errors.str().empty());
}

TEST(Application, HelpExplainsCurrentFeatureBoundary)
{
    std::ostringstream output;
    std::ostringstream errors;
    const std::array arguments{std::string_view{"--help"}};
    EXPECT_EQ(pipeforge::run_application("pipeforge-server", arguments, output, errors), 0);
    EXPECT_NE(output.str().find("Usage: pipeforge-server"), std::string::npos);
    EXPECT_NE(output.str().find("--serve"), std::string::npos);
    EXPECT_TRUE(errors.str().empty());
}

TEST(Application, NoArgumentsDoesNotPretendToStartIPC)
{
    std::ostringstream output;
    std::ostringstream errors;
    EXPECT_EQ(pipeforge::run_application("pipeforge-server", {}, output, errors), 2);
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(errors.str().find("command is required"), std::string::npos);
}

TEST(Application, RejectsUnknownOption)
{
    std::ostringstream output;
    std::ostringstream errors;
    const std::array arguments{std::string_view{"--connect"}};
    EXPECT_EQ(pipeforge::run_application("pipeforge-client", arguments, output, errors), 2);
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(errors.str().find("Unsupported arguments"), std::string::npos);
}

TEST(Application, RejectsExtraArgumentsEvenAfterHelp)
{
    std::ostringstream output;
    std::ostringstream errors;
    const std::array arguments{std::string_view{"--help"}, std::string_view{"unexpected"}};
    EXPECT_EQ(pipeforge::run_application("pipeforge-client", arguments, output, errors), 2);
    EXPECT_TRUE(output.str().empty());
    EXPECT_FALSE(errors.str().empty());
}

TEST(Application, ValidatesRuntimeOptionsBeforeCallingThePlatformHandler)
{
    std::ostringstream output;
    std::ostringstream errors;
    const std::array arguments{std::string_view{"--demo-mixed"},
                               std::string_view{"--timeout-ms"},
                               std::string_view{"250"},
                               std::string_view{"--endpoint"},
                               std::string_view{"\\\\.\\pipe\\test-m5"}};
    const auto handler = +[](const pipeforge::invocation& options,
                             std::ostream& destination,
                             std::ostream&,
                             pipeforge::diagnostic_session&) {
        EXPECT_EQ(options.command, pipeforge::application_command::demo_mixed);
        EXPECT_EQ(options.timeout, std::chrono::milliseconds{250});
        EXPECT_EQ(options.endpoint, L"\\\\.\\pipe\\test-m5");
        destination << "handled\n";
        return 0;
    };
    EXPECT_EQ(pipeforge::run_application("pipeforge-client", arguments, output, errors, handler),
              0);
    EXPECT_EQ(output.str(), "handled\n");
    EXPECT_TRUE(errors.str().empty());
}

TEST(Application, RejectsWrongRoleDuplicateAndOutOfRangeRuntimeOptions)
{
    for (const auto& arguments :
         {std::vector<std::string_view>{"--serve", "--sessions", "0"},
          std::vector<std::string_view>{"--serve", "--timeout-ms", "1"},
          std::vector<std::string_view>{"--serve", "--timeout-ms", "100", "--timeout-ms", "100"},
          std::vector<std::string_view>{"--serve", "--endpoint", "\\\\remote\\pipe\\test"},
          std::vector<std::string_view>{"--demo-sync"},
          std::vector<std::string_view>{"--help", "--sessions", "2"}}) {
        std::ostringstream output;
        std::ostringstream errors;
        EXPECT_EQ(pipeforge::run_application("pipeforge-server", arguments, output, errors), 2);
        EXPECT_TRUE(output.str().empty());
        EXPECT_FALSE(errors.str().empty());
    }
}
} // namespace
