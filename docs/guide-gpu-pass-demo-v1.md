# GPU Pass Demo v1 搭建指南

## 目标

搭建一个最小可运行 demo：进入 PIE 后由蓝图触发 compute shader，把 `SimpleComputeShader.usf` 的渐变结果写入 `RT_GPUComputeOutput`，再通过材质显示到场景中的平面上。

## 代码入口

蓝图节点：

```text
GPU Driven Pipeline > Execute Simple Compute Shader
```

输入：

- `Output Render Target`：`RT_GPUComputeOutput`

可选调试节点：

```text
GPU Driven Pipeline > Get Last Compute Dispatch Time
```

注意：这个时间是 CPU 侧 dispatch 调用耗时，不是真实 GPU 执行耗时。

## 资产路径

推荐资产路径：

```text
Content/GPUDrivenDemo/Maps/L_GPUComputePassDemo
Content/GPUDrivenDemo/Textures/RT_GPUComputeOutput
Content/GPUDrivenDemo/Materials/M_GPUComputeOutput
Content/GPUDrivenDemo/Blueprints/BP_GPUComputePassDemo
```

UE 资产重命名必须通过 Content Browser 完成，不要直接在文件系统里改 `.uasset` 或 `.umap`。

## Render Target 设置

创建 `RT_GPUComputeOutput`，类型为 `TextureRenderTarget2D`。

建议设置：

- Size X：`1024`
- Size Y：`1024`
- Format：`RTF RGBA8`
- 必须启用 UAV 支持

如果 RenderTarget 没有启用 UAV 支持，C++ 会打印 warning 并拒绝执行 compute pass，避免触发 D3D12 资源状态错误。

## 材质设置

创建 `M_GPUComputeOutput`。

设置：

- Material Domain：`Surface`
- Shading Model：`Unlit`

材质图：

1. 添加 `Texture Sample`。
2. 纹理选择 `RT_GPUComputeOutput`。
3. 将 RGB 输出连接到 `Emissive Color`。

## 蓝图 Actor

创建 `BP_GPUComputePassDemo`，父类为 Actor。

组件：

- 添加 `StaticMeshComponent`。
- Static Mesh 选择 Engine 自带 Plane。
- 材质设置为 `M_GPUComputeOutput`。

Event Graph：

1. 使用 `Event BeginPlay`。
2. 调用 `Execute Simple Compute Shader`。
3. 传入 `RT_GPUComputeOutput`。
4. 可选打印一次 `GPU pass dispatched`。

如果屏幕消息重复刷屏，优先移除蓝图里的 `Print String`，保留 C++ Output Log 作为验证信息。

## 关卡设置

1. 打开 `L_GPUComputePassDemo`。
2. 将 `BP_GPUComputePassDemo` 放入关卡。
3. 确认场景中只有一个测试 Actor。
4. 调整视角，让平面清楚可见。
5. 保存所有资产。

## 运行预期

点击 Play 后应看到：

- 平面显示红绿渐变。
- Output Log 出现 `GPUDrivenPipeline: Dispatched SimpleComputeShader ...`。
- 不出现 GPU crash 或 D3D device removed。

## 验证矩阵

可以分别测试 RenderTarget 尺寸：

- `512 x 512`
- `1024 x 1024`
- `2048 x 2048`

通过标准：

- 每个尺寸都能显示渐变。
- 多次 Play / Stop 不崩溃。
- Output Log 没有 UAV 或 shader 编译相关 warning。
