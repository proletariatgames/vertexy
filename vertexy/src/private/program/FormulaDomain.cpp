// Copyright Proletariat, Inc. All Rights Reserved.
#include "program/FormulaDomain.h"
#include "program/ProgramDSL.h"
#include "program/ProgramAST.h"

Vertexy::FormulaDomainValue::FormulaDomainValue(const wchar_t* name, function<const FormulaDomainDescriptor*()>&& descriptorFn, int valueIndex)
    : m_name(name)
    , m_descriptorFn(move(descriptorFn))
    , m_valueIndex(valueIndex)
{
}

Vertexy::ValueSet Vertexy::FormulaDomainValue::toValues() const
{
    auto descriptor = m_descriptorFn();
    ValueSet values(descriptor->getDomainSize(), false);
    if (m_valueIndex >= 0 && m_valueIndex < descriptor->getDomainSize())
    {
        values[m_valueIndex] = true;
    }
    return values;
}

Vertexy::FormulaDomainValueArray::FormulaDomainValueArray(const wchar_t* name, function<const FormulaDomainDescriptor*()>&& descriptorFn, int valueIndex, int arraySize)
    : m_name(name)
    , m_descriptorFn(move(descriptorFn))
    , m_firstValueIndex(valueIndex)
    , m_numValues(arraySize)
{
}

Vertexy::ValueSet Vertexy::FormulaDomainValueArray::toValues() const
{
    auto descriptor = m_descriptorFn();
    ValueSet values(descriptor->getDomainSize(), false);
    for (int i = 0; i < m_numValues; ++i)
    {
        values[m_firstValueIndex+i] = true;        
    }
    return values;
}

Vertexy::ValueSet Vertexy::FormulaDomainValueArray::toValues(int index) const
{
    auto descriptor = m_descriptorFn();
    ValueSet values(descriptor->getDomainSize(), false);
    if (index >= 0 && index < m_numValues)
    {
        values[m_firstValueIndex + index] = true;
    }
    return values;
}

Vertexy::detail::ProgramDomainTerm Vertexy::FormulaDomainValueArray::operator[](detail::ProgramBodyTerm&& subscriptTerm) const
{
    return detail::ProgramDomainTerm(make_unique<SubscriptDomainTerm>(*this, move(subscriptTerm.term)));
}

Vertexy::detail::ExplicitDomainArgument Vertexy::FormulaDomainValueArray::operator[](int index) const
{
    return detail::ExplicitDomainArgument(FormulaDomainValue(m_name, function{m_descriptorFn}, m_firstValueIndex + index));
}
