#pragma once

#include <kerfnest/kerfnest.h>

#include <stdexcept>
#include <string>

namespace kerfnest {

/// Thrown inside the solver; Solve turns it into its Refusal result.
struct RefusalError : std::runtime_error {
    Refusal refusal;

    RefusalError(const RefusalCode code, const std::string& message)
        : std::runtime_error(message), refusal{code, message}
    {
    }
};

} // namespace kerfnest
