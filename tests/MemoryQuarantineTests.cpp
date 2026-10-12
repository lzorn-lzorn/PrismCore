#include <core/memory/Memory.hpp>
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <thread>
#include <vector>

TEST_CASE("Quarantine bounds retained records and drains concurrent frees", "[memory][quarantine]")
{
    if constexpr (!MemoryCompileEnableDebugGuards || !MemoryCompileEnableUseAfterFreeDetection)
    {
        SUCCEED("Quarantine is disabled in this configuration");
        return;
    }

    core::AllocatorConfig config;
    config.UseAfterFreeQuarantineBytes = 1024u * 1024u * 1024u;
    config.UseAfterFreeQuarantineMaxEntries = 1024u * 1024u;
    core::AllocatorFacade::configure(config);
    constexpr std::size_t blockSize = 64;
    constexpr std::size_t count = 4096;
    std::vector<void *> blocks;
    blocks.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        blocks.push_back(core::AllocatorFacade::allocate(blockSize));
    for (auto *block : blocks)
        core::AllocatorFacade::deallocate(block, blockSize);
    REQUIRE(core::AllocatorFacade::getStats().QuarantineEntryCount ==
        core::mem::MemoryFacadeQuarantineRingCapacity);
    core::AllocatorFacade::flushDeferredFrees();
    REQUIRE(core::AllocatorFacade::getStats().QuarantineEntryCount == 0);
    REQUIRE(core::AllocatorFacade::getStats().QuarantineBytes == 0);

    blocks.clear();
    for (std::size_t index = 0; index < count; ++index)
        blocks.push_back(core::AllocatorFacade::allocate(blockSize));
    std::atomic<bool> finished{false};
    std::thread flusher([&] {
        while (!finished.load(std::memory_order_acquire))
        {
            core::AllocatorFacade::flushDeferredFrees();
            std::this_thread::yield();
        }
        core::AllocatorFacade::onThreadExit();
    });
    std::vector<std::thread> workers;
    constexpr std::size_t workerCount = 4;
    for (std::size_t worker = 0; worker < workerCount; ++worker)
        workers.emplace_back([&, worker] {
            for (std::size_t index = worker; index < blocks.size(); index += workerCount)
                core::AllocatorFacade::deallocate(blocks[index], blockSize);
            core::AllocatorFacade::onThreadExit();
        });
    for (auto &worker : workers)
        worker.join();
    finished.store(true, std::memory_order_release);
    flusher.join();
    core::AllocatorFacade::flushDeferredFrees();
    REQUIRE(core::AllocatorFacade::getStats().QuarantineEntryCount == 0);
    REQUIRE(core::AllocatorFacade::getStats().QuarantineBytes == 0);
    core::AllocatorFacade::onThreadExit();
}