// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

namespace Vertexy
{
	void _vertexy_log(wchar_t* msg, ...);
}

#define VERTEXY_LOG_ACTIVE() (true)
#define VERTEXY_LOG(msg, ...) Vertexy::_vertexy_log(L##msg L"\n", ##__VA_ARGS__)
#define VERTEXY_WARN(msg, ...) Vertexy::_vertexy_log(L"!!!Warning!!! " L##msg L"\n", ##__VA_ARGS__)
