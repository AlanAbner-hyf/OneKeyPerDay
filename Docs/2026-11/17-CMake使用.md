### What：CMake使用总结

### Why：C++的一款重要工具

### Content：

众所周知，C++代码需要经过预处理，编译，汇编，链接一系列操作才能被执行，这是个麻烦的过程，每次修改一点代码都需要弄这么几下。

所以，CMake这种工具就出来了，在我的理解中，CMake就是将这个几个步骤整合了一下，是的这几个步骤合并为一个步骤，一次操作就可以了，并且可以将编译的一些配置保存为配置文件，方便后续编译使用。

AI的一句话解释为：CMake 是一个跨平台的构建配置工具，你用它描述源码、依赖和编译要求，它便生成构建规则并调用构建工具完成程序的编译与链接。

**CMakeLists.txt**是CMake工具的一个重要文件，在我的理解中被用来保存编译、链接等配置。

AI的一句话解释为：CMakeLists.txt 是 CMake 的项目配置文件，用来声明构建目标、源码、依赖和编译要求，告诉 CMake 如何构建项目。

**add_library**用来添加一个库，是CMake中定义库目标的命令，用来指定库的名称、类型（如静态库、动态库或接口库）以及相关源码，供其它目标依赖和使用。

**add_executable**用来添加一个可执行程序，用来指定目标名称以及相关源码文件。

**target_include_directories**用来添加一个头文件搜索目录，并通过PRIVATE、PUBLIC、INTERFACE控制这些目录是供目标自己（PRIVATE）、使用者（INTERFACE）还是两者使用（PUBLIC）。

**target_compile_definitions**用来为目标设置编译时的预处理宏，并通过PRIVATE、PUBLIC、INTERFACE控制这些宏是供目标自身、使用者，还是两者使用。

**target_link_libraries**用来声明目标依赖的库并获取其使用要求，通过PRIVATE、PUBLIC、INTERFACE控制依赖是供目标自身、使用者，还是两者使用。
