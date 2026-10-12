# PrismCore

PrismCore 是使用 C++23 的第三方库，要求 CMake 3.20 或更新版本及支持 C++23 的编译器。

## 构建

从项目根目录构建，默认以静态库方式构建所有模块，不构建测试，也不加载 Catch2 或 benchmark：

```sh
cmake -S . -B build
cmake --build build --parallel
```

使用 PRISMCORE_BUILD_SHARED_LIBS 统一选择所有编译模块的库类型，默认 OFF。该选项独立于外部工程的 BUILD_SHARED_LIBS，不提供单模块构建开关：

```sh
cmake -S . -B build-static -DPRISMCORE_BUILD_SHARED_LIBS=OFF
cmake --build build-static --parallel

cmake -S . -B build-shared -DPRISMCORE_BUILD_SHARED_LIBS=ON
cmake --build build-shared --parallel
```

根目录入口与独立 core 入口均支持此选项。Common、Math、Memory 生成静态或动态库，其余模块仍为头文件接口目标；Core::Core 汇总所有模块，不生成独立的整体库文件。

core 目录可作为完全独立的 CMake 工程，不依赖根目录、tests 或 extern：

```sh
cmake -S src/core -B build-core
cmake --build build-core --parallel
```

仅需 core 时，可在 Git 稀疏检出中保留整个 src/core 目录：

```sh
git sparse-checkout init --cone
git sparse-checkout set src/core
cmake -S src/core -B build-core
cmake --build build-core --parallel
```

## 源码引入

外部项目可以引入整个仓库：

```cmake
add_subdirectory(extern/PrismCore)
target_link_libraries(MyApp PRIVATE Core::Core)
```

也可以只引入 core，包括只检出了 core 的情况：

```cmake
add_subdirectory(extern/PrismCore/src/core)
target_link_libraries(MyApp PRIVATE Core::Core)
```

两种方式任选其一。Core::Core 自动传递头文件目录、C++23 要求及模块链接依赖，无需设置全局 include 路径或全局 C++ 标准。

外部工程可以在 add_subdirectory 前设置库类型：

```cmake
set(PRISMCORE_BUILD_SHARED_LIBS ON CACHE BOOL "Build PrismCore shared libraries")
add_subdirectory(extern/PrismCore)
target_link_libraries(MyApp PRIVATE Core::Core)
```

也可在配置外部工程时传入 -DPRISMCORE_BUILD_SHARED_LIBS=ON。库类型在源码构建时确定，find_package 引入已安装的库不会改变其类型。

## 安装引入

根目录构建与独立 core 构建均支持安装：

```sh
cmake --install build --prefix /path/to/prismcore-install
```

外部项目使用安装包：

```cmake
find_package(PrismCore CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE Core::Core)
```

配置外部项目时通过 CMAKE_PREFIX_PATH 指定安装前缀：

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/prismcore-install
```

安装包支持移动安装前缀。头文件默认安装到 include，静态库、导入库和 Unix 动态库默认安装到 lib，Windows DLL 默认安装到 bin，具体位置遵循 GNUInstallDirs。

## 模块目标

| 目标 | 类型 | 依赖 |
| --- | --- | --- |
| Core::Core | INTERFACE | Math、Memory、Wrappers、Container、Reflect、Serde、Common |
| Core::Math | STATIC / SHARED | Common |
| Core::Memory | STATIC / SHARED | Common、Container、Threads::Threads |
| Core::Wrappers | INTERFACE | Common |
| Core::Container | INTERFACE | Common |
| Core::Reflect | INTERFACE | Common |
| Core::Serde | INTERFACE | Reflect |
| Core::Common | STATIC / SHARED | 错误策略实现、公开头文件路径、C++23 |

所有模块始终启用，暂不提供选择性构建设置，外部通常统一链接 Core::Core。Math 和 Memory 启用位置无关代码，并设置 Windows 自动符号导出；Math 当前仍为占位实现，部分工具链会提示其静态库没有全局符号。头文件模块没有独立二进制产物。

## 错误与断言

[Error.hpp](src/core/includes/core/common/Error.hpp) 提供全局 EErrorCode 和 Error；[Assert.hpp](src/core/includes/core/common/Assert.hpp) 提供共享 ErrorPolicy 与 UI_CHECK、UI_ASSERT、UI_VERIFY、UI_FATAL。它们由 Common 总头文件和 Core 总头文件导出，单独使用时链接 Core::Common 即可。

```cmake
target_link_libraries(MyApp PRIVATE Core::Common)
```

```cpp
#include <core/common/Assert.hpp>

void setVolume(int volume)
{
	UI_CHECK(volume >= 0, EErrorCode::InvalidArgument, "negative volume");
}
```

Error 保留消息、错误码和调用点 source_location。UI_CHECK 始终求值，失败默认抛出 Error。UI_ASSERT 在未定义 NDEBUG 时检查，默认输出位置与消息后 abort；定义 NDEBUG 时不求值条件。UI_VERIFY 始终求值，Debug 下与断言相同。UI_FATAL 默认记录后 abort。

errorPolicy() 的回调可在程序初始化阶段覆盖；修改策略时必须自行同步并发访问。自定义 OnAssert 可以返回；OnError 返回或为空时仍会抛出 Error，OnFatal 返回或为空时仍会终止。回调抛出的异常会直接传播。

Tag 是 functions 中的通用点分路径类型，通过 core/functions/Tag.hpp 包含，不再位于 serde 目录。此次迁移保持其现有行为，getFirstSameRoot 仍为未实现接口。

## 内存分配器

原 SeedAllocators 工程已迁移到 core/memory，公开接口仍在 core 命名空间。统一入口为 [Memory.hpp](src/core/includes/core/memory/Memory.hpp)，分配门面、STL allocator 和 PMR resource 定义在 [MemoryProxy.hpp](src/core/includes/core/memory/MemoryProxy.hpp)。线程缓存、中央池、页分配器、虚拟内存后端与诊断观察者随 core 一起构建、安装，不依赖原工程。

```cmake
target_link_libraries(MyApp PRIVATE Core::Memory)
```

```cpp
#include <core/memory/Memory.hpp>
#include <vector>

int main()
{
	core::AllocatorConfig config;
	core::AllocatorFacade::configure(config);
	void *block = core::AllocatorFacade::allocate(4096, 64);
	core::AllocatorFacade::deallocate(block, 4096, 64);
	{
		std::vector<int, core::EngineAllocator<int>> values(100, 42);
	}
	core::AllocatorFacade::flushDeferredFrees();
	core::AllocatorFacade::onThreadExit();
}
```

configure 必须在首次使用分配器前调用；释放时必须提供匹配的大小、对齐及生命周期入口。默认分配失败抛出 std::bad_alloc，nothrow 入口返回 nullptr。EngineMemoryResource 用于 std::pmr 容器，resource 的生命周期必须覆盖其使用者。工作线程退出时调用 onThreadExit 回收线程缓存；不要用系统 free/delete 释放 AllocatorFacade 返回的内存。

诊断选项支持 AUTO、ON、OFF；AUTO 在 Debug 开启，其余配置关闭：

| CMake 选项 | 默认值 | 用途 |
| --- | --- | --- |
| PRISMCORE_MEMORY_DEBUG_GUARDS | AUTO | 分配头、边界检查及填充模式 |
| PRISMCORE_MEMORY_UAF_DETECTION | AUTO | quarantine 延迟回收与释放后写入检查 |
| PRISMCORE_MEMORY_CAPTURE_STACK | AUTO | 诊断堆栈捕获 |
| PRISMCORE_MEMORY_ENABLE_GLOBAL_HOOKS | OFF | 可选进程级 malloc/new 等拦截 |

UAF quarantine 需要同时启用 DEBUG_GUARDS 和 UAF_DETECTION。这些宏由 Core::Memory 传递，消费端不要单独重定义；单配置安装包固定导出库构建时的值，多配置安装包应消费对应配置的库。

默认不替换外部程序的全局分配函数。显式开启 GLOBAL_HOOKS 会影响整个进程；静态链接时还需确保链接器保留 hooks 对象文件，平台动态符号拦截规则也可能限制覆盖范围。当前验证了 macOS 上 hooks 的编译和符号导出，尚未验证其运行时拦截或 Windows/Linux 行为。MemoryTracker 的手动记录接口与观察者不依赖全局 hooks。

分配器复用 [MPMCQueue.hpp](src/core/includes/core/containers/MPMCQueue.hpp)，不携带另一份队列实现。两份原实现的有界环、位置 CAS、序列号 acquire/release 发布与回收流程一致；原 size/empty 调用适配为 getSize/isEmpty。quarantine 使用 1024 槽位，队列满时弹出旧记录并释放后重试，重试失败直接回收当前块，因此延迟检测是有界、尽力而为的，不保证每次释放都有完整隔离窗口。getSize 只用于近似统计和预算，入队/出队结果决定所有权转移；分配器不调用 getSnapshot，也不将其视为并发安全快照。

## 反射与序列化

原独立 reflect 工程已迁移到 core，公开 API 仍位于 core 命名空间。原头文件路径替换为以下路径：

| 头文件 | 功能 |
| --- | --- |
| [TypeInfo.hpp](src/core/includes/core/reflect/TypeInfo.hpp) | 类型与字段信息、静态遍历 |
| [ReflectObject.hpp](src/core/includes/core/reflect/ReflectObject.hpp) | 类型擦除对象及复制、移动、类型检查 |
| [Reflect.hpp](src/core/includes/core/reflect/Reflect.hpp) | 反射宏、自动注册、运行时构造、成员访问、方法调用及继承查询 |
| [SerdeADT.hpp](src/core/includes/core/serde/SerdeADT.hpp) | 格式无关的中间数据结构 |
| [SerdeReflect.hpp](src/core/includes/core/serde/SerdeReflect.hpp) | 反射驱动的 toAdt/fromAdt 转换 |
| [SerdeEngine.hpp](src/core/includes/core/serde/SerdeEngine.hpp) | JSON/TOML 格式引擎 |
| [SerdeServer.hpp](src/core/includes/core/serde/SerdeServer.hpp) | 文件、目录与工作区服务 |
| [Tag.hpp](src/core/includes/core/functions/Tag.hpp) | 通用点分路径标签，位于 functions |
| [Serde.hpp](src/core/includes/core/serde/Serde.hpp) | 反射与序列化统一入口 |

只使用反射时链接 Core::Reflect 并包含 core/reflect/Reflect.hpp。运行时反射保留以 SerdeADT 传递参数与返回值的原 API，使用 serde 中的 ADT 与转换层，但不会包含 JSON/TOML 解析器。

使用完整序列化功能时链接 Core::Serde：

```cmake
target_link_libraries(MyApp PRIVATE Core::Serde)
```

```cpp
#include <core/serde/Serde.hpp>

struct Settings
{
	REFLECT_CLASS(Settings);
	REFLECT_FIELDS((int, volume), (std::string, name));
};

Settings settings{80, "player"};
core::SerdeJsonEngine engine;
const auto text = engine.serialize(core::toAdt(settings));
auto restored = core::fromAdt<Settings>(engine.parse(text));
```

REFLECT_CLASS、REFLECT_FIELDS、REFLECT_FUNCTIONS、ReflectRegistry、toAdt/fromAdt 与 SerdeServer 接口均保留。JSON 使用 nlohmann/json，TOML 使用 toml++，原单头文件及许可证随 core 携带并安装，稀疏检出不需要额外下载依赖。

当前支持 JSON 和 TOML 后端；Binary 枚举仍为预留接口。TOML 保留原行为，不支持的 null、binary、datetime 值会被跳过，不能用于这些类型的无损往返。

## 测试

测试选项 PRISMCORE_BUILD_TESTS 默认 OFF，仅根目录构建提供测试入口。启用时需要 tests 与 extern/Catch2 目录：

```sh
cmake -S . -B build-tests -DPRISMCORE_BUILD_TESTS=ON
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

测试覆盖反射元信息、对象生命周期、继承字段与方法、ADT 与 JSON/TOML 往返、JSON 二进制与日期值、文件服务及错误路径。原复杂对象示例保留在 [ReflectSerdeDemo.cpp](tests/ReflectSerdeDemo.cpp)，作为可选测试目标运行。

内存测试保留原分配器回归与使用示例，并增加 core 队列满/空、环回绕、多生产者多消费者逐项唯一性，以及 quarantine 超容量和并发 flush 测试。测试入口共七个 CTest 目标，包含 Tag 路径与 Error/Assert 策略、调用位置、宏求值行为回归。quarantine 专项在诊断关闭时跳过其检查，Debug 配置用于执行该项实际检测。已在 macOS 验证 Debug 静态、Debug 动态、Release 静态与 Release 动态配置，并验证静态/动态安装包及独立 core 消费；Windows 和 Linux 尚未运行验证。

## macOS 工具链

归档工具应与平台匹配。如果 Homebrew LLVM 与系统 ar 混用导致链接器报告 archive member '/' not a mach-o file，可在全新的构建目录显式选择系统归档工具：

```sh
cmake -S . -B build-native -DCMAKE_AR=/usr/bin/ar -DCMAKE_RANLIB=/usr/bin/ranlib
```