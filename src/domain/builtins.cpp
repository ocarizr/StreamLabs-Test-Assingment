#include "pipeforge/domain.hpp"
#include "pipeforge/validation.hpp"

#include <limits>
#include <utility>

namespace pipeforge::domain {
namespace {
namespace p = protocol;

const p::value* single_argument(const std::span<const p::named_value>& arguments,
                                std::string_view name)
{
    if (arguments.size() != 1 || arguments.front().name != name ||
        !p::valid_value(arguments.front().data)) {
        return nullptr;
    }

    return &arguments.front().data;
}

class counter_object final : public object {
  public:
    explicit counter_object(std::int64_t initial) : value_(initial) {}

    [[nodiscard]] std::unique_ptr<object> clone() const override
    {
        return std::make_unique<counter_object>(*this);
    }

    [[nodiscard]] std::vector<p::named_value> state() const override
    {
        return {{"value", value_}};
    }

    [[nodiscard]] result<p::value> invoke(std::string_view name,
                                          std::span<const p::named_value> arguments) override
    {
        if (name != "increment") {
            return p::make_error(p::error_code::not_found);
        }

        const auto* argument = single_argument(arguments, "delta");
        const auto* delta = argument ? std::get_if<std::int64_t>(argument) : nullptr;
        if (!delta) {
            return p::make_error(p::error_code::invalid_request);
        }

        // Check the subtraction before evaluating the addition: neither bound
        // expression overflows, including INT64_MIN as the supplied delta.
        if ((*delta > 0 && value_ > std::numeric_limits<std::int64_t>::max() - *delta) ||
            (*delta < 0 && value_ < std::numeric_limits<std::int64_t>::min() - *delta)) {
            return p::make_error(p::error_code::numeric_overflow);
        }

        value_ += *delta;
        return p::value{value_};
    }

  private:
    std::int64_t value_;
};

class text_buffer_object final : public object {
  public:
    explicit text_buffer_object(std::string initial) : text_(std::move(initial)) {}

    [[nodiscard]] std::unique_ptr<object> clone() const override
    {
        return std::make_unique<text_buffer_object>(*this);
    }

    [[nodiscard]] std::vector<p::named_value> state() const override
    {
        return {{"text", text_}};
    }

    [[nodiscard]] result<p::value> invoke(std::string_view name,
                                          std::span<const p::named_value> arguments) override
    {
        if (name != "append") {
            return p::make_error(p::error_code::not_found);
        }

        const auto* argument = single_argument(arguments, "suffix");
        const auto* suffix = argument ? std::get_if<std::string>(argument) : nullptr;
        if (!suffix) {
            return p::make_error(p::error_code::invalid_request);
        }

        if (suffix->size() > p::max_string_bytes - text_.size()) {
            return p::make_error(p::error_code::quota_exceeded);
        }

        text_ += *suffix;
        return p::value{text_};
    }

  private:
    std::string text_;
};
} // namespace

class_definition counter_definition()
{
    return {{"Counter",
             {{"value", p::value_kind::integer, true}},
             {{"increment", {{"delta", p::value_kind::integer}}, p::value_kind::integer}},
             {{"initial", p::value_kind::integer}}},
            [](std::span<const p::named_value> arguments) -> result<std::unique_ptr<object>> {
                const auto* argument = single_argument(arguments, "initial");
                const auto* initial = argument ? std::get_if<std::int64_t>(argument) : nullptr;
                if (!initial) {
                    return p::make_error(p::error_code::invalid_request);
                }

                return std::unique_ptr<object>{std::make_unique<counter_object>(*initial)};
            }};
}

class_definition text_buffer_definition()
{
    return {{"TextBuffer",
             {{"text", p::value_kind::text, true}},
             {{"append", {{"suffix", p::value_kind::text}}, p::value_kind::text}},
             {{"initial", p::value_kind::text}}},
            [](std::span<const p::named_value> arguments) -> result<std::unique_ptr<object>> {
                const auto* argument = single_argument(arguments, "initial");
                const auto* initial = argument ? std::get_if<std::string>(argument) : nullptr;
                if (!initial) {
                    return p::make_error(p::error_code::invalid_request);
                }

                return std::unique_ptr<object>{std::make_unique<text_buffer_object>(*initial)};
            }};
}
} // namespace pipeforge::domain
