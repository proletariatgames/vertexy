// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "ConstraintTypes.h"
#include "variable/SolverVariableDomain.h"

namespace Vertexy
{

namespace detail { class ProgramDomainTerm; class ExplicitDomainArgument; class ProgramBodyTerm; }

class FormulaDomainDescriptor;

class FormulaDomainValue
{
public:
    FormulaDomainValue(const wchar_t* name, function<const FormulaDomainDescriptor*()>&& descriptorFn, int valueIndex);
    
    const FormulaDomainDescriptor* getDescriptor() const { return m_descriptorFn(); }
    ValueSet toValues() const;
    const wchar_t* getName() const { return m_name; }
    int getValueIndex() const { return m_valueIndex; }
    
protected:
    const wchar_t* m_name;
    function<const FormulaDomainDescriptor*()> m_descriptorFn;
    int m_valueIndex;
};

class FormulaDomainValueArray
{
public:
    FormulaDomainValueArray(const wchar_t* name, function<const FormulaDomainDescriptor*()>&& descriptorFn, int valueIndex, int arraySize);

    const FormulaDomainDescriptor* getDescriptor() const { return m_descriptorFn(); }
    ValueSet toValues() const;
    ValueSet toValues(int index) const;

    detail::ProgramDomainTerm operator[](detail::ProgramBodyTerm&& subscriptTerm) const;
    detail::ExplicitDomainArgument operator[](int index) const;

    const wchar_t* getName() const { return m_name; }
    int getFirstValueIndex() const { return m_firstValueIndex; }
    int getNumValues() const { return m_numValues; }
    
protected:
    const wchar_t* m_name;
    function<const FormulaDomainDescriptor*()> m_descriptorFn;
    int m_firstValueIndex;
    int m_numValues;
};

class FormulaDomainDescriptor
{
public:
    FormulaDomainDescriptor(const wchar_t* name)
        : m_name(name)
        , m_domainSize(0)
    {
    }

    const wchar_t* getName() const { return m_name; }
    int getDomainSize() const { return m_domainSize; }

    SolverVariableDomain getSolverDomain() const { return SolverVariableDomain(0, m_domainSize-1); }
    
protected:
    FormulaDomainValue addValue(const wchar_t* valueName, function<const FormulaDomainDescriptor*()>&& desc)
    {
        FormulaDomainValue val(valueName, move(desc), m_domainSize);
        ++m_domainSize;
        return val;
    }
    FormulaDomainValueArray addArray(const wchar_t* arrayName, int arraySize, function<const FormulaDomainDescriptor*()>&& desc)
    {
        FormulaDomainValueArray val(arrayName, move(desc), m_domainSize, arraySize);
        m_domainSize += arraySize;
        return val;
    }
    
    const wchar_t* m_name;
    int m_domainSize;
};

class DefaultFormulaDomainDescriptor : public FormulaDomainDescriptor
{
public:
    DefaultFormulaDomainDescriptor() : FormulaDomainDescriptor(TEXT("default"))
    {
        m_domainSize = 1;
    }
};

}
