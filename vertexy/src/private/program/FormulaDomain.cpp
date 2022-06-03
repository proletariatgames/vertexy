// Copyright Proletariat, Inc. All Rights Reserved.
#include "program/FormulaDomain.h"
#include "program/ProgramDSL.h"
#include "program/ProgramAST.h"

Vertexy::FormulaDomainValue::FormulaDomainValue(const wchar_t* name, const FormulaDomainDescriptor* descriptor, int valueIndex)
    : m_name(name)
    , m_descriptor(descriptor)
    , m_valueIndex(valueIndex)
{
}

Vertexy::ValueSet Vertexy::FormulaDomainValue::toValues() const
{
    ValueSet values(m_descriptor->getDomainSize(), false);
    values[m_valueIndex] = true;
    return values;
}

Vertexy::FormulaDomainValueArray::FormulaDomainValueArray(const wchar_t* name, const FormulaDomainDescriptor* descriptor, int valueIndex, int arraySize)
    : m_name(name)
    , m_descriptor(descriptor)
    , m_firstValueIndex(valueIndex)
    , m_numValues(arraySize)
{
}

Vertexy::ValueSet Vertexy::FormulaDomainValueArray::toValues() const
{
    ValueSet values(m_descriptor->getDomainSize(), false);
    for (int i = 0; i < m_numValues; ++i)
    {
        values[m_firstValueIndex+i] = true;        
    }
    return values;
}

Vertexy::ValueSet Vertexy::FormulaDomainValueArray::toValues(int index) const
{
    vxy_assert(index >= 0 && index < m_numValues);
    ValueSet values(m_descriptor->getDomainSize(), false);
    values[m_firstValueIndex + index] = true;
    return values;
}

Vertexy::detail::ProgramDomainTerm Vertexy::FormulaDomainValueArray::operator[](detail::ProgramBodyTerm&& subscriptTerm) const
{
    return detail::ProgramDomainTerm(make_unique<SubscriptDomainTerm>(*this, move(subscriptTerm.term)));
}

Vertexy::detail::ExplicitDomainArgument Vertexy::FormulaDomainValueArray::operator[](int index) const
{
    vxy_assert(index >= 0 && index < m_numValues);
    return detail::ExplicitDomainArgument(FormulaDomainValue(m_name, m_descriptor, m_firstValueIndex + index));
}
