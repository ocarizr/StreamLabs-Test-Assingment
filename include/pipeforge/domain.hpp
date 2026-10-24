/** @file
 * Trusted class registration and session-owned server repositories.
 */
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
/** Expected domain failures are structured remote errors. */
template <typename T> using result = std::variant<T, protocol::remote_error>;

/** Hard ceiling for retained values per session. */
inline constexpr std::size_t max_stored_values = 100;
/** Hard ceiling for retained objects per session. */
inline constexpr std::size_t max_stored_objects = 100;
/** Hard logical retained-state budget per session. */
inline constexpr std::size_t max_session_bytes = std::size_t{8} * 1024 * 1024;
/** Maximum logical state bytes allowed for one registered object. */
inline constexpr std::size_t max_object_state_bytes = protocol::max_payload_bytes / 2;
/** Logical per-record overhead included in session accounting. */
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
    /** Return a nonnull deep copy, with no shared mutable object state. */
    [[nodiscard]] virtual std::unique_ptr<object> clone() const = 0;
    /** Return the exact registered attribute set and types as owning values. */
    [[nodiscard]] virtual std::vector<protocol::named_value> state() const = 0;
    /** Apply a bounded method to this candidate; expected failures return data. */
    [[nodiscard]] virtual result<protocol::value>
    invoke(std::string_view name, std::span<const protocol::named_value> arguments) = 0;

  protected:
    object() = default;
    /** Derived adapters may copy their own state while implementing clone(). */
    object(const object&) = default;
    /** Derived adapters retain ordinary value assignment for their owned state. */
    object& operator=(const object&) = default;
    /** Derived adapters may transfer their owned state. */
    object(object&&) = default;
    /** Derived adapters may replace their owned state by moving. */
    object& operator=(object&&) = default;
};

/** Trusted factory; returns an owned instance or expected error, retaining no arguments. */
using object_factory =
    std::function<result<std::unique_ptr<object>>(std::span<const protocol::named_value>)>;

/** Metadata is copied at registration. Factories must not retain the argument span. */
struct class_definition {
    protocol::class_description description; ///< Owning validated public metadata.
    object_factory create; ///< Trusted factory returning an independent owned object.
    std::size_t max_state_bytes =
        max_object_state_bytes; ///< Declared logical state ceiling; cannot exceed the hard bound.
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
    /** Begin a nonwrapping sequence; zero starts exhausted, maximum is issued once. */
    explicit identifier_source(std::uint64_t first = 1);
    identifier_source(const identifier_source&) = delete;
    identifier_source& operator=(const identifier_source&) = delete;
    identifier_source(identifier_source&&) = delete;
    identifier_source& operator=(identifier_source&&) = delete;
    ~identifier_source() = default;
    /** Return the next never-reused identity, or empty on exhaustion. */
    [[nodiscard]] std::optional<std::uint64_t> next();

  private:
    std::uint64_t next_;
};

/** Limits may tighten the hard ceilings above; larger settings are clamped. Zero denies storage. */
struct session_limits {
    std::size_t values =
        max_stored_values; ///< Requested retained-value ceiling, clamped to the hard maximum.
    std::size_t objects =
        max_stored_objects; ///< Requested retained-object ceiling, clamped to the hard maximum.
    std::size_t bytes =
        max_session_bytes; ///< Requested shared retained-state budget, clamped to the hard maximum.
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
    /** Borrow the frozen registry and shared ID source, clamping requested quotas. */
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
