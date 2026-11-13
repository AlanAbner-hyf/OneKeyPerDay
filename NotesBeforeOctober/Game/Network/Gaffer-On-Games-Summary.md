# Gaffer On Games 学习总结

> 面向：游戏开发 / 游戏客户端秋招面试内容范围：
>
> 1. UDP vs TCP
> 2. Deterministic Lockstep
> 3. Snapshot Interpolation
> 4. State Synchronization
> 5. Reliable Ordered Messages

---

# 1. 整体知识框架

Gaffer On Games 这几篇文章，本质上是在回答一个问题：

> **实时多人游戏中，如何在低延迟、低带宽、可靠性、平滑性和状态一致性之间做权衡？**

可以先建立这样一个整体框架：

```text
游戏网络问题
│
├── 传输层选择
│   └── UDP vs TCP
│
├── 同步方式
│   ├── Deterministic Lockstep
│   ├── Snapshot Interpolation
│   └── State Synchronization
│
└── UDP 上的可靠机制
    └── Reliable Ordered Messages
```

这五部分并不是五个完全独立的知识点，而是互相关联的。

例如：

```text
选择 UDP
↓
UDP 不可靠
↓
某些消息又必须可靠
↓
Reliable Ordered Messages
```

再比如：

```text
不想同步完整状态
↓
只同步输入
↓
Deterministic Lockstep
```

或者：

```text
不要求客户端自己算出完全相同结果
↓
直接同步服务器状态
↓
Snapshot Interpolation / State Synchronization
```

---

# 2. UDP vs TCP

## 2.1 TCP 的核心特性

TCP 提供：

```text
可靠
有序
面向连接
字节流
```

所谓可靠，指：

```text
数据丢失
↓
TCP 自动重传
```

所谓有序，指：

```text
发送：
A B C

接收：
必须按照
A B C
```

如果：

```text
B 丢了
```

即使：

```text
C 已经到了
```

应用层也必须等：

```text
B 重传成功
```

之后才能继续交付。

这会造成：

> **Head-of-Line Blocking（队头阻塞）**

---

## 2.2 为什么实时游戏不喜欢完全依赖 TCP？

假设服务器正在同步玩家位置：

```text
P100 = x10
P101 = x11
P102 = x12
```

其中：

```text
P101 丢失
```

但是：

```text
P102 已经到达
```

对于游戏来说，其实：

```text
x12
```

已经比：

```text
x11
```

更新。

旧状态：

```text
P101
```

已经没有价值。

但 TCP 仍然必须：

```text
等待 P101
↓
再交付 P102
```

这会增加延迟。

实时游戏很多数据满足：

> **新数据可以覆盖旧数据。**

例如：

```text
位置
旋转
速度
瞄准方向
Snapshot
```

这些数据通常不需要保证每一条都送达。

---

## 2.3 UDP 的特性

UDP：

```text
无连接
无可靠保证
无顺序保证
不自动重传
```

但是它的优势是：

```text
延迟低
控制权高
不会自动产生队头阻塞
```

因此游戏可以自己决定：

```text
什么数据允许丢
什么数据必须可靠
```

---

## 2.4 游戏里的典型设计

例如：

```text
玩家位置
→ UDP Unreliable
```

丢了一帧：

```text
x10
x11 ×
x12
```

直接用：

```text
x12
```

即可。

但是：

```text
购买武器
拾取道具
进入房间
关键 RPC
```

不能丢。

所以可以：

```text
UDP
│
├── Unreliable
│   ├── Position
│   ├── Rotation
│   └── Snapshot
│
└── Reliable
    ├── Inventory
    ├── Spawn
    └── RPC
```

---

## 2.5 面试记忆

一句话：

> **TCP 帮你统一保证可靠有序，UDP 把选择权交给游戏。**

游戏使用 UDP，并不是因为：

```text
UDP 永远比 TCP 快
```

而是因为：

> **游戏希望针对不同数据采用不同的可靠性策略。**

---

# 3. Deterministic Lockstep

Deterministic Lockstep 可以理解为：

> **不传完整游戏状态，只同步每个玩家的输入，让所有机器自己执行相同的游戏逻辑。**

---

## 3.1 基本流程

假设两个玩家：

```text
Player A
Player B
```

第 100 帧：

```text
A：MoveRight
B：Attack
```

服务器只广播：

```text
Frame 100 Inputs:
A → MoveRight
B → Attack
```

所有客户端：

```text
收到输入
↓
执行相同 Simulation
↓
得到相同结果
```

理论上：

```text
Client A State
=
Client B State
=
Server State
```

---

## 3.2 为什么叫 Deterministic？

因为：

```text
同一个初始状态
+
同一组输入
```

必须得到：

```text
完全相同结果
```

即：

```text
S(t+1) = F(S(t), Input)
```

只要：

```text
S(t)
Input
F
```

相同，那么结果必须一致。

---

## 3.3 一个简单例子

初始：

```text
x = 10
```

输入：

```text
MoveRight
```

所有机器都计算：

```text
x = x + 1
```

于是：

```text
Server  → 11
ClientA → 11
ClientB → 11
```

这就是理想状态。

---

## 3.4 最大难点：确定性

现实中很容易出现：

```text
Server：
10.00001

Client：
10.00002
```

一次误差很小。

但是经过大量物理计算：

```text
碰撞
速度
旋转
约束
```

可能逐渐变成：

```text
Server：
角色在左边

Client：
角色在右边
```

这就是：

> **Simulation Divergence**

产生差异的原因包括：

```text
不同 CPU
不同平台
浮点计算差异
不同编译器
随机数
物理引擎
执行顺序
```

所以 Lockstep 对：

```text
Determinism
```

要求非常高。

---

## 3.5 为什么 RTS 很适合？

例如 RTS：

```text
1000 个单位
```

如果同步完整状态：

```text
每个单位：
Position
Velocity
Health
Animation
...
```

带宽很大。

但是每个玩家每帧真正产生的输入可能只是：

```text
Move Unit Group A to X
Attack Unit B
Build Barracks
```

数据非常小。

所以：

```text
同步 Input
```

比：

```text
同步 State
```

省很多带宽。

---

## 3.6 Lockstep 的延迟问题

Lockstep 通常需要：

```text
所有玩家这一帧输入都准备好
↓
才能执行这一帧
```

例如：

```text
Frame 100
```

等待：

```text
A Input ✅
B Input ✅
C Input ❌
```

那么：

```text
Frame 100
```

可能无法继续。

因此它容易受到：

```text
高延迟玩家
丢包
```

影响。

---

## 3.7 适合什么游戏？

比较适合：

```text
RTS
策略游戏
单位数量极多
操作频率较低
```

不太适合直接用于：

```text
高速 FPS
复杂非确定性物理游戏
```

---

## 3.8 面试记忆

一句话：

> **Lockstep 同步的是输入，不是结果。**

核心优点：

```text
带宽低
```

核心代价：

```text
高确定性要求
+
容易受慢玩家影响
```

---

# 4. Snapshot Interpolation

Snapshot Interpolation 是另一种完全不同的思路：

> **服务器直接发送游戏世界状态，客户端不尝试复现服务器的完整 Simulation，而是在多个服务器 Snapshot 之间进行插值。**

---

## 4.1 Snapshot 是什么？

Snapshot：

> 某一个时刻的游戏世界状态。

例如：

```text
Snapshot #100

Player A:
Position = (10, 0, 2)
Rotation = 30°

Player B:
Position = (5, 0, 8)
Rotation = 90°
```

过一会：

```text
Snapshot #101

Player A:
Position = (11, 0, 2)

Player B:
Position = (5.5, 0, 8)
```

---

## 4.2 如果直接显示 Snapshot 会怎样？

假设服务器：

```text
10Hz
```

即每：

```text
100ms
```

发一次位置。

客户端：

```text
t=0
x=0

t=100ms
x=1

t=200ms
x=2
```

直接显示：

```text
0 → 1 → 2
```

角色会一卡一卡地跳。

---

## 4.3 Interpolation

客户端不立即显示最新 Snapshot。

而是：

```text
保存 Snapshot A
保存 Snapshot B
```

然后：

```text
A
↓
Interpolation
↓
B
```

例如：

```text
A: x=10
B: x=11
```

显示：

```text
10
10.1
10.2
10.3
...
11
```

运动就变得平滑。

---

## 4.4 为什么必须延迟显示？

因为 Interpolation 需要：

```text
两个已知点
```

如果客户端要渲染“当前时刻”：

```text
只知道最新 Snapshot
```

不知道下一个 Snapshot 会是什么。

因此必须故意：

```text
Render Past
```

例如当前真实时间：

```text
1000ms
```

客户端渲染：

```text
900ms
```

这样已经拥有：

```text
Snapshot @900
Snapshot @1000
```

就可以安全插值。

所以：

> **Interpolation 用少量额外延迟，换取平滑表现。**

---

## 4.5 网络抖动怎么办？

实际网络可能：

```text
Snapshot 100
Snapshot 101
Snapshot 102
```

发送很稳定。

但到达客户端：

```text
100
101

间隔突然变长

102
103 一起到
```

所以客户端通常维护：

```text
Snapshot Buffer
```

先缓存一些状态，再稳定播放。

---

## 4.6 优点

最大的优点：

```text
客户端不需要完全复制服务器 Simulation
```

服务器是什么状态：

```text
客户端就显示什么
```

因此：

```text
不要求确定性
```

非常适合：

```text
复杂物理
大量不可预测运动
```

---

## 4.7 缺点

最大的缺点：

```text
带宽高
```

因为服务器需要不断同步：

```text
Position
Rotation
Velocity
State
...
```

世界越大：

```text
对象越多
```

Snapshot 越大。

因此通常需要：

```text
Delta Compression
Quantization
Interest Management
```

等优化。

---

## 4.8 一个 FPS 例子

远程玩家：

```text
Server Snapshot:
t100 x=10

Server Snapshot:
t110 x=11
```

客户端不预测：

```text
下一步他会不会突然转身
```

而是：

```text
显示 100ms 前的状态
↓
在 x=10 到 x=11 之间插值
```

这样更加稳定。

---

## 4.9 面试记忆

一句话：

> **Snapshot Interpolation 不猜未来，而是平滑播放已知的过去。**

---

# 5. State Synchronization

这一节最容易和广义的“状态同步”混淆。

Gaffer 这里的 State Synchronization 特指一种：

> **客户端和服务器都运行 Simulation，但服务器持续发送部分对象状态进行纠偏。**

---

## 5.1 和 Snapshot Interpolation 最大区别

Snapshot Interpolation：

```text
Server
↓
持续发状态
↓
Client 根据状态插值显示
```

State Synchronization：

```text
Server Simulation
      ↓
  部分 State
      ↓
Client Simulation
      ↓
继续自己模拟
```

也就是说：

> **客户端不仅显示状态，还在自己做 Simulation。**

---

## 5.2 一个球的例子

Server：

```text
Position = 10
Velocity = 2
```

Client：

```text
Position = 10
Velocity = 2
```

接下来双方都自己模拟：

```text
Server:
10 → 10.2 → 10.4

Client:
10 → 10.2 → 10.4
```

如果客户端稍微跑偏：

```text
Client:
10.45
```

服务器发来：

```text
Position = 10.4
Velocity = 2
```

客户端：

```text
修正
↓
继续模拟
```

---

## 5.3 为什么必须同步 Velocity？

因为客户端需要继续向前模拟。

如果服务器只告诉：

```text
Position = 10
```

客户端不知道下一帧：

```text
往哪里走
多快
```

所以通常需要：

```text
Position
Orientation
Linear Velocity
Angular Velocity
```

---

## 5.4 为什么不需要每帧同步所有对象？

因为客户端自己在模拟。

例如 1000 个物体：

```text
服务器不必每帧发 1000 个
```

可以只发：

```text
最重要的 50 个
```

其他对象：

```text
客户端继续本地模拟
```

下一次再纠正。

---

## 5.5 Priority Accumulator

如果永远只同步高优先级对象：

```text
玩家 = 100
移动箱子 = 20
静止箱子 = 1
```

那么静止箱子可能永远得不到同步。

所以使用：

```text
Priority Accumulator
```

每帧累加：

```text
accumulator += priority
```

发送成功：

```text
accumulator = 0
```

没被选中：

```text
继续累积
```

因此低优先级对象虽然更新得慢，但最终也能获得同步机会。

---

## 5.6 丢包时怎么办？

State Synchronization 不要求：

```text
收到每一个包
```

如果一个状态包丢失：

```text
客户端继续 Simulation
```

直到下一次服务器状态到达：

```text
再纠正
```

所以它允许：

```text
Approximate Synchronization
```

而不是追求：

```text
每帧完全一样
```

---

## 5.7 Simulation 和 Rendering 分离

假设：

```text
Client Physics:
x = 10.5
```

服务器说：

```text
正确是 x = 10
```

物理层：

```text
10.5 → 10
```

应该直接修正。

但是如果画面也直接：

```text
10.5 → 10
```

玩家会看到跳变。

于是 Rendering 单独平滑：

```text
10.5
10.4
10.3
10.2
10.1
10
```

所以核心原则：

> **Simulation 立即正确，Rendering 慢慢变正确。**

---

## 5.8 Quantize Both Sides

网络为了省带宽：

```text
10.123456
```

可能只发送：

```text
10.12
```

如果服务器继续使用：

```text
10.123456
```

客户端使用：

```text
10.12
```

两边 Simulation 会逐渐产生差异。

所以：

```text
服务器自己也使用量化后的值
```

即：

```text
Server:
10.123456
↓
10.12

Client:
10.12
```

两边从相同状态继续模拟。

---

## 5.9 面试记忆

一句话：

> **State Synchronization = 两边都自己算，服务器不断纠偏。**

---

# 6. Reliable Ordered Messages

UDP 本身：

```text
可能丢
可能乱序
不做去重
不自动重传
```

但某些游戏消息必须：

```text
可靠
+
有序
```

例如：

```text
Spawn Player
Equip Weapon
Start Match
```

因此需要：

> **在 UDP 上实现 Message 级的 Reliable Ordered。**

---

## 6.1 Packet 和 Message 要区分

一个 UDP Packet：

```text
Packet #100
│
├── Message #10
├── Message #11
└── Message #12
```

两套编号：

```text
Packet Sequence
Message ID
```

Packet 是：

```text
运输容器
```

Message 是：

```text
业务数据
```

可靠的是：

```text
Message
```

而不是要求：

```text
每个 Packet 都必须重传成功
```

---

## 6.2 Packet ACK

每个 UDP Packet Header 可以包含：

```text
sequence
ack
ack_bits
```

### sequence

表示：

```text
当前 Packet 编号
```

例如：

```text
100
101
102
```

### ack

表示：

```text
最近收到的对方 Packet
```

例如：

```text
ack = 100
```

### ack_bits

表示：

```text
100 之前若干个 Packet 的接收情况
```

例如：

```text
100 ✅
99  ✅
98  ❌
97  ✅
```

可以用 bitfield 表示。

---

## 6.3 为什么 ACK 自己丢了也问题不大？

因为每个后续 Packet：

```text
都携带 ack + ack_bits
```

于是确认信息会重复出现。

例如：

```text
Packet A:
ack=100

Packet B:
ack=101
ack_bits 里仍然表示 100 已收到
```

所以：

```text
单个 ACK 包丢失
```

通常不会破坏整体确认机制。

---

## 6.4 Packet ACK 如何变成 Message ACK？

发送：

```text
Packet #100
│
├── M10
├── M11
└── M12
```

发送端记录：

```text
Packet 100
→ 包含 M10 M11 M12
```

后来：

```text
Packet #100 被 ACK
```

于是知道：

```text
M10 ✅
M11 ✅
M12 ✅
```

然后从：

```text
Reliable Send Queue
```

删除。

---

## 6.5 Reliable 如何实现？

假设：

```text
M10
```

第一次放在：

```text
Packet #100
```

但是：

```text
#100 丢失
```

M10 没有被 ACK。

所以：

```text
M10
```

仍然留在发送队列。

之后：

```text
Packet #105
├── M10
└── M11
```

再次发送。

只要未来某个包含：

```text
M10
```

的 Packet 被 ACK：

```text
M10
```

就算成功可靠送达。

因此：

> **Reliable = 未确认 Message 持续重发。**

---

## 6.6 Ordered 如何实现？

发送：

```text
M10
M11
M12
```

UDP 实际到达：

```text
M12
M10
M11
```

Receiver 不能直接交给业务层。

维护：

```text
next_message_id = 10
```

收到：

```text
M12
```

先缓存。

收到：

```text
M10
```

交付：

```text
M10
```

然后等：

```text
M11
```

M11 到达后：

```text
M11
M12
```

依次交付。

于是业务层看到：

```text
10
11
12
```

---

## 6.7 为什么可靠有序不能滥用？

假设：

```text
M10 丢失
M11 到了
M12 到了
```

因为要求 Ordered：

```text
M11
M12
```

必须等：

```text
M10
```

这就是：

> **Head-of-Line Blocking**

因此：

```text
位置
旋转
Snapshot
```

通常不能走 Reliable Ordered。

因为：

```text
新状态比旧状态更重要
```

例如：

```text
Position #100
Position #101 丢
Position #102 到达
```

直接用：

```text
#102
```

即可。

---

# 7. 五个知识点之间的关系

可以这样串起来：

```text
UDP vs TCP
│
│ 为什么游戏偏爱 UDP？
│ 因为希望自己控制可靠性
│
▼
Reliable Ordered Messages
│
│ UDP 上如何实现关键消息可靠？
│
▼
同步策略
│
├── Deterministic Lockstep
│   └── 同步 Input
│
├── Snapshot Interpolation
│   └── 同步完整/部分 Snapshot
│
└── State Synchronization
    └── 两边模拟 + 状态纠偏
```

---

# 8. 三种同步方案核心对比

| 对比项             | Deterministic Lockstep | Snapshot Interpolation | State Synchronization |
| ------------------ | ---------------------- | ---------------------- | --------------------- |
| 主要同步内容       | Input                  | State Snapshot         | Input + 部分 State    |
| 客户端是否自己模拟 | 是                     | 不依赖相同完整模拟     | 是                    |
| 是否要求确定性     | 很高                   | 很低                   | 较低                  |
| 网络带宽           | 低                     | 高                     | 中等                  |
| 丢包容忍度         | 较低                   | 较高                   | 较高                  |
| 状态误差           | 理论上不能有           | 服务器直接给结果       | 允许偏差后纠正        |
| 典型思想           | 大家算一样             | 服务器告诉你结果       | 自己算，服务器纠偏    |

一句话：

```text
Lockstep：
只发输入，大家必须算一样。

Snapshot：
直接发结果，客户端平滑播放。

State Synchronization：
两边都算，但服务器持续纠偏。
```

---

# 9. 和 Gabriel Gambetta Fast-Paced Multiplayer 的联系

Gabriel Gambetta 讲的是：

```text
Server Authoritative
+
Client Prediction
+
Server Reconciliation
+
Entity Interpolation
+
Lag Compensation
```

Gaffer 这几篇是在更底层、更系统地解释：

```text
为什么使用 UDP
如何设计同步方式
如何处理 Snapshot
如何让网络物理保持接近
如何在 UDP 上做可靠消息
```

两者可以组合理解。

例如一个 FPS：

```text
网络传输：
UDP

本地玩家：
Client-Side Prediction
+
Server Reconciliation

远程玩家：
Snapshot Interpolation

关键 RPC：
Reliable Ordered Messages

物理对象：
可考虑 State Synchronization

命中：
Lag Compensation
```

---

# 10. 一个完整 FPS 网络例子

假设一个 FPS 游戏：

## 玩家移动

客户端按：

```text
W
```

使用：

```text
Client Prediction
```

立即移动。

同时 Input：

```text
通过 UDP 发给服务器
```

服务器：

```text
Authoritative Simulation
```

客户端收到校正：

```text
Server Reconciliation
```

---

## 远程玩家

服务器周期性发送：

```text
Snapshot
```

客户端：

```text
Snapshot Buffer
↓
Interpolation
```

显示平滑运动。

---

## 场景物理对象

例如：

```text
爆炸后的箱子
球
可推动物体
```

服务器和客户端都做 Simulation。

服务器偶尔：

```text
发送 Position + Velocity
```

纠偏。

这更接近：

```text
State Synchronization
```

---

## 玩家购买武器

消息：

```text
Buy AK47
```

不能丢。

因此：

```text
Reliable Message
```

如果业务顺序要求：

```text
Buy
↓
Equip
↓
Fire
```

则：

```text
Reliable Ordered
```

---

## 玩家位置 Snapshot

不需要可靠：

```text
#100
#101 ×
#102
```

直接：

```text
使用 #102
```

即可。

---

# 11. 面试高频问题

## Q1：为什么游戏常用 UDP，而不是 TCP？

答题逻辑：

```text
实时游戏对延迟非常敏感
↓
很多状态允许丢包
↓
旧状态通常不值得重传
↓
TCP 的可靠有序可能产生队头阻塞
↓
UDP 允许游戏针对不同数据自定义可靠策略
```

---

## Q2：帧同步和状态同步最大的区别？

如果按更宽泛的游戏开发语境：

```text
帧同步：
同步 Input
所有端执行相同逻辑

状态同步：
Server 算结果
同步 State
```

如果专门问 Gaffer：

```text
State Synchronization
=
两边都模拟
+
服务器状态纠偏
```

---

## Q3：Snapshot Interpolation 为什么要增加延迟？

因为：

```text
Interpolation
```

需要两个已知 Snapshot。

因此必须：

```text
显示过去
```

才能确保：

```text
前后两个状态都已知
```

用少量延迟换：

```text
平滑
+
抗网络抖动
```

---

## Q4：UDP 上如何实现 Reliable？

```text
Packet Sequence
+
ack
+
ack_bits
↓
确认 Packet
↓
Packet → Message 映射
↓
确认 Message
↓
未 ACK Message 重发
```

---

## Q5：如何实现 Ordered？

```text
Message ID
+
Receive Buffer
```

乱序到达：

```text
12 10 11
```

业务层仍然收到：

```text
10 11 12
```

---

## Q6：为什么 Position 不适合 Reliable Ordered？

因为位置属于：

```text
Temporal State
```

新的位置可以覆盖旧的位置。

如果旧 Position 丢失：

```text
没必要重传
```

否则反而：

```text
增加延迟
+
造成队头阻塞
```

---

# 12. 最终知识地图

```text
Gaffer On Games
│
├── UDP vs TCP
│   │
│   ├── TCP：Reliable + Ordered
│   ├── UDP：Best Effort
│   └── 游戏希望自行决定可靠性
│
├── Deterministic Lockstep
│   │
│   ├── Sync Input
│   ├── Deterministic Simulation
│   ├── Low Bandwidth
│   └── High Determinism Requirement
│
├── Snapshot Interpolation
│   │
│   ├── Server Snapshot
│   ├── Snapshot Buffer
│   ├── Render Past
│   └── Interpolate
│
├── State Synchronization
│   │
│   ├── Both Sides Simulate
│   ├── Partial State Update
│   ├── Priority Accumulator
│   ├── Hard Physics Correction
│   ├── Visual Smoothing
│   └── Quantize Both Sides
│
└── Reliable Ordered Messages
    │
    ├── Packet Sequence
    ├── ack
    ├── ack_bits
    ├── Packet ACK
    ├── Message ACK
    ├── Reliable Resend
    └── Ordered Delivery
```

---

# 13. 一句话总结五篇文章

### UDP vs TCP

> 游戏选择 UDP，是为了自己控制哪些数据需要可靠，而不是让所有数据统一承担可靠有序的代价。

### Deterministic Lockstep

> 只同步输入，让所有机器基于同样输入执行完全一致的 Simulation。

### Snapshot Interpolation

> 服务器发送世界状态，客户端缓存过去的 Snapshot，并通过插值平滑显示。

### State Synchronization

> 客户端和服务器都持续 Simulation，服务器周期性发送关键状态进行纠偏。

### Reliable Ordered Messages

> 在 UDP 上通过 Sequence、ACK、重传和 Message ID，为真正重要的消息实现可靠有序传输。

---

# 14. 最值得记住的核心思想

如果只记一件事：

> **游戏网络并不存在一种“最好的同步方案”，核心永远是根据数据类型，在延迟、带宽、可靠性、一致性和平滑性之间做权衡。**

不同数据应该采用不同策略：

```text
位置 / 旋转
→ Unreliable

Snapshot
→ Unreliable + Sequenced

关键 RPC
→ Reliable Ordered

大规模确定性策略游戏
→ Lockstep

远程角色
→ Snapshot Interpolation

复杂网络物理
→ State Synchronization
```

这也是 Gaffer On Games 这几篇文章最重要的工程思想。
