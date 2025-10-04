#include "qoiview/async_decoder.hpp"

#include <fmt/base.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

namespace qoiview
{
    void AsyncDecoder::launch()
    {
        m_thread = std::jthread{ [&](std::stop_token token) { decode_task(token); } };
    }

    qoipp::Result<AsyncDecoder::Preparation> AsyncDecoder::prepare(fs::path path)
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

        try {
            m_file->handle.exceptions(std::fstream::badbit);
        } catch (const std::ios_base::failure& e) {
            spdlog::error("Failed to read file {:?}: {}", path.c_str(), e.what());
            return qoipp::make_error<Preparation>(qoipp::Error::IoError);
        }

        auto desc = m_decoder.initialize(header, qoipp::Channels::RGBA);
        if (not desc) {
            return qoipp::make_error<Preparation>(desc.error());
        }

        m_task.emplace(path, desc.value());

        m_buffer.clear();
        m_buffer.resize(desc->width * desc->height * static_cast<std::size_t>(desc->channels), 0x00);

        m_off_out    = 0;
        m_off_in     = qoipp::constants::header_size;
        m_line_start = 0;

        return Preparation{ std::move(desc).value(), m_buffer };
    }

    std::optional<AsyncDecoder::Work> AsyncDecoder::get()
    {
        if (m_line_start >= m_task->desc.height) {
            return std::nullopt;
        }

        if (m_running.load(std::memory_order::acquire)) {
            m_pause.store(true, std::memory_order::release);
            m_pause.wait(true);
        }

        const auto width = m_task->desc.width * static_cast<std::size_t>(m_task->desc.channels);

        auto start = m_line_start;
        auto stop  = m_off_out / width;

        if (start == stop) {
            return std::nullopt;
        }

        auto count = stop - start;
        auto span  = std::span{ m_buffer.begin() + static_cast<std::ptrdiff_t>(start * width), stop * width };

        m_line_start = stop + (m_off_out % width == 0);

        m_running.store(true, std::memory_order::release);
        m_running.notify_one();

        return Work{
            .data  = span,
            .start = start,
            .count = count,
        };
    }

    void AsyncDecoder::start()
    {
        m_running.store(true, std::memory_order::release);
        m_running.notify_one();
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
        constexpr auto buf_size = 64 * 1024uz;

        auto in_buf = qoipp::ByteVec(buf_size);

        auto create_out_span = [&] {
            return std::span{
                m_buffer.begin() + static_cast<std::ptrdiff_t>(m_off_out),
                m_buffer.size() - m_off_out,
            };
        };

        auto wake_waiters = [&] {
            m_reset.store(false, std::memory_order::release);
            m_reset.notify_one();

            m_pause.store(false, std::memory_order::release);
            m_pause.notify_one();
        };

        spdlog::debug("Decoder started");

        m_running.wait(false);

        while (not token.stop_requested()) {
            assert(m_file and m_task);

            auto [path, desc]   = m_task.value();
            auto& [file, fsize] = m_file.value();
            auto leftover       = 0uz;
            auto cancel         = false;

            spdlog::debug("Decoding start: {:?}", path.c_str());

            while (not token.stop_requested()         //
                   and m_off_out < m_buffer.size()    //
                   and m_off_in < fsize               //
                   and file.good()) {

                if (m_pause.exchange(false, std::memory_order::acq_rel)) {
                    m_running.store(false, std::memory_order::release);
                    m_pause.notify_one();
                    m_running.wait(false);
                } else if (cancel = m_reset.exchange(false, std::memory_order::acq_rel); cancel) {
                    m_running.store(false, std::memory_order::release);
                    m_reset.notify_one();
                    break;
                }

                try {
                    file.read(
                        reinterpret_cast<char*>(in_buf.data() + leftover),
                        static_cast<std::streamsize>(in_buf.size() - leftover)
                    );
                } catch (const std::ios_base::failure& e) {
                    spdlog::error("Failed to read file {:?}: {}", path.c_str(), e.what());
                }

                auto in_size = std::min(static_cast<std::size_t>(file.gcount()) + leftover, fsize - m_off_in);
                auto in      = std::span{ in_buf.begin(), in_size };
                auto out     = create_out_span();

                leftover = 0uz;

                while (not in.empty()) {
                    if (auto res = m_decoder.decode(out, in); res) {
                        m_off_out += res->written;
                        m_off_in  += res->processed;

                        if (res->processed == 0) {
                            leftover = in.size();
                            sr::copy(in, in_buf.begin());
                            in = {};
                        } else {
                            in = in.subspan(res->processed);
                        }
                    } else {
                        spdlog::error("Failed to decode {:?}: {}", path.c_str(), to_string(res.error()));

                        m_running.store(false, std::memory_order::release);
                        cancel = true;
                        wake_waiters();
                        break;
                    }
                }
            }

            if (cancel) {
                spdlog::debug("Decoding cancelled");

                m_running.wait(false);
                continue;
            }

            while (m_decoder.has_run_count()) {
                auto out   = create_out_span();
                m_off_out += m_decoder.drain_run(out).value();
            }

            if (m_off_out >= m_buffer.size() or m_off_in >= fsize or file.fail()) {
                spdlog::debug("Decoding complete{}", m_off_out < m_buffer.size() ? " (truncated)" : "");
                spdlog::debug("Decoded data: {}/{}", m_off_in, fsize);

                m_file->handle.close();
                m_running.store(false, std::memory_order::release);
                wake_waiters();
            }

            m_running.wait(false);
        }

        spdlog::debug("Decoder stopped");
    }
}
