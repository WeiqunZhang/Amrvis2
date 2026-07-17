#pragma once

#include <array>
#include <cstddef>

namespace amrvis {

struct Int3 {
    std::array<int, 3> values{0, 0, 0};

    [[nodiscard]] constexpr int operator[](std::size_t index) const noexcept
    {
        return values[index];
    }

    constexpr int& operator[](std::size_t index) noexcept { return values[index]; }

    friend constexpr bool operator==(const Int3&, const Int3&) = default;
};

struct Real3 {
    std::array<double, 3> values{0.0, 0.0, 0.0};

    [[nodiscard]] constexpr double operator[](std::size_t index) const noexcept
    {
        return values[index];
    }

    constexpr double& operator[](std::size_t index) noexcept { return values[index]; }

    friend constexpr bool operator==(const Real3&, const Real3&) = default;
};

struct IntBox {
    Int3 lower;
    Int3 upper;
    Int3 centering;

    [[nodiscard]] constexpr bool valid(int dimension) const noexcept
    {
        if (dimension < 1 || dimension > 3) {
            return false;
        }
        for (int axis = 0; axis < dimension; ++axis) {
            const auto i = static_cast<std::size_t>(axis);
            if (lower[i] > upper[i]) {
                return false;
            }
        }
        return true;
    }

    friend constexpr bool operator==(const IntBox&, const IntBox&) = default;
};

struct RealBox {
    Real3 lower;
    Real3 upper;

    [[nodiscard]] constexpr bool valid(int dimension) const noexcept
    {
        if (dimension < 1 || dimension > 3) {
            return false;
        }
        for (int axis = 0; axis < dimension; ++axis) {
            const auto i = static_cast<std::size_t>(axis);
            if (!(lower[i] < upper[i])) {
                return false;
            }
        }
        return true;
    }

    friend constexpr bool operator==(const RealBox&, const RealBox&) = default;
};

} // namespace amrvis

