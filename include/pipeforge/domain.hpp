#ifndef PIPEFORGE_DOMAIN_HPP
#define PIPEFORGE_DOMAIN_HPP

#include "pipeforge/contracts.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace pipeforge::domain {
template <typename T> using result = std::variant<T, protocol::remote_error>;

inline constexpr std::size_t max_stored_values = 100;
inline constexpr std::size_t max_stored_objects = 100;
inline constexpr std::size_t max_session_bytes = std::size_t{8} * 1024 * 1024;
inline constexpr std::size_t max_object_state_bytes = protocol::max_payload_bytes / 2;
inline constexpr std::size_t record_charge = 128;

/**
 * Trusted server-side behavior. Implementations must be bounded, do no blocking
 * I/O, and own their state. clone() returns an independent deep copy, never null.
 * state() reports exactly the registered attributes. The store mutates a clone
 * and publishes it only after validating result, state and quota. Unexpected
 * exceptions (including allocation failures) propagate; expected errors are data.
 */
class object {
  public:
    virtual ~object() = default;
    [[nodiscard]] virtual std::unique_ptr<object> clone() const = 0;
    [[nodiscard]] virtual std::vector<protocol::named_value> state() const = 0;
    [[nodiscard]] virtual result<protocol::value>
    invoke(std::string_view name, std::span<const protocol::named_value> arguments) = 0;

  protected:
    object() = default;
    object(const object&) = default;
    object& operator=(const object&) = default;
    object(object&&) = default;
    object& operator=(object&&) = default;
};

using object_factory =
    std::function<result<std::unique_ptr<object>>(std::span<const protocol::named_value>)>;

/** Metadata is copied at registration. Factories must not retain the argument span. */
struct class_definition {
    protocol::class_description description;
    object_factory create;
    std::size_t max_state_bytes = max_object_state_bytes;
};

/**
 * Startup-only trusted registry, bounded to protocol::max_entries classes.
 * Register before creating stores; do not mutate it while stores exist.
 * All domain objects are single-threaded; the host must serialize access.
 */
class class_registry {
  public:
    /** Reject invalid metadata/factories, duplicate names and registry exhaustion. */
    [[nodiscard]] std::optional<protocol::remote_error> add(class_definition definition);
    /** Observer valid until the next registration, registry move or destruction. */
    [[nodiscard]] const class_definition* find(std::string_view name) const;
    /** Sorted exclusive-key pagination; an empty next cursor marks the last page. */
    [[nodiscard]] result<protocol::classes_page> list(const protocol::list_classes& query) const;

  private:
    // At most 64 sorted startup entries; vector moves do not allocate tree sentinels.
    std::vector<class_definition> definitions_;
};

/**
 * Server-lifetime monotonic ID source shared by every session and repository.
 * IDs are never reused, are not authentication tokens, and exhaust without wrap.
 * A nondefault first ID supports deterministic exhaustion testing. Not thread-safe.
 */
class identifier_source {
  public:
    explicit identifier_source(std::uint64_t first = 1);
    identifier_source(const identifier_source&) = delete;
    identifier_source& operator=(const identifier_source&) = delete;
    identifier_source(identifier_source&&) = delete;
    identifier_source& operator=(identifier_source&&) = delete;
    ~identifier_source() = default;
    [[nodiscard]] std::optional<std::uint64_t> next();

  private:
    std::uint64_t next_;
};

/** Limits may tighten the hard ceilings above; larger settings are clamped. Zero denies storage. */
struct session_limits {
    std::size_t values = max_stored_values;
    std::size_t objects = max_stored_objects;
    std::size_t bytes = max_session_bytes;
};

/**
 * Session-owned repositories with a shared logical byte budget. Registry and ID
 * source must outlive the store. Copies/moves are forbidden. No I/O or internal
 * locking: callers serialize every operation, including close(). Replies own
 * their data. Status failures preserve stored state; close/destruction releases
 * all instances. Accounting bounds retained contract data, not exact heap usage.
 */
class session_store {
  public:
    session_store(const class_registry& registry,
                  identifier_source& identifiers,
                  session_limits limits = {});
    session_store(const session_store&) = delete;
    session_store& operator=(const session_store&) = delete;
    session_store(session_store&&) = delete;
    session_store& operator=(session_store&&) = delete;
    ~session_store() = default;

    /** Execute one typed operation; unknown IDs reveal no other session's state. */
    [[nodiscard]] protocol::outcome execute(const protocol::operation& operation);
    /** Idempotently destroy session state. Subsequent operations return access_denied. */
    void close();
    /** Logical retained bytes, including record/name/value charges; zero after close. */
    [[nodiscard]] std::size_t accounted_bytes() const;

  private:
    struct stored_object {
        const class_definition* definition;
        std::unique_ptr<object> instance;
        std::size_t charge;
    };

    [[nodiscard]] protocol::outcome apply(const protocol::list_classes& query) const;
    [[nodiscard]] protocol::outcome apply(const protocol::describe_class& query) const;
    [[nodiscard]] protocol::outcome apply(const protocol::put_value& query);
    [[nodiscard]] protocol::outcome apply(const protocol::get_value& query) const;
    [[nodiscard]] protocol::outcome apply(const protocol::list_values& query) const;
    [[nodiscard]] protocol::outcome apply(const protocol::create_object& query);
    [[nodiscard]] protocol::outcome apply(const protocol::get_object& query) const;
    [[nodiscard]] protocol::outcome apply(const protocol::release_object& query);
    [[nodiscard]] protocol::outcome apply(const protocol::get_attribute& query) const;
    [[nodiscard]] protocol::outcome apply(const protocol::invoke_method& query);

    const class_registry& registry_;
    identifier_source& identifiers_;
    session_limits limits_;
    std::map<std::uint64_t, protocol::value> values_;
    std::map<std::uint64_t, stored_object> objects_;
    std::size_t used_ = 0;
    bool closed_ = false;
};

/** Built-in definitions for startup registration; Counter requires integer initial. */
[[nodiscard]] class_definition counter_definition();
/** TextBuffer requires text initial and provides bounded append(suffix). */
[[nodiscard]] class_definition text_buffer_definition();
} // namespace pipeforge::domain
#endif // PIPEFORGE_DOMAIN_HPP
