#include <core/containers/MPMCQueue.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <atomic>
#include <thread>
#include <vector>

TEST_CASE("Memory queue preserves capacity and FIFO through wraparound", "[memory][queue]")
{
    core::MPMCQueue<int, 8> queue;
    static_assert(decltype(queue)::capacity() == 8);
    for (int round = 0; round < 100; ++round)
    {
        REQUIRE(queue.isEmpty());
        REQUIRE_FALSE(queue.tryPop());
        for (int value = 0; value < 8; ++value)
            REQUIRE(queue.tryPush(round * 8 + value));
        REQUIRE(queue.isFull());
        REQUIRE(queue.getSize() == 8);
        REQUIRE_FALSE(queue.tryPush(-1));
        std::array<int, 8> values{};
        REQUIRE(queue.tryPopBulk(values.begin(), values.size()) == values.size());
        for (int index = 0; index < 8; ++index)
            REQUIRE(values[index] == round * 8 + index);
        REQUIRE(queue.getSize() == 0);
    }
}

TEST_CASE("Memory queue transfers every record once with multiple consumers", "[memory][queue]")
{
    constexpr std::size_t producers = 4;
    constexpr std::size_t consumers = 4;
    constexpr std::size_t perProducer = 4000;
    constexpr std::size_t total = producers * perProducer;
    core::MPMCQueue<std::size_t, 64> queue;
    std::vector<std::atomic<unsigned>> seen(total);
    for (auto &count : seen)
        count.store(0);
    std::atomic<std::size_t> finishedProducers{0};
    std::atomic<std::size_t> received{0};
    std::atomic<bool> invalid{false};
    std::vector<std::thread> threads;

    for (std::size_t producer = 0; producer < producers; ++producer)
        threads.emplace_back([&, producer] {
            for (std::size_t index = 0; index < perProducer; ++index)
                while (!queue.tryPush(producer * perProducer + index))
                    std::this_thread::yield();
            finishedProducers.fetch_add(1, std::memory_order_release);
        });

    for (std::size_t consumer = 0; consumer < consumers; ++consumer)
        threads.emplace_back([&] {
            for (;;)
            {
                if (auto value = queue.tryPop())
                {
                    if (*value < total)
                        seen[*value].fetch_add(1, std::memory_order_relaxed);
                    else
                        invalid.store(true);
                    received.fetch_add(1, std::memory_order_relaxed);
                }
                else if (finishedProducers.load(std::memory_order_acquire) == producers)
                    break;
                else
                    std::this_thread::yield();
            }
        });

    for (auto &thread : threads)
        thread.join();
    REQUIRE_FALSE(invalid.load());
    REQUIRE(received.load() == total);
    REQUIRE(queue.isEmpty());
    for (const auto &count : seen)
        REQUIRE(count.load() == 1);
}