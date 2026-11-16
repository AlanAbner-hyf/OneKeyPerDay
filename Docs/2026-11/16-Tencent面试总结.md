### What：腾讯面试总结（AI总结）

### Why：面着玩了，没做准备，问了很多基础

### Content：

##### 数学

向量点积和叉积的区别：

- **点积得到标量**：`a·b = ax*bx + ay*by + az*bz = |a||b|cosθ`，衡量方向的一致程度，常用于求夹角、投影。
- **叉积得到向量（三维）**：`a×b = (ay*bz-az*by, az*bx-ax*bz, ax*by-ay*bx)`，垂直于两向量，方向按右手定则；长度为 `|a||b|sinθ`，等于两向量张成的平行四边形面积。常用于求法线、判断转向。
- **区别**：`a·b = b·a`，但 `a×b = -(b×a)`。非零向量点积为 0 表示垂直，叉积为零向量表示平行。

例：`a=(1,0,0)`、`b=(0,1,0)`，则 `a·b=0`，`a×b=(0,0,1)`。角色朝向为 `f`，指向敌人的向量为 `d`，`f·d>0` 表示敌人在前方半空间。

矩阵的特征值和特征向量：

- **定义**：对方阵 A，若 `Av=λv` 且 `v≠0`，则 λ 是特征值，v 是对应的特征向量。
- **几何意义（实特征值和实特征向量）**：变换后仍在原来那条直线上；λ 表示缩放倍数，λ<0 时反向，λ=0 时压缩为零向量。
- **求法**：先解 `det(A-λI)=0`，再解 `(A-λI)v=0`。

例：`A=diag(2,3)` 将 x 方向放大 2 倍、y 方向放大 3 倍。因此 `(1,0)` 对应 λ=2，`(0,1)` 对应 λ=3；`(1,1)` 变为 `(2,3)`，不与原向量共线，所以不是特征向量。

注意：实矩阵不一定有实特征值，例如二维旋转 90° 的矩阵；有 n 个线性无关特征向量的 n 阶矩阵才可对角化。

##### 计算机

C++野指针：

- **未初始化指针**：例如函数内的 `int* p;`，没有确定的有效指向。
- **悬空指针**：对象已释放或生命周期已结束，指针仍保存原地址；如 `delete` 后继续使用、返回局部变量地址、vector 扩容触发重新分配后使用旧元素指针。
- **规避**：初始化为 `nullptr`，明确对象生命周期和所有权，优先使用 RAII、`std::unique_ptr` 等管理资源。非空判断不能证明指针有效。

```cpp
int* p = new int(42);
int* q = p;
delete p;       // p、q 都成为悬空指针
p = nullptr;   // 只清空 p，q 仍然悬空
// int x = *q;  // 未定义行为
```

C++中free和delete的区别：

| 对比     | free                      | delete / delete[]               |
| -------- | ------------------------- | ------------------------------- |
| 本质     | C 标准库函数              | C++ 运算符                      |
| 配对     | malloc / calloc / realloc | new / new[]                     |
| 对象析构 | 不调用析构函数            | 析构对象，再释放存储空间        |
| 数组处理 | 释放整块分配的内存        | delete[] 析构各元素，再释放空间 |

**必须配对，混用是未定义行为**；对值为 nullptr 的对象指针调用 free 或 delete 均安全，释放后都不会自动把原指针置空。[C++ 标准草案：delete](https://eel.is/c++draft/expr.delete)

```cpp
auto p = new std::string("hello");
delete p;                  // 析构 string，再释放对象的存储空间
auto a = new int[10];
delete[] a;                // new[] 对应 delete[]
void* buffer = std::malloc(100);
std::free(buffer);         // malloc 对应 free
```

线程锁的类型：

| 类型       | 特点                                     | 典型场景 / C++ 对应                |
| ---------- | ---------------------------------------- | ---------------------------------- |
| 互斥锁     | 同时只允许一个线程持有，竞争时可阻塞等待 | 保护共享队列；std::mutex           |
| 自旋锁     | 获取失败就循环尝试，占用 CPU             | 临界区极短、预计等待很短           |
| 读写锁     | 多个读者可并行，写者独占                 | 读多写少的缓存；std::shared_mutex  |
| 递归锁     | 同一线程可重复加锁，需等次数解锁         | 存在重入调用；std::recursive_mutex |
| 定时互斥锁 | 支持超时获取，避免无限等待               | 有等待时限的任务；std::timed_mutex |

这些分类维度可组合，如递归定时互斥锁。乐观锁是先操作、再通过版本号或 CAS 校验冲突的策略；悲观锁是先加锁再操作。

例：多个线程修改同一计数器，用 RAII 管理锁，离开作用域自动解锁：

```cpp
std::mutex m;
int count = 0;
void increment() {
    std::lock_guard<std::mutex> guard(m);
    ++count; // 其他并发读写 count 的地方也必须受同一把锁保护
}
```

注意：`lock_guard`、`unique_lock` 是锁的管理器；条件变量和信号量是同步工具，不应直接当成互斥锁类型。

自旋锁如何实现：

核心是**原子地检查并占有锁**。下面用 `atomic_flag` 的 test-and-set 实现（C++11）：

```cpp
#include <atomic>
#include <mutex>

class SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {}
    }
    void unlock() {
        flag.clear(std::memory_order_release);
    }
};

SpinLock lock;
int count = 0;
void increment() {
    std::lock_guard<SpinLock> guard(lock);
    ++count;
}
```

- `test_and_set` 原子地置为 true 并返回旧值：返回 false 表示成功获得锁，返回 true 则继续等待。[C++ 标准草案：atomic_flag](https://eel.is/c++draft/atomics.flag)
- `release` 解锁与后续成功的 `acquire` 加锁配合，保证临界区内的修改对下一持锁线程可见。普通 bool 或 volatile 都不能替代原子操作。
- 这是最小实现，不保证公平，也不支持重入；长时间等待会浪费 CPU，实际可加入退避或处理器 pause 指令。临界区较长时优先使用互斥锁。

DrawCall：

- **定义**：通过图形 API 提交的一次绘制命令，让 GPU 按指定的渲染状态绘制一批图元。一次调用可画多个三角形或多个实例，不等于一个物体。
- **开销**：CPU 准备和提交命令、切换渲染状态均有成本；DrawCall 多时可能形成 CPU 瓶颈。GPU 开销还取决于顶点数、像素数和 Shader 复杂度。
- **数量关系**：一个物体可能因多个子网格、材质、Pass 或阴影绘制产生多次调用；多个物体也可能通过合批或实例化共享调用。
- **优化**：复用材质、合批、GPU Instancing、剔除不可见物体。减少调用次数不保证一定更快，需同时观察 CPU / GPU 帧耗时。[Unity 官方文档：DrawCall 优化](https://docs.unity.com/en-us/engine/6000.6/manual/analysis/graphics-performance-profiling/reduce-draw-calls/optimizing-draw-calls)

例：100 棵树使用相同 Mesh、相同材质，假设只有一个子网格和一个渲染 Pass，不合批时可产生 100 次 DrawCall；支持 GPU Instancing 时，可在实例数量限制内合为一次实例化绘制。[Unity 官方文档：GPU Instancing](https://docs.unity.com/en-us/engine/6000.6/manual/analysis/graphics-performance-profiling/reduce-draw-calls/gpuinstancing)
