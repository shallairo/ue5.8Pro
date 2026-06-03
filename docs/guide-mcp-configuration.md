# MCP Configuration Guide

## Purpose

Configure Unreal Engine's Model Context Protocol support so an AI assistant can connect to the UE5.8 Editor.

## Current Project State

`pro.uproject` enables:

- `ModelContextProtocol`
- `MCPClientToolset`

## Start The MCP Server

1. Open the project in UE5.8 Editor.
2. Open `Window > Developer Tools > Output Log`.
3. Run:

```text
ModelContextProtocol.StartServer
```

To use a custom port:

```text
ModelContextProtocol.StartServer 8000
```

The default endpoint is usually:

```text
http://localhost:8000/mcp
```

To stop the server:

```text
ModelContextProtocol.StopServer
```

## Client Configuration

For CodeBuddy, create or update:

```text
~/.codebuddy/.mcp.json
```

Use:

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

Restart the AI client after changing this file.

## Verification

Ask the client to perform a small editor action, such as listing available UE tools or taking a viewport screenshot.

If the connection fails:

- Confirm the server is running in Output Log.
- Confirm the URL and port match the client configuration.
- Confirm firewall rules are not blocking localhost traffic.
- Restart the editor after enabling MCP plugins.
