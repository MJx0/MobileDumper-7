#pragma once

#include <cwchar>
#include <stdexcept>
#include <string>

#include "../../Memory/IMemory.h"
#include "../../Settings.h"

#include "Enums.h"


namespace UC
{
	template <typename ArrayElementType>
	class TArray;

	template <typename SparseArrayElementType>
	class TSparseArray;

	template <typename SetElementType>
	class TSet;

	template <typename KeyElementType, typename ValueElementType>
	class TMap;

	template <typename KeyElementType, typename ValueElementType>
	class TPair;

	namespace Iterators
	{
		class FSetBitIterator;

		template <typename ArrayType>
		class TArrayIterator;

		template <class ContainerType>
		class TContainerIterator;

		template <typename SparseArrayElementType>
		using TSparseArrayIterator = TContainerIterator<TSparseArray<SparseArrayElementType>>;

		template <typename SetElementType>
		using TSetIterator = TContainerIterator<TSet<SetElementType>>;

		template <typename KeyElementType, typename ValueElementType>
		using TMapIterator = TContainerIterator<TMap<KeyElementType, ValueElementType>>;
	}


	namespace ContainerImpl
	{
		namespace HelperFunctions
		{
			inline uint32 FloorLog2(uint32 Value)
			{
				uint32 pos = 0;
				if (Value >= 1 << 16)
				{
					Value >>= 16;
					pos    += 16;
				}
				if (Value >= 1 << 8)
				{
					Value >>= 8;
					pos    += 8;
				}
				if (Value >= 1 << 4)
				{
					Value >>= 4;
					pos    += 4;
				}
				if (Value >= 1 << 2)
				{
					Value >>= 2;
					pos    += 2;
				}
				if (Value >= 1 << 1)
				{
					pos += 1;
				}
				return pos;
			}

			inline uint32 CountLeadingZeros(uint32 Value)
			{
				if (Value == 0)
					return 32;

				return 31 - FloorLog2(Value);
			}
		}

		template <int32 Size, uint32 Alignment>
		struct TAlignedBytes
		{
			alignas(Alignment) uint8 Pad[Size];
		};

		template <uint32 NumInlineElements>
		class TInlineAllocator
		{
		public:
			template <typename ElementType>
			class ForElementType
			{
			private:
				static constexpr int32 ElementSize  = sizeof(ElementType);
				static constexpr int32 ElementAlign = alignof(ElementType);

				static constexpr int32 InlineDataSizeBytes = NumInlineElements * ElementSize;

			private:
				TAlignedBytes<ElementSize, ElementAlign> InlineData[NumInlineElements];
				ElementType* SecondaryData;

			public:
				ForElementType()
				    : InlineData{},
				      SecondaryData(nullptr)
				{
				}

				ForElementType(ForElementType&&)      = default;
				ForElementType(const ForElementType&) = default;

			public:
				ForElementType& operator=(ForElementType&&)      = default;
				ForElementType& operator=(const ForElementType&) = default;

			public:
				inline const ElementType* GetAllocation() const { return SecondaryData ? SecondaryData : reinterpret_cast<const ElementType*>(&InlineData); }

				inline uint32 GetNumInlineBytes() const { return NumInlineElements; }
			};
		};

		class FBitArray
		{
		protected:
			static constexpr int32 NumBitsPerDWORD       = 32;
			static constexpr int32 NumBitsPerDWORDLogTwo = 5;

		private:
			TInlineAllocator<4>::ForElementType<int32> Data;
			int32 NumBits;
			int32 MaxBits;

		public:
			FBitArray()
			    : NumBits(0),
			      MaxBits(Data.GetNumInlineBytes() * NumBitsPerDWORD)
			{
			}

			FBitArray(const FBitArray&) = default;

			FBitArray(FBitArray&&) = default;

		public:
			FBitArray& operator=(FBitArray&&) = default;

			FBitArray& operator=(const FBitArray& Other) = default;

		private:
			inline void VerifyIndex(int32 Index) const
			{
				if (!IsValidIndex(Index))
					throw std::out_of_range("Index was out of range!");
			}

		public:
			inline int32 Num() const { return NumBits; }
			inline int32 Max() const { return MaxBits; }

			inline const uint32* GetData() const { return reinterpret_cast<const uint32*>(Data.GetAllocation()); }

			inline bool IsValidIndex(int32 Index) const { return Index >= 0 && Index < NumBits; }

			inline bool IsValid() const { return GetData() && NumBits > 0; }

		public:
			inline bool operator[](int32 Index) const
			{
				VerifyIndex(Index);
				return GetData()[Index / NumBitsPerDWORD] & (1 << (Index & (NumBitsPerDWORD - 1)));
			}

			inline bool operator==(const FBitArray& Other) const { return NumBits == Other.NumBits && GetData() == Other.GetData(); }
			inline bool operator!=(const FBitArray& Other) const { return NumBits != Other.NumBits || GetData() != Other.GetData(); }

		public:
			friend Iterators::FSetBitIterator begin(const FBitArray& Array);
			friend Iterators::FSetBitIterator end(const FBitArray& Array);
		};

		template <typename SparseArrayType>
		union TSparseArrayElementOrFreeListLink
		{
			SparseArrayType ElementData;

			struct
			{
				int32 PrevFreeIndex;
				int32 NextFreeIndex;
			};
		};

		template <typename SetType>
		class SetElement
		{
		public:
			template <typename SetElementType>
			friend class UC::TSet;

		private:
			SetType Value;
			int32 HashNextId;
			int32 HashIndex;
		};
	}


	template <typename KeyType, typename ValueType>
	class TPair
	{
	public:
		KeyType First;
		ValueType Second;

	public:
		TPair() = default;

		TPair(KeyType Key, ValueType Value)
		    : First(Key),
		      Second(Value)
		{
		}

	public:
		inline KeyType& Key() { return First; }
		inline const KeyType& Key() const { return First; }

		inline ValueType& Value() { return Second; }
		inline const ValueType& Value() const { return Second; }
	};

	template <typename ArrayElementType>
	class TArray
	{
		template <typename SparseArrayElementType>
		friend class TSparseArray;

	protected:
		static constexpr uint64 ElementAlign = alignof(ArrayElementType);
		static constexpr uint64 ElementSize  = sizeof(ArrayElementType);

	protected:
		ArrayElementType* Data;
		int32 NumElements;
		int32 MaxElements;

	public:
		TArray()
		    : Data(nullptr),
		      NumElements(0),
		      MaxElements(0)
		{
		}

		TArray(const TArray&) = default;

		TArray(TArray&&) = default;

	public:
		TArray& operator=(TArray&&)      = default;
		TArray& operator=(const TArray&) = default;

	private:
		inline int32 GetSlack() const { return MaxElements - NumElements; }

		inline void VerifyIndex(int32 Index) const
		{
			if (!IsValidIndex(Index))
				throw std::out_of_range("Index was out of range!");
		}

	public:
		/* Adds to the array if there is still space for one more element */
		inline bool Add(const ArrayElementType& Element)
		{
			if (GetSlack() <= 0)
				return false;

			Data[NumElements] = Element;
			NumElements++;

			return true;
		}

		inline bool Remove(int32 Index)
		{
			if (!IsValidIndex(Index))
				return false;

			NumElements--;

			for (int i = Index; i < NumElements; i++)
			{
				/* NumElements was decremented, acessing i + 1 is safe */
				Data[i] = Data[i + 1];
			}

			return true;
		}

		inline void Clear()
		{
			NumElements = 0;

			if (!Data)
				memset(Data, 0, NumElements * ElementSize);
		}

	public:
		inline int32 Num() const { return NumElements; }
		inline int32 Max() const { return MaxElements; }

		inline const ArrayElementType* GetDataPtr() const { return Data; }

		inline bool IsValidIndex(int32 Index) const { return Data && Index >= 0 && Index < NumElements; }

		inline bool IsValid() const { return Data && NumElements > 0 && MaxElements >= NumElements; }

		/* Returns the game-process address of element at Index without dereferencing it */
		inline uintptr_t GetElementAddress(int32 Index) const
		{
			return reinterpret_cast<uintptr_t>(Data) + static_cast<uintptr_t>(Index) * ElementSize;
		}

	public:
		/* Reads element from remote (game-process) memory via GMemory */
		inline ArrayElementType operator[](int32 Index) const
		{
			VerifyIndex(Index);
			return GMemory->Read<ArrayElementType>(GetElementAddress(Index));
		}

		/* Direct pointer write — only valid when Data is local (Add/Remove/Clear paths) */
		inline ArrayElementType& operator[](int32 Index)
		{
			VerifyIndex(Index);
			return Data[Index];
		}

		inline bool operator==(const TArray<ArrayElementType>& Other) const { return Data == Other.Data; }
		inline bool operator!=(const TArray<ArrayElementType>& Other) const { return Data != Other.Data; }

		inline explicit operator bool() const { return IsValid(); };

	public:
		template <typename T>
		friend Iterators::TArrayIterator<T> begin(const TArray& Array);
		template <typename T>
		friend Iterators::TArrayIterator<T> end(const TArray& Array);
	};

	class FString : public TArray<uint8>
	{
	public:
		friend std::ostream& operator<<(std::ostream& Stream, const UC::FString& Str) { return Stream << Str.ToString(); }

	public:
		using TArray::TArray;

	public:
		static size_t CharSize() { return InternalSettings::bUseChar16String ? sizeof(char16_t) : sizeof(wchar_t); }

		FString() = default;

		FString(const uint8* Str)
		{
			uint32 NullTerminatedLength;

			if (InternalSettings::bUseChar16String)
				NullTerminatedLength = static_cast<uint32>(std::char_traits<char16_t>::length(reinterpret_cast<const char16_t*>(Str))) + 1;
			else
				NullTerminatedLength = static_cast<uint32>(wcslen(reinterpret_cast<const wchar_t*>(Str))) + 1;

			Data        = const_cast<uint8*>(Str);
			NumElements = NullTerminatedLength;
			MaxElements = NullTerminatedLength;
		}

		FString(uint8* Str, int32 Num, int32 Max)
		{
			Data        = Str;
			NumElements = Num;
			MaxElements = Max;
		}

	public:
		std::string ToString() const;
		std::wstring ToWString() const;

	public:
		inline void* CStr() { return Data; }
		inline const void* CStr() const { return Data; }

		explicit inline operator bool() const { return NumElements > 0 && Data != nullptr; }

	public:
		inline bool operator==(const FString& Other) const
		{
			return Other && NumElements == Other.NumElements;
		}

		inline bool operator!=(const FString& Other) const { return !(*this == Other); }
	};

	// Utf8String that assumes C-APIs (strlen, strcmp) behaviour works for char8_t like Ansi strings, execept it's counting/comparing bytes not characters.
	class FUtf8String : public TArray<char8_t>
	{
	public:
		friend std::ostream& operator<<(std::ostream& Stream, const UC::FUtf8String& Str) { return Stream << Str.ToString(); }

	public:
		using TArray::TArray;

		FUtf8String(const char8_t* Str)
		{
			Data = const_cast<char8_t*>(Str);

			const uint32 NullTerminatedLength = static_cast<uint32>(strlen(reinterpret_cast<const char*>(Str)) + 1);

			NumElements = NullTerminatedLength;
			MaxElements = NullTerminatedLength;
		}

		FUtf8String(char8_t* Str, int32 Num, int32 Max)
		{
			Data        = Str;
			NumElements = Num;
			MaxElements = Max;
		}

	public:
		std::string ToString() const;
		std::wstring ToWString() const;

	public:
		inline char8_t* CStr() { return Data; }
		inline const char8_t* CStr() const { return Data; }

	public:
		inline bool operator==(const FUtf8String& Other) const { return Other ? NumElements == Other.NumElements : false; }
		inline bool operator!=(const FUtf8String& Other) const { return Other ? NumElements != Other.NumElements : true; }
	};

	class FAnsiString : public TArray<char>
	{
	public:
		friend std::ostream& operator<<(std::ostream& Stream, const UC::FAnsiString& Str) { return Stream << Str.ToString(); }

	public:
		using TArray::TArray;

		FAnsiString(const char* Str)
		{
			const uint32 NullTerminatedLength = static_cast<uint32>(strlen(Str) + 0x1);

			Data        = const_cast<char*>(Str);
			NumElements = NullTerminatedLength;
			MaxElements = NullTerminatedLength;
		}

		FAnsiString(char* Str, int32 Num, int32 Max)
		{
			Data        = Str;
			NumElements = Num;
			MaxElements = Max;
		}

	public:
		std::string ToString() const;
		std::wstring ToWString() const;

	public:
		inline char* CStr() { return Data; }
		inline const char* CStr() const { return Data; }

	public:
		inline bool operator==(const FAnsiString& Other) const { return Other ? NumElements == Other.NumElements : false; }
		inline bool operator!=(const FAnsiString& Other) const { return Other ? NumElements != Other.NumElements : true; }
	};

	template <typename SparseArrayElementType>
	class TSparseArray
	{
		template <typename SetElementType>
		friend class TSet;

	private:
		static constexpr uint32 ElementAlign = alignof(SparseArrayElementType);
		static constexpr uint32 ElementSize  = sizeof(SparseArrayElementType);

	private:
		using FElementOrFreeListLink = ContainerImpl::TSparseArrayElementOrFreeListLink<ContainerImpl::TAlignedBytes<ElementSize, ElementAlign>>;

	private:
		TArray<FElementOrFreeListLink> Data;
		ContainerImpl::FBitArray AllocationFlags;
		int32 FirstFreeIndex;
		int32 NumFreeIndices;

	public:
		TSparseArray()
		    : FirstFreeIndex(-1),
		      NumFreeIndices(0)
		{
		}

		TSparseArray(TSparseArray&&)      = default;
		TSparseArray(const TSparseArray&) = default;

	public:
		TSparseArray& operator=(TSparseArray&&)      = default;
		TSparseArray& operator=(const TSparseArray&) = default;

	private:
		inline void VerifyIndex(int32 Index) const
		{
			if (!IsValidIndex(Index))
				throw std::out_of_range("Index was out of range!");
		}

	public:
		inline int32 NumAllocated() const { return Data.Num(); }

		inline int32 Num() const { return NumAllocated() - NumFreeIndices; }
		inline int32 Max() const { return Data.Max(); }

		inline bool IsValidIndex(int32 Index) const { return Data.IsValidIndex(Index) && AllocationFlags[Index]; }

		inline bool IsValid() const { return Data.IsValid() && AllocationFlags.IsValid(); }

		/* Returns the game-process address of the element (SetElement::Value is at offset 0) */
		inline uintptr_t GetElementAddress(int32 Index) const
		{
			return Data.GetElementAddress(Index);
		}

	public:
		const ContainerImpl::FBitArray& GetAllocationFlags() const { return AllocationFlags; }

	public:
		/* Reads element from remote (game-process) memory via GMemory; returns by value */
		inline SparseArrayElementType operator[](int32 Index) const
		{
			VerifyIndex(Index);
			FElementOrFreeListLink Link = Data[Index];
			return *reinterpret_cast<const SparseArrayElementType*>(&Link.ElementData);
		}

		/* Direct reference — only valid when Data is local */
		inline SparseArrayElementType& operator[](int32 Index)
		{
			VerifyIndex(Index);
			return *reinterpret_cast<SparseArrayElementType*>(&Data.Data[Index].ElementData);
		}

		inline bool operator==(const TSparseArray<SparseArrayElementType>& Other) const { return Data == Other.Data; }
		inline bool operator!=(const TSparseArray<SparseArrayElementType>& Other) const { return Data != Other.Data; }

	public:
		template <typename T>
		friend Iterators::TSparseArrayIterator<T> begin(const TSparseArray& Array);
		template <typename T>
		friend Iterators::TSparseArrayIterator<T> end(const TSparseArray& Array);
	};

	template <typename SetElementType>
	class TSet
	{
	private:
		static constexpr uint32 ElementAlign = alignof(SetElementType);
		static constexpr uint32 ElementSize  = sizeof(SetElementType);

	private:
		using SetDataType = ContainerImpl::SetElement<SetElementType>;
		using HashType    = ContainerImpl::TInlineAllocator<1>::ForElementType<int32>;

	private:
		TSparseArray<SetDataType> Elements;
		HashType Hash;
		int32 HashSize;

	public:
		TSet()
		    : HashSize(0)
		{
		}

		TSet(TSet&&)      = default;
		TSet(const TSet&) = default;

	public:
		TSet& operator=(TSet&&)      = default;
		TSet& operator=(const TSet&) = default;

	private:
		inline void VerifyIndex(int32 Index) const
		{
			if (!IsValidIndex(Index))
				throw std::out_of_range("Index was out of range!");
		}

	public:
		inline int32 NumAllocated() const { return Elements.NumAllocated(); }

		inline int32 Num() const { return Elements.Num(); }
		inline int32 Max() const { return Elements.Max(); }

		inline bool IsValidIndex(int32 Index) const { return Elements.IsValidIndex(Index); }

		inline bool IsValid() const { return Elements.IsValid(); }

		/* Returns the game-process address of the SetElement (Value is at offset 0) */
		inline uintptr_t GetElementAddress(int32 Index) const
		{
			return Elements.GetElementAddress(Index);
		}

	public:
		const ContainerImpl::FBitArray& GetAllocationFlags() const { return Elements.GetAllocationFlags(); }

	public:
		/* Reads element from remote (game-process) memory via GMemory; returns by value */
		inline SetElementType operator[](int32 Index) const
		{
			VerifyIndex(Index);
			return Elements[Index].Value;
		}

		/* Direct reference — only valid when Data is local */
		inline SetElementType& operator[](int32 Index)
		{
			VerifyIndex(Index);
			return Elements[Index].Value;
		}

		inline bool operator==(const TSet<SetElementType>& Other) const { return Elements == Other.Elements; }
		inline bool operator!=(const TSet<SetElementType>& Other) const { return Elements != Other.Elements; }

	public:
		template <typename T>
		friend Iterators::TSetIterator<T> begin(const TSet& Set);
		template <typename T>
		friend Iterators::TSetIterator<T> end(const TSet& Set);
	};

	template <typename KeyElementType, typename ValueElementType>
	class TMap
	{
	public:
		using ElementType = TPair<KeyElementType, ValueElementType>;

	private:
		TSet<ElementType> Elements;

	private:
		inline void VerifyIndex(int32 Index) const
		{
			if (!IsValidIndex(Index))
				throw std::out_of_range("Index was out of range!");
		}

	public:
		inline int32 NumAllocated() const { return Elements.NumAllocated(); }

		inline int32 Num() const { return Elements.Num(); }
		inline int32 Max() const { return Elements.Max(); }

		inline bool IsValidIndex(int32 Index) const { return Elements.IsValidIndex(Index); }

		inline bool IsValid() const { return Elements.IsValid(); }

		/* Returns the game-process address of the TPair; Key (First) is at offset 0 */
		inline uintptr_t GetElementAddress(int32 Index) const
		{
			return Elements.GetElementAddress(Index);
		}

	public:
		const ContainerImpl::FBitArray& GetAllocationFlags() const { return Elements.GetAllocationFlags(); }

	public:
		inline decltype(auto) Find(const KeyElementType& Key, bool (*Equals)(const KeyElementType& Key, const ValueElementType& Value))
		{
			for (auto It = begin(*this); It != end(*this); ++It)
			{
				if (Equals(It->Key(), Key))
					return It;
			}

			return end(*this);
		}

	public:
		/* Reads element from remote (game-process) memory via GMemory; returns by value */
		inline ElementType operator[](int32 Index) const
		{
			VerifyIndex(Index);
			return Elements[Index];
		}

		/* Direct reference — only valid when Data is local */
		inline ElementType& operator[](int32 Index)
		{
			VerifyIndex(Index);
			return Elements[Index];
		}

		inline bool operator==(const TMap<KeyElementType, ValueElementType>& Other) const { return Elements == Other.Elements; }
		inline bool operator!=(const TMap<KeyElementType, ValueElementType>& Other) const { return Elements != Other.Elements; }

	public:
		template <typename KeyType, typename ValueType>
		friend Iterators::TMapIterator<KeyType, ValueType> begin(const TMap& Map);
		template <typename KeyType, typename ValueType>
		friend Iterators::TMapIterator<KeyType, ValueType> end(const TMap& Map);
	};

	namespace Iterators
	{
		class FRelativeBitReference
		{
		protected:
			static constexpr int32 NumBitsPerDWORD          = 32;
			static constexpr int32 NumBitsPerDWORFMT_LOGTwo = 5;

		public:
			inline explicit FRelativeBitReference(int32 BitIndex)
			    : WordIndex(BitIndex >> NumBitsPerDWORFMT_LOGTwo),
			      Mask(1 << (BitIndex & (NumBitsPerDWORD - 1)))
			{
			}

			int32 WordIndex;
			uint32 Mask;
		};

		class FSetBitIterator : public FRelativeBitReference
		{
		private:
			const ContainerImpl::FBitArray& Array;

			uint32 UnvisitedBitMask;
			int32 CurrentBitIndex;
			int32 BaseBitIndex;

		public:
			explicit FSetBitIterator(const ContainerImpl::FBitArray& InArray, int32 StartIndex = 0)
			    : FRelativeBitReference(StartIndex),
			      Array(InArray),
			      UnvisitedBitMask((~0U) << (StartIndex & (NumBitsPerDWORD - 1))),
			      CurrentBitIndex(StartIndex),
			      BaseBitIndex(StartIndex & ~(NumBitsPerDWORD - 1))
			{
				if (StartIndex != Array.Num())
					FindFirstSetBit();
			}

		public:
			inline FSetBitIterator& operator++()
			{
				UnvisitedBitMask &= ~this->Mask;

				FindFirstSetBit();

				return *this;
			}

			inline explicit operator bool() const { return CurrentBitIndex < Array.Num(); }

			inline bool operator==(const FSetBitIterator& Rhs) const { return CurrentBitIndex == Rhs.CurrentBitIndex && &Array == &Rhs.Array; }
			inline bool operator!=(const FSetBitIterator& Rhs) const { return CurrentBitIndex != Rhs.CurrentBitIndex || &Array != &Rhs.Array; }

		public:
			inline int32 GetIndex() { return CurrentBitIndex; }

			void FindFirstSetBit()
			{
				const uint32* ArrayData   = Array.GetData();
				const int32 ArrayNum      = Array.Num();
				const int32 LastWordIndex = (ArrayNum - 1) / NumBitsPerDWORD;

				uint32 RemainingBitMask = ArrayData[this->WordIndex] & UnvisitedBitMask;
				while (!RemainingBitMask)
				{
					++this->WordIndex;
					BaseBitIndex += NumBitsPerDWORD;
					if (this->WordIndex > LastWordIndex)
					{
						CurrentBitIndex = ArrayNum;
						return;
					}

					RemainingBitMask = ArrayData[this->WordIndex];
					UnvisitedBitMask = ~0;
				}

				const uint32 NewRemainingBitMask = RemainingBitMask & (RemainingBitMask - 1);

				this->Mask = NewRemainingBitMask ^ RemainingBitMask;

				CurrentBitIndex = BaseBitIndex + NumBitsPerDWORD - 1 - ContainerImpl::HelperFunctions::CountLeadingZeros(this->Mask);

				if (CurrentBitIndex > ArrayNum)
					CurrentBitIndex = ArrayNum;
			}
		};

		template <typename ArrayType>
		class TArrayIterator
		{
		private:
			TArray<ArrayType>& IteratedArray;
			int32 Index;

		public:
			TArrayIterator(const TArray<ArrayType>& Array, int32 StartIndex = 0x0)
			    : IteratedArray(const_cast<TArray<ArrayType>&>(Array)),
			      Index(StartIndex)
			{
			}

		public:
			inline int32 GetIndex() { return Index; }

			inline int32 IsValid() { return IteratedArray.IsValidIndex(GetIndex()); }

		public:
			inline TArrayIterator& operator++()
			{
				++Index;
				return *this;
			}
			inline TArrayIterator& operator--()
			{
				--Index;
				return *this;
			}

			/* Reads element via GMemory (const operator[]) */
			inline ArrayType operator*() const { return const_cast<const TArray<ArrayType>&>(IteratedArray)[GetIndex()]; }

			struct ArrowProxy
			{
				ArrayType Value;
				ArrayType* operator->() { return &Value; }
			};
			inline ArrowProxy operator->() { return {operator*()}; }

			inline bool operator==(const TArrayIterator& Other) const { return &IteratedArray == &Other.IteratedArray && Index == Other.Index; }
			inline bool operator!=(const TArrayIterator& Other) const { return &IteratedArray != &Other.IteratedArray || Index != Other.Index; }
		};

		template <class ContainerType>
		class TContainerIterator
		{
		public:
			using ElementType = decltype(std::declval<const ContainerType>().operator[](0));

		private:
			ContainerType& IteratedContainer;
			FSetBitIterator BitIterator;

		public:
			TContainerIterator(const ContainerType& Container, const ContainerImpl::FBitArray& BitArray, int32 StartIndex = 0x0)
			    : IteratedContainer(const_cast<ContainerType&>(Container)),
			      BitIterator(BitArray, StartIndex)
			{
			}

		public:
			inline int32 GetIndex() { return BitIterator.GetIndex(); }

			inline int32 IsValid() { return IteratedContainer.IsValidIndex(GetIndex()); }

		public:
			inline TContainerIterator& operator++()
			{
				++BitIterator;
				return *this;
			}

			/* Reads element from remote memory via GMemory; returns by value */
			inline ElementType operator*() { return IteratedContainer[GetIndex()]; }
			inline ElementType operator*() const { return IteratedContainer[GetIndex()]; }

			struct ArrowProxy
			{
				ElementType Value;
				ElementType* operator->() { return &Value; }
			};
			inline ArrowProxy operator->() { return {operator*()}; }
			inline ArrowProxy operator->() const { return {operator*()}; }

			inline bool operator==(const TContainerIterator& Other) const { return &IteratedContainer == &Other.IteratedContainer && BitIterator == Other.BitIterator; }
			inline bool operator!=(const TContainerIterator& Other) const { return &IteratedContainer != &Other.IteratedContainer || BitIterator != Other.BitIterator; }
		};
	}

	inline Iterators::FSetBitIterator begin(const ContainerImpl::FBitArray& Array) { return Iterators::FSetBitIterator(Array, 0); }
	inline Iterators::FSetBitIterator end(const ContainerImpl::FBitArray& Array) { return Iterators::FSetBitIterator(Array, Array.Num()); }

	template <typename T>
	inline Iterators::TArrayIterator<T> begin(const TArray<T>& Array)
	{
		return Iterators::TArrayIterator<T>(Array, 0);
	}
	template <typename T>
	inline Iterators::TArrayIterator<T> end(const TArray<T>& Array)
	{
		return Iterators::TArrayIterator<T>(Array, Array.Num());
	}

	template <typename T>
	inline Iterators::TSparseArrayIterator<T> begin(const TSparseArray<T>& Array)
	{
		return Iterators::TSparseArrayIterator<T>(Array, Array.GetAllocationFlags(), 0);
	}
	template <typename T>
	inline Iterators::TSparseArrayIterator<T> end(const TSparseArray<T>& Array)
	{
		return Iterators::TSparseArrayIterator<T>(Array, Array.GetAllocationFlags(), Array.NumAllocated());
	}

	template <typename T>
	inline Iterators::TSetIterator<T> begin(const TSet<T>& Set)
	{
		return Iterators::TSetIterator<T>(Set, Set.GetAllocationFlags(), 0);
	}
	template <typename T>
	inline Iterators::TSetIterator<T> end(const TSet<T>& Set)
	{
		return Iterators::TSetIterator<T>(Set, Set.GetAllocationFlags(), Set.NumAllocated());
	}

	template <typename T0, typename T1>
	inline Iterators::TMapIterator<T0, T1> begin(const TMap<T0, T1>& Map)
	{
		return Iterators::TMapIterator<T0, T1>(Map, Map.GetAllocationFlags(), 0);
	}
	template <typename T0, typename T1>
	inline Iterators::TMapIterator<T0, T1> end(const TMap<T0, T1>& Map)
	{
		return Iterators::TMapIterator<T0, T1>(Map, Map.GetAllocationFlags(), Map.NumAllocated());
	}

#if defined(_WIN64) || defined(__LP64__)
	static_assert(sizeof(TArray<int32>) == 0x10, "TArray has a wrong size!");
	static_assert(sizeof(TSet<int32>) == 0x50, "TSet has a wrong size!");
	static_assert(sizeof(TMap<int32, int32>) == 0x50, "TMap has a wrong size!");
#elif defined(_WIN32) || !defined(__LP64__)
	static_assert(sizeof(TArray<int32>) == 0x0C, "TArray has a wrong size!");
	static_assert(sizeof(TSet<int32>) == 0x3C, "TSet has a wrong size!");
	static_assert(sizeof(TMap<int32, int32>) == 0x3C, "TMap has a wrong size!");
#endif
}
