from conan import ConanFile
from conan.tools.cmake import cmake_layout

class GTradeClientConan(ConanFile):
    name = "gtrade-client"
    version = "1.0.0"

    settings = "os", "compiler", "build_type", "arch"

    # 依赖项
    requires = (
        "qt/5.15.11",
        "protobuf/3.21.12",
        "grpc/1.54.3",
        "yaml-cpp/0.8.0",
        "boost/1.83.0",
    )

    # 默认选项
    default_options = {
        "boost/*:without_stacktrace": False,
    }

    # 生成器
    generators = "CMakeToolchain", "CMakeDeps"

    # def layout(self):
    #     cmake_layout(self)

    def configure(self):
        # 可以根据 build_type 自动调整选项
        if self.settings.build_type == "Debug":
            self.output.info("Configuring for Debug build")
        else:
            self.output.info("Configuring for Release build")

        # Use static libraries instead of shared (DLLs)
        self.options["qt"].shared = True
        self.options["qt"].qtbase = True
        self.options["qt"].qtwidgets = True
