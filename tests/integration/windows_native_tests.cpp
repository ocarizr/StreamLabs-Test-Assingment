#include "pipeforge/pipe_server.hpp"

#include <aclapi.h>
#include <atomic>

#include <gtest/gtest.h>

#include "domain_test_support.hpp"
#include "native.hpp"

namespace pipeforge::transport {
namespace {
// Security/native-boundary fixtures intentionally use fixed DWORD and byte values.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
std::wstring native_endpoint()
{
    static std::atomic<unsigned int> sequence{0};
    return L"\\\\.\\pipe\\pipeforge-native-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
           std::to_wstring(++sequence);
}

TEST(PipeNative, ClientConnectedBeforeAcceptIsAValidConnection)
{
    const auto name = native_endpoint();
    auto security = native::logon_security();
    ASSERT_TRUE(std::holds_alternative<native::local_memory>(security));
    auto created =
        native::create_instance(name, 1, true, std::get<native::local_memory>(security).get());
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<native::unique_handle>>(created));
    const auto& pipe = std::get<std::shared_ptr<native::unique_handle>>(created);
    native::unique_handle client(CreateFileW(name.c_str(),
                                             GENERIC_READ | GENERIC_WRITE,
                                             0,
                                             nullptr,
                                             OPEN_EXISTING,
                                             FILE_FLAG_OVERLAPPED,
                                             nullptr));
    ASSERT_TRUE(static_cast<bool>(client));
    EXPECT_TRUE(std::holds_alternative<std::size_t>(
        native::accept(pipe->get(), clock::now() + std::chrono::seconds{2}, {})));
}

TEST(PipeNative, PendingReadsAndBackpressuredWritesDrainBeforeStorageIsReleased)
{
    const auto name = native_endpoint();
    auto security = native::logon_security();
    ASSERT_TRUE(std::holds_alternative<native::local_memory>(security));
    auto created =
        native::create_instance(name, 1, true, std::get<native::local_memory>(security).get());
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<native::unique_handle>>(created));
    const auto& pipe = std::get<std::shared_ptr<native::unique_handle>>(created);
    native::unique_handle client(CreateFileW(name.c_str(),
                                             GENERIC_READ | GENERIC_WRITE,
                                             0,
                                             nullptr,
                                             OPEN_EXISTING,
                                             FILE_FLAG_OVERLAPPED,
                                             nullptr));
    ASSERT_TRUE(static_cast<bool>(client));
    ASSERT_TRUE(
        std::holds_alternative<std::size_t>(native::accept(pipe->get(), deadline::max(), {})));
    for (bool writing : {false, true}) {
        protocol::bytes storage(protocol::max_payload_bytes);
        std::stop_source stop;
        native::overlapped_operation operation(pipe->get(), stop.get_token());
        ASSERT_FALSE(operation.setup_error());
        DWORD count = 0;
        const auto started = writing ? WriteFile(pipe->get(),
                                                 storage.data(),
                                                 static_cast<DWORD>(storage.size()),
                                                 &count,
                                                 operation.operation())
                                     : ReadFile(pipe->get(),
                                                storage.data(),
                                                static_cast<DWORD>(storage.size()),
                                                &count,
                                                operation.operation());
        const auto code = started ? ERROR_SUCCESS : GetLastError();
        ASSERT_FALSE(started);
        ASSERT_EQ(code, ERROR_IO_PENDING);
        operation.mark_pending();
        stop.request_stop();
        const auto result = detail::resolve_pending(operation, deadline::max());
        ASSERT_TRUE(std::holds_alternative<failure>(result));
        EXPECT_EQ(std::get<failure>(result).kind, failure_kind::cancelled);
        const auto observed =
            GetOverlappedResult(pipe->get(), operation.operation(), &count, FALSE);
        EXPECT_NE(observed ? ERROR_SUCCESS : GetLastError(), ERROR_IO_INCOMPLETE);
    }
}

TEST(PipeSecurity, ProtectedDaclGrantsOnlyTheLogonSidAndDeniesRestrictedToken)
{
    const auto name = native_endpoint();
    auto host = pipe_server::start(test_support::registered_classes(), {name});
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<pipe_server>>(host));
    auto connected = pipe_stream::connect(name, clock::now() + std::chrono::seconds{2});
    ASSERT_TRUE(std::holds_alternative<pipe_stream>(connected));
    const auto& client = std::get<pipe_stream>(connected);
    PSECURITY_DESCRIPTOR raw_descriptor = nullptr;
    PACL dacl = nullptr;
    ASSERT_EQ(GetSecurityInfo(client.native_handle(),
                              SE_KERNEL_OBJECT,
                              OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                  DACL_SECURITY_INFORMATION,
                              nullptr,
                              nullptr,
                              &dacl,
                              nullptr,
                              &raw_descriptor),
              ERROR_SUCCESS);
    const native::local_memory descriptor(raw_descriptor);
    ASSERT_NE(dacl, nullptr);
    ASSERT_EQ(dacl->AceCount, 1);
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    ASSERT_TRUE(GetSecurityDescriptorControl(descriptor.get(), &control, &revision));
    EXPECT_NE(control & SE_DACL_PROTECTED, 0);
    void* raw_ace = nullptr;
    ASSERT_TRUE(GetAce(dacl, 0, &raw_ace));
    auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw_ace);
    ASSERT_EQ(ace->Header.AceType, ACCESS_ALLOWED_ACE_TYPE);
    // The Win32 ACE layout stores a variable-length SID at SidStart.
    auto* sid = &ace->SidStart;
    ASSERT_TRUE(IsValidSid(sid));
    EXPECT_EQ(*GetSidSubAuthorityCount(sid), SECURITY_LOGON_IDS_RID_COUNT);
    EXPECT_EQ(*GetSidSubAuthority(sid, 0), SECURITY_LOGON_IDS_RID);
    HANDLE raw_token = nullptr;
    ASSERT_TRUE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &raw_token));
    native::unique_handle token(raw_token);
    HANDLE raw_impersonation = nullptr;
    ASSERT_TRUE(DuplicateToken(token.get(), SecurityIdentification, &raw_impersonation));
    native::unique_handle impersonation(raw_impersonation);
    GENERIC_MAPPING mapping{
        FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    PRIVILEGE_SET privileges{};
    DWORD privilege_size = sizeof(privileges);
    DWORD granted = 0;
    BOOL allowed = FALSE;
    ASSERT_TRUE(AccessCheck(descriptor.get(),
                            impersonation.get(),
                            FILE_READ_DATA | FILE_WRITE_DATA,
                            &mapping,
                            &privileges,
                            &privilege_size,
                            &granted,
                            &allowed));
    EXPECT_TRUE(allowed);
    alignas(DWORD) std::array<std::byte, SECURITY_MAX_SID_SIZE> world{};
    DWORD world_size = static_cast<DWORD>(world.size());
    ASSERT_TRUE(CreateWellKnownSid(WinWorldSid, nullptr, world.data(), &world_size));
    SID_AND_ATTRIBUTES restriction{world.data(), 0};
    HANDLE raw_restricted = nullptr;
    ASSERT_TRUE(CreateRestrictedToken(token.get(),
                                      DISABLE_MAX_PRIVILEGE,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &restriction,
                                      &raw_restricted));
    native::unique_handle restricted(raw_restricted);
    HANDLE raw_restricted_impersonation = nullptr;
    ASSERT_TRUE(
        DuplicateToken(restricted.get(), SecurityIdentification, &raw_restricted_impersonation));
    native::unique_handle restricted_impersonation(raw_restricted_impersonation);
    privilege_size = sizeof(privileges);
    ASSERT_TRUE(AccessCheck(descriptor.get(),
                            restricted_impersonation.get(),
                            FILE_READ_DATA | FILE_WRITE_DATA,
                            &mapping,
                            &privileges,
                            &privilege_size,
                            &granted,
                            &allowed));
    EXPECT_FALSE(allowed);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
} // namespace
} // namespace pipeforge::transport
