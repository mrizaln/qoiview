#include "qoiview/async_decoder.hpp"

#include <fmt/base.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

namespace qoiview
{
    void AsyncDecoder::launch()
    {
        m_thread = std::jthread{ [&](std::stop_token token) { run(token); } };
    }

    qoipp::Result<AsyncDecoder::Preparation> AsyncDecoder::prepare(fs::path path)
    {
        if (not m_complete.load(Ord::acquire)) {
            m_cancel.store(true, Ord::release);
            m_cancel.notify_one();
        }
        m_complete.wait(false);

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

        const auto width = m_task->desc.width * static_cast<std::size_t>(m_task->desc.channels);

        auto off_out = m_off_out.load(Ord::acquire);

        auto start = m_line_start;
        auto stop  = off_out / width;

        if (start == stop) {
            return std::nullopt;
        }

        auto count = stop - start;
        auto span  = std::span{ m_buffer.begin() + static_cast<std::ptrdiff_t>(start * width), stop * width };

        m_line_start = stop + (off_out % width == 0);

        return Work{
            .data  = span,
            .start = start,
            .count = count,
        };
    }

    void AsyncDecoder::start()
    {
        spdlog::debug("Decode start: {}", m_task.value_or({}).path.c_str());

        m_complete.store(false, Ord::release);
        m_wake.store(true, Ord::release);
        m_cv.notify_one();
    }

    void AsyncDecoder::stop()
    {
        if (m_thread.joinable()) {
            m_thread.request_stop();
            m_cancel.store(true, Ord::release);
            m_wake.store(true, Ord::release);
            m_cv.notify_one();
            m_thread.join();
        }
    }

    void AsyncDecoder::run(std::stop_token token)
    {
        while (not token.stop_requested()) {
            if (auto cancel = m_cancel.load(Ord::acquire); not m_complete.load(Ord::acquire) and not cancel) {
                m_complete.store(decode(), Ord::release);
                m_complete.notify_one();
            } else {
                if (cancel) {
                    m_cancel.store(false, Ord::release);
                    m_complete.store(true, Ord::release);
                    m_complete.notify_one();
                }
                auto lock = std::unique_lock{ m_mutex };
                m_cv.wait(lock, [this] { return m_wake.load(Ord::acquire); });
                m_wake.store(false, Ord::release);
            }
        }
    }

    // true : complete
    // false: incomplete
    bool AsyncDecoder::decode()
    {
        assert(m_file and m_task);

        auto [path, desc]   = m_task.value();
        auto& [file, fsize] = m_file.value();
        auto cancel         = false;

        auto off_out = m_off_out.load(Ord::acquire);

        auto create_out_span = [&] {
            return std::span{
                m_buffer.begin() + static_cast<std::ptrdiff_t>(off_out),
                m_buffer.size() - off_out,
            };
        };

        if (off_out < m_buffer.size()    //
            and m_off_in < fsize         //
            and file.good()) {

            try {
                file.read(
                    reinterpret_cast<char*>(m_in_buf.data() + m_leftover),
                    static_cast<std::streamsize>(m_in_buf.size() - m_leftover)
                );
            } catch (const std::ios_base::failure& e) {
                spdlog::error("Failed to read file {:?}: {}", path.c_str(), e.what());
            }

            auto in_size = std::min(static_cast<std::size_t>(file.gcount()) + m_leftover, fsize - m_off_in);
            auto in      = std::span{ m_in_buf.begin(), in_size };
            auto out     = create_out_span();

            m_leftover = 0uz;

            while (not in.empty()) {
                if (auto res = m_decoder.decode(out, in); res) {
                    off_out  += res->written;
                    m_off_in += res->processed;

                    if (res->processed == 0) {
                        m_leftover = in.size();
                        sr::copy(in, m_in_buf.begin());
                        in = {};
                    } else {
                        in = in.subspan(res->processed);
                    }
                } else {
                    spdlog::error("Failed to decode {:?}: {}", path.c_str(), to_string(res.error()));
                    cancel = true;
                    break;
                }
            }
        }

        if (cancel) {
            spdlog::debug("Decode cancelled");
            return true;
        }

        while (m_decoder.has_run_count()) {
            auto out  = create_out_span();
            off_out  += m_decoder.drain_run(out).value();
        }

        if (off_out >= m_buffer.size() or m_off_in >= fsize or file.fail()) {
            spdlog::debug("Decode complete{}: {}", off_out < m_buffer.size() ? " (trunc)" : "", path.c_str());
            spdlog::debug("Decoded data: {}/{}", m_off_in, fsize);

            m_off_out.store(off_out, Ord::release);

            m_file->handle.close();
            return true;
        }

        m_off_out.store(off_out, Ord::release);
        return false;
    }
}
