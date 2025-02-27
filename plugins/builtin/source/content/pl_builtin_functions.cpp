#include <hex/api/content_registry.hpp>

#include <hex/helpers/fmt.hpp>
#include <hex/helpers/net.hpp>
#include <hex/helpers/file.hpp>
#include <hex/helpers/utils.hpp>

#include <pl/token.hpp>
#include <pl/log_console.hpp>
#include <pl/evaluator.hpp>
#include <pl/patterns/pattern.hpp>

#include <vector>

#include <fmt/args.h>

namespace hex::plugin::builtin {

    std::string format(const auto &params) {
        auto format = pl::Token::literalToString(params[0], true);
        std::string message;

        fmt::dynamic_format_arg_store<fmt::format_context> formatArgs;

        for (u32 i = 1; i < params.size(); i++) {
            auto &param = params[i];

            std::visit(overloaded {
                           [&](pl::Pattern *value) {
                               formatArgs.push_back(value->toString());
                           },
                           [&](auto &&value) {
                               formatArgs.push_back(value);
                           } },
                param);
        }

        try {
            return fmt::vformat(format, formatArgs);
        } catch (fmt::format_error &error) {
            pl::LogConsole::abortEvaluation(hex::format("format error: {}", error.what()));
        }
    }

    void registerPatternLanguageFunctions() {
        using namespace pl;
        using FunctionParameterCount = pl::api::FunctionParameterCount;

        pl::api::Namespace nsStd = { "builtin", "std" };
        {
            /* print(format, args...) */
            ContentRegistry::PatternLanguage::addFunction(nsStd, "print", FunctionParameterCount::moreThan(0), [](Evaluator *ctx, auto params) -> std::optional<Token::Literal> {
                ctx->getConsole().log(LogConsole::Level::Info, format(params));

                return std::nullopt;
            });

            /* format(format, args...) */
            ContentRegistry::PatternLanguage::addFunction(nsStd, "format", FunctionParameterCount::moreThan(0), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return format(params);
            });

            /* env(name) */
            ContentRegistry::PatternLanguage::addFunction(nsStd, "env", FunctionParameterCount::exactly(1), [](Evaluator *ctx, auto params) -> std::optional<Token::Literal> {
                auto name = Token::literalToString(params[0], false);

                auto env = ctx->getEnvVariable(name);
                if (env)
                    return env;
                else {
                    ctx->getConsole().log(LogConsole::Level::Warning, hex::format("environment variable '{}' does not exist", name));
                    return "";
                }
            });

            /* pack_size(...) */
            ContentRegistry::PatternLanguage::addFunction(nsStd, "sizeof_pack", FunctionParameterCount::atLeast(0), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return u128(params.size());
            });

            /* error(message) */
            ContentRegistry::PatternLanguage::addFunction(nsStd, "error", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                LogConsole::abortEvaluation(Token::literalToString(params[0], true));

                return std::nullopt;
            });

            /* warning(message) */
            ContentRegistry::PatternLanguage::addFunction(nsStd, "warning", FunctionParameterCount::exactly(1), [](Evaluator *ctx, auto params) -> std::optional<Token::Literal> {
                ctx->getConsole().log(LogConsole::Level::Warning, Token::literalToString(params[0], true));

                return std::nullopt;
            });
        }

        api::Namespace nsStdMem = { "builtin", "std", "mem" };
        {

            /* base_address() */
            ContentRegistry::PatternLanguage::addFunction(nsStdMem, "base_address", FunctionParameterCount::none(), [](Evaluator *ctx, auto params) -> std::optional<Token::Literal> {
                hex::unused(params);

                return u128(ctx->getDataBaseAddress());
            });

            /* size() */
            ContentRegistry::PatternLanguage::addFunction(nsStdMem, "size", FunctionParameterCount::none(), [](Evaluator *ctx, auto params) -> std::optional<Token::Literal> {
                hex::unused(params);

                return u128(ctx->getDataSize());
            });

            /* find_sequence_in_range(occurrence_index, start_offset, end_offset, bytes...) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMem, "find_sequence_in_range", FunctionParameterCount::moreThan(3), [](Evaluator *ctx, auto params) -> std::optional<Token::Literal> {
                auto occurrenceIndex = Token::literalToUnsigned(params[0]);
                auto offsetFrom      = Token::literalToUnsigned(params[1]);
                auto offsetTo        = Token::literalToUnsigned(params[2]);

                std::vector<u8> sequence;
                for (u32 i = 3; i < params.size(); i++) {
                    auto byte = Token::literalToUnsigned(params[i]);

                    if (byte > 0xFF)
                        LogConsole::abortEvaluation(hex::format("byte #{} value out of range: {} > 0xFF", i, u64(byte)));

                    sequence.push_back(u8(byte & 0xFF));
                }

                std::vector<u8> bytes(sequence.size(), 0x00);
                u32 occurrences      = 0;
                const u64 bufferSize = ctx->getDataSize();
                const u64 endOffset  = offsetTo <= offsetFrom ? bufferSize : std::min(bufferSize, u64(offsetTo));
                for (u64 offset = offsetFrom; offset < endOffset - sequence.size(); offset++) {
                    ctx->readData(offset, bytes.data(), bytes.size());

                    if (bytes == sequence) {
                        if (occurrences < occurrenceIndex) {
                            occurrences++;
                            continue;
                        }

                        return u128(offset);
                    }
                }

                return i128(-1);
            });

            /* read_unsigned(address, size) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMem, "read_unsigned", FunctionParameterCount::exactly(2), [](Evaluator *ctx, auto params) -> std::optional<Token::Literal> {
                auto address = Token::literalToUnsigned(params[0]);
                auto size    = Token::literalToUnsigned(params[1]);

                if (size > 16)
                    LogConsole::abortEvaluation("read size out of range");

                u128 result = 0;
                ctx->readData(address, &result, size);

                return result;
            });

            /* read_signed(address, size) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMem, "read_signed", FunctionParameterCount::exactly(2), [](Evaluator *ctx, auto params) -> std::optional<Token::Literal> {
                auto address = Token::literalToUnsigned(params[0]);
                auto size    = Token::literalToUnsigned(params[1]);

                if (size > 16)
                    LogConsole::abortEvaluation("read size out of range");

                i128 value;
                ctx->readData(address, &value, size);
                return hex::signExtend(size * 8, value);
            });

            /* read_string(address, size) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMem, "read_string", FunctionParameterCount::exactly(2), [](Evaluator *ctx, auto params) -> std::optional<Token::Literal> {
                auto address = Token::literalToUnsigned(params[0]);
                auto size    = Token::literalToUnsigned(params[1]);

                std::string result(size, '\x00');
                ctx->readData(address, result.data(), size);

                return result;
            });
        }

        api::Namespace nsStdString = { "builtin", "std", "string" };
        {
            /* length(string) */
            ContentRegistry::PatternLanguage::addFunction(nsStdString, "length", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                auto string = Token::literalToString(params[0], false);

                return u128(string.length());
            });

            /* at(string, index) */
            ContentRegistry::PatternLanguage::addFunction(nsStdString, "at", FunctionParameterCount::exactly(2), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                auto string = Token::literalToString(params[0], false);
                auto index  = Token::literalToSigned(params[1]);

#if defined(OS_MACOS)
                const auto signIndex = index >> (sizeof(index) * 8 - 1);
                const auto absIndex  = (index ^ signIndex) - signIndex;
#else
                    const auto absIndex = std::abs(index);
#endif

                if (absIndex > string.length())
                    LogConsole::abortEvaluation("character index out of range");

                if (index >= 0)
                    return char(string[index]);
                else
                    return char(string[string.length() - -index]);
            });

            /* substr(string, pos, count) */
            ContentRegistry::PatternLanguage::addFunction(nsStdString, "substr", FunctionParameterCount::exactly(3), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                auto string = Token::literalToString(params[0], false);
                auto pos    = Token::literalToUnsigned(params[1]);
                auto size   = Token::literalToUnsigned(params[2]);

                if (pos > string.length())
                    LogConsole::abortEvaluation("character index out of range");

                return string.substr(pos, size);
            });

            /* parse_int(string, base) */
            ContentRegistry::PatternLanguage::addFunction(nsStdString, "parse_int", FunctionParameterCount::exactly(2), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                auto string = Token::literalToString(params[0], false);
                auto base   = Token::literalToUnsigned(params[1]);

                return i128(std::strtoll(string.c_str(), nullptr, base));
            });

            /* parse_float(string) */
            ContentRegistry::PatternLanguage::addFunction(nsStdString, "parse_float", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                auto string = Token::literalToString(params[0], false);

                return double(std::strtod(string.c_str(), nullptr));
            });
        }

        api::Namespace nsStdHttp = { "builtin", "std", "http" };
        {
            /* get(url) */
            ContentRegistry::PatternLanguage::addDangerousFunction(nsStdHttp, "get", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                const auto url = Token::literalToString(params[0], false);

                hex::Net net;
                return net.getString(url).get().body;
            });
        }


        api::Namespace nsStdFile = { "builtin", "std", "file" };
        {
            static u32 fileCounter = 0;
            static std::map<u32, fs::File> openFiles;

            /* open(path, mode) */
            ContentRegistry::PatternLanguage::addDangerousFunction(nsStdFile, "open", FunctionParameterCount::exactly(2), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                const auto path     = Token::literalToString(params[0], false);
                const auto modeEnum = Token::literalToUnsigned(params[1]);

                fs::File::Mode mode;
                switch (modeEnum) {
                    case 1:
                        mode = fs::File::Mode::Read;
                        break;
                    case 2:
                        mode = fs::File::Mode::Write;
                        break;
                    case 3:
                        mode = fs::File::Mode::Create;
                        break;
                    default:
                        LogConsole::abortEvaluation("invalid file open mode");
                }

                fs::File file(path, mode);

                if (!file.isValid())
                    LogConsole::abortEvaluation(hex::format("failed to open file {}", path));

                fileCounter++;
                openFiles.emplace(std::pair { fileCounter, std::move(file) });

                return u128(fileCounter);
            });

            /* close(file) */
            ContentRegistry::PatternLanguage::addDangerousFunction(nsStdFile, "close", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                const auto file = Token::literalToUnsigned(params[0]);

                if (!openFiles.contains(file))
                    LogConsole::abortEvaluation("failed to access invalid file");

                openFiles.erase(file);

                return std::nullopt;
            });

            /* read(file, size) */
            ContentRegistry::PatternLanguage::addDangerousFunction(nsStdFile, "read", FunctionParameterCount::exactly(2), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                const auto file = Token::literalToUnsigned(params[0]);
                const auto size = Token::literalToUnsigned(params[1]);

                if (!openFiles.contains(file))
                    LogConsole::abortEvaluation("failed to access invalid file");

                return openFiles[file].readString(size);
            });

            /* write(file, data) */
            ContentRegistry::PatternLanguage::addDangerousFunction(nsStdFile, "write", FunctionParameterCount::exactly(2), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                const auto file = Token::literalToUnsigned(params[0]);
                const auto data = Token::literalToString(params[1], true);

                if (!openFiles.contains(file))
                    LogConsole::abortEvaluation("failed to access invalid file");

                openFiles[file].write(data);

                return std::nullopt;
            });

            /* seek(file, offset) */
            ContentRegistry::PatternLanguage::addDangerousFunction(nsStdFile, "seek", FunctionParameterCount::exactly(2), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                const auto file   = Token::literalToUnsigned(params[0]);
                const auto offset = Token::literalToUnsigned(params[1]);

                if (!openFiles.contains(file))
                    LogConsole::abortEvaluation("failed to access invalid file");

                openFiles[file].seek(offset);

                return std::nullopt;
            });

            /* size(file) */
            ContentRegistry::PatternLanguage::addDangerousFunction(nsStdFile, "size", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                const auto file = Token::literalToUnsigned(params[0]);

                if (!openFiles.contains(file))
                    LogConsole::abortEvaluation("failed to access invalid file");

                return u128(openFiles[file].getSize());
            });

            /* resize(file, size) */
            ContentRegistry::PatternLanguage::addDangerousFunction(nsStdFile, "resize", FunctionParameterCount::exactly(2), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                const auto file = Token::literalToUnsigned(params[0]);
                const auto size = Token::literalToUnsigned(params[1]);

                if (!openFiles.contains(file))
                    LogConsole::abortEvaluation("failed to access invalid file");

                openFiles[file].setSize(size);

                return std::nullopt;
            });

            /* flush(file) */
            ContentRegistry::PatternLanguage::addDangerousFunction(nsStdFile, "flush", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                const auto file = Token::literalToUnsigned(params[0]);

                if (!openFiles.contains(file))
                    LogConsole::abortEvaluation("failed to access invalid file");

                openFiles[file].flush();

                return std::nullopt;
            });

            /* remove(file) */
            ContentRegistry::PatternLanguage::addDangerousFunction(nsStdFile, "remove", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                const auto file = Token::literalToUnsigned(params[0]);

                if (!openFiles.contains(file))
                    LogConsole::abortEvaluation("failed to access invalid file");

                openFiles[file].remove();

                return std::nullopt;
            });
        }


        api::Namespace nsStdMath = { "builtin", "std", "math" };
        {
            /* floor(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "floor", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::floor(Token::literalToFloatingPoint(params[0]));
            });

            /* ceil(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "ceil", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::ceil(Token::literalToFloatingPoint(params[0]));
            });

            /* round(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "round", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::round(Token::literalToFloatingPoint(params[0]));
            });

            /* trunc(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "trunc", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::trunc(Token::literalToFloatingPoint(params[0]));
            });


            /* log10(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "log10", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::log10(Token::literalToFloatingPoint(params[0]));
            });

            /* log2(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "log2", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::log2(Token::literalToFloatingPoint(params[0]));
            });

            /* ln(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "ln", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::log(Token::literalToFloatingPoint(params[0]));
            });


            /* fmod(x, y) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "fmod", FunctionParameterCount::exactly(2), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::fmod(Token::literalToFloatingPoint(params[0]), Token::literalToFloatingPoint(params[1]));
            });

            /* pow(base, exp) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "pow", FunctionParameterCount::exactly(2), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::pow(Token::literalToFloatingPoint(params[0]), Token::literalToFloatingPoint(params[1]));
            });

            /* sqrt(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "sqrt", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::sqrt(Token::literalToFloatingPoint(params[0]));
            });

            /* cbrt(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "cbrt", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::cbrt(Token::literalToFloatingPoint(params[0]));
            });


            /* sin(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "sin", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::sin(Token::literalToFloatingPoint(params[0]));
            });

            /* cos(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "cos", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::cos(Token::literalToFloatingPoint(params[0]));
            });

            /* tan(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "tan", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::tan(Token::literalToFloatingPoint(params[0]));
            });

            /* asin(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "asin", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::asin(Token::literalToFloatingPoint(params[0]));
            });

            /* acos(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "acos", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::acos(Token::literalToFloatingPoint(params[0]));
            });

            /* atan(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "atan", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::atan(Token::literalToFloatingPoint(params[0]));
            });

            /* atan2(y, x) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "atan", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::atan2(Token::literalToFloatingPoint(params[0]), Token::literalToFloatingPoint(params[1]));
            });


            /* sinh(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "sinh", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::sinh(Token::literalToFloatingPoint(params[0]));
            });

            /* cosh(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "cosh", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::cosh(Token::literalToFloatingPoint(params[0]));
            });

            /* tanh(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "tanh", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::tanh(Token::literalToFloatingPoint(params[0]));
            });

            /* asinh(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "asinh", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::asinh(Token::literalToFloatingPoint(params[0]));
            });

            /* acosh(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "acosh", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::acosh(Token::literalToFloatingPoint(params[0]));
            });

            /* atanh(value) */
            ContentRegistry::PatternLanguage::addFunction(nsStdMath, "atanh", FunctionParameterCount::exactly(1), [](Evaluator *, auto params) -> std::optional<Token::Literal> {
                return std::atanh(Token::literalToFloatingPoint(params[0]));
            });
        }
    }
}
