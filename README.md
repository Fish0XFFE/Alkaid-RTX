# Alkaid-RTX（摇光）

Minecraft 1.20.1 Forge 的实时光线追踪渲染器。用 Vulkan 光追重画整个世界：路径追踪的直射光与间接光、体积云、物理大气、分通道水体折射，再由 NRD 降噪 + DLSS 超分合成。

> 不是光影包。它替换的是方块渲染管线本身：区块几何进 BLAS、光追求交、自己着色、自己合成。



## 特性

- **光追全局光照**：每像素主射线 + 方向遮挡间接光，接触处与屋檐下自然变暗（不是调色出来的）
- **体积云**：半分辨率 compute 通道 + 上帧方向重投影，二阶多次散射近似、双瓣 HG 相位（前向 0.62 / 后向 −0.25）、沿光线积分太阳透射率、云影投到地面与水面
- **物理大气**：瑞利 / 米氏 / 臭氧按公里制单次散射 + 多次散射近似，64×64 太阳透射率 LUT（取代逐像素 8 步积分）
- **水体**：分通道折射 IOR（1.331 / 1.333 / 1.337）、分通道 Beer-Lambert 吸收、两层方向行波法线、水下 Snell 窗与全反射
- **材质**：像素级裁切（任意距离都正确）、方块面按朝向明暗、玻璃单次透射、钻石/绿宝石真反射、传送门漩涡、信标光束
- **天气**：晴 / 雨 / 雷（闪电照亮全场景）/ 雪（按生物群系温度自动切换），配套雾、云量、地面湿润度
- **稳定性**：设备丢失自动降级为原版渲染而不是卡死；帧超时三振后自动恢复

## 安装

1. Minecraft 1.20.1+ Forge 47.4.22（其他 47.x 一般也行）
2. 把 yaoguang-2.0.0-A.jar放进 mods/
3. 需要 RTX 20 系及以上（Vulkan 光追 + DLSS）

### DLSS 运行库（重要）

2.0.0-A 版不包含NVIDIA 的 nvngx_dlss.dll（许可不允许再分发）。

- 不放也能正常玩：光追、云、大气、水全部工作，只是 DLSS 不可用，改为按 renderScale 原生渲染
- 想要 DLSS：自己准备这个文件并放到

<游戏目录>/config/yaoguang/native/nvngx_dlss.dll
 
从哪下载、怎么确认生效、排错，见 **[docs/DLSS_RUNTIME.md](docs/DLSS_RUNTIME.md)**。

## 下载

到 [Releases](/releases) 下载最新的 yaoguang-2.0.0-A.jar。

## 构建（开发者）

依赖：

- JDK 17（设置 JAVA_HOME）
- CMake ≥ 3.28 + Visual Studio 2026（C++ 桌面工作负载）
- Vulkan SDK ≥ 1.4（glslc）
- 第三方库放到仓库同级的 externals/ 目录：nrd（NVIDIA Realtime Denoisers）、dxc（DirectXShaderCompiler）

步骤：

bash
# 1. 着色器（源在 src/native/shaders，产物进 src/main/resources/native/shaders）
glslc --target-env=vulkan1.3 -o src/main/resources/native/shaders/ray_gen.rgen.spv src/native/shaders/ray_gen.rgen

# 2. 原生库
cmake -S src/native -B build-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native --config Release --target yaoguang_rt

# 3. mod jar
./gradlew build

产物：build/libs/yaoguang-2.0.0-A.jar。

> 如果把 nvngx_dlss.dll 放进了 src/main/resources/native/windows/x86_64/，构建出来的 jar 会带 DLSS —— **那个 jar 只能自用，不要上传**。

## 目录结构

src/main/java/com/fish/yaoguang/   客户端、GUI、Mixin、JNI 桥
src/main/resources/native/         运行时资源（DLL、SPV）
src/native/shaders/                GLSL（ray_gen / closest_hit / alpha_test / miss / cloud / combine / composite）
src/native/src/                    RayTracingPipeline、SceneAccel、NRD、DLSS、Vulkan 上下文
docs/                              文档


## 已知问题

- 关闭游戏时偶发 `EXCEPTION_ACCESS_VIOLATION`：发生在 Stopping 之后、GL 驱动 teardown 阶段，进程已正常退出，不影响存档
- 末地亮度在某些机位会在两档之间跳变

## 第三方

- [NVIDIA Realtime Denoisers](https://github.com/NVIDIA-RTX/NRD)（MIT）—— NRD 降噪
- [DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler)（MIT/LLVM）—— ShaderMake 编译 NRD/DLSS 着色器
- NVIDIA DLSS —— **不随本项目分发**，请自行准备，见 [docs/DLSS_RUNTIME.md](docs/DLSS_RUNTIME.md)

## 许可

见 [LICENSE.txt](LICENSE.txt)。

