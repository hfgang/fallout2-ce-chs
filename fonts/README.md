# FreeType 字体（中文支持）

本目录提供可选的 FreeType 字体配置。把整个 `fonts` 目录复制到游戏目录
（与 `master.dat` 同级）后，引擎会优先使用 FreeType 渲染界面文字；
目录不存在或缺少可用字体时，自动回退到原始位图字体，不影响英文游戏。

**macOS 的 app 与 iOS 的 ipa 已经内置本目录的内容**，开箱即有中文，不用手动复制。
（iOS 上字体在 app 包内，游戏数据在 Documents；想换字体再把 `fonts` 拷进
Documents，包外那份优先。）

`fonts/chs/` 已内置 **Noto Sans SC**（SIL OFL 1.1 许可，见
`NotoSansSC-LICENSE.txt`），同时覆盖拉丁字母与 CJK 字形，**开箱即用**，
无需另外准备字体。

## 中文（GBK）配置

1. 把 `fonts` 目录复制到游戏目录（与 `master.dat` 同级）。

2. 在 `fallout2.cfg` 中设置（`[system]` 段，与上游英文配置相同的位置）：

   ```ini
   language=chs
   ```

   即中文社区的标准取值。也可以保持 `language=english` 不变——汉化包的
   消息文件通常在 `master.dat` 内替换，引擎会依次尝试
   `fonts/<language>/` → `fonts/chs/`，`english` 与 `chinese`/`zh` 等
   取值都会落到内置的中文配置。

3. 启动游戏。中文消息文件（GBK 编码）随你的汉化包放置即可，引擎会按
   `text/<语言>/` → `text/english/` → DAT 的顺序查找界面字体资源，
   文字渲染则完全由 FreeType 接管。

## 换用其他字体

把任意 TrueType 字体（`.ttf`/`.otf`）放进 `fonts/chs/`，并修改
`font.ini` 中对应 `[fontN]` 的 `fileName` 指向它。可选字体参考：

- **文泉驿微米黑 / WenQuanYi Micro Hei**（GPLv3 + 字体例外条款）
- 系统自带字体：Windows 的 `simhei.ttf`（中易黑体）、`msyh.ttc`
  （微软雅黑）；macOS `/System/Library/Fonts/Supplemental/` 下的黑体类；
  Linux 的 Noto Sans CJK 系列
- 像素字体 Zpix「最像素」可用于 `[font1]`（对话字体），个人/教育免费，
  商业使用需另行授权

## 配置项说明

`font.ini` 中每个 `[fontN]` 对应游戏内字体 `100+N`：

| 键 | 说明 |
| --- | --- |
| `maxHeight` | 字形像素高度（必填） |
| `maxWidth` | 字形像素宽度，缺省同 `maxHeight` |
| `lineSpacing` | 行间额外像素（必填） |
| `wordSpacing` | 空格宽度（必填） |
| `letterSpacing` | 字符间额外像素（必填） |
| `heightOffset` | 行高额外增量（必填） |
| `warpMode` | `1` = 任意位置换行（CJK），`0` = 优先在空格/连字符处换行 |
| `encoding` | 消息文件文本编码（必填，中文为 `GBK`） |
| `fileName` | 字体文件名（必填） |

调整 `maxHeight` 等数值可以改变文字大小；界面布局按原始位图字体的行高
调校，改动过大会导致个别界面排版错位。指标值取自参考中文移植
（askme765cs/fallout2-ce-vita）。

## 其他语言 / 自定义

字体目录按 `fallout2.cfg` 的 `language` 取值解析：

1. `fonts/<language>/font.ini` —— 各语言独立配置，优先级最高；
2. `fonts/chs/font.ini` —— 仅当 `language` 为 `english` 或中文类取值
   （`chs`/`chinese`/`zh` 等）时使用；
3. 都没有则禁用 FreeType，使用原始位图字体。

俄语、德语、日语等非 GBK 兼容的语言默认走原始位图字体，不会被中文配置
影响；如需 FreeType 渲染，可自建 `fonts/<language>/font.ini` 并设置合适
的 `encoding`（如 `BIG5`、`ISO-8859-1`）。
