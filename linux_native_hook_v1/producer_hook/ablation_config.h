#pragma once

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
constexpr int kMaxSubAblationStage = kWriterRingSubStageAtomicIndexSelfDrain;

constexpr int kTrackingModeGlobal = 0;
constexpr int kTrackingModeSharded = 1;

int GetAblationStage();
int GetSubAblationStage();
bool GetPidTidCacheEnabled();
int GetTrackingMode();

}  // namespace linux_native_hook_v1
