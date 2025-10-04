#include "qoiview/qoiview.hpp"

#include <CLI/CLI.hpp>
#include <glbinding/glbinding.h>
#include <qoipp/simple.hpp>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <limits>
#include <map>

namespace fs = std::filesystem;
namespace sv = std::views;
namespace sr = std::ranges;

using qoiview::Color;
using qoiview::QoiView;

enum class Sort
{
    Name,
    Date,
    Size,
};

struct Inputs
{
    std::deque<fs::path> files;
    std::size_t          start = std::numeric_limits<std::size_t>::max();
};

struct Args
{
    Inputs inputs;
    Color  background;
    int    width;
    int    height;
};

static inline const auto sort_map = std::map<std::string, Sort>{
    { "name", Sort::Name },
    { "date", Sort::Date },
    { "size", Sort::Size },
};

std::optional<Inputs> get_qoi_files(std::span<const fs::path> inputs)
{
    auto result          = std::optional<Inputs>{ std::in_place };
    auto& [files, start] = result.value();

    auto is_qoi = [](const fs::path& path) { return fs::is_regular_file(path); };

    if (inputs.size() == 1) {
        auto input = inputs.front();

        if (not fs::exists(input)) {
            fmt::println(stderr, "No such file or directory '{}'", input.c_str());
            return {};
        } else if (fs::is_directory(input)) {
            for (auto file : fs::directory_iterator(input) | sv::filter(is_qoi)) {
                files.push_back(fs::relative(file));
            }
            if (files.empty()) {
                fmt::println(stderr, "No valid qoi files found in '{}' directory", input.c_str());
                return {};
            }
        } else if (fs::is_regular_file(input)) {
            auto parent = fs::directory_iterator{ fs::canonical(input).parent_path() };
            for (auto input : parent | sv::filter(is_qoi)) {
                files.push_back(fs::relative(input));
            }
            auto is_input = [&](const fs::path& path) { return fs::equivalent(path, input); };
            start         = static_cast<std::size_t>(sr::find_if(files, is_input) - files.begin());
        } else {
            fmt::println(stderr, "Not a regular file or directory '{}'", input.c_str());
            return {};
        }
    } else {
        for (auto input : inputs | sv::filter(is_qoi)) {
            files.push_back(input);
        }
        if (files.empty()) {
            fmt::println(stderr, "No valid qoi files found in input arguments");
            return {};
        }
    }

    return result;
}

std::variant<Args, int> parse_args(int argc, char** argv)
{
    auto app = CLI::App{ "QoiView - A simple qoi image viewer", "qoiview" };

    auto files      = std::vector<fs::path>{};
    auto sort       = Sort::Name;
    auto background = std::string{ "222436" };
    auto reverse    = false;
    auto single     = false;
    auto width      = 0;
    auto height     = 0;
    auto debug      = false;
    auto verbose    = false;

    auto check_hex = [](std::string_view hex) {
        auto msg = "invalid color hex";
        if (hex.size() != 6 or not sr::all_of(hex, [](char c) { return std::isxdigit(c); })) {
            return msg;
        }
        return "";
    };

    app.set_version_flag("-v,--version", QOIVIEW_VERSION_STRING);
    app.add_option("files", files, "Input qoi file or directory")->required()->check(CLI::ExistingPath);
    app.add_option("-W,--width", width, "Width of the window")->transform(CLI::NonNegativeNumber);
    app.add_option("-H,--height", height, "Height of the window")->transform(CLI::NonNegativeNumber);
    app.add_option("-S,--sort", sort, "Sort the files")->transform(CLI::CheckedTransformer(sort_map));
    app.add_option("-b,--background", background, "Set background color (6-digit hex)")
        ->check(check_hex)
        ->default_val(background);
    app.add_flag("-r,--reverse", reverse, "Reverse sort");
    app.add_flag("-s,--single", single, "Run in single file mode");

    auto verbose_opt = app.add_flag("--verbose", verbose, "Print additional output");
    app.add_flag("--debug", debug, "Print debug outputs")->excludes(verbose_opt);

    if (argc == 1) {
        fmt::print(stderr, "{}", app.help());
        return 1;
    }

    CLI11_PARSE(app, argc, argv);

    if (not verbose and not debug) {
        spdlog::set_default_logger(spdlog::null_logger_mt("qoiview-log"));
        spdlog::set_level(spdlog::level::off);
    } else if (verbose) {
        spdlog::set_default_logger(spdlog::stderr_color_mt("qoiview-log"));
        spdlog::set_level(spdlog::level::info);
    } else if (debug) {
        spdlog::set_default_logger(spdlog::stderr_color_mt("qoiview-log"));
        spdlog::set_level(spdlog::level::debug);
    }
    spdlog::set_pattern("[qoiview] [%^%L%$] %v");

    auto inputs = std::optional<Inputs>{};
    if (single and files.size() != 1) {
        fmt::println(stderr, "Single mode is requested but multiple files is provided");
        return 1;
    } else if (single) {
        auto path = files.front();
        inputs    = Inputs{ .files = { path }, .start = 0 };
    } else {
        inputs = get_qoi_files(files);
    }

    if (not inputs.has_value()) {
        return 1;
    }

    auto comp_name = [&](const fs::path& l, const fs::path& r) -> bool { return (l < r) ^ reverse; };
    auto comp_size = [&](const fs::path& l, const fs::path& r) -> bool {
        return (fs::file_size(l) < fs::file_size(r)) ^ reverse;
    };
    auto comp_date = [&](const fs::path& l, const fs::path& r) -> bool {
        return (fs::last_write_time(l) < fs::last_write_time(r)) ^ reverse;
    };

    auto comp = [&](const fs::path& l, const fs::path& r) -> bool {
        switch (sort) {
        case Sort::Date: {
            if (comp_date(l, r)) {
                return true;
            }
            if (comp_date(r, l)) {
                return false;
            }

            return comp_name(l, r);
        } break;
        case Sort::Size: {
            if (comp_size(l, r)) {
                return true;
            }
            if (comp_size(r, l)) {
                return false;
            }

            return comp_name(l, r);

        } break;
        case Sort::Name:
        default: return comp_name(l, r);
        };
    };

    if (inputs->start == std::numeric_limits<std::size_t>::max()) {
        sr::sort(inputs->files, comp);
        inputs->start = 0;
    } else {
        auto first = inputs->files[inputs->start];
        sr::sort(inputs->files, comp);

        auto is_input = [&](const fs::path& path) { return fs::equivalent(path, first); };
        inputs->start = static_cast<std::size_t>(
            sr::find_if(inputs->files, is_input) - inputs->files.begin()
        );
    }

    auto to_color = [](std::string_view hex) {
        assert(hex.size() == 6);
        auto color = Color{};
        auto r_str = hex.substr(0, 2);
        auto g_str = hex.substr(2, 2);
        auto b_str = hex.substr(4, 2);
        std::from_chars(r_str.begin(), r_str.end(), color.r, 16);
        std::from_chars(g_str.begin(), g_str.end(), color.g, 16);
        std::from_chars(b_str.begin(), b_str.end(), color.b, 16);
        return color;
    };

    return Args{
        .inputs     = std::move(inputs).value(),
        .background = to_color(background),
        .width      = width,
        .height     = height,
    };
}

int main(int argc, char** argv)
try {
    auto args = parse_args(argc, argv);
    if (args.index() == 1) {
        return std::get<1>(args);
    }

    auto&& [inputs, background, width, height] = std::get<0>(args);

    if (not glfwInit()) {
        fmt::println(stderr, "Failed to initialize GLFW");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    auto* monitor = glfwGetPrimaryMonitor();
    auto* mode    = glfwGetVideoMode(monitor);

    auto header = qoipp::Desc{};
    while (true) {
        auto file = inputs.files[inputs.start];
        if (auto res = qoipp::read_header(file); not res) {
            spdlog::info("Failed to decode file {:?}: {}", file.c_str(), to_string(res.error()));
        } else {
            header = *res;
            break;
        }

        inputs.files.erase(inputs.files.begin() + static_cast<std::ptrdiff_t>(inputs.start));
        if (inputs.files.empty()) {
            break;
        }
        inputs.start = inputs.start % inputs.files.size();
    }

    if (inputs.files.empty()) {
        fmt::println(stderr, "No valid QOI file found");
        return 1;
    }

    if (width <= 0 and height <= 0) {
        width  = static_cast<int>(header.width);
        height = static_cast<int>(header.height);
    } else if (width <= 0) {
        auto ratio = static_cast<float>(header.width) / static_cast<float>(header.height);
        width      = static_cast<int>(static_cast<float>(height) * ratio);
    } else if (height <= 0) {
        auto ratio = static_cast<float>(header.width) / static_cast<float>(header.height);
        height     = static_cast<int>(static_cast<float>(width) / ratio);
    }

    spdlog::info("Window size set to {}x{}", width, height);

    if (height > mode->height or width > mode->width) {
        auto monitor_ratio = static_cast<float>(mode->width) / static_cast<float>(mode->height);
        auto image_ratio   = static_cast<float>(width) / static_cast<float>(height);

        if (monitor_ratio > image_ratio) {
            width  = static_cast<int>(static_cast<float>(mode->height) * image_ratio);
            height = mode->height;
        } else {
            width  = mode->width;
            height = static_cast<int>(static_cast<float>(mode->width) / image_ratio);
        }

        spdlog::warn("Window size exceeds screen resolution, changed to {}x{}", width, height);
    }

    if (width < 100 or height < 100) {
        width  = std::max(width, 100);
        height = std::max(height, 100);

        spdlog::warn("Window size is too small, changed to {}x{}", width, height);
    }

    auto* window = glfwCreateWindow(width, height, "QoiView", nullptr, nullptr);
    if (window == nullptr) {
        fmt::println(stderr, "Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glbinding::initialize(glfwGetProcAddress);

    auto view = QoiView{ window, inputs.files, inputs.start };
    view.run(width, height, background);

    glfwTerminate();
} catch (const std::exception& e) {
    fmt::println(stderr, "{}", e.what());
    glfwTerminate();
    return 1;
} catch (...) {
    fmt::println(stderr, "Unknown error");
    glfwTerminate();
    return 1;
}
