#include "pipeforge/domain.hpp"

#include "pipeforge/validation.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace pipeforge::domain {
namespace {
namespace p = protocol;
constexpr std::size_t page_byte_budget = p::max_payload_bytes / 2;

template <typename T> p::outcome widen(result<T> result)
{
    return std::visit(
        [](auto&& item) -> p::outcome {
            return std::forward<decltype(item)>(item);
        },
        std::move(result));
}

std::size_t state_charge(std::span<const p::named_value> state)
{
    std::size_t charge = 0;
    for (const auto& field : state) {
        charge += record_charge + field.name.size() + p::value_bytes(field.data);
    }

    return charge;
}

bool state_matches(const class_definition& definition, std::span<const p::named_value> state)
{
    if (state.size() != definition.description.attributes.size() || !p::valid_named_values(state)) {
        return false;
    }

    for (const auto& attribute : definition.description.attributes) {
        const auto found = std::ranges::find(state, attribute.name, &p::named_value::name);
        if (found == state.end() || p::kind_of(found->data) != attribute.kind) {
            return false;
        }
    }

    return state_charge(state) <= definition.max_state_bytes;
}
} // namespace

std::optional<p::remote_error> class_registry::add(class_definition definition)
{
    if (!p::valid_description(definition.description) || !definition.create ||
        definition.max_state_bytes == 0 || definition.max_state_bytes > max_object_state_bytes ||
        find(definition.description.name)) {
        return p::make_error(p::error_code::invalid_request);
    }

    if (definitions_.size() >= p::max_entries) {
        return p::make_error(p::error_code::quota_exceeded);
    }

    const auto insertion = std::ranges::lower_bound(definitions_,
                                                    definition.description.name,
                                                    std::less<>{},
                                                    [](const auto& entry) -> const std::string& {
                                                        return entry.description.name;
                                                    });
    definitions_.insert(insertion, std::move(definition));
    return std::nullopt;
}

const class_definition* class_registry::find(std::string_view name) const
{
    const auto found =
        std::ranges::find(definitions_, name, [](const auto& entry) -> const std::string& {
            return entry.description.name;
        });
    return found == definitions_.end() ? nullptr : &*found;
}

result<p::classes_page> class_registry::list(const p::list_classes& query) const
{
    if (!p::valid_name(query.after, true) || !p::valid_page_size(query.limit)) {
        return p::make_error(p::error_code::invalid_request);
    }

    p::classes_page page;
    auto item = std::ranges::upper_bound(
        definitions_, query.after, std::less<>{}, [](const auto& entry) -> const std::string& {
            return entry.description.name;
        });
    while (item != definitions_.end() && page.names.size() < query.limit) {
        page.names.push_back(item->description.name);
        ++item;
    }

    if (item != definitions_.end()) {
        page.next = page.names.back();
    }

    return page;
}

identifier_source::identifier_source(std::uint64_t first) : next_(first) {}

std::optional<std::uint64_t> identifier_source::next()
{
    if (next_ == 0) {
        return std::nullopt;
    }

    const auto id = next_;
    next_ = id == std::numeric_limits<std::uint64_t>::max() ? 0 : id + 1;
    return id;
}

session_store::session_store(const class_registry& registry,
                             identifier_source& identifiers,
                             session_limits limits)
    : registry_(registry), identifiers_(identifiers),
      limits_{std::min(limits.values, max_stored_values),
              std::min(limits.objects, max_stored_objects),
              std::min(limits.bytes, max_session_bytes)}
{
}

p::outcome session_store::execute(const p::operation& operation)
{
    if (closed_) {
        return p::make_error(p::error_code::access_denied);
    }

    return std::visit(
        [this](const auto& query) {
            return apply(query);
        },
        operation);
}

void session_store::close()
{
    objects_.clear();
    values_.clear();
    used_ = 0;
    closed_ = true;
}

std::size_t session_store::accounted_bytes() const
{
    return used_;
}

p::outcome session_store::apply(const p::list_classes& query) const
{
    return widen(registry_.list(query));
}

p::outcome session_store::apply(const p::describe_class& query) const
{
    if (!p::valid_name(query.name)) {
        return p::make_error(p::error_code::invalid_request);
    }

    const auto* definition = registry_.find(query.name);
    if (!definition) {
        return p::make_error(p::error_code::not_found);
    }

    return definition->description;
}

p::outcome session_store::apply(const p::put_value& query)
{
    if (!p::valid_value(query.data)) {
        return p::make_error(p::error_code::invalid_request);
    }

    const auto charge = record_charge + p::value_bytes(query.data);
    if (values_.size() >= limits_.values || charge > limits_.bytes - used_) {
        return p::make_error(p::error_code::quota_exceeded);
    }

    const auto id = identifiers_.next();
    if (!id) {
        return p::make_error(p::error_code::quota_exceeded);
    }

    values_.emplace(*id, query.data);
    used_ += charge;
    return p::value_record{*id, query.data};
}

p::outcome session_store::apply(const p::get_value& query) const
{
    if (query.id == 0) {
        return p::make_error(p::error_code::invalid_request);
    }

    const auto found = values_.find(query.id);
    return found == values_.end() ? p::outcome{p::make_error(p::error_code::not_found)}
                                  : p::outcome{p::value_record{found->first, found->second}};
}

p::outcome session_store::apply(const p::list_values& query) const
{
    if (!p::valid_page_size(query.limit)) {
        return p::make_error(p::error_code::invalid_request);
    }

    p::values_page page;
    std::size_t bytes = 0;
    auto item = values_.upper_bound(query.after);
    while (item != values_.end() && page.values.size() < query.limit) {
        const auto charge = record_charge + p::value_bytes(item->second);
        if (charge > page_byte_budget - bytes) {
            break;
        }

        bytes += charge;
        page.values.push_back({item->first, item->second});
        ++item;
    }

    if (item != values_.end()) {
        page.next = page.values.back().id;
    }

    return page;
}

p::outcome session_store::apply(const p::create_object& query)
{
    if (!p::valid_name(query.class_name)) {
        return p::make_error(p::error_code::invalid_request);
    }

    const auto* definition = registry_.find(query.class_name);
    if (!definition) {
        return p::make_error(p::error_code::not_found);
    }

    if (!p::valid_arguments(query.arguments, definition->description.constructor_parameters)) {
        return p::make_error(p::error_code::invalid_request);
    }

    if (objects_.size() >= limits_.objects) {
        return p::make_error(p::error_code::quota_exceeded);
    }

    auto created = definition->create(query.arguments);
    if (const auto* error = std::get_if<p::remote_error>(&created)) {
        return p::make_error(error->code);
    }

    auto instance = std::move(std::get<std::unique_ptr<object>>(created));
    if (!instance) {
        return p::make_error(p::error_code::internal_failure);
    }

    auto state = instance->state();
    if (!state_matches(*definition, state)) {
        return p::make_error(p::error_code::internal_failure);
    }

    const auto charge = record_charge + query.class_name.size() + state_charge(state);
    if (charge > limits_.bytes - used_) {
        return p::make_error(p::error_code::quota_exceeded);
    }

    const auto id = identifiers_.next();
    if (!id) {
        return p::make_error(p::error_code::quota_exceeded);
    }

    objects_.emplace(*id, stored_object{definition, std::move(instance), charge});
    used_ += charge;
    return p::object_snapshot{*id, query.class_name, std::move(state)};
}

p::outcome session_store::apply(const p::get_object& query) const
{
    if (query.id == 0) {
        return p::make_error(p::error_code::invalid_request);
    }

    const auto found = objects_.find(query.id);
    if (found == objects_.end()) {
        return p::make_error(p::error_code::not_found);
    }

    auto state = found->second.instance->state();
    if (!state_matches(*found->second.definition, state)) {
        return p::make_error(p::error_code::internal_failure);
    }

    return p::object_snapshot{
        query.id, found->second.definition->description.name, std::move(state)};
}

p::outcome session_store::apply(const p::release_object& query)
{
    if (query.id == 0) {
        return p::make_error(p::error_code::invalid_request);
    }

    const auto found = objects_.find(query.id);
    if (found == objects_.end()) {
        return p::make_error(p::error_code::not_found);
    }

    used_ -= found->second.charge;
    objects_.erase(found);
    return p::released{};
}

p::outcome session_store::apply(const p::get_attribute& query) const
{
    if (!p::valid_name(query.name)) {
        return p::make_error(p::error_code::invalid_request);
    }

    auto snapshot = apply(p::get_object{query.id});
    if (const auto* error = std::get_if<p::remote_error>(&snapshot)) {
        return *error;
    }

    const auto& state = std::get<p::object_snapshot>(snapshot).state;
    const auto found = std::ranges::find(state, query.name, &p::named_value::name);
    return found == state.end() ? p::outcome{p::make_error(p::error_code::not_found)}
                                : p::outcome{found->data};
}

p::outcome session_store::apply(const p::invoke_method& query)
{
    if (query.id == 0 || !p::valid_name(query.name)) {
        return p::make_error(p::error_code::invalid_request);
    }

    const auto found = objects_.find(query.id);
    if (found == objects_.end()) {
        return p::make_error(p::error_code::not_found);
    }

    auto& entry = found->second;
    const auto& description = entry.definition->description;
    const auto method = std::ranges::find(description.methods, query.name, &p::method::name);
    if (method == description.methods.end()) {
        return p::make_error(p::error_code::not_found);
    }

    if (!p::valid_arguments(query.arguments, method->parameters)) {
        return p::make_error(p::error_code::invalid_request);
    }

    // Execute against isolated state. Failed calls, invalid plugin output and
    // quota violations discard the candidate rather than partially mutating it.
    auto candidate = entry.instance->clone();
    if (!candidate) {
        return p::make_error(p::error_code::internal_failure);
    }

    auto invoked = candidate->invoke(query.name, query.arguments);
    if (const auto* error = std::get_if<p::remote_error>(&invoked)) {
        return p::make_error(error->code);
    }

    auto value = std::move(std::get<p::value>(invoked));
    auto state = candidate->state();
    if (!p::valid_value(value) || p::kind_of(value) != method->result_kind ||
        !state_matches(*entry.definition, state)) {
        return p::make_error(p::error_code::internal_failure);
    }

    const auto charge = record_charge + description.name.size() + state_charge(state);
    const auto retained = used_ - entry.charge;
    if (charge > limits_.bytes - retained) {
        return p::make_error(p::error_code::quota_exceeded);
    }

    entry.instance = std::move(candidate);
    entry.charge = charge;
    used_ = retained + charge;
    return p::outcome{std::move(value)};
}
} // namespace pipeforge::domain
