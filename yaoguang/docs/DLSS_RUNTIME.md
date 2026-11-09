# DLSS 运行库说明（2.0.0-A 版）

2.0.0-A 版**不包含** `nvngx_dlss.dll`。

这个文件是 NVIDIA 的 DLSS 运行库，NVIDIA 的许可不允许随 mod 再分发，所以从 A 版里剔除了。
**不放它也能正常进游戏、正常开光追**，只是 DLSS 选项不可用（会自动退回按 renderScale 原生渲染 + NRD 降噪）。

---

## 一、你需要什么

| 文件 | 是否需要自己提供 | 说明 |
|---|---|---|
| `nvngx_dlss.dll` | **需要** | DLSS 超分功能的实现库，30–60 MB |
| `_nvngx.dll` / `nvngx.dll` | 不需要 | NGX 核心，跟随显卡驱动安装，程序会自动从 `C:\Windows\System32` 和 `DriverStore` 里找 |

只要 `nvngx_dlss.dll` 这一个文件。

## 二、从哪儿拿

**方式 1（最简单）：从你已经装好的支持 DLSS 的游戏里复制**

打开游戏安装目录，找 `nvngx_dlss.dll`，常见位置：

```
...\Cyberpunk 2077\bin\x64\nvngx_dlss.dll
...\No Man's Sky\Binaries\nvngx_dlss.dll
...\HITMAN 3\Retail\nvngx_dlss.dll
...\Marvel's Spider-Man Remastered\nvngx_dlss.dll
```

文件名就是 `nvngx_dlss.dll`，DLSS 2.x 和 3.x 都是这个名字。建议用 3.7 以上的版本。

**方式 2：NVIDIA 官方 SDK**

- 仓库：`https://github.com/NVIDIA/DLSS`（需要同意 NVIDIA 的许可）
- 文件在 `lib/windows/x64/nvngx_dlss.dll`

**注意**：不要直接用别的 mod 打包里的那份 —— 那同样是二次分发，本说明只支持“从你自己的游戏/SDK 里取”。

## 三、放在哪儿

把 `nvngx_dlss.dll` 放到**游戏目录**下的下面任意一个位置（推荐第一个）：

```
<游戏目录>/config/yaoguang/native/nvngx_dlss.dll      ← 推荐
<游戏目录>/yaoguang/native/nvngx_dlss.dll
```

`<游戏目录>` 就是放 `mods` 文件夹的那一层：

- HMCL / PCL2 / 官方启动器：一般是 `.minecraft`
- 整合包：整合包自己的目录（和 `mods` 同级）

例（HMCL，默认位置）：

```
C:\Users\<你>\.minecraft\config\yaoguang\native\nvngx_dlss.dll
```

也可以不放进游戏目录，改用环境变量指定完整路径：

```
set YAOGUANG_NGX_DLL=D:\dlss\nvngx_dlss.dll
```

## 四、怎么确认生效

进游戏后看日志（`logs/latest.log`）：

- 成功：
  ```
  [Yaoguang] nvngx_dlss.dll taken from C:\...\config\yaoguang\native\nvngx_dlss.dll
  [Yaoguang] DLSS initialized
  ```
- 没放 / 路径不对：
  ```
  [Yaoguang] nvngx_dlss.dll not found - DLSS disabled, put the file at config/yaoguang/native/nvngx_dlss.dll to enable it
  ```
  这时游戏照常运行，只是 DLSS 关掉。

## 五、常见问题

1. **放了还是提示找不到**：确认文件名完全是 `nvngx_dlss.dll`（没有 `.dll.dll`，也没有被浏览器改名成 `(1)`）。
2. **提示 DLSS 初始化失败**：显卡不支持 DLSS（需要 RTX 20 系及以上），或者驱动太旧。
3. **DLL 版本太老**：DLSS 2.0 的 DLL 在本项目里可能初始化失败，换 3.x。
4. **杀软拦截**：部分杀软会拦 `LoadLibrary` 加载游戏目录下的 DLL，加白名单即可。

---

## 给开发者：怎么构建带 DLSS 的版本（自用，不要分发）

1. 把 `nvngx_dlss.dll` 放到 `src/main/resources/native/windows/x86_64/nvngx_dlss.dll`
2. `gradlew build`
3. 产物 `build/libs/yaoguang-2.0.0-A.jar` 就带 DLSS 了 —— **这个 jar 只能自己用，不要上传**

发布用的、可再分发的 jar 由 `releases/yaoguang-2.0.0-A.jar` 提供（已剔除该 DLL）。
