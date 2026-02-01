#pragma once
#include "uring_types.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

// HTTP请求结构体
struct HttpRequest {
  std::string_view method;
  std::string_view url;
  std::string_view version;
  std::unordered_map<std::string_view, std::string_view> headers;
  std::string_view body;
  size_t content_length;
  bool is_chunked;

  HttpRequest() : content_length(0), is_chunked(false) {}
};

// HTTP响应结构体
struct HttpResponse {
  std::string response_line;
  std::unordered_map<std::string, std::string> headers;
  std::vector<char> body;

  HttpResponse() {
    response_line = "HTTP/1.1 200 OK\r\n";
    headers["Content-Type"] = "text/html; charset=utf-8";
    headers["Connection"] = "keep-alive";
  }
};

class HttpTask {
private:
  // 解析状态
  struct ParseState {
    bool request_line_parsed;
    bool headers_parsed;
    bool body_parsed;
    size_t body_bytes_received;

    ParseState()
        : request_line_parsed(false), headers_parsed(false), body_parsed(false),
          body_bytes_received(0) {}
  };

  HttpRequest request_;
  ParseState parse_state_;
  // 打印请求
  // void printf_request();
  // 解析请求行
  bool parse_request_line(std::string_view data);

  // 解析请求头
  bool parse_request_headers(std::string_view data);

  // 解析请求体
  bool parse_request_body(std::string_view data);

  // 发送简单响应
  void send_simple_response(UringConnectionInfo *info,
                            const HttpResponse &response);

  // 发送文件响应（零拷贝）
  void send_file_response(UringConnectionInfo *info,
                          const std::string &file_path);

  // 处理简单任务
  void handle_task(UringConnectionInfo *info);

public:
  HttpTask() = default;
  // 主处理函数：流式解析和处理HTTP请求
  bool handle_message(UringConnectionInfo *info);

  // 静态方法：判断是否为HTTP任务
  static bool is_http_task(UringConnectionInfo *info);

  // 静态方法：判断HTTP解析是否完成
  static bool is_http_parse_complete(UringConnectionInfo *info);
  // 判断报文是否完整
  static ParseResult is_complete_message(UringConnectionInfo *info);
};