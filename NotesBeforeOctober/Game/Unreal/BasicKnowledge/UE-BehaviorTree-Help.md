# UE5 Behavior Tree（行为树）系统详解

## 1. 行为树概述

行为树（Behavior Tree，简称 BT）是一种层次化的人工智能决策系统，常用于控制游戏 NPC 的行为。

它的核心作用是：

**根据 NPC 当前的环境信息与自身状态，选择应该执行的行为，并管理行为的执行顺序、切换和中断。**

例如，一个游戏 NPC 可能具有以下行为：

- 没有发现玩家时，随机巡逻。
- 发现可疑玩家时，靠近并观察。
- 确认玩家身份后，追逐并攻击。
- 遇到危险时，选择逃跑。
- 失去目标视野后，前往最后发现的位置搜索。

如果完全使用 `if-else` 实现，随着 NPC 行为复杂度提高，决策代码会越来越难以维护。

行为树通过将决策逻辑组织为树形结构，使 AI 逻辑更加模块化、可复用、易于调试。

---

## 2. UE5 AI 系统整体架构

UE5 的 AI 系统主要包括以下组件：

| 组件              | 作用                           |
| ----------------- | ------------------------------ |
| AIController      | 控制 NPC，负责组织 AI 决策逻辑 |
| Behavior Tree     | 根据条件选择并执行行为         |
| Blackboard        | 保存 AI 决策需要的数据         |
| AI Perception     | 感知玩家和环境变化             |
| Task              | 执行移动、攻击等具体操作       |
| Navigation System | 提供寻路与导航能力             |
| EQS               | 查询环境中的合适位置或目标     |

### 2.1 整体工作流程

```text
                NPC Character
                      |
                AIController
                      |
          +-----------+-----------+
          |                       |
    AI Perception            Behavior Tree
          |                       |
          v                       |
      Blackboard <----------------+
          |             读取决策数据
          |
          v
     Behavior Tree
          |
          v
         Task
          |
    +-----+------+
    |            |
  MoveTo       Attack
    |            |
 Navigation    Gameplay
  System        Logic
```

典型执行过程：

1. AI Perception 发现玩家。
2. AIController 接收感知事件。
3. AIController 将目标信息写入 Blackboard。
4. Behavior Tree 根据黑板数据判断应该执行什么行为。
5. Task 执行具体动作。
6. 当黑板数据发生变化时，行为树可以重新评估条件并切换分支。

需要注意：

- Behavior Tree 本身不负责感知。
- Blackboard 本身不负责决策。
- Task 负责具体操作，但不一定直接实现底层寻路算法。
- AIController 通常负责组织这些系统之间的交互。

---

## 3. 行为树的核心节点

UE5 行为树主要包括以下节点：

1. Root（根节点）
2. Composite（组合节点）
3. Task（任务节点）
4. Decorator（装饰器节点）
5. Service（服务节点）

其中 Composite、Task、Decorator 和 Service 是最重要的部分。

### 3.1 Composite：组合节点

Composite 负责控制子节点的执行顺序。

最常用的组合节点是：

- Selector
- Sequence
- Simple Parallel

### 3.2 Selector：选择节点

Selector 按照从左到右的顺序尝试子节点。

执行规则：

- 子节点返回 Failed：继续执行下一个子节点。
- 子节点返回 Succeeded：整个 Selector 返回成功。
- 子节点返回 InProgress：等待当前子节点继续运行。
- 所有子节点都失败：整个 Selector 返回失败。

例如：

```text
             Selector
                |
      +---------+---------+
      |         |         |
    Attack    Escape    Patrol
```

假设 NPC 当前没有攻击目标，也不存在危险：

1. Attack 条件不满足，失败。
2. Escape 条件不满足，失败。
3. Patrol 开始执行。

如果 Patrol 成功，则 Selector 返回成功。

Selector 可以理解为：

**按照优先级依次尝试，直到找到能够成功执行的分支。**

通常越靠左的节点优先级越高。

注意：

Selector 并不意味着每一帧都会自动从左到右重新检查所有节点。

UE5 可以通过 Decorator 的 Observer Aborts 机制，在条件变化时中断低优先级分支。

### 3.3 Sequence：顺序节点

Sequence 依次执行子节点。

执行规则：

- 子节点返回 Succeeded：继续执行下一个子节点。
- 子节点返回 Failed：整个 Sequence 立即失败。
- 子节点返回 InProgress：等待当前子节点完成。
- 所有子节点成功：整个 Sequence 返回成功。

例如：

```text
             Sequence
                |
      +---------+---------+
      |         |         |
   MoveTo     Rotate     Attack
```

执行过程：

1. NPC 移动到玩家附近。
2. 转向玩家。
3. 执行攻击。

如果移动失败，则后续 Rotate 和 Attack 不会执行。

Sequence 可以理解为：

**只有前一个步骤成功，才会执行下一个步骤。**

### 3.4 Selector 与 Sequence 对比

| 特性         | Selector             | Sequence             |
| ------------ | -------------------- | -------------------- |
| 主要用途     | 选择行为             | 顺序执行行为         |
| 子节点成功   | 整体成功             | 继续下一个           |
| 子节点失败   | 尝试下一个           | 整体失败             |
| 子节点运行中 | 等待                 | 等待                 |
| 全部节点成功 | 第一个成功时已结束   | 整体成功             |
| 全部节点失败 | 整体失败             | 第一个失败时已结束   |
| 典型场景     | 攻击、逃跑、巡逻选择 | 移动、转向、攻击流程 |

---

## 4. Task：任务节点

Task 是行为树中真正执行操作的节点。

常见内置 Task：

- Move To：移动到指定 Actor 或位置。
- Wait：等待一定时间。
- Rotate to Face BB Entry：面向黑板指定目标。
- Run EQS Query：执行环境查询。
- Run Behavior：运行其他行为树。

也可以使用 C++ 或蓝图实现自定义 Task。

例如：

- BTTask_Attack
- BTTask_FindPatrolLocation
- BTTask_Escape
- BTTask_ObserveTarget

### 4.1 自定义 Task

C++ 中通常继承 `UBTTaskNode`。

示例：

```cpp
EBTNodeResult::Type UBTTask_DoAttack::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory)
{
    AAIController* AIController =
        OwnerComp.GetAIOwner();

    if (!AIController)
    {
        return EBTNodeResult::Failed;
    }

    // 执行攻击逻辑

    return EBTNodeResult::Succeeded;
}
```

Task 的常见执行状态：

| 状态       | 含义                                 |
| ---------- | ------------------------------------ |
| Succeeded  | 任务成功完成                         |
| Failed     | 任务执行失败                         |
| InProgress | 任务仍在执行                         |
| Aborted    | 任务被中止，通常出现在中止处理流程中 |

### 4.2 同步 Task 与异步 Task

同步 Task：

```text
ExecuteTask
    |
执行操作
    |
返回 Succeeded 或 Failed
```

例如立即设置 Blackboard 中某个值。

异步 Task：

```text
ExecuteTask
    |
启动移动或动画
    |
返回 InProgress
    |
等待异步操作完成
    |
FinishLatentTask
    |
返回最终执行结果
```

如果 Task 返回 `InProgress`，任务结束时通常需要调用：

```cpp
FinishLatentTask(
    OwnerComp,
    EBTNodeResult::Succeeded
);
```

否则，行为树可能一直认为该任务没有完成。

需要注意，Task 被中止时也应该正确处理未完成的移动、定时器或其他异步操作，避免产生无效回调。

---

## 5. Decorator：装饰器节点

Decorator 用于控制节点或分支能否执行，以及在条件发生变化时是否需要中断当前行为。

例如：

```text
Selector
    |
    +-- Sequence [HasTarget == true]
    |       |
    |       +-- MoveTo Target
    |       |
    |       +-- Attack
    |
    +-- Patrol
```

只有当 Blackboard 中 `HasTarget == true` 时，攻击分支才允许执行。

### 5.1 Observer Aborts

Observer Aborts 是 UE 行为树中非常重要的机制。

常见模式：

| 模式           | 含义                                     |
| -------------- | ---------------------------------------- |
| None           | 条件变化不会主动中断分支                 |
| Self           | 条件不再满足时，中断当前受控分支         |
| Lower Priority | 高优先级分支满足条件时，中断低优先级分支 |
| Both           | 同时支持 Self 和 Lower Priority          |

例如：

```text
Selector
    |
    +-- Attack
    |     Decorator:
    |     HasTarget == true
    |     Observer Aborts = Both
    |
    +-- Patrol
```

NPC 正在 Patrol 时：

1. AI Perception 发现玩家。
2. Blackboard 中 `HasTarget` 更新为 true。
3. Decorator 观察到条件变化。
4. 行为树重新评估攻击分支。
5. 中断正在执行的 Patrol。
6. 切换到 Attack。

如果攻击过程中 `HasTarget` 变成 false，Self 中断机制也可以终止攻击分支。

注意：

- Lower Priority 主要影响右侧低优先级分支。
- Self 主要影响装饰器所控制的当前分支。
- 中断需要 Decorator 具备相应的观察能力。
- 修改 Blackboard 值不意味着所有行为都会无条件立即中断。

### 5.2 Decorator 的应用场景

例如：

- 判断是否存在攻击目标。
- 判断目标是否处于攻击范围内。
- 判断生命值是否低于阈值。
- 判断 NPC 是否拥有目标视线。
- 判断当前高层行为意图是否为 Attack。

---

## 6. Service：服务节点

Service 通常用于周期性更新行为树所需的数据。

例如：

- 更新目标距离。
- 更新可疑程度。
- 判断是否需要重新选择目标。
- 计算 NPC 与玩家的相对位置。
- 更新 Blackboard 中的状态。

示例：

```text
Sequence
    |
    +-- Service: UpdateSuspicion
    |
    +-- MoveTo Target
    |
    +-- Observe Target
```

当对应分支处于活跃状态时，Service 可以按照设定间隔执行。

例如每 0.5 秒更新一次可疑度。

### 6.1 Service 与 Task 的区别

| 对比项                 | Task                 | Service                    |
| ---------------------- | -------------------- | -------------------------- |
| 主要职责               | 执行具体行为         | 周期性辅助更新             |
| 是否属于执行流程       | 是                   | 依附于节点                 |
| 是否提供成功或失败结果 | 是                   | 通常不直接决定任务成功失败 |
| 是否可以定时执行       | 可以，但需要自行实现 | 内置周期执行机制           |
| 常见用途               | 移动、攻击、等待     | 更新距离、可疑度、目标信息 |

Service 不是独立线程。

它由行为树系统按照配置的时间间隔进行调度。

---

## 7. Blackboard：黑板系统

Blackboard 是 UE AI 系统中的键值数据存储组件。

Behavior Tree 可以通过 Blackboard 获取决策所需的信息。

例如：

| Key              | 类型   | 含义                   |
| ---------------- | ------ | ---------------------- |
| CurrentTarget    | Object | 当前目标 Actor         |
| HasLOS           | Bool   | 是否拥有目标视线       |
| LastSeenLocation | Vector | 最后一次发现目标的位置 |
| SuspicionLevel   | Float  | 可疑程度               |
| PatrolLocation   | Vector | 巡逻目标位置           |
| Intent           | Enum   | 高层行为意图           |

### 7.1 C++ 更新 Blackboard

```cpp
UBlackboardComponent* BB =
    GetBlackboardComponent();

BB->SetValueAsObject(
    TEXT("CurrentTarget"),
    TargetActor
);

BB->SetValueAsBool(
    TEXT("HasLOS"),
    true
);
```

行为树中的 Task、Decorator、Service 可以读取这些数据。

### 7.2 Blackboard 的主要价值

Blackboard 将 AI 的数据与行为逻辑分离。

例如：

```text
AI Perception
     |
     v
更新 CurrentTarget
     |
     v
Blackboard
     |
     v
Decorator 判断 HasTarget
     |
     v
Behavior Tree 切换攻击分支
```

这样，感知系统不需要直接控制攻击 Task。

它只需要维护目标信息，具体决策交给行为树完成。

---

## 8. UE 行为树的事件驱动机制

传统行为树通常采用周期性更新模式：

```text
每次 Tick
    |
从根节点开始遍历
    |
检查条件
    |
选择分支
    |
执行任务
```

UE 的行为树采用了事件驱动优化。

当 NPC 正在执行一个持续性任务时，如果没有相关条件变化，就不需要每帧重新遍历整棵行为树。

例如：

```text
NPC 正在巡逻
      |
AI Perception 发现玩家
      |
Blackboard 更新目标
      |
Decorator 观察到变化
      |
中断 Patrol
      |
切换 Attack
```

这种设计能够减少不必要的条件检查。

尤其是在大量 NPC 同时运行时，可以降低 AI 决策逻辑的重复执行开销。

但事件驱动不意味着 UE 行为树完全不使用 Tick。

例如：

- Service 可以周期性执行。
- Task 可以启用 Tick。
- AI Perception 有自己的感知更新机制。
- CharacterMovement 和导航系统也有各自的更新流程。

UE 优化的是行为树不必要的重复搜索，而不是取消所有周期性更新。

---

## 9. FSM 与 Behavior Tree 的区别

FSM 是 Finite State Machine，即有限状态机。

FSM 通常由以下元素构成：

- State：状态
- Transition：状态转换
- Condition：状态转换条件

例如：

```text
          发现敌人
    Idle ----------> Attack
     ^                 |
     |                 |
     +-----------------+
           失去目标
```

行为树则更适合描述一个行为的执行流程。

例如：

```text
Attack Behavior Tree
          |
       Sequence
          |
     +----+----+
     |    |    |
   Move Rotate Attack
```

### 9.1 FSM 与 BT 对比

| 对比项   | FSM                | Behavior Tree          |
| -------- | ------------------ | ---------------------- |
| 核心概念 | 状态与状态转换     | 节点与执行流程         |
| 适合场景 | 高层行为模式切换   | 复杂行为组织           |
| 扩展方式 | 增加状态与转换     | 增加子树和节点         |
| 优势     | 状态明确、容易理解 | 模块化、可复用         |
| 局限     | 状态多时转换复杂   | 大型行为树也可能难维护 |

### 9.2 FSM + BT 混合架构

在复杂游戏 NPC 中，可以使用 FSM 管理高层意图，再使用 BT 完成状态内部的行为。

例如：

```text
               FSM
                |
       +--------+--------+
       |        |        |
      Idle    Attack    Escape
       |        |        |
       v        v        v
    IdleBT   AttackBT  EscapeBT
       |        |        |
     Patrol   MoveTo   FindSafePos
              Rotate   MoveToSafe
              Attack   Hide
```

在 UE 中，这些行为既可以组织为独立的行为树，也可以通过一棵主行为树中的多个子树实现。

例如可以使用 Blackboard 中的 `Intent` 作为分支选择依据。

### 9.3 混合架构的设计原则

建议：

**FSM 负责决定做什么，BT 负责决定怎么做。**

例如：

FSM 决定当前 NPC 应进入 Attack 状态。

BT 决定：

1. 应该追逐哪个目标。
2. 应该如何接近目标。
3. 是否需要调整攻击位置。
4. 何时转向目标。
5. 何时执行攻击。

应避免 FSM 和 BT 同时维护互相冲突的高层决策逻辑。

---

## 10. 项目示例：拟人化 NPC AI

以《WhoAreYouAndWhoAmI》项目为例。

NPC 具有以下能力：

- 不完美视觉感知。
- 可疑度系统。
- 拟人化巡逻。
- 攻击与逃跑意图。
- LastSeenLocation 搜索。
- 基于 EQS 的位置选择。

### 10.1 AI 执行流程

```text
           AI Perception
                 |
                 v
            感知到玩家
                 |
                 v
          更新 Blackboard
                 |
                 v
          Suspicion System
                 |
                 v
            FSM 决策
                 |
       +---------+---------+
       |         |         |
      Idle     Attack    Escape
       |         |         |
       v         v         v
   随机巡逻    追逐攻击    逃跑躲避
```

### 10.2 Attack 子树

```text
Sequence [Intent == Attack]
    |
    +-- CheckTarget
    |
    +-- MoveToTarget
    |
    +-- RotateToTarget
    |
    +-- DoAttack
```

### 10.3 Escape 子树

```text
Sequence [Intent == Escape]
    |
    +-- FindSafeLocation
    |
    +-- MoveToSafeLocation
    |
    +-- WaitUntilSafe
```

其中 FindSafeLocation 可以通过 EQS 获取合理逃跑位置。

### 10.4 失去视野后的行为

当 NPC 失去玩家视野时：

1. AI Perception 更新 HasLOS。
2. 保留 LastSeenLocation。
3. 根据可疑程度判断是否继续搜索。
4. 如果需要搜索，则进入搜索行为。
5. 移动到 LastSeenLocation。
6. 如果未重新发现目标，则逐渐降低可疑度并恢复普通行为。

这样可以避免 NPC 在失去视野后立即知道玩家的准确位置。

---

## 11. C++ 开发注意事项

### 11.1 行为树节点可能被多个 NPC 共享

UE 默认的原生行为树节点通常不是每个 NPC 独立创建一个对象实例。

多个 AI 运行同一个 Behavior Tree 时，可能使用共享的节点对象。

因此，不应该随意把每个 NPC 的独立运行状态存放在非实例化 Task 的普通成员变量中。

例如：

```cpp
// 非实例化节点中不宜用普通成员变量
// 保存某个 NPC 独有的任务执行状态。

AActor* CurrentTarget;
float CurrentTimer;
```

因为这些数据可能被多个 NPC 的执行过程共同访问。

合适的方案包括：

1. 使用 Blackboard 保存 AI 决策数据。
2. 使用 NodeMemory 保存任务的运行时数据。
3. 必要时启用节点实例化机制。

### 11.2 NodeMemory 的作用

`NodeMemory` 用于保存每个行为树实例对应节点的运行时数据。

例如：

```cpp
struct FAttackTaskMemory
{
    float ElapsedTime;
    bool bAttackStarted;
};
```

这样，即使多个 NPC 使用同一个 Task 节点对象，也可以维护相互独立的执行状态。

### 11.3 不要滥用 Tick

如果状态只会在某些事件发生时变化，应优先考虑事件驱动。

例如：

玩家进入视野后，通过 Perception 回调更新 Blackboard。

不需要让每个 Task 每一帧重新检查玩家是否存在。

当然，对于持续性的距离判断、转向控制等逻辑，周期性更新仍然可能是合理的。

### 11.4 正确处理中止逻辑

任务在执行时可能被 Decorator 中断。

例如 NPC 正在执行 MoveTo，但目标已经消失。

自定义异步 Task 应考虑：

- 是否要取消正在执行的操作。
- 是否需要清理 Timer。
- 是否需要取消动画回调。
- 是否需要清理异步请求。
- 是否可能出现任务结束后回调再次触发。

否则可能导致已经中断的行为仍继续执行。

---

## 12. 常见面试问题

### Q1：UE 行为树的作用是什么？

行为树是一种层次化 AI 决策系统，用于根据环境和自身状态选择 NPC 的行为，并管理行为执行、切换与中断。

它通过 Composite 控制流程、Task 执行具体行为、Decorator 判断条件、Service 更新辅助数据，并结合 Blackboard 实现决策数据管理。

### Q2：Selector 和 Sequence 有什么区别？

Selector 用于选择行为。

子节点失败时继续尝试下一个，成功时直接结束。

Sequence 用于组织一系列步骤。

子节点成功时继续执行下一个，失败时直接结束。

因此，Selector 类似带有优先级的选择逻辑，而 Sequence 类似顺序执行的工作流程。

### Q3：Decorator 和 Service 有什么区别？

Decorator 用于判断分支能否执行，以及条件变化时是否需要中断当前分支。

Service 主要负责在关联分支活跃期间，按照一定间隔更新 AI 决策所需的数据。

简单来说：

- Decorator：控制能不能做。
- Service：更新决策所需的信息。

### Q4：Blackboard 的作用是什么？

Blackboard 是行为树使用的数据存储系统。

它保存当前目标、目标位置、视野状态、高层意图等信息。

感知系统或其他 AI 逻辑可以更新 Blackboard，行为树通过读取这些数据作出决策。

这样能够实现数据获取与行为控制之间的解耦。

### Q5：为什么说 UE 行为树是事件驱动的？

因为 UE 的行为树不需要每帧都从根节点完整遍历所有分支。

它可以依赖 Blackboard 数据变化、Decorator 观察机制以及任务完成事件来触发执行流程变化。

这减少了不必要的条件判断和重复搜索。

但是，事件驱动不代表完全没有 Tick，Service 和 Task 仍然可以周期性更新。

### Q6：Observer Aborts 有什么作用？

Observer Aborts 用于控制 Decorator 条件变化时，是否中断正在运行的行为树分支。

主要有四种：

- None：不主动中断。
- Self：中断自身分支。
- Lower Priority：中断低优先级分支。
- Both：两者都支持。

例如，NPC 正在巡逻，突然发现敌人，攻击分支可以通过 Lower Priority 中断巡逻并切换到攻击。

### Q7：行为树和 FSM 有什么区别？

FSM 通过状态和状态转换描述 AI 的行为模式，适合管理 Idle、Attack、Escape 等高层意图。

行为树通过层次化节点描述行为的判断与执行流程，更适合组织复杂动作。

在项目中可以将两者结合：

FSM 负责高层决策，BT 负责具体行为执行。

### Q8：为什么 Task 需要返回 InProgress？

因为很多游戏行为不是瞬间完成的。

例如：

- 移动到目标位置。
- 播放攻击动画。
- 等待技能释放。
- 执行异步查询。

这些任务需要持续一段时间。

返回 InProgress 表示任务尚未完成，待操作真正结束后，再通过 FinishLatentTask 通知行为树。

### Q9：多个 NPC 共用同一棵行为树，会发生状态冲突吗？

通常不会，因为每个 AI 具有独立的行为树执行上下文和 Blackboard 数据。

但需要注意，原生 Task 节点对象默认可能被共享。

如果把某个 NPC 的运行时数据存放在共享节点的普通成员变量中，就可能产生状态冲突。

因此应使用 Blackboard、NodeMemory 或适当的节点实例化机制保存独立状态。

### Q10：为什么项目要使用 FSM + BT，而不是只使用 BT？

因为项目中的 NPC 既需要明确的高层行为意图，又需要复杂的状态内部执行逻辑。

FSM 适合管理 Idle、Attack、Escape 等高层状态及其切换条件。

BT 适合管理攻击、搜索、移动、躲避等具体行为流程。

混合架构能够将高层决策和底层行为组织分离，减少决策逻辑之间的耦合，也更方便调试和扩展。

---

## 13. 面试时的完整回答

如果面试官问：

“你了解 UE 的行为树系统吗？它的实现原理是什么？”

可以回答：

UE 的行为树是一种层次化的 AI 决策系统，通过不同类型的节点组织 NPC 的行为逻辑。

其中 Selector 和 Sequence 是最核心的组合节点。Selector 按照优先级选择合适的行为，Sequence 则负责顺序执行多个任务。

Task 节点负责执行具体行为，例如移动、攻击和等待；Decorator 负责条件判断和行为中断；Service 负责周期性更新 AI 决策所需的信息。

行为树通常通过 Blackboard 获取当前目标、位置、状态等信息，而这些信息可以由 AI Perception 或其他游戏系统更新。

UE 行为树的一个重要特点是事件驱动。通过 Blackboard 的变化通知和 Decorator 的 Observer Aborts 机制，行为树可以在条件变化时中断当前分支并切换行为，减少不必要的重复搜索。

在我的游戏项目中，我采用 FSM + BT 的混合架构，由 FSM 管理 NPC 的高层意图，例如普通行为、攻击和逃跑，再由行为树负责各个意图内部的具体行动。同时结合 AI Perception、Blackboard 和 EQS，实现 NPC 的感知、行为选择以及拟人化运动。

---

## 14. 核心知识点速查

| 知识点          | 核心作用                 |
| --------------- | ------------------------ |
| Behavior Tree   | 决策与行为组织           |
| AIController    | AI 控制与系统协调        |
| Blackboard      | 存储决策数据             |
| Selector        | 从左到右选择可执行行为   |
| Sequence        | 按顺序执行多个行为       |
| Task            | 执行具体操作             |
| Decorator       | 条件判断与行为中断       |
| Service         | 周期性更新数据           |
| Observer Aborts | 响应条件变化并中断分支   |
| InProgress      | 异步任务尚未完成         |
| NodeMemory      | 保存节点的独立运行时状态 |
| AI Perception   | 感知玩家和环境           |
| EQS             | 查询合适的位置或目标     |
| FSM             | 管理高层状态与状态切换   |

### 最终总结

UE 行为树最重要的五个知识点：

1. **Selector 负责选择，Sequence 负责顺序执行。**
2. **Task 执行行为，Decorator 控制条件，Service 更新信息。**
3. **Blackboard 负责数据共享，Behavior Tree 负责决策。**
4. **Observer Aborts 和事件驱动机制是理解 UE 行为切换的重要基础。**
5. **FSM + BT 可以把高层意图决策与具体行为执行分离。**

对于游戏客户端开发面试，除了能够解释这些概念，还应该重点掌握：

- Selector 与 Sequence 的实际执行顺序。
- Observer Aborts 如何中断正在运行的任务。
- 异步 Task 如何正确完成与中止。
- Blackboard 如何与 AI Perception 交互。
- 多个 NPC 共用行为树时如何避免状态冲突。
- FSM、BT、EQS 在实际项目中的职责划分。

---

## 15. 参考资料

1. Unreal Engine 官方文档：Behavior Tree Overviewhttps://dev.epicgames.com/documentation/en-us/unreal-engine/behavior-tree-in-unreal-engine---overview
2. Unreal Engine 官方文档：Behavior Tree Node Referencehttps://dev.epicgames.com/documentation/en-us/unreal-engine/behavior-tree-node-reference-in-unreal-engine
3. Unreal Engine 官方文档：Behavior Tree User Guide
   https://dev.epicgames.com/documentation/en-us/unreal-engine/behavior-tree-in-unreal-engine---user-guide
