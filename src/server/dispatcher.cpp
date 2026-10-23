#include "pipeforge/diagnostics.hpp"
#include "pipeforge/services.hpp"
#include "pipeforge/validation.hpp"

#include <string>

namespace pipeforge::server {
namespace {
namespace p = protocol;

std::string_view operation_name(const p::operation& operation)
{
    // Names are local constants, never class/member names received from peers.
    constexpr std::array<std::string_view, std::variant_size_v<p::operation>> names{
        "list_classes",
        "describe_class",
        "put_value",
        "get_value",
        "list_values",
        "create_object",
        "get_object",
        "release_object",
        "get_attribute",
        "invoke_method"};
    return names.at(operation.index());
}
} // namespace

dispatcher::dispatcher(domain::session_store& store, diagnostic_session* diagnostics)
    : store_(store), diagnostics_(diagnostics)
{
}

p::result<p::response> dispatcher::dispatch(const p::request& request)
{
    if (diagnostics_) {
        PIPEFORGE_PROFILE_SCOPE(*diagnostics_, "server.dispatch");
        return process(request);
    }

    return process(request);
}

p::result<p::response> dispatcher::process(const p::request& request)
{
    if (request.id == 0) {
        return p::failure{p::failure_code::invalid_fields, true, std::nullopt};
    }

    p::outcome result = request.protocol_version == p::version
                            ? store_.execute(request.body)
                            : p::outcome{p::make_error(p::error_code::unsupported_version)};
    if (diagnostics_) {
        const auto* error = std::get_if<p::remote_error>(&result);
        diagnostics_->log(error ? log_level::warning : log_level::info,
                          "request=" + std::to_string(request.id) +
                              " operation=" + std::string(operation_name(request.body)) + " code=" +
                              std::to_string(error ? static_cast<unsigned int>(error->code) : 0));
    }

    return p::response{request.id, std::move(result)};
}

p::result<p::response> dispatcher::reject(const p::failure& failure) const
{
    if (failure.close_session || !failure.request_id || *failure.request_id == 0 ||
        failure.code == p::failure_code::malformed || failure.code == p::failure_code::truncated) {
        return p::failure{failure.code, true, std::nullopt};
    }

    auto code = p::error_code::invalid_request;
    if (failure.code == p::failure_code::unsupported_version) {
        code = p::error_code::unsupported_version;
    }
    else if (failure.code == p::failure_code::limit_exceeded) {
        code = p::error_code::quota_exceeded;
    }

    return p::response{*failure.request_id, p::make_error(code)};
}
} // namespace pipeforge::server
