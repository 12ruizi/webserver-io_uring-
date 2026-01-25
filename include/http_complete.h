#pragma once
#include "funC.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
//无状态设计
class HttpTask {
public:
  // 完整的HTTP报文完整性检查，返回报文长度（0表示不完整）
  static size_t is_http_parse_complete(Info_net_s *info) {
    size_t readable_size = info->rubffer.get_readable_size();
    if (readable_size == 0)
      return 0;

    char *read_ptr = info->rubffer.get_read_head();
    std::string_view buffer(read_ptr, readable_size);

    // 1. 检查请求头是否完整（以\r\n\r\n结尾）
    size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
      std::cout << "HTTP头不完整，需要继续接收数据" << std::endl;
      return 0;
    }
    std::string_view header(buffer.data(), header_end + 4);
    // 2. 查找Content-Length字段
    size_t content_length_pos = header.find("Content-Length:");
    if (content_length_pos != std::string::npos) {
      // 完整的Content-Length解析逻辑
      size_t start = content_length_pos + strlen("Content-Length:");
      size_t end = header.find("\r\n", start);
      if (end == std::string::npos) {
        std::cout << "Content-Length格式错误" << std::endl;
        return 0;
      }
      // 提取Content-Length值字符串
      std::string_view length_str =
          std::string_view(buffer.data() + start, end - start);

      // 去除前后空格
      while (!length_str.empty() && std::isspace(length_str.front())) {
        length_str.remove_prefix(1);
      }
      while (!length_str.empty() && std::isspace(length_str.back())) {
        length_str.remove_suffix(1);
      }
      if (length_str.empty()) {
        std::cout << "Content-Length值为空" << std::endl;
        return 0;
      }
      // 验证是否为纯数字
      if (!std::all_of(length_str.begin(), length_str.end(),
                       [](char c) { return std::isdigit(c); })) {
        std::cout << "Content-Length包含非数字字符: " << length_str
                  << std::endl;
        return 0;
      }

      try {
        size_t content_length = std::stol(std::string(length_str));
        size_t total_length = header_end + 4 + content_length;
        std::cout << "Content-Length: " << content_length
                  << ", 总长度: " << total_length
                  << ", 当前缓冲区: " << readable_size << std::endl;
        if (readable_size >= total_length) {
          std::cout << "HTTP报文完整，准备处理" << std::endl;
          return total_length;
        } else {
          std::cout << "HTTP报文不完整，需要继续接收数据" << std::endl;
          return 0;
        }
      } catch (const std::exception &e) {
        std::cout << "Content-Length解析异常: " << e.what() << std::endl;
        return 0;
      }
    }

    // 3. 处理没有Content-Length的情况（GET请求等）
    // 检查是否为chunked编码
    size_t transfer_encoding_pos = header.find("Transfer-Encoding:");
    if (transfer_encoding_pos != std::string::npos) {
      size_t start = transfer_encoding_pos + strlen("Transfer-Encoding:");
      size_t end = header.find("\r\n", start);
      if (end != std::string::npos) {
        std::string_view encoding_str =
            std::string_view(buffer.data() + start, end - start);
        // 去除空格并检查是否为chunked
        std::string encoding(encoding_str);
        encoding.erase(std::remove_if(encoding.begin(), encoding.end(),
                                      [](char c) { return std::isspace(c); }),
                       encoding.end());

        if (encoding.find("chunked") != std::string::npos) {
          std::cout << "检测到chunked编码，暂不支持" << std::endl;
          return 0; // 暂不支持chunked编码
        }
      }
    }

    // 4. 默认情况：只有请求头，没有请求体
    size_t message_length = header_end + 4;
    std::cout << "无请求体，报文长度: " << message_length << std::endl;
    return message_length;
  }

  // 解析HTTP方法
  static std::string parse_http_method(std::string_view buffer) {
    if (buffer.size() < 3)
      return "UNKNOWN";
    if (buffer.compare(0, 3, "GET") == 0)
      return "GET";
    if (buffer.compare(0, 4, "POST") == 0)
      return "POST";
    if (buffer.compare(0, 4, "PUT") == 0)
      return "PUT";
    if (buffer.compare(0, 6, "DELETE") == 0)
      return "DELETE";
    if (buffer.compare(0, 4, "HEAD") == 0)
      return "HEAD";

    return "UNKNOWN";
  }

  // 解析URL路径
  static std::string parse_url_path(std::string_view buffer) {
    size_t method_end = buffer.find(' ');
    if (method_end == std::string::npos)
      return "/";

    size_t url_start = method_end + 1;
    size_t url_end = buffer.find(' ', url_start);
    if (url_end == std::string::npos)
      return "/";
    std::string url =
        std::string(buffer.substr(url_start, url_end - url_start));
    // 简单的URL规范化
    if (url.empty() || url[0] != '/') {
      url = "/" + url;
    }

    return url;
  }

  // 解析HTTP版本
  static std::string parse_http_version(std::string_view buffer) {
    size_t version_start = buffer.find("HTTP/");
    if (version_start == std::string::npos)
      return "HTTP/1.0";
    size_t version_end = buffer.find("\r\n", version_start);
    if (version_end == std::string::npos)
      return "HTTP/1.0";
    return std::string(
        buffer.substr(version_start, version_end - version_start));
  }

  // 完整的响应发送函数
  static void
  send_response(Info_net_s *info, int status_code,
                std::string_view status_message, std::string_view response_body,
                std::string_view content_type = "text/html; charset=utf-8") {
    std::ostringstream response_stream;
    // 状态行
    response_stream << "HTTP/1.1 " << status_code << " " << status_message
                    << "\r\n";
    // 响应头
    response_stream << "Content-Type: " << content_type << "\r\n";
    response_stream << "Content-Length: " << response_body.size() << "\r\n";
    response_stream << "Connection: keep-alive\r\n";
    response_stream << "Server: IO_URING_Server/1.0\r\n";
    response_stream << "\r\n"; // 空行分隔头部和正文
    // 响应体
    response_stream << response_body;
    std::string response = response_stream.str();
    return send_data_in_chunks(info, response);
  }
  static bool send_data_in_chunks(Info_net_s *info, const std::string &data) {
    const size_t CHUNK_SIZE = 4096; // 4kb
    size_t total_sent = 0;
    size_t data_size = data.size();
    while (total_sent < data_size) {
      size_t chunk_size = std::min(CHUNK_SIZE, data_size - total_sent);
      size_t writable_size = info->wbuffer.get_writable_size(); //可写入大小
      if (writable_size >= chunk_size) {
        char *write_ptr = info->wbuffer.get_write_tail();
        memcpy(write_ptr, data.c_str() + total_sent, chunk_size);
        info->wbuffer.write_date(chunk_size);
        total_sent += chunk_size;
        std::cout << "发送数据块：" << chunk_size << "字节，累计："
                  << total_sent << "/" << data_size << std::endl;

      } else {

        //缓冲区不足，
        std::cout << "缓冲区不足，等待下次发送" << std::endl;
        return false; //需要继续发送
      }
    }
    std::cout << "数据发送完毕：" << total_sent << "字节" << std::endl;
    return true;
  }
  //使用sendfile的零拷贝文件传输
  static bool send_large_file_chunked(Info_net_s *info,
                                      const std::string &filename) {
    int file_fd = open(filename.c_str(), O_RDONLY);
    if (file_fd < 0) {
      std::cout << "打开文件失败：" << filename << std::endl;
      return false;
    }
    struct stat file_stat;
    if (fstat(file_fd, &file_stat) < 0) {
      std::cout << "获取文件状态失败：" << filename << std::endl;
      close(file_fd);
      return false;
    }
  }

  // 静态文件服务
  static bool serve_static_file(Info_net_s *info, const std::string &filename) {
    std::string filepath =
        "/home/rui/share/warehouse/1精匠/webserver/io_uring/html/" + filename;
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
      std::cout << "文件不存在: " << filepath << std::endl;
      return false;
    }

    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string file_content(file_size, '\0');
    if (!file.read(&file_content[0], file_size)) {
      std::cout << "文件读取失败: " << filepath << std::endl;
      return false;
    }
    file.close();

    // 根据文件扩展名设置Content-Type
    std::string content_type = "text/html; charset=utf-8";
    if (filename.find(".css") != std::string::npos) {
      content_type = "text/css";
    } else if (filename.find(".js") != std::string::npos) {
      content_type = "application/javascript";
    } else if (filename.find(".png") != std::string::npos) {
      content_type = "image/png";
    } else if (filename.find(".jpg") != std::string::npos) {
      content_type = "image/jpeg";
    }

    send_response(info, 200, "OK", file_content, content_type);
    return true;
  }

  static void process_get_request(Info_net_s *info, const std::string &url) {
    if (url == "/" || url == "/index.html") {
      std::string response_body =
          "<html><body>"
          "<h1>欢迎使用高性能IO_URING服务器</h1>"
          "<p>当前时间: " +
          get_current_time() +
          "</p>"
          "<p><a href=\"/list1.html\">查看示例页面</a></p>"
          "</body></html>";
      send_response(info, 200, "OK", response_body);
    } else {
      // 尝试提供静态文件
      std::string filename = url.substr(1); // 去除开头的'/'
      if (!serve_static_file(info, filename)) {
        send_response(info, 404, "Not Found",
                      "<html><body><h1>404 Not Found</h1></body></html>");
      }
    }
  }

  static void process_post_request(Info_net_s *info, const std::string &url) {
    // 简单的POST请求处理
    std::string response_body = "<html><body>"
                                "<h1>POST请求已接收</h1>"
                                "<p>服务器已成功处理您的POST请求</p>"
                                "<p>URL: " +
                                url +
                                "</p>"
                                "</body></html>";
    send_response(info, 200, "OK", response_body);
  }

  static std::string get_current_time() {
    // 简单的时间获取函数
    time_t now = time(nullptr);
    char buf[100];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf);
  }

public:
  // 主处理函数 - 改为静态
  static bool handle_http_request(Info_net_s *info) {
    std::cout << "开始处理HTTP请求..." << std::endl;

    // 1. 检查报文完整性
    size_t message_length = is_http_parse_complete(info);
    if (message_length == 0) {
      std::cout << "HTTP报文不完整,需要继续接收数据" << std::endl;
      return false;
    }
    // 2. 获取完整的报文数据
    char *read_ptr = info->rubffer.get_read_head();
    std::string_view http_message(read_ptr, message_length);
    // 3. 解析HTTP请求
    std::string method = parse_http_method(http_message);
    std::string url = parse_url_path(http_message);
    std::string version = parse_http_version(http_message);
    std::cout << "HTTP请求: " << method << " " << url << " " << version
              << std::endl;

    // 4. 根据方法类型处理
    if (method == "GET") {
      process_get_request(info, url);
    } else if (method == "POST") {
      process_post_request(info, url);
    } else {
      send_response(
          info, 405, "Method Not Allowed",
          "<html><body><h1>405 Method Not Allowed</httph1></body></html>");
    }
    // 5. 更新读缓冲区
    info->rubffer.read_date(message_length);
    std::cout << "HTTP请求处理完成,已消费" << message_length << "字节"
              << std::endl;

    return true;
  }
};