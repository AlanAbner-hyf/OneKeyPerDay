# C# Reflection 原理总结

## 1. 什么是反射（Reflection）

反射（Reflection）是指：

> 程序在运行时能够获取自身类型信息，并动态访问、创建和操作对象的能力。

例如：

``` csharp
class Player
{
    public int Health;

    public void Attack()
    {
    }
}
```

运行时：

``` csharp
Type type = typeof(Player);
```

可以获取 Player 类型信息，并进一步： - 查看字段 - 查看方法 -
获取类型信息 - 动态调用方法 - 动态创建对象

------------------------------------------------------------------------

# 2. C# 反射为什么可以实现？

C# 反射依赖于 .NET CLR 保存的类型元数据（Metadata）。

编译流程：

    Player.cs
        |
        | C# Compiler
        ↓
    Player.dll

生成的 DLL 不仅包含执行代码，还包含：

    Player.dll

    ├── IL代码（Intermediate Language）
    ├── Metadata（元数据）
    └── Assembly信息

其中 Metadata 是反射的数据来源。

------------------------------------------------------------------------

# 3. Metadata（元数据）

Metadata 保存程序结构信息。

例如：

``` csharp
public class Player
{
    public int Health;

    public void Attack(int damage)
    {
    }
}
```

编译后 Metadata 会记录：

    Type:
    {
        Name: Player

        Fields:
        {
            Name: Health
            Type: int
        }

        Methods:
        {
            Name: Attack
            Parameters: int damage
        }
    }

因此 CLR 可以知道：

-   类名
-   字段
-   字段类型
-   方法
-   参数列表

------------------------------------------------------------------------

# 4. IL（Intermediate Language，中间语言）

## 4.1 IL是什么？

IL 是 .NET 平台中的中间代码。

C# 源代码不会直接编译成 CPU 可以执行的机器码，而是先编译成 IL，然后由
CLR 中的 JIT 编译器转换为机器码执行。

执行流程：

    C#代码
     |
     | C# Compiler
     ↓
    IL + Metadata
     |
     | CLR加载
     ↓
    JIT编译器
     |
     ↓
    CPU机器码
     |
     ↓
    程序执行

------------------------------------------------------------------------

## 4.2 为什么需要 IL？

如果 C# 直接生成机器码：

    C#代码
     |
     ↓
    x86机器码

那么不同平台需要重新编译。

例如：

    Windows x86
    Linux ARM
    Mac ARM

都需要不同机器码。

而 IL：

    C#代码
     |
     ↓
    IL
     |
     ↓
    不同平台CLR
     |
     ↓
    对应机器码

使同一个 DLL 可以在不同平台运行。

------------------------------------------------------------------------

## 4.3 IL示例

C#：

``` csharp
public int Add(int a, int b)
{
    return a + b;
}
```

对应 IL：

``` il
.method public int32 Add(int32 a, int32 b)
{
    ldarg.1
    ldarg.2
    add
    ret
}
```

含义：

  IL指令   作用
  -------- ----------
  ldarg    加载参数
  add      执行加法
  ret      返回结果

IL 可以理解为：

    高级语言
        ↑
        |
        C#

    中间语言
        ↑
        |
        IL

    机器语言
        ↑
        |
        CPU机器码

------------------------------------------------------------------------

# 5. IL 与 Metadata 的关系

.NET DLL：

    Player.dll

    ├── IL代码
    │
    ├── Metadata
    │
    └── Assembly信息

两者职责不同：

## IL负责：

> 程序如何执行

例如：

``` csharp
Health -= damage;
```

对应具体执行逻辑。

## Metadata负责：

> 程序是什么

例如：

    Class:
     Player

    Field:
     Health:int

    Method:
     Attack(int)

二者结合：

-   Metadata 让 CLR 知道有哪些类型、字段和方法
-   IL 让 CLR 知道这些方法如何执行

------------------------------------------------------------------------

# 6. JIT（Just-In-Time Compiler）

JIT 是 CLR 中负责即时编译的组件。

作用：

    IL
     |
     ↓
    JIT
     |
     ↓
    机器码

例如第一次调用：

``` csharp
player.Attack(10);
```

CLR：

    发现 Attack 没有机器码

    ↓

    读取 IL

    ↓

    JIT 编译

    ↓

    缓存机器码

之后再次调用：

    直接执行机器码

------------------------------------------------------------------------

# 7. Type对象

反射核心对象：

``` csharp
System.Type
```

例如：

``` csharp
Type type = typeof(Player);
```

得到的是描述 Player 类型信息的 Type 对象，而不是 Player 实例。

关系：

    Player实例

    player
     |
     v
    Player


    类型信息

    typeof(Player)
     |
     v
    System.Type

------------------------------------------------------------------------

# 8. 字段反射

例如：

``` csharp
FieldInfo field =
    typeof(Player).GetField("Health");
```

流程：

    GetField("Health")

            |
            v

    System.Type

            |
            v

    查询Metadata

            |
            v

    FieldInfo

然后：

``` csharp
field.SetValue(player,100);
```

可以动态修改字段。

------------------------------------------------------------------------

# 9. 方法反射

例如：

``` csharp
MethodInfo method =
    typeof(Player).GetMethod("Attack");

method.Invoke(
    player,
    new object[]{50}
);
```

流程：

    MethodInfo

    保存:
    - 方法名称
    - 参数列表
    - 返回类型

            |
            v

    CLR调用器

            |
            v

    执行方法

------------------------------------------------------------------------

# 10. CLR为什么支持反射？

.NET 从设计阶段就考虑：

-   类型安全
-   垃圾回收
-   动态加载
-   跨语言运行

因此 CLR 必须知道：

    对象是什么类型？
    有哪些字段？
    有哪些方法？
    参数是什么？

这些信息由 Metadata 保存。

------------------------------------------------------------------------

# 11. C++为什么没有类似反射？

标准 C++：

``` cpp
class Player
{
public:
    int Health;
    void Attack(int damage);
};
```

编译后主要变为机器码。

运行时通常不知道：

    这个变量是否叫 Health
    这个函数是否叫 Attack

因为类型名称和成员信息不会默认保留。

------------------------------------------------------------------------

# 12. C++如何实现类似反射？

C++ 可以通过代码生成实现反射。

例如：

Qt:

    Q_OBJECT
     |
    MOC生成元数据

Unreal:

    UCLASS()
    UPROPERTY()
    UFUNCTION()

            |
            v

    Unreal Header Tool

            |
            v

    UClass
    FProperty
    UFunction

------------------------------------------------------------------------

# 13. C# Reflection 与 Unreal Reflection 对比

               C# Reflection   Unreal Reflection
  ------------ --------------- -------------------
  实现方式     CLR原生支持     UHT代码生成
  信息来源     Metadata        生成代码
  支持类型     所有.NET类型    主要UObject
  运行时查询   支持            支持
  动态调用     支持            支持

------------------------------------------------------------------------

# 14. 总结

反射的本质：

> 让程序在运行时拥有一份描述自身结构的数据。

C#：

    代码
     |
    编译器
     |
    IL + Metadata
     |
    CLR
     |
    System.Type
     |
    Reflection API

Unreal：

    C++代码
     |
    UHT扫描宏
     |
    生成反射代码
     |
    UClass/FProperty/UFunction
     |
    UE Reflection System

二者思想相同：

> 都是在运行时保存额外的类型描述信息，从而实现动态访问和操作。
