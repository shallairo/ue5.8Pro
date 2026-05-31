# UE5.8 MCP Configuration Guide (Corrected)

## Overview
This guide provides step-by-step instructions for configuring the Model Context Protocol (MCP) server in UE5.8, enabling AI assistants (like CodeBuddy Code) to control the Unreal Editor.

## Current Status
Based on `pro.uproject`, the following MCP plugins are already enabled:
- `ModelContextProtocol` - Core MCP protocol support (FriendlyName: "Unreal MCP")
- `MCPClientToolset` - MCP client tools

## Configuration Steps

### Step 1: Enable MCP Plugin in UE5 Editor

1. **Open UE5 Editor** with your project
2. **Navigate to Plugins**:
   - `Edit > Plugins`
   - Search for "Unreal MCP" or "ModelContextProtocol"
   - Ensure the plugin is enabled (should already be enabled based on .uproject)
   - Restart editor if prompted

### Step 2: Start MCP Server via Console Commands

**Important**: The ModelContextProtocol plugin uses console commands, not Project Settings.

1. **Open Output Log**:
   - `Window > Developer Tools > Output Log`

2. **Start MCP Server**:
   - In the Output Log console, type:
     ```
     ModelContextProtocol.StartServer
     ```
   - Or specify a custom port:
     ```
     ModelContextProtocol.StartServer 8000
     ```

3. **Verify Server Status**:
   - Check Output Log for messages like:
     ```
     LogMcpNativeTransport: MCP server started on http://localhost:8000/mcp
     ```
   - Default server address: `http://localhost:8000/mcp`

4. **Stop Server** (if needed):
   - Use command: `ModelContextProtocol.StopServer`

### Step 3: Configure AI Client Connection

For **CodeBuddy Code**, the configuration file should be created at:
- `~/.codebuddy/.mcp.json` (user scope, recommended)
- Or `<project root>/.mcp.json` (project scope)

**Correct Configuration**:
```json
{
  "mcpServers": {
    "unreal-engine": {
      "type": "http",
      "url": "http://localhost:8000/mcp",
      "description": "UE5.8 Model Context Protocol server for AI assistant control"
    }
  }
}
```

**Important Notes**:
1. The configuration file must have `"mcpServers"` wrapper object
2. Use `"type": "http"` for HTTP-based MCP servers
3. The URL should match the UE5 MCP server address (default: `http://localhost:8000/mcp`)
4. Restart CodeBuddy Code after creating/updating the configuration file
5. **Correct file location**: `~/.codebuddy/.mcp.json` (not `~/.codebuddy.json` or `~/.codebuddy/mcp.json`)

### Step 4: Test MCP Connection

1. **Restart CodeBuddy Code** (if already running)
2. **Test Basic Operations**:
   - Ask CodeBuddy to perform simple UE5 operations
   - Example commands:
     - "List all available UE5 MCP tools"
     - "Take a screenshot of the current viewport"
     - "Create a new cube actor at location (0, 0, 100)"

3. **Verify in UE5**:
   - Check Output Log for MCP-related messages
   - Look for tool execution logs

## Available MCP Tools

Once configured, you'll have access to **22 core MCP tools** organized by category:

### Core Tools
- `manage_asset` - Asset management (import, create, modify)
- `manage_blueprint` - Blueprint editing and components
- `control_actor` - Actor spawning, deletion, transformation
- `control_editor` - Editor sessions, camera, screenshots
- `manage_level` - Level loading, saving, streaming
- `system_control` - System commands, testing, logging
- `inspect` - Object introspection and properties
- `manage_tools` - Dynamic tool management

### World Building Tools
- `build_environment` - Landscapes, foliage, procedural terrain
- `manage_level_structure` - Levels, sub-levels, world partition
- `manage_geometry` - Geometry scripting and procedural meshes

### Game Systems Tools
- `animation_physics` - Animation, physics, Control Rig
- `manage_effect` - Niagara VFX, particles, debug shapes
- `manage_gas` - Gameplay Ability System (GAS)
- `manage_character` - Character creation and movement
- `manage_combat` - Weapons, projectiles, damage systems
- `manage_ai` - AI controllers, behavior trees, EQS
- `manage_inventory` - Items, equipment, loot tables
- `manage_interaction` - Interactables, destructibles, triggers

### Utility Tools
- `manage_audio` - Audio assets, components, MetaSound
- `manage_sequence` - Sequencer, cinematics, animations
- `manage_networking` - Network replication, sessions, input

## Troubleshooting

### Issue 1: MCP Server Not Starting
**Symptoms**: No server messages in Output Log
**Solutions**:
1. Ensure ModelContextProtocol plugin is enabled
2. Try different port: `ModelContextProtocol.StartServer 8001`
3. Check for port conflicts (8000 might be in use)
4. Restart UE5 Editor

### Issue 2: CodeBuddy Cannot Connect
**Symptoms**: "Connection refused" or timeout errors
**Solutions**:
1. Verify MCP server is running in UE5 Editor
2. Check URL is correct: `http://localhost:8000/mcp`
3. Test with curl: `curl http://localhost:8000/mcp`
4. Check firewall settings

### Issue 3: Tools Not Available
**Symptoms**: CodeBuddy says no tools available
**Solutions**:
1. Restart both UE5 Editor and CodeBuddy
2. Verify MCP server is running
3. Check MCP client configuration is correct
4. Look for errors in UE5 Output Log

## Integration with GPU Optimization Project

MCP can help with our GPU optimization project:
1. **Automated Test Scene Creation**:
   - Create complex test scenes programmatically
   - Generate instanced meshes for performance testing

2. **Performance Benchmarking**:
   - Run automated performance tests
   - Collect and analyze performance data

3. **Asset Management**:
   - Import and organize test assets
   - Create materials and textures programmatically

4. **Documentation Generation**:
   - Generate test reports automatically
   - Create documentation through AI-assisted workflows

## Next Steps

1. **Start MCP Server** in UE5 Editor using console command
2. **Test Connection** with basic operations
3. **Integrate into Workflow** for automated testing
4. **Create Custom Tools** for project-specific needs

## Important Notes

- **Experimental Plugin**: ModelContextProtocol is experimental; interfaces may change
- **Local Only**: Default configuration only accepts local connections
- **Security**: Be cautious when enabling external access
- **Port**: Default port is 8000, not 3000 (as previously documented)

Start by testing the basic connection, then we'll integrate MCP into our GPU optimization testing workflow.