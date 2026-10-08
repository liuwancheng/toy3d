#include "asset_loader/asset_loader.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

#include "logging/logger.h"
#include "threading/thread.h"
#include "threading/thread_manager.h"

namespace toy3d
{
    namespace
    {
        std::size_t priority_bucket(AssetLoadPriority priority)
        {
            // Explicit mapping keeps the tier order independent of the enum's ordinal values.
            switch (priority)
            {
            case AssetLoadPriority::Critical:
                return 0u;
            case AssetLoadPriority::High:
                return 1u;
            case AssetLoadPriority::Normal:
                return 2u;
            case AssetLoadPriority::Low:
                return 3u;
            }
            return 2u;
        }
    } // namespace

    AssetLoadPriority default_asset_load_priority(const std::string& expected_type)
    {
        // A cube face is the slowest preview decode, so the asset type itself carries the tier;
        // callers keep the explicit overload when their own urgency differs.
        if (expected_type == "toy3d.EnvironmentAssetData")
        {
            return AssetLoadPriority::High;
        }
        return AssetLoadPriority::Normal;
    }

    AssetLoader::~AssetLoader()
    {
        shutdown();
    }

    bool AssetLoader::initialize(FileSystem& files, ThreadManager& threads)
    {
        shutdown();
        files_ = &files;
        threads_ = &threads;
        try
        {
            thread_ = std::make_unique<Thread>(threads, "AssetLoader",
                                               [this]()
                                               {
                                                   worker_loop();
                                               });
        }
        catch (const std::exception& error)
        {
            TOY_LOG_ERROR("Asset loader thread could not start: {}", error.what());
            files_ = nullptr;
            threads_ = nullptr;
            return false;
        }
        return true;
    }

    std::shared_ptr<AssetLoadState> AssetLoader::submit(const std::shared_ptr<AssetLoadJob>& job,
                                                        const AssetIndex& index, AssetLoadPriority priority)
    {
        auto state = std::make_shared<AssetLoadState>();
        // request() only submits a job it owns, so the pointer is non-null by construction.
        const AssetId asset = job->reference().asset_id;
        if (files_ == nullptr || !asset.valid())
        {
            state->failed = true;
            state->error = "Asset loader is not initialized.";
            return state;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        // The cache, the failure memory and the in-flight registry are keyed by identity, while a
        // typed handle downcasts the job it receives. Asking for the same identity with another
        // expected type is a caller error: report it instead of handing back a job whose kind the
        // typed getter would cast blindly.
        const auto type_conflict = [&job, &state](const AssetLoadJob& existing) -> std::shared_ptr<AssetLoadState>
        {
            if (existing.reference().expected_type == job->reference().expected_type)
            {
                return nullptr;
            }
            state->failed = true;
            state->error = "Asset identity is already known as " + existing.reference().expected_type +
                           " but was requested as " + job->reference().expected_type + ".";
            return state;
        };
        const auto cached = cache_.find(asset);
        if (cached != cache_.end() && cached->second.job)
        {
            if (const auto conflict = type_conflict(*cached->second.job))
            {
                return conflict;
            }
            cached->second.last_use = ++use_counter_;
            state->job = cached->second.job;
            return state;
        }
        const auto failure = failed_.find(asset);
        if (failure != failed_.end())
        {
            state->failed = true;
            state->error = failure->second.expected_type == job->reference().expected_type
                               ? failure->second.message
                               : "Asset identity failed as " + failure->second.expected_type +
                                     " but was requested as " + job->reference().expected_type + ".";
            return state;
        }
        const auto pending = in_flight_.find(asset);
        if (pending != in_flight_.end())
        {
            // invalidate()/invalidate_all()/shutdown() mark an entry stale and deregister it under
            // this same lock, so a registered entry is never stale here.
            if (const auto conflict = type_conflict(*pending->second->job))
            {
                return conflict;
            }
            auto& waiters = pending->second->waiters;
            waiters.erase(std::remove_if(waiters.begin(), waiters.end(),
                                         [](const std::shared_ptr<AssetLoadState>& waiter)
                                         {
                                             return !waiter || waiter->cancelled.load();
                                         }),
                          waiters.end());
            waiters.push_back(state);
            if (priority_bucket(priority) < priority_bucket(pending->second->priority))
            {
                // A more urgent waiter raises the shared decode instead of starting a second one.
                pending->second->priority = priority;
                push_locked(pending->second);
            }
            return state;
        }
        auto entry = std::make_shared<JobEntry>();
        entry->job = job;
        entry->index = index;
        entry->priority = priority;
        entry->waiters.push_back(state);
        in_flight_.emplace(asset, entry);
        push_locked(entry);
        wake_.trigger();
        return state;
    }

    void AssetLoader::push_locked(const std::shared_ptr<JobEntry>& entry)
    {
        queues_[priority_bucket(entry->priority)].push_back(entry);
    }

    void AssetLoader::drop_locked(const std::shared_ptr<JobEntry>& entry)
    {
        // A re-queued or replaced entry must not deregister the identity's current decode.
        const auto found = in_flight_.find(entry->job->reference().asset_id);
        if (found != in_flight_.end() && found->second == entry)
        {
            in_flight_.erase(found);
        }
    }

    void AssetLoader::cache_insert_locked(const AssetId& asset, const std::shared_ptr<AssetLoadJob>& job)
    {
        const std::size_t bytes = job->bytes();
        const auto existing = cache_.find(asset);
        if (existing != cache_.end())
        {
            // Replacing an identity must not double count its bytes in the eviction budget.
            cached_bytes_ -= existing->second.bytes;
        }
        CacheEntry entry;
        entry.job = job;
        entry.bytes = bytes;
        entry.last_use = ++use_counter_;
        cached_bytes_ += entry.bytes;
        cache_[asset] = std::move(entry);
        failed_.erase(asset);
    }

    std::shared_ptr<AssetLoader::JobEntry> AssetLoader::take_next_locked()
    {
        for (auto& queue : queues_)
        {
            while (!queue.empty())
            {
                auto entry = queue.front();
                queue.pop_front();
                if (entry->dispatched)
                {
                    // A priority raise re-queues the same entry; the first bucket entry wins.
                    continue;
                }
                if (entry->stale)
                {
                    // The identity was invalidated while this job was queued: never start a
                    // decode whose result adopt() would discard.
                    drop_locked(entry);
                    continue;
                }
                const bool wanted = std::any_of(entry->waiters.begin(), entry->waiters.end(),
                                                [](const std::shared_ptr<AssetLoadState>& waiter)
                                                {
                                                    return waiter && !waiter->cancelled.load();
                                                });
                if (!wanted)
                {
                    // Every requester explicitly withdrew before the decode started. Dropping
                    // the handle is deliberately not a withdrawal: a polling consumer that
                    // re-requests each frame still wants the shared decode to finish.
                    drop_locked(entry);
                    continue;
                }
                entry->dispatched = true;
                return entry;
            }
        }
        return nullptr;
    }

    void AssetLoader::worker_loop()
    {
        for (;;)
        {
            std::shared_ptr<JobEntry> entry;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_)
                {
                    return;
                }
                entry = take_next_locked();
            }
            if (!entry)
            {
                // Bounded wait keeps shutdown cooperative without busy spinning.
                wake_.wait_for(std::chrono::milliseconds(20));
                continue;
            }
            // Type-independent: the job owns reading and decoding its own payload. A throwing or
            // failing decode must not kill the loader thread, or every waiter would stay pending
            // forever, and it must not let a partial payload reach adopt().
            try
            {
                const AssetStatus status = entry->job->decode(*files_, entry->index);
                if (!status.succeeded() && entry->job->error().empty())
                {
                    entry->decode_error = status.message.empty() ? "Asset decode failed." : status.message;
                }
            }
            catch (const std::exception& error)
            {
                const char* message = error.what();
                entry->decode_error =
                    (message != nullptr && *message != '\0') ? message : "Asset decode threw an exception.";
            }
            catch (...)
            {
                entry->decode_error = "Asset decode threw a non-standard exception.";
            }
            {
                std::lock_guard<std::mutex> lock(results_mutex_);
                results_.push_back(std::move(entry));
            }
        }
    }

    void AssetLoader::tick()
    {
        // Adoption can re-enter this function: a Job's adopt() may resolve another asset (a
        // StaticMesh adopting its materials resolves textures, which waits). Consuming one entry
        // at a time from the shared queue keeps a nested tick able to adopt the rest of the same
        // batch instead of waiting out a timeout for a result it already has.
        for (;;)
        {
            std::shared_ptr<JobEntry> entry;
            {
                std::lock_guard<std::mutex> lock(results_mutex_);
                if (results_.empty())
                {
                    break;
                }
                entry = std::move(results_.front());
                results_.pop_front();
            }
            adopt(entry);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        trim_cache_locked();
    }

    void AssetLoader::adopt(const std::shared_ptr<JobEntry>& entry)
    {
        if (!entry || !entry->job)
        {
            return;
        }
        const AssetId asset = entry->job->reference().asset_id;
        bool stale = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = in_flight_.find(asset);
            if (found != in_flight_.end() && found->second == entry)
            {
                in_flight_.erase(found);
            }
            stale = entry->stale;
        }
        if (stale)
        {
            // An invalidated candidate never replaces the current object.
            return;
        }
        std::string error = entry->decode_error.empty() ? entry->job->error() : entry->decode_error;
        bool adopted = error.empty();
        if (adopted)
        {
            // Runtime object creation is Game Thread work: adopt() runs here, never on the worker.
            const AssetStatus status = entry->job->adopt();
            adopted = status.succeeded() && entry->job->error().empty();
            if (!adopted)
            {
                error = entry->job->error().empty() ? status.message : entry->job->error();
            }
        }
        if (adopted)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cache_insert_locked(asset, entry->job);
        }
        else
        {
            std::lock_guard<std::mutex> lock(mutex_);
            FailureEntry failure;
            failure.expected_type = entry->job->reference().expected_type;
            failure.message = error.empty() ? "Asset decode failed." : error;
            failed_[asset] = std::move(failure);
        }
        // Serialized against submit() so a waiter list cannot change while it is read.
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& waiter : entry->waiters)
        {
            if (!waiter || waiter->cancelled.load())
            {
                continue;
            }
            if (adopted)
            {
                waiter->job = entry->job;
            }
            else
            {
                waiter->failed = true;
                waiter->error = error.empty() ? "Asset decode failed." : error;
            }
        }
    }

    void AssetLoader::trim_cache_locked()
    {
        while (cached_bytes_ > cache_budget_bytes_ && cache_.size() > 1u)
        {
            auto oldest = cache_.begin();
            for (auto it = cache_.begin(); it != cache_.end(); ++it)
            {
                if (it->second.last_use < oldest->second.last_use)
                {
                    oldest = it;
                }
            }
            cached_bytes_ -= oldest->second.bytes;
            cache_.erase(oldest);
        }
    }

    void AssetLoader::invalidate(const AssetId& asset)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto cached = cache_.find(asset);
        if (cached != cache_.end())
        {
            cached_bytes_ -= cached->second.bytes;
            cache_.erase(cached);
        }
        const auto pending = in_flight_.find(asset);
        if (pending != in_flight_.end())
        {
            // The identity's content changed: the candidate is discarded and its registration
            // is released, so the next request starts a fresh decode instead of waiting on a
            // result that will never be delivered.
            pending->second->stale = true;
            for (const auto& waiter : pending->second->waiters)
            {
                if (waiter && !waiter->cancelled.load())
                {
                    waiter->invalidated = true;
                }
            }
            in_flight_.erase(pending);
        }
        failed_.erase(asset);
    }

    void AssetLoader::invalidate_all()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& pair : in_flight_)
        {
            pair.second->stale = true;
            for (const auto& waiter : pair.second->waiters)
            {
                if (waiter && !waiter->cancelled.load())
                {
                    waiter->invalidated = true;
                }
            }
        }
        in_flight_.clear();
        cache_.clear();
        failed_.clear();
        cached_bytes_ = 0u;
    }

    std::size_t AssetLoader::cached_bytes() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return cached_bytes_;
    }

    std::size_t AssetLoader::cached_entries() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.size();
    }

    void AssetLoader::shutdown()
    {
        if (thread_)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = true;
                for (auto& queue : queues_)
                {
                    queue.clear();
                }
                for (auto& pair : in_flight_)
                {
                    // A handle held across shutdown must not stay pending forever: the decode is
                    // abandoned and every waiter becomes terminal-with-retry.
                    for (const auto& waiter : pair.second->waiters)
                    {
                        if (waiter && !waiter->cancelled.load())
                        {
                            waiter->invalidated = true;
                        }
                    }
                }
                in_flight_.clear();
            }
            wake_.trigger();
            try
            {
                if (thread_->is_joinable())
                {
                    thread_->join();
                }
            }
            catch (const std::exception& error)
            {
                TOY_LOG_ERROR("Asset loader thread join failed: {}", error.what());
            }
            thread_.reset();
        }
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            results_.clear();
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& queue : queues_)
            {
                queue.clear();
            }
            in_flight_.clear();
            cache_.clear();
            failed_.clear();
            cached_bytes_ = 0u;
            stopping_ = false;
        }
        files_ = nullptr;
        threads_ = nullptr;
    }
} // namespace toy3d
