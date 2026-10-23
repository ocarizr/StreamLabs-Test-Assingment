#include "pipeforge/application.hpp"
#include "pipeforge/version.hpp"

#include <array>
#include <gtest/gtest.h>
#include <sstream>

namespace {
TEST(Application, ReportsBuildVersion) {
    std::ostringstream output;
    std::ostringstream errors;
    const std::array arguments{std::string_view{"--version"}};
    EXPECT_EQ(pipeforge::run_application("pipeforge-client", arguments, output, errors), 0);
    EXPECT_EQ(output.str(), "pipeforge-client " + std::string(pipeforge::version) + "\n");
    EXPECT_TRUE(errors.str().empty());
}

TEST(Application, HelpExplainsCurrentFeatureBoundary) {
    std::ostringstream output;
    std::ostringstream errors;
    const std::array arguments{std::string_view{"--help"}};
    EXPECT_EQ(pipeforge::run_application("pipeforge-server", arguments, output, errors), 0);
    EXPECT_NE(output.str().find("Usage: pipeforge-server"), std::string::npos);
    EXPECT_NE(output.str().find("IPC is not implemented"), std::string::npos);
    EXPECT_TRUE(errors.str().empty());
}

TEST(Application, NoArgumentsDoesNotPretendToStartIPC) {
    std::ostringstream output;
    std::ostringstream errors;
    EXPECT_EQ(pipeforge::run_application("pipeforge-server", {}, output, errors), 2);
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(errors.str().find("IPC is not implemented"), std::string::npos);
}

TEST(Application, RejectsUnknownOption) {
    std::ostringstream output;
    std::ostringstream errors;
    const std::array arguments{std::string_view{"--connect"}};
    EXPECT_EQ(pipeforge::run_application("pipeforge-client", arguments, output, errors), 2);
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(errors.str().find("Unsupported arguments"), std::string::npos);
}

TEST(Application, RejectsExtraArgumentsEvenAfterHelp) {
    std::ostringstream output;
    std::ostringstream errors;
    const std::array arguments{std::string_view{"--help"}, std::string_view{"unexpected"}};
    EXPECT_EQ(pipeforge::run_application("pipeforge-client", arguments, output, errors), 2);
    EXPECT_TRUE(output.str().empty());
    EXPECT_FALSE(errors.str().empty());
}
} // namespace
