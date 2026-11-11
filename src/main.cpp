/**
 * @file main.cpp
 * @brief 通用 AI 对话客户端（兼容所有 OpenAI 兼容 API）
 *
 * 本程序通过 libcurl 发送 HTTPS 请求到用户配置的 AI API 端点，
 * 实现用户提问并获取 AI 回复的控制台交互功能。
 *
 * 兼容：DeepSeek / OpenAI / Ollama / 通义千问 / 智谱 / 等所有 OpenAI 兼容接口
 *
 * 依赖库：libcurl（用于 HTTPS 网络请求）
 * 编译方式：使用 CMake 构建系统进行编译
 *
 * @author DeepseekTest Project
 * @version 1.0.0
 * @date 2026-06-09
 */

// ============================================================================
// 头文件包含区域
// ============================================================================

// 标准输入输出流，用于控制台输入输出
#include <iostream>
// 标准字符串类，用于存储和操作字符串
#include <string>
// C 风格字符串函数，如 strlen、strcpy 等
#include <cstring>
// 字符判断函数，如 isprint
#include <cctype>

// Windows API 头文件，用于设置控制台编码（仅在 Windows 平台编译）
#ifdef _WIN32
    // 包含 Windows 系统 API（如 SetConsoleOutputCP）
    #include <windows.h>
#endif
// libcurl 核心头文件，提供 HTTP/HTTPS 请求功能
#include <curl/curl.h>
#include <stdlib.h>

// ============================================================================
// 辅助函数声明区域
// ============================================================================

/**
 * @brief libcurl 写回调函数
 * @param contents 指向接收到的数据的指针（可能不是以 null 结尾）
 * @param size     每个数据单元的大小（始终为 1）
 * @param nmemb    数据单元的数量（即接收到的字节数）
 * @param userp    用户自定义指针，指向存储响应数据的 std::string 对象
 * @return size_t  实际处理的字节数，返回非总字节数会中止传输
 *
 * 此函数由 libcurl 内部调用，每次接收到新的响应数据块时触发。
 * 它将数据追加到 userp 指向的 std::string 中，实现响应数据的累积存储。
 */
static size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    // 计算本次接收到的总字节数（size * nmemb）
    size_t totalSize = size * nmemb;

    // 将 userp 转换为 std::string 指针，用于累积存储响应数据
    std::string* responseStr = static_cast<std::string*>(userp);

    // 将接收到的数据追加到响应字符串末尾
    // static_cast<char*>(contents)：将 void* 转为 char* 以便字符串操作
    // .append(ptr, len)：追加指定长度的数据，避免依赖 null 终止符
    responseStr->append(static_cast<char*>(contents), totalSize);

    // 返回已处理的字节数，告知 libcurl 回调执行成功
    return totalSize;
}

/**
 * @brief 从 JSON 字符串中提取指定键后面的字符串值
 * @param json 完整的 JSON 响应字符串
 * @param key  要查找的 JSON 键名（不含引号）
 * @return std::string 提取到的值字符串；若未找到则返回空字符串
 *
 * 简易 JSON 提取器，专门用于解析 DeepSeek API 的响应格式。
 * 通过查找 "\"key\":\"" 模式定位目标值，并处理字符串中的转义字符。
 *
 * 注意：此函数是专门为 DeepSeek API 响应格式设计的简易解析器，
 * 不支持嵌套对象和数组的完整 JSON 解析。
 */
static std::string ExtractJsonString(const std::string& json, const std::string& key)
{
    const std::string quotedKey = "\"" + key + "\"";
    size_t startPos = json.find(quotedKey);
    if (startPos == std::string::npos)
    {
        return "";
    }

    startPos = json.find_first_not_of(" \t\r\n", startPos + quotedKey.size());
    if (startPos == std::string::npos || json[startPos] != ':')
    {
        return "";
    }

    startPos = json.find_first_not_of(" \t\r\n", startPos + 1);
    if (startPos == std::string::npos || json[startPos] != '"')
    {
        return "";
    }
    ++startPos;

    // 用于存储提取结果的字符串
    std::string result;

    // 从值的起始位置开始逐字符解析，直到遇到未转义的双引号
    for (size_t i = startPos; i < json.length(); ++i)
    {
        // 获取当前字符
        char ch = json[i];

        // 检查是否为转义字符反斜杠
        if (ch == '\\')
        {
            // 确保反斜杠后面还有字符，防止越界访问
            if (i + 1 < json.length())
            {
                // 获取反斜杠后面的转义目标字符
                char nextCh = json[i + 1];

                // 根据转义类型处理不同的转义序列
                switch (nextCh)
                {
                case '"':  // 转义双引号 \"
                    result += '"';     // 添加实际的双引号字符
                    break;
                case '\\': // 转义反斜杠 \\
                    result += '\\';    // 添加实际的反斜杠字符
                    break;
                case 'n':  // 转义换行 \n
                    result += '\n';    // 添加实际的换行符
                    break;
                case 't':  // 转义制表符 \t
                    result += '\t';    // 添加实际的制表符
                    break;
                case 'r':  // 转义回车 \r
                    result += '\r';    // 添加实际的回车符
                    break;
                default:   // 其他未知转义序列
                    // 保持原样：保留反斜杠和后面的字符
                    result += ch;
                    result += nextCh;
                    break;
                }

                // 跳过已处理的转义序列中的第二个字符（反斜杠后的字符）
                ++i;
            }
            else
            {
                // 反斜杠是字符串最后一个字符（异常情况），直接保留
                result += ch;
            }
        }
        // 检查是否为未转义的双引号（表示值的结束）
        else if (ch == '"')
        {
            // 遇到值结束标记，停止解析
            break;
        }
        else
        {
            // 普通字符，直接追加到结果中
            result += ch;
        }
    }

    // 返回提取到的字符串值
    return result;
}

// 将用户输入编码成合法的 JSON 字符串内容。
static std::string EscapeJson(const std::string& value)
{
    static const char hex[] = "0123456789abcdef";
    std::string escaped;
    for (unsigned char ch : value)
    {
        if (ch == '"' || ch == '\\')
        {
            escaped += '\\';
            escaped += static_cast<char>(ch);
        }
        else if (ch < 0x20)
        {
            escaped += "\\u00";
            escaped += hex[ch >> 4];
            escaped += hex[ch & 0x0f];
        }
        else
        {
            escaped += static_cast<char>(ch);
        }
    }
    return escaped;
}

/**
 * @brief 发送聊天请求到 AI API 并获取回复（兼容所有 OpenAI 兼容接口）
 * @param apiEndpoint  API 端点 URL（如 https://api.deepseek.com/chat/completions）
 * @param apiKey       API 密钥（用于身份验证）
 * @param modelName    模型名称（如 deepseek-chat、gpt-4o、qwen-plus）
 * @param question     用户输入的问题文本
 * @return std::string AI 返回的回复内容；失败时返回空字符串
 *
 * 使用 libcurl 向指定的 API 端点发送 POST 请求，
 * 请求体为包含用户消息的 JSON 格式数据。
 * 从响应中提取 AI 助手的回复内容并返回。
 *
 * 兼容所有 OpenAI 兼容 API：DeepSeek、OpenAI、Ollama、通义千问、智谱等。
 *
 * 异常处理：网络错误、API 错误响应均通过 stderr 输出错误信息。
 */
static std::string SendToAI(const std::string& apiEndpoint,
                            const std::string& apiKey,
                            const std::string& modelName,
                            const std::string& question)
{
    if (apiEndpoint.rfind("https://", 0) != 0 &&
        (!apiKey.empty() || apiEndpoint.rfind("http://127.0.0.1:", 0) != 0))
    {
        std::cerr << "[错误] 云端端点需要 HTTPS。无密钥的本地 Ollama 可使用 http://127.0.0.1:端口。" << std::endl;
        return "";
    }

    // 初始化 libcurl 全局环境（线程安全，可多次调用）
    // 必须在调用任何其他 curl 函数之前调用
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // 创建 CURL 会话句柄，所有 curl 操作都通过此句柄进行
    CURL* curl = curl_easy_init();

    // 用于存储 API 返回的完整 JSON 响应数据
    std::string responseData;

    // 如果 CURL 句柄创建成功，则配置请求参数
    if (curl)
    {
        // ----- 配置 CURL 请求选项 -----

        // 设置目标 URL：用户指定的 API 端点
        curl_easy_setopt(curl, CURLOPT_URL, apiEndpoint.c_str());

        // 设置 HTTP 请求方法为 POST（默认是 GET）
        curl_easy_setopt(curl, CURLOPT_POST, 1L);

        // ----- 构建 JSON 请求体 -----
        // 使用原始字符串拼接方式构建 JSON，兼容所有 OpenAI 兼容 API
        std::string postData = "{"
            "\"model\": \"" + EscapeJson(modelName) + "\","                  // 用户指定的模型名称
            "\"messages\": [{"                                   // 消息数组开始
                "\"role\": \"user\","                            // 消息角色为用户
                "\"content\": \"" + EscapeJson(question) + "\""              // 用户输入的问题内容
            "}],"
            "\"stream\": false"                                  // 不使用流式输出
            "}";

        // 将请求体数据传递给 libcurl，用于 POST 请求的 body
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());

        // 设置请求体的大小（字节数）
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)postData.size());

        // ----- 设置 HTTP 请求头 -----
        // 创建 libcurl 链表用于存放自定义请求头
        struct curl_slist* headers = nullptr;

        // 添加 Content-Type 头：告知服务器请求体为 JSON 格式
        headers = curl_slist_append(headers, "Content-Type: application/json");

        // 如果 API Key 不为空，添加 Authorization 头
        // Ollama 等本地部署模型无需认证，此时跳过
        if (!apiKey.empty())
        {
            // 添加 Authorization 头：使用 Bearer Token 方式进行身份验证
            std::string authHeader = "Authorization: Bearer " + apiKey;
            headers = curl_slist_append(headers, authHeader.c_str());
        }

        // 将构建好的请求头链表应用到当前 CURL 会话
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // ----- 设置响应数据处理 -----
        // 设置写回调函数：libcurl 收到响应数据时调用此函数
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);

        // 设置写回调函数的用户自定义参数：传入 responseData 的地址
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);

        // 验证 HTTPS 证书和主机名。
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        // 设置请求超时时间（秒）：超过此时间未响应则中止
        // 60 秒足够大多数 AI 对话生成完成
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

        // ----- 执行 HTTP 请求 -----
        // 此调用会阻塞直到请求完成、超时或出错
        CURLcode res = curl_easy_perform(curl);

        // 检查请求是否成功
        if (res != CURLE_OK)
        {
            // 向标准错误输出流打印错误信息
            // curl_easy_strerror 将 CURLcode 转为可读的错误描述字符串
            std::cerr << "[错误] 网络请求失败: " << curl_easy_strerror(res) << std::endl;

            // 清理请求头链表，释放内存
            curl_slist_free_all(headers);

            // 销毁 CURL 会话句柄，释放相关资源
            curl_easy_cleanup(curl);

            // 清理 libcurl 全局环境
            // 返回空字符串表示请求失败
            return "";
        }

        // ----- 提取 API 响应中的回复内容 -----
        // 从 JSON 响应中提取 AI 助手的回复文本
        std::string reply = ExtractJsonString(responseData, "content");

        // 检查是否成功提取到回复内容
        if (reply.empty())
        {
            // 尝试提取错误信息（如 API key 无效、配额不足等）
            std::string errorMsg = ExtractJsonString(responseData, "message");

            // 如果错误信息也为空，表示可能是未知的响应格式
            if (errorMsg.empty())
            {
                // 输出完整的原始响应用于调试
                std::cerr << "[错误] 无法解析 API 响应。原始数据：" << std::endl;
                std::cerr << responseData << std::endl;
            }
            else
            {
                // 输出从 API 返回的具体错误信息
                std::cerr << "[错误] API 返回错误: " << errorMsg << std::endl;
            }

            // 清理资源（与成功路径相同）
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            // 返回空字符串表示提取失败
            return "";
        }

        // ----- 清理 libcurl 资源 -----
        // 释放自定义请求头链表的内存
        curl_slist_free_all(headers);

        // 销毁 CURL 会话句柄
        curl_easy_cleanup(curl);

        // 清理 libcurl 全局环境
        // 返回提取到的 AI 回复文本
        return reply;
    }

    // CURL 句柄创建失败时，输出错误信息
    std::cerr << "[错误] 无法初始化 libcurl 会话。" << std::endl;

    // 清理 libcurl 全局环境
    // 返回空字符串表示初始化失败
    return "";
}

/**
 * @brief 程序主入口函数
 * @return int 程序退出码（0 表示正常退出）
 *
 * 程序流程：
 * 1. 引导用户配置 API 连接信息（端点、模型、API Key，均可直接回车使用默认值）
 * 2. 进入交互循环，用户可以反复输入问题
 * 3. 将问题发送到配置的 AI API 并显示回复
 * 4. 输入 "exit" 或 "quit" 退出程序
 */
int main()
{

    // ====== 步骤零：设置控制台编码为 UTF-8（解决中文乱码问题）======

    // 条件编译：仅在 Windows 平台执行（Linux/macOS 默认已是 UTF-8）
    #ifdef _WIN32
        // 设置控制台输出代码页为 UTF-8（65001）
        // 使 std::cout 输出的中文能正确显示
        SetConsoleOutputCP(65001);

        // 设置控制台输入代码页为 UTF-8（65001）
        // 使 std::cin 能正确读取用户输入的中文
        SetConsoleCP(65001);
    #endif

    // ====== 步骤一：显示程序欢迎信息 ======

    // 输出程序标题和分隔线到控制台
    std::cout << "========================================" << std::endl;
    std::cout << "    通用 AI 对话客户端（OpenAI 兼容）   " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    // ====== 步骤二：配置 API 连接信息（端点、模型、API Key）======

    // 提示用户输入 API 端点 URL（可直接回车使用默认值）
    std::cout << "API 端点 URL [默认: DeepSeek]" << std::endl;
    std::cout << "  常用端点参考:" << std::endl;
    std::cout << "    DeepSeek:  https://api.deepseek.com/chat/completions" << std::endl;
    std::cout << "    OpenAI:    https://api.openai.com/v1/chat/completions" << std::endl;
    std::cout << "    Ollama:    http://localhost:11434/v1/chat/completions" << std::endl;
    std::cout << "    通义千问:  https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions" << std::endl;
    std::cout << "    智谱:      https://open.bigmodel.cn/api/paas/v4/chat/completions" << std::endl;
    std::cout << "  输入 URL: ";

    // 存储用户输入的 API 端点 URL
    std::string apiEndpoint;

    // 读取用户输入
    std::getline(std::cin, apiEndpoint);

    // 如果用户直接回车（空输入），使用 DeepSeek 默认端点
    if (apiEndpoint.empty())
    {
        apiEndpoint = "https://api.deepseek.com/chat/completions";
        std::cout << "  -> 使用默认: DeepSeek" << std::endl;
    }

    std::cout << std::endl;

    // 提示用户输入模型名称（可直接回车使用默认值）
    std::cout << "模型名称 [默认: deepseek-chat]" << std::endl;
    std::cout << "  常用模型参考:" << std::endl;
    std::cout << "    DeepSeek:   deepseek-chat / deepseek-reasoner" << std::endl;
    std::cout << "    OpenAI:     gpt-4o / gpt-4o-mini / o4-mini" << std::endl;
    std::cout << "    Ollama:     llama3 / qwen2.5 / deepseek-r1" << std::endl;
    std::cout << "    通义千问:   qwen-plus / qwen-max" << std::endl;
    std::cout << "    智谱:       glm-4-plus / glm-4-flash" << std::endl;
    std::cout << "  输入模型名: ";

    // 存储用户输入的模型名称
    std::string modelName;

    // 读取用户输入
    std::getline(std::cin, modelName);

    // 如果用户直接回车（空输入），使用默认模型
    if (modelName.empty())
    {
        modelName = "deepseek-chat";
        std::cout << "  -> 使用默认: deepseek-chat" << std::endl;
    }

    std::cout << std::endl;

    // 提示用户输入 API Key（Ollama 本地模型可跳过）
    std::cout << "API Key（使用 Ollama 本地模型可直接回车跳过）: ";

    // 用于存储用户输入的 API Key
    std::string apiKey;

    // 使用 std::getline 从标准输入读取整行（包括空格）
    // 相比 std::cin >>，getline 可以正确处理包含空格的输入
    std::getline(std::cin, apiKey);

    // 只是友好提示，不强制要求（Ollama 等本地部署无需 Key）
    if (apiKey.empty())
    {
        std::cout << "  -> API Key 为空，将不带认证信息发送请求。" << std::endl;
    }

    // ====== 步骤三：显示使用说明 ======

    // 输出空行分隔
    std::cout << std::endl;

    // 显示当前配置摘要
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "当前配置：" << std::endl;
    std::cout << "  端点: " << apiEndpoint << std::endl;
    std::cout << "  模型: " << modelName << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    // 输出使用提示信息
    std::cout << "使用说明：" << std::endl;

    // 提示退出方式
    std::cout << "  - 输入 \"exit\" 或 \"quit\" 或 \"退出\" 退出程序" << std::endl;

    // 提示提问方式
    std::cout << "  - 直接输入问题即可与 AI 对话" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << std::endl;

    // ====== 步骤四：交互式对话循环 ======

    // 使用无限循环保持程序运行，直到用户选择退出
    while (true)
    {
        // 提示用户输入问题
        std::cout << "您: ";

        // 存储用户输入的问题文本
        std::string question;

        // 读取用户输入的一整行文本
        std::getline(std::cin, question);

        // 检查是否为空输入（用户直接按回车）
        if (question.empty())
        {
            // 跳过空输入，继续下一次循环
            continue;
        }

        // ----- 输入预处理：去除首尾空格和制表符 -----
        // 复制原问题字符串用于裁剪（避免修改原始输入）
        std::string trimmed = question;

        // 查找第一个非空白字符的位置
        // find_first_not_of 返回第一个不在给定字符集合中的字符索引
        size_t start = trimmed.find_first_not_of(" \t\r\n");

        // 如果全是空白字符，跳过本次循环
        if (start == std::string::npos)
        {
            continue;
        }

        // 去除前导空白字符
        trimmed = trimmed.substr(start);

        // 查找最后一个非空白字符的位置
        size_t end = trimmed.find_last_not_of(" \t\r\n");

        // 去除尾部空白字符（保留有效内容）
        trimmed = trimmed.substr(0, end + 1);

        // ----- 检查是否为退出命令（不区分大小写）-----
        // 将裁剪后的输入转换为小写用于比较
        std::string lowerQuestion = trimmed;

        // 逐字符转换为小写（ASCII 英文字母范围 A-Z -> a-z）
        for (char& ch : lowerQuestion)
        {
            // 判断是否为大写字母（ASCII 65-90 = 'A'-'Z'）
            if (ch >= 'A' && ch <= 'Z')
            {
                // 转换为小写：大写字母 ASCII 码 + 32 = 对应小写字母
                ch = ch + ('a' - 'A');
            }
        }

        // 比较转换后的字符串是否为退出命令
        if (lowerQuestion == "exit" || lowerQuestion == "quit" || lowerQuestion == "退出")
        {
            // 输出告别信息
            std::cout << "再见！感谢使用。" << std::endl;

            // 跳出循环，程序将正常退出
            break;
        }

        // 输出提示，表示正在等待 AI 回复
        std::cout << "AI: ";

        // 调用核心函数：将问题发送到 AI API 并获取回复
        std::string reply = SendToAI(apiEndpoint, apiKey, modelName, question);

        // 检查是否成功获取回复
        if (!reply.empty())
        {
            // 在控制台输出 AI 的回复内容
            std::cout << reply << std::endl;
        }
        else
        {
            // 请求失败时的提示信息（详细错误已在 SendToAI 中输出）
            std::cout << "[请求失败，请检查网络连接或 API 配置是否正确]" << std::endl;
        }

        // 输出空行分隔不同轮次的对话
        std::cout << std::endl;
    }

    // ====== 步骤五：程序正常退出 ======

    // 清理 libcurl 全局环境
    curl_global_cleanup();

    system("pause");
    // 返回 0 表示程序成功执行完毕
    return 0;
}
