# Unreal Engine 通过 UHT 实现反射

## 1. 为什么 Unreal 需要自己实现反射

标准 C++ 没有像 C#、Java 那样完整的运行时反射机制。

例如：

```cpp
class Player
{
public:
    int Health;
    void Attack(int Damage);
};
```

程序运行以后，标准 C++ 并不会天然提供：

```cpp
FindProperty("Health");
FindMethod("Attack");
```

也就是说，程序不能直接在运行时查询一个类有哪些成员、成员是什么类型、函数参数是什么等信息。

但 Unreal Engine 的很多系统都依赖这些能力，例如：

- Blueprint
- 编辑器 Details 面板
- 序列化
- 垃圾回收 GC
- 网络 RPC
- 属性复制 Replication
- UObject 动态创建

因此 Unreal 在标准 C++ 之上构建了一套自己的反射系统。

---

## 2. Unreal 反射系统的核心思路

核心流程：

```text
UE C++ 头文件
    |
    | UHT 扫描
    v
生成反射代码
.generated.h / .gen.cpp
    |
    | C++ Compiler
    v
机器码
    |
    v
Unreal Runtime
    |
    +-- UClass
    +-- FProperty
    +-- UFunction
```

因此 Unreal 的反射不是 C++ 原生反射，而是：

> 宏 + UHT 代码生成 + 运行时元数据系统。

---

## 3. UHT 是什么

UHT 全称：

```text
Unreal Header Tool
```

它会扫描 Unreal C++ 头文件中的特殊宏，例如：

```cpp
UCLASS()
USTRUCT()
UENUM()
UPROPERTY()
UFUNCTION()
GENERATED_BODY()
```

例如：

```cpp
UCLASS()
class AMyCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Health;

    UFUNCTION(BlueprintCallable)
    void Attack(int32 Damage);
};
```

UHT 会读取这些标记并生成对应的反射注册代码。

---

## 4. UCLASS、UPROPERTY、UFUNCTION 是什么

这些都不是 C++ 关键字，而是 Unreal 定义的宏。

### UCLASS

```cpp
UCLASS()
class AMyActor : public AActor
```

表示这个类需要加入 Unreal 反射系统，运行时会对应一个 `UClass` 类型描述对象。

### UPROPERTY

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite)
float MoveSpeed;
```

表示这个字段需要加入属性反射系统。运行时通常对应 `FProperty` 及其派生类型，例如：

```text
FIntProperty
FFloatProperty
FObjectProperty
FStructProperty
FArrayProperty
```

### UFUNCTION

```cpp
UFUNCTION(BlueprintCallable)
void Attack();
```

表示函数需要加入函数反射系统，运行时对应 `UFunction`。

---

## 5. GENERATED_BODY() 的作用

支持 Unreal 反射的类通常会包含：

```cpp
GENERATED_BODY()
```

它可以理解为：

> 把 UHT 自动生成的代码接入当前 C++ 类型。

这些生成代码会涉及：

- 类型信息
- StaticClass 相关逻辑
- 注册逻辑
- 序列化相关声明
- UObject 反射辅助代码

因此 `GENERATED_BODY()` 是手写 C++ 与 UHT 生成代码之间的重要连接点。

---

## 6. .generated.h 与 .gen.cpp

假设有：

```text
MyCharacter.h
```

其中一般会：

```cpp
#include "MyCharacter.generated.h"
```

这个文件由 UHT 自动生成，通常必须放在当前头文件其他 include 之后。

UHT 还会生成类似：

```text
MyCharacter.gen.cpp
```

其中包含真正用于注册反射信息的 C++ 代码。

可以粗略理解成：

```text
注册 AMyCharacter
注册 Health 属性
注册 Attack 函数
```

真实代码会更复杂，但核心思想就是：

> 把手写类转换成一套可以被 Unreal Runtime 查询的元数据。

---

## 7. 一个最小示例

```cpp
UCLASS()
class AMyCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Health = 100;

    UFUNCTION(BlueprintCallable)
    void Attack(int32 Damage);
};
```

UHT 可以把它概念化为：

```text
AMyCharacter

Class:
    AMyCharacter

Parent:
    ACharacter

Properties:
    Health
        Type: int32
        EditAnywhere: true
        BlueprintReadWrite: true

Functions:
    Attack
        Parameter:
            Damage : int32
        BlueprintCallable: true
```

运行时 Unreal 就可以知道：

```text
AMyCharacter
 |
 +-- Health : int32
 |
 +-- Attack(int32)
```

---

## 8. 运行时的 UClass

对于支持反射的 UObject 类型，可以获得：

```cpp
UClass* Class = AMyCharacter::StaticClass();
```

`UClass` 可以理解为：

> 描述某个 UObject 类型的运行时对象。

它会记录：

- 类名
- 父类
- 属性
- 函数
- Flags
- 构造与类型信息

这和 C# 中：

```csharp
typeof(Player)
```

返回 `System.Type` 的思想很类似。

---

## 9. 运行时的 FProperty

假设：

```cpp
UPROPERTY(EditAnywhere)
int32 Health;
```

运行时会存在对应的属性描述信息。

概念上：

```text
FIntProperty

Name:
    Health

Type:
    int32

Owner:
    AMyCharacter
```

Unreal 可以通过反射遍历属性，例如：

```cpp
for (TFieldIterator<FProperty> It(MyClass); It; ++It)
{
    FProperty* Property = *It;
    UE_LOG(LogTemp, Log, TEXT("%s"), *Property->GetName());
}
```

---

## 10. 运行时的 UFunction

例如：

```cpp
UFUNCTION(BlueprintCallable)
void Attack(int32 Damage);
```

运行时会对应一个 `UFunction`。

可以通过类似：

```cpp
UFunction* Func = Object->FindFunction(TEXT("Attack"));
```

找到函数。

然后 Unreal 可以借助：

```cpp
ProcessEvent()
```

进行动态调用。

概念流程：

```text
"Attack"
   |
FindFunction
   |
   v
UFunction
   |
ProcessEvent
   |
   v
执行函数
```

---

## 11. Blueprint 为什么依赖反射

例如：

```cpp
UFUNCTION(BlueprintCallable)
void Attack();
```

Blueprint 编辑器之所以能生成 `Attack` 节点，是因为它读取到了对应的 `UFunction` 反射信息，而不是直接解析你的 C++ 源码。

```text
C++ Attack()
      |
     UHT
      |
  UFunction
      |
Blueprint Editor
      |
生成 Attack 节点
```

同理：

```cpp
UPROPERTY(BlueprintReadWrite)
int32 Health;
```

Blueprint 能访问 `Health`，也是因为它拥有对应的 `FProperty` 元数据。

---

## 12. Details 面板为什么依赖反射

例如：

```cpp
UPROPERTY(EditAnywhere)
float MoveSpeed = 600.f;
```

编辑器会自动显示：

```text
Move Speed    600
```

原因是编辑器可以通过反射得到：

```text
属性名: MoveSpeed
类型: float
标记: EditAnywhere
```

然后自动生成对应 UI。

---

## 13. 序列化为什么依赖反射

Unreal 需要保存 Actor、UObject、Asset、Level 等对象数据。

反射系统可以知道：

```text
属性名
属性类型
属性所在对象
如何读取和写入
```

因此可以遍历对象属性并完成自动序列化，而不必为每个 UObject 手写全部保存逻辑。

---

## 14. GC 为什么依赖反射

例如：

```cpp
UPROPERTY()
UTexture2D* Texture;
```

Unreal 的垃圾回收系统需要知道 UObject 之间的引用关系。

概念上：

```text
Root Object
    |
    v
AMyActor
    |
    v
Texture
```

反射信息可以帮助引擎发现这些 UObject 引用，从而进行可达性分析。

需要注意，现代 UE 的 UObject 引用管理还涉及 `TObjectPtr`、`AddReferencedObjects`、`FGCObject` 等机制，因此不能简单理解为“没有 UPROPERTY 的 UObject 指针一定会立刻被回收”。

---

## 15. RPC 为什么依赖反射

例如：

```cpp
UFUNCTION(Server, Reliable)
void ServerAttack();
```

`Server` 和 `Reliable` 都不是 C++ 原生语义。

UHT 会把这些说明符写入生成的函数元数据中。

运行时可以理解成：

```text
ServerAttack
    |
    v
UFunction
    |
    +-- Net
    +-- Server
    +-- Reliable
```

这样网络系统才能知道这个函数需要通过网络 RPC 机制执行。

---

## 16. Replication 为什么依赖反射

例如：

```cpp
UPROPERTY(Replicated)
int32 Health;
```

Unreal 网络系统会知道 `Health` 是一个可复制属性。

它会与：

```cpp
GetLifetimeReplicatedProps()
```

等机制配合完成网络同步。

因此：

```text
UPROPERTY(Replicated)
       |
       v
FProperty
       |
       v
Replication System
```

---

## 17. Unreal 反射的完整链路

例如：

```cpp
UCLASS()
class AMyCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Replicated)
    int32 Health;

    UFUNCTION(Server, Reliable)
    void ServerAttack();
};
```

完整流程：

```text
开发者编写 C++ Header
          |
          v
        UHT
          |
          +-- UCLASS
          +-- UPROPERTY
          +-- UFUNCTION
          |
          v
生成代码
          |
          +-- .generated.h
          +-- .gen.cpp
          |
          v
C++ Compiler
          |
          v
机器码
          |
          v
Unreal Runtime
          |
          +-- UClass
          +-- FProperty
          +-- UFunction
          |
          v
--------------------------------
| Blueprint                    |
| Details Panel                |
| Serialization                |
| Garbage Collection           |
| RPC                          |
| Replication                  |
--------------------------------
```

---

## 18. Unreal 与 C# 反射对比

C#：

```text
C# Source
   |
Compiler
   |
IL + Metadata
   |
CLR
   |
System.Type
   |
Reflection
```

Unreal：

```text
C++ Header
   |
UHT
   |
Generated Reflection Code
   |
C++ Compiler
   |
UClass / FProperty / UFunction
   |
UE Reflection
```

| 对比项 | C# | Unreal C++ |
|---|---|---|
| 反射支持 | .NET 原生 | Unreal 自己实现 |
| 元数据生成 | C# 编译器 | UHT |
| 类型描述 | System.Type | UClass |
| 字段描述 | FieldInfo / PropertyInfo | FProperty |
| 函数描述 | MethodInfo | UFunction |
| 运行环境 | CLR | Unreal Runtime |
| 是否依赖特殊宏 | 否 | 是 |

---

## 19. 普通 C++ 类型为什么不能直接使用 Unreal 反射

普通结构：

```cpp
struct FMyData
{
    int32 Value;
};
```

默认只是普通 C++ 类型。

如果需要 Unreal 反射：

```cpp
USTRUCT()
struct FMyData
{
    GENERATED_BODY()

    UPROPERTY()
    int32 Value;
};
```

这样 UHT 才会生成对应的反射信息。

因此：

```text
普通 C++ 类型
    !=
Unreal Reflection Type
```

---

## 20. UProperty 与 FProperty

旧版 Unreal Engine 中经常会看到：

```cpp
UProperty
```

现代 UE 中属性反射系统主要使用：

```cpp
FProperty
```

可以粗略理解为架构从：

```text
UObject
 |
UProperty
```

演进为：

```text
FField
 |
FProperty
```

但开发者使用的宏仍然叫：

```cpp
UPROPERTY()
```

要区分：

- `UPROPERTY`：给 UHT 识别的宏
- `FProperty`：运行时描述属性的 C++ 类型

---

## 21. 面试中的简洁解释

可以这样回答：

> 标准 C++ 没有 C# 那种完整的运行时反射，所以 Unreal 自己实现了一套反射系统。开发者通过 UCLASS、UPROPERTY、UFUNCTION 等宏标记需要参与反射的类型和成员，UHT 会在正式 C++ 编译之前扫描这些头文件，并生成 `.generated.h`、`.gen.cpp` 等额外代码。编译后，运行时通过 UClass、FProperty、UFunction 等对象保存类、属性和函数的元数据。Blueprint、编辑器属性面板、序列化、GC、RPC 和属性复制等系统都会使用这些反射信息。

如果继续追问 `GENERATED_BODY()`：

> 它是 UHT 生成代码接入手写 C++ 类型的重要位置。

---

## 22. 最终总结

Unreal Reflection 的核心不是：

```text
C++ 原生拥有反射
```

而是：

```text
C++代码
   +
特殊宏
   +
UHT代码生成
   +
运行时元数据对象
```

最终形成：

```text
UCLASS
UPROPERTY
UFUNCTION
    |
    v
   UHT
    |
    v
.generated.h / .gen.cpp
    |
    v
C++ Compiler
    |
    v
UClass / FProperty / UFunction
    |
    v
UE Reflection System
```

因此可以把 Unreal 的反射理解为：

> Unreal 在标准 C++ 上通过 UHT 进行代码生成，并在运行时维护额外的类型元数据，从而实现了一套类似 C# Reflection 的机制。
