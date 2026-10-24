// Hand-offs to the operating system shell.
#pragma once

#include <string>

namespace dd::platform {

// Opens a URL (or a file path) with the system default handler.
bool openExternal(const std::string& target, std::string& error);

// Monotonic seconds since start, for status-message timeouts and animation.
double nowSeconds();

} // namespace dd::platform
