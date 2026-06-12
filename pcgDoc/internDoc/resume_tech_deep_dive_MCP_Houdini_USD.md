# NextPCG Server 技术模块深度分析（自包含版 · 简历 / 面试准备）

> **使用场景**：离开公司后无源码访问，仅凭本文档即可复述实现细节、撰写/完善简历、应对面试追问。
> 因此本文**内嵌真实代码片段、数据格式样例、调用时序与底层原理推导**，力求"看文档=看过源码"。
> 覆盖 4 个模块：**① MCP 协议深度集成 ② Houdini 深度集成 ③ Houdini 会话管理复杂度 ④ USD 模块**。

---

## 目录
- [0. 项目全景](#0-项目全景)
- [模块一：MCP 协议深度集成](#模块一mcp-协议深度集成)
- [模块二：Houdini 深度集成](#模块二houdini-深度集成)
- [模块三：Houdini 会话管理复杂度](#模块三houdini-会话管理复杂度)
- [模块四：USD 模块](#模块四usd-模块)
- [附录 A：简历条目模板](#附录-a简历条目模板可直接改写)
- [附录 B：高频面试问答（带详解）](#附录-b高频面试问答带详解)
- [附录 C：术语速查表](#附录-c术语速查表)

---

## 0. 项目全景

**NextPCG Server**：把 **Houdini 程序化生成（PCG）能力**通过 **MCP 协议**暴露给 **AI Agent（Cursor/Claude）与 Unreal 编辑器**的 Flask（Python 3.10）服务端。

数据主链路：

```
AI Agent / UE Editor
   │  JSON-RPC 2.0  over  SSE / Streamable HTTP
   ▼
①  MCP 协议层 (Server/app/mcp/)
   │  工具调用 → PSON 任务描述(JSON)
   ▼
②  Houdini 集成层 (Server/app/handlers/server_houdini.py + PythonPackage/nextpcg/pyhapi)
   │  HDA 加载 / instantiate / cook，IO 管线(JSON / attr_raw / USD)
   ▼
③  会话管理层 (pyhapi/hsession.py)
   │  HARS 进程池 / thrift 命名管道 / 任务队列 / 会话重置
   ▼
④  USD 模块 (usd_converter.py + server_houdini USD Direct)
   │  USD ↔ JSON(attr_raw) 转换，USD Direct 原生 IO
   ▼
几何结果(曲线/网格/实例) → 回 UE 落地为 ISM/资产
```

**一句话定位（面试开场）**：
> "我做的是一套'AI 可调用的 Houdini 服务'：用 MCP 协议把 Houdini 的 HDA 封装成 AI/UE 能调用的工具，请求经协议层翻译成 HDA cook 任务，在常驻的 Houdini 引擎进程里执行，再把几何结果序列化回引擎。我主要负责协议层、Houdini 集成、会话管理与 IO 管线。"

---

# 模块一：MCP 协议深度集成

## 1.1 总体结构

```
Server/app/mcp/
├── server/mcp_sse_server.py   # 传输层：SSE + Streamable HTTP；JSON-RPC 派发；UE 反向桥接
├── registry/tool_registry.py  # 单例 MCPToolRegistry：扫描/生成schema/执行
├── tools/                     # 工具实现：@nextpcgmethod + Houdini/Python/UE 适配器
│   ├── houdini_tools.py       #   execute_houdini_pson_tool（HDA cook 入口）
│   ├── python_tools.py        #   Python Dson 插件执行
│   ├── ue_tools.py            #   UE 工具包装
│   ├── city_tools.py / wild_tools.py  # 复合工作流
│   └── tool_scanner.py        #   PSON/DSON 文件扫描
└── utils/                     # 超时/上下文/可观测性/路径/工作流模板
```

关键常量：
- `MCP_PROTOCOL_VERSION = "2024-11-05"`（同时另有 Streamable HTTP 走 2025-03-26 风格）
- `MAX_MCP_TOOL_NAME_LENGTH = 40`（工具名长度上限，见 1.4）

**接入 Flask 的启动序列**（`server_main.py`）：
```python
import_module('app.mcp.tools')   # 导入所有 tools/*.py → 触发 @nextpcgmethod 装饰器注册
register_mcp_tools()             # MCPToolRegistry.scan_and_register()：装饰器工具 + 扫描 PSON/DSON
register_mcp_sse_routes(app)     # 注册 /mcp/sse、/mcp/messages/、/mcp/http
```

## 1.2 传输层：SSE + JSON-RPC（含真实代码）

### 三条路由
| 路由 | 方法 | 作用 |
|---|---|---|
| `/mcp/sse` | GET | 建立 SSE 长连接，下行推送 |
| `/mcp/messages/?session_id=` | POST | JSON-RPC 上行入口 |
| `/mcp/http` | POST | Streamable HTTP（无状态，单次请求-响应） |

### SSE 为什么需要"endpoint 事件"
SSE 是**单向**的（服务端→客户端）。客户端要发请求，必须另起一个 POST 通道。所以握手时服务端先推一个 `endpoint` 事件，告诉客户端"把 JSON-RPC POST 到这个带 session_id 的地址"。

**SSE 端点真实实现**（`mcp_sse_server.py`）：
```python
@mcp_sse_blueprint.route('/mcp/sse', methods=['GET'])
def mcp_sse_endpoint():
    session_id = str(uuid.uuid4())
    q = _get_or_create_session(session_id)          # 每个 session 一个 queue.Queue
    messages_url = f"/mcp/messages/?session_id={session_id}"

    def generate():
        yield _format_sse('endpoint', messages_url)  # 首个事件：告知 POST 地址（MCP 规范）
        try:
            while True:
                try:
                    item = q.get(timeout=30)         # 阻塞取下行消息
                    if item is None:                 # 哨兵 → 关闭流
                        break
                    yield _format_sse(item['event'], item['data'])
                except queue.Empty:
                    yield _format_sse('ping', {'time': int(time.time())})  # 30s keepalive
        finally:
            _remove_session(session_id)

    return Response(stream_with_context(generate()), mimetype='text/event-stream',
                    headers={'Cache-Control': 'no-cache', 'X-Accel-Buffering': 'no',
                             'Connection': 'keep-alive', 'Access-Control-Allow-Origin': '*'})
```

SSE 帧格式（`_format_sse`）：
```
event: {type}\n
data: {json}\n
\n
```

### 消息入口与"同步回退"（重要兼容性设计）
有些 MCP 客户端读完 `endpoint` 事件后**不再监听 SSE 流**，导致没有活跃队列接收响应。所以 POST 入口做了回退：SSE 活跃 → 返回 `202`，后台线程处理，结果经 SSE 推回；SSE 不活跃 → **同步内联返回 JSON**。
```python
@mcp_sse_blueprint.route('/mcp/messages/', methods=['POST', 'OPTIONS'])
def mcp_messages_endpoint():
    session_id = request.args.get('session_id', '')
    body = request.get_json(force=True)
    if not _has_active_sse_session(session_id):
        response_payload = _handle_jsonrpc_sync(session_id, body)   # 同步回退
        return Response(json.dumps(response_payload), status=200, mimetype='application/json')
    threading.Thread(target=_handle_jsonrpc, args=(session_id, body), daemon=True).start()
    return '', 202   # 异步：结果走 SSE
```

### JSON-RPC 2.0 报文样例
请求（客户端 → 服务端，POST 到 messages_url）：
```json
{"jsonrpc":"2.0","id":7,"method":"tools/call",
 "params":{"name":"nextpcg_city_run_block_processing","arguments":{"block_curve_file":"[...]","parse_ground":true}}}
```
响应（服务端 → 客户端，经 SSE `event: message`）：
```json
{"jsonrpc":"2.0","id":7,
 "result":{"content":[{"type":"text","text":"{\"success\": true, \"count\": 765, ...}"}],"isError":false}}
```
关键点：MCP 的 `tools/call` 结果必须是 `content:[{type:"text", text:...}]`；工具内部返回的 dict 在 SSE 边界被 `json.dumps` 成字符串塞进 `text`。错误码：`-32601` 方法不存在、`-32603` 内部错误。

### 派发的方法集（`_dispatch_method`）
标准：`initialize` / `tools/list` / `tools/call` / `ping` / `notifications/*`
UE 扩展：`client/register`（UE 上报工具目录）/ `tools/call_response`（UE 回传结果）

## 1.3 服务端 → UE 的"反向 RPC"（最有亮点的设计）

MCP 本是"客户端调服务端"。但本项目里很多工具要**让服务端反过来命令 UE 编辑器**执行（如生成 Landscape、导入 ISM）。

实现要点（`call_ue_tool`）：
1. UE 通过 `client/register` 注册并保持一条 SSE 长连接 + 它能执行的工具列表。
2. 服务端选一个声明了该工具的 UE session，把 `tools/call` 经它的 SSE 队列推过去。
3. 在 `_pending_tool_calls[request_id] = {event: threading.Event(), holder: {}}` 注册一个等待项，调用线程 `event.wait(timeout=30)` 阻塞。
4. UE 执行完回 `tools/call_response`，handler 把结果写进 holder 并 `event.set()` 唤醒。
5. **最后一个 UE 断开** → 取消所有在途 Houdini 任务 + 唤醒所有 pending（避免线程永久阻塞）。

伪代码：
```python
def call_ue_tool(tool_name, arguments, timeout=30.0):
    sess = pick_ue_session_that_has(tool_name)        # 取第一个声明该工具的 UE session
    req_id = next_id()
    ev = threading.Event(); _pending_tool_calls[req_id] = {'event': ev, 'holder': {}}
    _send_to_session(sess, 'message', {'jsonrpc':'2.0','id':req_id,
        'method':'tools/call','params':{'name':tool_name,'arguments':arguments}})
    if not ev.wait(timeout): raise TimeoutError(...)
    return _pending_tool_calls.pop(req_id)['holder']['result']
```
**底层原理（面试点）**：这是用 `queue.Queue`（下行）+ `threading.Event`（请求-响应配对）在 WSGI 多线程模型下，把"异步 SSE 推送"伪装成"同步阻塞调用"的经典模式；`request_id` 做请求-响应关联（类似 RPC correlation id）。

## 1.4 工具名 64 字符限制：两级截断 + 别名（真实算法）

**背景**：Claude/Bedrock 限制工具名 ≤ 64 字符；MCP 客户端还会加前缀 `mcp__{server}__`（约 22 字符）。所以服务端把工具名压到 ≤ 40。

**真实算法**（`tool_registry.py`）：
```python
MAX_MCP_TOOL_NAME_LENGTH = 40
TOOL_NAME_ALIASES = {
    'sop_hrakyan_scatter_decal_instances_on_splines_1_3': 'sop_hrakyan_scatter_decal_instances_1_3',
}

def _truncate_tool_name(name, max_len=40):
    if name in TOOL_NAME_ALIASES:          # 1) 显式别名优先
        return TOOL_NAME_ALIASES[name]
    if len(name) <= max_len:               # 2) 够短直接用
        return name
    parts = name.split('_')                # 3) 去掉作者前缀（第一段）
    if len(parts) > 2:
        s = '_'.join(parts[1:])
        if len(s) <= max_len: return s
        s = '_'.join(parts[-2:])           #    退而求其次：留最后两段
        if len(s) <= max_len: return s
    # 4) 硬截断 + 4 位 hash 后缀保唯一
    suffix = f"_{abs(hash(name.encode())) % 10000:04d}"
    return name[:max_len-len(suffix)].rstrip('_') + suffix
```
注册期限制 40；`tools/list` 列举期再做一次 `_auto_shorten_tool_name`（上限 42，给 PSON/landscape 工具用静态别名表）；**调用期**用 `_dynamic_name_aliases_reverse` 把短名还原回真名。

## 1.5 工具来源（四类统一进注册表）

1. **`@nextpcgmethod(expose_as_mcp=True, mcp_timeout=…, mcp_category=…, mcp_description=…)`** 装饰的 Python 函数。装饰器在 import 时把函数登记进一个全局 `_MCP_TOOL_REGISTRY` 列表（位于 `PythonPackage/nextpcg/pypapi/dson.py`），`scan_and_register` 再 drain 进服务端单例。
2. **PSON 自动扫描**：`HoudiniProjects/*/hda/pson/*.pson` —— HDA 接口的 JSON 描述。扫描即把任意 HDA 变成 AI 可调用工具（零代码）。冲突时按 `mtime` 最新者胜。
3. **DSON 扫描**：`PythonProjects/*/dson/*.dson` → Python 插件工具。
4. **UE 工具**：UE 经 `client/register` 上报；`tools/list` 时只在"服务端注册表里没有同名"时才暴露 UE 工具（**server 端 Python 包装优先**，因为它修了 io_mode/路径等）。

**PSON → MCP schema**：从 PSON 的 `Params`/`Inputs` 生成 JSON Schema，工具登记为 `func=None, source='pson', pson_path, hda_name, io_mode`，执行时延迟路由到 `execute_houdini_pson_tool`。

## 1.6 工具调用的完整执行流

```
POST /mcp/messages/?session_id=…  (tools/call)
  → _handle_jsonrpc[_sync] → _dispatch_method → _handle_tools_call
  → set_current_mcp_session_id(session_id)          # threading.local 存当前 session
  → MCPToolRegistry.execute_tool(name, args)
       ├─ source==pson/dson → _execute_pson_tool
       │     ├─ houdini → execute_houdini_pson_tool  → server_houdini.create_session_task()
       │     └─ python  → execute_python_pson_tool   → server_python.create_session_task()
       └─ else → func(**arguments)                   # @nextpcgmethod 直接调
  → 规范化为 content:[{type:text}]
  → clear_current_mcp_session_id()
```
长任务（Houdini cook）用 generator 产出中间进度，经 `notifications/progress` 流式上报（阈值 30s 起报）。

## 1.7 难点 / 亮点 / 改进 / 面试点

**难点**：SSE 并发与长任务不阻塞 Flask；服务端→UE 反向 RPC；64 字符名治理；数百工具的工具面治理（隐藏集、去重）；错误信封一致性。

**亮点**：多传输适配（SSE / Streamable HTTP / 同步回退）；PSON 零代码工具暴露；AI↔服务端↔UE 三方编排；进度流 + 断连取消。

**改进**：把已实现但未接入的 `TimeoutManager`/`PerformanceMonitor`/`ErrorHandler` 接进热路径；多 UE 连接的会话选择/负载均衡（当前取第一个）；大输出截断/落盘；工具名长度策略集中文档化。

**面试知识点（详解）**：
- **SSE vs WebSocket vs 长轮询**：SSE 基于 HTTP、单向服务端推、文本帧、自动重连、代理穿透好；WebSocket 双向但握手/基建更重；长轮询开销大。本项目选 SSE 是为兼容主流 MCP 客户端。
- **JSON-RPC 2.0**：无状态请求/响应/通知，`id` 关联，标准 error 码。
- **WSGI 并发模型**：Flask 默认线程池，长任务必须丢后台线程，否则占满 worker；用 `queue.Queue`+`Event` 做跨线程协调。
- **correlation-id 模式**：反向 RPC 用 `request_id` 配对请求与异步响应。
- **协议演进兼容**：同时支持 2024-11-05(SSE) 与 2025-03-26(Streamable HTTP)。

---

# 模块二：Houdini 深度集成

## 2.1 总体结构

核心：`Server/app/handlers/server_houdini.py`（约 4400 行，单一 Houdini 编排器）+ `PythonPackage/nextpcg/pyhapi/`（HAPI 的 Python 封装）。

### pyhapi 分层（自底向上）
```
hapi.py     ctypes 绑定 libHAPIL（C 动态库）：cook / geo / parm / session 等 C 函数
hdata.py    HAPI 结构体的 Python 镜像（Session/NodeInfo/GeoInfo/PartInfo/CookOptions）
hsession.py HSession / HSessionManager / HSessionPool / @HSessionTask（会话生命周期，见模块三）
hnode.py    HNode / HInputNode，cook_async()、get_display_geos()
hasset.py   HAsset：load_asset_library_from_file(.hda) → instantiate() → HNode
hgeo.py     HGeoMesh / HGeoCurve / HGeoInstancer / HGeoHeightfield / HGeoVolume
hparm.py    参数封装；按钮触发异步 cook
compat/     version_detector + adapter：Houdini 19.5 ~ 21+ 的 API 形态差异
```
**FFI 原理（面试点）**：HAPI 是 C 接口；Python 用 ctypes 把 C 结构体（如 `HAPI_PartInfo`）镜像成 Python 类，按内存布局读写，调用 C 函数传指针/句柄（node_id、session 句柄）。

### 单任务流水线（`_session_task_houdini_impl`）
```
1. 建任务工作目录 temp/mcp_work/<hda>_<时间戳>/，独立日志 houdini_task.log
2. 写 houdini_task.pson（任务描述）
3. await session.restart_fut         # 与在途会话重置串行
4. geo_parent = HNode('Object/geo','NextPCGGeo')   # 几何容器
5. 冷会话预热：首个任务先 cook 一个 Sop/box
6. HAsset(session, hda路径).instantiate('HDANode', parent=geo_parent)
7. 接入输入节点（按 io_mode 选择，见 2.2）
8. 为每个 HDA 输出建输出节点
9. cook 顺序：输入节点 → SyncBarrier → HDA → 输出节点
10. 重新读 houdini_task.pson 拿输出文件引用
11. geo_parent.delete()             # 所有退出路径都执行（防污染）
```

### PSON 是什么
PSON = 项目自定义的 **JSON 任务描述**（不是二进制）。结构（真实字段）：
```json
{
  "system": {"user": "...", "source_engine": "ue", "use_usd_direct": false},
  "<hda键，如 yohozhou.cityBlockProcessing_for_UE5.hda>": {
    "Inputs": {"input1": {"Geo_0": ".../Geo_0_Curve1.json", "Attr_0": ".../Geo_0_Curve1.attr_raw"}},
    "Params": {"parseGround": 1, ...},
    "Outputs": {}
  },
  "io_mode": 2,
  "filename_to_md5": {...}
}
```
IO 用的 `kivlin_nextpcg_io_input.hda` / `kivlin_nextpcg_io_output.hda` 内嵌一个 `python_pythonic` SOP，运行时读 `pson_filename`/`work_path` 参数，按 `Inputs`/`Outputs` 描述把磁盘文件物化成 Houdini 几何 / 把几何写回磁盘。

## 2.2 IO 管线（四种 io_mode）

```python
class IOMode(Enum):
    server_text   = 0   # 服务端用 HAPI 直接建几何（HInputNode/HGeo*），get_display_geos 取回
    houdini_text  = 1   # 几何 JSON 内联，Houdini 端 Python SOP 解析
    houdini_binary= 2   # JSON + .attr_raw 二进制属性块（JSON 存 offset/length）★主力
    hdk_binary    = 3   # 有 C++ DSO(SOP_NextPCGInput/Output) 时用 HDK 节点
```

### `.attr_raw` 二进制属性格式（核心亮点，能讲很深）
每个属性**独立 zlib 压缩**写进一个二进制文件；JSON 里存每个属性的字节 offset/length。真实 JSON 元数据样例（来自一个 building footprint 文件）：
```json
"point": {
  "Index":  {"type":2,"offset":0,  "data_type":"i32",  "length":14,"raw_length":16},
  "P":      {"type":2,"offset":102,"data_type":"f32x3","length":41,"raw_length":48},
  "orient": {"type":2,"offset":182,"data_type":"f32x4","length":16,"raw_length":64}
},
"prim": {
  "bdf":     {"type":2,"offset":388, "data_type":"s","length":17,  "raw_length":9},
  "Dct_bdf": {"type":2,"offset":405, "data_type":"s","length":1133,"raw_length":5506}
},
"attribs_filename": "Geo_0_Curve1.attr_raw"
```
字段含义：`offset`=在 .attr_raw 里的字节起点；`length`=压缩后字节数；`raw_length`=解压后字节数；`data_type`=`f32`/`f32x3`(vec3)/`f32x4`(quat)/`i32`/`s`(string)；`type`=属主类（1=point,2=prim,3=detail）。

**读取算法**（Houdini 端 `load_attribs_from_file`）：
```python
f.seek(offset); raw = zlib.decompress(f.read(length))
if data_type 数值:  arr = np.frombuffer(raw, dtype).reshape(-1, channels)
else (string):      arr = json.loads(raw.decode())   # 形如 [["a"],["b"]]
```
**字符串为何用嵌套数组 `[["a"],["b"]]`**：C++ 端按 `StringBuffer->AsArray()[i]->AsArray()[0]->AsString()` 读取，外层是点索引、内层是单元素数组。

**为什么这样设计（面试点）**：① 按属性分块 → 可按需随机读取单个属性，不必整体反序列化；② zlib 压缩省带宽（几何属性高度可压）；③ 同一布局跨语言（C++ HDK / Python）共享；④ JSON 只存元数据，人类可读、易调试。

### 坐标系/单位转换（UE ↔ Houdini）
UE 是 **Z-up，厘米**；Houdini 是 **Y-up，米**。转换（C++ 导入端自动做，**禁止手动再转**）：
```
位置  P(Houdini m) [x,y,z]  → UE(cm) [x*100, z*100, y*100]
四元数 orient [x,y,z,w]      → UE     [x, z, y, -w]
缩放  scale [x,y,z]          → UE     [x, z, y]
```
**推导（面试点）**：Y-up→Z-up 是把 Y、Z 轴对调；左右手系差异体现在四元数 w 取负；米→厘米是 ×100。曾有 bug：手动又转一次导致实例 1/100 缩放跑到世界原点。

## 2.3 四个关键机制（深挖，面试高频）

### ① GroupFixer —— prim group 注入
**问题**：IO 输入 HDA 建曲线只带点属性 `Index`，**不建 Houdini prim group**；但下游城市 HDA 内部有 `hrakyan_check_prims_not_in_group` 校验，要求每个 prim 属于某命名 group，否则报 "No geometry generated!"。
**方案**：对 Curve 输入插一个 `Sop/python` 节点，按每个 prim 首点的 `Index` 建 `group{Index}`。**真实注入脚本**：
```python
import hou
node = hou.pwd(); geo = node.geometry()
idx_attr = geo.findPointAttrib('Index')
if idx_attr is not None:
    for prim in geo.prims():
        pts = prim.points()
        if pts:
            idx = pts[0].attribValue('Index')
            if idx >= 0:
                gname = 'group{}'.format(idx)
                grp = geo.findPrimGroup(gname) or geo.createPrimGroup(gname)
                grp.add(prim)
```
**血泪教训（真实修过的 bug，面试讲这个）**：曾有 `_SKIP_GROUP_FIXER_HDAS = ['cityBlockProcessing','cityBuildingProcessing']` 把 block/building 排除出 GroupFixer。结果它们的 **Instance 输出分支（block 的 ground/sidewalk/plaza、building 的楼体 module）静默产 0 几何**，只有校验宽松的 footprint 曲线分支幸存——这掩盖了问题（"能 cook、有输出，但没楼"）。根因：这俩 HDA 内部同样有 `check_prims_not_in_group` 校验，**依赖 prim group**；排除清单是把另一个 bug（SyncBarrier 假阴性）误判成了 group 的锅。把排除清单清空后，block 恢复 **719 ground + 44 footprint + 2 plaza**，building 恢复 **23 个多层楼体 module** 实例。
> 教训关键词：**"校验依赖前置处理"**、**"用同输入对照实验隔离变量"**、**"Curve 分支幸存掩盖了 Instance 分支失败"**。

### ② SyncBarrier —— cook 同步屏障 + 0 点检测（真实代码）
**问题 A**：`cook_async()` 在几何完全 materialize 前就返回 success，HDA 立刻读到空输入 → "No geometry generated"。
**问题 B**：对**锁定的 HDA 外层节点**调 `HAPI_GetDisplayGeoInfo` **恒返回 partCount=0**，即使内部几何已生成（显示几何 API 不暴露封装 HDA 内部几何）。
**方案**：barrier 不查锁定的 io_input 外层节点，而查它后面那个**非锁定**节点（GroupFixer 的 Python SOP 或 Null 直通 SOP）的 `get_display_geos()`；并把它从"探针"升级为"门"——全 0 就中止。真实代码：
```python
_barrier_total_pts = 0; _barrier_per_node = []
for node in nextpcg_io_input_node_list:           # 注意：list 里放的是 GroupFixer/Null，不是锁定 HDA
    try:
        geos = node.get_display_geos()             # 强制 HAPI 同步：要返回 geo/part info 必须几何就绪
        pts = sum(g.point_count for g in geos) if geos else 0
        _barrier_total_pts += pts; _barrier_per_node.append(pts)
    except Exception:
        _barrier_per_node.append(-1)
if _barrier_node_count > 0 and _barrier_total_pts == 0 and not json_data.get('allow_empty_input'):
    raise RuntimeError(f"[SyncBarrier] All input nodes materialized 0 points {_barrier_per_node}. Aborting.")
await asset_node.cook_async(...)   # 屏障通过后才 cook 主 HDA
```
**双重价值**：既做了"cook 完成"同步（`get_display_geos` 会阻塞到几何就绪），又把"输入为空"的失败**提前几十秒暴露**（不浪费一次注定失败的 cook）。

### ③ 输出脚本注入
`_inject_python_script_to_hda_node`：在输出 IO HDA 里找子节点 `python1`/`python_pythonic`/`python_origin`，把其 `python` 参数覆盖成仓库最新脚本（含 Houdini 21 兼容修复）。坑：
- HAPI 设字符串参数会**反转义反斜杠**，需先把脚本里的 `\` 双写成 `\\`。
- `save_hip` 会还原锁定 HDA 的默认参数，所以 **cook 前必须重新注入**。
- 某些 HDA 用 `skip_output_inject`：先 cook HDA，cook 后再注入并**显式 cook 子 python 节点**（否则 Houdini 认为父节点已 cooked 会跳过）。

### ④ point_count=0 JSON bug
UE 的 `ExportInstancedStaticMesh` 把 `"point_count": 0` 写进 JSON，但真实 P 数据在 `.attr_raw` 里。Houdini 端读到 0 就跳过建点 → 输入空。修复：cook 前扫描输入 JSON，从 attr_raw 解出 P 块真实点数，回填 `point_count` 和 `part_info.pointCount`。

## 2.4 难点 / 亮点 / 改进 / 面试点

**难点**：锁定 HDA 显示几何 API 的限制（驱动了 GroupFixer/Null/SyncBarrier）；async cook 的竞态；attr_raw 二进制契约与 UE JSON 的 quirk；坐标系/单位换算；Houdini 21 Python API 变化；不可中断的 cook。

**亮点**：把任意 HDA 抽象成"几何→参数→几何"纯函数；JSON/二进制/USD 三套 IO；每任务独立 log+PSON 快照可离线复现；GroupFixer/SyncBarrier 都是从 cook log 反推 HAPI 行为得出的工程性修复。

**改进**：SyncBarrier 按输入类型设阈值+重试退避；统一 io_mode 文档与枚举命名；锁定 HDA 查询统一走 `get_geo_info`；H21 注入用 `compat.has_feature()` 门控。

**面试知识点**：FFI/ctypes；HAPI 的 node/cook/geo/part/attribute(point/vertex/prim/detail) 四级属性模型；async cook + 状态轮询；zlib + numpy.frombuffer/reshape；左右手系与四元数；封装节点的内部几何不可见；竞态"返回≠完成"的屏障同步。

---

# 模块三：Houdini 会话管理复杂度

> 这是最能体现"系统工程/稳定性"能力的模块。

## 3.1 架构：进程外 HARS + thrift 命名管道会话池

生产环境**不用** in-process 会话（崩溃会拖垮 Flask），而用**进程外 HARS（Houdini Engine 服务进程）+ thrift 命名管道**的**会话池**，每个会话挂一个独立 HARS 进程。

```
HSessionManager
  └── HSessionPool(session_count=N)
        ├── HSession #0  ── thrift named pipe "pipe_name0" ── HARS 进程 #0
        ├── HSession #1  ── thrift named pipe "pipe_name1" ── HARS 进程 #1
        └── ...
        └── 后台线程跑 asyncio event loop，每个会话一个 worker 协程
```

会话池创建（真实代码）：
```python
def create_thrift_pipe_session(self, rootpath, pipe_name_prefix, auto_close=True, timeout=10000.0):
    valid = 0
    for i in range(self._session_count):
        s = HSession()
        if s.create_thrift_pipe_session(rootpath, pipe_name_prefix + str(i), auto_close, timeout):
            valid += 1; self.sessions.append(s)
    self._session_count = valid
    return valid >= 1
```

`initial_session()` 启动序列（每一步都有原因）：
```
1. 从环境剥离 HOUDINI_DSO_PATH    # 防止 HARS 扫描 Server cwd 加载错误 DSO
2. 把选定 Houdini 的 bin/dsolib 前置进 PATH   # 多版本隔离
3. load_library(...)              # 绑定 HAPI（只绑一次，全局 ph）
4. get_or_create_session_pool(session_count=...)
5. run_task_consumer_on_background()   # 后台线程 + asyncio worker
```
服务启动时先 `kill_orphan_hars_processes()` 清残留，再 `initial_session()`。

## 3.2 任务队列 / 每会话一个 worker（真实代码）

```python
async def __worker_loop(self, i):                 # 每个 HSession 一个 worker
    while True:
        try:
            avail_session = self.sessions[i]
            fut, task_to_proceed, *args = await self.task_queue.get()   # 取任务
            await asyncio.wait_for(task_to_proceed(avail_session, *args), self._max_task_time)
        except asyncio.CancelledError:
            break
        except BaseException as e:
            task_error = e                          # 任务异常不杀 worker
        finally:
            if got_obj:
                if not fut.done():
                    fut.set_exception(task_error) if task_error else fut.set_result(True)
                self.task_queue.task_done()
```
- **生产者**：`create_session_task` → `enqueue_task(future, task, *args)`。
- **Flask 侧等待**：轮询 future（每 0.1s），总超时 `NEXTPCG_HOUDINI_COOK_TIMEOUT_SEC`（默认 **1800s**）。
- **取消**：`cancel_all_houdini_tasks` 设 Event + 清空队列 + 线程安全 `set_result(None)`。
- **在途任务表**：`_active_tasks_lock` 保护 `work_path → Future`。

## 3.3 任务后会话重置（关键取舍，真实代码）

```python
def HSessionTask(task):                 # 装饰器：每个 Houdini 任务都包一层
    async def wrapper(*args, **kwargs):
        try:
            await task(*args, **kwargs)
            args[0].restart_session()    # 成功后重置会话
        except Exception:
            args[0].restart_session()    # 失败也重置（避免会话进入无效态）
    return wrapper
```
`restart_session_async` 内部 = **`HAPI.cleanup()` + 重新 `__initialize_session()`**，**不杀 HARS 进程**（杀进程重启的代码被注释为 "would fail"）。即：部分释放内存、**复用同一 HARS PID**。任务开始前 `await session.restart_fut` 保证与在途重置串行。

> **取舍（面试点）**：杀进程重启最干净但慢且易失败；只 `HAPI.cleanup` 快但残留部分状态。项目选了后者 + 节点删除 + 预热来折中。

## 3.4 三个高频踩坑（"血泪"，面试好讲）

1. **`NextPCGGeo` 节点污染（P0-B04）**：HARS 长寿命 + 反复建 `Object/geo` 节点 → 旧 cook cache 残留、`NextPCGGeo*` 累积导致后续任务读到脏几何。**修复**：每条退出路径（成功/异常/early-return）都 `geo_parent_node.delete()`。
2. **冷会话首 cook 出 0（warm-up）**：新会话第一次 cook 可能返回空，故首任务先 cook 一个 `Sop/box` 预热；预热失败则不标记 warmed。
3. **无法中断 HAPI cook**：HAPI 没有协作式取消。Flask 侧超时/客户端断开只能让请求**放弃等待**，**HARS 仍把 cook 跑完**。配套 orphan 进程清理兜底。

## 3.5 版本兼容
- `compat/version_detector.py`：最低 19.5，已测最高 21.5，查 `HAPI_GetEnvInt` 版本字段。
- thrift 会话创建签名：Houdini 20+ 需 `HAPI_SessionInfo`，19.5 是旧签名。
- Houdini 21 Python API：`type(v) is tuple` 失效 → 改 list 强转（在输出脚本里处理）。

## 3.6 难点 / 亮点 / 改进 / 面试点

**难点**：进程长寿命的状态泄漏；不可中断 cook；冷启动 0 输出；多版本 API 差异；跨进程几何传输。

**亮点**：进程外会话池隔离崩溃；生命周期闭环（启动清残留→预热→串行化→任务后 cleanup→节点删除）；PATH/DSO 精细隔离；每会话独立 worker 实现并行。

**改进**：安全的 HARS 进程级回收（每 N 任务或内存阈值真重启）；任务对 work_path 的会话亲和；协作式取消（进程 kill 实现真中断）；结构化指标（屏障点数/预热失败率/每 HDA 超时率）。

**面试知识点**：
- **进程池 vs 线程池 vs 协程**：Houdini 用进程隔离的原因——C 库非线程安全、崩溃隔离、绕开 GIL。
- **IPC**：命名管道、thrift RPC，跨进程传几何。
- **asyncio + 线程桥接**：后台线程跑 event loop，`run_coroutine_threadsafe`，future 跨线程 `set_result`，轮询 vs 事件。
- **资源生命周期**：句柄泄漏、cache 失效、幂等清理、`try/finally` 全路径释放。
- **超时 vs 取消**：超时=放弃等待，取消=停止执行；协作式 vs 抢占式。
- **冷启动**：预热、连接池预填。

---

# 模块四：USD 模块

> **诚实定位（面试加分）**：USD 在本项目是 **"转换为主、原生 IO 为辅(实验性)"**。主路径：`USD → JSON(+attr_raw) → Houdini → JSON → 可选 JSON→USD`。新的 **USD Direct 模式**在输入含 USD 时绕过 JSON，但若干计划中的 helper 已实现却**未接入主链路**（死代码），文中如实标注。

## 4.1 总体结构

| 层 | 路径 | 职责 |
|---|---|---|
| 转换库 | `PythonPackage/nextpcg/pypapi/usd_converter.py` | `usd_to_pson_json` / `pson_json_to_usd`，网格/材质/USDZ 提取 |
| 转换门面 | `pypapi/format_converter.py` | `is_usd_file` / `check_usd_support` / `usd_to_json` / `json_to_usd` |
| Houdini 编排 | `server_houdini.py` | USD 自动检测、File SOP 输入、HAPI USD 写出、任务后转换 |
| 上传钩子 | `server_main.py`（`/UploadFile/`） | 上传即 USD→JSON |
| 依赖 | `requirements.txt` | `usd-core>=23.11`（运行时**可选**） |

`pxr` 在 `server_main.py` 顶部**提前 import**，规避 Windows DLL 加载顺序问题。

## 4.2 USD Direct 模式（自动检测）
触发：`system.use_usd_direct=True` 或任一 HDA 输入 `Geo_X` 以 `.usd/.usda/.usdc/.usdz` 结尾（`_has_usd_inputs` 扫描）：
```python
use_usd_direct = system_data.get('use_usd_direct', False) or _has_usd_inputs(json_data)
```
行为：
1. **跳过** USD→JSON 解析，USD 直接写进 PSON。
2. **输入**用原生 **File SOP**（保留 packed USD prim，供带 `unpackusd` 的 HDA 用；用 Python SOP 会拍平成多边形丢层级)。
3. **输出**用 Null SOP 直通 + cook 后 HAPI `get_display_geos` + `_write_hgeos_to_usd` 写 USD。
4. **跳过**任务后的 JSON→USD 转换。

## 4.3 转换管线
- **USD → JSON**（`usd_to_pson_json`）：开 stage（USDZ 可解压）→ `traverse_stage` 读 `upAxis`/`metersPerUnit`、网格/相机/灯光/材质 → `extract_mesh_data`（世界变换烘焙、UE Z-up→Houdini Y-up、三角化、材质绑定、注入 `Index`/`lod`/`unreal_material` 等 HDA 兼容属性）→ 多网格合并 → io_mode=2 写 attr_raw / io_mode=1 内联。
- **JSON → USD**（`pson_json_to_usd`）：按 `target_engine` 设 stage 元数据（UE: Z-up + 0.01 mpu；Unity: Y-up + 1.0 mpu）→ 写 `/Root` Xform → 材质/网格 → 可选 USDZ 打包（`UsdUtils.CreateNewUsdzPackage`）。

坐标系总表：

| 方向 | UE | Unity |
|---|---|---|
| USD→Houdini(输入) | Z-up→Y-up，按 mpu 缩放 | 不翻轴 |
| Houdini→USD(输出) | Y-up/m → Z-up/cm | Y-up/m，翻 Z |

## 4.4 难点 / 亮点 / 改进 / 面试点

**难点**：多套实现并存（HAPI 路径 / HDA 内嵌脚本 / 未接入生成器）；死代码与开关矛盾（`nextpcg_input.py` 声明 USD Direct disabled，但服务端检测到 USD 又自动启用）；round-trip 保真（File SOP 保层级，pxr 拍平丢层级，JSON 路径合并多网格丢分离）；可选 `usd-core` 优雅降级；潜在 bug（USD Direct 输出每个 output index 都用 `asset_node.get_display_geos()`，多输出 HDA 可能拿错几何）。

**亮点**：功能完整的转换库（材质、`GeomSubset` 面级材质、USDZ 纹理提取、attr_raw 生成）；File SOP 保 packed USD；`_write_hgeos_to_usd` 用 prim `path` 属性重建 USD 层级；上传头透传 `source_engine`/`io_mode`；PyInstaller `hook-pxr.py` 打包 `pxr.*`。

**改进**：接入或删除 `_resolve_pson_usd_inputs`；统一 USD Direct 输出写法；修按 output port 取几何的 bug；上传转换时自动设 `use_usd=true`；JSON 路径保留层级；扩展 `_write_hgeos_to_usd` 支持 instance/curve/volume。

**面试知识点**：
- **USD 基础**：Stage / Prim / Layer / Composition(Arcs) / `UsdGeomMesh` / `upAxis` / `metersPerUnit` / USDZ(zip 容器)。
- **场景图与变换**：世界变换烘焙、Xform 层级、坐标系/单位换算。
- **材质**：`UsdShade`、GeomSubset 面级材质绑定。
- **可选依赖与降级**：运行时 feature detection、import 失败优雅处理、PyInstaller 打包动态库、Windows DLL 加载顺序。
- **技术判断**：为什么不全量上 USD Direct——保真/复杂度/收益权衡（体现工程取舍）。

---

# 附录 A：简历条目模板（可直接改写）

> STAR 式：动词 + 难点 + 量化/结果。按目标岗位精简。

- **MCP 协议服务端**：设计实现基于 **JSON-RPC 2.0** 的 MCP 服务端，支持 **SSE / Streamable HTTP 双传输 + 同步回退**，并实现 **AI↔服务端↔Unreal 三方双向 RPC**（用 `queue.Queue`+`threading.Event`+correlation-id 在 WSGI 线程模型下把异步 SSE 推送封装成同步调用）；实现 PSON/DSON **零代码工具自动注册**（数百工具）与 64 字符工具名的**两级截断+别名+反向映射**保证多客户端兼容。
- **Houdini Engine 深度集成**：基于 **HAPI(ctypes)** 封装 session/node/asset/geo 多层 API，把任意 **HDA 抽象为"几何→参数→几何"纯函数**；设计 **JSON / 二进制(按属性 zlib 分块的 attr_raw) / USD** 三套 IO 协议与 **UE(Z-up,cm)↔Houdini(Y-up,m)** 坐标/四元数转换。
- **Houdini 会话管理**：实现**进程外 HARS + thrift 命名管道会话池**（每会话独立 asyncio worker），解决进程隔离、冷会话预热、任务后 `HAPI.cleanup` 重置、`NextPCGGeo` 节点污染清理与 Houdini 19.5–21 多版本兼容；任务超时 1800s + orphan 进程清理兜底。
- **疑难 Bug 攻坚**：定位并修复"**锁定 HDA 的 `HAPI_GetDisplayGeoInfo` 恒返回 0 点**"导致的 cook 假阴性（**SyncBarrier** 改查非锁定节点并升级为门控）；以及"**prim group 前置校验缺失致 instance 输出静默归零**"（**GroupFixer**），恢复 block 719 实例 / building 多层楼体模块生成。
- **USD 管线**：实现 **USD↔PSON(JSON/attr_raw) 双向转换**（材质 / GeomSubset 面级材质 / USDZ）与 **USD Direct 原生 IO**（File SOP 保 packed USD 层级），支持 **UE/Unity 双引擎坐标系**与可选 `usd-core` 优雅降级。

---

# 附录 B：高频面试问答（带详解）

**Q1：为什么用 SSE 而不是 WebSocket？**
A：① MCP 客户端生态（Cursor/Claude Desktop）以 SSE+POST 为主；② SSE 基于 HTTP、单向服务端推够用（客户端另起 POST 发请求）、代理穿透简单、自带断线重连；③ WebSocket 双向但握手/基建更重。我们还额外提供 Streamable HTTP 给无状态新 SDK，并做了"客户端不监听流时同步内联返回"的回退。

**Q2：服务端怎么"反向"调用 UE 编辑器？**
A：UE 通过 `client/register` 上报工具并保持 SSE 长连接。服务端把 `tools/call` 经该 session 的 SSE 队列推过去，在 `_pending_tool_calls[request_id]` 注册一个 `threading.Event`，调用线程 `wait(30s)` 阻塞；UE 处理完回 `tools/call_response`，handler 写结果并 `event.set()` 唤醒。`request_id` 做请求-响应配对（correlation-id）。最后一个 UE 断开会取消所有在途任务并唤醒等待者，防线程永久阻塞。

**Q3：Houdini cook 为什么要 SyncBarrier？它解决了什么 HAPI 限制？**
A：两件事。① `cook_async` 返回 success ≠ 几何就绪，HDA 立刻读会读到空；② 对**锁定 HDA 外层节点**查 `HAPI_GetDisplayGeoInfo` 恒返回 partCount=0（封装节点内部几何不通过显示几何 API 暴露）。所以 barrier 改查输入链上**非锁定**节点（GroupFixer/Null SOP）的 `get_display_geos`，既强制同步（要返回 geo/part info 必须几何就绪），又把"全 0 输入"提前几十秒报错中止，避免注定失败的 cook。

**Q4：attr_raw 为什么不用一个大 blob？嵌套数组字符串是为什么？**
A：按属性独立 zlib 压缩 + JSON 存 offset/length/raw_length，好处：可按需随机读单个属性、跨语言(C++/Python)共享同一布局、避免整体反序列化、JSON 元数据可读易调试。字符串用 `[["a"],["b"]]` 是适配 C++ 端 `AsArray()[i]->AsArray()[0]->AsString()` 的二维读法（外层点索引、内层单元素）。

**Q5：会话管理最难的点？为什么不每次杀进程重启？**
A：Houdini 进程长寿命带来状态泄漏（节点/cache 累积）和不可中断 cook。杀进程最干净但慢且"would fail"，所以选 `HAPI.cleanup` 重置 + 每路径 `geo_parent.delete()` + 冷会话预热折中。代价是只能部分释放、复用同一 PID；配 orphan 清理兜底。超时只能放弃等待，cook 仍会跑完（HAPI 无协作式取消）。

**Q6：讲一个你修过的最有意思的 bug。**
A：block/building HDA 突然只出 footprint 曲线、不出地面和楼体实例。表象像"缺资产/楼太矮"。我用**同一份 46 条曲线对照实验**发现 footprint 数（44）和历史完全一致、只有 instance 输出归零，排除了输入问题；再看 cook 日志逐节点，发现这俩 HDA 被错误地排除出了 GroupFixer，而它们内部有 `check_prims_not_in_group` 校验，**依赖 prim group**——没 group 时 instance 分支静默产 0 几何，只有校验宽松的 Curve 分支幸存掩盖了问题。清空排除清单后 719 地面实例 + 多层楼体 module 全部恢复。教训：**校验依赖前置处理**、**用对照实验隔离变量**、**警惕"部分成功"掩盖失败**。

**Q7：UE↔Houdini 坐标转换怎么做？为什么四元数 w 要取负？**
A：UE Z-up/cm，Houdini Y-up/m。位置 `[x,y,z]m → [x*100,z*100,y*100]cm`（Y、Z 对调 + ×100）；四元数 `[x,y,z,w] → [x,z,y,-w]`。w 取负是因为 Y/Z 轴交换改变了手性（左右手系），旋转方向要反转。坑：C++ 导入端已自动转，若手动再转会导致 1/100 缩放跑到原点。

**Q8：PSON 是什么？和 io_mode 的关系？**
A：PSON 是项目自定义的 JSON 任务描述（system/Inputs/Params/Outputs/`*.hda` 键/io_mode）。io_mode 决定几何怎么传：0=服务端 HAPI 直接建；1=JSON 内联；2=JSON+attr_raw 二进制（主力）；3=HDK C++ DSO。IO 用专门的 io_input/io_output HDA，内嵌 python SOP 运行时按 PSON 读写磁盘几何。

---

# 附录 C：术语速查表

| 术语 | 含义 |
|---|---|
| **MCP** | Model Context Protocol，AI Agent 与工具/数据源交互的协议 |
| **JSON-RPC 2.0** | 轻量远程调用协议，请求/响应/通知，`id` 关联，标准 error 码 |
| **SSE** | Server-Sent Events，基于 HTTP 的单向服务端推送，文本帧 `event:/data:` |
| **HAPI** | Houdini Engine API，C 接口，控制 Houdini node/cook/geo |
| **HARS** | Houdini Engine Server，承载会话的独立进程 |
| **HDA** | Houdini Digital Asset，封装好的程序化节点资产（`.hda`） |
| **PSON** | 本项目自定义的 JSON 任务/HDA 接口描述 |
| **attr_raw** | 按属性 zlib 分块的二进制几何属性文件，JSON 存 offset/length |
| **io_mode** | 几何传输模式：0 server_text / 1 houdini_text / 2 houdini_binary / 3 hdk_binary |
| **GroupFixer** | 给输入几何按 Index 建 prim group 的 Python SOP（满足 HDA 校验） |
| **SyncBarrier** | cook 前查非锁定节点几何，做同步 + 0 点门控 |
| **ISM** | Instanced Static Mesh，UE 的实例化静态网格 |
| **USD** | Universal Scene Description，皮克斯场景描述格式（Stage/Prim/Layer） |
| **USDZ** | USD 的 zip 容器格式（含纹理） |
| **packed USD** | Houdini 里以 packed primitive 形式保留 USD 结构，不拍平 |
| **upAxis / metersPerUnit** | USD stage 元数据：上轴方向 / 单位米数（UE=0.01） |
| **ctypes/FFI** | Python 调 C 动态库的外部函数接口 |

---

*本文档基于对仓库的只读分析整理；所有代码片段与数据样例均取自真实源码与实跑日志。USD 模块存在已实现但未接入主链路的代码，已如实标注，便于面试区分"已落地"与"在建"。*
