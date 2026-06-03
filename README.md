# charset_detection

ファイルの文字コードを判別するコマンドラインツールです。
入力バイト列を各文字コードとして UTF-16 に変換し、日本語文字ビグラムの出現頻度スコアを比較することで文字コードを推定します。

判別対象の文字コード：

- **UTF-8**
- **EUC-JP**
- **ISO-2022-JP**
- 上記いずれにも該当しない場合は **その他**（`unknown`）

## 使い方

```sh
charset_detection [ファイル名]
```

判別結果（`utf8` / `eucjp` / `iso2022jp` / `unknown`）を標準出力に1行で表示します。
ファイル名を省略した場合は `input.utf8.txt` を対象とします。

```sh
> charset_detection input.eucjp.txt
eucjp
```

## ビルド

Qt の qmake プロジェクト（[charset_detection.pro](charset_detection.pro)）として構成しています。
Qt 自体には依存せず、コンソールアプリケーションとしてビルドされます（C++17）。

```sh
qmake
make        # MSVC 環境では nmake
```

qmake を使わず、MSVC で直接ビルドすることもできます。

```sh
cl /EHsc /std:c++17 /Fe:charset_detection.exe main.cpp charset_detection.cpp unicode_conversion.cpp
```

## 判別ロジック

中核となるのは [charset_detection.cpp](charset_detection.cpp) の `detect_charaset()` 関数です。
**スコアリング方式**を採用しており、変換後テキストの「日本語らしさ」を数値で比較します。

### 判別の流れ

1. 入力バイト列を UTF-8 / EUC-JP / ISO-2022-JP の 3 通りで UTF-16 に変換する。
2. 各変換結果に対してビグラム（連続する 2 文字対）スコアを計算する。
   - `validation_jp.table` に登録された文字対の出現頻度を合計し、文字対の総数で割った平均値をスコアとする。
   - 日本語として自然なテキストになるほどスコアが高くなる。
3. 最高スコアの文字コードを採用する。全スコアが 0 の場合は `unknown` を返す。

### 文字コード変換

- **UTF-8** → UTF-16：[unicode_conversion.cpp](unicode_conversion.cpp) の `convert_utf8_to_utf16()` を使用。

- **EUC-JP** → UTF-16（`convert_eucjp_to_utf16`）
  以下のバイト構造を処理する：
  - `0x00`–`0x7F`：ASCII
  - `0x8E` + `0xA1`–`0xDF`：SS2（半角カナ、JIS X 0201）
  - `0x8F` + 2バイト：SS3（JIS X 0212）
  - `0xA1`–`0xFE` + `0xA1`–`0xFE`：JIS X 0208（2バイト文字）

- **ISO-2022-JP** → UTF-16（`convert_iso2022jp_to_utf16`）
  ESC シーケンスで文字集合を切り替えるステートマシン方式：
  - `ESC ( B`：ASCII
  - `ESC ( I`：JIS X 0201（半角カナ）
  - `ESC $ @`：JIS X 0208-1978
  - `ESC $ B`：JIS X 0208-1983

### テーブルファイルの生成

`eucjp.table` および `validation_jp.table` はビルド済みのデータファイルです。
再生成が必要な場合は `charset_detection.cpp` 内の `save_eucjp_table()` / `save_validation_jp_table()` を使用します（通常のビルドでは実行されません）。

## ファイル構成

| ファイル | 内容 |
|----------|------|
| [main.cpp](main.cpp) | エントリポイント（ファイル読み込みと結果出力） |
| [charset_detection.cpp](charset_detection.cpp) | 文字コード判別ロジック（`detect_charaset`・各変換関数） |
| [charset_detection.h](charset_detection.h) | `charset_detection.cpp` の公開インターフェース |
| [unicode_conversion.cpp](unicode_conversion.cpp) | UTF-8 ↔ UTF-16 変換 |
| [unicode_conversion.h](unicode_conversion.h) | `unicode_conversion.cpp` の公開インターフェース |
| [eucjp.table](eucjp.table) | EUC-JP → Unicode 変換テーブル（`iconv` で生成済み） |
| [validation_jp.table](validation_jp.table) | 日本語文字ビグラム出現頻度テーブル（スコアリング用） |
| [charset_detection.pro](charset_detection.pro) | qmake プロジェクトファイル |
| `input.utf8.txt` | UTF-8 のテスト用データ |
| `input.eucjp.txt` | EUC-JP のテスト用データ |
| `input.iso2022jp.txt` | ISO-2022-JP のテスト用データ |
