# MCP 配置指南

## 目标

配置 UE5.8 的 Model Context Protocol 支持，让 AI 工具可以连接 Unreal Editor。

## 当前项目状态

`pro.uproject` 中已启用：

- `ModelContextProtocol`
- `MCPClientToolset`

## 启动 MCP Server

1. 打开 UE5.8 Editor。
2. 打开 `Window > Developer Tools > Output Log`。
3. 在控制台输入：

```text
ModelContextProtocol.StartServer
```

如需指定端口：

```text
ModelContextProtocol.StartServer 8000
```

默认地址通常是：

```text
http://localhost:8000/mcp
```

停止服务：

```text
ModelContextProtocol.StopServer
```

## 客户端配置

CodeBuddy 配置文件位置：

```text
~/.codebuddy/.mcp.json
```

示例：

```json
{
  "mcpServers": {
    "unreal-engine": {
      "type": "http",
      "url": "http://localhost:8000/mcp",
      "description": "UE5.8 Model Context Protocol server"
    }
  }
}
```

修改配置后需要重启客户端。

## 验证方式

让客户端执行一个很小的编辑器操作，例如列出 UE 工具、截取视口截图，或读取当前关卡信息。

如果连接失败：

- 确认 UE Output Log 里 server 已启动。
- 确认端口和 URL 一致。
- 确认本机防火墙没有阻止 localhost。
- 确认 MCP 插件启用后已经重启 Editor。
