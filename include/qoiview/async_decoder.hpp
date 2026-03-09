#pragma once

#include "qoiview/common.hpp"

#include <qoipp/stream.hpp>

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

namespace qoiview
{
    class AsyncDecoder
    {
    public:
        using Ord = std::memory_order;

        struct Task
        {
            fs::path    path;
            qoipp::Desc desc;
        };

        struct Work
        {
            qoipp::ByteCSpan data;
            std::size_t      start;
            std::size_t      count;
        };

        struct File
        {
            std::ifstream handle;
            std::size_t   size;
        };

        struct Preparation
        {
            qoipp::Desc      desc;
            qoipp::ByteCSpan buffer;
        };

        AsyncDecoder() = default;
        ~AsyncDecoder() { stop(); }

        void launch();

        qoipp::Result<Preparation> prepare(fs::path path);
        std::optional<Work>        get();

        void start();
        void stop();

        std::optional<Task> current() const { return m_task; }

    private:
        using Id = int32_t;

        void run(std::stop_token token);
        bool decode();

        std::jthread m_thread;

        std::atomic<bool> m_wake     = false;
        std::atomic<bool> m_cancel   = false;
        std::atomic<bool> m_complete = true;

        std::mutex              m_mutex;
        std::condition_variable m_cv;

        qoipp::StreamDecoder     m_decoder;
        std::optional<Task>      m_task;
        std::optional<File>      m_file;
        std::vector<qoipp::Byte> m_buffer;

        qoipp::ByteVec m_in_buf   = qoipp::ByteVec(16 * 1024);
        std::size_t    m_leftover = 0;

        std::atomic<std::size_t> m_off_out    = 0;
        std::size_t              m_off_in     = 0;
        std::size_t              m_line_start = 0;
    };
}
