// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

namespace csolver
{
	void _csolver_log(wchar_t* msg, ...);
}

#define CS_LOG_ACTIVE() (true)
#define CS_LOG(msg, ...) csolver::_csolver_log(L##msg L"\n", ##__VA_ARGS__)
#define CS_WARN(msg, ...) csolver::_csolver_log(L"!!!Warning!!! " L##msg L"\n", ##__VA_ARGS__)
