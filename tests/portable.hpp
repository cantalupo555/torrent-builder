#pragma once

#ifdef _WIN32
#include <process.h>
#define portable_getpid() _getpid()
#else
#include <unistd.h>
#define portable_getpid() getpid()
#endif
