#pragma once

#include "asset/asset_index.h"
#include "asset_loader/asset_load_job.h"
#include "threading/event.h"
#include "threading/thread.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace toy3d
{
    class AssetLoader;
    class FileSystem;
    class ThreadManager;

    // Decode priority: the loader thread always serves a higher tier before a lower one, so an
    // assembly or preview cube no longer queues behind unrelated decoding.
    enum class AssetLoadPriority
    {
        Critical,
        High,
        Normal,
        Low
    };

    // Standard tier per asset type, so every loading front ranks the same kinds consistently
    // instead of each call site inventing its own order.
    AssetLoadPriority default_asset_load_priority(const std::string& expected_type);

    // Shared request state. Handles of different asset kinds point at this one state type, which
    // is what lets the loader queue and resolve them without naming any payload type.
    struct AssetLoadState
    {
        // Set once the adopted runtime object exists; holding it keeps that object alive even
        // after the shared cache evicts the identity.
        std::shared_ptr<AssetLoadJob> job;
        std::string error;
        bool failed = false;
        // Set by invalidate()/invalidate_all()/shutdown() so a waiter retries instead of hanging.
        bool invalidated = false;
        // Read by the loader thread when it skips an unwanted job.
        std::atomic<bool> cancelled{false};
    };

    // Game Thread handle for one request. tick()/wait() deliver the adopted runtime object
    // through the typed getter that the entry creating the request bound to it.
    template <typename T> class AssetHandle
    {
      public:
        AssetHandle() = default;

        bool valid() const
        {
            return state_ != nullptr;
        }
        bool pending() const
        {
            return state_ != nullptr && !state_->job && !state_->failed && !state_->invalidated &&
                   !state_->cancelled.load();
        }
        bool ready() const
        {
            return state_ != nullptr && static_cast<bool>(state_->job);
        }
        bool failed() const
        {
            return state_ != nullptr && state_->failed;
        }
        // Terminal-with-retry: the identity was invalidated while this request was in flight, so
        // no result will arrive and the caller asks again for the current content.
        bool invalidated() const
        {
            return state_ != nullptr && state_->invalidated;
        }
        std::string error() const
        {
            return state_ != nullptr ? state_->error : std::string{};
        }
        // Returned by value: a temporary handle cannot hand out a reference into freed state.
        T get() const
        {
            return ready() && getter_ ? getter_(*state_->job) : T{};
        }
        void cancel()
        {
            if (state_ != nullptr)
            {
                state_->cancelled.store(true);
            }
        }

      private:
        friend class AssetLoader;
        std::shared_ptr<AssetLoadState> state_;
        std::function<T(const AssetLoadJob&)> getter_;
    };

    // Single decode entry point for runtime and editor: one dedicated thread reads owned asset
    // snapshots and produces owned CPU payloads through AssetLoadJob subclasses; the Game Thread
    // adopts them into runtime objects, so runtime ownership never leaves the Game Thread. The
    // loader itself never names a payload type: typed entries live next to their Job.
    class AssetLoader final
    {
      public:
        AssetLoader() = default;
        ~AssetLoader();
        AssetLoader(const AssetLoader&) = delete;
        AssetLoader& operator=(const AssetLoader&) = delete;

        // Composition root owns the loader; FileSystem must outlive it and is only read here.
        bool initialize(FileSystem& files, ThreadManager& threads);
        void shutdown();
        bool running() const
        {
            return thread_ != nullptr;
        }

        // Game Thread: the one primitive every typed entry uses. Identical requests share one
        // decode, a cached identity returns its already adopted object, and a failed decode is
        // retained per identity until invalidate() so polling never restarts it.
        template <typename T>
        AssetHandle<T> request(std::shared_ptr<AssetLoadJob> job, const AssetIndex& index, AssetLoadPriority priority,
                               std::function<T(const AssetLoadJob&)> getter)
        {
            AssetHandle<T> handle;
            if (job)
            {
                handle.state_ = submit(job, index, priority);
            }
            handle.getter_ = std::move(getter);
            return handle;
        }

        // Game Thread: adopt finished decodes and trim the cache.
        void tick();

        // Game Thread: bounded wait for a request the caller cannot proceed without (assembly,
        // which has no frame to poll in). It ticks while waiting so the Game Thread keeps adopting
        // results, and returns false on failure, invalidation or timeout. Frame paths must keep
        // polling tick() unless they are assembly actions that cannot proceed without the asset.
        template <typename T> bool wait(AssetHandle<T>& handle, std::chrono::milliseconds timeout)
        {
            if (!handle.valid())
            {
                return false;
            }
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            for (;;)
            {
                tick();
                if (!handle.pending())
                {
                    return handle.ready();
                }
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        // Game Thread: drop a cached identity and discard its in-flight candidate, releasing
        // every waiter so it can re-request the current content.
        void invalidate(const AssetId& asset);
        // Game Thread: drop every cached identity (catalog rescan or project switch).
        void invalidate_all();
        std::size_t cached_bytes() const;
        std::size_t cached_entries() const;

      private:
        // Scheduling state stays with the loader; resource knowledge stays with the job.
        struct JobEntry
        {
            std::shared_ptr<AssetLoadJob> job;
            // Owned snapshot: the loader thread never reads the live catalog.
            AssetIndex index;
            AssetLoadPriority priority = AssetLoadPriority::Normal;
            bool dispatched = false;
            bool stale = false;
            // Set when the worker's decode threw; adopt() reports it instead of adopting.
            std::string decode_error;
            // A queued decode must survive the caller dropping its handle: the result still fills
            // the shared cache, and polling consumers request again every frame.
            std::vector<std::shared_ptr<AssetLoadState>> waiters;
        };
        struct CacheEntry
        {
            std::shared_ptr<AssetLoadJob> job;
            std::size_t bytes = 0u;
            std::uint64_t last_use = 0u;
        };
        // Failure memoization stays type-aware: an identity that failed as one kind must not
        // report that failure to an entry point asking for another kind.
        struct FailureEntry
        {
            std::string expected_type;
            std::string message;
        };

        std::shared_ptr<AssetLoadState> submit(const std::shared_ptr<AssetLoadJob>& job, const AssetIndex& index,
                                               AssetLoadPriority priority);
        void worker_loop();
        void push_locked(const std::shared_ptr<JobEntry>& entry);
        std::shared_ptr<JobEntry> take_next_locked();
        // Erase the identity's registration only when this exact job still owns it.
        void drop_locked(const std::shared_ptr<JobEntry>& entry);
        void cache_insert_locked(const AssetId& asset, const std::shared_ptr<AssetLoadJob>& job);
        void adopt(const std::shared_ptr<JobEntry>& entry);
        void trim_cache_locked();

        FileSystem* files_ = nullptr;
        ThreadManager* threads_ = nullptr;
        std::unique_ptr<Thread> thread_;
        Event wake_{EventMode::AutoReset};

        mutable std::mutex mutex_;
        std::array<std::deque<std::shared_ptr<JobEntry>>, 4> queues_;
        std::map<AssetId, std::shared_ptr<JobEntry>> in_flight_;
        // A decode failure is retained per identity until invalidate(): a consumer that polls
        // every frame observes the error instead of restarting the same failing decode.
        std::map<AssetId, FailureEntry> failed_;
        std::map<AssetId, CacheEntry> cache_;
        std::size_t cached_bytes_ = 0u;
        std::size_t cache_budget_bytes_ = 64u * 1024u * 1024u;
        std::uint64_t use_counter_ = 0u;
        bool stopping_ = false;

        std::mutex results_mutex_;
        std::deque<std::shared_ptr<JobEntry>> results_;
    };
} // namespace toy3d
