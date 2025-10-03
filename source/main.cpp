#include "qoiview/qoiview.hpp"

#include <CLI/CLI.hpp>
#include <glbinding/glbinding.h>
#include <qoipp/simple.hpp>

#include <algorithm>
#include <cassert>
#include <filesystem>

namespace fs = std::filesystem;
namespace sv = std::views;
namespace sr = std::ranges;

struct Inputs
{
    std::vector<fs::path> files;
    std::size_t           start;
};

std::optional<Inputs> get_qoi_files(std::span<const fs::path> inputs)
{
    auto result          = std::optional<Inputs>{ std::in_place, std::vector<fs::path>{}, 0 };
    auto& [files, start] = result.value();

    auto is_qoi = [](const fs::path& path) { return fs::is_regular_file(path); };

    if (inputs.size() == 1) {
        auto input = inputs.front();

        if (not fs::exists(input)) {
            fmt::println(stderr, "No such file or directory '{}'", input.c_str());
            return {};
        } else if (fs::is_directory(input)) {
            for (auto file : fs::directory_iterator(input) | qoiview::sv::filter(is_qoi)) {
                files.push_back(file);
            }
            if (files.empty()) {
                fmt::println(stderr, "No valid qoi files found in '{}' directory", input.c_str());
                return {};
            }
        } else if (fs::is_regular_file(input)) {
            if (is_qoi(input)) {
                auto parent = fs::directory_iterator{ fs::canonical(input).parent_path() };
                for (auto input : parent | sv::filter(is_qoi)) {
                    files.push_back(input);
                }
                auto is_input = [&](const fs::path& path) { return fs::equivalent(path, input); };
                start         = static_cast<std::size_t>(sr::find_if(files, is_input) - files.begin());
            } else {
                fmt::println(stderr, "Not a valid qoi file '{}'", input.c_str());
                return {};
            }
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

int main(int argc, char** argv)
try {
    auto app = CLI::App{ "QoiView - A simple qoi image viewer", "qoiview" };

    auto files  = std::vector<fs::path>{};
    auto single = false;

    app.set_version_flag("-v,--version", QOIVIEW_VERSION_STRING);
    app.add_option("files", files, "Input qoi file or directory")->required()->check(CLI::ExistingPath);
    app.add_flag("-s,--single", single, "Run in single file mode");

    if (argc == 1) {
        fmt::print(stderr, "{}", app.help());
        return 1;
    }

    CLI11_PARSE(app, argc, argv);

    auto inputs = std::optional<Inputs>{};
    if (single and files.size() != 1) {
        fmt::println(stderr, "Single mode provided but multiple files included");
        return 1;
    } else if (single) {
        auto path = files.front();
        if (not qoipp::read_header(path).has_value()) {
            fmt::println(stderr, "Not a valid qoi file '{}'", path.c_str());
            return 1;
        }
        inputs = Inputs{ {}, 0 };
        inputs->files.push_back(path);
    } else {
        inputs = get_qoi_files(files);
    }

    if (not inputs.has_value()) {
        return 1;
    }

    if (not glfwInit()) {
        fmt::println(stderr, "Failed to initialize GLFW");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    auto* monitor = glfwGetPrimaryMonitor();
    auto* mode    = glfwGetVideoMode(monitor);

    auto header = qoipp::read_header(inputs->files[inputs->start]).value();

    auto width  = 0;
    auto height = 0;

    if (static_cast<int>(header.height) > mode->height or static_cast<int>(header.width) > mode->width) {
        auto monitor_ratio = static_cast<float>(mode->width) / static_cast<float>(mode->height);
        auto image_ratio   = static_cast<float>(header.width) / static_cast<float>(header.height);

        if (monitor_ratio > image_ratio) {
            width  = static_cast<int>(static_cast<float>(mode->height) * image_ratio);
            height = mode->height;
        } else {
            width  = mode->width;
            height = static_cast<int>(static_cast<float>(mode->width) / image_ratio);
        }
    } else {
        width  = static_cast<int>(header.width);
        height = static_cast<int>(header.height);
    }

    width  = std::max(width, 100);
    height = std::max(height, 100);

    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

    auto* window = glfwCreateWindow(width, height, "QoiView", nullptr, nullptr);
    if (window == nullptr) {
        fmt::println(stderr, "Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glbinding::initialize(glfwGetProcAddress);

    auto view = qoiview::QoiView{ window, inputs->files, inputs->start };
    view.run(width, height);

    glfwTerminate();
} catch (const std::exception& e) {
    fmt::println(stderr, "{}", e.what());
    return 1;
} catch (...) {
    fmt::println(stderr, "Unknown error");
    return 1;
}
