#include "pipeforge/application.hpp"
#include "pipeforge/client.hpp"
#include "pipeforge/diagnostics.hpp"

#include <algorithm>
#include <limits>
#include <ostream>

namespace pipeforge {
namespace {
namespace p = protocol;
namespace c = client;
namespace t = transport;
constexpr std::int64_t initial_counter = 7;
constexpr std::int64_t other_counter = 42;
constexpr std::int64_t increment = 5;
constexpr std::uint32_t page_size = 2;
constexpr std::size_t quota_probe_limit = 128;

/** Demo composition uses public contracts only; no domain or server linkage. */
class scenario {
  public:
    scenario(c::connection& client, const invocation& options) : client_(client), options_(options)
    {
    }

    ~scenario() = default;
    scenario(const scenario&) = delete;
    scenario& operator=(const scenario&) = delete;
    scenario(scenario&&) = delete;
    scenario& operator=(scenario&&) = delete;

    [[nodiscard]] t::deadline until() const
    {
        return t::clock::now() + options_.timeout;
    }

    c::call_result execute(p::operation operation)
    {
        ++calls_;
        const auto blocking =
            options_.command == application_command::demo_sync ||
            (options_.command == application_command::demo_mixed && calls_ % page_size == 0);
        return blocking ? client_.call(std::move(operation), until())
                        : client_.call_async(std::move(operation), until()).get();
    }

    template <typename T> std::optional<T> ask(p::operation operation)
    {
        return extract<T>(execute(std::move(operation)));
    }

    template <typename T> static std::optional<T> extract(c::call_result result)
    {
        const auto* response = std::get_if<p::response>(&result);
        const auto* outcome = response ? std::get_if<T>(&response->body) : nullptr;
        return outcome ? std::optional<T>{*outcome} : std::nullopt;
    }

    bool error(p::operation operation, p::error_code expected)
    {
        const auto result = ask<p::remote_error>(std::move(operation));
        return result && result->code == expected;
    }

    bool values()
    {
        const std::vector<p::value> inputs{std::string{},
                                           std::string{"caf\xc3\xa9"},
                                           std::numeric_limits<std::int64_t>::min(),
                                           std::numeric_limits<std::int64_t>::max(),
                                           double{0},
                                           double{1.25}};
        std::vector<p::value_record> saved;
        saved.reserve(inputs.size());
        if (options_.command == application_command::demo_sync) {
            for (const auto& input : inputs) {
                const auto record = ask<p::value_record>(p::put_value{input});
                if (!record) {
                    return false;
                }

                saved.push_back(*record);
            }
        }
        else {
            // Submit the entire bounded batch before waiting: real process demos
            // exercise multiple outstanding requests, not just future.get loops.
            std::vector<std::future<c::call_result>> pending;
            pending.reserve(inputs.size());
            for (const auto& input : inputs) {
                pending.push_back(client_.call_async(p::put_value{input}, until()));
            }

            for (auto& future : pending) {
                const auto record = extract<p::value_record>(future.get());
                if (!record) {
                    return false;
                }

                saved.push_back(*record);
            }
        }

        for (std::size_t index = 0; index < inputs.size(); ++index) {
            const auto record = ask<p::value_record>(p::get_value{saved.at(index).id});
            if (!record || record->data != inputs.at(index)) {
                return false;
            }
        }

        std::vector<p::value_record> listed;
        std::uint64_t cursor = 0;
        while (true) {
            const auto page = ask<p::values_page>(p::list_values{cursor, page_size});
            if (!page || listed.size() + page->values.size() > inputs.size()) {
                return false;
            }

            listed.insert(listed.end(), page->values.begin(), page->values.end());
            if (page->next == 0) {
                break;
            }

            if (page->values.empty() || page->next <= cursor) {
                return false;
            }

            cursor = page->next;
        }

        if (listed != saved) {
            return false;
        }

        stored_value_ = saved.front().id;

        // Bound the probe even against a misconfigured server; the standard host
        // reaches its per-session quota without disconnecting the client.
        for (std::size_t index = 0; index < quota_probe_limit; ++index) {
            auto result = execute(p::put_value{std::int64_t{0}});
            const auto* response = std::get_if<p::response>(&result);
            if (!response) {
                return false;
            }

            if (const auto* error = std::get_if<p::remote_error>(&response->body)) {
                return error->code == p::error_code::quota_exceeded;
            }

            if (!std::holds_alternative<p::value_record>(response->body)) {
                return false;
            }
        }

        return false;
    }

    bool objects()
    {
        const auto classes = ask<p::classes_page>(p::list_classes{});
        if (!classes || !std::ranges::binary_search(classes->names, "Counter") ||
            !std::ranges::binary_search(classes->names, "TextBuffer")) {
            return false;
        }

        const auto metadata = ask<p::class_description>(p::describe_class{"TextBuffer"});
        if (!metadata || metadata->constructor_parameters.empty() || metadata->methods.empty()) {
            return false;
        }

        const auto first =
            ask<p::object_snapshot>(p::create_object{"Counter", {{"initial", initial_counter}}});
        const auto second =
            ask<p::object_snapshot>(p::create_object{"Counter", {{"initial", other_counter}}});
        if (!first || !second || first->id == second->id) {
            return false;
        }

        retained_object_ = second->id;
        const auto changed =
            ask<p::value>(p::invoke_method{first->id, "increment", {{"delta", increment}}});
        const auto attribute = ask<p::value>(p::get_attribute{first->id, "value"});
        const auto retained = ask<p::object_snapshot>(p::get_object{first->id});
        const auto independent = ask<p::value>(p::get_attribute{second->id, "value"});
        const p::value expected{initial_counter + increment};
        if (!changed || !attribute || !retained || !independent || *changed != expected ||
            *attribute != expected || *independent != p::value{other_counter} ||
            retained->state != std::vector<p::named_value>{{"value", expected}}) {
            return false;
        }

        const auto text = ask<p::object_snapshot>(
            p::create_object{"TextBuffer", {{"initial", std::string{"hello"}}}});
        if (!text) {
            return false;
        }

        const auto appended = ask<p::value>(
            p::invoke_method{text->id, "append", {{"suffix", std::string{" world"}}}});
        if (!appended || *appended != p::value{std::string{"hello world"}}) {
            return false;
        }

        if (!error(p::invoke_method{first->id, "increment", {{"delta", std::string{"wrong"}}}},
                   p::error_code::invalid_request) ||
            !error(p::get_attribute{first->id, "missing"}, p::error_code::not_found) ||
            !error(p::create_object{"MissingClass", {}}, p::error_code::not_found)) {
            return false;
        }

        if (!ask<p::released>(p::release_object{first->id}) ||
            !error(p::get_object{first->id}, p::error_code::not_found)) {
            return false;
        }

        c::connection isolated(c::named_pipe_connector());
        if (!std::holds_alternative<c::connected>(
                isolated.connect_async(options_.endpoint, until()).get())) {
            return false;
        }

        scenario outsider(isolated, options_);
        return outsider.error(p::get_object{second->id}, p::error_code::not_found) &&
               outsider.error(p::get_value{stored_value_}, p::error_code::not_found);
    }

    bool reconnect()
    {
        client_.close();
        c::connection fresh(c::named_pipe_connector());
        if (!std::holds_alternative<c::connected>(fresh.connect(options_.endpoint, until()))) {
            return false;
        }

        scenario replacement(fresh, options_);
        return replacement.error(p::get_object{retained_object_}, p::error_code::not_found) &&
               replacement.error(p::get_value{stored_value_}, p::error_code::not_found);
    }

  private:
    c::connection& client_;
    const invocation& options_;
    std::size_t calls_ = 0;
    std::uint64_t retained_object_ = 0;
    std::uint64_t stored_value_ = 0;
};
} // namespace

int run_client_command(const invocation& options,
                       std::ostream& output,
                       std::ostream& errors,
                       diagnostic_session& diagnostics)
{
    c::connection client(c::named_pipe_connector(), &diagnostics);
    const auto until = t::clock::now() + options.timeout;
    const auto opened = options.command == application_command::demo_sync
                            ? client.connect(options.endpoint, until)
                            : client.connect_async(options.endpoint, until).get();
    if (const auto* error = std::get_if<c::failure>(&opened)) {
        errors << "Client connection failed kind=" << static_cast<unsigned int>(error->kind)
               << " native=" << error->native_error << '\n';
        return 1;
    }

    scenario demo(client, options);
    if (!demo.values() || !demo.objects() || !demo.reconnect()) {
        errors << "R1-R8 scenario failed; no operation was replayed.\n";
        return 1;
    }

    const auto mode = options.command == application_command::demo_sync    ? "sync"
                      : options.command == application_command::demo_async ? "async"
                                                                           : "mixed";
    output << "R1-R8 scenario passed mode=" << mode << '\n';
    return output ? 0 : 1;
}
} // namespace pipeforge
