# CODEBUDDY.md

This file provides guidance to CodeBuddy Code when working with code in this repository.

## Project Overview

This is a **UE5.8 source-built project** with minimal skeleton structure. The project uses Unreal Engine 5.8 from source at `D:\unreal\UnrealEngine\Engine\`.

## Build System

**Primary Build System: Unreal Build Tool (UBT)**

### Target Files
- `Source/pro.Target.cs` - Game runtime target
- `Source/proEditor.Target.cs` - Editor target

### Module Dependencies
From `Source/pro/pro.Build.cs`:
```csharp
PublicDependencyModuleNames.AddRange(new string[] { 
    "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" 
});
```

## Common Development Commands

### Building
```bash
# Via Visual Studio (recommended)
1. Open pro.sln
2. Select Development Editor | Win64
3. Build Solution (Ctrl+Shift+B)

# Generate project files
Right-click pro.uproject → "Generate Visual Studio project files"
```

### Running
```bash
# Via Visual Studio
1. Set pro as startup project
2. Press F5 (Debug) or Ctrl+F5 (Run)

# Via Editor
1. Open pro.uproject
2. Click Play button in viewport
```

### Testing
```bash
# Automation tests via Editor
1. Open pro.uproject
2. Window → Developer Tools → Session Frontend
3. Select Automation tab

# Command line (if automation tools configured)
AutomationTool.exe -project=E:\unrealProject\pro\pro.uproject -run=AutomationTests
```

## Code Architecture

### Current State: Minimal Skeleton
The project has only basic module definition:
- `Source/pro/pro.h` - Module header (empty except includes)
- `Source/pro/pro.cpp` - Module implementation (1 line of actual code)

### Rendering Configuration
From `Config/DefaultEngine.ini`:
- **Ray Tracing**: Enabled (`r.RayTracing=True`)
- **Virtual Shadows**: Enabled (`r.Shadow.Virtual.Enable=1`)
- **Dynamic Global Illumination**: Lumen (method 1)
- **Reflection Method**: Lumen (method 1)
- **Substrate**: Enabled (UE5.8 material system)
- **Platform Targets**: Windows (DX12), Linux (Vulkan SM6), Mac (Metal SM6)

### Plugin Usage
Only one plugin is enabled:
- **ModelingToolsEditorMode** - Editor-only modeling tools

## Directory Structure

```
pro/
├── Binaries/              # Compiled binaries (git-ignored)
├── Config/                # Configuration files
├── Content/               # Game assets (not in version control)
├── DerivedDataCache/      # Cached data (git-ignored)
├── Intermediate/          # Build intermediates (git-ignored)
├── Saved/                 # Editor state and logs (git-ignored)
├── Source/                # C++ source code
│   ├── pro/               # Main game module
│   ├── pro.Target.cs      # Game target configuration
│   └── proEditor.Target.cs # Editor target configuration
├── pro.uproject           # Project descriptor
├── pro.sln                # Visual Studio solution
└── Automation_pro.sln     # Automation tooling solution
```

## Important Notes

1. **Source-Built Engine**: This project requires Unreal Engine 5.8 built from source
2. **Git Configuration**: Build artifacts are excluded via `.gitignore`
3. **Content Management**: Game assets (`.uasset`, `.umap`) are not tracked in version control
4. **Visual Studio Requirements**: See `.vsconfig` for required components

## Development Workflow

1. **Initial Setup**:
   - Ensure UE5.8 source is built at `D:\unreal\UnrealEngine`
   - Clone this repository
   - Right-click `pro.uproject` → "Generate Visual Studio project files"

2. **Daily Development**:
   - Use Visual Studio for C++ development
   - Use Unreal Editor for level design and testing
   - Run automation tests via Session Frontend in Editor

3. **Adding New Modules**:
   - Create new module folders under `Source/`
   - Update `pro.Build.cs` for additional dependencies
   - Regenerate project files after adding new modules