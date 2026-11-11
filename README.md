# DeepseekTest

一个用 C++17 和 libcurl 编写的命令行聊天客户端。它向 OpenAI 兼容的 Chat Completions 接口发送单轮请求，支持 HTTPS 云端服务，也可以连接本机 Ollama。

## 构建

安装 C++17 编译器、CMake 和 libcurl 开发库后，在仓库目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Windows 也可以从 PowerShell 运行 `.\build.ps1`。启动构建生成的 `DeepseekTest` 或 `DeepseekTest.exe`，依次输入接口地址、模型名和 API Key，再输入问题。

## 本地试用

1. 启动 Ollama，并准备本机已下载的模型。
2. 接口地址填 `http://127.0.0.1:11434/v1/chat/completions`，模型名填本机模型名。
3. API Key 直接回车，输入一句话测试回复。

云端请求校验证书和主机名。模型名以所选服务提供的配置为准。项目用于练习 HTTP 请求与响应处理，当前支持非流式、单轮问答。

[请求实现](src/main.cpp) · [构建配置](CMakeLists.txt)
