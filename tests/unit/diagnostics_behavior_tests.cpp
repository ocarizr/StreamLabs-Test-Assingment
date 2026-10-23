#include "pipeforge/diagnostics.hpp"

#include <barrier>
#include <cstddef>
#include <set>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "test_files.hpp"

namespace {
using pipeforge::test_support::diagnostics_test;
using pipeforge::test_support::read_file;

std::vector<nlohmann::json> duration_events(const std::filesystem::path& path)
{
    const auto trace = nlohmann::json::parse(read_file(path));
    std::vector<nlohmann::json> events;
    for (const auto& event : trace.at("traceEvents")) {
        if (event.at("ph") == "X") {
            events.push_back(event);
        }
    }

    return events;
}

TEST_F(diagnostics_test, AppendFlushAndFinishArePredictable)
{
    const auto path = file("application.log");
    {
        std::ofstream output(path);
        output << "existing\n";
    }
    pipeforge::diagnostic_session session("unit", {.log_file = path});
    session.log(pipeforge::log_level::error, "failure {literal}");
    EXPECT_TRUE(session.finish());
    const auto text = read_file(path);
    EXPECT_NE(text.find("existing"), std::string::npos);
    EXPECT_NE(text.find("failure {literal}"), std::string::npos);
    EXPECT_NE(text.find("[error]"), std::string::npos);
    EXPECT_TRUE(session.finish());
    session.log(pipeforge::log_level::info, "after finish");
    EXPECT_EQ(read_file(path), text);
}

TEST_F(diagnostics_test, AliasPathsAreRejectedBeforeTruncation)
{
    const auto path = file("preserved.txt");
    {
        std::ofstream output(path);
        output << "preserved";
    }
    EXPECT_THROW((pipeforge::diagnostic_session(
                     "unit", {.log_file = path, .trace_file = file("./preserved.txt")})),
                 pipeforge::diagnostic_io_error);
    EXPECT_EQ(read_file(path), "preserved");
}

#ifdef _WIN32
TEST_F(diagnostics_test, RejectsCaseAliasesBeforeCreatingFiles)
{
    EXPECT_THROW((pipeforge::diagnostic_session(
                     "unit", {.log_file = file("same.log"), .trace_file = file("SAME.LOG")})),
                 pipeforge::diagnostic_io_error);
    EXPECT_FALSE(std::filesystem::exists(file("same.log")));
}
#endif

TEST_F(diagnostics_test, SerializesEscapedUnicodeLabelsAsValidJson)
{
    if (!pipeforge::profiling_enabled) {
        GTEST_SKIP();
    }

    const auto path = file("escaping.json");
    pipeforge::diagnostic_session session("unit", {.trace_file = path});
    const std::string label = "quoted\" slash\\ line\n café";
    {
        PIPEFORGE_PROFILE_SCOPE(session, label);
    }
    EXPECT_TRUE(session.finish());
    const auto events = duration_events(path);
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events.front().at("name"), label);
    EXPECT_GE(events.front().at("dur").get<std::int64_t>(), 0);
    EXPECT_GT(events.front().at("pid").get<std::uint64_t>(), 0);
}

TEST_F(diagnostics_test, EnforcesEventBudget)
{
    if (!pipeforge::profiling_enabled) {
        GTEST_SKIP();
    }

    const auto path = file("bounded.json");
    pipeforge::diagnostic_session session("unit", {.trace_file = path, .trace_event_limit = 1});
    {
        PIPEFORGE_PROFILE_SCOPE(session, "recorded");
    }
    {
        PIPEFORGE_PROFILE_SCOPE(session, "dropped");
    }
    EXPECT_TRUE(session.finish());
    EXPECT_EQ(duration_events(path).size(), 1);
    EXPECT_EQ(nlohmann::json::parse(read_file(path)).at("otherData").at("droppedEvents"), 1);
}

TEST_F(diagnostics_test, FinalizesEmptyTrace)
{
    if (!pipeforge::profiling_enabled) {
        GTEST_SKIP();
    }

    const auto path = file("empty.json");
    pipeforge::diagnostic_session session("unit", {.trace_file = path});
    EXPECT_TRUE(session.finish());
    EXPECT_TRUE(duration_events(path).empty());
}

TEST_F(diagnostics_test, FinalizesAfterScopesOutliveSessionOwner)
{
    if (!pipeforge::profiling_enabled) {
        GTEST_SKIP();
    }

    const auto path = file("deferred.json");
    std::unique_ptr<pipeforge::profile_scope> pending;
    {
        pipeforge::diagnostic_session session("unit", {.trace_file = path});
        pending = std::make_unique<pipeforge::profile_scope>(session, "pending");
        EXPECT_FALSE(session.finish());
    }
    pending.reset();
    EXPECT_EQ(duration_events(path).size(), 1);
}

TEST_F(diagnostics_test, RecordsDuringExceptionUnwinding)
{
    if (!pipeforge::profiling_enabled) {
        GTEST_SKIP();
    }

    const auto path = file("unwind.json");
    pipeforge::diagnostic_session session("unit", {.trace_file = path});
    EXPECT_THROW(([&session] {
                     PIPEFORGE_PROFILE_SCOPE(session, "unwind");
                     static_cast<void>(session);
                     throw std::runtime_error("test failure");
                 }()),
                 std::runtime_error);
    EXPECT_TRUE(session.finish());
    EXPECT_EQ(duration_events(path).size(), 1);
}

TEST_F(diagnostics_test, ConcurrentThreadsProduceDistinctTracks)
{
    if (!pipeforge::profiling_enabled) {
        GTEST_SKIP();
    }

    const auto path = file("threads.json");
    pipeforge::diagnostic_session session("unit", {.trace_file = path});
    constexpr std::size_t thread_count = 2;
    constexpr std::size_t events_per_thread = 8;
    std::barrier checkpoint(static_cast<std::ptrdiff_t>(thread_count));
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (std::size_t index = 0; index < thread_count; ++index) {
        workers.emplace_back([&session, &checkpoint] {
            static_cast<void>(session);
            checkpoint.arrive_and_wait();
            for (std::size_t event = 0; event < events_per_thread; ++event) {
                PIPEFORGE_PROFILE_SCOPE(session, "worker");
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    EXPECT_TRUE(session.finish());
    const auto events = duration_events(path);
    EXPECT_EQ(events.size(), thread_count * events_per_thread);
    std::set<std::uint64_t> tracks;
    for (const auto& event : events) {
        tracks.insert(event.at("tid").get<std::uint64_t>());
    }

    EXPECT_EQ(tracks.size(), thread_count);
}
} // namespace
