# UE5.8 Project

基于Unreal Engine 5.8源码版的项目。

## 项目结构

- `Config/`: 项目配置文件
- `Source/`: C++源代码
- `Content/`: 游戏资源（未包含在版本控制中）
- `pro.uproject`: 项目文件

## 设置

1. 确保已安装Unreal Engine 5.8源码版
2. 克隆此仓库
3. 右键点击`pro.uproject`，选择"Generate Visual Studio project files"
4. 用Visual Studio打开生成的`.sln`文件

## 构建

使用Visual Studio或Unreal Editor构建项目。

## 注意事项

- `Binaries/`、`Intermediate/`、`DerivedDataCache/`和`Saved/`目录已被.gitignore排除
- 大型资源文件（如`.uasset`、`.umap`）未包含在版本控制中
- 如需管理大型资源，请考虑使用Git LFS