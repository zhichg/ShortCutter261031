# 离线拼音数据

- `pinyin-data/`：https://github.com/mozillazg/pinyin-data
- `phrase-pinyin-data/`：https://github.com/mozillazg/phrase-pinyin-data

各目录中的 `REVISION` 固定本次使用的上游版本，`LICENSE` 保留 MIT 许可。
原始数据仅在开发时使用；运行 `python scripts/build-pinyin.py` 生成
`src/pinyin_data.h`，将字音及非默认读音的词组编入程序。用户使用时无需联网、Python 或额外安装。

排序和搜索使用相同的无声调拼音，优先匹配最长的多音词组，其他汉字采用字典首选读音。
`ü` 使用 `v` 表示；词典未覆盖的字符保留原文。
