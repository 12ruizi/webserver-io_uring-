#include "funC.h"
#include <cstddef>
#include <string_view>
// http任务就是根据conn->rbuffer 解析http报文
// 1,判断报文是否完整，失败了标记位置直接返回，成功也要标记位置返回 2 ,分类报文
// (响应还是请求）3 处理报文 报文处理的话很困难，因为缓冲区原因
//可能会有多个报文在一个缓冲区中 也可能报文不完整 任务也不好判断
//,因为http是流式协议
//可能会有多个报文在一个缓冲区中,所以每次处理就只处理一个报文
//标记位置返回让外层继续处理 但是又要分情况讨论  1,报文完整
//标记位置，外层直接删除这个报文，调整缓冲区 填写读状态，发送读任务 2
// 报文不完整，标记位置，外层继续接收数据(调整缓冲区，填写读状态，发送读任务）)
//但是感觉这样处理很麻烦  直接在这里处理完一个报文，然后调整缓冲区不就行了吗？
//开始分析 这两种情况（哪一个速率快）假设要准备线程池的话，单生产者多消费者模型
//主线程负责io_uring 线程池负责处理任务就行 线程池处理任务  主线程负责io_uring
//这种情况需要把报文完整性判断和报文分类都放在线程池中处理？
//那么任务如何提交呢？
//直接提交一个rubffer就行？还有一个函数处理模块 ！！！！！！！！！！！！！！！
//进阶协程 ！！！！！！！！！！！！！！
//进阶缓冲区设置！！！！！！！！！！！！
//进阶 无锁队列！！！！！！！！！！！！！
//进阶  消息队列！！！！！！！！
//进阶 nginx 模块化设计！！！！！！！！
//进阶 日志模块  1 简单日志模块  2 异步日志模块
//进阶 连接池管理  1，新的连接 2 mysql 或者redis连接池 3 线程池管理
//进阶内存泄露检测  内存池  ，死锁检测
//进阶 测试模块  单元测试 模拟测试 压力测试
//重中之重dpdk 学习

// http任务类就是负责解析报文，完成报文内容，完成响应 发送请求
// 1，首先判断读的缓冲区是否完整，设置已经读取位置，（上一层io_uring_prep_read(),因为他是拷贝缓冲区（需要设置每次读取的最大长度）
//                                              （但是由于请求可能不完全，所以要标记，prep的时候对string进行扩容，完成的时候对他进行调整大小为正确大小）
// 2，请求报文：设置请求报文解析器：这里只是简单判断他是get还是post类型
//      响应报文：设置响应报文解析器:

//响应报文结构体
typedef struct {
  // http响应状态行
  std::string status_line;
  // http响应头
  std::string headers;
  // http响应体
  std::string body;

} response_t;

class Http_task {
private:
  size_t message_length; //单个报文的总长度 包括请求头和请求体
  //直接判断报文是否完善 不管他是请求报文还是响应报文
  bool is_http_parse_complete(const std::string &buffer) {
    // 1.检查是否包含完整的请求行和头部（以\r\n\r\n结尾）
    size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
      std::cout << "请求头不完整" << std::endl;
      //!!!!
      return false; //请求头不完整
    }
    std::string_view header(buffer.data(), header_end + 4); //包含\r\n\r\n
    // 2.查找content-length字段
    size_t content_length_pos = header.find(
        "Content-Length:"); //查找content-length字段 pos是字母C的位置
    if (content_length_pos !=
        std::string::npos) // nops没有找到string返回的是npos
    {
      //找到了content-length字段 说明有请求体 开始解析 content-length的值
      size_t start =
          content_length_pos + strlen("Content-Length:"); //值的开始位置
      size_t end = header.find("\r\n", start);            //值的结束位置
      std::string_view length_str =
          std::string_view(buffer.data() + start, end - start);

      while (!length_str.empty() && isspace(length_str.front())) { //去除空格
        length_str.remove_prefix(1);
      }

      try { //如果有length字段 判断请求体是否完整 是否多余
        size_t content_length =
            std::stoul(std::string(length_str)); //转换为数字
        //计算请求体的大小；
        size_t total_length = header_end + 4 + content_length;
        if (buffer.size() == total_length) {
          std::cout << "请求体完整" << std::endl;
          message_length = total_length;
          return true; //请求体完整
        } else if (buffer.size() > total_length) {
          std::cout << "请求体完整但是多余数据" << std::endl;
          message_length = total_length;
          return true; //请求体完整 但是多余数据
        } else {
          std::cout << "请求体不完整" << std::endl;
          return false; //请求体不完整
        }
      } catch (...) {
        //解析失败
        return false;
      }
    }

    //没有请求体 只有请求头，就可能是get请求或者chunked编码
    //对于get请求，头部结束就是报文结束
    //检测是否为get请求

    std::cout << "没有请求体 请求头完整" << std::endl;
    message_length = header_end + 4;
    return true;
  }

  void send_response(Info_net_s *info, int status_code,
                     std::string_view status_message,
                     std::string_view response_body) {
    std::ostringstream response_stream;
    response_stream << "HTTP/1.1 " << status_code << " " << status_message
                    << "\r\n";
    response_stream << "Content-Length: " << response_body.size() << "\r\n";
    response_stream << "Connection: keep-alive\r\n";
    response_stream << "\r\n";
    response_stream << response_body;
    std::string response = response_stream.str();
  }
  //静态文件服务处理器
  bool server_static_file(Info_net_s *info, std::string &filename) {
    std::string filepath = "./html/" + filename;
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
      std::cout << "打开文件失败" << std::endl;
      return false;
    }
    //读取文件内容
    std::stringstream buffer;
    buffer << file.rdbuf(); //将文件内容读取到buffer中
    std::string file_content = buffer.str();
    //关闭文件
    file.close();
    send_response(info, 200, "OK", file_content);
  }

public:
  Http_task() : message_length(0) {}
  //处理http请求 ,这里需要解析http报文 ，然后根据报文内容处理不同的请求
  ////应该先判断请求是否完整//再解析是get还是post
  void _handle_http(Info_net_s *info) {
    std::cout << "开始处理HTTP报文" << std::endl;
    //判断请求是否完整  这里只是简单判断有没有\r\n\r\n
    bool ret = is_http_parse_complete(info->rbuffer);
    if (ret) {
      std::cout << "开始分类报文" << std::endl;
      classfi_http_message(info);
    } else {
      return; //继续接收数据
    }
  }

  //解析报文 有报文总长度  报文分类
  void classfi_http_message(Info_net_s *info) {
    if (message_length == 0) {
      std::cout << "报文长度为0，无法解析，判断出错" << std::endl;
      return;
    }
    //判断报文是请求报文还是响应报文
    std::string_view buffer(info->rbuffer.data(), message_length);
    if (buffer.find("HTTP/", 0, 5) == 0) //响应报文
    {
      std::cout << "解析到HTTP响应报文，当前只处理请求报文，忽略响应报文"
                << std::endl;
      //打印响应报文
      std::cout << "响应报文内容：" << buffer << std::endl;

      return;
    } else //请求报文
    {
      std::cout << "解析到HTTP请求报文，开始处理请求" << std::endl;
      //处理请求
      process_http_request(info, buffer);
    }
  }
  //处理请求的报文体。
  void process_http_request(Info_net_s *info, std::string_view &buffer) {
    //根据请求头部进行不同处理
    if (buffer.find("GET ") == 0) { //处理GET请求
      std::cout << "处理GET请求" << std::endl;
      size_t url_start = buffer.find(" ") + 1;
      size_t url_end = buffer.find(" ", url_start);
      std::string url =
          std::string(buffer.substr(url_start, url_end - url_start));
      std::cout << "请求的URL:" << url << std::endl;
      if (url == "/") { //生成响应报文 回复一个html文件  //根据url返回不同资源
        std::string response_body =
            "<html><body><h1>Welcome to the Home Page</h1></body></html>";
        send_response(info, 200, "OK", response_body);
        //删除已处理的请求报文，放到send_response中
      } else if (url == "/list1.html") {
        //返回list1.html文件
        std::string path = url.substr(1);
        if (server_static_file(info, path)) {
          //文件服务成功
        } else {
          //文件不存在
          std::string response_body =
              "<html><body><h1>404 Not Found</h1><p>File " + url +
              " not found</p></body></html>";
          send_response(info, 404, "Not Found", response_body);
        }
      } else {
        //返回404 not found
        std::string response_body =
            "<html><body><h1>404 Not Found</h1></body></html>";
        std::ostringstream response_stream;
        response_stream << "HTTP/1.1 404 Not Found\r\n";
        response_stream << "Content-Length: " << response_body.size() << "\r\n";
        response_stream << "Content-Type: text/html\r\n";
        response_stream << "Connection: close\r\n";
        response_stream << "\r\n";
        response_stream << response_body;
        std::string response = response_stream.str();
        info->wbuffer = response;
        info->rbuffer.erase(0, message_length);
      }
    } else if (buffer.find("POST ") ==
               0) { //这里只需要简单回复一个确认收到POST请求
      std::cout << "处理POST请求" << std::endl;
      std::string response_body =
          "<html><body><h1>POST request received</h1></body></html>";
      send_response(info, 200, "OK", response_body);

    } else { //回复一段文字
      std::cout << "未知请求方法，当前只处理GET和POST请求" << std::endl;
      std::string response_body =
          "<html><body><h1>Unknown request method</h1></body></html>";
      send_response(info, 400, "error", response_body);
      return;
    }
  }
};