#include "unicode_conversion.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iconv.h>
#include <map>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>


#include "eucjp.table"
#include "validation_jp.table"

// 文字コード判別ツール
// 入力バイト列を UTF-8 / EUC-JP / ISO-2022-JP として UTF-16 に変換し、
// 日本語文字ビグラムの出現頻度スコアを比較することで文字コードを推定する。

constexpr char32_t REPLACEMENT_CHARACTER = 0xfffd;

// JIS X 0208 コードを EUC-JP コードに変換する（上位バイトに 0x80 を加算）
static inline uint16_t x_jistoeuc(uint16_t jis)
{
	if (jis >= 0x2121 && jis <= 0x7e7e) {
		uint16_t lead = (jis >> 8) & 0xff;
		uint16_t trail = jis & 0xff;
		if (lead >= 0x21 && lead <= 0x7e && trail >= 0x21 && trail <= 0x7e) {
			return jis | 0x8080;
		}
	}
	return 0;
}

// EUC-JP コードを JIS X 0208 コードに変換する（上位バイトから 0x80 を減算）
static inline uint16_t x_euctojis(uint16_t jis)
{
	if (jis >= 0xa1a1 && jis <= 0xfefe) {
		uint16_t lead = (jis >> 8) & 0xff;
		uint16_t trail = jis & 0xff;
		if (lead >= 0xa1 && lead <= 0xfe && trail >= 0xa1 && trail <= 0xfe) {
			return jis & ~0x8080;
		}
	}
	return 0;
}

// EUC-JP コードポイントを Unicode コードポイントに変換する。
// eucjp.table の eucjp_to_unicode_table を二分探索して対応する値を返す。
// ASCII 範囲（0x00–0x7F）はそのまま返す。対応するエントリがなければ 0 を返す。
static uint32_t convert_eucjp_to_unicode(uint32_t euc)
{
	if (euc < 0x80) {
		return euc;
	}
	size_t n = std::size(eucjp_to_unicode_table);
	size_t lo = 0;
	size_t hi = n;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		auto c = eucjp_to_unicode_table[mid].eucjp;
		if (c < euc) {
			lo = mid + 1;
		} else if (c > euc) {
			hi = mid;
		} else {
			return eucjp_to_unicode_table[mid].unicode;
		}
	}
	return 0;
}

static inline bool is_hiragana(uint32_t unicode)
{
	return unicode >= 0x3040 && unicode <= 0x309f;
}

static inline bool is_katakana(uint32_t unicode)
{
	return unicode >= 0x30a0 && unicode <= 0x30ff;
}

// 指定した Unicode コードポイントの「日本語らしさ」スコアを返す。
// validation_jp.table に登録された文字対（ビグラム）キーに対して出現頻度を返す。
// テーブルにないコードポイントは 0、ASCII（< 0x100）は常に 0。
static size_t jp_validation_count(uint32_t unicode)
{
	if (unicode < 0x100) return 0;

	// 日本語文で頻出する記号は固定スコア 1 を返す
	switch ((uint32_t)unicode) {
	case 0x3001: // U+3001 IDEOGRAPHIC COMMA
	case 0x3002: // U+3002 IDEOGRAPHIC FULL STOP
	case 0x300c: // U+300C LEFT CORNER BRACKET
	case 0x300d: // U+300D RIGHT CORNER BRACKET
	case 0x30fc: // U+30FC KATAKANA-HIRAGANA PROLONGED SOUND MARK
	case 0xff0c: // U+FF0C FULLWIDTH COMMA
	case 0xff0e: // U+FF0E FULLWIDTH FULL STOP
		return 1;
	}
	if (is_hiragana(unicode)) return 1;
	if (is_katakana(unicode)) return 1;

	size_t n = std::size(validation_jp_table);
	size_t lo = 0;
	size_t hi = n;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		auto c = validation_jp_table[mid].unicode;
		if (c < unicode) {
			lo = mid + 1;
		} else if (c > unicode) {
			hi = mid;
		} else {
			return validation_jp_table[mid].count;
		}
	}
	return 0;
}

// 直前と現在の文字コードポイントを保持するビグラム用構造体
struct Pair {
	uint32_t leading = 0;
	uint32_t trailing = 0;
};

// EUC-JP バイト列を UTF-16 文字列に変換する。
// 対応するバイト構造：
//   0x00–0x7F : ASCII
//   0x8E + 0xA1–0xDF : SS2（半角カナ、JIS X 0201）
//   0x8F + 2バイト : SS3（JIS X 0212）
//   0xA1–0xFE + 0xA1–0xFE : JIS X 0208 の 2 バイト文字
std::u16string convert_eucjp_to_utf16(std::string_view const &s)
{
	std::u16string out;

	auto Push = [&](char32_t c) {
		uint16_t utf16l = (char16_t)c;
		if (utf16l != 0) {
			uint16_t utf16h = (char16_t)(c >> 16);
			if (utf16h != 0) {
				out.push_back((char16_t)utf16h);
			}
			out.push_back((char16_t)utf16l);
		} else {
			out.push_back(REPLACEMENT_CHARACTER);
		}
	};

	const char *ptr = s.data();
	const char *end = ptr + s.size();
	while (ptr < end) {
		uint32_t eucjp = 0;
		unsigned char c0 = (unsigned char)*ptr++;
		char const *next = ptr;
		if (c0 <= 0x7f) {
			eucjp = c0;
		} else if (c0 == 0x8e) { // SS2: half-width katakana (JIS X 0201)
			if (next + 1 < end) {
				unsigned char c1 = (unsigned char)*next++;
				if (c1 >= 0xa1 && c1 <= 0xdf) {
					eucjp = (0x8e << 8) | c1;
				}
			}
		} else if (c0 == 0x8f) { // SS3: JIS X 0212 (3 bytes)
			if (next + 2 < end) {
				unsigned char c1 = (unsigned char)*next++;
				unsigned char c2 = (unsigned char)*next++;
				if (c1 >= 0xa1 && c1 <= 0xfe && c2 >= 0xa1 && c2 <= 0xfe) {
					eucjp = (0x8f << 16) | (c1 << 8) | c2;
				}
			}
		} else if (c0 >= 0xa1 && c0 <= 0xfe) { // JIS X 0208 (2 bytes)
			if (next + 1 < end) {
				unsigned char c1 = (unsigned char)*next++;
				if (c1 >= 0xa1 && c1 <= 0xfe) {
					eucjp = (c0 << 8) | c1;
				}
			}
		} else {
			eucjp = 0;
		}
		ptr = next;
		uint32_t unicode = convert_eucjp_to_unicode(eucjp);
		Push(unicode);
	}
	return out;
}

// Shift_JIS コードを JIS X 0208 コードに変換する。Shift_JIS のバイト構造を解析して対応する JIS コードを計算する。
static inline uint16_t x_jmstojis(uint16_t c)
{
	if (c >= 0xe000) {
		c -= 0x4000;
	}
	c = (((c >> 8) - 0x81) << 9) | (unsigned char)c;
	if ((unsigned char)c >= 0x80) {
		c -= 1;
	}
	if ((unsigned char)c >= 0x9e) {
		c += 0x62;
	} else {
		c -= 0x40;
	}
	c += 0x2121;
	return c;
}

// JIS X 0208 コードを Shift_JIS コードに変換する。JIS X 0208 のバイト構造を解析して対応する Shift_JIS コードを計算する。
static inline uint16_t x_jistojms(uint16_t c)
{
	c -= 0x2121;
	if (c & 0x100) {
		c += 0x9e;
	} else {
		c += 0x40;
	}
	if ((unsigned char)c >= 0x7f) {
		c++;
	}
	c = (((c >> (8 + 1)) + 0x81) << 8) | ((unsigned char)c);
	if (c >= 0xa000) {
		c += 0x4000;
	}
	return c;
}

// Shift_JISバイト列を UTF-16 文字列に変換する。
// Shift_JIS のバイト構造を解析して、対応する JIS コードを経由して Unicode に変換する。
// ASCII（0x00–0x7F）と半角カナ（0xA1–0xDF）は直接 Unicode にマッピングされる。
// 対応する JIS コードがないバイト列は REPLACEMENT_CHARACTER に置き換えられる。
std::u16string convert_sjis_to_utf16(std::string_view const &s)
{
	std::u16string out;
	
	char const *begin = s.data();
	char const *end = begin + s.size();
	char const *ptr = begin;
	while (ptr < end) {
		int c0 = (unsigned char)*ptr++;
		if (c0 <= 0x7f) {
			out.push_back(c0);
			continue;
		}
		if (c0 >= 0xa1 && c0 <= 0xdf) {
			out.push_back(0xff61 + (c0 - 0xa1));
			continue;
		}
		if (ptr < end) {
			int c1 = (unsigned char)*ptr++;
			uint16_t sjis = (c0 << 8) | c1;
			uint16_t jis = x_jmstojis(sjis);
			if (jis != 0) {
				uint32_t eucjp = x_jistoeuc(jis);
				uint32_t unicode = convert_eucjp_to_unicode(eucjp);
				out.push_back((char16_t)unicode);
				continue;
			}
		}
		out.push_back(REPLACEMENT_CHARACTER);
	}
	return out;
}

// ISO-2022-JP バイト列を UTF-16 文字列に変換する。
// ESC シーケンスで文字集合を切り替えながらデコードするステートマシン方式。
//   ESC ( B → ASCII
//   ESC ( I → JIS X 0201（半角カナ）
//   ESC $ @ → JIS X 0208-1978
//   ESC $ B → JIS X 0208-1983
std::u16string convert_iso2022jp_to_utf16(std::string_view s)
{
	std::u16string out;
	char const *begin = s.data();
	char const *end = begin + s.size();
	char const *ptr = begin;
	enum EscapeState {
		ASCII,
		JIS_X_0201,
		JIS_X_0208_1978,
		JIS_X_0208_1983,
	} state = ASCII;
	while (ptr < end) {
		int c0 = (unsigned char)*ptr++;
		if (c0 == 0x1b) {
			if (ptr + 2 <= end) {
				unsigned char c1 = (unsigned char)ptr[0];
				unsigned char c2 = (unsigned char)ptr[1];
				if (c1 == '(' && c2 == 'B') {
					state = ASCII;
					ptr += 2;
				} else if (c1 == '(' && c2 == 'I') {
					state = JIS_X_0201;
					ptr += 2;
				} else if (c1 == '$' && c2 == '@') {
					state = JIS_X_0208_1978;
					ptr += 2;
				} else if (c1 == '$' && c2 == 'B') {
					state = JIS_X_0208_1983;
					ptr += 2;
				}
			}
			continue;
		}
		if (state == ASCII) {
			if (c0 < 0x80) {
				out.push_back(c0);
				continue;
			}
		}
		if (state == JIS_X_0201) {
			if (c0 >= 0xa1 && c0 <= 0xdf) {
				out.push_back(0xff61 + (c0 - 0xa1));
			} else {
				out.push_back(REPLACEMENT_CHARACTER);
			}
			continue;
		}
		if (state == JIS_X_0208_1978 || state == JIS_X_0208_1983) {
			if (ptr < end) {
				int c1 = (unsigned char)*ptr++;
				if (c0 >= 0x21 && c0 <= 0x7e && c1 >= 0x21 && c1 <= 0x7e) {
					uint16_t jis = (c0 << 8) | c1;
					uint32_t eucjp = x_jistoeuc(jis);
					uint32_t unicode = convert_eucjp_to_unicode(eucjp);
					out.push_back((char16_t)unicode);
					continue;
				}
			}
		}
		out.push_back(REPLACEMENT_CHARACTER);
	}
	return out;
}

// UTF-8 テキストを解析し、日本語らしさ判定用のビグラム頻度テーブルを生成する。
// 非 ASCII 文字（>= 0x100）の連続する 2 文字対の出現回数を集計し、
// 上位 4096 エントリを validation_jp.table ファイルに書き出す。
// このテーブルは detect_charaset のスコアリングに使用される。
std::string save_validation_jp_table(std::string_view utf8)
{
	std::u16string s = convert_utf8_to_utf16(utf8);

	Pair pair;
	std::map<uint32_t, size_t> counts;
	auto Push = [&](char32_t c) {
		pair.leading = pair.trailing;
		pair.trailing = c;
		if (pair.leading >= 0x100 && pair.trailing >= 0x100) {
			uint32_t key = (pair.leading << 16) | pair.trailing;
			counts[key]++;
		}
	};

	for (size_t i = 0; i < s.size(); i++) {
		Push(s[i]);
	}

	std::vector<std::pair<uint32_t, size_t>> vec;
	for (const auto &entry : counts) {
		vec.push_back(entry);
	}
	vec.resize(std::min(4096UL, vec.size()));
	FILE *fp = fopen("validation_jp.table", "w");
	if (fp) {
		fputs(R"---(struct {
    uint32_t unicode;
    uint32_t count;
} validation_jp_table[] = {
)---",
			fp);
		for (const auto &entry : vec) {
			uint32_t key = entry.first;
			size_t count = entry.second;
			fprintf(fp, "    0x%08x, %d,\n", key, count);
		}
		fputs("};\n", fp);
		fclose(fp);
	}
}

// 入力バイト列の文字コードをスコアリング方式で推定する。
// 手順：
//   1. 入力を UTF-8 / EUC-JP / ISO-2022-JP の 3 通りで UTF-16 に変換する。
//   2. 各変換結果に対して jp_validation_count を用いたビグラムスコアを計算する。
//      スコアは「非 ASCII 文字対の平均出現頻度」であり、日本語として自然なほど高くなる。
//   3. 最高スコアの文字コードを採用する。全スコアが 0 の場合は空文字列を返す。
std::string detect_charaset(std::string_view v)
{
	std::u16string u16_from_utf8 = convert_utf8_to_utf16(v);
	std::u16string u16_from_eucjp = convert_eucjp_to_utf16(v);
	std::u16string u16_from_sjis = convert_sjis_to_utf16(v);
	std::u16string u16_from_iso2022jp = convert_iso2022jp_to_utf16(v);

	// UTF-16 文字列の日本語らしさスコアを計算するラムダ。
	// 非 ASCII 文字対（ビグラム）ごとに jp_validation_count を加算し、
	// 対の総数で割った平均値を返す。
	auto Validate = [](std::u16string const &utf16) -> float {
		float score = 0;
		size_t count = 0;

		Pair pair;
		for (size_t i = 0; i < utf16.size(); i++) {
			pair.leading = pair.trailing;
			pair.trailing = utf16[i];
			if (pair.leading >= 0x100 && pair.trailing >= 0x100) {
				uint32_t key = (pair.leading << 16) | pair.trailing;
				score += jp_validation_count(key);
				count++;
			}
		}
		if (count > 0) {
			score /= count;
		}
		return score;
	};

	std::vector<std::pair<std::string, float>> scores;
	scores.emplace_back("UTF-8", Validate(u16_from_utf8));
	scores.emplace_back("EUC-JP", Validate(u16_from_eucjp));
	scores.emplace_back("Shift_JIS", Validate(u16_from_sjis));
	scores.emplace_back("ISO-2022-JP", Validate(u16_from_iso2022jp));
	std::sort(scores.begin(), scores.end(), [](const auto &a, const auto &b) {
		return a.second > b.second;
	});
	if (scores.front().second > 0) {
		return scores.front().first;
	}
	return {};
}

// iconv を使って EUC-JP → Unicode 変換テーブルを生成し eucjp.table に書き出す。
// 1バイト（0x80–0xFF）、2バイト（0x8000–0xFFFF）、
// SS3 プレフィックス付き 3バイト（0x8F0000–）の全コードを列挙して変換する。
// 生成したテーブルは convert_eucjp_to_unicode で使用される。
// このツール自体のビルド時には呼び出されず、テーブル再生成時のみ使用する。
static void save_eucjp_table()
{
	std::map<uint32_t, uint32_t> eucjp_to_unicode_map;
	iconv_t cd = iconv_open("UTF-8", "EUC-JP");
	if (cd != (iconv_t)-1) {
		char in[100];
		char out[100];
		auto Convert = [&]() -> uint32_t {
			char *in_ptr = in;
			char *out_ptr = out;
			size_t in_bytes = strlen(in);
			size_t out_bytes = sizeof(out);
			size_t result = iconv(cd, &in_ptr, &in_bytes, &out_ptr, &out_bytes);
			if (result != (size_t)-1) {
				size_t n = sizeof(out) - out_bytes;
				out[n] = 0;
				std::u16string u = convert_utf8_to_utf16(out);
				if (u.size() == 1) {
					return u.c_str()[0];
				} else if (u.size() == 2) {
					return (u.c_str()[0] << 16) | u.c_str()[1];
				}
				return 0;
			}
			return 0;
		};
		for (uint32_t c = 0x80; c < 0x100; c++) {
			in[0] = c;
			in[1] = 0;
			uint32_t u = Convert();
			if (u != 0) {
				eucjp_to_unicode_map[c] = u;
			}
		}
		for (uint32_t c = 0x8000; c < 0x10000; c++) {
			in[0] = (c >> 8) & 0xff;
			in[1] = c & 0xff;
			in[2] = 0;
			if (in[1] == 0) continue;
			uint32_t u = Convert();
			if (u != 0) {
				eucjp_to_unicode_map[c] = u;
			}
		}
		for (uint32_t c = 0x8000; c < 0x10000; c++) {
			in[0] = 0x8f;
			in[1] = (c >> 8) & 0xff;
			in[2] = c & 0xff;
			in[3] = 0;
			if (in[2] == 0) continue;
			uint32_t u = Convert();
			if (u != 0) {
				eucjp_to_unicode_map[(0x8f << 16) | c] = u;
			}
		}
	}
	// std::map<uint32_t, uint32_t> eucjp_to_unicode_map;
	std::map<uint32_t, uint32_t> unicode_eucjp_to_map;
	for (const auto &pair : eucjp_to_unicode_map) {
		unicode_eucjp_to_map[pair.second] = pair.first;
	}

	{
		FILE *fp = fopen("eucjp.table", "w");
		if (fp) {
			fputs(R"---(struct {
    uint32_t eucjp;
    uint32_t unicode;
} eucjp_to_unicode_table[] = {
)---",
				fp);
			for (auto const &pair : eucjp_to_unicode_map) {
				fprintf(fp, "    0x%04x, 0x%04x,\n", pair.first, pair.second);
			}
			fputs("};\n", fp);
			fflush(fp);

			fputs(R"---(struct {
    uint32_t eucjp;
    uint32_t unicode;
} unicode_to_eucjp_table[] = {
)---",
				fp);
			for (auto const &pair : unicode_eucjp_to_map) {
				fprintf(fp, "    0x%04x, 0x%04x,\n", pair.first, pair.second);
			}
			fputs("};\n", fp);
			fclose(fp);
		}
	}
}
