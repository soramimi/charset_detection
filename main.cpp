
#include "charset_detection.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#define O_BINARY 0
#endif

int main(int argc, char **argv)
{
	// if (0) ブロック内の save_eucjp_table() はテーブル再生成用のコードで、
	// 通常のビルドでは実行されない。
	if (0) {
		// save_eucjp_table();
		return 0;
	}
	// コマンドライン引数でファイル名を指定しなかった場合はデフォルトファイルを使用する
	char const *input = (argc > 1) ? argv[1] : "input.utf8.txt";
	{
		int fd = open(input, O_RDONLY | O_BINARY);
		if (fd != -1) {
			struct stat st;
			if (fstat(fd, &st) == 0 && st.st_size > 0) {
				std::vector<char> buf(st.st_size);
				if (read(fd, buf.data(), buf.size()) == st.st_size) {
					std::string_view v(buf.data(), buf.size());
					std::string charset = detect_charaset(v);
					puts(charset.empty() ? "unknown" : charset.c_str());
				}
			}
			close(fd);
		}
	}
	
	return 0;
}
