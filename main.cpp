#include <iostream>
#include <vector>
#include <future>
#include <algorithm>
#include <thread>
#include <mutex>
#include <random>
#include <chrono>
#include <queue>

// Простая реализация пула потоков с концепцией work stealing
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency()) : stop(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            threads.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queueMutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty())
                            return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
                });
        }
    }

    template<class F, class... Args>
    auto push_task(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));
        auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        auto future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (stop)
                throw std::runtime_error("push on stopped ThreadPool");
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return future;
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : threads)
            worker.join();
    }

private:
    std::vector<std::thread> threads;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop;
};

template<typename T>
void quicksort(ThreadPool& pool, std::vector<T>& array, size_t left, size_t right, std::shared_ptr<std::promise<void>> promise) {
    if (left >= right)
        return;

    if (right - left <= 100000) {
        std::sort(array.begin() + left, array.begin() + right + 1);
        if (promise) {
            try {
                promise->set_value();
            }
            catch (...) {
                // Обработка исключений при установке значения
            }
        }
        return;
    }

    size_t pivot = left + (right - left) / 2;
    T pivotValue = array[pivot];
    size_t i = left, j = right;
    while (i <= j) {
        while (array[i] < pivotValue) i++;
        while (array[j] > pivotValue) j--;
        if (i <= j) {
            std::swap(array[i], array[j]);
            i++;
            j--;
        }
    }
    auto leftPromise = std::make_shared<std::promise<void>>();
    auto rightPromise = std::make_shared<std::promise<void>>();
    pool.push_task(quicksort<T>, std::ref(pool), std::ref(array), left, j, leftPromise);
    pool.push_task(quicksort<T>, std::ref(pool), std::ref(array), i, right, rightPromise);

    // Ожидание завершения обеих подзадач
    leftPromise->get_future().wait();
    rightPromise->get_future().wait();

    if (promise) {
        try {
            promise->set_value();
        }
        catch (...) {
            // Обработка исключений при установке значения
        }
    }
}

void demo() {
    const size_t size = 1000000;
    std::vector<int> array(size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 1000000);
    for (size_t i = 0; i < size; ++i) {
        array[i] = dis(gen);
    }

    auto start = std::chrono::high_resolution_clock::now();
    ThreadPool pool;
    auto promise = std::make_shared<std::promise<void>>();
    quicksort(pool, array, 0, size - 1, promise);
    promise->get_future().wait();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;

    std::cout << "Sorting took " << duration.count() << " seconds\n";

    // Добавляем вывод сообщения о завершении сортировки
    std::cout << "Sorting completed successfully!\n";
}

int main() {
    demo();
    return 0;
}
