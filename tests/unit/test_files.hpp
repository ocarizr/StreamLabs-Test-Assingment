#ifndef PIPEFORGE_TEST_FILES_HPP
#define PIPEFORGE_TEST_FILES_HPP

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>

namespace pipeforge::test_support {
inline std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

class diagnostics_test : public testing::Test {
  protected:
    void SetUp() override
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path() /
                     ("pipeforge-" +
                      std::string(testing::UnitTest::GetInstance()->current_test_info()->name()) +
                      "-" + std::to_string(stamp));
        created_ = std::filesystem::create_directory(directory_);
        ASSERT_TRUE(created_);
    }

    void TearDown() override
    {
        if (created_) {
            // Cleanup is confined to the directory successfully created here.
            std::error_code error;
            std::filesystem::remove_all(directory_, error);
            EXPECT_FALSE(error);
        }
    }

    [[nodiscard]] std::filesystem::path file(std::string_view name) const
    {
        return directory_ / name;
    }

  private:
    std::filesystem::path directory_;
    bool created_ = false;
};
} // namespace pipeforge::test_support

#endif // PIPEFORGE_TEST_FILES_HPP
