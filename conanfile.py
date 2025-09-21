from conan import ConanFile
from conan.tools.cmake import cmake_layout

class Recipe(ConanFile):
    settings   = ["os", "compiler", "build_type", "arch"]
    generators = ["CMakeToolchain", "CMakeDeps"]
    requires   = ["fmt/11.1.3", "glfw/3.4", "glad/0.1.36", "cli11/2.4.1"]

    def layout(self):
        cmake_layout(self)
