//
// Created by Lzorn on 2026/9/26.
//
// SeedAllocators 典型用法示例：
//   1. 统一分配入口（AllocatorFacade）
//   2. 生命周期提示（transient / persistent）
//   3. STL 容器适配（EngineAllocator）
//   4. PMR 容器适配（EngineMemoryResource）
//   5. 观测器（统计 / 泄漏检测）
//

#include <core/memory/MemoryProxy.hpp>

#include <cstdio>
#include <list>
#include <map>
#include <string>
#include <vector>

// 对外只使用 core 命名空间；core::mem 为分配器内部实现。
using namespace core;

// 演示：把统计观测器挂到事件总线上。
struct HudStatsObserver
{
    void onMemoryEvent(const MemoryEvent &event)
    {
        if (event.Type == MemoryEventType::Allocate)
        {
            ++allocations;
        }
    }

    int allocations = 0;
};

int main()
{
    // ---- 1. 统一分配入口 ------------------------------------------------
    void *block = AllocatorFacade::allocate(1024);
    std::printf("allocate(1024) -> %p\n", block);
    AllocatorFacade::deallocate(block, 1024);

    // 对齐 + 零初始化
    void *aligned = AllocatorFacade::allocate(4096, 64, 0, /*zero*/ true);
    AllocatorFacade::deallocate(aligned, 4096, 64);

    // ---- 2. 生命周期提示 ------------------------------------------------
    // transient：帧内/短生命周期对象，倾向快速复用路径。
    void *frameLocal = AllocatorFacade::allocateTransient(256);
    AllocatorFacade::deallocateTransient(frameLocal, 256);

    // persistent：长生命周期对象，绕过 TLS 缓存污染。
    void *resident = AllocatorFacade::allocatePersistent(512);
    AllocatorFacade::deallocatePersistent(resident, 512);

    // ---- 3. STL 容器适配 ------------------------------------------------
    std::vector<int, EngineAllocator<int>> numbers;
    for (int i = 0; i < 1000; ++i) numbers.push_back(i);

    std::map<int, std::string, std::less<>, EngineAllocator<std::pair<const int, std::string>>> table;
    table[42] = "answer";

    // ---- 4. PMR 容器适配 ------------------------------------------------
    EngineMemoryResource resource;
    std::pmr::vector<double> pmrNumbers(&resource);
    for (int i = 0; i < 1000; ++i) pmrNumbers.push_back(i * 0.5);

    // ---- 5. 观测器 ------------------------------------------------------
    HudStatsObserver hud;
    MemoryStatisticsObserver stats;
    AllocatorFacade::bindObserver(hud);
    AllocatorFacade::bindObserver(stats);

    void *observed = AllocatorFacade::allocate(128);
    AllocatorFacade::deallocate(observed, 128);

    const auto snap = stats.getSnapshot();
    std::printf("allocations(hud)=%d, allocate(events)=%llu\n",
                hud.allocations,
                static_cast<unsigned long long>(snap.AllocateCount));

    AllocatorFacade::unbindObserver(stats);
    AllocatorFacade::unbindObserver(hud);

    // 退出前刷新 deferred frees（UAF quarantine）。
    AllocatorFacade::flushDeferredFrees();
    AllocatorFacade::onThreadExit();
    std::printf("done\n");
    return 0;
}
