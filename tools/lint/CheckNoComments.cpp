#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
std::size_t skipSplices(std::string_view text, std::size_t pos) {
    while (pos < text.size() && text[pos] == '\\') {
        if (pos + 1 < text.size() && text[pos + 1] == '\n') {
            pos += 2;
        } else if (pos + 2 < text.size() && text[pos + 1] == '\r' && text[pos + 2] == '\n') {
            pos += 3;
        } else {
            break;
        }
    }
    return pos;
}

char take(std::string_view text, std::size_t& pos) {
    pos = skipSplices(text, pos);
    return pos < text.size() ? text[pos++] : '\0';
}

bool identifier(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool hasComment(std::string_view text) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        const char c = take(text, pos);
        if (c == '/') {
            auto next = pos;
            const char following = take(text, next);
            if (following == '/' || following == '*') {
                return true;
            }
        }
        if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
            std::string token(1, c);
            while (true) {
                auto next = pos;
                const char following = take(text, next);
                if (!identifier(following)) {
                    break;
                }
                token += following;
                pos = next;
            }
            auto next = pos;
            if ((token == "R" || token == "u8R" || token == "uR" || token == "UR" || token == "LR")
                && take(text, next) == '"') {
                const auto open = text.find('(', next);
                if (open == std::string_view::npos || open - next > 16) {
                    throw std::runtime_error("Invalid raw literal delimiter");
                }
                const auto delimiter = text.substr(next, open - next);
                if (delimiter.find_first_of(" \t\r\n\\)") != std::string_view::npos) {
                    throw std::runtime_error("Invalid raw literal delimiter");
                }
                const std::string end = ")" + std::string(delimiter) + "\"";
                const auto close = text.find(end, open + 1);
                if (close == std::string_view::npos) {
                    throw std::runtime_error("Unterminated raw literal");
                }
                pos = close + end.size();
            }
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            char previous = c;
            while (true) {
                auto next = pos;
                const char following = take(text, next);
                if (!identifier(following) && following != '.' && following != '\''
                    && !((following == '+' || following == '-')
                         && (previous == 'e' || previous == 'E' || previous == 'p' || previous == 'P'))) {
                    break;
                }
                previous = following;
                pos = next;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            bool closed = false;
            while (pos < text.size()) {
                const char current = take(text, pos);
                if (current == '\\') {
                    take(text, pos);
                } else if (current == c) {
                    closed = true;
                    break;
                }
            }
            if (!closed) {
                throw std::runtime_error("Unterminated literal");
            }
        }
    }
    return false;
}

int selfTest() {
    const std::string line(2, '/');
    const std::string block = std::string(1, '/') + '*';
    const std::pair<std::string, bool> cases[] = {
        {"int value = 1;", false},
        {line + " forbidden", true},
        {block + " forbidden *" + '/', true},
        {"auto url = \"https:" + line + "example.test\";", false},
        {"auto raw = u8R\"tag(" + line + block + ")tag\";", false},
        {"auto raw = R\"(" + line + ")\";" + line, true},
        {"auto c = '/'; auto n = 1'000;" + line, true},
        {std::string("/") + '\\' + '\n' + '/', true},
        {std::string("/") + '\\' + "\r\n" + '*', true},
        {"auto raw = R\"tag(a)ta" + std::string(1, '\\') + "\ng\"" + line + ")tag\";", false},
        {"auto quote = \"a\\\"" + line + "b\";", false},
        {"auto c = '\\'';", false},
        {"auto n = 0x1'ff;", false},
        {"auto raw = LR\"(" + block + ")\";" + block, true}
    };
    for (std::size_t i = 0; i < std::size(cases); ++i) {
        if (hasComment(cases[i].first) != cases[i].second) {
            std::cerr << "Scanner case " << i << " failed\n";
            return 1;
        }
    }
    return 0;
}

bool checkFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot read " + path.string());
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (hasComment(text)) {
        std::cerr << path.string() << ": code comment forbidden\n";
        return false;
    }
    return true;
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
            return selfTest();
        }
        if (argc != 2) {
            std::cerr << "Usage: prism_check_comments <root|--self-test>\n";
            return 2;
        }
        const std::filesystem::path root(argv[1]);
        if (!std::filesystem::is_directory(root)) {
            throw std::runtime_error("Source root does not exist");
        }
        bool clean = true;
        for (const auto* directory : {"src", "shaders", "app", "bench", "tests", "tools"}) {
            const auto path = root / directory;
            if (!std::filesystem::exists(path)) {
                continue;
            }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                const auto extension = entry.path().extension().string();
                if (extension == ".h" || extension == ".hpp" || extension == ".cpp"
                    || extension == ".c" || extension == ".mm" || extension == ".m"
                    || extension == ".vert" || extension == ".frag" || extension == ".comp"
                    || extension == ".glsl" || extension == ".metal" || extension == ".hlsl"
                    || extension == ".wgsl" || extension == ".inl") {
                    clean = checkFile(entry.path()) && clean;
                }
            }
        }
        return clean ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
