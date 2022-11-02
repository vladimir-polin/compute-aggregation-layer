/*
 * Copyright (C) 2022 Intel Corporation
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

#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

bool getEventPoolIpcHandle(ze_event_pool_handle_t eventPool, ze_ipc_event_pool_handle_t &eventPoolIpcHandle) {
    log<Verbosity::info>("Getting IPC handle of ze_event_pool_handle_t = %p", eventPool);

    const auto zeEventPoolGetIpcHandleResult = zeEventPoolGetIpcHandle(eventPool, &eventPoolIpcHandle);
    if (zeEventPoolGetIpcHandleResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeEventPoolGetIpcHandle() call has failed! Error code: %d", static_cast<int>(zeEventPoolGetIpcHandleResult));
        return false;
    }

    uint64_t firstBytes{};
    std::memcpy(&firstBytes, eventPoolIpcHandle.data, sizeof(firstBytes));

    log<Verbosity::info>("Successfully got IPC handle of %p! First bytes of handle as u64 = " PRIu64, eventPool, firstBytes);
    return true;
}

bool openIpcHandleOfEventPool(ze_context_handle_t context, const ze_ipc_event_pool_handle_t &eventPoolIpcHandle, ze_event_pool_handle_t &eventPoolOpenedFromIpcHandle) {
    log<Verbosity::info>("Opening ze_ipc_event_pool_handle_t via zeEventPoolOpenIpcHandle()!");

    const auto zeEventPoolOpenIpcHandleResult = zeEventPoolOpenIpcHandle(context, eventPoolIpcHandle, &eventPoolOpenedFromIpcHandle);
    if (zeEventPoolOpenIpcHandleResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeEventPoolOpenIpcHandle() call has failed! Error code: %d", static_cast<int>(zeEventPoolOpenIpcHandleResult));
        return false;
    }

    log<Verbosity::info>("Successfully opened IPC handle! Event pool handle = %p", static_cast<void *>(eventPoolOpenedFromIpcHandle));
    return true;
}

bool closeEventPoolFromIpcHandle(ze_event_pool_handle_t &eventPoolOpenedFromIpcHandle) {
    log<Verbosity::info>("Closing ze_ipc_event_pool_handle_t via zeEventPoolCloseIpcHandle()");

    const auto zeEventPoolCloseIpcHandleResult = zeEventPoolCloseIpcHandle(eventPoolOpenedFromIpcHandle);
    if (zeEventPoolCloseIpcHandleResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeEventPoolCloseIpcHandle() call has failed! Error code: %d", static_cast<int>(zeEventPoolCloseIpcHandleResult));
        return false;
    }

    eventPoolOpenedFromIpcHandle = nullptr;
    log<Verbosity::info>("Successfully closed IPC handle!");

    return true;
}

bool copyBuffersOnHost(void *destination, const void *source, size_t size) {
    log<Verbosity::info>("Copying %zd bytes from %p to %p", size, source, destination);
    std::memcpy(destination, source, size);

    log<Verbosity::info>("Data has been copied successfully!");
    return true;
}

bool signalEventFromHost(ze_event_handle_t event) {
    log<Verbosity::info>("Signaling event = %p from host!", static_cast<void *>(event));

    const auto zeEventHostSignalResult = zeEventHostSignal(event);
    if (zeEventHostSignalResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeEventHostSignal() call has failed! Error code: %d", static_cast<int>(zeEventHostSignalResult));
        return false;
    }

    log<Verbosity::info>("Successfully signaled given event!");
    return true;
}

int main(int argc, const char *argv[]) {
    using namespace Cal::Testing::Utils::LevelZero;

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
    RUN_REQUIRED_STEP(createCommandQueue(context, devices[0], ordinal, queue));

    ze_command_list_handle_t cmdList{};
    RUN_REQUIRED_STEP(createCommandList(context, devices[0], ordinal, cmdList));

    constexpr size_t elementsCount{1024};
    constexpr size_t bufferSize{elementsCount * sizeof(unsigned int)};
    constexpr size_t alignment{8};

    void *sourceBuffer{nullptr};
    void *intermediateBuffer1{nullptr};
    void *intermediateBuffer2{nullptr};
    void *destinationBuffer{nullptr};

    RUN_REQUIRED_STEP(allocateHostMemory(context, bufferSize, alignment, sourceBuffer));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemcpy(sourceBuffer, 0xAA, bufferSize));

    RUN_REQUIRED_STEP(allocateHostMemory(context, bufferSize, alignment, intermediateBuffer1));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemcpy(intermediateBuffer1, 0xBB, bufferSize));

    RUN_REQUIRED_STEP(allocateHostMemory(context, bufferSize, alignment, intermediateBuffer2));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemcpy(intermediateBuffer2, 0xCC, bufferSize));

    RUN_REQUIRED_STEP(allocateHostMemory(context, bufferSize, alignment, destinationBuffer));
    RUN_REQUIRED_STEP(fillBufferOnHostViaMemcpy(destinationBuffer, 0xDD, bufferSize));

    constexpr uint32_t eventsCount{10};
    ze_event_pool_handle_t eventPool{};
    RUN_REQUIRED_STEP(createEventPool(context, eventsCount, &devices[0], 1, eventPool));

    ze_event_handle_t intermediateBuffer1FilledEvent{};
    RUN_REQUIRED_STEP(createEvent(eventPool, 3, intermediateBuffer1FilledEvent));

    RUN_REQUIRED_STEP(copyBuffersOnHost(intermediateBuffer1, sourceBuffer, bufferSize));
    RUN_REQUIRED_STEP(signalEventFromHost(intermediateBuffer1FilledEvent));

    ze_event_handle_t intermediateBuffer2FilledEvent{};
    RUN_REQUIRED_STEP(createEvent(eventPool, 2, intermediateBuffer2FilledEvent));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, intermediateBuffer2, intermediateBuffer1, bufferSize, intermediateBuffer2FilledEvent, 1, &intermediateBuffer1FilledEvent));

    ze_event_handle_t copyingFinishedEvent{};
    RUN_REQUIRED_STEP(createEvent(eventPool, 1, copyingFinishedEvent));
    RUN_REQUIRED_STEP(appendMemoryCopy(cmdList, destinationBuffer, intermediateBuffer2, bufferSize, copyingFinishedEvent, 1, &intermediateBuffer2FilledEvent));

    RUN_REQUIRED_STEP(closeCommandList(cmdList));
    RUN_REQUIRED_STEP(executeCommandLists(queue, 1, &cmdList, nullptr));

    RUN_REQUIRED_STEP(synchronizeViaEvent(copyingFinishedEvent));
    RUN_REQUIRED_STEP(verifyMemoryCopyResults(sourceBuffer, destinationBuffer, bufferSize));

    ze_ipc_event_pool_handle_t eventPoolIpcHandle{};
    RUN_REQUIRED_STEP(getEventPoolIpcHandle(eventPool, eventPoolIpcHandle));

    ze_event_pool_handle_t eventPoolFromIpcHandle{};
    RUN_REQUIRED_STEP(openIpcHandleOfEventPool(context, eventPoolIpcHandle, eventPoolFromIpcHandle));
    RUN_REQUIRED_STEP(closeEventPoolFromIpcHandle(eventPoolFromIpcHandle));

    RUN_REQUIRED_STEP(destroyEvent(copyingFinishedEvent));
    RUN_REQUIRED_STEP(destroyEvent(intermediateBuffer2FilledEvent));
    RUN_REQUIRED_STEP(destroyEvent(intermediateBuffer1FilledEvent));
    RUN_REQUIRED_STEP(destroyEventPool(eventPool));

    RUN_REQUIRED_STEP(destroyCommandList(cmdList));
    RUN_REQUIRED_STEP(destroyCommandQueue(queue));

    RUN_REQUIRED_STEP(freeMemory(context, sourceBuffer));
    RUN_REQUIRED_STEP(freeMemory(context, intermediateBuffer1));
    RUN_REQUIRED_STEP(freeMemory(context, intermediateBuffer2));
    RUN_REQUIRED_STEP(freeMemory(context, destinationBuffer));

    RUN_REQUIRED_STEP(destroyContext(context));

    return 0;
}
