#include "pipeforge/client.hpp"

#include "pipeforge/diagnostics.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <thread>

namespace pipeforge::client {
namespace {
namespace p = protocol;
namespace t = transport;

template <typename T> std::future<T> ready(T value)
{
    std::promise<T> promise;
    promise.set_value(std::move(value));
    return promise.get_future();
}

failure translate(const t::failure& error)
{
    switch (error.kind) {
    case t::failure_kind::timeout:
        return {failure_kind::timeout, error.native_error};
    case t::failure_kind::cancelled:
        return {failure_kind::cancelled, error.native_error};
    case t::failure_kind::disconnected:
        return {failure_kind::disconnected, error.native_error};
    case t::failure_kind::access_denied:
        return {failure_kind::access_denied, error.native_error};
    case t::failure_kind::protocol_violation:
        return {failure_kind::protocol_violation, error.native_error};
    case t::failure_kind::invalid_configuration:
        return {failure_kind::invalid_request, error.native_error};
    default:
        return {failure_kind::transport_failure, error.native_error};
    }
}

bool matches(const p::operation& operation, const p::outcome& outcome)
{
    if (std::holds_alternative<p::remote_error>(outcome)) {
        return true;
    }

    return std::visit(
        [&outcome](const auto& request) {
            using operation_type = std::decay_t<decltype(request)>;
            if constexpr (std::is_same_v<operation_type, p::list_classes>) {
                return std::holds_alternative<p::classes_page>(outcome);
            }
            else if constexpr (std::is_same_v<operation_type, p::describe_class>) {
                const auto* description = std::get_if<p::class_description>(&outcome);
                return description && description->name == request.name;
            }
            else if constexpr (std::is_same_v<operation_type, p::put_value>) {
                const auto* record = std::get_if<p::value_record>(&outcome);
                return record && record->data == request.data;
            }
            else if constexpr (std::is_same_v<operation_type, p::get_value>) {
                const auto* record = std::get_if<p::value_record>(&outcome);
                return record && record->id == request.id;
            }
            else if constexpr (std::is_same_v<operation_type, p::list_values>) {
                return std::holds_alternative<p::values_page>(outcome);
            }
            else if constexpr (std::is_same_v<operation_type, p::create_object>) {
                const auto* object = std::get_if<p::object_snapshot>(&outcome);
                return object && object->class_name == request.class_name;
            }
            else if constexpr (std::is_same_v<operation_type, p::get_object>) {
                const auto* object = std::get_if<p::object_snapshot>(&outcome);
                return object && object->id == request.id;
            }
            else if constexpr (std::is_same_v<operation_type, p::release_object>) {
                return std::holds_alternative<p::released>(outcome);
            }
            else {
                return std::holds_alternative<p::value>(outcome);
            }
        },
        operation);
}
} // namespace

class connection_state {
  public:
    connection_state(connector open, diagnostic_session* diagnostics)
        : open_(std::move(open)), diagnostics_(diagnostics ? diagnostics : &silent_)
    {
    }

    ~connection_state()
    {
        close();
    }

    connection_state(const connection_state&) = delete;
    connection_state& operator=(const connection_state&) = delete;
    connection_state(connection_state&&) = delete;
    connection_state& operator=(connection_state&&) = delete;

    std::future<connect_result>
    connect_async(std::wstring endpoint, t::deadline until, const std::stop_token& stop)
    {
        std::unique_lock lock(mutex_);
        if (started_ || closed_ || !open_ || !t::valid_endpoint(endpoint)) {
            return ready(connect_result{failure{failure_kind::invalid_request}});
        }

        started_ = true;
        auto future = opening_.get_future();
        if (stop.stop_requested() || t::clock::now() >= until) {
            fail_locked({stop.stop_requested() ? failure_kind::cancelled : failure_kind::timeout});
            return future;
        }

        try {
            // Hold the publication lock until all workers exist: a partially
            // acquired thread set must never publish a successful connection.
            writer_ = std::jthread([this, endpoint = std::move(endpoint), until, stop] {
                write_loop(endpoint, until, stop);
            });
            reader_ = std::jthread([this] {
                read_loop();
            });
            monitor_ = std::jthread([this] {
                monitor_loop();
            });
        }
        catch (const std::system_error& error) {
            // Thread creation is an OS resource boundary. Drain any partial set.
            fail_locked(
                {failure_kind::internal_failure, static_cast<std::uint32_t>(error.code().value())});
            lock.unlock();
            close();
        }

        return future;
    }

    std::future<call_result>
    call_async(p::operation operation, t::deadline until, const std::stop_token& stop)
    {
        const std::lock_guard lock(mutex_);
        if (!connected_ || closed_) {
            return ready(call_result{failure{failure_kind::not_connected}});
        }

        if (stop.stop_requested()) {
            return ready(call_result{failure{failure_kind::cancelled}});
        }

        if (t::clock::now() >= until) {
            return ready(call_result{failure{failure_kind::timeout}});
        }

        if (pending_.size() >= max_pending_calls ||
            next_id_ == std::numeric_limits<std::uint64_t>::max()) {
            return ready(call_result{failure{failure_kind::quota_exceeded}});
        }

        // Validate/encode before admission; each owned buffer is at most one M2
        // payload. The entry cap also bounds queued and in-flight buffer counts.
        p::request request{next_id_, std::move(operation)};
        auto encoded = p::encode_request(request);
        if (!std::holds_alternative<p::bytes>(encoded)) {
            return ready(call_result{failure{failure_kind::invalid_request}});
        }

        ++next_id_;
        auto entry = std::make_shared<pending_call>(
            std::move(request), std::get<p::bytes>(std::move(encoded)), until, stop);
        auto future = entry->promise.get_future();
        pending_.emplace(entry->request.id, entry);
        queued_.push_back(std::move(entry));
        changed_.notify_all();
        return future;
    }

    void close() noexcept
    {
        {
            const std::lock_guard lock(mutex_);
            fail_locked({failure_kind::cancelled});
        }
        // No worker waits for the controller's mutex while it is being joined.
        // The channel remains alive until both directions have drained their I/O.
        writer_ = std::jthread{};
        reader_ = std::jthread{};
        monitor_ = std::jthread{};

        if (channel_) {
            channel_->close();
        }
    }

  private:
    enum class phase : std::uint8_t { queued, writing, sent };

    struct pending_call {
        pending_call(p::request message,
                     p::bytes payload,
                     t::deadline deadline,
                     const std::stop_token& cancellation)
            : request(std::move(message)), encoded(std::move(payload)), until(deadline),
              stop(cancellation)
        {
        }

        p::request request;
        p::bytes encoded;
        t::deadline until;
        std::stop_token stop;
        std::promise<call_result> promise;
        phase progress = phase::queued;
    };

    // The mutex/closed/opening_done invariants protect every promise's single
    // completion. A future_error here is a fatal programming defect, not I/O.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    void fail_locked(failure common, std::uint64_t specific_id = 0, failure specific = {}) noexcept
    {
        if (closed_) {
            return;
        }

        closed_ = true;
        shutdown_.request_stop();
        if (started_ && !opening_done_) {
            opening_.set_value(common);
            opening_done_ = true;
        }

        for (auto& [id, entry] : pending_) {
            entry->promise.set_value(id == specific_id ? specific : common);
        }

        pending_.clear();
        queued_.clear();
        changed_.notify_all();
    }

    bool wait_connected(std::unique_lock<std::mutex>& lock)
    {
        changed_.wait(lock, [this] {
            return connected_ || closed_;
        });
        return !closed_;
    }

    void
    write_loop(const std::wstring& endpoint, t::deadline until, const std::stop_token& connect_stop)
    {
        auto opened = [&] {
            PIPEFORGE_PROFILE_SCOPE(*diagnostics_, "client.connect");
            const std::stop_callback cancellation(connect_stop, [this] {
                shutdown_.request_stop();
            });
            if (shutdown_.stop_requested()) {
                return t::result<std::unique_ptr<channel>>{t::failure{t::failure_kind::cancelled}};
            }

            return open_(endpoint, until, shutdown_.get_token());
        }();
        {
            const std::lock_guard lock(mutex_);
            if (const auto* error = std::get_if<t::failure>(&opened)) {
                fail_locked(translate(*error));
                return;
            }

            channel_ = std::get<std::unique_ptr<channel>>(std::move(opened));
            if (closed_) {
                return;
            }

            if (!channel_) {
                fail_locked({failure_kind::internal_failure});
                return;
            }

            if (shutdown_.stop_requested()) {
                fail_locked({failure_kind::cancelled});
                return;
            }

            if (t::clock::now() >= until) {
                fail_locked({failure_kind::timeout});
                return;
            }

            connected_ = true;
            opening_done_ = true;
            opening_.set_value(connected{});
            changed_.notify_all();
        }

        while (true) {
            std::shared_ptr<pending_call> entry;
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [this] {
                    return closed_ || !queued_.empty();
                });
                if (closed_) {
                    return;
                }

                entry = queued_.front();
                queued_.pop_front();
                // Cancellation observed before this transition is unsent and
                // local; after it, execution is conservatively indeterminate.
                if (entry->stop.stop_requested() || t::clock::now() >= entry->until) {
                    entry->promise.set_value(failure{entry->stop.stop_requested()
                                                         ? failure_kind::cancelled
                                                         : failure_kind::timeout});
                    pending_.erase(entry->request.id);
                    continue;
                }

                entry->progress = phase::writing;
            }
            auto sent = [&] {
                PIPEFORGE_PROFILE_SCOPE(*diagnostics_, "client.send");
                return channel_->send(entry->encoded, entry->until, shutdown_.get_token());
            }();
            const std::lock_guard lock(mutex_);
            if (closed_) {
                return;
            }

            if (const auto* error = std::get_if<t::failure>(&sent)) {
                fail_locked({failure_kind::disconnected}, entry->request.id, translate(*error));
                return;
            }

            // A fast peer may already have completed the promise. Never reinsert
            // that entry or access the map through a reference retained over I/O.
            if (pending_.contains(entry->request.id)) {
                entry->progress = phase::sent;
            }
        }
    }

    void read_loop()
    {
        {
            std::unique_lock lock(mutex_);
            if (!wait_connected(lock)) {
                return;
            }
        }
        while (!shutdown_.stop_requested()) {
            auto payload = [&] {
                PIPEFORGE_PROFILE_SCOPE(*diagnostics_, "client.receive");
                return channel_->receive(t::deadline::max(), shutdown_.get_token());
            }();
            std::unique_lock lock(mutex_);
            if (closed_) {
                return;
            }

            if (const auto* error = std::get_if<t::failure>(&payload)) {
                fail_locked(translate(*error));
                return;
            }

            auto decoded = p::decode_response(std::get<p::bytes>(payload));
            if (!std::holds_alternative<p::response>(decoded)) {
                fail_locked({failure_kind::protocol_violation});
                return;
            }

            auto response = std::get<p::response>(std::move(decoded));
            const auto found = pending_.find(response.id);
            if (found == pending_.end() || found->second->progress == phase::queued ||
                !matches(found->second->request.body, response.body)) {
                fail_locked({failure_kind::protocol_violation});
                return;
            }

            // A response racing the monitor cannot turn an already expired or
            // cancelled sent request into success simply by winning the lock.
            const auto& entry = found->second;
            if (entry->stop.stop_requested() || t::clock::now() >= entry->until) {
                fail_locked({failure_kind::disconnected},
                            response.id,
                            {entry->stop.stop_requested() ? failure_kind::cancelled
                                                          : failure_kind::timeout});
                return;
            }

            const auto response_id = response.id;
            found->second->promise.set_value(std::move(response));
            pending_.erase(found);
            lock.unlock();

            // Optional synchronous file logging must not hold the admission/
            // cancellation mutex while the diagnostic sink performs I/O.
            diagnostics_->log(log_level::debug,
                              "client response id=" + std::to_string(response_id));
        }
    }

    void monitor_loop()
    {
        std::unique_lock lock(mutex_);
        if (!wait_connected(lock)) {
            return;
        }

        constexpr auto cancellation_slice = std::chrono::milliseconds{5};
        while (!closed_) {
            if (pending_.empty()) {
                changed_.wait(lock, [this] {
                    return closed_ || !pending_.empty();
                });
                continue;
            }

            const auto now = t::clock::now();
            auto wake = now + cancellation_slice;
            for (auto iterator = pending_.begin(); iterator != pending_.end();) {
                const auto& entry = iterator->second;
                const auto cancelled = entry->stop.stop_requested();
                if (cancelled || now >= entry->until) {
                    const failure cause{cancelled ? failure_kind::cancelled
                                                  : failure_kind::timeout};
                    if (entry->progress != phase::queued) {
                        fail_locked({failure_kind::disconnected}, iterator->first, cause);
                        return;
                    }

                    entry->promise.set_value(cause);
                    // Erase cancelled queue entries too, so repeated cancellation
                    // and readmission cannot accumulate stale payload buffers.
                    std::erase(queued_, entry);
                    iterator = pending_.erase(iterator);
                }
                else {
                    wake = std::min(wake, entry->until);
                    ++iterator;
                }
            }

            changed_.wait_until(lock, wake);
        }
    }

    connector open_;
    diagnostic_session silent_{"pipeforge-client", {}};
    diagnostic_session* diagnostics_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::stop_source shutdown_;
    std::unique_ptr<channel> channel_;
    std::promise<connect_result> opening_;
    std::map<std::uint64_t, std::shared_ptr<pending_call>> pending_;
    std::deque<std::shared_ptr<pending_call>> queued_;
    std::uint64_t next_id_ = 1;
    bool started_ = false;
    bool connected_ = false;
    bool opening_done_ = false;
    bool closed_ = false;
    std::jthread writer_;
    std::jthread reader_;
    std::jthread monitor_;
};

connection::connection(connector open, diagnostic_session* diagnostics)
    : state_(std::make_unique<connection_state>(std::move(open), diagnostics))
{
}

connection::~connection() = default;

std::future<connect_result>
connection::connect_async(std::wstring endpoint, t::deadline until, const std::stop_token& stop)
{
    return state_->connect_async(std::move(endpoint), until, stop);
}

connect_result
connection::connect(std::wstring endpoint, t::deadline until, const std::stop_token& stop)
{
    return connect_async(std::move(endpoint), until, stop).get();
}

std::future<call_result>
connection::call_async(p::operation operation, t::deadline until, const std::stop_token& stop)
{
    return state_->call_async(std::move(operation), until, stop);
}

call_result connection::call(p::operation operation, t::deadline until, const std::stop_token& stop)
{
    return call_async(std::move(operation), until, stop).get();
}

void connection::close() noexcept
{
    state_->close();
}
} // namespace pipeforge::client
