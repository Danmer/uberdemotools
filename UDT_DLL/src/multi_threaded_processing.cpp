#include "multi_threaded_processing.hpp"
#include "file_stream.hpp"
#include "utils.hpp"
#include "threads.hpp"
#include "parser_context.hpp"
#include "system.hpp"
#include "timer.hpp"
#include "api_helpers.hpp"

#include <stdlib.h>
#include <assert.h>


#define    UDT_MIN_BYTE_SIZE_PER_THREAD    ((u64)(6 * (1<<20)))
#define    UDT_MAX_THREAD_COUNT            (16)


struct FileInfo
{
	u64 ByteCount;
	const char* FilePath;
	u32 ThreadIdx;
	u32 InputIdx;
};

static int SortByFileSizesDescending(const void* aPtr, const void* bPtr)
{
	const u64 a = ((FileInfo*)aPtr)->ByteCount;
	const u64 b = ((FileInfo*)bPtr)->ByteCount;

	return (int)(b - a);
}

static int SortByThreadIndexAscending(const void* aPtr, const void* bPtr)
{
	const u32 a = ((FileInfo*)aPtr)->ThreadIdx;
	const u32 b = ((FileInfo*)bPtr)->ThreadIdx;

	return (int)(a - b);
}

udtDemoThreadAllocator::udtDemoThreadAllocator()
{
}

bool udtDemoThreadAllocator::Process(const char** filePaths, u32 fileCount, u32 maxThreadCount)
{
	if(maxThreadCount <= 1 || fileCount <= 1)
	{
		return false;
	}

	u32 processorCoreCount = 1;
	GetProcessorCoreCount(processorCoreCount);
	if(processorCoreCount == 1)
	{
		return false;
	}

	// Get file sizes and make sure we have enough data to process
	// to even consider launching new threads.
	udtVMArrayWithAlloc<FileInfo> files((uptr)sizeof(FileInfo) * (uptr)fileCount);
	u64 totalByteCount = 0;
	files.Resize(fileCount);
	for(u32 i = 0; i < fileCount; ++i)
	{
		const u64 byteCount = udtFileStream::GetFileLength(filePaths[i]);
		files[i].FilePath = filePaths[i];
		files[i].ByteCount = byteCount;
		files[i].ThreadIdx = (u32)-1;
		files[i].InputIdx = i;
		totalByteCount += byteCount;
	}

	if(totalByteCount < 2 * UDT_MIN_BYTE_SIZE_PER_THREAD)
	{
		return false;
	}

	// Prepare the final thread array.
	maxThreadCount = udt_min(maxThreadCount, (u32)UDT_MAX_THREAD_COUNT);
	maxThreadCount = udt_min(maxThreadCount, processorCoreCount);
	maxThreadCount = udt_min(maxThreadCount, fileCount);
	const u32 finalThreadCount = udt_min(maxThreadCount, (u32)(totalByteCount / UDT_MIN_BYTE_SIZE_PER_THREAD));
	Threads.Init((uptr)sizeof(udtParsingThreadData) * (uptr)finalThreadCount);
	Threads.Resize(finalThreadCount);
	memset(Threads.GetStartAddress(), 0, (size_t)Threads.GetSize() * sizeof(udtParsingThreadData));
	for(u32 i = 0; i < finalThreadCount; ++i)
	{
		Threads[i].AllocStats = udtVMLinearAllocator::Stats();
		Threads[i].Finished = false;
		Threads[i].Stop = false;
		Threads[i].Result = false;
	}

	// Sort files by size.
	qsort(files.GetStartAddress(), (size_t)fileCount, sizeof(FileInfo), &SortByFileSizesDescending);

	// Assign files to threads.
	for(u32 i = 0; i < fileCount; ++i)
	{
		u32 threadIdx = 0;
		u64 smallestThreadByteCount = (u64)-1;
		for(u32 j = 0; j < finalThreadCount; ++j)
		{
			if(Threads[j].TotalByteCount < smallestThreadByteCount)
			{
				smallestThreadByteCount = Threads[j].TotalByteCount;
				threadIdx = j;
			}
		}

		Threads[threadIdx].TotalByteCount += files[i].ByteCount;
		files[i].ThreadIdx = threadIdx;
	}

	// Sort files by thread index.
	qsort(files.GetStartAddress(), (size_t)fileCount, sizeof(FileInfo), &SortByThreadIndexAscending);
	assert(files[0].ThreadIdx == 0); // Ascending order means thread 0 is always first.

	// Build and finalize the arrays.
	u32 threadIdx = 0;
	u32 firstFileIdx = 0;
	FilePaths.Init((uptr)sizeof(const char*) * (uptr)fileCount);
	FilePaths.Resize(fileCount);
	FileSizes.Init((uptr)sizeof(u64) * (uptr)fileCount);
	FileSizes.Resize(fileCount);
	InputIndices.Init((uptr)sizeof(u32) * (uptr)fileCount);
	InputIndices.Resize(fileCount);
	for(u32 i = 0; i < fileCount; ++i)
	{
		if(files[i].ThreadIdx != threadIdx)
		{
			assert(threadIdx < Threads.GetSize());
			Threads[threadIdx].FirstFileIndex = firstFileIdx;
			Threads[threadIdx].FileCount = i - firstFileIdx;
			firstFileIdx = i;
			++threadIdx;
		}

		FilePaths[i] = files[i].FilePath;
		FileSizes[i] = files[i].ByteCount;
		InputIndices[i] = files[i].InputIdx;
	}
	Threads[threadIdx].FirstFileIndex = firstFileIdx;
	Threads[threadIdx].FileCount = fileCount - firstFileIdx;
	
#if defined(UDT_DEBUG)
	// The following is to catch errors in the file distribution across threads.
	for(u32 i = 0; i < finalThreadCount; ++i)
	{
		const u32 first = Threads[i].FirstFileIndex;

		u64 totalByteCount = 0;
		for(u32 j = 0; j < Threads[i].FileCount; ++j)
		{
			totalByteCount += udtFileStream::GetFileLength(FilePaths[first + j]);
		}

		assert(Threads[i].TotalByteCount == totalByteCount);
	}
#endif
	
	return true;
}

struct MultiThreadedProgressContext
{
	u64 TotalByteCount;
	u64 ProcessedByteCount;
	u64 CurrentJobByteCount;
	f32* Progress;
	udtTimer* Timer;
};

static void MultiThreadedProgressProgressCallback(f32 jobProgress, void* userData)
{
	MultiThreadedProgressContext* const context = (MultiThreadedProgressContext*)userData;
	if(context == NULL || context->Timer == NULL || context->Progress == NULL)
	{
		return;
	}

	if(context->Timer->GetElapsedMs() < UDT_MIN_PROGRESS_TIME_MS)
	{
		return;
	}

	context->Timer->Restart();

	const u64 jobProcessed = (u64)((f64)context->CurrentJobByteCount * (f64)jobProgress);
	const u64 totalProcessed = context->ProcessedByteCount + jobProcessed;
	const f32 realProgress = udt_clamp((f32)totalProcessed / (f32)context->TotalByteCount, 0.0f, 1.0f);

	*context->Progress = realProgress;
}

static void ThreadFunction(void* userData)
{
	udtParsingThreadData* const data = (udtParsingThreadData*)userData;
	if(data == NULL)
	{
		data->Finished = true;
		return;
	}

	udtParsingSharedData* const shared = data->Shared;
	if(shared->JobType >= (u32)udtParsingJobType::Count)
	{
		data->Finished = true;
		return;
	}

	if(shared->JobType == (u32)udtParsingJobType::CutByPattern && shared->JobSpecificInfo == NULL)
	{
		data->Finished = true;
		return;
	}

	if(shared->JobType == (u32)udtParsingJobType::Conversion && shared->JobSpecificInfo == NULL)
	{
		data->Finished = true;
		return;
	}

	const u32 startIdx = data->FirstFileIndex;
	const u32 endIdx = startIdx + data->FileCount;

	udtTimer timer;
	timer.Start();

	MultiThreadedProgressContext progressContext;
	progressContext.TotalByteCount = data->TotalByteCount;
	progressContext.ProcessedByteCount = 0;
	progressContext.CurrentJobByteCount = 0;
	progressContext.Timer = &timer;
	progressContext.Progress = &data->Progress;

	udtParseArg newParseInfo = *shared->ParseInfo;
	newParseInfo.ProgressCb = &MultiThreadedProgressProgressCallback;
	newParseInfo.ProgressContext = &progressContext;

	s32* const errorCodes = shared->MultiParseInfo->OutputErrorCodes;

	if(!InitContextWithPlugIns(*data->Context, newParseInfo, data->FileCount, (udtParsingJobType::Id)shared->JobType, shared->JobSpecificInfo))
	{
		data->Result = false;
		data->Finished = true;
		return;
	}

	for(u32 i = startIdx; i < endIdx; ++i)
	{
		if(shared->ParseInfo->CancelOperation != NULL && *shared->ParseInfo->CancelOperation != 0)
		{
			break;
		}

		const u32 errorCodeIdx = data->Context->InputIndices[i - startIdx];
		const u64 currentJobByteCount = shared->FileSizes[i];
		progressContext.CurrentJobByteCount = currentJobByteCount;

		bool success = false;
		if(shared->JobType == (u32)udtParsingJobType::General)
		{
			success = ParseDemoFile(data->Context, &newParseInfo, shared->FilePaths[i], false);
		}
		else if(shared->JobType == (u32)udtParsingJobType::CutByPattern)
		{
			success = CutByPattern(data->Context, &newParseInfo, shared->FilePaths[i]);
		}
		else if(shared->JobType == (u32)udtParsingJobType::Conversion)
		{
			success = ConvertDemoFile(data->Context, &newParseInfo, shared->FilePaths[i], (const udtProtocolConversionArg*)shared->JobSpecificInfo);
		}
		errorCodes[errorCodeIdx] = GetErrorCode(success, shared->ParseInfo->CancelOperation);

		progressContext.ProcessedByteCount += currentJobByteCount;
	}

	udtVMLinearAllocator::GetThreadStats(data->AllocStats);

	data->Result = true;
	data->Finished = true;
}

bool udtMultiThreadedParsing::Process(udtParserContext* contexts,
									  udtDemoThreadAllocator& threadInfo,
									  const udtParseArg* parseInfo,
									  const udtMultiParseArg* multiParseInfo,
									  udtParsingJobType::Id jobType,
									  const void* jobSpecificInfo)
{
	assert(contexts != NULL);
	assert(parseInfo != NULL);
	assert(multiParseInfo != NULL);
	assert(jobType < (u32)udtParsingJobType::Count);

	const u32 threadCount = threadInfo.Threads.GetSize();

	udtParsingSharedData sharedData;
	memset(&sharedData, 0, sizeof(sharedData));
	sharedData.JobSpecificInfo = jobSpecificInfo;
	sharedData.MultiParseInfo = multiParseInfo;
	sharedData.ParseInfo = parseInfo;
	sharedData.FilePaths = threadInfo.FilePaths.GetStartAddress();
	sharedData.FileSizes = threadInfo.FileSizes.GetStartAddress();
	sharedData.JobType = (u32)jobType;
	
	for(u32 i = 0, count = multiParseInfo->FileCount; i < count; ++i)
	{
		multiParseInfo->OutputErrorCodes[i] = (s32)udtErrorCode::Unprocessed;
	}

	udtTimer timer;
	timer.Start();

	bool success = true;
	udtVMArrayWithAlloc<udtThread> threads((uptr)sizeof(udtThread) * (uptr)threadCount);
	threads.Resize(threadCount);
	for(u32 i = 0; i < threadCount; ++i)
	{
		udtParserContext* const context = contexts + i;
		const u32 demoCount = threadInfo.Threads[i].FileCount;
		const u32 firstDemoIdx = threadInfo.Threads[i].FirstFileIndex;
		context->InputIndices.Resize(demoCount);
		for(u32 j = 0; j < demoCount; ++j)
		{
			context->InputIndices[j] = threadInfo.InputIndices[firstDemoIdx + j];
		}

		udtThread& thread = threads[i];
		new (&thread) udtThread;
		threadInfo.Threads[i].Context = contexts + i;
		threadInfo.Threads[i].Shared = &sharedData;
		if(!thread.CreateAndStart(&ThreadFunction, &threadInfo.Threads[i]))
		{
			success = false;
			goto thread_clean_up;
		}
	}

	for(;;)
	{
		// Find the first non-finished thread.
		u32 threadIdx = (u32)-1;
		for(u32 i = 0; i < threadCount; ++i)
		{
			if(!threadInfo.Threads[i].Finished)
			{
				threadIdx = i;
			}
		}

		if(threadIdx == (u32)-1)
		{
			// All threads are done or something went wrong.
			break;
		}

		udtParsingThreadData& data = threadInfo.Threads[threadIdx];
		if(threads[threadIdx].TimedJoin(UDT_MIN_PROGRESS_TIME_MS))
		{
			data.Finished = true;
		}

		if(timer.GetElapsedMs() < UDT_MIN_PROGRESS_TIME_MS)
		{
			continue;
		}

		timer.Restart();

		// The actual progress is that of the slowest thread.
		f32 progress = 2.0f;
		for(u32 i = 0; i < threadCount; ++i)
		{
			progress = udt_min(progress, threadInfo.Threads[i].Progress);
		}

		(*parseInfo->ProgressCb)(progress, parseInfo->ProgressContext);
	}
	
	// If the above code is correct and never fails, this is redundant.
	for(u32 i = 0; i < threadCount; ++i)
	{
		threads[i].Join();
	}
	
thread_clean_up:
	for(u32 i = 0; i < threadCount; ++i)
	{
		threads[i].Release();
	}

	if(success && (parseInfo->Flags & (u32)udtParseArgFlags::PrintAllocStats) != 0)
	{
		udtVMLinearAllocator::Stats allocStats = udtVMLinearAllocator::Stats();
		udtVMLinearAllocator::GetThreadStats(allocStats);
		for(u32 i = 0; i < threadCount; ++i)
		{
			const udtVMLinearAllocator::Stats& threadStats = threadInfo.Threads[i].AllocStats;
			allocStats.AllocatorCount += threadStats.AllocatorCount;
			allocStats.ReservedByteCount += threadStats.ReservedByteCount;
			allocStats.CommittedByteCount += threadStats.CommittedByteCount;
			allocStats.UsedByteCount += threadStats.UsedByteCount;
		}
		const uptr extraByteCount = (uptr)sizeof(udtParserContext) * (uptr)threadCount;
		allocStats.CommittedByteCount += extraByteCount;
		allocStats.UsedByteCount += extraByteCount;
		contexts[0].Parser._tempAllocator.Clear();
		LogLinearAllocatorStats(threadCount, multiParseInfo->FileCount, contexts[0].Context, contexts[0].Parser._tempAllocator, allocStats);
	}

	return success;
}
