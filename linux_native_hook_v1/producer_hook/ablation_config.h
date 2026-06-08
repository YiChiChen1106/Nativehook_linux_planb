#pragma once

#include <cstdint>

namespace linux_native_hook_v1 {

constexpr int kAblationStageHookEntry = 1;
constexpr int kAblationStageGuard = 2;
constexpr int kAblationStageMutex = 3;
constexpr int kAblationStageTracking = 4;
constexpr int kAblationStageRecordWrite = 5;
constexpr int kAblationStageNotify = 6;
constexpr int kDefaultAblationStage = kAblationStageNotify;

constexpr int kSubAblationStageDisabled = 0;
constexpr int kTrackingSubStageSampleFilterOnly = 1;
constexpr int kTrackingSubStageInsertOnly = 2;
constexpr int kTrackingSubStageLookupOnly = 3;
constexpr int kTrackingSubStageFullErase = 4;
constexpr int kRecordWriteSubStageRecordFillMinimal = 5;
constexpr int kRecordWriteSubStageMetadataClock = 6;
constexpr int kRecordWriteSubStageMetadataPidTid = 7;
constexpr int kRecordWriteSubStageMetadataThreadName = 8;
constexpr int kRecordWriteSubStageRingIndexCheck = 9;
constexpr int kRecordWriteSubStageShmRecordCopy = 10;
constexpr int kRecordWriteSubStageAtomicIndexSelfDrain = 11;
constexpr int kRecordWriteSubStageMetadataPidOnly = 12;
constexpr int kRecordWriteSubStageMetadataTidSyscallOnly = 13;
constexpr int kRecordWriteSubStageMetadataPidTidSyscall = 14;
constexpr int kRecordWriteSubStageMetadataCachedPidOnly = 15;
constexpr int kRecordWriteSubStageMetadataThreadLocalTidOnly = 16;
constexpr int kRecordWriteSubStageMetadataCachedPidThreadLocalTid = 17;
constexpr int kRecordWriteSubStageCachedThreadNameNoRing = 18;
constexpr int kRecordWriteSubStageCachedRingIndexCheck = 19;
constexpr int kRecordWriteSubStageCachedShmRecordCopy = 20;
constexpr int kRecordWriteSubStageCachedAtomicIndexSelfDrain = 21;
constexpr int kWriterRingSubStageWriterLockOnly = 22;
constexpr int kWriterRingSubStageRecordFillCached = 23;
constexpr int kWriterRingSubStageThreadNameNoRing = 24;
constexpr int kWriterRingSubStageRingIndexCheck = 25;
constexpr int kWriterRingSubStageShmRecordCopy = 26;
constexpr int kWriterRingSubStageAtomicIndexSelfDrain = 27;
constexpr int kStage6WriterRingSubStageNoWriterRing = 28;
constexpr int kStage6WriterRingSubStageWriterMutexOnly = 29;
constexpr int kStage6WriterRingSubStageRingIndexCheck = 30;
constexpr int kStage6WriterRingSubStageRecordCopyNoPublish = 31;
constexpr int kStage6WriterRingSubStageAtomicPublishNoNotify = 32;
constexpr int kStage6WriterRingSubStageFullNotify = 33;

constexpr int kStackWriterSubStageWriteOnly = 34;
constexpr int kStackWriterSubStageFlushOnly = 35;
constexpr int kStackWriterSubStageFull = 36;
constexpr int kMaxSubAblationStage = kStackWriterSubStageFull;

constexpr int kTrackingModeGlobal = 0;
constexpr int kTrackingModeSharded = 1;
constexpr int kTrackingModeThreadLocalFallback = 2;
constexpr int kTrackingModeThreadLocalOnly = 3;
constexpr uint32_t kMaxStage6BatchSize = 64;

int GetAblationStage();
int GetSubAblationStage();
bool GetPidTidCacheEnabled();
int GetTrackingMode();
bool GetHotpathProfileEnabled();
uint32_t GetStage6BatchSize();
uint32_t GetStackWriterBatchSize();
uint32_t GetLockDelayNs();

}  // namespace linux_native_hook_v1
