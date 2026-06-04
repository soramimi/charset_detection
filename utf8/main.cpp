#include "../charset_detection.h"
#include "../unicode_conversion.h"
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
	struct Option {
		bool overwrite = false;
		std::vector<std::string> inputs;
		std::string output;
		const bool convert_newline = true;
	};
	
	Option opt;
	bool error = false;
	
	int argi = 1;
	while (argi < argc) {
		std::string_view arg = argv[argi++];
		if (arg[0] == '-') {
			if (arg == "-i") {
				if (!opt.output.empty()) {
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

	std::string_view original;
	std::vector<char> converted;
	bool f_converted = false;
	
	auto Process = [&](){
		f_converted = false;
		converted.clear();
		
		std::string charset = detect_charaset(original);
		
		std::optional<std::string> result;
		if (charset == "UTF-8") {
			// pass
		} else if (charset == "EUC-JP") {
			result = convert_utf16_to_utf8(convert_eucjp_to_utf16(original));
		} else if (charset == "Shift_JIS") {
			result = convert_utf16_to_utf8(convert_sjis_to_utf16(original));
		} else if (charset == "ISO-2022-JP") {
			result = convert_utf16_to_utf8(convert_iso2022jp_to_utf16(original));
		} else {
			return; // unknown charset, do nothing
		}
		
		if (opt.convert_newline) {
			std::vector<std::string_view> lines;
			char const *begin;
			char const *end;
			if (result) {
				begin = result->data();
				end = result->data() + result->size();
			} else {
				begin = original.data();
				end = original.data() + original.size();
			}
			char const *left = begin;
			char const *right = begin;
			while (1) {
				int c = -1;
				if (right < end) {
					c = (unsigned char)*right++;
				}
				if (c == '\n' || c == '\r' || c == -1) {
					lines.emplace_back(left, right - left);
					if (c == '\r' && right + 1 < end && right[1] == '\n') {
						right++;
					}
					if (c == -1) break;
					right++;
					left = right;
				}
			}
			std::string newstr;
			for (std::string_view line : lines) {
				bool lf = false;
				if (!line.empty() && line.back() == '\n') {
					line.remove_suffix(1);
					lf = true;
				}
				if (!line.empty() && line.back() == '\r') {
					line.remove_suffix(1);
					lf = true;
				}
				newstr += line;
				if (lf) {
					newstr += '\n';
				}
			}
			result = newstr;
		}
		
		f_converted = (bool)result;
		if (f_converted && !result->empty()) {
			converted = std::vector<char>(result->data(), result->data() + result->size());
		}
	};
	
	auto Save = [&](FILE *fp){
		if (f_converted) {
			fwrite(converted.data(), 1, converted.size(), fp);
		} else {
			fwrite(original.data(), 1, original.size(), fp);
		}
	};
	
	if (opt.inputs.empty()) {
		std::vector<char> input;
		char buf[4096];
		while (1) {
			int n = fread(buf, 1, sizeof(buf), stdin);
			if (n <= 0) break;
			input.insert(input.end(), buf, buf + n);
		}
		original = {input.data(), input.size()};
		Process();
		Save(stdout);
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
			
			original = {input.data(), input.size()};
			Process();
			
			char const *path = nullptr;
			if (opt.overwrite) {
				if (!f_converted) continue;
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
				Save(fp);
				fclose(fp);
			} else {
				Save(stdout);
			}
		}
	}
	
	return 0;
}
