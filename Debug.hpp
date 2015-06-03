
#pragma once

#if defined(_MSC_VER) && defined(_DEBUG)
#define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#define new DEBUG_NEW
#endif
