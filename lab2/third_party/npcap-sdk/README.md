# Npcap SDK（第三方依赖）

这里的头文件和导入库来自 **Npcap SDK 1.13**（官方下载：https://npcap.com/#download）。

本机只安装了 Npcap 运行库（`C:\Windows\System32\Npcap\wpcap.dll`），没有安装 SDK，
因此把编译所需的这部分文件随工程一起放进来，保证 `build.bat` 在任意机器上都能直接编译，
不需要再单独安装 SDK。

保留的内容：

| 路径 | 作用 |
| --- | --- |
| `Include\` | `pcap.h` 及其头文件，程序里 `#include <pcap.h>` 用 |
| `Lib\x64\wpcap.lib` | 导入库，链接 `wpcap.dll` 用（64 位） |
| `Lib\x64\Packet.lib` | 导入库，链接 `Packet.dll` 用（驱动程序接口） |

已删除 SDK 中的 `docs\`、`Examples-pcap\`、`Examples-remote\` 以及 `Lib\x86`、`Lib\ARM64`，
它们与本实验无关。

> 如果以后把 Npcap SDK 安装到了系统目录（例如 `C:\npcap-sdk`），可以删掉本目录，
> 并把 `build.bat` 里的 `set "SDK=third_party\npcap-sdk"` 改成 SDK 的实际路径。
