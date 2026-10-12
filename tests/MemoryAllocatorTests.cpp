//
// Created by Lzorn on 2026/9/26.
//
// SeedAllocators 鲁棒性测试套件。
//
// 覆盖：
//   - 底层数据结构（FreeList / MemSpan / RadixTreeMap / MPMCQueue / BootstrapArena）
//   - 分配器核心（小对象 / 大对象 / 对齐 / 零初始化 / 生命周期）
//   - 观测模块（统计 / 泄漏 / 事件总线）
//   - STL / PMR 集成
//   - 多线程并发压力
//

#include <core/memory/MemoryProxy.hpp>
#include <core/containers/MPMCQueue.hpp>
#include <core/memory/RadixTreeMap.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <list>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace core;
// core::mem 为分配器内部实现；本测试做白盒测试时通过别名显式引用内部类型。
namespace mem = core::mem;

// ---------------------------------------------------------------------------
// 极简测试框架
// ---------------------------------------------------------------------------
namespace
{
int g_failures   = 0;
int g_checks     = 0;

void check(bool condition, const char *expr, const char *file, int line)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        std::printf("  [FAIL] %s:%d  %s\n", file, line, expr);
    }
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

// 统计事件类型的观测器，用于断言 guard/UAF 等事件是否被发出。
struct RecordingObserver
{
    std::atomic<std::uint64_t> Allocate       = 0;
    std::atomic<std::uint64_t> Deallocate     = 0;
    std::atomic<std::uint64_t> GuardCorruption = 0;
    std::atomic<std::uint64_t> UseAfterFree   = 0;
    std::atomic<std::uint64_t> ValidationError = 0;

    void onMemoryEvent(const MemoryEvent &event)
    {
        switch (event.Type)
        {
            case MemoryEventType::Allocate: Allocate.fetch_add(1, std::memory_order_relaxed); break;
            case MemoryEventType::Deallocate: Deallocate.fetch_add(1, std::memory_order_relaxed); break;
            case MemoryEventType::GuardCorruption: GuardCorruption.fetch_add(1, std::memory_order_relaxed); break;
            case MemoryEventType::UseAfterFree: UseAfterFree.fetch_add(1, std::memory_order_relaxed); break;
            case MemoryEventType::ValidationError: ValidationError.fetch_add(1, std::memory_order_relaxed); break;
            default: break;
        }
    }
};

} // namespace

// ---------------------------------------------------------------------------
// 1. FreeList
// ---------------------------------------------------------------------------
static void testFreeList()
{
    std::printf("[test] FreeList\n");
    mem::FreeList list;
    list.setMaxCount(1024);

    CHECK(list.isEmpty());
    CHECK(list.getCount() == 0);

    // push / pop 顺序（LIFO）
    std::byte a[8], b[8], c[8];
    list.push(&a);
    list.push(&b);
    list.push(&c);
    CHECK(list.getCount() == 3);
    CHECK(list.pop() == &c);
    CHECK(list.pop() == &b);
    CHECK(list.pop() == &a);
    CHECK(list.isEmpty());
    CHECK(list.pop() == nullptr);

    // pushBatch / popBatch
    std::byte x0[8], x1[8], x2[8], x3[8];
    list.pushBatch(&x0, &x0, 1); // 单元素批
    void *tail = nullptr;
    void *head = list.popBatch(4, tail);
    CHECK(head == &x0 && tail == &x0);

    list.clear();
    CHECK(list.isEmpty());
    CHECK(list.getCount() == 0);
}

// ---------------------------------------------------------------------------
// 2. MemSpan
// ---------------------------------------------------------------------------
static void testMemSpan()
{
    std::printf("[test] MemSpan\n");
    constexpr std::uint32_t BlockSize  = 32;
    constexpr std::uint16_t TotalBlocks = 16;

    std::vector<std::byte> arena(static_cast<std::size_t>(BlockSize) * TotalBlocks);
    mem::PageSpan backing;
    backing.Base       = arena.data();
    backing.PageCount  = 1;
    backing.PageSize   = 4096;

    mem::MemSpan span;
    span.setup(BlockSize, TotalBlocks, 3, 0, arena.data(), backing);

    CHECK(span.getFreeBlocks() == TotalBlocks);
    CHECK(!span.isFull());
    CHECK(span.isEmpty());

    void *blocks[TotalBlocks];
    for (int i = 0; i < TotalBlocks; ++i)
    {
        blocks[i] = span.popBlock();
        CHECK(blocks[i] != nullptr);
    }
    CHECK(span.isFull());
    CHECK(span.popBlock() == nullptr);

    for (int i = 0; i < TotalBlocks; ++i)
    {
        span.pushBlock(blocks[i]);
    }
    CHECK(span.isEmpty());
    CHECK(span.getFreeBlocks() == TotalBlocks);
}

// ---------------------------------------------------------------------------
// 3. RadixTreeMap
// ---------------------------------------------------------------------------
static void testRadixTreeMap()
{
    std::printf("[test] RadixTreeMap\n");
    struct Node { int value; };
    Node n1{1}, n2{2};

    using TestMap = mem::RadixTreeMap<Node *, 0, 8, 8, 8, 16, 32>;
    TestMap map;

    const auto r1 = map.insertOrAssign(0x00001000, &n1);
    CHECK(r1 == TestMap::InsertResult::Inserted);
    CHECK(map.find(0x00001000) == &n1);

    // 覆盖写
    const auto r2 = map.insertOrAssign(0x00001000, &n2);
    CHECK(r2 == TestMap::InsertResult::Replaced);
    CHECK(map.find(0x00001000) == &n2);

    // 不存在
    CHECK(map.find(0xdead0000) == nullptr);

    // 键越界（8+8+8=24 位）
    const auto r3 = map.insertOrAssign(0x01000000, &n1);
    CHECK(r3 == TestMap::InsertResult::OutOfRange);

    // erase
    CHECK(map.erase(0x00001000));
    CHECK(map.find(0x00001000) == nullptr);
    CHECK(!map.erase(0x00001000));
}

// ---------------------------------------------------------------------------
// 4. MPMCQueue
// ---------------------------------------------------------------------------
static void testMPMCQueue()
{
    std::printf("[test] MPMCQueue\n");
    MPMCQueue<int, 64> queue;

    CHECK(queue.isEmpty());
    CHECK(!queue.tryPop().has_value());

    for (int i = 0; i < 32; ++i)
    {
        CHECK(queue.tryPush(i));
    }
    CHECK(queue.getSize() == 32);

    for (int i = 0; i < 32; ++i)
    {
        auto v = queue.tryPop();
        CHECK(v.has_value());
        if (v) CHECK(*v == i);
    }
    CHECK(queue.isEmpty());

    // 多线程 MPSC/MPMC 冒烟
    MPMCQueue<int, 1024> mq;
    std::atomic<std::uint64_t> sum{0};
    constexpr int Producers = 4;
    constexpr int PerProducer = 2000;

    std::vector<std::thread> threads;
    for (int p = 0; p < Producers; ++p)
    {
        threads.emplace_back([&mq, p]()
        {
            for (int i = 0; i < PerProducer; ++i)
            {
                while (!mq.tryPush(p * PerProducer + i)) { core::relax(); }
            }
        });
    }
    threads.emplace_back([&mq, &sum]()
    {
        int got = 0;
        while (got < Producers * PerProducer)
        {
            auto v = mq.tryPop();
            if (v) { sum.fetch_add(static_cast<std::uint64_t>(*v), std::memory_order_relaxed); ++got; }
            else { core::relax(); }
        }
    });
    for (auto &t : threads) t.join();

    // 求和校验
    std::uint64_t expected = 0;
    for (int p = 0; p < Producers; ++p)
        for (int i = 0; i < PerProducer; ++i)
            expected += static_cast<std::uint64_t>(p * PerProducer + i);
    CHECK(sum.load() == expected);
}

// ---------------------------------------------------------------------------
// 5. 分配器核心：基础分配 / 释放
// ---------------------------------------------------------------------------
static void testBasicAllocation()
{
    std::printf("[test] basic allocation\n");
    const std::size_t sizes[] = {
        1, 7, 8, 15, 16, 24, 31, 32, 40, 48, 56, 63, 64, 80, 96, 112, 128,
        160, 192, 224, 256, 320, 384, 448, 512, 1024, 2048, 4096, 8192,
        16384, 32768, 65536, 131072, 200000, 262144
    };

    for (std::size_t size : sizes)
    {
        void *p = AllocatorFacade::allocate(size);
        CHECK(p != nullptr);
        if (p)
        {
            std::memset(p, 0xAB, size);
            AllocatorFacade::deallocate(p, size);
        }
    }

    // 零大小
    void *z = AllocatorFacade::allocate(0);
    CHECK(z != nullptr);
    AllocatorFacade::deallocate(z, 0);

    // nullptr 释放是安全的
    AllocatorFacade::deallocate(nullptr, 16);

    AllocatorFacade::flushDeferredFrees();
}

// ---------------------------------------------------------------------------
// 6. 对齐分配
// ---------------------------------------------------------------------------
static void testAlignment()
{
    std::printf("[test] alignment\n");
    const std::size_t aligns[] = {8, 16, 32, 64, 128, 256, 512, 1024, 4096};

    for (std::size_t align : aligns)
    {
        void *p = AllocatorFacade::allocate(128, align);
        CHECK(p != nullptr);
        if (p)
        {
            CHECK((reinterpret_cast<std::uintptr_t>(p) % align) == 0);
            AllocatorFacade::deallocate(p, 128, align);
        }
    }

    // 高对齐大对象走 dedicated 路径
    void *p = AllocatorFacade::allocate(64, 8192);
    CHECK(p != nullptr);
    if (p)
    {
        CHECK((reinterpret_cast<std::uintptr_t>(p) % 8192) == 0);
        AllocatorFacade::deallocate(p, 64, 8192);
    }

    AllocatorFacade::flushDeferredFrees();
}

// ---------------------------------------------------------------------------
// 7. 零初始化
// ---------------------------------------------------------------------------
static void testZeroMemory()
{
    std::printf("[test] zero memory\n");
    void *p = AllocatorFacade::allocate(4096, alignof(std::max_align_t), 0, /*zero*/ true);
    CHECK(p != nullptr);
    if (p)
    {
        const auto *bytes = static_cast<const unsigned char *>(p);
        bool allZero = true;
        for (std::size_t i = 0; i < 4096; ++i)
            if (bytes[i] != 0) { allZero = false; break; }
        CHECK(allZero);
        AllocatorFacade::deallocate(p, 4096);
    }
    AllocatorFacade::flushDeferredFrees();
}

// ---------------------------------------------------------------------------
// 8. 大对象（dedicated 路径）
// ---------------------------------------------------------------------------
static void testLargeAllocation()
{
    std::printf("[test] large allocation\n");
    const std::size_t sizes[] = {300 * 1024, 512 * 1024, 1024 * 1024, 4 * 1024 * 1024};

    for (std::size_t size : sizes)
    {
        void *p = AllocatorFacade::allocate(size);
        CHECK(p != nullptr);
        if (p)
        {
            std::memset(p, 0xCD, size);
            AllocatorFacade::deallocate(p, size);
        }
    }
    AllocatorFacade::flushDeferredFrees();
}

// ---------------------------------------------------------------------------
// 9. 生命周期（transient / persistent）
// ---------------------------------------------------------------------------
static void testLifetime()
{
    std::printf("[test] lifetime\n");
    for (int round = 0; round < 100; ++round)
    {
        void *t = AllocatorFacade::allocateTransient(256);
        void *p = AllocatorFacade::allocatePersistent(256);
        CHECK(t != nullptr && p != nullptr);
        if (t) AllocatorFacade::deallocateTransient(t, 256);
        if (p) AllocatorFacade::deallocatePersistent(p, 256);
    }

    // 大对象 transient 走 dedicated cache
    void *t = AllocatorFacade::allocateTransient(2 * 1024 * 1024);
    CHECK(t != nullptr);
    if (t) AllocatorFacade::deallocateTransient(t, 2 * 1024 * 1024);

    AllocatorFacade::flushDeferredFrees();
}

// ---------------------------------------------------------------------------
// 10. 事件总线与观测器
// ---------------------------------------------------------------------------
static void testObservers()
{
    std::printf("[test] observers\n");

    RecordingObserver recorder;
    MemoryStatisticsObserver stats;
    MemoryLeakDetectorObserver leak;

    AllocatorFacade::bindObserver(recorder);
    AllocatorFacade::bindObserver(stats);
    AllocatorFacade::bindObserver(leak);

    void *p = AllocatorFacade::allocate(128);
    CHECK(p != nullptr);
    AllocatorFacade::deallocate(p, 128);
    AllocatorFacade::flushDeferredFrees();

    // 事件总线应至少产生一次 allocate / deallocate
    CHECK(recorder.Allocate.load() >= 1);
    CHECK(recorder.Deallocate.load() >= 1);

    // 泄漏检测器应保持平衡（flush 后无残留）
    CHECK(leak.isEmpty());

    // 统计快照：分配数与释放数一致
    const auto snap = stats.getSnapshot();
    CHECK(snap.AllocateCount == snap.DeallocateCount);

    AllocatorFacade::unbindObserver(leak);
    AllocatorFacade::unbindObserver(stats);
    AllocatorFacade::unbindObserver(recorder);
}

// ---------------------------------------------------------------------------
// 11. Guard 破坏检测（Debug 守卫）
// ---------------------------------------------------------------------------
static void testGuardDetection()
{
    std::printf("[test] guard corruption detection\n");
    if constexpr (!MemoryCompileEnableDebugGuards)
    {
        std::printf("  (skipped: debug guards disabled)\n");
        return;
    }

    RecordingObserver recorder;
    AllocatorFacade::bindObserver(recorder);

    void *p = AllocatorFacade::allocate(64);
    CHECK(p != nullptr);
    if (p)
    {
        // 向后写越界，破坏 back guard
        auto *bytes = static_cast<unsigned char *>(p);
        for (std::size_t i = 64; i < 64 + 16; ++i) bytes[i] = 0x00;
        AllocatorFacade::deallocate(p, 64);
    }

    AllocatorFacade::flushDeferredFrees();
    CHECK(recorder.GuardCorruption.load() >= 1);
    AllocatorFacade::unbindObserver(recorder);
}

// ---------------------------------------------------------------------------
// 12. STL 集成（EngineAllocator / PMR）
// ---------------------------------------------------------------------------
static void testStlIntegration()
{
    std::printf("[test] STL integration\n");

    // EngineAllocator 适配多种标准容器
    std::vector<int, EngineAllocator<int>> vec;
    for (int i = 0; i < 100000; ++i) vec.push_back(i);
    std::int64_t sum = 0;
    for (int v : vec) sum += v;
    CHECK(sum == 4999950000LL);

    std::list<int, EngineAllocator<int>> list;
    for (int i = 0; i < 10000; ++i) list.push_back(i);
    CHECK(list.size() == 10000);

    std::map<int, int, std::less<>, EngineAllocator<std::pair<const int, int>>> map;
    for (int i = 0; i < 10000; ++i) map[i] = i * 2;
    CHECK(map.size() == 10000);
    CHECK(map[42] == 84);

    std::unordered_map<int, int, std::hash<int>, std::equal_to<>,
                       EngineAllocator<std::pair<const int, int>>> umap;
    for (int i = 0; i < 10000; ++i) umap[i] = i;
    CHECK(umap.size() == 10000);

    std::basic_string<char, std::char_traits<char>, EngineAllocator<char>> str;
    str = "hello allocator";
    CHECK(str == "hello allocator");

    // PMR 资源适配多种容器
    EngineMemoryResource resource;
    std::pmr::vector<int> pmrVec(&resource);
    for (int i = 0; i < 10000; ++i) pmrVec.push_back(i);
    CHECK(pmrVec.size() == 10000);

    std::pmr::list<int> pmrList(&resource);
    for (int i = 0; i < 1000; ++i) pmrList.push_back(i);
    CHECK(pmrList.size() == 1000);

    std::pmr::string pmrStr("pmr string", &resource);
    CHECK(pmrStr == "pmr string");

    std::pmr::unordered_map<int, int> pmrMap(&resource);
    for (int i = 0; i < 1000; ++i) pmrMap[i] = i;
    CHECK(pmrMap.size() == 1000);

    // 相等性：同一资源应相等，不同资源不等。
    EngineMemoryResource other;
    CHECK(resource == resource);
    CHECK(resource != other);

    AllocatorFacade::flushDeferredFrees();
}

// ---------------------------------------------------------------------------
// 13. 多线程并发压力
// ---------------------------------------------------------------------------
static void testMultithreadedStress()
{
    std::printf("[test] multithreaded stress\n");
    constexpr int Threads    = 8;
    constexpr int Iterations = 20000;

    std::atomic<bool> failed{false};
    std::vector<std::thread> workers;

    for (int t = 0; t < Threads; ++t)
    {
        workers.emplace_back([t, &failed]()
        {
            std::mt19937 rng(static_cast<std::uint32_t>(t) + 0x9e3779b9u);
            std::uniform_int_distribution<std::size_t> sizeDist(1, 8192);

            // 记录 (指针, 大小)，保证释放时传入与分配一致的 size。
            std::vector<std::pair<void *, std::size_t>> batch;
            batch.reserve(256);

            for (int i = 0; i < Iterations; ++i)
            {
                const std::size_t size = sizeDist(rng);
                void *p = AllocatorFacade::allocate(size);
                if (!p) { failed.store(true); return; }

                std::memset(p, 0x5A, size);
                if (batch.size() >= 256)
                {
                    for (auto &[b, s] : batch) AllocatorFacade::deallocate(b, s);
                    batch.clear();
                }
                else
                {
                    batch.emplace_back(p, size);
                }
            }

            for (auto &[b, s] : batch) AllocatorFacade::deallocate(b, s);
        });
    }

    for (auto &w : workers) w.join();
    CHECK(!failed.load());

    AllocatorFacade::flushDeferredFrees();
}

// ---------------------------------------------------------------------------
// 14. 运行时状态查询
// ---------------------------------------------------------------------------
static void testRuntimeStats()
{
    std::printf("[test] runtime stats\n");
    const auto stats = AllocatorFacade::getStats();
    // 仅验证接口可调用且不崩溃
    CHECK(stats.PageToSpanEntryCount >= 0);
    CHECK(stats.QuarantineEntryCount >= 0);
}

// ---------------------------------------------------------------------------
// 15. 预分配预热 API
// ---------------------------------------------------------------------------
static void testPrewarm()
{
    std::printf("[test] prewarm\n");
    std::vector<MemoryPrewarmRequest> requests;
    MemoryPrewarmRequest req;
    req.Size      = 128;
    req.Alignment = 16;
    req.Count     = 512;
    requests.push_back(req);

    const std::size_t warmed = AllocatorFacade::prewarm(requests);
    CHECK(warmed == 512);
}

// ---------------------------------------------------------------------------
// 16. 跨线程释放（生产者/消费者）
// ---------------------------------------------------------------------------
static void testCrossThreadFree()
{
    std::printf("[test] cross-thread free\n");
    constexpr int Threads    = 4;
    constexpr int Iterations = 20000;

    std::atomic<bool> failed{false};
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // 每个生产者独占一个无锁队列交给对应消费者释放。
    std::vector<MPMCQueue<void *, 8192>> queues(Threads);

    for (int t = 0; t < Threads; ++t)
    {
        producers.emplace_back([t, &queues, &failed]()
        {
            for (int i = 0; i < Iterations; ++i)
            {
                void *p = AllocatorFacade::allocate(256);
                if (!p) { failed.store(true); return; }
                while (!queues[t].tryPush(p)) { core::relax(); }
            }
        });

        consumers.emplace_back([t, &queues, &failed]()
        {
            for (int i = 0; i < Iterations; ++i)
            {
                auto p = queues[t].tryPop();
                while (!p) { p = queues[t].tryPop(); core::relax(); }
                AllocatorFacade::deallocate(*p, 256);
            }
        });
    }

    for (auto &t : producers) t.join();
    for (auto &t : consumers) t.join();
    CHECK(!failed.load());

    AllocatorFacade::flushDeferredFrees();
}

// ---------------------------------------------------------------------------
// 17. Use-After-Free 检测（窗口化）
// ---------------------------------------------------------------------------
static void testUseAfterFreeDetection()
{
    std::printf("[test] use-after-free detection\n");
    if constexpr (!MemoryCompileEnableUseAfterFreeDetection || !MemoryCompileEnableDebugGuards)
    {
        std::printf("  (skipped: UAF detection disabled)\n");
        return;
    }

    RecordingObserver recorder;
    AllocatorFacade::bindObserver(recorder);

    void *p = AllocatorFacade::allocate(64);
    CHECK(p != nullptr);
    AllocatorFacade::deallocate(p, 64);

    // 释放后写入（模拟 UAF），破坏 FreedPattern。
    auto *volatile bytes = static_cast<unsigned char *>(p);
    for (std::size_t i = 0; i < 64; ++i) bytes[i] = 0xEF;

    AllocatorFacade::flushDeferredFrees();
    CHECK(recorder.UseAfterFree.load() >= 1);

    AllocatorFacade::unbindObserver(recorder);
}

// ---------------------------------------------------------------------------
// 18. 错误 size 释放的鲁棒性（不应崩溃，debug 下应上报损坏）
// ---------------------------------------------------------------------------
static void testMismatchedFreeRobustness()
{
    std::printf("[test] mismatched free robustness\n");
    RecordingObserver recorder;
    AllocatorFacade::bindObserver(recorder);

    void *p = AllocatorFacade::allocate(64);
    CHECK(p != nullptr);
    // 故意用错误的 size 释放：不应崩溃。
    AllocatorFacade::deallocate(p, 128);

    AllocatorFacade::flushDeferredFrees();
    if constexpr (MemoryCompileEnableDebugGuards)
    {
        CHECK(recorder.GuardCorruption.load() >= 1);
    }

    AllocatorFacade::unbindObserver(recorder);
}

// ---------------------------------------------------------------------------
// 19. 页分配器参数（不同 SmallRegionReserveBytes）
// ---------------------------------------------------------------------------
static void testPageAllocatorParameters()
{
    std::printf("[test] page allocator parameters\n");
    for (const size_t reserve : {size_t(1) * 1024 * 1024, size_t(4) * 1024 * 1024, size_t(16) * 1024 * 1024})
    {
        AllocatorConfig cfg;
        cfg.SmallRegionReserveBytes = reserve;

        mem::PageAllocator pa(cfg);
        CHECK(pa.getPageSize() > 0);

        const auto s1 = pa.acquireSpan(1);
        const auto s2 = pa.acquireSpan(8);
        CHECK(s1.isValid());
        CHECK(s2.isValid());
        CHECK(s2.PageCount == 8);

        if (s1.isValid()) pa.releaseSpan(s1);
        if (s2.isValid()) pa.releaseSpan(s2);

        // 释放后应能从缓存再次取回
        const auto s3 = pa.acquireSpan(1);
        CHECK(s3.isValid());
        if (s3.isValid()) pa.releaseSpan(s3);
    }
}

// ---------------------------------------------------------------------------
// 20. 中央池参数（全 size class 的 fetch/return）
// ---------------------------------------------------------------------------
static void testCentralPoolParameters()
{
    std::printf("[test] central pool parameters\n");
    AllocatorConfig cfg;
    cfg.SmallRegionReserveBytes = 8 * 1024 * 1024;

    mem::PageAllocator pa(cfg);
    mem::CentralPool cp(pa);

    for (uint16_t cls = 0; cls < mem::NumSizeClasses; ++cls)
    {
        const size_t blockSize = mem::classIndexToSize(cls);
        const mem::SizeClass info = cp.getSizeClassInfo(cls);
        CHECK(info.BlockSize == blockSize);
        CHECK(info.BlocksPerSpan > 0);
        CHECK(info.PagesPerSpan > 0);

        void *batch[64]{};
        const uint32_t n = cp.fetchBatch(cls, batch, 64);
        CHECK(n > 0 && n <= 64);

        // 块地址应互不相同
        for (uint32_t i = 0; i < n; ++i)
        {
            CHECK(batch[i] != nullptr);
            for (uint32_t j = i + 1; j < n; ++j)
            {
                CHECK(batch[i] != batch[j]);
            }
        }

        // 串联成侵入式链后归还
        if (n > 0)
        {
            for (uint32_t i = 0; i + 1 < n; ++i)
            {
                *reinterpret_cast<void **>(batch[i]) = batch[i + 1];
            }
            *reinterpret_cast<void **>(batch[n - 1]) = nullptr;
            cp.returnBatch(cls, batch[0], n);
        }
    }
}

// ---------------------------------------------------------------------------
// 21. 线程缓存参数（小预算强制 spill/trim）
// ---------------------------------------------------------------------------
static void testThreadCacheParameters()
{
    std::printf("[test] thread cache parameters\n");
    AllocatorConfig cfg;
    cfg.SmallRegionReserveBytes = 4 * 1024 * 1024;
    cfg.ThreadCacheMaxBytes     = 64 * 1024;  // 小预算
    cfg.RefillBatchSize         = 16;

    mem::PageAllocator pa(cfg);
    mem::CentralPool cp(pa);
    mem::ThreadCache tc(cp, cfg);

    constexpr uint16_t Class   = 7; // 64 字节
    const size_t blockSize     = mem::classIndexToSize(Class);
    CHECK(blockSize == 64);

    std::vector<void *> blocks;
    blocks.reserve(10000);
    for (int i = 0; i < 10000; ++i)
    {
        const auto a = tc.allocate(Class, blockSize);
        CHECK(a.Block != nullptr);
        if (a.Block) blocks.push_back(a.Block);
    }

    for (void *b : blocks)
    {
        tc.deallocate(Class, blockSize, b);
    }

    tc.flushAll();
    CHECK(tc.getCurrentBytes() == 0);
    CHECK(tc.getDeferredBytes() == 0);
}

// ---------------------------------------------------------------------------
// 22. MemoryEventBus
// ---------------------------------------------------------------------------
static void testMemoryEventBus()
{
    std::printf("[test] memory event bus\n");

    MemoryEventBus bus;
    CHECK(!bus.hasObservers());

    struct MockObserver
    {
        int Count = 0;
        void onMemoryEvent(const MemoryEvent &) { ++Count; }
    };

    MockObserver a;
    MockObserver b;
    bus.bind(a);
    CHECK(bus.hasObservers());
    bus.bind(b);
    CHECK(bus.hasObservers());

    MemoryEvent ev;
    bus.emit(ev);
    CHECK(a.Count == 1);
    CHECK(b.Count == 1);

    bus.unbind(a);
    bus.emit(ev);
    CHECK(a.Count == 1);
    CHECK(b.Count == 2);

    bus.unbind(b);
    CHECK(!bus.hasObservers());
    bus.emit(ev);  // 无观察者时安全
    CHECK(a.Count == 1);
}

// ---------------------------------------------------------------------------
// 23. MemoryStatisticsObserver（计数器 + 峰值 + 下溢钳制）
// ---------------------------------------------------------------------------
static void testMemoryStatisticsObserver()
{
    std::printf("[test] memory statistics observer\n");

    MemoryStatisticsObserver obs;
    MemoryEvent ev;

    ev.Type = MemoryEventType::Allocate;
    ev.Size = 100;
    obs.onMemoryEvent(ev);

    ev.Type = MemoryEventType::ThreadCacheHit;
    obs.onMemoryEvent(ev);

    ev.Type = MemoryEventType::Deallocate;
    ev.Size = 100;
    obs.onMemoryEvent(ev);

    auto snap = obs.getSnapshot();
    CHECK(snap.AllocateCount == 1);
    CHECK(snap.DeallocateCount == 1);
    CHECK(snap.ThreadCacheHitCount == 1);
    CHECK(snap.CurrentBytes == 0);
    CHECK(snap.PeakBytes == 100);

    // 下溢钳制：释放一个未分配的 size，CurrentBytes 应钳制到 0 而非回绕。
    ev.Type = MemoryEventType::Deallocate;
    ev.Size = 500;
    obs.onMemoryEvent(ev);
    snap = obs.getSnapshot();
    CHECK(snap.CurrentBytes == 0);
    CHECK(snap.DeallocateCount == 2);
}

// ---------------------------------------------------------------------------
// 24. MemoryLeakDetectorObserver
// ---------------------------------------------------------------------------
static void testMemoryLeakDetectorObserver()
{
    std::printf("[test] memory leak detector\n");

    MemoryLeakDetectorObserver leak;
    CHECK(leak.isEmpty());

    MemoryEvent ev;
    ev.Type = MemoryEventType::Allocate;
    ev.UserPtr = reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x1000));
    ev.Size = 64;
    leak.onMemoryEvent(ev);
    CHECK(!leak.isEmpty());
    CHECK(leak.getLeakCount() == 1);

    ev.Type = MemoryEventType::Deallocate;
    leak.onMemoryEvent(ev);
    CHECK(leak.isEmpty());

    // 多个泄漏
    for (int i = 0; i < 3; ++i)
    {
        ev.Type = MemoryEventType::Allocate;
        ev.UserPtr = reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x2000 + i));
        leak.onMemoryEvent(ev);
    }
    CHECK(leak.getLeakCount() == 3);

    const auto snap = leak.getLeakSnapshot();
    CHECK(snap.size() == 3);

    std::ostringstream out;
    CHECK(leak.reportLeaks(out));
    CHECK(out.str().find("unresolved allocations=3") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 25. MemoryTracker（引擎事件 / hook 事件 / 问题检测 / JSON 报告）
// ---------------------------------------------------------------------------
static void testMemoryTracker()
{
    std::printf("[test] memory tracker\n");
    if constexpr (!MemoryTracker::isCompiledEnabled())
    {
        std::printf("  (skipped: tracker disabled)\n");
        return;
    }

    MemoryTracker &tracker = MemoryTracker::global();
    tracker.clear();
    MemoryTracker::setHookEnabled(true);
    MemoryTracker::setPayloadPreviewBytes(0); // 使用伪造指针，禁用 payload 读取

    // 1. 引擎事件
    MemoryEvent ev;
    ev.Type = MemoryEventType::Allocate;
    ev.UserPtr = reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x1000));
    ev.Size = 64;
    ev.Alignment = 16;
    ev.AllocationId = 1;
    tracker.onMemoryEvent(ev);

    ev.Type = MemoryEventType::Deallocate;
    tracker.onMemoryEvent(ev);

    MemoryTrackerStatistics s = tracker.getStatistics();
    CHECK(s.AllocateCount == 1);
    CHECK(s.DeallocateCount == 1);

    // 2. hook 正常分配/释放
    MemoryTracker::onHookAllocation(HookAllocationOp::Malloc,
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x2000)), 128,
        MemoryTrackerUnknownSize, nullptr);
    MemoryTracker::onHookFree(HookAllocationOp::Free,
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x2000)),
        MemoryTrackerUnknownSize, MemoryTrackerUnknownSize, nullptr);
    s = tracker.getStatistics();
    CHECK(s.HookAllocationCount >= 1);
    CHECK(s.HookFreeCount >= 1);

    // 3. double free
    MemoryTracker::onHookAllocation(HookAllocationOp::Malloc,
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x3000)), 64,
        MemoryTrackerUnknownSize, nullptr);
    MemoryTracker::onHookFree(HookAllocationOp::Free,
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x3000)),
        MemoryTrackerUnknownSize, MemoryTrackerUnknownSize, nullptr);
    MemoryTracker::onHookFree(HookAllocationOp::Free,
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x3000)),
        MemoryTrackerUnknownSize, MemoryTrackerUnknownSize, nullptr);
    s = tracker.getStatistics();
    CHECK(s.DoubleFreeCount >= 1);

    // 4. invalid free
    MemoryTracker::onHookFree(HookAllocationOp::Free,
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x9999)),
        MemoryTrackerUnknownSize, MemoryTrackerUnknownSize, nullptr);
    s = tracker.getStatistics();
    CHECK(s.InvalidFreeCount >= 1);

    // 5. mismatched free function（new 之后用 free）
    MemoryTracker::onHookAllocation(HookAllocationOp::New,
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x4000)), 64,
        MemoryTrackerUnknownSize, nullptr);
    MemoryTracker::onHookFree(HookAllocationOp::Free,
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x4000)),
        MemoryTrackerUnknownSize, MemoryTrackerUnknownSize, nullptr);
    s = tracker.getStatistics();
    CHECK(s.MismatchedFreeFunctionCount >= 1);

    // 6. size mismatch
    MemoryTracker::onHookAllocation(HookAllocationOp::Malloc,
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x5000)), 100,
        MemoryTrackerUnknownSize, nullptr);
    MemoryTracker::onHookFree(HookAllocationOp::Free,
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x5000)), 200,
        MemoryTrackerUnknownSize, nullptr);
    s = tracker.getStatistics();
    CHECK(s.SizeMismatchCount >= 1);

    // 7. 报告 + JSON 文件
    const MemoryTrackerReport report = tracker.getReport();
    CHECK(report.Issues.size() >= 4);

    const char *path = "seed_allocators_memory_report.json";
    CHECK(tracker.flushToFile(path));
    {
        std::ifstream file(path);
        CHECK(file.good());
        file.seekg(0, std::ios::end);
        CHECK(file.tellg() > 0);
    }
    std::remove(path);

    // 8. clear 重置全部状态
    tracker.clear();
    s = tracker.getStatistics();
    CHECK(s.EngineEventCount == 0);
    CHECK(s.IssueCount == 0);
    CHECK(s.DoubleFreeCount == 0);
    CHECK(s.InvalidFreeCount == 0);

    MemoryTracker::setHookEnabled(false);
    tracker.clear();
}

// ---------------------------------------------------------------------------
int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== SeedAllocators test suite ===\n");

    testFreeList();
    testMemSpan();
    testRadixTreeMap();
    testMPMCQueue();
    testBasicAllocation();
    testAlignment();
    testZeroMemory();
    testLargeAllocation();
    testLifetime();
    testObservers();
    testGuardDetection();
    testStlIntegration();
    testMultithreadedStress();
    testRuntimeStats();
    testPrewarm();
    testCrossThreadFree();
    testUseAfterFreeDetection();
    testMismatchedFreeRobustness();
    testPageAllocatorParameters();
    testCentralPoolParameters();
    testThreadCacheParameters();
    testMemoryEventBus();
    testMemoryStatisticsObserver();
    testMemoryLeakDetectorObserver();
    testMemoryTracker();

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
