module;

#include <atomic>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <functional>
#include <thread>
#include <mutex>
#include <semaphore>
#include <format>
#include <unordered_map>
#include <unordered_set>
#include <print>
#include <stdexcept>
#include <span>

export module YT:Types;

namespace YT
{
    /// Number of worker threads (including main thread)
    export class Threading
    {
    public:
        static constexpr int NumThreads = 1;
    };

    export template<typename T>
    using Vector = std::vector<T>;

    export using String = std::string;
    export using StringView = std::string_view;

    export template<typename K, typename V>
    using Map = std::unordered_map<K, V>;

    export template<typename T>
    using Set = std::unordered_set<T>;

    export template<typename K, typename V>
    using Pair = std::pair<K, V>;

    export template<typename K, typename V>
    auto MakePair(K && k, V && v) { return std::make_pair(std::forward<decltype(k)>(k), std::forward<decltype(v)>(v)); }

    export template<typename T>
    using OptionalPtr = T*;

    export template<typename T>
    using RequiredPtr = T*;

    export template<typename T>
    using MaybeInvalid = T;

    export template<typename T>
    using OutRef = T&;

    export template<typename T>
    using UniquePtr = std::unique_ptr<T>;

    export template<typename T, typename... Args>
    UniquePtr<T> MakeUnique(Args&&... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    export template <typename T>
    using Optional = std::optional<T>;

    export template<typename T>
    using Function = std::function<T>;

    export template <typename T>
    using Span = std::span<T>;

    export using Mutex = std::mutex;
    export using Thread = std::thread;

    constexpr int MaxInt = std::numeric_limits<int>::max();
    export using Semaphore = std::counting_semaphore<MaxInt>;

    export template<typename... Args>
    String Format(std::format_string<Args...> format, Args&&... args)
    {
        return std::format(format, std::forward<Args>(args)...);
    }

    export template <typename... Args>
    void PrintStr(std::string_view str)
    {
        std::println("{}", str);
    }

    export template <typename... Args>
    void Print(std::format_string<Args...> format, Args&&... args)
    {
        std::println(format, std::forward<Args>(args)...);
    }

    export template <typename... Args>
    void VerbosePrint(std::format_string<Args...> format, Args&&... args)
    {
        std::println(format, std::forward<Args>(args)...);
    }

    export template <typename... Args>
    void FatalPrint(std::format_string<Args...> format, Args&&... args)
    {
        std::println(format, std::forward<Args>(args)...);
        //__builtin_debugtrap();
    }

    export bool WarnCheck(bool condition) noexcept
    {
        if(!condition)
        {
            //__builtin_debugtrap();
        }

        return condition;
    }

    export using Exception = std::runtime_error;

    export struct ApplicationInitInfo final
    {
        StringView m_ApplicationName = "YTApplication";
        int m_ApplicationVersion = 1;

        std::size_t m_ThreadPoolSize = std::thread::hardware_concurrency();
        int m_UpdateRate = 60;
    };

    export struct WindowInitInfo final
    {
        String m_WindowName = "YTWindow";
        std::uint32_t m_Width = 800;
        std::uint32_t m_Height = 600;

        bool m_Fullscreen = false;
        bool m_Resizable = false;

        bool m_AlphaBackground = true;
    };

}
