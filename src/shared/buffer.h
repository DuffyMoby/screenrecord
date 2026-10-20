#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>
#include <array> // IWYU pragma: keep
#include <condition_variable>
#include <mutex>

struct Frame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> view;
};

class Buffer {
    public:
        Buffer() = default; // TODO

        Frame& acquire_write() {
            std::unique_lock<std::mutex> lock(mutex_);

            not_full_.wait(lock, [this] { return free_slots() > 0; });

            Frame& frame = buffer_[write_index_];
            write_index_ = (write_index_ + 1) % size;
            writing_++;

            return frame;
        }

        void commit_write() {
            std::lock_guard<std::mutex> lock(mutex_);

            writing_--;
            filled_++;
            not_empty_.notify_one();
        }

        Frame& acquire_read() {
            std::unique_lock<std::mutex> lock(mutex_);

            not_empty_.wait(lock, [this] { return filled_ > 0; });

            Frame& frame = buffer_[read_index_];
            read_index_ = (read_index_ + 1) % size;
            filled_--;
            reading_++;

            return frame;
        }

        void release_read() {
            std::lock_guard<std::mutex> lock(mutex_);

            reading_--;
            not_full_.notify_one();
        }

    private:
        // Call with mutex_ held.
        std::size_t free_slots() const {
            return size - filled_ - writing_ - reading_;
        }

        static constexpr std::size_t size = 8;
        mutable std::mutex mutex_;
        std::array<Frame, size> buffer_;
        std::condition_variable not_empty_;
        std::condition_variable not_full_;
        std::size_t filled_ = 0;   // committed, not yet acquired for reading
        std::size_t writing_ = 0;  // acquired for writing, not yet committed
        std::size_t reading_ = 0;  // acquired for reading, not yet released
        std::size_t write_index_ = 0; // Tail
        std::size_t read_index_ = 0; // Head
};
