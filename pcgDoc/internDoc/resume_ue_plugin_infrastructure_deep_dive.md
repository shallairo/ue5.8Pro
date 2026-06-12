# GenesisGroup UE 插件基础设施深度分析（自包含版 · 简历 / 面试准备）

> **使用场景**：离开公司后无源码访问，仅凭本文档即可复述实现细节、撰写/完善简历、应对面试追问。
> 因此本文**内嵌真实代码片段、数据格式样例、调用时序与底层原理推导**，力求"看文档=看过源码"。
> **范围**：插件基础设施/框架层，不涉及具体 PCG 功能（城市/荒野/地形生成等）。
> **插件路径**：`CitySample/Plugins/GenesisGroup/`

---

## 目录
- [0. 插件全景](#0-插件全景)
- [模块一：MCP 通信层（EverythingCopilot）](#模块一mcp-通信层everythingcopilot)
- [模块二：JSON Field 类型系统（NextPCG）](#模块二json-field-类型系统nextpcg)
- [模块三：异步 Tick 状态机](#模块三异步-tick-状态机)
- [模块四：几何数学库（GeometryCopilot）](#模块四几何数学库geometrycopilot)
- [模块五：操作栈框架（EditorUtilityCopilot）](#模块五操作栈框架editorutilitycopilot)
- [模块六：点云与规则处理器（RuleProcessor）](#模块六点云与规则处理器ruleprocessor)
- [附录 A：简历条目模板](#附录-a简历条目模板可直接改写)
- [附录 B：高频面试问答（带详解）](#附录-b高频面试问答带详解)
- [附录 C：术语速查表](#附录-c术语速查表)

---

## 0. 插件全景

**GenesisGroup** 是 NextPCG 平台的 **Unreal Engine 客户端插件群**，包含 19 个子插件模块，为 AI Agent（通过 MCP 协议）和编辑器提供程序化内容生成能力。

### 模块分类

```
┌─────────────────────────────────────────────────────────────────┐
│                    基础设施层（本篇重点）                          │
├─────────────────────────────────────────────────────────────────┤
│  EverythingCopilot    MCP 通信中枢：SSE + JSON-RPC + 工具派发    │
│  NextPCG              核心框架：JSON Field + 数据类型 + I/O 管线  │
│  GeometryCopilot      几何数学：Bezier / 图论 / 细分              │
│  EditorUtilityCopilot 编辑器框架：操作栈 / 上下文传递             │
│  RuleProcessor        点云 + Slice-and-Dice 规则系统             │
├─────────────────────────────────────────────────────────────────┤
│                    功能层（不在本篇范围）                          │
├─────────────────────────────────────────────────────────────────┤
│  NextPCGTools         编辑器工具、模式、内容浏览器扩展             │
│  NextPCGLandscape     地形 PCG                                  │
│  NextPCGBiome         生物群系生成                               │
│  NextPCGWater         水体系统                                   │
│  SplineCopilot        样条/路径编辑                              │
│  AnimationCopilot     动画/音频驱动                              │
│  VexCopilot           VEX 脚本节点图                             │
│  WorldBuildCopilot    世界构建 / HLOD                            │
│  PerformanceCopilot   性能分析 / Imposter                        │
│  ...                  其余域模块                                 │
└─────────────────────────────────────────────────────────────────┘
```

**一句话定位（面试开场）**：
> "我负责 GenesisGroup 插件群的基础设施层：用 UE 的 `FHttpModule` 实现了真 SSE 流式接收（UE 无原生 SSE 支持），构建了 MCP 协议客户端与 100+ 工具的注册/派发系统；设计了 JSON Field 多态类型系统和 14 状态异步 Tick 管线，实现 UE 与 Houdini Server 之间的非阻塞数据交换。"

---

# 模块一：MCP 通信层（EverythingCopilot）

## 1.1 总体结构

```
EverythingCopilot/Source/EverythingCopilot/
├── Public/Mcp/
│   └── EverythingCopilotMcpSubsystem.h       # 核心子系统声明
├── Private/Mcp/
│   ├── EverythingCopilotMcpSubsystem.cpp     # SSE 连接/解析/派发 (~2034 行)
│   ├── EverythingCopilotMcpWebSocket.cpp     # Mock 结果存储槽（非真实 Socket）
│   ├── EverythingCopilotMcp_ProcessRequest.cpp   # 派发入口
│   ├── EverythingCopilotMcp_ControlHandlers.cpp  # control_actor/editor
│   ├── EverythingCopilotMcp_NextPCGHandlers.cpp  # NextPCG 工具 (~7976 行)
│   ├── EverythingCopilotMcp_LandscapeHandlers.cpp
│   ├── EverythingCopilotMcp_AssetHandlers.cpp
│   └── ... (共 ~34 个 handler 文件)
└── Private/Slates/
    ├── SEverythingCopilotChatRoom.cpp        # AI 聊天界面
    ├── SEverythingCopilotAssetSearcher.cpp   # 资产搜索 UI
    └── SEverythingCopilotPromptPopup.cpp     # 快速提示输入
```

**核心类**：

| 类 | 基类 | 职责 |
|---|---|---|
| `UEverythingCopilotMcpSubsystem` | `UEditorSubsystem` | MCP 客户端核心：SSE 连接、JSON-RPC POST、工具注册表、派发、响应 |
| `FEverythingCopilotMcpWebSocket` | 普通 C++ 类 | **注意：名字误导**——不是真实网络 Socket；仅作工具执行结果的**存储槽**（mock 模式） |
| `FMcpToolDefinitionBuilder` | 本地帮助类 | Fluent API 构建 JSON 工具 schema |

**关键数据成员**：
```cpp
TSharedPtr<IHttpRequest> SseRequest;           // 长连接 SSE GET 请求（只有一个）
FString McpServerBaseUrl;                      // e.g. "http://127.0.0.1:5000"
FString McpMessagesUrl;                        // 从 endpoint 事件里解析出 POST 地址
FString McpSessionId;                          // SSE session_id
int32 SseProcessedBytes;                       // 已处理的 SSE 累积字节位置
bool bSseConnected;
TMap<FString, FAutomationHandler> AutomationHandlers;   // 工具名 → handler 函数
int32 ReconnectAttemptCount;                   // 当前重连次数（最多 10）
```

## 1.2 SSE 连接：在不支持 SSE 的环境里实现真流式接收

### 核心难点

UE 的 `FHttpModule` 是通用 HTTP 客户端，**原本为短请求设计**，没有原生 SSE 支持。要实现 SSE（服务端持续推流），需要在数据流入时**增量读取**，不能等整个响应结束。

### 实现方案（真实代码）

```cpp
// EverythingCopilotMcpSubsystem.cpp line 345
void UEverythingCopilotMcpSubsystem::StartSseConnection()
{
    SseRequest = FHttpModule::Get().CreateRequest();
    SseRequest->SetURL(McpServerBaseUrl + TEXT("/mcp/sse"));
    SseRequest->SetVerb(TEXT("GET"));
    SseRequest->SetHeader(TEXT("Accept"),        TEXT("text/event-stream"));
    SseRequest->SetHeader(TEXT("Cache-Control"), TEXT("no-cache"));
    SseRequest->SetTimeout(0);             // 长连接不超时

    // 关键：用 OnRequestProgress64（UE 5.6+）实现增量读取
    SseRequest->OnProcessRequestComplete().BindUObject(this, &ThisClass::OnSseRequestComplete);
    SseRequest->OnHeaderReceived()         .BindUObject(this, &ThisClass::OnSseHeadersReceived);
    SseRequest->OnRequestProgress64()      .BindUObject(this, &ThisClass::OnSseProgress64);
    SseRequest->ProcessRequest();
}
```

**增量读取算法**（`OnSseProgress64`，line 366）：
```cpp
const TArray<uint8>& FullBody = Req->GetResponse()->GetContent();  // 当前全量累积 body
FString NewChunk = /* FullBody[SseProcessedBytes..end] 转字符串 */;
SseProcessedBytes = FullBody.Num();   // 推进指针，避免重复处理
SseBuffer += NewChunk;                // 追加到局部 buffer
ProcessSseBuffer();                   // 解析 buffer 里完整的 SSE 事件
```

**设计亮点**：
- `SetTimeout(0)` 确保长连接不被 HTTP 层超时关闭
- `SseProcessedBytes` 指针避免每次回调都重新解析整个 buffer
- `OnRequestProgress64` 是 UE 5.6+ 新增的"已收到 N 字节"回调，是实现真 SSE 的关键

### SSE 报文解析（`ProcessSseBuffer`，line 435）

```cpp
void UEverythingCopilotMcpSubsystem::ProcessSseBuffer()
{
    // SSE 协议每个事件以 \n\n 分隔
    TArray<FString> Events;
    SseBuffer.ParseIntoArray(Events, TEXT("\n\n"), true);

    for (const FString& Event : Events)
    {
        FString EventType, EventData;
        TArray<FString> Lines;
        Event.ParseIntoArrayLines(Lines, true);

        for (const FString& Line : Lines)
        {
            if (Line.StartsWith(TEXT("event:")))
                EventType = Line.Mid(6).TrimStartAndEnd();
            else if (Line.StartsWith(TEXT("data:")))
                EventData = Line.Mid(5).TrimStartAndEnd();
        }
        HandleSseEvent(EventType, EventData);
    }
}
```

**局限（面试可提）**：仅处理 `\n\n` 分隔，不支持 `id:` 字段、多行 `data:` 续行、注释行（`:`开头），是**手写的简化 SSE 解析器**。

### 事件处理（`HandleSseEvent`，line 476）

| event 类型 | 行为 |
|---|---|
| `endpoint` | 解析 POST 地址 + session_id，设 `bSseConnected=true`，发 initialize + client/register |
| `message` | 解析 JSON-RPC，若 `method=="tools/call"` 则 `HandleMcpRequest()` |
| `ping` | 忽略（keepalive） |

**`endpoint` 事件的地址解析**（line 480）：
```cpp
// EventData 例："/mcp/messages/?session_id=abc-123"（相对路径）
// 或 "http://127.0.0.1:5000/mcp/messages/?session_id=abc-123"（绝对路径）
if (EventData.StartsWith(TEXT("http")))
    McpMessagesUrl = EventData;          // 绝对路径直接用
else
    McpMessagesUrl = McpServerBaseUrl + EventData;  // 相对路径拼接

// 提取 session_id
FString Left, Right;
if (EventData.Split(TEXT("session_id="), &Left, &Right))
    McpSessionId = Right;
```

## 1.3 指数退避重连

```cpp
// EverythingCopilotMcpSubsystem.cpp line 645
void UEverythingCopilotMcpSubsystem::ScheduleReconnect()
{
    if (ReconnectAttemptCount >= MaxReconnectAttempts)  // MaxReconnectAttempts = 10
    {
        UE_LOG(LogEverythingCopilot, Warning, TEXT("Max reconnect attempts reached"));
        return;
    }

    // 指数退避：2.0 * 1.5^attempt，上限 30s
    float Delay = FMath::Min(2.0f * FMath::Pow(1.5f, ReconnectAttemptCount), 30.0f);
    GEditor->GetTimerManager()->SetTimer(ReconnectTimerHandle, [this](){
        StartSseConnection();
    }, Delay, false);
    ReconnectAttemptCount++;
}
```

**设计考量**：
- 初期（前几次）快速重连（3s、4.5s…），后期间隔拉长
- 上限 30s 是经验值（超过 30s 用户能感知、且大多数短暂故障已恢复）
- 最多 10 次防止永远后台重连

## 1.4 JSON-RPC 派发与 Mock Socket 模式

### 核心设计：Mock Socket 模式

UE 的 handler 函数签名中有一个 `Socket` 参数，但在 SSE 模式下**没有真实网络连接**。解决方案是创建一个 Mock Socket 作为**结果存储槽**：

```cpp
// EverythingCopilotMcpSubsystem.cpp line 670
void UEverythingCopilotMcpSubsystem::HandleMcpRequest(const FString& RequestId,
    const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments)
{
    // 1. 创建 Mock Socket（结果存储槽）
    TSharedPtr<FEverythingCopilotMcpWebSocket> MockSocket = MakeShared<...>();
    MockSocket->SetMockMode(true);

    // 2. 调度到 Game Thread 执行工具
    AsyncTask(ENamedThreads::GameThread, [this, RequestId, ToolName, Arguments, MockSocket]()
    {
        ProcessAutomationRequest(RequestId, ToolName, Arguments, MockSocket);

        // 3. 读取 Mock 结果
        FString ResultJson = MockSocket->GetMockResult();

        // 4. 构造 tools/call_response 负载
        TSharedPtr<FJsonObject> ResponseParams = MakeShared<FJsonObject>();
        ResponseParams->SetStringField(TEXT("request_id"), RequestId);
        ResponseParams->SetStringField(TEXT("result"), ResultJson);

        // 5. POST 回服务端
        PostJsonRpc(TEXT("tools/call_response"), ResponseParams);
    });
}
```

**设计亮点**：
- 同一套 handler 代码既能处理 WebSocket（旧）也能处理 SSE（新）
- Mock Socket 模式解耦了传输层和业务逻辑
- `AsyncTask(GameThread)` 确保所有编辑器 API 调用在主线程

### 工具派发入口（`ProcessAutomationRequest`）

```cpp
// EverythingCopilotMcp_ProcessRequest.cpp line 4
bool UEverythingCopilotMcpSubsystem::ProcessAutomationRequest(
    const FString& RequestId, const FString& Action,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FEverythingCopilotMcpWebSocket> RequestingSocket)
{
    if (FAutomationHandler* Handler = AutomationHandlers.Find(Action))
        return (*Handler)(RequestId, Action, Payload, RequestingSocket);
    else {
        SendAutomationError(RequestingSocket, RequestId, TEXT("Unknown action"), TEXT("UNKNOWN_ACTION"));
        return false;
    }
}
```

`AutomationHandlers` 是 `TMap<FString, FAutomationHandler>` 哈希表，O(1) 查找。

### Handler 函数签名

```cpp
using FAutomationHandler = TFunction<bool(
    const FString& RequestId,
    const FString& Action,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FEverythingCopilotMcpWebSocket> Socket)>;
```

## 1.5 FMcpToolDefinitionBuilder：Fluent API 构建工具 Schema

```cpp
// EverythingCopilotMcpSubsystem.cpp line 21
class FMcpToolDefinitionBuilder
{
    TSharedPtr<FJsonObject> RootObject;        // 顶层工具定义
    TSharedPtr<FJsonObject> ParametersObject;  // "type":"object" 包装
    TSharedPtr<FJsonObject> PropertiesObject;  // properties 映射

public:
    FMcpToolDefinitionBuilder(const FString& ToolName);
    FMcpToolDefinitionBuilder& SetDescription(const FString& Desc);
    FMcpToolDefinitionBuilder& AddParameter(const FString& Name, const FString& Type,
                                             const FString& Desc, bool bRequired = false);
    FMcpToolDefinitionBuilder& AddEnumParameter(const FString& Name,
                                                 const TArray<FString>& Values,
                                                 const FString& Desc, bool bRequired = false);
    FMcpToolDefinitionBuilder& AddArrayParameter(const FString& Name, const FString& ItemType,
                                                  const FString& Desc, bool bRequired = false);
    TSharedPtr<FJsonObject> Build();

    static bool ValidateToolDefinition(const TSharedPtr<FJsonObject>& Def);
};
```

**使用示例**：
```cpp
auto ToolDef = FMcpToolDefinitionBuilder("control_actor")
    .SetDescription("Control actors in the level")
    .AddEnumParameter("action", {"spawn", "delete", "move"}, "The action to perform", true)
    .AddParameter("actorPath", "string", "Path or name of the actor")
    .AddParameter("location", "object", "Spawn location {x,y,z}")
    .Build();
```

**递归 Schema 校验**（`ValidateSchema`，line 151）：
```cpp
static bool ValidateSchema(const TSharedPtr<FJsonObject>& Schema)
{
    // 检查 array.items 是否有 type
    if (Type == "array") {
        const TSharedPtr<FJsonObject>* Items;
        if (Schema->TryGetObjectField(TEXT("items"), Items))
            ValidateSchema(*Items);  // 递归
    }
    // 检查 object.properties 每个字段是否有 type
    if (Type == "object") {
        const TSharedPtr<FJsonObject>* Props;
        if (Schema->TryGetObjectField(TEXT("properties"), Props))
            for (auto& Pair : (*Props)->Values)
                ValidateSchema(Pair.Value->AsObject());  // 递归
    }
}
```

## 1.6 工具注册与批量注册

### 注册方式（三个渠道）

1. **代码注册**：`RegisterHandler` 在 `InitializeHandlers()` 用 `FMcpToolDefinitionBuilder` 构建 schema
2. **外部 JSON**：`LoadToolDefinitions()` 读 `Plugins/EverythingCopilot/Content/Mcp/Tools/*.json`
3. **过滤**：`UEverythingCopilotSettings::EnabledTools` 非空时只暴露白名单工具

### 批量注册模式（NextPCG ~80+ 工具）

```cpp
// EverythingCopilotMcpSubsystem.cpp line 1690
struct FNextPCGToolDef { FString Name; FString UEFunction; FString Description; FString Category; };
TArray<FNextPCGToolDef> NextPCGTools = {
    {"nextpcg_export_mesh", "ExportMesh", "Export mesh to file", "nextpcg_asset"},
    {"nextpcg_import_heightmap", "ImportHeightmap", "Import heightmap", "nextpcg_terrain"},
    // ... ~80+ 工具
};

for (const FNextPCGToolDef& Tool : NextPCGTools) {
    RegisterHandler(Tool.Name, [this, UEFunc = Tool.UEFunction](...) {
        return HandleNextPCGGenericTool(RID, S, P, Act, UEFunc, Desc);
    }, Builder.Build());
}
```

### `HandleNextPCGGenericTool`：大 if-else 分发

```cpp
// EverythingCopilotMcp_NextPCGHandlers.cpp line 922
bool HandleNextPCGGenericTool(..., const FString& UEFunctionName, ...)
{
    if (Act == TEXT("CityRunBlockProcessing"))         { /* 实现 */ }
    else if (Act == TEXT("CityExportBuildingInputs"))  { /* 实现 */ }
    // ... 约数百个 if-else 分支 ...
    else {
        SendAutomationError(Socket, RequestId,
            FString::Printf(TEXT("Function '%s' not implemented"), *UEFunctionName),
            TEXT("NOT_IMPLEMENTED"));
    }
}
```

## 1.7 线程模型

| 阶段 | 执行线程 | 说明 |
|---|---|---|
| `OnRequestProgress64` / `ProcessSseBuffer` / `HandleSseEvent` | **HTTP 模块线程** | 不能在这里做 UE 编辑器 API 操作 |
| `HandleMcpRequest` 调度 | HTTP 线程 → `AsyncTask(GameThread)` | 调度完立即返回 |
| 所有工具 handler | **Game Thread（主线程）** | 可以安全调用 UE 编辑器 API |
| `PostJsonRpc`（响应）| GT 内发起，HTTP 异步发出 | 即发即忘，不阻塞 GT |
| `ScheduleReconnect` timer | Editor TimerManager（GT） | |

**关键规则**：
- SSE 解析不能调编辑器 API → 必须用 `AsyncTask` marshal 到 GT
- 所有编辑器 mutation（spawn actor、导入资产）必须在 GT
- `PostJsonRpc` 是异步 HTTP，不阻塞 GT

## 1.8 难点 / 亮点 / 改进

**难点**：
1. `FHttpModule` 实现真 SSE：UE HTTP 模块为短请求设计，用 `OnRequestProgress64` + 手动指针实现增量流读取是非标准用法
2. 手写 SSE 解析器：不支持 `id:`/多行 `data:`/注释行，健壮性有限
3. 非标准 JSON-RPC 响应：`tools/call_response` 靠 `params.request_id` 关联，不是标准 id 匹配
4. `FEverythingCopilotMcpWebSocket` 命名误导：历史 WebSocket → SSE 迁移遗留
5. `HandleNextPCGGenericTool` 大单体：~7600 行 if-else，维护成本高

**亮点**：
1. SSE over FHttpModule 的增量流读取：在 UE 这个不原生支持 SSE 的环境里实现了真流式事件消费
2. 指数退避重连：`2.0 * 1.5^n` 上限 30s、10 次上限，工程实用
3. Mock Socket 模式：统一了 WebSocket/SSE 两种传输的 handler 代码
4. 工具 schema 外置 JSON：`Content/Mcp/Tools/*.json` 可以不重编译修改工具描述
5. 批量注册 + 反射调用：80+ 工具通过结构体数组批量注册，降低样板代码

**改进**：
1. 注册 `execute_python` 工具：直接用 `IPythonScriptPlugin::ExecPythonCommandEx` 替代文件+控制台两跳
2. 完善 SSE 解析：支持 `id:` 字段、多行 `data:`，改用状态机解析
3. 拆分 `HandleNextPCGGenericTool`：按功能域拆成子 dispatcher
4. 重命名 `FEverythingCopilotMcpWebSocket`：改为 `FMcpToolResultHolder`
5. 连接状态指标：暴露 `bSseConnected`、`ReconnectAttemptCount` 到 Editor 状态栏

---

# 模块二：JSON Field 类型系统（NextPCG）

## 2.1 总体结构

```
NextPCG/Source/NextPCGRuntime/
├── Public/JsonFields/
│   └── NextPCGJsonField.h                    # 基类：抽象多态字段
├── Private/JsonFields/
│   ├── NextPCGJsonIntField.h
│   ├── NextPCGJsonFloatField.h
│   ├── NextPCGJsonStringField.h
│   ├── NextPCGJsonFloat2Field.h
│   ├── NextPCGJsonFloat3Field.h
│   ├── NextPCGJsonFloat4Field.h
│   ├── NextPCGJsonColorField.h
│   ├── NextPCGJsonTransformField.h
│   ├── NextPCGJsonStaticMeshField.h
│   ├── NextPCGJsonTextureField.h
│   ├── NextPCGJsonCurveField.h
│   ├── NextPCGJsonEnumField.h
│   ├── NextPCGJsonListField.h
│   ├── NextPCGJsonFileField.h
│   ├── NextPCGJsonHeightFieldField.h
│   └── NextPCGJsonInstancedStaticMeshField.h
```

**设计目标**：用一个统一的类型系统把 JSON 数据映射到 UE 的 Pin/Property 系统，同时驱动异步 I/O 管线。

## 2.2 基类：`NextPCGJsonField`（line 24）

**设计模式**：**抽象多态字段** + **Template Method 钩子**集成 Tick 状态机。

```cpp
class NextPCGJsonField
{
protected:
    FString PropertyName;                      // JSON 键名
    TSharedPtr<FJsonObject> JsonObject;        // 解析后的 JSON 数据

public:
    // Pin 系统（连接到 UE 的 PCG 节点图）
    virtual FName GetPinCategory() = 0;        // "real", "string", "int", ...
    virtual FName GetPinSubCategory();
    virtual FString GetFriendlyName();
    virtual FString GetLabel();
    virtual FString GetPropertyName();

    // UE Property 桥接
    virtual bool CreateUEProperty(FFieldVariant NewStruct, FProperty*& Property) = 0;
    virtual bool GetUEPropertyValue(void* Container, FString& OutValue);

    // 值管理
    virtual void FillDefaultValue();
    virtual void FillValue();

    // I/O 状态机钩子（与 Tick 状态机集成）
    virtual bool ExportConversion(...) = 0;    // 导出时是否需要文件转换
    virtual bool ImportDownload(...) = 0;      // 导入时是否需要下载文件
    virtual bool ImportConversion(...) = 0;    // 导入时是否需要格式转换
};
```

**关键设计**：`ExportConversion`/`ImportDownload`/`ImportConversion` 三个虚方法直接集成到异步 Tick 状态机（见模块三），每个字段类型决定自己是否需要文件 I/O。

## 2.3 具体字段类型：以 Float 为例

```cpp
// NextPCGJsonFloatField.h
class NextPCGJsonFloatField : public NextPCGJsonField
{
public:
    virtual FName GetPinCategory() override { return "real"; }
    virtual FName GetPinSubCategory() override { return "double"; }

protected:
    float Value;
    float DefaultValue;

    // ListSubFieldBase 特化：控制 I/O 行为
    template<> struct ListSubFieldBase<NextPCGJsonFloatField> {
        constexpr static bool ShoudExportConversion = false;  // 不需要文件导出
        constexpr static bool ShoudImportDownload = false;    // 不需要文件下载
        constexpr static bool ShoudImportConversion = false;  // 不需要格式转换
    };
};
```

**设计亮点**：`ListSubFieldBase<T>` 模板特化作为**策略类**，用编译期常量控制字段类型在 Tick 状态机中的行为。简单类型（float/int/string）不需要文件 I/O，复杂类型（StaticMesh/Texture/HeightField）需要。

## 2.4 复杂字段类型：StaticMesh

```cpp
class NextPCGJsonStaticMeshField : public NextPCGJsonField
{
    // ListSubFieldBase 特化：
    // ShoudExportConversion = true   → 导出时需要 OBJ→StaticMesh 转换
    // ShoudImportDownload = true     → 导入时需要从 Server 下载文件
    // ShoudImportConversion = true   → 导入时需要 OBJ/FBX/GLTF 解析
};
```

## 2.5 难点 / 亮点 / 改进

**难点**：
1. 18 种字段类型的虚方法实现，每种都需要正确处理 JSON 序列化/反序列化
2. `ListSubFieldBase` 模板特化需要与 Tick 状态机精确同步
3. 复杂类型（StaticMesh/Texture）的文件 I/O 路径容易出错

**亮点**：
1. 类型安全的 JSON→UE Property 映射：避免了手写 if-else 类型判断
2. 策略类模式：编译期决定 I/O 行为，零运行时开销
3. 统一的虚方法接口：新增字段类型只需继承 + 实现 6 个虚方法

**改进**：
1. 考虑用 `TVariant`（C++17 `std::variant` 的 UE 版）替代虚函数，减少 vtable 开销
2. 为复杂类型添加批量 I/O 支持（当前是逐字段串行）

---

# 模块三：异步 Tick 状态机

## 3.1 总体结构

**文件**：`NextPCGRuntime/Public/NextPCGFileInterfaceType.h`（line 1163）

这是整个 PCG 数据管线的**异步调度核心**，驱动 UE ↔ Server 之间的非阻塞数据交换。

## 3.2 状态枚举：14 个状态

```cpp
enum class ENextPCGTickStatus : uint8 {
    IDLE,                    // 空闲
    BEGIN,                   // 开始处理
    EXPORT_CONVERSION,       // 导出转换（字段 → 文件）
    DIFF_FILE_CACHE_PENDING, // 文件缓存差异计算（MD5 去重）
    EXPORT_PENDING,          // 文件上传中
    HOUDINI_REQUEST,         // 发送 Houdini 请求
    HOUDINI_PENDING,         // 等待 Houdini 结果
    IMPORT_DOWNLOAD,         // 下载结果文件
    IMPORT_PENDING,          // 下载进行中
    IMPORT_CONVERSION,       // 导入转换（文件 → 字段）
    CALL_RETURN_DELEGATE,    // 调用完成回调
    PROFILE_REQUEST,         // 性能分析请求
    PROFILE_PENDING,         // 性能分析进行中
    CLEAN_CONTEXT,           // 清理上下文
    END                      // 结束
};
```

**状态流转**：
```
IDLE → BEGIN → EXPORT_CONVERSION → DIFF_FILE_CACHE_PENDING → EXPORT_PENDING
     → HOUDINI_REQUEST → HOUDINI_PENDING → IMPORT_DOWNLOAD → IMPORT_PENDING
     → IMPORT_CONVERSION → CALL_RETURN_DELEGATE → PROFILE_REQUEST → PROFILE_PENDING
     → CLEAN_CONTEXT → END → IDLE
```

## 3.3 Tick 上下文：`FNextPCGTickContext`

```cpp
struct FNextPCGTickContext {
    // 导出阶段
    TArray<FNextPCGUploadFileContext> ExportUploadFileContexts;  // 待上传文件
    TSharedFuture<TArray<...>> DiffFileCacheFuture;              // 异步缓存差异
    TArray<TSharedFuture<...>> ExportFutures;                    // 进行中的上传
    TFunction<TSharedFuture<...>()> CurrentExportTasks;          // 当前批次工厂
    TArray<TFunction<...>> ExportQueues;                          // 待处理上传批次

    // 导入阶段
    TFunction<TSharedFuture<...>()> CurrentImportTasks;          // 当前批次工厂
    TArray<TFunction<...>> ImportQueues;                          // 待处理下载批次
    TArray<TSharedFuture<FNextPCGDownloadFileContext>> ImportFutures;  // 进行中的下载

    // Houdini 阶段
    TArray<TSharedFuture<FNextPCGHoudiniContext>> HoudiniFutures;

    // 状态
    bool bAllFuturesReady;
    bool bAllFuturesSuccess;
    FScriptDelegate DelegateInstance;  // 完成回调
};
```

**关键设计**：`ExportQueues` 和 `ImportQueues` 是 `TFunction` 工厂数组——不是 Future。这允许**批量延迟执行**：系统可以入队多个批次，通过状态机顺序处理，而不是同时启动所有上传。

## 3.4 支持类型

### `FNextPCGUploadFileContext`

```cpp
struct FNextPCGUploadFileContext {
    FString LocalFilename;           // 本地文件路径
    FString RemoteFilename;          // 远程文件名
    FString User;                    // 用户名
    FString Path;                    // 远程路径
    bool bExist;                     // 文件是否存在
    FString RemoteOverrideFilename;  // 远程覆盖文件名
    // 性能统计
    double Upload;                   // 上传耗时
    double Diff;                     // 差异计算耗时
    bool bSuccess;
};
```

### `FNextPCGDownloadFileContext`

```cpp
struct FNextPCGDownloadFileContext {
    FString LocalFilename;
    FString Url;
    FString User;
    FString RemoteFilename;
    FString Path;
    bool UsePDG;
    double Download;                 // 下载耗时
    bool bSuccess;
};
```

### `FNextPCGGenericTask`

```cpp
class FNextPCGGenericTask {
    TUniqueFunction<void(ENamedThreads::Type, const FGraphEventRef&)> Function;
    ENamedThreads::Type TargetThread;
};
```

用于任意线程跳转的轻量任务包装。

### `FNextPCGError`

```cpp
struct FNextPCGError {
    FString ErrorTag;       // 错误标签
    FString ErrorInfo;      // 错误信息
    FString ErrorHip;       // Houdini 场景文件
    FString ErrorCookResult;// Cook 结果
};
```

## 3.5 难点 / 亮点 / 改进

**难点**：
1. 14 个状态的显式状态机，每个状态都有异步 Future 需要等待
2. `TFunction` 工厂模式的延迟执行增加了调试复杂度
3. 状态转换中的错误处理和清理逻辑
4. 与 JSON Field 系统的 `ExportConversion`/`ImportConversion` 钩子集成

**亮点**：
1. 批量延迟执行：用 `TFunction` 工厂代替立即创建 Future，支持分批处理大文件集
2. MD5 去重：`DIFF_FILE_CACHE_PENDING` 状态避免重复上传未修改文件
3. 性能统计：每个阶段都有耗时记录，便于瓶颈分析
4. 与 JSON Field 系统的深度集成：字段类型决定 I/O 行为

**改进**：
1. 考虑用 UE 的 `FAutoDeleteAsyncTask` 替代手动 Future 管理
2. 添加进度回调（当前只有完成回调）
3. 支持取消（当前只能等待完成或超时）

---

# 模块四：几何数学库（GeometryCopilot）

## 4.1 总体结构

```
GeometryCopilot/Source/
├── GeoCo_Primitives/Public/
│   ├── GeoCoBezier.h              # 三次 Bezier 曲线
│   ├── GeoCoMath.h                # 基础数学函数
│   └── GeoCoVector.h              # 维度泛型向量
├── GeoCo_Operations/Public/
│   ├── GeoCoBezierFitting.h       # 曲线拟合
│   ├── GeoCoBezierIntersection.h  # 曲线求交
│   ├── GeoCoBezierOffset.h        # 曲线偏移
│   ├── GeoCoBezierBlend.h         # 曲线混合
│   ├── GeoCoSubdivision.h         # 细分算法
│   ├── GeoCoArcLength.h           # 弧长计算
│   └── GeoCoCircleArc.h           # 圆弧基元
└── GeoCo_Graph/Public/
    ├── GeoCoGraph.h               # 泛型图数据结构
    ├── GeoCoGraphAlgorithms.h     # 图算法
    ├── GeoCoGraphTraits.h         # 图特征系统
    └── GeoCoGraphTypes.h          # 图类型定义
```

## 4.2 Bezier 曲线：维度泛型模板

```cpp
// GeoCoBezier.h line 32
template<int Dim>
struct TGeoCoCubicBezier
{
    static constexpr int Dimension = Dim;
    static constexpr int Degree = 3;
    static constexpr int NumControlPoints = 4;

    TGeoCoVector<Dim> ControlPoints[4];  // 4 个控制点

    // 工厂方法
    static TGeoCoCubicBezier FromHermite(const TGeoCoVector<Dim>& Start, const TGeoCoVector<Dim>& StartTangent,
                                          const TGeoCoVector<Dim>& End,   const TGeoCoVector<Dim>& EndTangent);
    static TGeoCoCubicBezier MakeLinear(const TGeoCoVector<Dim>& Start, const TGeoCoVector<Dim>& End);

    // 访问器
    const TGeoCoVector<Dim>& P0() const { return ControlPoints[0]; }
    const TGeoCoVector<Dim>& P1() const { return ControlPoints[1]; }
    const TGeoCoVector<Dim>& P2() const { return ControlPoints[2]; }
    const TGeoCoVector<Dim>& P3() const { return ControlPoints[3]; }
    const TGeoCoVector<Dim>& StartPoint() const { return P0(); }
    const TGeoCoVector<Dim>& EndPoint() const { return P3(); }
};
```

**设计亮点**：
- `Dim` 模板参数（2 或 3）实现 2D/3D 曲线的代码复用
- `MakeLinear()` 将线段表示为三次 Bezier（内控制点在 1/3 和 2/3 处）
- `FromHermite()` 委托给自由函数 `GeoCo::HermiteToBezier()`，保持结构体轻量

## 4.3 曲线拟合

```cpp
// GeoCoBezierFitting.h
template<int Dim>
struct TGeoCoFittingResult
{
    TArray<TGeoCoCubicBezier<Dim>> Segments;  // 拟合后的 Bezier 段
    double MaxError;                           // 最大拟合误差
    bool bSuccess;
};

namespace GeoCo::Internal {
    // 弦长参数化：按累积弦长归一化到 [0,1]
    template<int Dim>
    TArray<double> ComputeChordLengthParameters(const TArray<TGeoCoVector<Dim>>& Vertices);

    // 向心参数化：弦长平方根，对急转弯更鲁棒
    template<int Dim>
    TArray<double> ComputeCentripetalParameters(const TArray<TGeoCoVector<Dim>>& Vertices);
}
```

**设计亮点**：两种参数化算法都是**头文件模板**，编译期实例化，零运行时开销。

## 4.4 图论模块：策略模式

### 类型定义（`GeoCoGraphTypes.h`）

```cpp
template<typename EdgeType>
struct TGeoCoGraphEdgeRef
{
    int32 EdgeIndex;
    bool bForward;  // 边的方向
};

template<typename EdgeType>
struct TGeoCoGraphPath
{
    TArray<TGeoCoGraphEdgeRef<EdgeType>> Edges;
    bool bIsClosed;
    int32 StartPointDegree;  // 起点度数
    int32 EndPointDegree;    // 终点度数
};
```

### 特征系统（`GeoCoGraphTraits.h`）

```cpp
template<typename EdgeType>
struct TGeoCoGraphTraits
{
    // 主模板：未特化时编译失败
    static_assert(sizeof(EdgeType) == 0, "Must specialize TGeoCoGraphTraits for your edge type");

    // 必须实现的静态方法：
    static int32 GetLeavePoint(const EdgeType& Edge, bool bForward);
    static int32 GetArrivePoint(const EdgeType& Edge, bool bForward);
    static bool IsEdgeValid(const EdgeType& Edge);
};
```

**设计模式**：**策略模式**通过模板特化实现——用户为自己的边类型特化 `TGeoCoGraphTraits`，图算法自动适配。

### 拓扑分析算法（`GeoCoGraphAlgorithms.h`）

```cpp
template<typename PointType, typename EdgeType>
TArray<TGeoCoGraphPath<EdgeType>> AnalyzeTopology(
    const TArray<PointType>& Points,
    const TArray<EdgeType>& Edges)
{
    // 1. 构建邻接表：PointToEdges[PointIndex] = [EdgeRef, ...]
    TMultiMap<int32, TGeoCoGraphEdgeRef<EdgeType>> PointToEdges;
    for (int32 i = 0; i < Edges.Num(); ++i) {
        int32 Leave = Traits::GetLeavePoint(Edges[i], true);
        int32 Arrive = Traits::GetArrivePoint(Edges[i], true);
        PointToEdges.Add(Leave, {i, true});
        PointToEdges.Add(Arrive, {i, false});
    }

    // 2. 识别路径起点（度数 != 2 的点）
    TArray<int32> StartPoints;
    for (auto& Pair : PointToEdges) {
        if (Pair.Value.Num() != 2)
            StartPoints.Add(Pair.Key);
    }

    // 3. 从每个起点遍历，直到遇到另一个起点或闭合
    TArray<TGeoCoGraphPath<EdgeType>> Paths;
    for (int32 Start : StartPoints) {
        TGeoCoGraphPath<EdgeType> Path;
        TraverseFrom(Start, PointToEdges, Edges, Path);
        Paths.Add(Path);
    }
    return Paths;
}
```

## 4.5 难点 / 亮点 / 改进

**难点**：
1. 维度泛型模板的编译期实例化：2D/3D 共用同一套算法代码
2. 图特征系统的模板特化：用户必须正确实现三个静态方法
3. 拓扑分析的边界处理：闭合路径、孤立点、多连通分量

**亮点**：
1. 头文件模板：所有算法编译期实例化，零运行时开销
2. 策略模式：图算法与具体边类型解耦，可复用于任意几何图
3. 两种参数化算法：弦长和向心参数化覆盖不同曲线特征

**改进**：
1. 添加 SIMD 加速的向量运算
2. 支持 NURBS 曲线（当前只有 Bezier）
3. 图算法添加最短路径、最小生成树等

---

# 模块五：操作栈框架（EditorUtilityCopilot）

## 5.1 总体结构

**文件**：`EditorUtilityCopilot/Source/EditorUtilityCopilot/Public/OperationStack/EditorUtilityCopilotOperationStack.h`

**设计模式**：**Composite + Command**，支持 UE Transaction（撤销/重做）。

## 5.2 三层架构

### 层 1：接口（`IEditorUtilityCopilotOperationInterface`，line 52）

```cpp
UINTERFACE()
class UEditorUtilityCopilotOperationInterface : public UInterface
{
    GENERATED_BODY()
};

class IEditorUtilityCopilotOperationInterface
{
    virtual void Run() = 0;                                    // 执行操作
    virtual TArray<TSubclassOf<...>> GetSupportedOperationClasses() = 0;  // 声明子操作类型
    virtual void CollectOperations(TArray<...>& OutOperations) = 0;       // 收集到扁平列表
};
```

### 层 2：操作节点（`UEditorUtilityCopilotOperation`，line 70）

```cpp
UCLASS(Abstract, BlueprintType)
class UEditorUtilityCopilotOperation : public UObject,
                                         public IEditorUtilityCopilotOperationInterface
{
    GENERATED_BODY()

public:
    // 树结构
    TSoftObjectPtr<UEditorUtilityCopilotOperation> SurOperation;   // 父节点
    TArray<TObjectPtr<UEditorUtilityCopilotOperation>> SubOperations;  // 子节点

    // 执行顺序
    int32 GlobalIndex;   // 栈内全局序号
    int32 LocalIndex;    // 父节点内局部序号

    // 流程控制
    bool bIsEnabled;
    EEditorUtilityCopilotOperationStatus Status;  // Idle/Processing/Succeed/Aborted/Failed

    // Slate 集成
    virtual TSharedRef<SWidget> CreateHeaderWidget();  // 自定义头部 UI
    virtual TSharedRef<SWidget> CreateBodyWidget();    // 自定义内容 UI
};
```

### 层 3：栈管理器（`UEditorUtilityCopilotOperationStack`，line 155）

```cpp
UCLASS(BlueprintType)
class UEditorUtilityCopilotOperationStack : public UObject
{
    // 操作树根节点
    UPROPERTY() TObjectPtr<UEditorUtilityCopilotOperation> OperationRoot;

    // 扁平操作列表（按执行顺序）
    UPROPERTY() TArray<TObjectPtr<UEditorUtilityCopilotOperation>> Operations;

    // 允许的操作类型白名单
    UPROPERTY() TArray<TSubclassOf<UEditorUtilityCopilotOperation>> SupportedOperationClasses;

    // 关键操作
    void AddOperation(UEditorUtilityCopilotOperation* Op, UEditorUtilityCopilotOperation* Parent = nullptr,
                      bool bInTransactional = true);
    void AddOperationOfClass(TSubclassOf<...> OpClass, ...);
    void CreateDefaultOperations(int32 MaxDepth = 3);
    void RemoveOperation(UEditorUtilityCopilotOperation* Op, bool bInTransactional = true);
    void MoveOperationInStack(int32 SourceLocalIdx, int32 TargetLocalIdx, int32 SurGlobalIdx);
    void ReorderOperations();
};
```

## 5.3 状态枚举

```cpp
enum class EEditorUtilityCopilotOperationStatus : uint8 {
    Idle,
    Processing,
    Succeed,
    Aborted,
    Failed
};
```

## 5.4 上下文传递系统

```cpp
UCLASS(Abstract)
class UEditorUtilityCopilotOperationContext : public UObject
{
    GENERATED_BODY()
};

USTRUCT()
struct FEditorUtilityCopilotOperationContextCollection
{
    UPROPERTY() TMap<FName, TObjectPtr<UEditorUtilityCopilotOperationContext>> Contexts;
    TSoftObjectPtr<UEditorUtilityCopilotOperation> CurrentOperation;  // 当前拥有上下文的操作
};
```

**设计亮点**：命名上下文映射允许不同类型的操作共享数据，`CurrentOperation` 追踪哪个操作拥有上下文。

## 5.5 UE Transaction 集成

所有变异方法（`AddOperation`、`RemoveOperation`、`MoveOperationInStack`）都有 `bInTransactional` 参数：

```cpp
void AddOperation(UEditorUtilityCopilotOperation* Op, ..., bool bInTransactional = true)
{
    if (bInTransactional)
    {
        GEditor->BeginTransaction(NSLOCTEXT("OpStack", "AddOperation", "Add Operation"));
        // ... 修改 ...
        GEditor->EndTransaction();
    }
    else { /* 直接修改 */ }
}
```

**设计亮点**：操作栈的所有修改都可以通过 UE 的标准撤销/重做系统回退。

## 5.6 难点 / 亮点 / 改进

**难点**：
1. 树结构 + 扁平列表的双视图同步：修改树后必须重算 `GlobalIndex`
2. UE Transaction 集成：每个变异方法都需要正确的事务边界
3. Slate 集成：每个操作类型都可以自定义 UI

**亮点**：
1. Composite 模式：操作可以包含子操作，支持任意深度的层次结构
2. Transaction 集成：所有修改可撤销/重做
3. 上下文传递系统：命名映射 + 操作所有权追踪
4. `CreateDefaultOperations`：递归自动创建默认操作树

**改进**：
1. 添加操作依赖（某些操作必须在其他操作之后执行）
2. 支持异步操作（当前 `Run()` 是同步的）
3. 添加操作模板/预设

---

# 模块六：点云与规则处理器（RuleProcessor）

## 6.1 总体结构

```
RuleProcessor/Source/RuleProcessor/
├── Public/
│   ├── PointCloud.h                    # 点云基类
│   ├── PointCloudView.h               # 过滤视图
│   └── PointCloudQuery.h              # SQL 查询
├── Private/
│   ├── PointCloud.cpp
│   ├── PointCloudSqliteHelpers.h      # SQLite 工具
│   ├── PointCloudSliceAndDice*.h/cpp  # 规则处理管线
│   └── PointCloudWorldPartitionHelpers.h  # World Partition 集成
```

## 6.2 点云基类（`UPointCloud`，line 41）

```cpp
UCLASS(Abstract, BlueprintType, hidecategories=(Object))
class UPointCloud : public UObject
{
    GENERATED_BODY()
    friend class UPointCloudView;  // View 有私有访问权

public:
    // 数据类型
    USTRUCT(BlueprintType)
    struct FPointCloudPoint {
        UPROPERTY() FTransform Transform;
        UPROPERTY() TMap<FString, FString> Attributes;  // 任意字符串元数据
    };

    // 过滤模式
    UENUM()
    enum EFilterMode { FILTER_Or, FILTER_And, FILTER_Not };

    // 加载模式
    UENUM()
    enum ELoadMode { ADD, REPLACE };

    // 生命周期
    virtual bool IsInitialized() PURE_VIRTUAL(...);
    virtual bool AttemptToUpdate() PURE_VIRTUAL(...);
    virtual bool NeedsUpdating() PURE_VIRTUAL(...);

    // 视图
    virtual UPointCloudView* MakeView() PURE_VIRTUAL(...);

    // 属性系统
    virtual TArray<FString> GetDefaultAttributes() PURE_VIRTUAL(...);   // 所有点共享
    virtual TArray<FString> GetMetadataAttributes() PURE_VIRTUAL(...);  // 稀疏，逐点
    virtual bool HasDefaultAttribute(const FString& AttrName) PURE_VIRTUAL(...);
    virtual bool HasMetaDataAttribute(const FString& AttrName) PURE_VIRTUAL(...);

    // 信息
    virtual int32 GetCount() PURE_VIRTUAL(...);
    virtual FBox GetBounds() PURE_VIRTUAL(...);

    // I/O
    virtual bool LoadFromCsv(const FString& Filename, const FBox* InImportBounds = nullptr) PURE_VIRTUAL(...);

    // 编辑器专用（不打进 Cook）
    virtual bool IsEditorOnly() const override { return true; }
};
```

**设计亮点**：
- **双层属性系统**：Default Attributes（所有点共享的属性，如来源文件）vs Metadata（稀疏的逐点属性，如标注）
- **View 模式**：`MakeView()` 返回过滤视图，不修改源数据
- **PURE_VIRTUAL 接口**：具体实现（SQLite 后端）提供真实逻辑
- **空间过滤**：`LoadFromCsv` 支持 `InImportBounds` 在导入时过滤

## 6.3 难点 / 亮点 / 改进

**难点**：
1. SQLite 后端的抽象：`UPointCloud` 是纯虚基类，具体实现完全由子类决定
2. 双层属性系统的设计：Default vs Metadata 的语义区分
3. View 的过滤逻辑：必须高效处理大量点云数据

**亮点**：
1. 策略模式：`UPointCloud` 定义接口，SQLite 实现细节完全封装
2. View 模式：过滤不修改源数据，支持多视图并行
3. Editor Only：点云资产不打进 Cook 包，减小发布体积

**改进**：
1. 添加空间索引（R-tree / KD-tree）加速空间查询
2. 支持流式加载大点云（当前全量加载）
3. 添加点云可视化调试工具

---

# 附录 A：简历条目模板（可直接改写）

> STAR 式：动词 + 难点 + 量化/结果。按目标岗位精简。

- **UE 插件 MCP 通信层**：在 UE `UEditorSubsystem` 中实现基于 **`FHttpModule` + `OnRequestProgress64`** 的真 SSE 流式接收（UE 无原生 SSE 支持），手写 SSE 帧解析器；实现 **指数退避重连**（2×1.5^n，上限 30s，最多 10 次）；工具调用经 **`AsyncTask(GameThread)`** 线程 marshal 后通过 `TMap<FString,TFunction>` 哈希派发到对应 handler，结果以 `tools/call_response` POST 回服务端。支持 **100+ 工具**的批量注册与 FMcpToolDefinitionBuilder Fluent API。

- **JSON Field 多态类型系统**：设计 18 种字段类型的抽象多态系统，用 `ListSubFieldBase<T>` **编译期策略类**控制每种类型在异步 I/O 管线中的行为（是否需要文件导出/下载/转换），零运行时开销。

- **14 状态异步 Tick 管线**：实现 UE ↔ Server 之间的非阻塞数据交换状态机，支持 **TFunction 工厂延迟执行**（分批处理大文件集）、**MD5 去重**避免重复上传、性能统计记录每个阶段耗时。

- **几何数学库**：实现**维度泛型 Bezier 曲线**模板（2D/3D 共用）、**策略模式图论框架**（用户特化 `TGeoCoGraphTraits` 即可复用拓扑分析算法）、弦长/向心两种参数化算法。

- **操作栈框架**：实现 **Composite + Command** 模式的编辑器操作栈，支持任意深度层次结构、**UE Transaction 集成**（所有修改可撤销/重做）、命名上下文传递系统、Slate 自定义 UI 集成。

---

# 附录 B：高频面试问答（带详解）

**Q1：UE 里怎么实现 SSE？为什么用 `OnRequestProgress64`？**
A：UE 的 `FHttpModule` 是通用 HTTP 客户端，原本为短请求设计。要实现 SSE（服务端持续推流），需要在流入时**增量读**，不能等整个响应结束。`OnRequestProgress64` 是 UE 5.6+ 提供的"已收到 N 字节"回调，每次有新数据就触发；配合 `GetContent()` 和一个 `SseProcessedBytes` 指针只处理新增字节，实现了类似 SSE 的流式消费。`SetTimeout(0)` 确保长连接不被 HTTP 层超时关闭。

**Q2：工具调用为什么要 `AsyncTask(GameThread)`？直接在回调里处理不行吗？**
A：`OnRequestProgress64` 在 FHttpModule 工作线程触发，不是 UE 主线程（Game Thread）。UE 的编辑器 API（spawn actor、导入资产、调 `GEngine->Exec` 等）**必须在 GT 调用**，否则会崩溃或产生未定义行为。所以在 HTTP 线程解析出 `tools/call` 后立刻用 `AsyncTask(ENamedThreads::GameThread, ...)` 把实际工具执行 marshal 到 GT，SSE 解析线程只负责调度。

**Q3：Mock Socket 模式是什么？为什么需要它？**
A：handler 函数签名中有一个 `Socket` 参数，用于发送结果。在 WebSocket 模式下这是真实网络连接；但在 SSE 模式下**没有实时双向连接**。Mock Socket 是一个"结果存储槽"——handler 把结果写进 Mock Socket（`SetMockResult(json)`），控制流返回到 `HandleMcpRequest` 的 lambda 读取结果并 POST 回去。这样同一套 handler 代码既能处理 WebSocket 也能处理 SSE。

**Q4：JSON Field 系统的 `ListSubFieldBase` 是什么？为什么用编译期策略？**
A：`ListSubFieldBase<T>` 是模板特化的策略类，用 `constexpr static bool` 控制字段类型在异步 I/O 管线中的行为。简单类型（float/int/string）不需要文件 I/O（三个 flag 都是 false），复杂类型（StaticMesh/Texture）需要文件导出/下载/转换（flag 为 true）。用编译期常量是因为这些行为在类型确定后就不会改变，不需要运行时判断。

**Q5：14 状态 Tick 管线的 `TFunction` 工厂是什么？为什么不直接用 Future？**
A：`ExportQueues` 和 `ImportQueues` 存的是 `TFunction<TSharedFuture<...>()>`——一个返回 Future 的工厂函数，而不是已经启动的 Future。这允许**批量延迟执行**：系统可以入队多个批次（比如 100 个文件分 10 批），通过状态机顺序处理每一批，而不是同时启动 100 个上传请求。好处是控制并发度、避免网络拥塞、支持分批错误处理。

**Q6：图论框架的 `TGeoCoGraphTraits` 是什么模式？**
A：策略模式（Policy-based design）。`TGeoCoGraphAlgorithms` 里的 `AnalyzeTopology` 算法是泛型的，不依赖具体边类型。用户为自己的边类型特化 `TGeoCoGraphTraits<EdgeType>`，实现 `GetLeavePoint`、`GetArrivePoint`、`IsEdgeValid` 三个静态方法，图算法自动适配。好处是同一套拓扑分析代码可以用于 Bezier 图、道路网、管网等不同场景。

**Q7：操作栈为什么支持 UE Transaction？**
A：操作栈管理编辑器的状态修改（添加/删除/重排操作）。如果不支持撤销，用户误操作后无法恢复。通过在每个变异方法里包裹 `GEditor->BeginTransaction/EndTransaction`，所有修改自动进入 UE 的撤销栈，用户可以用 Ctrl+Z 回退。`bInTransactional` 参数允许某些内部修改跳过事务（比如初始化时的批量创建）。

---

# 附录 C：术语速查表

| 术语 | 含义 |
|---|---|
| **MCP** | Model Context Protocol，AI Agent 与工具/数据源交互的协议 |
| **SSE** | Server-Sent Events，基于 HTTP 的单向服务端推送 |
| **JSON-RPC 2.0** | 轻量远程调用协议，请求/响应/通知，`id` 关联 |
| **UEditorSubsystem** | UE 编辑器子系统，自动生命周期管理 |
| **FHttpModule** | UE HTTP 客户端模块 |
| **OnRequestProgress64** | UE 5.6+ HTTP 增量回调 |
| **AsyncTask(GameThread)** | 把任务调度到 UE 主线程执行 |
| **TFunction** | UE 的 `std::function` 等价物 |
| **TSharedFuture** | UE 的共享 Future（可多次 get） |
| **PURE_VIRTUAL** | UE 的纯虚函数宏 |
| **FMcpToolDefinitionBuilder** | Fluent API 构建 JSON Schema |
| **Mock Socket** | 结果存储槽，模拟 WebSocket 的 Send 行为 |
| **ListSubFieldBase** | 编译期策略类，控制字段 I/O 行为 |
| **Composite 模式** | 树形结构，节点可以包含子节点 |
| **Transaction** | UE 的撤销/重做事务系统 |
| **FAutomationHandler** | `TFunction<bool(...)>` 工具处理函数签名 |

---

*本文档基于对插件源码的只读分析整理；所有代码片段与行号取自真实源码。范围限于基础设施/框架层，不涉及具体 PCG 功能。*
