#include "util/Logging.h"

#include <Windows.h>
#include <EASTL/string.h>

using namespace eastl;
namespace csolver
{

void _csolver_log(wchar_t* msg, ...)
{
	static bool bInitialized = false;
	if (!bInitialized)
	{
		// necessary for wide string output to work. (see OutputDebugStringW documentation)
		DEBUG_EVENT event;
		WaitForDebugEventEx(&event, 0);
		bInitialized = true;
	}

	va_list args;
	va_start(args, msg);

	wstring s;
	s.append_sprintf_va_list(msg, args);
	::OutputDebugStringW(s.c_str());
	wprintf(s.c_str());
}

} // namespace csolver