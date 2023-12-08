/*
 * Copyright (C) 2022-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#define FULL_VERBOSITY_IN_RELEASE_BUILD 1
#define ENABLE_BLOATED_VERBOSITY 1

#include "cal.h"
#include "level_zero/ze_api.h"
#include "shared/log.h"
#include "test/utils/assertions.h"
#include "test/utils/dynamic_library.h"
#include "test/utils/l0_common_steps.h"

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, const char *argv[]) {
    using namespace Cal::Testing::Utils::LevelZero;

    // Setup.
    Cal::Utils::initMaxDynamicVerbosity(Verbosity::debug);

    std::vector<ze_driver_handle_t> drivers{};
    std::vector<ze_device_handle_t> devices{};

    RUN_REQUIRED_STEP(initL0());
    RUN_REQUIRED_STEP(getDrivers(drivers));
    RUN_REQUIRED_STEP(getDevices(drivers[0], devices));

    ze_context_handle_t context{};
    RUN_REQUIRED_STEP(createContext(drivers[0], context));

    uint32_t ordinal{};
    RUN_REQUIRED_STEP(getComputeQueueOrdinal(devices[0], ordinal));

    ze_command_queue_handle_t queue{};
    RUN_REQUIRED_STEP(createCommandQueue(context, devices[0], ordinal, queue, ze_command_queue_mode_t::ZE_COMMAND_QUEUE_MODE_ASYNCHRONOUS));

    ze_command_list_handle_t cmdList{};
    RUN_REQUIRED_STEP(createCommandList(context, devices[0], ordinal, cmdList));

    ze_fence_handle_t fence{};
    RUN_REQUIRED_STEP(createFence(queue, fence));

    constexpr uint32_t eventsCount{10};
    ze_event_pool_handle_t eventPool{};
    RUN_REQUIRED_STEP(createEventPool(context, eventsCount, &devices[0], 1, eventPool));

    ze_event_handle_t allCopiesFinishedEvent{};
    RUN_REQUIRED_STEP(createEvent(eventPool, 1, allCopiesFinishedEvent));

    constexpr size_t bufferSize{1024};
    constexpr size_t patternSize{16};
    constexpr size_t alignment{8};

    std::vector<char> sourceBufferFromHeap(bufferSize);
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(sourceBufferFromHeap.data(), 0x44, bufferSize));

    std::vector<char> destinationBufferFromHeap(bufferSize);
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(destinationBufferFromHeap.data(), 0x55, bufferSize));

    void *usmHostBuffer{nullptr};
    RUN_REQUIRED_STEP(allocateHostMemory(context, bufferSize, alignment, usmHostBuffer));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmHostBuffer, 0xAA, bufferSize));

    void *usmSharedBuffer{nullptr};
    RUN_REQUIRED_STEP(allocateSharedMemory(context, bufferSize, alignment, devices[0], usmSharedBuffer));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmSharedBuffer, 0xBB, bufferSize));

    RUN_REQUIRED_STEP(appendMemoryPrefetch(cmdList, usmSharedBuffer, bufferSize));

    void *usmDeviceBuffer{nullptr};
    RUN_REQUIRED_STEP(allocateDeviceMemory(context, bufferSize, alignment, devices[0], usmDeviceBuffer));

    // Case 1: zeCommandListAppendMemoryCopy synchronized via zeCommandQueueSynchronize.
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmDeviceBuffer, sourceBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, destinationBufferFromHeap.data(), usmSharedBuffer, bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmHostBuffer, usmSharedBuffer, bufferSize));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, nullptr));
    RUN_REQUIRED_STEP(synchronizeCommandQueue(queue));

    RUN_REQUIRED_STEP(verifyMemoryCopyResults(usmSharedBuffer, destinationBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(verifyMemoryCopyResults(usmSharedBuffer, usmHostBuffer, bufferSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, destinationBufferFromHeap.data(), sourceBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmHostBuffer, usmDeviceBuffer, bufferSize));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, nullptr));
    RUN_REQUIRED_STEP(synchronizeCommandQueue(queue));

    RUN_REQUIRED_STEP(verifyMemoryCopyResults(sourceBufferFromHeap.data(), destinationBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(verifyMemoryCopyResults(sourceBufferFromHeap.data(), usmHostBuffer, bufferSize));

    // Case 2: zeCommandListAppendMemoryCopy synchronized via zeFenceHostSynchronize.
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(sourceBufferFromHeap.data(), 0x11, bufferSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(destinationBufferFromHeap.data(), 0x22, bufferSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmHostBuffer, 0x33, bufferSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmSharedBuffer, 0x77, bufferSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetFence(fence));

    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmDeviceBuffer, sourceBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, destinationBufferFromHeap.data(), usmSharedBuffer, bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmHostBuffer, usmSharedBuffer, bufferSize));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, fence));
    RUN_REQUIRED_STEP(synchronizeViaFence(fence));

    RUN_REQUIRED_STEP(verifyMemoryCopyResults(usmSharedBuffer, destinationBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(verifyMemoryCopyResults(usmSharedBuffer, usmHostBuffer, bufferSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetFence(fence));

    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, destinationBufferFromHeap.data(), sourceBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmHostBuffer, usmDeviceBuffer, bufferSize));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, fence));
    RUN_REQUIRED_STEP(synchronizeViaFence(fence));

    RUN_REQUIRED_STEP(verifyMemoryCopyResults(sourceBufferFromHeap.data(), destinationBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(verifyMemoryCopyResults(sourceBufferFromHeap.data(), usmHostBuffer, bufferSize));

    // Case 3: zeCommandListAppendMemoryCopy synchronized via zeFenceQueryStatus.
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(sourceBufferFromHeap.data(), 0x11, bufferSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(destinationBufferFromHeap.data(), 0x22, bufferSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmHostBuffer, 0x33, bufferSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmSharedBuffer, 0x77, bufferSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetFence(fence));

    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmDeviceBuffer, sourceBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, destinationBufferFromHeap.data(), usmSharedBuffer, bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmHostBuffer, usmSharedBuffer, bufferSize));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, fence));
    RUN_REQUIRED_STEP(synchronizeViaFencePooling(fence, std::chrono::milliseconds{2000u}));

    RUN_REQUIRED_STEP(verifyMemoryCopyResults(usmSharedBuffer, destinationBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(verifyMemoryCopyResults(usmSharedBuffer, usmHostBuffer, bufferSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetFence(fence));

    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, destinationBufferFromHeap.data(), sourceBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmHostBuffer, usmDeviceBuffer, bufferSize));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, fence));
    RUN_REQUIRED_STEP(synchronizeViaFencePooling(fence, std::chrono::milliseconds{2000u}));

    RUN_REQUIRED_STEP(verifyMemoryCopyResults(sourceBufferFromHeap.data(), destinationBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(verifyMemoryCopyResults(sourceBufferFromHeap.data(), usmHostBuffer, bufferSize));

    // Case 4: zeCommandListAppendMemoryCopy synchronized via zeEventHostSynchronize.
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(sourceBufferFromHeap.data(), 0x11, bufferSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(destinationBufferFromHeap.data(), 0x22, bufferSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmHostBuffer, 0x33, bufferSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmSharedBuffer, 0x77, bufferSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetEvent(allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmDeviceBuffer, sourceBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, destinationBufferFromHeap.data(), usmSharedBuffer, bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmHostBuffer, usmSharedBuffer, bufferSize));
    RUN_REQUIRED_STEP(appendBarrier(cmdList, allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, nullptr));
    RUN_REQUIRED_STEP(synchronizeViaEvent(allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(verifyMemoryCopyResults(usmSharedBuffer, destinationBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(verifyMemoryCopyResults(usmSharedBuffer, usmHostBuffer, bufferSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetEvent(allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, destinationBufferFromHeap.data(), sourceBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmHostBuffer, usmDeviceBuffer, bufferSize));
    RUN_REQUIRED_STEP(appendBarrier(cmdList, allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, nullptr));
    RUN_REQUIRED_STEP(synchronizeViaEvent(allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(verifyMemoryCopyResults(sourceBufferFromHeap.data(), destinationBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(verifyMemoryCopyResults(sourceBufferFromHeap.data(), usmHostBuffer, bufferSize));

    // Case 5: zeCommandListAppendMemoryCopy synchronized via zeEventQueryStatus.
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(sourceBufferFromHeap.data(), 0x11, bufferSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(destinationBufferFromHeap.data(), 0x22, bufferSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmHostBuffer, 0x33, bufferSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmSharedBuffer, 0x77, bufferSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetEvent(allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmDeviceBuffer, sourceBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, destinationBufferFromHeap.data(), usmSharedBuffer, bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmHostBuffer, usmSharedBuffer, bufferSize));
    RUN_REQUIRED_STEP(appendBarrier(cmdList, allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, nullptr));
    RUN_REQUIRED_STEP(synchronizeViaEventPooling(allCopiesFinishedEvent, std::chrono::milliseconds{2000u}));

    RUN_REQUIRED_STEP(verifyMemoryCopyResults(usmSharedBuffer, destinationBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(verifyMemoryCopyResults(usmSharedBuffer, usmHostBuffer, bufferSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetEvent(allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, destinationBufferFromHeap.data(), sourceBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, usmHostBuffer, usmDeviceBuffer, bufferSize));
    RUN_REQUIRED_STEP(appendBarrier(cmdList, allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, nullptr));
    RUN_REQUIRED_STEP(synchronizeViaEventPooling(allCopiesFinishedEvent, std::chrono::milliseconds{2000u}));

    RUN_REQUIRED_STEP(verifyMemoryCopyResults(sourceBufferFromHeap.data(), destinationBufferFromHeap.data(), bufferSize));
    RUN_REQUIRED_STEP(verifyMemoryCopyResults(sourceBufferFromHeap.data(), usmHostBuffer, bufferSize));

    // Case 6: zeCommandListAppendMemoryFill synchronized via zeCommandQueueSynchronize.
    static constexpr std::array<char, patternSize> patternFromStack = {1, 7, 1, 9, 1, 5, 1, 4, 2, 3, 4, 5, 6, 2, 3, 3};
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmHostBuffer, 0xEE, patternSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmSharedBuffer, 0xDD, bufferSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(appendMemoryFill(cmdList, usmHostBuffer, patternFromStack.data(), patternSize, bufferSize));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, nullptr));
    RUN_REQUIRED_STEP(synchronizeCommandQueue(queue));

    RUN_REQUIRED_STEP(verifyMemoryFillResults(usmHostBuffer, bufferSize, patternFromStack.data(), patternSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(appendMemoryFill(cmdList, usmSharedBuffer, usmHostBuffer, patternSize, bufferSize));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, nullptr));
    RUN_REQUIRED_STEP(synchronizeCommandQueue(queue));

    RUN_REQUIRED_STEP(verifyMemoryFillResults(usmSharedBuffer, bufferSize, usmHostBuffer, patternSize));

    // Case 7: zeCommandListAppendMemoryFill synchronized via zeFenceHostSynchronize.
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmHostBuffer, 0xEE, patternSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmSharedBuffer, 0xDD, bufferSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetFence(fence));
    RUN_REQUIRED_STEP(appendMemoryFill(cmdList, usmHostBuffer, patternFromStack.data(), patternSize, bufferSize));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, fence));
    RUN_REQUIRED_STEP(synchronizeViaFence(fence));

    RUN_REQUIRED_STEP(verifyMemoryFillResults(usmHostBuffer, bufferSize, patternFromStack.data(), patternSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetFence(fence));
    RUN_REQUIRED_STEP(appendMemoryFill(cmdList, usmSharedBuffer, usmHostBuffer, patternSize, bufferSize));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, fence));
    RUN_REQUIRED_STEP(synchronizeViaFence(fence));

    RUN_REQUIRED_STEP(verifyMemoryFillResults(usmSharedBuffer, bufferSize, usmHostBuffer, patternSize));

    // Case 8: zeCommandListAppendMemoryFill synchronized via zeFenceQueryStatus.
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmHostBuffer, 0xEE, patternSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmSharedBuffer, 0xDD, bufferSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetFence(fence));
    RUN_REQUIRED_STEP(appendMemoryFill(cmdList, usmHostBuffer, patternFromStack.data(), patternSize, bufferSize));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, fence));
    RUN_REQUIRED_STEP(synchronizeViaFencePooling(fence, std::chrono::milliseconds{2000u}));

    RUN_REQUIRED_STEP(verifyMemoryFillResults(usmHostBuffer, bufferSize, patternFromStack.data(), patternSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetFence(fence));
    RUN_REQUIRED_STEP(appendMemoryFill(cmdList, usmSharedBuffer, usmHostBuffer, patternSize, bufferSize));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, fence));
    RUN_REQUIRED_STEP(synchronizeViaFencePooling(fence, std::chrono::milliseconds{2000u}));

    RUN_REQUIRED_STEP(verifyMemoryFillResults(usmSharedBuffer, bufferSize, usmHostBuffer, patternSize));

    // Case 9: zeCommandListAppendMemoryFill synchronized via zeEventHostSynchronize.
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmHostBuffer, 0xEE, patternSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmSharedBuffer, 0xDD, bufferSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetEvent(allCopiesFinishedEvent));
    RUN_REQUIRED_STEP(appendMemoryFill(cmdList, usmHostBuffer, patternFromStack.data(), patternSize, bufferSize, allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, nullptr));
    RUN_REQUIRED_STEP(synchronizeViaEvent(allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(verifyMemoryFillResults(usmHostBuffer, bufferSize, patternFromStack.data(), patternSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetEvent(allCopiesFinishedEvent));
    RUN_REQUIRED_STEP(appendMemoryFill(cmdList, usmSharedBuffer, usmHostBuffer, patternSize, bufferSize, allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, fence));
    RUN_REQUIRED_STEP(synchronizeViaEvent(allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(verifyMemoryFillResults(usmSharedBuffer, bufferSize, usmHostBuffer, patternSize));

    // Case 10: zeCommandListAppendMemoryFill synchronized via zeEventQueryStatus.
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmHostBuffer, 0xEE, patternSize));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemset(usmSharedBuffer, 0xDD, bufferSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetEvent(allCopiesFinishedEvent));
    RUN_REQUIRED_STEP(appendMemoryFill(cmdList, usmHostBuffer, patternFromStack.data(), patternSize, bufferSize, allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, nullptr));
    RUN_REQUIRED_STEP(synchronizeViaEventPooling(allCopiesFinishedEvent, std::chrono::milliseconds{2000u}));

    RUN_REQUIRED_STEP(verifyMemoryFillResults(usmHostBuffer, bufferSize, patternFromStack.data(), patternSize));

    RUN_REQUIRED_STEP(resetCommandList(cmdList));
    RUN_REQUIRED_STEP(resetEvent(allCopiesFinishedEvent));
    RUN_REQUIRED_STEP(appendMemoryFill(cmdList, usmSharedBuffer, usmHostBuffer, patternSize, bufferSize, allCopiesFinishedEvent));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, fence));
    RUN_REQUIRED_STEP(synchronizeViaEventPooling(allCopiesFinishedEvent, std::chrono::milliseconds{2000u}));

    RUN_REQUIRED_STEP(verifyMemoryFillResults(usmSharedBuffer, bufferSize, usmHostBuffer, patternSize));

    // Teardown.
    RUN_REQUIRED_STEP(freeMemory(context, usmDeviceBuffer));
    RUN_REQUIRED_STEP(freeMemory(context, usmSharedBuffer));
    RUN_REQUIRED_STEP(freeMemory(context, usmHostBuffer));

    RUN_REQUIRED_STEP(destroyEvent(allCopiesFinishedEvent));
    RUN_REQUIRED_STEP(destroyEventPool(eventPool));

    RUN_REQUIRED_STEP(destroyFence(fence));
    RUN_REQUIRED_STEP(destroyCommandList(cmdList));
    RUN_REQUIRED_STEP(destroyCommandQueue(queue));

    RUN_REQUIRED_STEP(destroyContext(context));

    return 0;
}
