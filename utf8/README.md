# utf8

テキストファイルの文字コードを自動判別して UTF-8 に変換するコマンドラインツールです。
`charset_detection` ライブラリを使用して UTF-8 / EUC-JP / Shift_JIS / ISO-2022-JP を判別し、UTF-8 に変換して出力します。

## 使い方

```sh
utf8 [-i | -o output_file] input_file [...]
```

### オプション

| オプション | 説明 |
|-----------|------|
| `-i` | 入力ファイルを直接上書きする |
| `-o output_file` | 変換結果を指定ファイルに書き出す（入力ファイルが1つの場合のみ） |
| オプションなし | 変換結果を標準出力に出力する |

`-i` と `-o` は同時に指定できません。

### 使用例

```sh
# 変換結果を標準出力に表示する
utf8 input.eucjp.txt

# ファイルを直接 UTF-8 に上書き変換する
utf8 -i input.sjis.txt

# 変換結果を別ファイルに書き出す
utf8 -o output.txt input.iso2022jp.txt

# 複数ファイルをまとめて上書き変換する
utf8 -i file1.txt file2.txt file3.txt
```

## 変換ロジック

1. 入力ファイルを読み込む。
2. `detect_charaset()` で文字コードを判別する（UTF-8 / EUC-JP / Shift_JIS / ISO-2022-JP）。
3. 判別結果に応じて各変換関数で UTF-16 に変換し、さらに UTF-8 に変換する。
   - 既に UTF-8 の場合はそのまま出力する。
   - 判別不能（`unknown`）の場合も入力をそのまま出力する。

## ビルド

Qt の qmake プロジェクト（[utf8.pro](utf8.pro)）として構成しています。
Qt 自体には依存せず、コンソールアプリケーションとしてビルドされます（C++17）。
`charset_detection.cpp` / `unicode_conversion.cpp` を親ディレクトリから参照しています。

```sh
qmake
make        # MSVC 環境では nmake
```

## ファイル構成

| ファイル | 内容 |
|----------|------|
| [main.cpp](main.cpp) | エントリポイント（引数解析・変換処理） |
| [utf8.pro](utf8.pro) | qmake プロジェクトファイル |
| `../charset_detection.cpp` | 文字コード判別・変換ロジック |
| `../unicode_conversion.cpp` | UTF-8 ↔ UTF-16 変換 |
