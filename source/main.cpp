#include "qoiview/qoiview.hpp"

#include <CLI/CLI.hpp>
#include <qoipp/simple.hpp>

#include <algorithm>
#include <cassert>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char** argv)
try {
    auto app = CLI::App{ "QoiView - A simple qoi image viewer", "qoiview" };

    auto files  = std::vector<fs::path>{};
    auto single = false;

    app.set_version_flag("-v,--version", QOIVIEW_VERSION_STRING);
    app.add_option("files", files, "Input qoi file or directory")->required()->check(CLI::ExistingPath);
    app.add_flag("-s,--single", single, "Run in single file mode")->default_val(false);

    if (argc == 1) {
        fmt::print(stderr, "{}", app.help());
        return 1;
    }

    CLI11_PARSE(app, argc, argv);

    auto inputs = std::optional<qoiview::Inputs>{};
    if (single and files.size() != 1) {
        fmt::println(stderr, "Single mode provided but multiple files included");
        return 1;
    } else if (single) {
        auto path = files.front();
        if (not qoipp::read_header(path).has_value()) {
            fmt::println(stderr, "Not a valid qoi file '{}'", path.c_str());
            return 1;
        }
        inputs = qoiview::Inputs{ {}, 0 };
        inputs->files.push_back(path);
    } else {
        inputs = qoiview::get_qoi_files(files);
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

    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

    auto* window = glfwCreateWindow(width, height, "QoiView", nullptr, nullptr);
    if (window == nullptr) {
        fmt::println(stderr, "Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

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
