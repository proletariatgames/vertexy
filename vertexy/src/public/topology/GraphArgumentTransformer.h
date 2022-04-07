// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ITopology.h"
#include "SignedClause.h"

namespace Vertexy
{

template <typename T, typename R>
struct TransformedGraphArgument
{
	TransformedGraphArgument(bool isValid, const T& value = T{}, shared_ptr<const IGraphRelation<R>> relation = nullptr)
		: isValid(isValid)
	  , value(value)
	  , relation(relation)
	{
	}

	bool isValid;
	T value;
	shared_ptr<const IGraphRelation<R>> relation;
};

/**
 * Given a graph instance and vertex index, transforms graph relations into FVarID and FSignedClauses for a given vertex.
 * The return value is a FTransformedGraphArgument, which contains whether the transformation succeeded, the transformed value,
 * and the relation that caused the transform.
 */
struct GraphArgumentTransformer
{
	template <typename VarType>
	static auto transformGraphArgument(int, VarType&& arg)
	{
		return TransformedGraphArgument<VarType, VarType>(true, arg, nullptr);
	}

	template <typename T>
	static auto transformGraphArgument(int vertexIndex, const shared_ptr<IGraphRelation<T>>& arg)
	{
		return transformGraphArgument(vertexIndex, const_shared_pointer_cast<const IGraphRelation<T>>(arg));
	}

	template <typename T>
	static auto transformGraphArgument(int vertexIndex, const shared_ptr<const IGraphRelation<T>>& arg)
	{
		T relatedValue;
		bool success = arg->getRelation(vertexIndex, relatedValue);
		return TransformedGraphArgument(success, relatedValue, arg);
	}

	template <typename T>
	static auto transformGraphArgument(int vertexIndex, const TSignedClause<shared_ptr<const IGraphRelation<T>>>& arg)
	{
		T relatedValue;
		bool success = arg.variable->getRelation(vertexIndex, relatedValue);
		return TransformedGraphArgument(success, TSignedClause<T>(relatedValue, arg.sign, arg.values), arg.variable);
	}
};

} // namespace Vertexy