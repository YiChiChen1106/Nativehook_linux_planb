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
constexpr int kMaxSubAblationStage = kTrackingSubStageFullErase;

int GetAblationStage();
int GetSubAblationStage();

}  // namespace linux_native_hook_v1
