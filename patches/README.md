# 补丁库

放静态字节补丁（`.bp`）。启动器启动时按目标 exe 的指纹自动挑（`[Patch] AutoScan = 1`），不用在 ini 里一条条列。

## 文件格式

```bp
; @patch name=krkr2-xxx desc=一句话说明
; @match exe=game.exe size=123456 crc32=89ABCDEF engine=krkr2
0x12345 : 8B 45 FC -> 33 C0 90
```

- 地址是 RVA；写 `module.dll:0x1234` 可以指定非主模块
- 原始字节必须和进程内存里的一致，否则这条跳过——不会硬写
- **没有 `@match` 的补丁不参与自动扫描**，只能在 ini 里手动列。这条是有意为之：打错补丁会把游戏改坏，自动应用必须过匹配这一关

## @match 条件

| 键 | 说明 |
|---|---|
| `exe` | 目标文件名，不含路径，大小写不敏感 |
| `size` | 文件字节数 |
| `crc32` | 全文件 CRC32，十六进制 |
| `engine` | 引擎标识（`krkr2` / `yuris` 等），引擎没识别出来时带这个条件的补丁会被跳过 |

条件写得越全越安全。只写 `exe` 在玩家换版本后会误命中，建议 `exe + size + crc32` 一起写。

## 算 size 和 crc32

```bash
python3 -c "import sys,zlib; d=open(sys.argv[1],'rb').read(); print(len(d), '%08X' % (zlib.crc32(d) & 0xffffffff))" game.exe
```

## 命名

`<引擎>-<问题>.bp`，例如 `krkr2-font-cache.bp`。放一层目录即可，不递归扫描。

## 注意

- CRC32 + 文件大小只能防误配，不是签名。玩家改过的 exe（汉化、破解）会不匹配，这是预期行为，别放宽条件去兼容
- 模板见 `template.bp.txt`，它不会被扫描（后缀不是 `.bp`）
