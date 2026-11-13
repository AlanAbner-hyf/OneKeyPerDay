# Fast-Paced Multiplayer 学习总结

> 原文：Gabriel Gambetta — *Fast-Paced Multiplayer*  
> 面向：游戏开发 / 游戏客户端秋招面试  
> 核心主题：Authoritative Server、Client-Side Prediction、Server Reconciliation、Entity Interpolation、Lag Compensation

---

## 1. 整个系列到底在解决什么问题？

多人实时游戏的核心矛盾是：

> **服务器必须拥有最终决定权，但玩家又不能感受到网络往返延迟。**

如果完全相信客户端：

- 客户端可以修改位置、血量、速度等数据；
- 容易产生作弊；
- 不同客户端可能对世界状态产生不同理解。

因此，实时多人游戏通常采用 **Authoritative Server（权威服务器）**：

```text
Client
  │
  │ Input
  ▼
Server
  │
  │ Authoritative State
  ▼
Client
```

客户端只负责发送“我想做什么”，例如：

```text
MoveRight
Jump
Shoot
```

而不是直接告诉服务器：

```text
MyPosition = (100, 20)
MyHealth = 9999
```

服务器根据输入计算真正的游戏状态。

问题在于：

```text
玩家输入
   │
   ├── 网络延迟 ──► Server
   │                │
   │                │ 处理
   │                ▼
   ◄── 网络延迟 ── State
```

如果 RTT 是 200 ms，那么玩家按下按键后可能要等约 200 ms 才看到角色移动。

对于 FPS、动作游戏而言，这是不可接受的。

因此 Fast-Paced Multiplayer 的后续技术，本质上都在解决：

> **如何在服务器保持权威的前提下，让玩家感觉游戏像单机一样即时。**

---

# 2. Part I：Client-Server Game Architecture

## 2.1 Authoritative Server

核心原则：

> **Never trust the client.**

服务器维护真正的 World State。

客户端：

```text
发送 Input
↓
显示游戏
```

服务器：

```text
接收 Input
↓
验证 Input
↓
执行游戏逻辑
↓
更新 World State
↓
同步状态给客户端
```

例如玩家当前位置：

```text
x = 10
```

客户端发送：

```text
MoveRight
```

服务器计算：

```text
x = 11
```

而不是客户端直接发送：

```text
x = 11
```

这样可以防止典型的：

- Speed Hack
- Teleport Hack
- 修改血量
- 修改位置

---

## 2.2 纯服务器权威的问题

假设单向延迟为：

```text
100 ms
```

流程：

```text
Client Input
    │
    │ 100 ms
    ▼
Server
    │
    │ 处理
    ▼
State
    │
    │ 100 ms
    ▼
Client
```

玩家需要等待约：

```text
200 ms
```

才能看到操作结果。

于是出现第一个核心技术：

# Client-Side Prediction

---

# 3. Part II：Client-Side Prediction

Client-Side Prediction（客户端预测）的核心思想：

> **客户端不用等待服务器确认，先假设自己的输入会成功，并立即在本地执行。**

例如：

```text
当前位置：
x = 10

玩家按下：
MoveRight
```

客户端做两件事：

```text
1. Send MoveRight → Server

2. Local Simulation
   x = 11
```

所以玩家立即看到角色移动。

与此同时服务器稍后也会：

```text
收到 MoveRight
↓
执行
↓
x = 11
```

如果客户端和服务器使用相同逻辑：

```text
Client Prediction == Server Result
```

玩家就完全感觉不到网络延迟。

---

## 3.1 为什么客户端预测通常成立？

因为大多数玩家输入都是合法的。

例如：

```text
当前位置 x = 10

Input:
MoveRight
```

通常服务器最终也会得到：

```text
x = 11
```

因此客户端可以提前计算服务器大概率会得到的结果。

这要求客户端和服务器的游戏逻辑至少在预测部分足够一致。

---

# 4. Prediction 带来的新问题：状态不同步

假设：

```text
Client 当前：
x = 10
```

玩家快速输入：

```text
Input #1 → Right
Input #2 → Right
```

客户端立即预测：

```text
x = 12
```

但是服务器可能只处理完：

```text
Input #1
```

服务器此时发送：

```text
x = 11
```

如果客户端无条件采用服务器状态：

```text
Client x = 12

Server State = 11

Client Correction:
12 → 11
```

之后服务器处理 Input #2：

```text
11 → 12
```

玩家看到：

```text
12 → 11 → 12
```

角色出现明显抖动。

原因并不是客户端预测错误，而是：

> **客户端看到的是“现在”，服务器传回来的状态实际上属于“过去”。**

因此需要：

# Server Reconciliation

---

# 5. Server Reconciliation

Server Reconciliation（服务器校正）的关键机制：

> **服务器状态 + 尚未被服务器处理的本地输入 = 客户端当前预测状态**

---

## 5.1 Input Sequence Number

客户端给每一个 Input 一个序号：

```text
Input #1
Input #2
Input #3
...
```

例如：

```text
#1 MoveRight
#2 MoveRight
#3 Jump
```

客户端发送 Input 的同时，把它保存到：

```text
Pending Inputs
```

---

## 5.2 Server ACK

服务器处理输入以后告诉客户端：

```text
Authoritative State

+

Last Processed Input
```

例如：

```text
Position = 11
LastProcessedInput = #1
```

代表：

```text
服务器已经处理到了 #1
```

但：

```text
#2
```

可能仍然在网络中，或者尚未执行。

---

## 5.3 Reconciliation 过程

客户端收到：

```text
Server Position = 11
LastProcessedInput = #1
```

执行：

### Step 1：接受服务器权威状态

```text
Position = 11
```

### Step 2：删除服务器已经处理的输入

```text
Pending:

#1 MoveRight
#2 MoveRight
```

删除 #1：

```text
Pending:

#2 MoveRight
```

### Step 3：重新执行服务器还没有确认的 Input

```text
Position = 11

Replay #2 MoveRight

Position = 12
```

最终：

```text
Client Position = 12
```

玩家不会看到：

```text
12 → 11 → 12
```

---

# 6. Prediction + Reconciliation 的完整流程

客户端：

```text
玩家输入
   │
   ├──────────────► Server
   │
   ▼
Local Prediction
   │
   ▼
立即显示
```

服务器：

```text
Input
  │
  ▼
Validate
  │
  ▼
Simulation
  │
  ▼
Authoritative State
  │
  ▼
Client
```

客户端收到服务器状态后：

```text
Authoritative State
        +
Unacknowledged Inputs
        │
        ▼
Replay
        │
        ▼
Predicted Present State
```

一句话记忆：

> **Predict first, reconcile later.**

---

# 7. Part III：Entity Interpolation

Prediction + Reconciliation 主要解决：

```text
My Player
```

也就是：

> **自己控制的角色如何感觉即时。**

但是游戏中还有：

```text
Other Players
NPC
Vehicles
Projectiles
```

这些对象怎么办？

---

## 7.1 Server Tick / Time Step

服务器通常不会每收到一个 Input 就马上广播整个世界状态。

更常见的是按照固定频率更新世界，例如：

```text
10 Hz
```

即：

```text
每 100 ms 更新一次
```

流程：

```text
Input
Input
Input
Input
     │
     ▼
Server Tick
     │
     ▼
Process Inputs
     │
     ▼
Broadcast State
```

这样可以控制：

- CPU 消耗
- 网络带宽
- 状态广播频率

---

## 7.2 低频 Snapshot 的问题

假设服务器：

```text
10 Hz
```

客户端每：

```text
100 ms
```

收到一次其他玩家的位置。

如果直接显示：

```text
t=0ms     x=0
t=100ms   x=1
t=200ms   x=2
```

屏幕效果就是：

```text
0 → 1 → 2 → 3
```

每 100ms 跳一下。

即：

> **Remote Player Jitter / Choppy Movement**

因此需要：

# Entity Interpolation

---

# 8. Entity Interpolation

Interpolation 不预测未来。

它做的是：

> **缓存服务器已经确认的历史状态，然后在两个已知状态之间进行插值。**

服务器告诉客户端：

```text
t = 900ms
Position = A

t = 1000ms
Position = B
```

客户端并不急着立即显示：

```text
B
```

而是故意让远程角色落后一些，例如：

```text
100 ms
```

然后显示：

```text
A
↓
Interpolation
↓
B
```

于是：

```text
A → → → → B
```

移动变得平滑。

---

# 9. 为什么 Interpolation 要“看过去”？

Interpolation 需要：

```text
两个已知状态
```

才能插值。

如果尝试直接显示“现在”，客户端只有：

```text
Last Snapshot
```

却不知道：

```text
Next Snapshot
```

所以没有第二个确定状态可供插值。

解决方案是故意延迟渲染。

例如：

```text
Real Time:
1000 ms

Render Time:
900 ms
```

此时客户端已经拥有：

```text
State @ 900
State @ 1000
```

因此可以安全插值。

这也是整个系列非常重要的一句话：

> **You see yourself in the present, but you see other players in the past.**

即：

```text
Local Player
    ↓
Present

Remote Players
    ↓
Past
```

---

# 10. Dead Reckoning 与 Interpolation

文章还介绍了另一种方案：

## Dead Reckoning

根据：

```text
Position
Velocity
Acceleration
```

预测目标未来位置。

例如：

```text
P(t+Δt) ≈ P(t) + VΔt
```

适用于：

- 船
- 飞机
- 移动方向变化比较平缓的物体

但是 FPS 玩家：

```text
跑
停
突然转向
跳跃
蹲下
```

行为变化非常突然。

未来位置很难预测。

因此 FPS 等快速游戏通常更适合：

```text
Entity Interpolation
```

而不是简单 Dead Reckoning。

---

# 11. Part IV：Lag Compensation

到这里系统已经实现：

```text
Local Player
↓
Client Prediction
↓
Server Reconciliation

Remote Player
↓
Entity Interpolation
```

体验已经非常流畅。

但新的问题来了：

# Shooting

假设：

```text
Remote Player
```

因为 Interpolation 被显示在：

```text
100 ms 以前
```

的位置。

你瞄准屏幕里的敌人：

```text
HEAD
```

点击射击。

但是服务器当前的敌人可能已经移动到了别的位置。

如果服务器按照“当前世界”判定：

```text
MISS
```

玩家就会觉得：

> “我明明打到头了，为什么没有命中？”

---

# 12. Lag Compensation 的核心思想

解决办法是：

> **Server Rewind（服务器回溯）**

玩家射击时发送：

```text
Shoot
Timestamp
Aim
```

服务器保存过去一段时间的世界状态。

收到射击请求后，根据时间戳重建或回溯到：

```text
玩家开枪时看到的世界
```

然后在过去的状态中执行：

```text
Hit Test
```

例如：

```text
Current Server Time
        │
        │ rewind
        ▼
Shot Time
        │
        ▼
Reconstruct World
        │
        ▼
Raycast / Hit Detection
```

如果在那个时间点：

```text
Crosshair → Enemy Head
```

则判定：

```text
Hit
```

---

# 13. Lag Compensation 的代价

Server Rewind 对射击者比较公平。

但是对被射击者可能产生一种经典体验：

```text
玩家已经躲到墙后
↓
突然死亡
```

原因是：

服务器为了射击者进行了时间回溯。

在射击者视角的过去状态中：

```text
Target
```

可能仍然没有进入掩体。

所以这里存在一个权衡：

```text
Shooter Fairness
        VS
Target Fairness
```

网络游戏不存在让所有玩家都看到完全相同“现在”的方案。

很多网络同步技术，本质上是在不同的不一致之间做权衡。

---

# 14. 整套架构串起来

整个 Fast-Paced Multiplayer 可以归纳成：

```text
                ┌─────────────────┐
                │      Client     │
                └────────┬────────┘
                         │
                       Input
                         │
             ┌───────────┴───────────┐
             │                       │
             ▼                       ▼
      Local Prediction             Server
             │                       │
             │                       ▼
             │               Authoritative Sim
             │                       │
             │                       ▼
             │                World Snapshot
             │                       │
             ◄───────────────────────┘
             │
        Reconciliation
             │
             ▼
      Local Player State
```

对于 Remote Players：

```text
Server Snapshots
      │
      ▼
Snapshot Buffer
      │
      ▼
Interpolation
      │
      ▼
Smooth Remote Players
```

射击：

```text
Client Shoot
    │
    │ Timestamp + Aim
    ▼
Server
    │
    ▼
Rewind World
    │
    ▼
Hit Detection
    │
    ▼
Authoritative Result
```

---

# 15. 四个技术分别解决什么？

| 技术 | 解决的问题 | 主要作用对象 |
|---|---|---|
| Authoritative Server | 作弊、状态一致性 | 整个游戏世界 |
| Client-Side Prediction | 操作延迟 | Local Player |
| Server Reconciliation | 客户端预测与服务器状态不一致 | Local Player |
| Entity Interpolation | 远程玩家移动卡顿 | Remote Players |
| Lag Compensation | 高延迟下射击判定不符合玩家视觉结果 | Hit Detection |

可以直接记：

```text
Prediction
→ 解决“我自己延迟”

Reconciliation
→ 解决“我预测错了”

Interpolation
→ 解决“别人看起来一卡一卡”

Lag Compensation
→ 解决“我明明打中了为什么服务器说没打中”
```

---

# 16. 三条时间线

理解网络同步最重要的是意识到：

> 客户端和服务器并不处于完全相同的时间线上。

可以理解成：

```text
Local Player
     │
     ▼
Present / Predicted Future

Server
     │
     ▼
Authoritative Present

Remote Player Rendering
     │
     ▼
Known Past
```

因此：

```text
客户端自己的角色：
预测“现在”

远程角色：
渲染“过去”

服务器：
决定“真相”
```

这是 Fast-Paced Multiplayer 最核心的思想之一。

---

# 17. Snapshot / Input / State 的区别

面试中容易混淆。

## Input

表示：

```text
玩家想做什么
```

例如：

```text
MoveForward
Jump
Shoot
```

---

## State

表示：

```text
世界现在是什么样
```

例如：

```text
Position
Rotation
Velocity
Health
```

---

## Snapshot

表示：

> **某一个时间点的世界状态快照**

例如：

```text
Snapshot #100

Player A:
Position = ...

Player B:
Position = ...

NPC:
Position = ...
```

服务器会周期性把 Snapshot 发给客户端。

---

# 18. 面试：为什么不直接让客户端决定位置？

回答逻辑：

```text
客户端不可信
↓
如果直接上传 Position
↓
客户端可以修改位置
↓
Teleport / Speed Hack
```

所以：

> 客户端发送 Input，服务器根据游戏规则计算 State。

---

# 19. 面试：为什么有 Prediction 还需要 Reconciliation？

Prediction 只是：

```text
猜服务器之后会得到什么结果
```

但是：

- 网络延迟
- 其他玩家
- 碰撞
- 服务器验证
- 状态变化

都可能让预测结果与服务器不同。

因此服务器状态必须拥有最终决定权。

Reconciliation 做的就是：

```text
Server State
+
Unacknowledged Inputs
↓
重新构造客户端当前状态
```

---

# 20. 面试：Prediction 和 Interpolation 有什么区别？

这是非常高频的问题。

## Prediction

```text
使用已知信息预测未来
```

主要用于：

```text
Local Player
```

目的：

```text
减少输入延迟
```

---

## Interpolation

```text
在两个已经知道的历史状态之间插值
```

主要用于：

```text
Remote Player
```

目的：

```text
平滑运动
```

最大区别：

```text
Prediction → 猜未来

Interpolation → 重放过去
```

---

# 21. 面试：为什么远程玩家通常不直接 Prediction？

因为你不知道：

```text
其他玩家下一步会输入什么
```

FPS 玩家可能随时：

```text
左转
右转
停止
跳跃
开枪
```

远程玩家的输入高度不可预测。

因此直接 Prediction 很容易出现：

```text
Prediction Error
↓
Correction
↓
Jitter
```

很多场景下采用：

```text
Snapshot Buffer + Interpolation
```

更加稳定。

---

# 22. 面试：网络同步中的“不一致”是不是 Bug？

不一定。

实际上整个系统主动制造了某些“不一致”。

例如：

```text
Local Player:
Present

Remote Player:
100ms Ago

Server:
Authoritative Present
```

三者看到的世界并不完全相同。

这是为了在：

```text
Responsiveness
Smoothness
Authority
Fairness
Bandwidth
```

之间取得平衡。

真正成熟的实时网络架构并不是消灭所有延迟，而是：

> **隐藏延迟，并控制延迟造成的不一致。**

---

# 23. 一道典型面试题

### Q：一个 FPS 玩家 RTT 为 100ms，为什么按 W 后角色不能等待服务器返回以后再移动？

可以回答：

> 如果客户端等待服务器确认后再移动，玩家输入至少需要经历一次 RTT 才能在屏幕上产生反馈，操作会产生明显延迟。因此客户端通常会采用 Client-Side Prediction，在发送 Input 给服务器的同时立即在本地执行相同的移动逻辑。
>
> 服务器仍然保持权威。当服务器状态返回时，客户端通过 Input Sequence Number 判断服务器已经处理到哪个输入，然后以服务器状态为基准重新执行尚未确认的 Input，这就是 Server Reconciliation。
>
> 对于其他玩家，由于客户端不知道他们未来会进行什么操作，一般不会使用同样的预测方式，而是缓存服务器 Snapshot，通过 Entity Interpolation 渲染稍微滞后的历史状态，使远程角色运动保持平滑。
>
> 对射击这类对时间和位置非常敏感的操作，服务器还可以根据射击时间进行 Server Rewind / Lag Compensation，在玩家当时看到的世界状态中进行命中判定。

这基本就是整个 Fast-Paced Multiplayer 的主线。

---

# 24. 最终知识框架

```text
Fast-Paced Multiplayer
│
├── Authoritative Server
│      │
│      └── Server owns truth
│
├── Local Player
│      │
│      ├── Client-Side Prediction
│      │       └── Immediate response
│      │
│      └── Server Reconciliation
│              ├── Input Sequence
│              ├── Server ACK
│              └── Replay Pending Inputs
│
├── Remote Players
│      │
│      ├── Server Snapshot
│      ├── Snapshot Buffer
│      └── Entity Interpolation
│
└── Combat
       │
       └── Lag Compensation
               ├── Timestamp
               ├── Server History
               ├── Server Rewind
               └── Hit Detection
```

---

# 25. 一句话总结

整套 Fast-Paced Multiplayer 的本质是：

> **服务器负责真相，客户端负责体验。**

服务器：

```text
Authoritative
Correct
Secure
```

客户端：

```text
Predict
Smooth
Hide Latency
```

最终目标不是让网络延迟消失，因为这是不可能的，而是：

> **让玩家尽可能感觉不到网络延迟的存在。**

---

# 26. 建议记住的关键词

```text
Authoritative Server
RTT
Input
World State
Server Tick
Snapshot
Client-Side Prediction
Input Sequence Number
Pending Input
Server Acknowledgement
Server Reconciliation
Replay
Dead Reckoning
Entity Interpolation
Snapshot Buffer
Lag Compensation
Server Rewind
Hit Detection
```

如果这些概念能够按因果关系完整讲出来，那么 Gabriel Gambetta 的 *Fast-Paced Multiplayer* 这一部分已经基本掌握。

---

## 原始资料

- Part I — Client-Server Game Architecture  
  https://www.gabrielgambetta.com/client-server-game-architecture.html

- Part II — Client-Side Prediction and Server Reconciliation  
  https://www.gabrielgambetta.com/client-side-prediction-server-reconciliation.html

- Part III — Entity Interpolation  
  https://www.gabrielgambetta.com/entity-interpolation.html

- Part IV — Lag Compensation  
  https://www.gabrielgambetta.com/lag-compensation.html

- Sample Code and Live Demo  
  https://www.gabrielgambetta.com/client-side-prediction-live-demo.html
