#include "qoiview/async_decoder.hpp"

#include <fmt/base.h>
#include <fmt/ranges.h>
#include <qoipp/common.hpp>

namespace qoiview
{
    void AsyncDecoder::launch()
    {
        m_thread = std::jthread{ [&](std::stop_token token) { decode_task(token); } };
    }

    std::optional<qoipp::Desc> AsyncDecoder::start(fs::path path)
    {
        if (m_running.load(std::memory_order::acquire)) {
            m_reset.store(true, std::memory_order::release);
            m_reset.wait(true);
        }

        m_decoder.reset();

        auto size = fs::file_size(path);
        m_file    = File{ .handle = std::ifstream{ path, std::ios::binary }, .size = size };

        auto header = qoipp::ByteArr<qoipp::constants::header_size>{};
        m_file->handle.read(reinterpret_cast<char*>(header.data()), header.size());
        auto desc = m_decoder.initialize(header);
        if (not desc) {
            return std::nullopt;
        }

        m_task.emplace(path, desc.value());

        m_buffer.resize(desc->width * desc->height * static_cast<std::size_t>(desc->channels));

        m_offset_out = 0;
        m_offset_in  = 0;
        m_line_start = 0;

        m_running.store(true, std::memory_order::release);
        m_running.notify_one();

        return std::move(desc).value();
    }

    std::optional<AsyncDecoder::Work> AsyncDecoder::get()
    {
        if (m_line_start >= m_task->desc.height) {
            return std::nullopt;
        }

        if (m_running.load(std::memory_order::acquire)) {
            m_pause_req.store(true, std::memory_order::release);
            m_pause_req.wait(true);
        }

        const auto width = m_task->desc.width * static_cast<std::size_t>(m_task->desc.channels);

        auto start = m_line_start;
        auto stop  = m_offset_out / width;

        if (start == stop) {
            return std::nullopt;
        }

        auto count = stop - start;
        auto span  = std::span{ m_buffer.begin() + static_cast<long>(start * width), stop * width };

        m_line_start = stop + (m_offset_out % width == 0);

        m_pause_flag.store(false, std::memory_order::release);
        m_pause_flag.notify_one();

        return Work{
            .data  = span,
            .start = start,
            .count = count,
        };
    }

    void AsyncDecoder::stop()
    {
        m_thread.request_stop();

        m_running.store(true, std::memory_order::release);
        m_running.notify_one();

        m_thread.join();
    }

    void AsyncDecoder::decode_task(std::stop_token token)
    {
        constexpr auto buf_size = 48 * 1024uz;

        auto in_buf = qoipp::ByteVec(buf_size);

        auto create_out_span = [&] {
            return std::span{
                m_buffer.begin() + static_cast<long>(m_offset_out),
                m_buffer.size() - m_offset_out,
            };
        };

        m_running.wait(false);

        while (not token.stop_requested()) {
            if (not m_file) {
                m_running.store(false, std::memory_order::release);
                continue;
            }

            auto& [file, size] = m_file.value();
            auto data_size     = size - qoipp::constants::header_size - qoipp::constants::end_marker_size;
            auto leftover      = 0uz;

            fmt::println("running on file: {}", m_task->path.c_str());

            while (not token.stop_requested()                        //
                   and m_offset_out < m_buffer.size()                //
                   and m_offset_in < data_size                       //
                   and m_running.load(std::memory_order::acquire)    //
                   and not m_reset.load(std::memory_order::acquire)) {

                file.read(
                    reinterpret_cast<char*>(in_buf.data() + leftover),
                    static_cast<long>(in_buf.size() - leftover)
                );

                auto in  = std::span{ in_buf.begin(), static_cast<std::size_t>(file.gcount()) + leftover };
                auto out = create_out_span();

                leftover = 0uz;

                while (not in.empty()) {
                    if (auto res = m_decoder.decode(out, in); res) {
                        m_offset_out += res->written;
                        m_offset_in  += res->processed;

                        if (res->processed == 0) {
                            leftover = in.size();
                            sr::copy(in, in_buf.begin());
                            in = {};
                        } else {
                            in = in.subspan(res->processed);
                        }
                    } else {
                        fmt::println("decode {:<40} fail: {}", m_task->path.c_str(), to_string(res.error()));
                        m_running.store(false, std::memory_order::release);
                    }
                }

                if (auto req = true; m_pause_req.compare_exchange_strong(req, false)) {
                    m_pause_flag.store(true, std::memory_order::release);
                    m_pause_req.notify_one();
                    m_pause_flag.wait(true);
                }
            }

            while (m_decoder.has_run_count()) {
                auto out      = create_out_span();
                m_offset_out += m_decoder.drain_run(out).value();
            }

            if (auto value = true; m_reset.compare_exchange_strong(value, false)) {
                m_running.store(false, std::memory_order::release);
                m_reset.notify_one();

                continue;
            }

            if (m_offset_out >= m_buffer.size() or m_offset_in >= data_size or file.fail()) {
                m_file->handle.close();

                m_running.store(false, std::memory_order::release);

                m_reset.store(false, std::memory_order::release);
                m_reset.notify_one();

                m_pause_req.store(false, std::memory_order::release);
                m_pause_req.notify_one();
            }

            m_running.wait(false);
        }
    }
}
