// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "util/Asserts.h"
#include "util/BitUtils.h"

#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/string.h>

#ifndef TEXT
#define TEXT(s) L ## s
#endif

namespace Vertexy
{

using namespace eastl;

template <typename ALLOCATOR=EASTLAllocatorType, int NUM_INLINE_WORDS = 1, typename WORD_TYPE=uint64_t>
class TValueBitset final
{
private:
	static constexpr int NUM_BITS_PER_WORD = sizeof(WORD_TYPE) * 8;
	static constexpr int MAX_INLINE_BITS = NUM_INLINE_WORDS * NUM_BITS_PER_WORD;

	template <typename T>
	static T allBitsSet();

	template <>
	constexpr static uint64_t allBitsSet() { return ~0ULL; }

	template <>
	constexpr static uint32_t allBitsSet() { return ~0U; }

	template <typename T>
	static uint32_t bitsToWordsShift();

	template <>
	constexpr static uint32_t bitsToWordsShift<uint64_t>() { return 6; }

	template <>
	constexpr static uint32_t bitsToWordsShift<uint32_t>() { return 5; }

public:
	template <typename InWordType>
	struct TConstWordIterator
	{
	public:
		using WordType = typename remove_const<InWordType>::type;

		TConstWordIterator(InWordType* data, int32_t startIndex, int32_t numBits, int32_t lastWord)
			: m_data(data)
			, m_index(startIndex >> bitsToWordsShift<WordType>())
			, m_lastWord(lastWord)
			, m_mask(allBitsSet<WordType>())
			, m_finalMask(m_mask)
		{
			constexpr uint32_t numBitsPerWord = sizeof(WordType) * 8;
			uint32_t shift = numBitsPerWord - (numBits % numBitsPerWord);
			if (shift < numBitsPerWord)
			{
				m_finalMask = allBitsSet<WordType>() >> shift;
			}
			if (lastWord == 0)
			{
				m_mask = m_finalMask;
			}
		}

		explicit operator bool() const
		{
			return m_index <= m_lastWord;
		}

		WordType getWord()
		{
			return m_data[m_index] & m_mask;
		}

		WordType getWord(const WordType* otherData)
		{
			return otherData[m_index] & m_mask;
		}

		int32_t index() const { return m_index; }

		TConstWordIterator& operator++()
		{
			m_index++;
			if (m_index == m_lastWord)
			{
				m_mask = m_finalMask;
			}
			else
			{
				m_mask = allBitsSet<WordType>();
			}
			return *this;
		}

	protected:
		InWordType* m_data;
		int32_t m_index;
		int32_t m_lastWord;
		WordType m_mask;
		WordType m_finalMask;
	};

	template <typename WordType>
	struct TWordIterator : public TConstWordIterator<WordType>
	{
		TWordIterator(WordType* data, int32_t startIndex, int32_t numBits, int32_t lastWord)
			: TConstWordIterator(data, startIndex, numBits, lastWord)
		{
		}

		void setWord(WordType word)
		{
			m_data[m_index] = word & m_mask;
		}
	};

	template <typename WordType>
	class TBitReference
	{
	public:
		TBitReference(WordType& data, WordType mask)
			: m_data(data)
			, m_mask(mask)
		{
		}

		void operator=(bool value)
		{
			if (value)
			{
				m_data |= m_mask;
			}
			else
			{
				m_data &= ~m_mask;
			}
		}

		TBitReference& operator=(const TBitReference& other)
		{
			*this = (bool)other;
			return *this;
		}

		operator bool() const { return (m_data & m_mask) != 0; }

	protected:
		WordType& m_data;
		WordType m_mask;
	};

	template <typename Allocator, int NumInlineWords, typename WordType>
	class TSetBitIterator
	{
	public:
		TSetBitIterator(const TValueBitset<Allocator, NumInlineWords, WordType>& parent, int32_t bitIndex)
			: m_parent(parent)
			, m_index(bitIndex >> bitsToWordsShift<WordType>())
			, m_mask(WordType(1) << (bitIndex & (sizeof(WordType) * 8 - 1)))
			, m_unvisitedMask(allBitsSet<WordType>() << (bitIndex & (sizeof(WordType) * 8 - 1)))
			, m_currentBit(bitIndex)
			, m_baseBitIndex(bitIndex & ~(sizeof(WordType) * 8 - 1))
		{
			scan();
		}

		// for end_set_bits()
		explicit TSetBitIterator(const TValueBitset<Allocator, NumInlineWords, WordType>& parent)
			: m_parent(parent)
			, m_index(parent.size() >> bitsToWordsShift<WordType>())
			, m_mask(0)
			, m_unvisitedMask(0)
			, m_currentBit(parent.size())
			, m_baseBitIndex(0)
		{
		}

		TSetBitIterator& operator++()
		{
			m_unvisitedMask &= ~m_mask;
			scan();

			return *this;
		}

		bool operator==(const TSetBitIterator& rhs) const
		{
			return &m_parent == &rhs.m_parent && m_currentBit == rhs.m_currentBit;
		}

		bool operator!=(const TSetBitIterator& rhs) const
		{
			return !(*this == rhs);
		}

		int32_t operator*() const
		{
			return m_currentBit;
		}

	protected:
		void scan()
		{
			const WordType* data = m_parent.data();
			int32_t size = m_parent.size();
			int32_t lastWordIndex = (size - 1) >> bitsToWordsShift<WordType>();

			WordType remaining = data[m_index] & m_unvisitedMask;
			while (remaining == 0)
			{
				++m_index;
				m_baseBitIndex += sizeof(WordType) * 8;
				if (m_index > lastWordIndex)
				{
					m_currentBit = size;
					return;
				}

				remaining = data[m_index];
				m_unvisitedMask = allBitsSet<WordType>();
			}

			// lop off lowest bit
			WordType newRemaining = remaining & (remaining - 1);
			// mask the lowest bit
			m_mask = remaining ^ newRemaining;

			m_currentBit = m_baseBitIndex + sizeof(WordType) * 8 - 1 - BitUtils::countLeadingZeros(m_mask);
			if (m_currentBit > size)
			{
				m_currentBit = size;
			}
		}

		const TValueBitset<Allocator, NumInlineWords, WordType>& m_parent;
		int32_t m_index;
		WordType m_mask;
		WordType m_unvisitedMask;
		int32_t m_currentBit;
		WordType m_baseBitIndex;
	};

	using reference = TBitReference<WORD_TYPE>;
	using const_reference = bool;
	using this_type = TValueBitset<ALLOCATOR, NUM_INLINE_WORDS, WORD_TYPE>;
	using value_type = bool;
	using allocator_type = ALLOCATOR;
	using element_type = WORD_TYPE;

	TValueBitset()
		: m_numBits(0)
		, m_dataPtr(m_inlineWords)
	{
	}

	TValueBitset(int numBits, bool initialValue = false)
		: m_numBits(0)
		, m_dataPtr(m_inlineWords)
	{
		init(numBits, initialValue);
	}

	~TValueBitset()
	{
		if (m_dataPtr != m_inlineWords)
		{
			delete [] m_dataPtr;
			m_dataPtr = nullptr;
		}
	}

	TValueBitset(const TValueBitset& copy)
		: m_numBits(0)
		, m_dataPtr(m_inlineWords)
	{
		setSize(copy.m_numBits);
		memcpy(m_dataPtr, copy.m_dataPtr, numWordsRequired(m_numBits) * sizeof(WORD_TYPE));
	}

	TValueBitset(TValueBitset&& other) noexcept
		: m_numBits(other.m_numBits)
		, m_dataPtr(m_inlineWords)
	{
		if (other.m_dataPtr == other.m_inlineWords)
		{
			memcpy(m_dataPtr, other.m_dataPtr, NUM_INLINE_WORDS * sizeof(WORD_TYPE));
		}
		else
		{
			m_dataPtr = other.m_dataPtr;
			other.m_dataPtr = other.m_inlineWords;
			other.m_numBits = 0;
		}
	}

	template <int NumInlineWords, typename Allocator>
	TValueBitset(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& copy)
		: m_numBits(0)
		, m_dataPtr(m_inlineWords)
	{
		setSize(copy.m_numBits);
		memcpy(m_dataPtr, copy.m_dataPtr, numWordsRequired(m_numBits) * sizeof(WORD_TYPE));
	}

	TValueBitset& operator=(const TValueBitset& copy)
	{
		setSize(copy.m_numBits);
		memcpy(m_dataPtr, copy.m_dataPtr, numWordsRequired(m_numBits) * sizeof(WORD_TYPE));
		return *this;
	}

	TValueBitset& operator=(TValueBitset&& other) noexcept
	{
		m_numBits = other.m_numBits;
		if (other.m_dataPtr != other.m_inlineWords)
		{
			if (m_dataPtr != m_inlineWords)
			{
				swap(m_dataPtr, other.m_dataPtr);
			}
			else
			{
				m_dataPtr = other.m_dataPtr;
				other.m_dataPtr = other.m_inlineWords;
			}
			other.m_numBits = 0;
		}
		else
		{
			vxy_sanity(m_numBits <= MAX_INLINE_BITS);
			memcpy(m_dataPtr, other.data(), NUM_INLINE_WORDS * sizeof(WORD_TYPE));
		}
		return *this;
	}

	template <int NumInlineWords, typename Allocator>
	bool operator==(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other) const
	{
		if (other.m_numBits != m_numBits)
		{
			return false;
		}

		const WORD_TYPE* otherData = other.data();
		for (auto it = getWordIterator(); it; ++it)
		{
			if (it.getWord() != it.getWord(otherData))
			{
				return false;
			}
		}
		return true;
	}

	template <int NumInlineWords, typename Allocator>
	inline bool operator!=(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other) const
	{
		return !(*this == other);
	}

	void init(int32_t inNumBits, bool bitValue = false)
	{
		const WORD_TYPE defaultWordValue = bitValue ? allBitsSet<WORD_TYPE>() : 0;

		setSize(inNumBits);
		memset(m_dataPtr, defaultWordValue, numWordsRequired(m_numBits) * sizeof(WORD_TYPE));
	}

	inline void clear()
	{
		m_numBits = 0;
	}

	template <typename Allocator, int NumInlineWords>
	void append(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other, int32_t num, int32_t readOffset = 0)
	{
		vxy_assert(num <= other.size());

		int offs = size();
		pad(offs + num, false);
		TSetBitIterator<ALLOCATOR, NUM_INLINE_WORDS, WORD_TYPE> it(other, readOffset);
		int32_t count = 0;
		for (auto itEnd = other.endSetBits(); it != itEnd && count < num; ++it, ++count)
		{
			operator[](offs + (*it - readOffset)) = true;
		}
	}

	void pad(int32_t numBits, bool fillValue)
	{
		if (numBits <= m_numBits)
		{
			return;
		}

		if (numBits > MAX_INLINE_BITS)
		{
			WORD_TYPE* prevDataPtr = m_dataPtr;
			m_dataPtr = new WORD_TYPE[numWordsRequired(numBits)];
			memcpy(m_dataPtr, prevDataPtr, numWordsRequired(m_numBits) * sizeof(WORD_TYPE));

			if (prevDataPtr != m_inlineWords)
			{
				delete [] prevDataPtr;
			}
		}

		int32_t oldNumBits = m_numBits;
		m_numBits = numBits;
		setRange(oldNumBits, numBits, fillValue);
		vxy_sanity(at(numBits-1) == fillValue);
	}

	void setRange(int startBit, int endBit, bool fillValue)
	{
		vxy_sanity(endBit >= startBit);
		vxy_assert(startBit < m_numBits);
		vxy_assert(endBit <= m_numBits);

		if (endBit == startBit)
		{
			return;
		}

		int32_t startWord = startBit >> bitsToWordsShift<WORD_TYPE>();
		int32_t lastWord = (endBit - 1) >> bitsToWordsShift<WORD_TYPE>();
		vxy_sanity(lastWord < numWordsRequired(m_numBits));

		WORD_TYPE startMask = allBitsSet<WORD_TYPE>() << (startBit % NUM_BITS_PER_WORD);
		WORD_TYPE endMask = allBitsSet<WORD_TYPE>() >> (NUM_BITS_PER_WORD - endBit % NUM_BITS_PER_WORD) % NUM_BITS_PER_WORD;

		WORD_TYPE* dataPtr = data() + startWord;
		if (fillValue)
		{
			for (int32_t i = startWord; i <= lastWord; ++i, ++dataPtr)
			{
				WORD_TYPE mask = allBitsSet<WORD_TYPE>();
				if (i == startWord)
				{
					mask = startMask;
					if (i == lastWord)
					{
						mask &= endMask;
					}
				}
				else if (i == lastWord)
				{
					mask = endMask;
				}

				*dataPtr |= mask;
			}
		}
		else
		{
			for (int32_t i = startWord; i <= lastWord; ++i, ++dataPtr)
			{
				WORD_TYPE mask = allBitsSet<WORD_TYPE>();
				if (i == startWord)
				{
					mask = startMask;
					if (i == lastWord)
					{
						mask &= endMask;
					}
				}
				else if (i == lastWord)
				{
					mask = endMask;
				}

				*dataPtr &= ~mask;
			}
		}
	}

	inline int32_t size() const { return m_numBits; }

	inline TBitReference<WORD_TYPE> operator[](int32_t index)
	{
		vxy_assert(index >= 0 && index < m_numBits);
		constexpr WORD_TYPE ONE = 1;
		return TBitReference<WORD_TYPE>(data()[index >> bitsToWordsShift<WORD_TYPE>()], ONE << (index & (NUM_BITS_PER_WORD - 1)));
	}

	inline bool at(int32_t index) const
	{
		vxy_assert(index >= 0 && index < m_numBits);
		constexpr WORD_TYPE ONE = 1;
		return ((data()[index >> bitsToWordsShift<WORD_TYPE>()]) & (ONE << (index & (NUM_BITS_PER_WORD - 1)))) != 0;
	}

	inline bool operator[](int32_t index) const
	{
		return at(index);
	}

	int32_t indexOf(bool bitValue) const
	{
		WORD_TYPE wordValue = bitValue ? 0 : allBitsSet<WORD_TYPE>();

		// Skip words that are entirely set/unset
		int32_t i = 0;
		int32_t numWords = numWordsRequired(m_numBits);
		const WORD_TYPE* dataPtr = data();

		while (i < numWords && dataPtr[i] == wordValue)
		{
			++i;
		}

		if (i < numWords)
		{
			const WORD_TYPE bits = bitValue ? dataPtr[i] : ~(dataPtr[i]);
			const int32_t lowestBit = BitUtils::countTrailingZeros(bits) + (i << bitsToWordsShift<WORD_TYPE>());
			if (lowestBit < m_numBits)
			{
				return lowestBit;
			}
		}

		return -1;
	}

	int32_t lastIndexOf(bool bitValue) const
	{
		WORD_TYPE wordValue = bitValue ? 0 : allBitsSet<WORD_TYPE>();

		// Skip words that are entirely set/unset
		int32_t numWords = numWordsRequired(m_numBits);
		int32_t i = numWords - 1;
		const WORD_TYPE* dataPtr = data();

		WORD_TYPE mask = lastWordMask();
		while (i >= 0 && (dataPtr[i] & mask) == (wordValue & mask))
		{
			--i;
			mask = allBitsSet<WORD_TYPE>();
		}

		if (i >= 0)
		{
			const WORD_TYPE bits = bitValue ? dataPtr[i] : ~(dataPtr[i]);
			const int32_t highestBit = ((NUM_BITS_PER_WORD - 1) - BitUtils::countLeadingZeros(bits & mask)) + (i << bitsToWordsShift<WORD_TYPE>());
			return highestBit;
		}

		return -1;
	}

	inline bool contains(bool bitVale) const
	{
		return indexOf(bitVale) >= 0;
	}

	inline TSetBitIterator<ALLOCATOR, NUM_INLINE_WORDS, WORD_TYPE> beginSetBits() const
	{
		return TSetBitIterator<ALLOCATOR, NUM_INLINE_WORDS, WORD_TYPE>(*this, 0);
	}

	inline TSetBitIterator<ALLOCATOR, NUM_INLINE_WORDS, WORD_TYPE> endSetBits() const
	{
		return TSetBitIterator<ALLOCATOR, NUM_INLINE_WORDS, WORD_TYPE>(*this);
	}

	// return true if any set bits in Other are set in this.
	template <int NumInlineWords, typename Allocator>
	inline bool anyPossible(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other) const
	{
		vxy_assert(other.size() >= size());
		auto it = getWordIterator();
		auto otherData = other.data();
		for (; it; ++it)
		{
			if ((it.getWord() & it.getWord(otherData)) != 0)
			{
				return true;
			}
		}
		return false;
	}

	// return true if any set bits in Other are set in this, between (FirstBit, LastBit) inclusive.
	template <int NumInlineWords, typename Allocator>
	inline bool anyPossibleInRange(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other, int firstBit, int lastBit) const
	{
		vxy_assert(other.size() >= size());
		vxy_sanity(lastBit > firstBit);
		vxy_sanity(lastBit < size());

		int endIndx = lastBit >> bitsToWordsShift<WORD_TYPE>();
		auto otherData = other.data();
		auto it = getWordIterator(firstBit);
		for (it = getWordIterator(firstBit); it.index() <= endIndx; ++it)
		{
			if ((it.getWord() & it.getWord(otherData)) != 0)
			{
				return true;
			}
		}
		return false;
	}

	// return true if all set bits in Other are set in this.
	template <int NumInlineWords, typename Allocator>
	inline bool allPossible(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other) const
	{
		vxy_assert(other.size() >= size());
		auto otherData = other.data();
		for (auto it = getWordIterator(); it; ++it)
		{
			WORD_TYPE word = it.getWord();
			WORD_TYPE otherWord = it.getWord(otherData);
			if ((word & otherWord) != otherWord)
			{
				return false;
			}
		}

		return true;
	}

	// include any bits set in Other from this.
	template <int NumInlineWords, typename Allocator>
	inline void include(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other)
	{
		vxy_assert(other.size() == size());
		auto otherData = other.data();
		for (auto it = getWordIterator(); it; ++it)
		{
			it.setWord(it.getWord() | it.getWord(otherData));
		}
	}

	// include any bits set in Other from this, starting at the given offset.
	template <int NumInlineWords, typename Allocator>
	inline void includeAt(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other, int writePosition)
	{
		// TODO: optimize
		for (int i = 0; i < other.size(); ++i)
		{
			operator[](i+writePosition) = at(i+writePosition) | other[i];
		}
	}

	template <int NumInlineWords, typename Allocator>
	inline TValueBitset including(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other) const
	{
		TValueBitset out(*this);
		out.include(other);
		return out;
	}

	// remove any bits set in Other from this.
	template <int NumInlineWords, typename Allocator>
	inline void exclude(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other)
	{
		vxy_assert(other.size() == size());
		auto otherData = other.data();
		for (auto it = getWordIterator(); it; ++it)
		{
			WORD_TYPE prev = it.getWord();
			it.setWord(prev & (~it.getWord(otherData)));
		}
	}
	
	// remove any bits set in Other from this, starting at the given offset.
	template <int NumInlineWords, typename Allocator>
	inline void excludeAt(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other, int writePosition)
	{
		// TODO: optimize
		for (int i = 0; i < other.size(); ++i)
		{
			if (other[i])
			{
				operator[](i+writePosition) = false;
			}
		}
	}

	// remove any bits set in Other from this.
	// Unlike exclude, this returns whether there were any changes.
	template <int NumInlineWords, typename Allocator>
	inline bool excludeCheck(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other)
	{
		vxy_assert(other.size() == size());
		bool changed = false;
		auto otherData = other.data();
		for (auto it = getWordIterator(); it; ++it)
		{
			WORD_TYPE prev = it.getWord();
			it.setWord(prev & (~it.getWord(otherData)));
			changed |= (prev != it.getWord());
		}
		return changed;
	}

	template <int NumInlineWords, typename Allocator>
	inline TValueBitset excluding(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other) const
	{
		TValueBitset out(*this);
		out.exclude(other);
		return out;
	}

	// XOR each bit in the set
	template <int NumInlineWords, typename Allocator>
	inline void xor(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other)
	{
		vxy_assert(other.size() == size());
		auto otherData = other.data();
		for (auto it = getWordIterator(); it; ++it)
		{
			it.setWord(it.getWord() ^ it.getWord(otherData));
		}
	}

	template <int NumInlineWords, typename Allocator>
	inline TValueBitset xoring(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other) const
	{
		TValueBitset out(*this);
		out.xor(other);
		return out;
	}

	// remove any bits not set in Other from this.
	template <int NumInlineWords, typename Allocator>
	inline void intersect(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other)
	{
		vxy_assert(other.size() == size());
		auto otherData = other.data();
		for (auto it = getWordIterator(); it; ++it)
		{
			WORD_TYPE prev = it.getWord();
			it.setWord(prev & it.getWord(otherData));
		}
	}

	// remove any bits not set in Other from this, starting at an offset
	template <int NumInlineWords, typename Allocator>
	inline void intersectAt(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other, int writePosition)
	{
		// TODO: optimize
		for (int i = 0; i < other.size(); ++i)
		{
			operator[](i+writePosition) = at(i+writePosition) & other[i];
		}
	}
	
	// remove any bits not set in Other from this.
	// Unlike intersect(), this returns whether there were any changes.
	template <int NumInlineWords, typename Allocator>
	inline bool intersectCheck(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other)
	{
		vxy_assert(other.size() == size());
		bool changed = false;
		auto otherData = other.data();
		for (auto it = getWordIterator(); it; ++it)
		{
			WORD_TYPE prev = it.getWord();
			it.setWord(prev & it.getWord(otherData));
			changed |= (prev != it.getWord());
		}
		return changed;
	}

	template <int NumInlineWords, typename Allocator>
	inline TValueBitset intersecting(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other) const
	{
		TValueBitset out(*this);
		out.intersect(other);
		return out;
	}

	// Returns true if all the bits set in this are set in Other as well.
	template <int NumInlineWords, typename Allocator>
	inline bool isSubsetOf(const TValueBitset<Allocator, NumInlineWords, WORD_TYPE>& other) const
	{
		vxy_assert(other.size() == size());
		auto otherData = other.data();
		for (auto it = getWordIterator(); it; ++it)
		{
			WORD_TYPE prev = it.getWord();
			if ((prev & (~it.getWord(otherData))) != 0)
			{
				return false;
			}
		}
		return true;
	}

	// Inverse all set bits.
	void invert()
	{
		WORD_TYPE* cur = data();
		WORD_TYPE* end = cur + numWordsRequired(m_numBits);
		for (; cur != end; ++cur)
		{
			*cur = ~(*cur);
		}
	}

	inline TValueBitset inverted() const
	{
		TValueBitset out = *this;
		out.invert();
		return out;
	}

	// Return true if only a single bit is set.
	inline bool isSingleton() const
	{
		int bit = indexOf(true);
		return bit >= 0 && lastIndexOf(true) == bit;
	}

	// Return true if only a single bit is set, in which case OutValue is the index of the bit.
	inline bool isSingleton(int32_t& outSingleBitSetIndex) const
	{
		outSingleBitSetIndex = indexOf(true);
		return outSingleBitSetIndex >= 0 && lastIndexOf(true) == outSingleBitSetIndex;
	}

	// Return true if no bits are set.
	inline bool isZero() const
	{
		return indexOf(true) < 0;
	}

	inline void setZeroed()
	{
		int32_t numBytes = numWordsRequired(m_numBits) * sizeof(WORD_TYPE);
		memset(data(), 0, numBytes);
	}

	int32_t getNumSetBits() const
	{
		int32_t num = 0;
		for (auto it = getWordIterator(); it; ++it)
		{
			num += BitUtils::countBits(it.getWord());
		}
		return num;
	}

	wstring toString() const
	{
		wstring out = TEXT("[");
		bool first = true;
		int32_t start = indexOf(true);
		while (start >= 0 && start < size())
		{
			int end = start + 1;
			while (end < size() && operator[](end))
			{
				++end;
			}

			if (first)
			{
				first = false;
			}
			else
			{
				out += TEXT("; ");
			}
			if (start + 1 == end)
			{
				out.append_sprintf(TEXT("%d"), start);
			}
			else
			{
				out.append_sprintf(TEXT("%d - %d"), start, end - 1);
			}

			start = end;
			while (start < size() && !operator[](start))
			{
				++start;
			}
		}
		out += TEXT("]");
		return out;
	}

	inline const WORD_TYPE* data() const
	{
		return m_dataPtr;
	}

	inline WORD_TYPE* data()
	{
		return m_dataPtr;
	}

	inline TConstWordIterator<const WORD_TYPE> getWordIterator(int startIndex = 0) const
	{
		int lastWord = (m_numBits - 1) >> bitsToWordsShift<WORD_TYPE>();
		return TConstWordIterator<const WORD_TYPE>(data(), startIndex, m_numBits, lastWord);
	}

	inline TWordIterator<WORD_TYPE> getWordIterator(int startIndex = 0)
	{
		int lastWord = (m_numBits - 1) >> bitsToWordsShift<WORD_TYPE>();
		return TWordIterator<WORD_TYPE>(data(), startIndex, m_numBits, lastWord);
	}

private:
	inline void setSize(int bitCount)
	{
		if (bitCount > MAX_INLINE_BITS && bitCount > m_numBits)
		{
			const int newNumWords = numWordsRequired(bitCount);
			if (newNumWords != numWordsRequired(m_numBits))
			{
				vxy_sanity(newNumWords > numWordsRequired(m_numBits));
				if (m_dataPtr != m_inlineWords)
				{
					delete [] m_dataPtr;
				}
				m_dataPtr = new WORD_TYPE[numWordsRequired(bitCount)]();
			}
		}
		m_numBits = bitCount;
	}

	static inline int32_t numWordsRequired(int numBits)
	{
		return (numBits + NUM_BITS_PER_WORD - 1) >> bitsToWordsShift<WORD_TYPE>();
	}

	inline WORD_TYPE lastWordMask() const
	{
		const uint32_t unusedBits = (NUM_BITS_PER_WORD - static_cast<WORD_TYPE>(m_numBits) % NUM_BITS_PER_WORD) % NUM_BITS_PER_WORD;
		return allBitsSet<WORD_TYPE>() >> unusedBits;
	}

	int32_t m_numBits;
	WORD_TYPE* m_dataPtr;
	WORD_TYPE m_inlineWords[NUM_INLINE_WORDS];
};

} // namespace Vertexy


// Hashing for value_bitset
namespace eastl
{

template <typename ALLOCATOR, int NUM_INLINE_WORDS, typename WORD_TYPE>
struct hash<Vertexy::TValueBitset<ALLOCATOR, NUM_INLINE_WORDS, WORD_TYPE>>
{
	inline size_t operator()(const Vertexy::TValueBitset<ALLOCATOR, NUM_INLINE_WORDS, WORD_TYPE>& val) const
	{
		size_t hash = 0;
		eastl::hash<WORD_TYPE> hasher;
		for (auto it = val.getWordIterator(); it; ++it)
		{
			hash ^= hasher(it.getWord());
		}

		return hash;
	}
};

} // namespace eastl
