/*
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "l0_common_steps.h"

#include "shared/log.h"

#include <cstdint>

namespace Cal::Testing::Utils::LevelZero {

bool initL0() {
    const auto zeInitResult = zeInit(0);
    if (zeInitResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeInit() call has failed! Error code = %d", static_cast<int>(zeInitResult));
        return false;
    }

    return true;
}

bool getDrivers(std::vector<ze_driver_handle_t> &drivers) {
    uint32_t numDrivers{0};

    auto zeDriverGetResult = zeDriverGet(&numDrivers, nullptr);
    if (zeDriverGetResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeDriverGet() call has failed! Error code = %d", static_cast<int>(zeDriverGetResult));
        return false;
    }

    if (numDrivers == 0) {
        log<Verbosity::error>("zeDriverGet() returned drivers count equals 0!");
        return false;
    } else {
        log<Verbosity::info>("Number of supported drivers: %d", static_cast<int>(numDrivers));
    }

    log<Verbosity::info>("Calling zeDriverGet() to get drivers!");
    drivers.resize(numDrivers);

    zeDriverGetResult = zeDriverGet(&numDrivers, drivers.data());
    if (zeDriverGetResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeDriverGet() call has failed! Error code = %d", static_cast<int>(zeDriverGetResult));
        return false;
    }

    return !drivers.empty();
}

bool getDevices(ze_driver_handle_t driver, std::vector<ze_device_handle_t> &devices) {
    uint32_t numDevices{0};

    auto zeDeviceGetResult = zeDeviceGet(driver, &numDevices, nullptr);
    if (zeDeviceGetResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeDeviceGet() call has failed! Error code = %d", static_cast<int>(zeDeviceGetResult));
        return false;
    }

    if (numDevices == 0) {
        log<Verbosity::error>("zeDeviceGet() returned devices count equals 0!");
        return false;
    } else {
        log<Verbosity::info>("Number of available devices: %d", static_cast<int>(numDevices));
    }

    log<Verbosity::info>("Calling zeDeviceGet() to get devices!");
    devices.resize(numDevices);

    zeDeviceGetResult = zeDeviceGet(driver, &numDevices, devices.data());
    if (zeDeviceGetResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeDeviceGet() call has failed! Error code = %d", static_cast<int>(zeDeviceGetResult));
        return false;
    }

    return !devices.empty();
}

bool createContext(ze_driver_handle_t driver, ze_context_handle_t &context) {
    ze_context_desc_t contextDescription{};
    contextDescription.stype = ZE_STRUCTURE_TYPE_CONTEXT_DESC;

    const auto zeContextCreateResult = zeContextCreate(driver, &contextDescription, &context);
    if (zeContextCreateResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeContextCreate() call has failed! Error code = %d", static_cast<int>(zeContextCreateResult));
        return false;
    }

    log<Verbosity::info>("L0 context has been created! context = %p", static_cast<void *>(context));
    return true;
}

bool destroyContext(ze_context_handle_t &context) {
    const auto zeContextDestroyResult = zeContextDestroy(context);
    if (zeContextDestroyResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeContextDestroy() call has failed for context = %p! Error code = %d",
                              context, static_cast<int>(zeContextDestroyResult));
        return false;
    }

    context = nullptr;
    log<Verbosity::info>("L0 context has been destroyed!");

    return true;
}

bool getComputeQueueOrdinal(ze_device_handle_t device, uint32_t &ordinal) {
    log<Verbosity::info>("Getting command queue properties for device to get compute queue ordinal!");

    uint32_t queueGroupPropertiesCount{0};
    auto zeDeviceGetCommandQueueGroupPropertiesResult = zeDeviceGetCommandQueueGroupProperties(device, &queueGroupPropertiesCount, nullptr);
    if (zeDeviceGetCommandQueueGroupPropertiesResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeDeviceGetCommandQueueGroupProperties() call for getting count has failed! Error code = %d",
                              static_cast<int>(zeDeviceGetCommandQueueGroupPropertiesResult));
        return false;
    }

    std::vector<ze_command_queue_group_properties_t> queueGroupProperties{};
    queueGroupProperties.resize(queueGroupPropertiesCount);

    for (auto &queueGroupProperty : queueGroupProperties) {
        queueGroupProperty.stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_GROUP_PROPERTIES;
    }

    zeDeviceGetCommandQueueGroupPropertiesResult = zeDeviceGetCommandQueueGroupProperties(device, &queueGroupPropertiesCount, queueGroupProperties.data());
    if (zeDeviceGetCommandQueueGroupPropertiesResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeDeviceGetCommandQueueGroupProperties() call for getting properties has failed! Error code = %d",
                              static_cast<int>(zeDeviceGetCommandQueueGroupPropertiesResult));
        return false;
    }

    for (uint32_t i = 0; i < queueGroupPropertiesCount; ++i) {
        if (queueGroupProperties[i].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COMPUTE) {
            log<Verbosity::info>("Using ordinal = %d --> compute flag enabled", static_cast<int>(i));
            ordinal = i;

            return true;
        }
    }

    log<Verbosity::error>("Could not find the ordinal for compute!");
    return false;
}

bool createCommandQueue(ze_context_handle_t context, ze_device_handle_t device, uint32_t ordinal, ze_command_queue_handle_t &queue) {
    ze_command_queue_desc_t queueDescription{};

    queueDescription.stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC;
    queueDescription.mode = ZE_COMMAND_QUEUE_MODE_DEFAULT;
    queueDescription.priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDescription.ordinal = ordinal;

    ze_command_queue_handle_t commandQueue{};
    const auto zeCommandQueueCreateResult = zeCommandQueueCreate(context, device, &queueDescription, &queue);
    if (zeCommandQueueCreateResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeCommandQueueCreate() call has failed! Error code = %d", static_cast<int>(zeCommandQueueCreateResult));
        return false;
    }

    log<Verbosity::info>("Command queue created successfully! Handle = %p", static_cast<void *>(queue));
    return true;
}

bool executeCommandLists(ze_command_queue_handle_t queue, uint32_t numCmdLists, ze_command_list_handle_t *cmdLists, ze_fence_handle_t fence) {
    log<Verbosity::info>("Executing command list via zeCommandQueueExecuteCommandLists()!");

    const auto zeCommandQueueExecuteCommandListsResult = zeCommandQueueExecuteCommandLists(queue, numCmdLists, cmdLists, fence);
    if (zeCommandQueueExecuteCommandListsResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeCommandQueueExecuteCommandLists() call has failed! Error code = %d", static_cast<int>(zeCommandQueueExecuteCommandListsResult));
        return -1;
    }

    log<Verbosity::info>("Execution started!");
    return true;
}

bool synchronizeCommandQueue(ze_command_queue_handle_t queue) {
    log<Verbosity::info>("Waiting for finishing execution via zeCommandQueueSynchronize()!");

    const auto zeCommandQueueSynchronizeResult = zeCommandQueueSynchronize(queue, UINT64_MAX);
    if (zeCommandQueueSynchronizeResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeCommandQueueSynchronize() call has failed! Error code = %d", static_cast<int>(zeCommandQueueSynchronizeResult));
        return false;
    }

    log<Verbosity::info>("Execution finished!");
    return true;
}

bool destroyCommandQueue(ze_command_queue_handle_t &queue) {
    const auto zeCommandQueueDestroyResult = zeCommandQueueDestroy(queue);
    if (zeCommandQueueDestroyResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeCommandQueueDestroy() call has failed! Error code = %d", static_cast<int>(zeCommandQueueDestroyResult));
        return false;
    }

    queue = nullptr;
    log<Verbosity::info>("L0 command queue has been destroyed!");

    return true;
}

bool createCommandList(ze_context_handle_t context, ze_device_handle_t device, uint32_t ordinal, ze_command_list_handle_t &list) {
    ze_command_list_desc_t commandListDescription = {ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC, nullptr, ordinal, 0};
    const auto zeCommandListCreateResult = zeCommandListCreate(context, device, &commandListDescription, &list);
    if (zeCommandListCreateResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeCommandListCreate() call has failed! Error code = %d", static_cast<int>(zeCommandListCreateResult));
        return false;
    }

    log<Verbosity::info>("Command list created successfully! Handle = %p", static_cast<void *>(list));
    return true;
}

bool appendMemoryCopy(ze_command_list_handle_t cmdList,
                      void *destination,
                      const void *source,
                      size_t size,
                      ze_event_handle_t signalEvent,
                      uint32_t waitEventsCount,
                      ze_event_handle_t *waitEvents) {
    log<Verbosity::info>("Appending memory copy operation to command list!");

    const auto zeCommandListAppendMemoryCopyResult = zeCommandListAppendMemoryCopy(cmdList,
                                                                                   destination,
                                                                                   source,
                                                                                   size,
                                                                                   signalEvent,
                                                                                   waitEventsCount,
                                                                                   waitEvents);
    if (zeCommandListAppendMemoryCopyResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeCommandListAppendMemoryCopy() call has failed! Error code = %d", static_cast<int>(zeCommandListAppendMemoryCopyResult));
        return false;
    }

    log<Verbosity::info>("Command appended successfuly!");
    return true;
}

bool closeCommandList(ze_command_list_handle_t list) {
    log<Verbosity::info>("Closing command list via zeCommandListClose()!");

    const auto zeCommandListCloseResult = zeCommandListClose(list);
    if (zeCommandListCloseResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeCommandListClose() call has failed! Error code = %d", static_cast<int>(zeCommandListCloseResult));
        return false;
    }

    log<Verbosity::info>("Command list closed succesfully!");
    return true;
}

bool destroyCommandList(ze_command_list_handle_t &list) {
    const auto zeCommandListDestroyResult = zeCommandListDestroy(list);
    if (zeCommandListDestroyResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeCommandListDestroy() call has failed! Error code = %d", static_cast<int>(zeCommandListDestroyResult));
        return false;
    }

    list = nullptr;
    log<Verbosity::info>("L0 command list has been destroyed!");

    return true;
}

bool allocateHostMemory(ze_context_handle_t context, size_t bufferSize, size_t alignment, void *&usmHostBuffer) {
    ze_host_mem_alloc_desc_t hostMemAllocDesc = {ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC};

    const auto zeMemAllocHostResult = zeMemAllocHost(context, &hostMemAllocDesc, bufferSize, alignment, &usmHostBuffer);
    if (zeMemAllocHostResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeMemAllocHost() call has failed! Error code: %d", static_cast<int>(zeMemAllocHostResult));
        return false;
    }

    log<Verbosity::info>("Allocation has been successful!");
    return true;
}

bool freeMemory(ze_context_handle_t context, void *&buffer) {
    const auto zeMemFreeResult = zeMemFree(context, buffer);
    if (zeMemFreeResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeMemFree() call has failed! Error code: %d", static_cast<int>(zeMemFreeResult));
        return false;
    }

    buffer = nullptr;
    log<Verbosity::info>("Memory has been freed successfuly!");

    return true;
}

bool createEventPool(ze_context_handle_t context, uint32_t eventsCount, ze_device_handle_t *poolDevices, uint32_t devicesCount, ze_event_pool_handle_t &eventPool) {
    log<Verbosity::info>("Creating event pool via zeEventPoolCreate()!");

    ze_event_pool_desc_t eventPoolDescription{};
    eventPoolDescription.count = eventsCount;
    eventPoolDescription.flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE | ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP;

    const auto zeEventPoolCreateResult = zeEventPoolCreate(context, &eventPoolDescription, devicesCount, poolDevices, &eventPool);
    if (zeEventPoolCreateResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeEventPoolCreate() call has failed! Error code: %d", static_cast<int>(zeEventPoolCreateResult));
        return false;
    }

    log<Verbosity::info>("Event pool has been created successfully! Handle = %p", static_cast<void *>(eventPool));
    return true;
}

bool destroyEventPool(ze_event_pool_handle_t &eventPool) {
    log<Verbosity::info>("Destroying event pool via zeEventPoolDestroy()!");

    const auto zeEventPoolDestroyResult = zeEventPoolDestroy(eventPool);
    if (zeEventPoolDestroyResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeEventPoolDestroy() call has failed! Error code = %d", static_cast<int>(zeEventPoolDestroyResult));
        return false;
    }

    eventPool = nullptr;
    log<Verbosity::info>("Destruction of event pool has been successful!");

    return true;
}

bool createEvent(ze_event_pool_handle_t eventPool, uint32_t index, ze_event_handle_t &event) {
    log<Verbosity::info>("Creating an event via zeEventCreate()!");

    ze_event_desc_t eventDesc{};
    eventDesc.index = index;
    eventDesc.wait = ZE_EVENT_SCOPE_FLAG_HOST;

    const auto zeEventCreateResult = zeEventCreate(eventPool, &eventDesc, &event);
    if (zeEventCreateResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeEventCreate() call has failed! Error code = %d", static_cast<int>(zeEventCreateResult));
        return false;
    }

    log<Verbosity::info>("Event has been created successfully! Handle = %p", static_cast<void *>(event));
    return true;
}

bool destroyEvent(ze_event_handle_t &event) {
    log<Verbosity::info>("Destroying event via zeEventDestroy()!");

    const auto zeEventDestroyResult = zeEventDestroy(event);
    if (zeEventDestroyResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeEventDestroy() call has failed! Error code: %d", static_cast<int>(zeEventDestroyResult));
        return false;
    }

    event = nullptr;
    log<Verbosity::info>("Event destruction has been successful!");

    return true;
}

bool createFence(ze_command_queue_handle_t queue, ze_fence_handle_t &fence) {
    log<Verbosity::info>("Creating fence via zeFenceCreate()!");

    ze_fence_desc_t fenceDesc{};
    const auto zeFenceCreateResult = zeFenceCreate(queue, &fenceDesc, &fence);
    if (zeFenceCreateResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeFenceCreate() call has failed! Error code = %d", static_cast<int>(zeFenceCreateResult));
        return false;
    }

    log<Verbosity::info>("Successfully created a fence! Handle = %p", static_cast<void *>(fence));
    return true;
}

bool synchronizeViaFence(ze_fence_handle_t fence) {
    log<Verbosity::info>("Waiting for finish of command queue via zeFenceHostSynchronize()!");

    const auto zeFenceHostSynchronizeResult = zeFenceHostSynchronize(fence, UINT64_MAX);
    if (zeFenceHostSynchronizeResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeFenceHostSynchronize() call has failed! Error code = %d", static_cast<int>(zeFenceHostSynchronizeResult));
        return false;
    }

    log<Verbosity::info>("Execution finished!");
    return true;
}

bool destroyFence(ze_fence_handle_t &fence) {
    log<Verbosity::info>("Destroying fence via zeFenceDestroy()!");

    const auto zeFenceDestroyResult = zeFenceDestroy(fence);
    if (zeFenceDestroyResult != ZE_RESULT_SUCCESS) {
        log<Verbosity::error>("zeFenceDestroy() call has failed! Error code = %d", static_cast<int>(zeFenceDestroyResult));
        return false;
    }

    fence = nullptr;
    log<Verbosity::info>("Fence destruction has been successful!");

    return true;
}

} // namespace Cal::Testing::Utils::LevelZero
