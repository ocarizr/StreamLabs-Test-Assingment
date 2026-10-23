#include "pipeforge/protocol.hpp"

#include <chrono>
#include <iostream>
#include <string_view>

#include <nlohmann/json.hpp>

namespace {
template <typename Message, typename Encode, typename Decode>
bool measure(std::string_view label,
             const Message& message,
             const nlohmann::json& reference,
             Encode encode,
             Decode decode)
{
    const auto initial = encode(message);
    if (!std::holds_alternative<pipeforge::protocol::bytes>(initial)) {
        return false;
    }

    const auto& payload = std::get<pipeforge::protocol::bytes>(initial);
    if (decode(payload) != pipeforge::protocol::result<Message>{message}) {
        return false;
    }

    constexpr std::size_t iterations = 2000;
    const auto start = std::chrono::steady_clock::now();
    std::size_t observed = 0;
    for (std::size_t index = 0; index < iterations; ++index) {
        const auto encoded = encode(message);
        if (const auto* bytes = std::get_if<pipeforge::protocol::bytes>(&encoded)) {
            observed += bytes->size();
        }
        else {
            return false;
        }
    }

    const auto middle = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < iterations; ++index) {
        if (!std::holds_alternative<Message>(decode(payload))) {
            return false;
        }
    }

    const auto end = std::chrono::steady_clock::now();
    const auto encode_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(middle - start).count() /
        static_cast<std::int64_t>(iterations);
    const auto decode_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - middle).count() /
        static_cast<std::int64_t>(iterations);
    std::cout << label << ',' << payload.size() << ',' << reference.dump().size() << ','
              << encode_ns << ',' << decode_ns << ',' << observed << '\n';
    return true;
}
} // namespace

// Measurement-only process: unrecoverable allocation failure remains fatal.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main()
{
    namespace protocol = pipeforge::protocol;
    std::cout << "case,protobuf_bytes,json_reference_bytes,encode_ns,decode_ns,checksum\n";
    const protocol::request control{1, protocol::get_object{1}};
    const protocol::request object{
        1, protocol::invoke_method{1, "increment", {{"delta", std::int64_t{-1}}}}};
    const std::string text(protocol::max_string_bytes, 'x');
    const protocol::request long_text{1, protocol::put_value{text}};
    protocol::values_page page;
    nlohmann::json values = nlohmann::json::array();
    for (std::uint64_t id = 1; id <= protocol::max_entries; ++id) {
        page.values.push_back({id, -static_cast<std::int64_t>(id)});
        values.push_back({{"id", id}, {"integer", -static_cast<std::int64_t>(id)}});
    }

    const protocol::response batch{1, page};
    const auto requests = [](std::string_view label,
                             const protocol::request& message,
                             const nlohmann::json& reference) {
        return measure(
            label, message, reference, protocol::encode_request, protocol::decode_request);
    };
    return requests(
               "control",
               control,
               nlohmann::json{{"version", 1}, {"request_id", 1}, {"get_object", {{"id", 1}}}}) &&
                   requests(
                       "object",
                       object,
                       nlohmann::json{{"version", 1},
                                      {"request_id", 1},
                                      {"invoke_method",
                                       {{"id", 1},
                                        {"name", "increment"},
                                        {"arguments", {{{"name", "delta"}, {"integer", -1}}}}}}}) &&
                   requests("long_text",
                            long_text,
                            nlohmann::json{{"version", 1},
                                           {"request_id", 1},
                                           {"put_value", {{"text", text}}}}) &&
                   measure("numeric_batch",
                           batch,
                           nlohmann::json{{"version", 1}, {"request_id", 1}, {"values", values}},
                           protocol::encode_response,
                           protocol::decode_response)
               ? 0
               : 1;
}
