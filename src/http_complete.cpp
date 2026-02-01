#include "http_complete.h"
#include <chrono>
#include <filesystem>
#include <thread>

// 判断报文是否完整
ParseResult HttpTask::is_complete_message(UringConnectionInfo *info) {
  char *read_head = info->read_buffer.get_read_head();
  size_t read_size = info->read_buffer.get_readable_size();
  if (read_size == 0) {
    return ParseResult::NEEED_MORE_DATA;
  }

  std::string_view request(read_head, read_size);

  // 查找请求头结束标记
  size_t headers_end = request.find("\r\n\r\n");
  if (headers_end == std::string_view::npos) {
    return ParseResult::NEEED_MORE_DATA;
  }

  // 解析Content-Length
  size_t content_length_pos = request.find("Content-Length:");
  if (content_length_pos != std::string_view::npos &&
      content_length_pos < headers_end) {
    size_t content_length_end = request.find("\r\n", content_length_pos);
    if (content_length_end != std::string_view::npos) {
      try {
        std::string content_len_str = std::string(
            request.substr(content_length_pos + 15,
                           content_length_end - (content_length_pos + 15)));
        // 去除空格
        content_len_str.erase(0, content_len_str.find_first_not_of(" \t"));
        content_len_str.erase(content_len_str.find_last_not_of(" \t") + 1);

        size_t content_len = std::stoul(content_len_str);
        size_t total_expected = headers_end + 4 + content_len;

        if (read_size >= total_expected) {
          info->bytes_NO_read = total_expected - read_size;
          return ParseResult::COMPLETE;
        } else {
          info->bytes_NO_read = total_expected - read_size;
          return ParseResult::NEEED_MORE_DATA;
        }
      } catch (const std::exception &e) {
        return ParseResult::INVALID_FORMAT;
      }
    }
  }

  // 检查Transfer-Encoding: chunked
  size_t chunked_pos = request.find("Transfer-Encoding: chunked");
  if (chunked_pos != std::string_view::npos && chunked_pos < headers_end) {
    return ParseResult::CHUNKED_UNSUPPORTED; // 暂不支持chunked编码
  }

  // 没有Content-Length，只有请求头的情况（如GET请求）
  return ParseResult::COMPLETE;
}

// 解析请求行
bool HttpTask::parse_request_line(std::string_view data) {
  size_t line_end = data.find("\r\n");
  if (line_end == std::string_view::npos) {
    return false;
  }

  std::string_view request_line = data.substr(0, line_end);

  // 解析方法
  size_t method_end = request_line.find(' ');
  if (method_end == std::string_view::npos) {
    return false;
  }
  request_.method = request_line.substr(0, method_end);

  // 解析URL
  size_t url_start = method_end + 1;
  size_t url_end = request_line.find(' ', url_start);
  if (url_end == std::string_view::npos) {
    return false;
  }
  request_.url = request_line.substr(url_start, url_end - url_start);

  // 解析HTTP版本
  request_.version = request_line.substr(url_end + 1);

  parse_state_.request_line_parsed = true;
  return true;
}

// 解析请求头
bool HttpTask::parse_request_headers(std::string_view data) {
  size_t headers_start = data.find("\r\n") + 2;
  size_t headers_end = data.find("\r\n\r\n");

  if (headers_end == std::string_view::npos) {
    return false;
  }

  std::string_view headers_data =
      data.substr(headers_start, headers_end - headers_start);

  size_t line_start = 0;
  while (line_start < headers_data.length()) {
    size_t line_end = headers_data.find("\r\n", line_start);
    if (line_end == std::string_view::npos) {
      line_end = headers_data.length();
    }

    std::string_view header_line =
        headers_data.substr(line_start, line_end - line_start);
    size_t colon_pos = header_line.find(':');
    if (colon_pos != std::string_view::npos) {
      std::string_view key = header_line.substr(0, colon_pos);
      std::string_view value = header_line.substr(colon_pos + 1);

      // 去除value前后的空格
      size_t value_start = value.find_first_not_of(" \t");
      size_t value_end = value.find_last_not_of(" \t");
      if (value_start != std::string_view::npos &&
          value_end != std::string_view::npos) {
        value = value.substr(value_start, value_end - value_start + 1);
      }

      request_.headers[key] = value;

      // 处理Content-Length
      if (key == "Content-Length") {
        try {
          request_.content_length = std::stoul(std::string(value));
        } catch (const std::exception &e) {
          return false;
        }
      }

      // 处理Transfer-Encoding
      if (key == "Transfer-Encoding" && value == "chunked") {
        request_.is_chunked = true;
      }
    }

    if (line_end == headers_data.length())
      break;
    line_start = line_end + 2;
  }

  parse_state_.headers_parsed = true;
  return true;
}

// 解析请求体
bool HttpTask::parse_request_body(std::string_view data) {
  size_t body_start = data.find("\r\n\r\n") + 4;

  if (request_.content_length > 0) {
    if (data.length() - body_start >= request_.content_length) {
      request_.body = data.substr(body_start, request_.content_length);
      parse_state_.body_parsed = true;
      return true;
    }
    return false;
  }

  // 没有请求体的情况
  parse_state_.body_parsed = true;
  return true;
}

// 发送简单响应
void HttpTask::send_simple_response(UringConnectionInfo *info,
                                    const HttpResponse &response) {
  std::string headers_str;
  for (const auto &header : response.headers) {
    headers_str += header.first + ": " + header.second + "\r\n";
  }

  // 构建完整的HTTP响应
  std::string full_response = response.response_line + headers_str + "\r\n";

  // 将响应头写入缓冲区
  size_t header_size = full_response.size();
  size_t body_size = response.body.size();
  size_t total_size = header_size + body_size;

  // 确保写缓冲区有足够空间
  if (info->write_buffer.get_writable_size() >= total_size) {
    // 写入响应头
    char *write_tail = info->write_buffer.get_write_tail();
    std::memcpy(write_tail, full_response.c_str(), header_size);
    info->write_buffer.write_data(header_size);

    // 写入响应体
    if (body_size > 0) {
      write_tail = info->write_buffer.get_write_tail();
      std::memcpy(write_tail, response.body.data(), body_size);
      info->write_buffer.write_data(body_size);
    }

    std::cout << "HTTP响应已写入缓冲区，总大小: " << total_size
              << "字节（头:" << header_size << "字节，体:" << body_size
              << "字节）" << std::endl;
    std::cout << "响应状态行: " << response.response_line;
  } else {
    std::cerr << "写缓冲区空间不足，需要" << total_size << "字节，可用"
              << info->write_buffer.get_writable_size() << "字节" << std::endl;
  }
}

// 发送文件响应（零拷贝）
void HttpTask::send_file_response(UringConnectionInfo *info,
                                  const std::string &file_path) {
  // 这里应该使用sendfile或splice等零拷贝技术
  // 简化实现：读取文件到缓冲区
  std::ifstream file(file_path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    // 文件不存在，发送404响应
    HttpResponse response;
    response.response_line = "HTTP/1.1 404 Not Found\r\n";
    std::string body_str = "Not Found";
    response.body.assign(body_str.begin(), body_str.end());
    response.headers["Content-Type"] = "text/plain; charset=utf-8";
    response.headers["Content-Length"] = std::to_string(response.body.size());
    send_simple_response(info, response);
    return;
  }

  size_t file_size = file.tellg();
  file.seekg(0);

  HttpResponse response;
  response.response_line = "HTTP/1.1 200 OK\r\n";
  response.headers["Content-Length"] = std::to_string(file_size);

  // 根据文件扩展名设置Content-Type
  size_t dot_pos = file_path.find_last_of('.');
  if (dot_pos != std::string::npos) {
    std::string ext = file_path.substr(dot_pos + 1);
    if (ext == "html" || ext == "htm") {
      response.headers["Content-Type"] = "text/html; charset=utf-8";
    } else if (ext == "css") {
      response.headers["Content-Type"] = "text/css; charset=utf-8";
    } else if (ext == "js") {
      response.headers["Content-Type"] = "application/javascript";
    } else if (ext == "png") {
      response.headers["Content-Type"] = "image/png";
    } else if (ext == "jpg" || ext == "jpeg") {
      response.headers["Content-Type"] = "image/jpeg";
    }
  }

  // 读取文件内容
  response.body.resize(file_size);
  file.read(response.body.data(), file_size);

  send_simple_response(info, response);
}

// 处理简单任务
void HttpTask::handle_task(UringConnectionInfo *info) {
  HttpResponse response;

  if (request_.method == "GET") {
    // 处理根路径
    if (request_.url == "/") {
      response.response_line = "HTTP/1.1 200 OK\r\n";
      std::string body_str = "Hello World!";
      response.body.assign(body_str.begin(), body_str.end());
    }
    // 处理健康检查
    else if (request_.url == "/health") {
      response.response_line = "HTTP/1.1 200 OK\r\n";
      std::string body_str = "OK";
      response.body.assign(body_str.begin(), body_str.end());
    }
    // 处理静态文件
    else {
      std::string file_path = "../../html" + std::string(request_.url);
      send_file_response(info, file_path);
      return;
    }
  } else if (request_.method == "POST") {
    response.response_line = "HTTP/1.1 200 OK\r\n";
    std::string body_str = "POST received";
    response.body.assign(body_str.begin(), body_str.end());
  } else {
    response.response_line = "HTTP/1.1 405 Method Not Allowed\r\n";
    std::string body_str = "Method Not Allowed";
    response.body.assign(body_str.begin(), body_str.end());
  }

  response.headers["Content-Type"] = "text/plain; charset=utf-8";
  response.headers["Content-Length"] = std::to_string(response.body.size());
  send_simple_response(info, response);
}
// 主处理函数
bool HttpTask::handle_message(UringConnectionInfo *info) {
  std::cout << "http处理主函数--------httpcpp" << std::endl;
  ParseResult result = info->parse_result;
  switch (result) {
  case ParseResult::CHUNKED_UNSUPPORTED: {
    // 不支持chunked编码
    info->parse_result = ParseResult::CHUNKED_UNSUPPORTED;
    HttpResponse chunked_response;
    chunked_response.response_line = "HTTP/1.1 501 Not Implemented\r\n";
    chunked_response.body = {'C', 'h', 'u', 'n', 'k', 'e', 'd', ' ', 'e', 'n',
                             'c', 'o', 'd', 'i', 'n', 'g', ' ', 'n', 'o', 't',
                             ' ', 's', 'u', 'p', 'p', 'o', 'r', 't', 'e', 'd'};
    chunked_response.headers["Content-Length"] =
        std::to_string(chunked_response.body.size());
    send_simple_response(info, chunked_response);
    return true;
    break;
  }
  case ParseResult::COMPLETE: {
    // 报文完整，开始解析
    std::string request_data;
    char *read_head = info->read_buffer.get_read_head();
    size_t read_size = info->read_buffer.get_readable_size();
    if (info->extra_buffer != nullptr) {
      // 拼接数据
      std::string_view second_data(info->extra_buffer, info->bytes_NO_read);
      request_data = std::string(read_head, read_size);
      request_data.append(second_data.begin(), second_data.end());
    } else {
      request_data = std::string(read_head, read_size);
    }
    // 解析请求行
    if (!parse_request_line(request_data)) {
      return false;
    }

    // 解析请求头
    if (!parse_request_headers(request_data)) {
      return false;
    }

    // 解析请求体
    if (!parse_request_body(request_data)) {
      return false;
    }
    handle_task(info);

    // 处理完成后，重置parse_result为需要更多数据，准备处理下一个请求
    info->parse_result = ParseResult::NEEED_MORE_DATA;

    // 释放extra_buffer这里只是将info的指针置空，实际内存由内存池管理
    if (info->extra_buffer != nullptr) {
      info->extra_buffer = nullptr;
      info->extra_buffer_in_use = false;
      info->bytes_NO_read = 0;
    }

    // 打印请求数据
    std::cout << "http____----request_data:" << request_data << std::endl;
    // 打印回应数据
    std::cout << "http____-----response_data:"
              << info->write_buffer.get_readable_size() << std::endl;
    // 处理完成后，从读缓冲区移除已处理的数据
    size_t total_processed =
        request_data.find("\r\n\r\n") + 4 + request_.content_length;
    info->read_buffer.read_data(total_processed);
    return true;
  }
  default:
    return false;
  }

  return false;
}

// 静态方法：判断是否为HTTP任务
bool HttpTask::is_http_task(UringConnectionInfo *info) {
  if (info->read_buffer.get_readable_size() < 4) {
    return false;
  }

  char *read_head = info->read_buffer.get_read_head();
  std::string_view data(
      read_head, std::min(size_t(10), info->read_buffer.get_readable_size()));

  // 检查是否为HTTP方法
  return (data.find("GET ") == 0 || data.find("POST ") == 0 ||
          data.find("PUT ") == 0 || data.find("DELETE ") == 0 ||
          data.find("HEAD ") == 0 || data.find("OPTIONS ") == 0);
}

// 静态方法：判断HTTP解析是否完整
bool HttpTask::is_http_parse_complete(UringConnectionInfo *info) {
  if (info->parse_result == ParseResult::COMPLETE) {
    return true;
  }
  ParseResult result = is_complete_message(info);
  return result == ParseResult::COMPLETE;
}

// 静态方法：处理HTTP请求（带回调版本）
static void handle_http_request_with_callback(
    UringConnectionInfo *info,
    std::function<void(UringConnectionInfo *)> callback) {

  // 创建HTTP任务实例
  HttpTask http_task;

  // 处理HTTP消息
  bool handled = http_task.handle_message(info);

  if (handled) {
    // 处理完成后，通过回调通知主线程
    if (callback) {
      callback(info);
    }
  } else {
    // 处理失败，也需要通知主线程继续读取
    if (callback) {
      callback(info);
    }
  }
}