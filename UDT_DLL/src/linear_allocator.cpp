#include "linear_allocator.hpp"
#include "virtual_memory.hpp"
#include "assert_or_fatal.hpp"


// We don't use large pages (which are 1 MB large instead of 4 KB large).
#define UDT_MEMORY_PAGE_SIZE    4096


#if defined(UDT_TRACK_LINEAR_ALLOCATORS)
static udtIntrusiveList LinearAllocators;

#define offsetof(s,m)  ((size_t)((ptrdiff_t)&reinterpret_cast<const volatile char&>((((s *)0)->m))))
void udtVMLinearAllocator::GetStats(Stats& stats)
{
	stats = {};

	udtIntrusiveListNode* node = LinearAllocators.Root.Next;
	while(node != &LinearAllocators.Root)
	{
		udtVMLinearAllocator* const allocator = (udtVMLinearAllocator*)((u8*)node + offsetof(udtVMLinearAllocator, _listNode));
		
		++stats.AllocatorCount;
		stats.CommittedByteCount += allocator->_committedByteCount;
		stats.ReservedByteCount += allocator->_reservedByteCount;
		stats.UsedByteCount += allocator->_firstFreeByteIndex;
		node = node->Next;
	}
}
#undef offsetof
#endif


udtVMLinearAllocator::udtVMLinearAllocator()
{
	_addressSpaceStart = NULL;
	_firstFreeByteIndex = 0;
	_reservedByteCount = 0;
	_commitByteCountGranularity = 0;
	_committedByteCount = 0;

#if defined(UDT_TRACK_LINEAR_ALLOCATORS)
	AddNode(&_listNode, &LinearAllocators.Root);
#endif
}

udtVMLinearAllocator::~udtVMLinearAllocator()
{
#if defined(UDT_TRACK_LINEAR_ALLOCATORS)
	RemoveNode(&_listNode);
#endif

	Destroy();
}

bool udtVMLinearAllocator::Init(u32 reservedByteCount, u32 commitByteCountGranularity, bool commitFirstBlock)
{
	if(_addressSpaceStart != NULL)
	{
		return false;
	}

	UDT_ASSERT_OR_FATAL((commitByteCountGranularity % (u32)UDT_MEMORY_PAGE_SIZE) == 0);

	// Ensure the reserve size is a multiple of the commit granularity.
	// If it is, leave it as is. If it's not, bump it up to the next multiple.
	reservedByteCount = (reservedByteCount + commitByteCountGranularity - 1) & (~(commitByteCountGranularity - 1));

	// Make sure we reserve at least 2 memory pages while still respecting the commit granularity.
	if(reservedByteCount < (u32)(UDT_MEMORY_PAGE_SIZE * 2))
	{
		reservedByteCount += commitByteCountGranularity;
	}

	UDT_ASSERT_OR_FATAL(reservedByteCount >= (u32)(UDT_MEMORY_PAGE_SIZE * 2));

	u8* const data = (u8*)VirtualMemoryReserve(reservedByteCount);
	if(data == NULL)
	{
		UDT_ASSERT_OR_FATAL_ALWAYS("VirtualMemoryReserve failed");
		return false;
	}

	if(commitFirstBlock)
	{
		if(!VirtualMemoryCommit(data, commitByteCountGranularity))
		{
			UDT_ASSERT_OR_FATAL_ALWAYS("VirtualMemoryCommit failed");
			VirtualMemoryDecommitAndRelease(data, reservedByteCount);
			return false;
		}
	}
	
	_addressSpaceStart = data;
	_firstFreeByteIndex = 0;
	_reservedByteCount = reservedByteCount;
	_committedByteCount = commitFirstBlock ? commitByteCountGranularity : 0;
	_commitByteCountGranularity = commitByteCountGranularity;

	return true;
}

u8* udtVMLinearAllocator::Allocate(u32 byteCount)
{
	UDT_ASSERT_OR_FATAL(_addressSpaceStart != NULL);

	byteCount = (byteCount + 3) & (~3); // Will make sure the next alignment is 4-byte aligned, just like this one.
	UDT_ASSERT_OR_FATAL(_firstFreeByteIndex + byteCount <= _reservedByteCount); // We didn't reserve enough address space!

	if(_firstFreeByteIndex + byteCount > _committedByteCount)
	{
		// How many more commit chunks do we need?
		const u32 neededByteCount = _firstFreeByteIndex + byteCount - _committedByteCount;
		const u32 chunkCount = (neededByteCount + _commitByteCountGranularity - 1) / _commitByteCountGranularity;
		const u32 newByteCount = chunkCount * _commitByteCountGranularity;
		if(!VirtualMemoryCommit(_addressSpaceStart + _committedByteCount, newByteCount))
		{
			UDT_ASSERT_OR_FATAL_ALWAYS("VirtualMemoryCommit failed");
			return NULL;
		}
		_committedByteCount += newByteCount;
	}

	u8* const data = _addressSpaceStart + _firstFreeByteIndex;
	_firstFreeByteIndex += byteCount;

	return data;
}

void udtVMLinearAllocator::Pop(u32 byteCount)
{
	if(byteCount > _firstFreeByteIndex)
	{
		return;
	}

	_firstFreeByteIndex -= byteCount;
}

void udtVMLinearAllocator::Clear()
{
	_firstFreeByteIndex = 0;
}

void udtVMLinearAllocator::Purge()
{
	if(_committedByteCount - _firstFreeByteIndex < UDT_MEMORY_PAGE_SIZE)
	{
		return;
	}

	const uptr pageSizeM1 = (uptr)(UDT_MEMORY_PAGE_SIZE - 1);
	u8* const memoryToDecommit = (u8*)((uptr)(_addressSpaceStart + _firstFreeByteIndex + pageSizeM1) & (~pageSizeM1));
	u8* const committedEnd = _addressSpaceStart + _committedByteCount;
	const u32 byteCount = (u32)(committedEnd - memoryToDecommit);
	VirtualMemoryDecommit(memoryToDecommit, byteCount);
	_committedByteCount -= byteCount;
}

void udtVMLinearAllocator::SetCurrentByteCount(u32 byteCount)
{
	if(byteCount > _committedByteCount)
	{
		return;
	}

	_firstFreeByteIndex = byteCount;
}

u32	udtVMLinearAllocator::GetCurrentByteCount() const
{
	return _firstFreeByteIndex;
}

u32 udtVMLinearAllocator::GetCommittedByteCount() const
{
	return _committedByteCount;
}

u8* udtVMLinearAllocator::GetStartAddress() const
{
	return _addressSpaceStart;
}

u8* udtVMLinearAllocator::GetCurrentAddress() const
{
	return _addressSpaceStart + _firstFreeByteIndex;
}

void udtVMLinearAllocator::Destroy()
{
	if(_addressSpaceStart == NULL)
	{
		return;
	}

	VirtualMemoryDecommitAndRelease(_addressSpaceStart, _reservedByteCount);
	_addressSpaceStart = NULL;
	_firstFreeByteIndex = 0;
	_reservedByteCount = 0;
	_commitByteCountGranularity = 0;
	_committedByteCount = 0;
}
