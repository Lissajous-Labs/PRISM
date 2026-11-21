#pragma once

#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::nonblocking)
#define PRISM_NONBLOCKING [[clang::nonblocking]]
#endif
#endif
#ifndef PRISM_NONBLOCKING
#define PRISM_NONBLOCKING
#endif
