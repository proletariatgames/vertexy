// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"

namespace Vertexy
{
using namespace eastl;

using EventListenerHandle = uint32_t;
constexpr uint32_t INVALID_EVENT_LISTENER_HANDLE = 0xFFFFFFFF;

template <typename Fn>
class TEventDispatcher
{
	struct EventNode
	{
		EventNode(uint32_t handle, eastl::function<Fn> callback)
			: callback(callback)
		  , handle(handle)
		{
		}

		eastl::function<Fn> callback;
		uint32_t handle;
	};

public:
	TEventDispatcher()
	{
	}

	EventListenerHandle add(const eastl::function<Fn>& callback)
	{
		if (!m_broadcasting)
		{
			compact();
		}

		m_handlers.emplace_back(m_nextHandle++, callback);
		return m_handlers.back().handle;
	}

	void remove(EventListenerHandle handle)
	{
		auto it = find_if(m_handlers.begin(), m_handlers.end(), [&](auto& node) { return node.handle == handle; });
		if (it != m_handlers.end())
		{
			it->callback = nullptr;
			if (!m_broadcasting)
			{
				compact();
			}
		}
	}

	template <typename... Args>
	void broadcast(Args&&... args) const
	{
		TValueGuard<bool> guard(m_broadcasting, true);
		for (auto it = m_handlers.begin(); it != m_handlers.end(); ++it)
		{
			if (it->callback != nullptr)
			{
				invoke(it->callback, args...);
			}
		}
	}

	bool isBound() const
	{
		return !m_handlers.empty();
	}

protected:
	void compact()
	{
		vxy_sanity(!m_broadcasting);
		for (auto it = m_handlers.rbegin(), itEnd = m_handlers.rend(); it != itEnd; ++it)
		{
			if (it->callback == nullptr)
			{
				m_handlers.erase_unsorted(it);
			}
		}
	}

	vector<EventNode> m_handlers;
	uint32_t m_nextHandle = 0;
	mutable bool m_broadcasting = false;
};

} // namespace Vertexy