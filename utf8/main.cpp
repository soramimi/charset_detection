#include <cstdio>
#include <string>
#include <vector>
#include "../charset_detection.h"
#include "../unicode_conversion.h"

int main(int argc, char **argv)
{
	struct Option {
		bool overwrite = false;
		std::vector<std::string> inputs;
		std::string output;
	};
	
	Option opt;
	bool error = false;
	
	int argi = 1;
	while (argi < argc) {
		std::string_view arg = argv[argi++];
		if (arg[0] == '-') {
			if (arg == "-i") {
				if (!opt.inputs.empty()) {
					fprintf(stderr, "error: cannot use -o and -i together\n");
					error = true;
				}
				opt.overwrite = true;
				continue;
			}
			if (arg == "-o") {
				if (argi < argc) {
					if (opt.overwrite) {
						fprintf(stderr, "error: cannot use -o and -i together\n");
						error = true;
					} else if (!opt.output.empty()) {
						fprintf(stderr, "error: multiple -o options specified\n");
						error = true;
					} else {
						opt.output = argv[argi++];
					}
				} else {
					fprintf(stderr, "warning: missing argument for -o\n");
				}
				continue;
			}
			fprintf(stderr, "error: invalid argument: %s\n", arg.data());
			error = true;
		} else {
			opt.inputs.emplace_back(arg);
		}
	}
	
	if (opt.inputs.size() != 1 && !opt.output.empty()) {
		fprintf(stderr, "error: cannot specify output file when multiple input files are given\n");
		error = true;
	}
	
	if (opt.inputs.empty()) {
		fprintf(stderr, "usage: %s [-i | -o output_file] [input_file ...]\n", argv[0]);
		return 0;
	}	
	
	if (error) return 1;
	
	auto Convert = [&](std::string_view v){
		std::string s = detect_charaset(v);
		if (s == "UTF-8") {
			s = v;
		} else if (s == "EUC-JP") {
			s = convert_utf16_to_utf8(convert_eucjp_to_utf16(v));
		} else if (s == "Shift_JIS") {
			s = convert_utf16_to_utf8(convert_sjis_to_utf16(v));
		} else if (s == "ISO-2022-JP") {
			s = convert_utf16_to_utf8(convert_iso2022jp_to_utf16(v));
		} else {
			s = v;
		}
		return s;
	};
	
	if (opt.inputs.empty()) {
		std::vector<char> input;
		char buf[4096];
		while (1) {
			int n = fread(buf, 1, sizeof(buf), stdin);
			if (n <= 0) break;
			input.insert(input.end(), buf, buf + n);
		}
		std::string_view v(input.data(), input.size());
		std::string s = Convert(v);
		fwrite(s.c_str(), 1, s.size(), stdout);
	} else {
		for (std::string const &file : opt.inputs) {
			FILE *fp = fopen(file.c_str(), "rb");
			if (!fp) {
				fprintf(stderr, "warning: failed to open file: %s\n", file.c_str());
				continue;
			}
			std::vector<char> input;
			char buf[4096];
			while (1) {
				int n = fread(buf, 1, sizeof(buf), fp);
				if (n <= 0) break;
				input.insert(input.end(), buf, buf + n);
			}
			fclose(fp);
			
			std::string_view v(input.data(), input.size());
			std::string s = Convert(v);
			
			char const *path = nullptr;
			if (opt.overwrite) {
				path = file.c_str();
			} else if (!opt.output.empty()) {
				path = opt.output.c_str();
			}
			if (path) {
				fp = fopen(path, "wb");
				if (!fp) {
					fprintf(stderr, "warning: failed to open file for writing: %s\n", file.c_str());
					continue;
				}
				fwrite(s.c_str(), 1, s.size(), fp);
				fclose(fp);
			} else {
				fwrite(s.c_str(), 1, s.size(), stdout);
			}
		}
	}
	
	return 0;
}
