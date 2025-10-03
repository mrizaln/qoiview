#pragma once

#include "qoiview/common.hpp"

#include <qoipp/stream.hpp>

#include <fstream>
#include <thread>

namespace qoiview
{
    class AsyncDecoder
    {
    public:
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

        void launch();

        qoipp::Result<Preparation> prepare(fs::path path);
        std::optional<Work>        get();

        void start();
        void cancel();
        void stop();

        std::optional<Task> current() const { return m_task; }

    private:
        void resume();
        void decode_task(std::stop_token token);

        std::jthread      m_thread;
        std::atomic<bool> m_running    = false;
        std::atomic<bool> m_reset      = false;
        std::atomic<bool> m_pause_req  = false;
        std::atomic<bool> m_pause_flag = false;

        qoipp::StreamDecoder     m_decoder;
        std::optional<Task>      m_task;
        std::optional<File>      m_file;
        std::vector<qoipp::Byte> m_buffer;

        std::size_t m_offset_out = 0;
        std::size_t m_offset_in  = 0;
        std::size_t m_line_start = 0;
    };
}
