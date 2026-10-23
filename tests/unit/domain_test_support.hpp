#ifndef PIPEFORGE_DOMAIN_TEST_SUPPORT_HPP
#define PIPEFORGE_DOMAIN_TEST_SUPPORT_HPP

#include "pipeforge/domain.hpp"

#include <gtest/gtest.h>

namespace pipeforge::test_support {
inline domain::class_registry registered_classes()
{
    domain::class_registry registry;
    EXPECT_FALSE(registry.add(domain::counter_definition()));
    EXPECT_FALSE(registry.add(domain::text_buffer_definition()));
    return registry;
}

template <typename T> T expect_result(protocol::outcome output)
{
    const auto* result = std::get_if<T>(&output);
    EXPECT_NE(result, nullptr);
    return result ? *result : T{};
}
} // namespace pipeforge::test_support
#endif // PIPEFORGE_DOMAIN_TEST_SUPPORT_HPP
